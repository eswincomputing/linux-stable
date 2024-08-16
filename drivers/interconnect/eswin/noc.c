// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN Noc Driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: HuangYiFeng<huangyifeng@eswincomputing.com>
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>

#include "noc.h"
#include "noc_regs.h"

//#pragma GCC optimize("O0")

#define XGOLD632_NOC_ERROR_ID 0x011B0B00
#define XGOLD631_NOC_ERROR_ID 0x00FA1200
#define XGOLD726_NOC_ERROR_ID 0x010C0C00

struct win2030_noc_control win2030_noc_ctrl;

/**
 * Tasklet called by interrupt when a measurement is finished.
 * @param data a pointer to the struct win2030_noc_probe which generated the
 * interrupt
 */
void win2030_noc_stat_measure_tasklet(unsigned long data)
{
	struct win2030_noc_probe *probe = (struct win2030_noc_probe *)data;

	if (probe_t_trans == probe->type) {
		win2030_noc_prof_do_measure(probe);
	} else {
		win2030_noc_stat_do_measure(probe);
	}
}

/**
 *
 * @param _dev
 * @param np
 * @param property_name
 * @return
 */
const char **win2030_get_strings_from_dts(struct device *_dev,
					       struct device_node *np,
					       const char *property_name)
{
	struct property *prop;
	const char *s;
	const char **lut = NULL;
	int i = 0, lut_len;

	if (of_property_read_bool(np, property_name)) {
		lut_len = of_property_count_strings(np, property_name);

		if (lut_len < 0) {
			dev_err(_dev, "Invalid description of %s for %s\n",
				property_name, np->name);
			return ERR_PTR(lut_len);
		}

		lut = kcalloc(lut_len + 1, sizeof(char *), GFP_KERNEL);
		if (!lut)
			return ERR_PTR(-ENOMEM);

		of_property_for_each_string(np, property_name, prop, s)
			lut[i++] = s;

		/* To find out the end of the list */
		lut[lut_len] = NULL;
	}

	return lut;
}

static struct win2030_bitfield *win2030_alloc_bitfield(void)
{
	struct win2030_bitfield *bitfield;

	bitfield = kzalloc(sizeof(*bitfield), GFP_KERNEL);
	if (!bitfield)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&bitfield->lock);
	INIT_LIST_HEAD(&bitfield->link);
	return bitfield;
}

struct win2030_bitfield *win2030_new_bitfield(struct win2030_register *reg,
						 const char *name,
						 unsigned offset,
						 unsigned width,
						 const char **lut)
{
	struct win2030_bitfield *bf;
	unsigned long flags;

	if (!reg)
		return ERR_PTR(-EINVAL);

	bf = win2030_alloc_bitfield();
	if (!bf)
		return ERR_PTR(-ENOMEM);

	bf->parent = reg;
	bf->name = kstrdup(name, GFP_KERNEL);
	bf->offset = offset;
	bf->length = width;
	bf->mask = ((BIT(width) - 1) << offset);
	bf->lut = lut;

	spin_lock_irqsave(&reg->lock, flags);
	list_add_tail(&bf->link, &reg->bitfields);
	spin_unlock_irqrestore(&reg->lock, flags);

	return bf;
}

static void win2030_free_bitfield(struct win2030_bitfield *bf)
{
	kfree(bf->name);
	kfree(bf);
}

static struct win2030_bitfield *win2030_get_bitfield(struct device *_dev,
						 struct device_node *np)
{
	int ret, i;
	unsigned values[2];
	struct win2030_bitfield *bitfield;
	u64 *values_tab;

	if (!np->name)
		return ERR_PTR(-EINVAL);

	bitfield = win2030_alloc_bitfield();
	if (!bitfield)
		return ERR_PTR(-ENOMEM);

	bitfield->name = np->name;

	ret = of_property_read_u32_array(np, "offset,length", values, 2);
	if (ret) {
		dev_err(_dev,
			"\"Offset,length\" properties of register %s\n missing",
			bitfield->name);
		ret = -EINVAL;
		goto free_bf;
	}

	bitfield->offset = values[0];
	bitfield->length = values[1];
	bitfield->mask = (((1UL << bitfield->length) - 1) << bitfield->offset);

	if ((bitfield->offset > 31) || (bitfield->length > 32)) {
		ret = -EINVAL;
		goto free_bf;
	}

	of_property_read_string(np, "description", &bitfield->description);

	bitfield->lut = win2030_get_strings_from_dts(_dev, np, "lut");
	if (IS_ERR(bitfield->lut)) {
		ret = PTR_ERR(bitfield->lut);
		dev_err(_dev, "Error %d while parsing lut of %s\n", ret,
			bitfield->name);
		bitfield->lut = NULL;
	}
	bitfield->aperture_size = of_property_count_elems_of_size(np,
			"aperture-idx,aperture-base", sizeof(u64) * 4);
	if (bitfield->aperture_size > 0) {
		bitfield->init_flow = devm_kcalloc(_dev, bitfield->aperture_size * 4,
						sizeof(u64), GFP_KERNEL);
		if (!bitfield->init_flow) {
			ret = -ENOMEM;
			goto free_all;
		}
		bitfield->target_flow = bitfield->init_flow + bitfield->aperture_size;
		bitfield->target_sub_range = bitfield->target_flow + bitfield->aperture_size;
		bitfield->aperture_base = bitfield->target_sub_range + bitfield->aperture_size;

		values_tab = devm_kcalloc(_dev, bitfield->aperture_size * 4,
				sizeof(*values_tab), GFP_KERNEL);
		if (!values_tab) {
			ret = -ENOMEM;
			goto free_all;
		}
		ret = of_property_read_u64_array(np, "aperture-idx,aperture-base",
				values_tab, bitfield->aperture_size * 4);
		if (ret) {
			dev_err(_dev,
				"Error while parsing aperture of %s bitfield, ret %d!\n",
				bitfield->name, ret);
			goto free_all;
		}
		for (i = 0; i < bitfield->aperture_size; i++) {
			bitfield->init_flow[i] = values_tab[4 * i];
			bitfield->target_flow[i] = values_tab[4 * i + 1];
			bitfield->target_sub_range[i] = values_tab[4 * i + 2];
			bitfield->aperture_base[i] = values_tab[4 * i + 3];
		}
		devm_kfree(_dev, values_tab);
	}

	dev_dbg(_dev, "Bitfield %s, offset %x, length %d created\n",
		bitfield->name, bitfield->offset, bitfield->length);

	return bitfield;

free_all:
	kfree(bitfield->lut);
free_bf:
	kfree(bitfield);
	return ERR_PTR(ret);
}

static struct win2030_register *win2030_alloc_register(void)
{
	struct win2030_register *reg;

	reg = kzalloc(sizeof(*reg), GFP_KERNEL);
	if (!reg)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&reg->lock);
	spin_lock_init(&reg->hw_lock);
	INIT_LIST_HEAD(&reg->link);
	INIT_LIST_HEAD(&reg->bitfields);

	return reg;
}

struct win2030_register *win2030_new_register(struct device *parent,
				 const char *name,
				 void __iomem *base,
				 unsigned offset,
				 unsigned width)
{
	struct win2030_register *reg;

	reg = win2030_alloc_register();
	if (!reg)
		return ERR_PTR(-ENOMEM);

	reg->parent = parent;
	reg->name = kstrdup(name, GFP_KERNEL);
	reg->base = base;
	reg->offset = offset;
	reg->length = width;

	return reg;
}

static void win2030_free_register(struct win2030_register *reg)
{
	struct win2030_bitfield *bf, *tmp;

	list_for_each_entry_safe(bf, tmp, &reg->bitfields, link)
		win2030_free_bitfield(bf);

	kfree(reg->name);
	kfree(reg);
}

/**
 * Create ErrorLog register from dts file
 * @param _dev
 * @param np
 * @return
 */
static struct win2030_register *win2030_get_err_register(struct device *_dev,
						 struct device_node *np)
{
	int ret;
	struct device_node *child = NULL;
	unsigned values[2];
	struct win2030_register *reg;

	if (!np->name)
		return ERR_PTR(-EINVAL);

	reg = win2030_alloc_register();
	if (!reg)
		return ERR_PTR(-ENOMEM);

	reg->name = np->name;
	reg->parent = _dev;
	reg->base = of_iomap(_dev->of_node, 0);

	ret = of_property_read_u32_array(np, "offset,length", values, 2);
	if (ret) {
		dev_err(_dev,
			"\"Offset,length\" properties of register %s\n missing",
			reg->name);
		ret = -EINVAL;
		goto free_reg;
	}

	reg->offset = values[0];
	reg->length = values[1];
	if (reg->length > 32) {
		ret = -EINVAL;
		goto free_reg;
	}

	/* Not mandatory properties  */
	of_property_read_string(np, "description", &reg->description);

	ret = of_property_read_u32(np, "aperture-link", values);
	if (!ret)
		reg->aperture_link = values[0];
	else
		reg->aperture_link = -1;

	ret = of_property_read_u32(np, "msb-link", values);
	if (!ret)
		reg->msb_link = values[0];
	else
		reg->msb_link = -1;

	/* Create bitfields */
	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, "eswin,win2030,bitfield")) {
			struct win2030_bitfield *bitfield;
			bitfield = win2030_get_bitfield(_dev, child);
			if (!(IS_ERR(bitfield))) {
				list_add_tail(&bitfield->link, &reg->bitfields);
				bitfield->parent = reg;
			}
		}
	}
	dev_dbg(_dev, "Register %s, offset %x, length %d created\n", reg->name,
		reg->offset, reg->length);
	return reg;

free_reg:
	kfree(reg);
	return ERR_PTR(ret);
}

int win2030_find_register_by_name(
	const struct win2030_noc_device *noc_device,
	const char *name, struct win2030_register **reg)
{
	struct win2030_register *tmp_reg = NULL;

	list_for_each_entry(tmp_reg, &noc_device->registers, link) {
		if (!strcmp(name, tmp_reg->name)) {
			*reg = tmp_reg;
			return 0;
		}
	}
	return -EINVAL;
}
#if 0
int win2030_find_routeid_idx_by_name(struct device *_dev,
	const char *sub_route_id, const char *name, int *idx)
{
	int ret;
	struct win2030_register *reg;
	struct win2030_bitfield *bf;
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);

	ret = win2030_find_register_by_name(noc_device, "ErrorLogger1", &reg);
	if (0 != ret) {
		dev_err(_dev, "%s register not found!\n", "ErrorLogger1");
		return -EINVAL;
	}
	list_for_each_entry(bf, &reg->bitfields, link) {
		if (strcmp(bf->name, sub_route_id) == 0) {
			ret = xgold_bitfield_get_value_from_enum(bf, name, idx);
			if (0 == ret) {
				break;
			}
		}
	}
	return ret;
}
#endif

/**
 * Create ErrorLogx register set from DTS file
 * @param _dev
 * @param regs
 * @return
 */
static int win2030_get_err_registers(struct device *_dev, struct list_head *regs)
{
	struct device_node *parent = _dev->of_node;
	struct device_node *np = NULL;
	struct win2030_register *reg;

	for_each_child_of_node(parent, np) {
		if (of_device_is_compatible(np, "eswin,win2030,register")) {
			reg = win2030_get_err_register(_dev, np);
			if (!(IS_ERR(reg)))
				list_add_tail(&reg->link, regs);
		}
	}

	return 0;
}

static const char *noc_cnt_src_evt_lut[] = {
	"off",
	"cycle",
	"idle",
	"xfer",
	"busy",
	"wait",
	"pkt",
	"lut",
	"byte",
	"press0",
	"disabled",
	"disabled",
	"filt0",
	"filt1",
	"filt2",
	"filt3",
	"chain",
	"lut_byte_en",
	"lut_byte",
	"filt_byte_en",
	"filt_byte",
	"disabled",
	"disabled",
	"disabled",
	"disabled",
	"disabled",
	"disabled",
	"disabled",
	"disabled",
	"disabled",
	"disabled",
	"disabled",
	NULL,
};

static const char *noc_cnt_alarm_mode_lut[] = {
	"off",
	"min",
	"max",
	"min_max",
	NULL
};

static const char *win2030_noc_enable_enum[] = {
	"disable",
	"enable",
	NULL,
};

static const char *win2030_noc_on_off_enum[] = {
	"off",
	"on",
	NULL
};

static const char *win2030_noc_qos_mode_enum[] = {
	"fixed",
	"limiter",
	"bypass",
	"regulator",
	NULL
};

static struct of_device_id win2030_noc_of_match[] = {
	{
	 .compatible = "eswin,win2030-noc",
	},
	{},
};

#include <linux/sched.h>
#include <linux/sched/debug.h>
void stack_dump(void)
{
	struct task_struct *task_list = NULL;

	for_each_process(task_list) {
		show_stack(task_list, NULL, KERN_EMERG);
	}
}
static int win2030_noc_get_error(struct win2030_noc_device *noc_device)
{
	void __iomem *base = noc_device->hw_base;
	struct win2030_noc_error *noc_err;
	unsigned long flags;
	int i;
	char buf[1024] = {'\0'};

	struct device *mydev = noc_device->dev;

	noc_err = kzalloc(sizeof(struct win2030_noc_error), GFP_ATOMIC);
	if (!noc_err)
		return -ENOMEM;

	noc_err->timestamp = get_jiffies_64();
	dev_err(mydev, "Error interrupt happen!\n");
	for (i = 0; i < noc_device->error_logger_cnt; i++) {
		noc_err->err[noc_device->err_log_lut[i]] = ioread32(ERRLOG_0_ERRLOG0(base) +
					noc_device->err_log_lut[i] * sizeof(u32));
		dev_err(mydev, "ErrLog%d 0x%08x", noc_device->err_log_lut[i],
			noc_err->err[noc_device->err_log_lut[i]]);
	}
	spin_lock_irqsave(&noc_device->lock, flags);
	list_add_tail(&noc_err->link, &noc_device->err_queue);
	spin_unlock_irqrestore(&noc_device->lock, flags);

	noc_error_dump(buf, noc_device, noc_err);
	dev_err(mydev, "%s\n", buf);
	//stack_dump();
	iowrite32(1, ERRLOG_0_ERRCLR(base));
	return 0;
}

static irqreturn_t win2030_noc_error_irq(int irq, void *dev)
{
	struct win2030_noc_device *noc_device = dev;
	void __iomem *base = noc_device->hw_base;

	while (ioread32(ERRLOG_0_ERRVLD(base)))
		win2030_noc_get_error(noc_device);

	BUG_ON(noc_device->trap_on_error == true);

	return IRQ_HANDLED;
}

void zebu_stop(void)
{
	void __iomem *base = ioremap(0x51810000, 0x1000);
	printk("%s %d\n",__func__,__LINE__);
	writel_relaxed(0x8000, base + 0x668);
}

/**
 * Copy ErrorLog registers into error_registers field + Enable error interrupt
 * @param _dev
 * @return
 */
static int win2030_noc_error_init(struct device *_dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	char reg_name[16];
	struct win2030_register *reg;
	int i;
	int ret;
	int cnt;
	u32 *err_log_lut = NULL;
	struct platform_device *pdev = to_platform_device(_dev);
	struct device_node *np = _dev->of_node;

	cnt = of_property_count_u32_elems(np, "errlogger,idx");
	if (cnt <= 0) {
		return cnt;
	}
	noc_device->error_logger_cnt = cnt;
	err_log_lut = devm_kcalloc(_dev, cnt, sizeof(u32), GFP_KERNEL);
	if (!err_log_lut) {
		return -ENOMEM;
	}
	ret = of_property_read_u32_array(np, "errlogger,idx", err_log_lut, cnt);
	if (ret) {
		dev_err(_dev, "Error while parsing errlogger,idx, ret %d!\n", ret);
		return ret;
	}
	noc_device->err_log_lut = err_log_lut;

	for (i = 0; i < noc_device->error_logger_cnt; i++) {
		scnprintf(reg_name, ARRAY_SIZE(reg_name), "ErrorLogger%d",
			err_log_lut[i]);
		ret = win2030_find_register_by_name(noc_device, reg_name, &reg);
		if (0 != ret) {
			dev_err(_dev, "%s register not found!\n", reg_name);
			return -EINVAL;
		}
		noc_device->error_registers[noc_device->err_log_lut[i]] = reg;
	}
	noc_device->err_irq = platform_get_irq_byname(pdev, "error");
	if (noc_device->err_irq <= 0) {
		dev_warn(_dev, "error irq not defined!\n");
		ret = noc_device->err_irq;
		return ret;
	} else {
		ret = devm_request_irq(_dev, noc_device->err_irq, win2030_noc_error_irq,
				IRQF_SHARED, dev_name(_dev), noc_device);
		if (ret) {
			dev_err(_dev, "Error %d while installing error irq\n",
					(int)noc_device->err_irq);
			return ret;
		}
	}
	dev_info(_dev, "enable error logging\n");
	iowrite32(1, ERRLOG_0_FAULTEN(noc_device->hw_base));
	return 0;
}

static struct win2030_bitfield *win2030_noc_create_register_and_bitfield(
		struct win2030_noc_probe *probe,
		const char *name,
		void *hw_base,
		unsigned reg_offset,
		unsigned bf_offset,
		unsigned bf_width,
		const char **bf_lut)
{
	struct win2030_register *reg;

	if (probe == NULL)
		return ERR_PTR(-EINVAL);

	reg = win2030_new_register(probe->dev, name, hw_base, reg_offset, 32);
	if (IS_ERR_OR_NULL(reg))
		return ERR_PTR(-EINVAL);

	return win2030_new_bitfield(reg, name, bf_offset, bf_width, bf_lut);
}

static irqreturn_t win2030_noc_stat_irq(int irq, void *dev)
{
	unsigned status;
	struct win2030_noc_probe *probe = dev;
	int ret = 0;

	status = ioread32(probe->hw_base + PROBE_STATALARMSTATUS);
	dev_dbg_once(probe->dev, "counter irq occur, status %d\n", status);
	if (status) {
		/* Disable alarm and statics*/
		ret |= win2030_noc_reg_write(probe->main_ctl, PROBE_MAINCTL_ALARMEN_MASK,
					   PROBE_MAINCTL_ALARMEN_OFFSET, 0);

		ret |= win2030_noc_reg_write(probe->main_ctl, PROBE_MAINCTL_STATEN_MASK,
				   PROBE_MAINCTL_STATEN_OFFSET, 0);
		if (ret < 0) {
			dev_err(probe->dev, "%s failed to disable alarm and "
				"statics, ret %d", probe->id, ret);
		}
		//iowrite32(0, probe->hw_base + PROBE_MAINCTL);

		/* Clear alarm */
		iowrite32(1, probe->hw_base + PROBE_STATALARMCLR);
		tasklet_schedule(&probe->tasklet);
	}
	return IRQ_HANDLED;
}

/**
 * Create a counter register set (includes PortSel, AlarmMode, Src, Val)
 * @param cnt
 * @param probe
 * @param cnt_id
 * @return
 */
static int win2030_noc_init_counter(struct win2030_noc_counter *cnt,
				  struct win2030_noc_probe *probe,
				  unsigned cnt_id)
{
	struct win2030_bitfield *bf;

	if ((cnt == NULL) || (probe == NULL))
		return -EINVAL;

	cnt->id = cnt_id;
	cnt->parent = probe;
	INIT_LIST_HEAD(&cnt->register_link);
#if 0
	/* Port Selection */
	bf = win2030_noc_create_register_and_bitfield(probe, "PortSel",
			    PROBE_COUNTERS_PORTSEL(cnt->id),
			    PROBE_COUNTERS_PORTSEL_COUNTERS_PORTSEL_OFFSET,
			    PROBE_COUNTERS_PORTSEL_COUNTERS_PORTSEL_WIDTH,
			    probe->available_portsel);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	cnt->portsel = bf;
#endif
	/* AlarmMode */
	bf = win2030_noc_create_register_and_bitfield(probe, "AlarmMode",
			    probe->hw_base,
			    PROBE_COUNTERS_ALARMMODE(cnt->id),
			    PROBE_COUNTERS_ALARMMODE_COUNTERS_ALARMMODE_OFFSET,
			    PROBE_COUNTERS_ALARMMODE_COUNTERS_ALARMMODE_WIDTH,
			    noc_cnt_alarm_mode_lut);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	cnt->alarm_mode = bf;
	list_add_tail(&bf->parent->parent_link, &cnt->register_link);

	/* Src */
	bf = win2030_noc_create_register_and_bitfield(probe, "Src",
				    probe->hw_base,
				    PROBE_COUNTERS_SRC(cnt->id),
				    PROBE_COUNTERS_SRC_INTEVENT_OFFSET,
				    PROBE_COUNTERS_SRC_INTEVENT_WIDTH,
				    noc_cnt_src_evt_lut);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	cnt->source_event = bf;
	list_add_tail(&bf->parent->parent_link, &cnt->register_link);

	/* Val */
	bf = win2030_noc_create_register_and_bitfield(probe, "Val",
				    probe->hw_base,
				    PROBE_COUNTERS_VAL(cnt->id),
				    PROBE_COUNTERS_VAL_COUNTERS_VAL_OFFSET,
				    PROBE_COUNTERS_VAL_COUNTERS_VAL_WIDTH,
				    NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	cnt->value = bf;
	list_add_tail(&bf->parent->parent_link, &cnt->register_link);

	return 0;
}

static struct win2030_register *win2030_noc_get_compatible_register(struct
								win2030_noc_device
								*noc_device,
								const char
								*compatible)
{
	struct device *_dev = noc_device->dev;
	struct device_node *parent = _dev->of_node;
	struct device_node *np = NULL;
	struct win2030_register *reg = NULL;
	int ret;

	for_each_child_of_node(parent, np) {
		if (of_device_is_compatible(np, compatible)) {
			list_for_each_entry(reg, &noc_device->registers, link) {
				ret = strcmp(np->name, reg->name);
				if (ret == 0)
					return reg;
			}
		}
	}

	return NULL;
}

/**
 * Create a filter register set (includes RouteIdBase, RouteIdMask, AddrBase_Low,
 *    WindowSize, SecurityBase, SecurityMask, Opcode, Status, Length, Urgency)
 * @param noc_device
 * @param filter
 * @param probe
 * @param id
 * @return
 */
static int win2030_noc_init_filter(struct win2030_noc_device *noc_device,
				 struct win2030_noc_filter *filter,
				 struct win2030_noc_probe *probe, unsigned id)
{
	struct win2030_bitfield *bf, *bf_template = NULL;
	struct win2030_register *reg, *reg_template;
	unsigned route_idmask_width;

	if ((noc_device == NULL) || (filter == NULL) || (probe == NULL))
		return -EINVAL;

	filter->id = id;
	filter->parent = probe;
	INIT_LIST_HEAD(&filter->register_link);

	/*"RouteIdBase"*/
	reg_template = win2030_noc_get_compatible_register(noc_device,
				"eswin,win2030,noc,filter,routeid");
	if (!reg_template)
		return -ENODEV;

	reg = win2030_new_register(probe->dev, "RouteIdBase", probe->hw_base,
				 PROBE_FILTERS_ROUTEIDBASE(id), 32);

	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	route_idmask_width = 0;
	list_for_each_entry(bf_template, &reg_template->bitfields, link) {
		bf = win2030_new_bitfield(reg, bf_template->name,
					bf_template->offset,
					bf_template->length, bf_template->lut);
		if (IS_ERR_OR_NULL(bf))
			return -EINVAL;
		if ((bf_template->offset + bf_template->length)
			> route_idmask_width)
				route_idmask_width = (bf_template->offset
					+ bf_template->length);
	}
	filter->route_id_base = reg;
	list_add_tail(&reg->parent_link, &filter->register_link);

	/* RouteIdMask*/
	bf = win2030_noc_create_register_and_bitfield(probe, "RouteIdMask",
				probe->hw_base,
				PROBE_FILTERS_ROUTEIDMASK(id), 0,
				route_idmask_width, NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->route_id_mask = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	/* AddressBase_Low*/
	bf = win2030_noc_create_register_and_bitfield(probe, "AddressBase_Low",
			probe->hw_base,
			PROBE_FILTERS_ADDRBASE_LOW(id),
			PROBE_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_OFFSET,
			PROBE_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_WIDTH,
			NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->addr_base_low = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	/* AddressBase_High*/
	bf = win2030_noc_create_register_and_bitfield(probe, "AddressBase_High",
			probe->hw_base,
			PROBE_FILTERS_ADDRBASE_HIGH(id),
			PROBE_FILTERS_ADDRBASE_HIGH_FILTERS_ADDRBASE_HIGH_OFFSET,
			PROBE_FILTERS_ADDRBASE_HIGH_FILTERS_ADDRBASE_HIGH_WIDTH,
			NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->addr_base_high = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	/* Window Size */
	bf = win2030_noc_create_register_and_bitfield(probe, "WindowSize",
			probe->hw_base,
			PROBE_FILTERS_WINDOWSIZE(id),
			PROBE_FILTERS_WINDOWSIZE_FILTERS_WINDOWSIZE_OFFSET,
			PROBE_FILTERS_WINDOWSIZE_FILTERS_WINDOWSIZE_WIDTH,
			NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->window_size = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

#if 0
	/*no security deployment*/

	unsigned security_mask_width;

	/* Security Base */
	reg_template = win2030_noc_get_compatible_register(noc_device,
					"eswin,win2030,noc,filter,security");
	if (!reg_template)
		return -ENODEV;

	scnprintf(_name, ARRAY_SIZE(_name), "probe%d_filter%d_security_base",
		  probe->id, id);

	reg = win2030_new_register(noc_device->dev, _name, noc_device->hw_base,
				 PROBE_FILTERS_SECURITYBASE(0, probe->id, id), 32);

	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	filter->security_base = reg;
	security_mask_width = 0;
	list_for_each_entry(bf_template, &reg_template->bitfields, link) {
		bf = win2030_new_bitfield(reg, bf_template->name,
					bf_template->offset,
					bf_template->length, bf_template->lut);
		if (IS_ERR_OR_NULL(bf))
			return -EINVAL;
		if ((bf_template->offset + bf_template->length)
			> security_mask_width)
				security_mask_width = (bf_template->offset
					+ bf_template->length);
	}

	/* Security Mask */
	scnprintf(_name, ARRAY_SIZE(_name), "probe%d_filter%d_security_mask",
		  probe->id, id);

	bf = win2030_noc_create_register_and_bitfield(probe, _name,
				PROBE_FILTERS_SECURITYMASK
				(0,
				probe->id, id), 0,
				security_mask_width, NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->security_mask = bf;
#endif
	/* Opcode*/
	reg = win2030_new_register(probe->dev, "Opcode",
			probe->hw_base,
			PROBE_FILTERS_OPCODE(id), 32);

	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "RdEn",
				PROBE_FILTERS_OPCODE_RDEN_OFFSET,
				PROBE_FILTERS_OPCODE_RDEN_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "WrEn",
				PROBE_FILTERS_OPCODE_WREN_OFFSET,
				PROBE_FILTERS_OPCODE_WREN_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "LockEn",
				PROBE_FILTERS_OPCODE_LOCKEN_OFFSET,
				PROBE_FILTERS_OPCODE_LOCKEN_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "UrgEn",
				PROBE_FILTERS_OPCODE_URGEN_OFFSET,
				PROBE_FILTERS_OPCODE_URGEN_WIDTH,
				win2030_noc_enable_enum);

	filter->op_code = reg;
	list_add_tail(&reg->parent_link, &filter->register_link);

	/* Status */
	reg = win2030_new_register(probe->dev, "Status", probe->hw_base,
				 PROBE_FILTERS_STATUS(id), 32);

	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "status_request_enable",
				PROBE_FILTERS_STATUS_REQEN_OFFSET,
				PROBE_FILTERS_STATUS_REQEN_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "status_response_enable",
				PROBE_FILTERS_STATUS_RSPEN_OFFSET,
				PROBE_FILTERS_STATUS_RSPEN_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->status = reg;
	list_add_tail(&reg->parent_link, &filter->register_link);

	/* Length */
	bf = win2030_noc_create_register_and_bitfield(probe, "Length",
				probe->hw_base,
				PROBE_FILTERS_LENGTH(id),
				PROBE_FILTERS_LENGTH_FILTERS_LENGTH_OFFSET,
				PROBE_FILTERS_LENGTH_FILTERS_LENGTH_WIDTH,
				NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->length = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	/* Urgency */
	bf = win2030_noc_create_register_and_bitfield(probe, "Urgency",
				probe->hw_base,
				PROBE_FILTERS_URGENCY(id),
				PROBE_FILTERS_URGENCY_FILTERS_URGENCY_OFFSET,
				PROBE_FILTERS_URGENCY_FILTERS_URGENCY_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->urgency = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	return 0;
}

/**
 * Create a filter register set (includes RouteIdBase, RouteIdMask, AddrBase_Low,
 *    WindowSize, SecurityBase, SecurityMask, Opcode, Status, Length, Urgency)
 * @param noc_device
 * @param filter
 * @param probe
 * @param id
 * @return
 */
static int win2030_noc_init_trans_filter(struct win2030_noc_device *noc_device,
				 struct win2030_noc_trans_filter *filter, void *hw_base,
				 struct win2030_noc_probe *probe, unsigned id)
{
	struct win2030_bitfield *bf;
	struct win2030_register *reg;

	if ((noc_device == NULL) || (filter == NULL) || (probe == NULL))
		return -EINVAL;

	filter->id = id;
	filter->parent = probe;
	INIT_LIST_HEAD(&filter->register_link);

	/* Mode */
	bf = win2030_noc_create_register_and_bitfield(probe, "Mode",
				hw_base,
				PROBE_TRANS_FILTERS_MODE,
				PROBE_TRANS_FILTERS_MODE_FILTERS_MODE_OFFSET,
				PROBE_TRANS_FILTERS_MODE_FILTERS_MODE_WIDTH,
				NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->mode = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	/* AddressBase_Low */
	bf = win2030_noc_create_register_and_bitfield(probe, "AddressBase_Low",
			hw_base,
			PROBE_TRANS_FILTERS_ADDRBASE_LOW,
			PROBE_TRANS_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_OFFSET,
			PROBE_TRANS_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_WIDTH,
			NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->addr_base_low = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	/* AddressBase_High */
	bf = win2030_noc_create_register_and_bitfield(probe, "AddressBase_High",
			hw_base,
			PROBE_TRANS_FILTERS_ADDRBASE_HIGH,
			PROBE_TRANS_FILTERS_ADDRBASE_HIGH_FILTERS_ADDRBASE_HIGH_OFFSET,
			PROBE_TRANS_FILTERS_ADDRBASE_HIGH_FILTERS_ADDRBASE_HIGH_WIDTH,
			NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->addr_base_high = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	/* WindowSize */
	bf = win2030_noc_create_register_and_bitfield(probe, "WindowSize",
			hw_base,
			PROBE_TRANS_FILTERS_WINDOWSIZE,
			PROBE_TRANS_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_OFFSET,
			PROBE_TRANS_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_WIDTH,
			NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->window_size = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	/* OpCode */
	reg = win2030_new_register(probe->dev, "OpCode", hw_base,
				 PROBE_TRANS_FILTERS_OPCODE, 32);

	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "opcode_rden",
				PROBE_TRANS_FILTERS_OPCODE_RDEN_OFFSET,
				PROBE_TRANS_FILTERS_OPCODE_RDEN_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "opcode_wren",
				PROBE_TRANS_FILTERS_OPCODE_WREN_OFFSET,
				PROBE_TRANS_FILTERS_OPCODE_WREN_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->op_code = reg;
	list_add_tail(&reg->parent_link, &filter->register_link);

	/* UserBase */
	bf = win2030_noc_create_register_and_bitfield(probe, "UserBase",
			hw_base,
			PROBE_TRANS_FILTERS_USER_BASE,
			PROBE_TRANS_FILTERS_USER_BASE_FILTERS_USER_BASE,
			PROBE_TRANS_FILTERS_USER_BASE_FILTERS_USER_BASE_WIDTH,
			NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->user_base = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	/* UserMask */
	bf = win2030_noc_create_register_and_bitfield(probe, "UserMask",
				hw_base,
				PROBE_TRANS_FILTERS_USER_MASK,
				PROBE_TRANS_FILTERS_USER_MASK_FILTERS_USER_MASK,
				PROBE_TRANS_FILTERS_USER_MASK_FILTERS_USER_MASK_WIDTH,
				NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	filter->user_mask = bf;
	list_add_tail(&bf->parent->parent_link, &filter->register_link);

	return 0;
}

/**
 * Create a profiler register set (includes En, Mode, ObservedSel,
 *    NTenureLines, Thresholds, OverFlowStatus, OverFlowReset, PendingEventMode, PreScaler)
 * @param noc_device
 * @param profiler
 * @param probe
 * @param id
 * @return
 */
static int win2030_noc_init_trans_profiler(struct win2030_noc_device *noc_device,
				 struct win2030_noc_trans_profiler *profiler, void *hw_base,
				 struct win2030_noc_probe *probe, unsigned id)
{
	struct win2030_bitfield *bf;
	int i, j;
	char reg_name[32];

	if ((noc_device == NULL) || (profiler == NULL) || (probe == NULL))
		return -EINVAL;

	profiler->id = id;
	profiler->parent = probe;
	INIT_LIST_HEAD(&profiler->register_link);

	/* En */
	bf = win2030_noc_create_register_and_bitfield(probe, "En",
				hw_base,
				PROBE_TRANS_PROFILER_EN,
				PROBE_TRANS_PROFILER_EN_PROFILER_EN_OFFSET,
				PROBE_TRANS_PROFILER_EN_PROFILER_EN_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	profiler->en = bf;
	list_add_tail(&bf->parent->parent_link, &profiler->register_link);

	/* Mode */
	bf = win2030_noc_create_register_and_bitfield(probe, "Mode",
			hw_base,
			PROBE_TRANS_PROFILER_MODE,
			PROBE_TRANS_PROFILER_MODE_PROFILER_MODE_OFFSET,
			PROBE_TRANS_PROFILER_MODE_PROFILER_MODE_WIDTH,
			NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	profiler->mode = bf;
	list_add_tail(&bf->parent->parent_link, &profiler->register_link);

	/* ObservedSel */
	for (i = 0; i < probe->nr_portsel; i++) {
		scnprintf(reg_name, ARRAY_SIZE(reg_name), "ObservedSel_%d", i);
		bf = win2030_noc_create_register_and_bitfield(probe, reg_name,
				hw_base,
				PROBE_TRANS_PROFILER_OBSERVED_SEL + i * 0x4,
				PROBE_TRANS_PROFILER_OBSERVED_SEL_PROFILER_OBSERVED_SEL_OFFSET,
				PROBE_TRANS_PROFILER_OBSERVED_SEL_PROFILER_OBSERVED_SEL_WIDTH,
				NULL);

		if (IS_ERR_OR_NULL(bf))
			return -EINVAL;

		profiler->observed_sel[i] = bf;
		list_add_tail(&bf->parent->parent_link, &profiler->register_link);
	}

	/* NTenureLines */
	for (i = 0; i < probe->nr_portsel - 1; i++) {
		scnprintf(reg_name, ARRAY_SIZE(reg_name), "NTenureLines_%d", i);
		bf = win2030_noc_create_register_and_bitfield(probe, reg_name,
				hw_base,
				PROBE_TRANS_PROFILER_N_TENURE_LINES + i * 0x4,
				PROBE_TRANS_PROFILER_N_TENURE_LINES_PROFILER_N_TENURE_LINES_OFFSET,
				PROBE_TRANS_PROFILER_N_TENURE_LINES_PROFILER_N_TENURE_LINES_WIDTH,
				NULL);

		if (IS_ERR_OR_NULL(bf))
			return -EINVAL;

		profiler->n_tenure_lines[i] = bf;
		list_add_tail(&bf->parent->parent_link, &profiler->register_link);
	}

	/* Thresholds */
	for (i = 0; i < probe->nr_portsel; i++) {
		for (j = 0; j < WIN2030_NOC_BIN_CNT - 1; j++) {
			scnprintf(reg_name, ARRAY_SIZE(reg_name), "Thresholds_%d_%d", i, j);
			bf = win2030_noc_create_register_and_bitfield(probe, reg_name,
					hw_base,
					PROBE_TRANS_PROFILER_THRESHOLDS + i * 0x10 + j * 0x4,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_OFFSET,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_WIDTH,
					NULL);

			if (IS_ERR_OR_NULL(bf))
				return -EINVAL;

			profiler->thresholds[i][j] = bf;
			list_add_tail(&bf->parent->parent_link, &profiler->register_link);
		}
	}

	/* OverFlowStatus */
	bf = win2030_noc_create_register_and_bitfield(probe, "OverFlowStatus",
				hw_base,
				PROBE_TRANS_PROFILER_OVER_FLOW_STATUS,
				PROBE_TRANS_PROFILER_OVER_FLOW_STATUS_PROFILER_OVER_FLOW_STATUS_OFFSET,
				PROBE_TRANS_PROFILER_OVER_FLOW_STATUS_PROFILER_OVER_FLOW_STATUS_WIDTH,
				NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	profiler->over_flow_status = bf;
	list_add_tail(&bf->parent->parent_link, &profiler->register_link);

	/* OverFlowReset */
	bf = win2030_noc_create_register_and_bitfield(probe, "OverFlowReset",
				hw_base,
				PROBE_TRANS_PROFILER_OVER_FLOW_RESET,
				PROBE_TRANS_PROFILER_OVER_FLOW_RESET_PROFILER_OVER_FLOW_RESET_OFFSET,
				PROBE_TRANS_PROFILER_OVER_FLOW_RESET_PROFILER_OVER_FLOW_RESET_WIDTH,
				NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	profiler->over_flow_reset = bf;
	list_add_tail(&bf->parent->parent_link, &profiler->register_link);

	/* PendingEventMode */
	bf = win2030_noc_create_register_and_bitfield(probe, "PendingEventMode",
				hw_base,
				PROBE_TRANS_PROFILER_PENDING_EVENT_MODE,
				PROBE_TRANS_PROFILER_PENDING_EVENT_MODE_PROFILER_PENDING_EVENT_MODE_OFFSET,
				PROBE_TRANS_PROFILER_PENDING_EVENT_MODE_PROFILER_PENDING_EVENT_MODE_OFFSET,
				NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	profiler->pending_event_mode = bf;
	list_add_tail(&bf->parent->parent_link, &profiler->register_link);

	/* PreScaler */
	bf = win2030_noc_create_register_and_bitfield(probe, "PreScaler",
				hw_base,
				PROBE_TRANS_PROFILER_PRE_SCALER,
				PROBE_TRANS_PROFILER_PRE_SCALER_PROFILER_PRE_SCALER_OFFSET,
				PROBE_TRANS_PROFILER_PRE_SCALER_PROFILER_PRE_SCALER_WIDTH,
				NULL);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	profiler->pre_scaler = bf;
	list_add_tail(&bf->parent->parent_link, &profiler->register_link);

	return 0;
}

static void win2030_noc_remove_probe(struct win2030_noc_probe *probe)
{
	unsigned i;
	struct win2030_noc_counter *counter;
	struct win2030_register *reg = NULL;

	/*TODO free registers */
	if (probe_t_pkt == probe->type) {
		struct win2030_noc_filter *filter = probe->filters;
		for (i = 0; i < probe->nr_filters; i++) {
			list_for_each_entry(reg, &filter->register_link, parent_link) {
				win2030_free_register(reg);
			}
			filter++;
		}
	} else {
		struct win2030_noc_trans_filter *filter = probe->trans_filters;
		for (i = 0; i < probe->nr_filters; i++) {
			list_for_each_entry(reg, &filter->register_link, parent_link) {
				win2030_free_register(reg);
			}
			filter++;
		}
	}

	for (i = 0, counter = probe->counters; i < probe->nr_counters; i++) {
		list_for_each_entry(reg, &counter->register_link, parent_link) {
			win2030_free_register(reg);
		}
		counter++;
	}
	kfree(probe->filters);
	kfree(probe->counters);
}

static struct win2030_register *win2030_noc_new_probe_register(char *reg_name,
			unsigned offset, struct win2030_noc_probe *probe)
{
	struct win2030_register *reg;

	reg = win2030_new_register(probe->dev, reg_name, probe->hw_base,
				 offset, 32);
	if (!IS_ERR_OR_NULL(reg))
		list_add_tail(&reg->parent_link, &probe->register_link);

	return reg;
}

/**
 * Create a probe register set (includes MainCtl, ConfigCtl, StatPeriod, StatAlarmMin, StatAlarmMax
 * @param noc_device
 * @param probe
 * @param cnt_id
 * @return
 */
static int win2030_noc_init_probe(struct win2030_noc_device *noc_device,
				struct win2030_noc_probe *probe,
				struct device_node *np)
{
	struct device *_dev = probe->dev;
	struct win2030_register *reg;
	struct win2030_bitfield *bf;

	probe->parent = noc_device;
	tasklet_init(&probe->tasklet, win2030_noc_stat_measure_tasklet,
		(unsigned long)probe);

	probe->nr_portsel = of_property_count_strings(np, "portsel");
	if (probe->nr_portsel <= 0) {
		return -EINVAL;
	}
	probe->available_portsel = win2030_get_strings_from_dts(_dev, np, "portsel");

	/**
	 * Register: MainCtl
	 */
	reg = probe->main_ctl = win2030_noc_new_probe_register("MainCtl",
				PROBE_MAINCTL, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "StatEn",
				PROBE_MAINCTL_STATEN_OFFSET,
				PROBE_MAINCTL_STATEN_WIDTH,
				win2030_noc_enable_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "AlarmEn",
				PROBE_MAINCTL_ALARMEN_OFFSET,
				PROBE_MAINCTL_ALARMEN_WIDTH,
				win2030_noc_enable_enum);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "StatCondDump",
				PROBE_MAINCTL_STATCONDDUMP_OFFSET,
				PROBE_MAINCTL_STATCONDDUMP_WIDTH,
				win2030_noc_on_off_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "FiltByteAlwaysChainableEn",
				PROBE_MAINCTL_FILT_BYTE_ALWAYS_CAHINABLE_EN_OFFSET,
				PROBE_MAINCTL_FILT_BYTE_ALWAYS_CAHINABLE_EN_WIDTH,
				win2030_noc_on_off_enum);

	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	/**
	 * Register: CfgCtl
	 */
	reg = probe->cfg_ctl = win2030_noc_new_probe_register("CfgCtl",
				PROBE_CFGCTL, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "Global_Enable",
				PROBE_CFGCTL_GLOBALEN_OFFSET,
				PROBE_CFGCTL_GLOBALEN_WIDTH,
				win2030_noc_enable_enum);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "Active",
				PROBE_CFGCTL_GLOBALEN_OFFSET,
				PROBE_CFGCTL_GLOBALEN_WIDTH,
				win2030_noc_on_off_enum);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;
#if 0
	/**
	 * Register: TracePortSel
	 */
	scnprintf(mystr, ARRAY_SIZE(mystr), "probe(%s)_TracePortSel", probe->id);
	reg = win2030_new_register(noc_device->dev, mystr, probe->hw_base,
				 PROBE_TRACEPORTSEL,
				 32);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	probe->trace_port_sel = reg;

	bf = win2030_new_bitfield(reg, "TracePortSel",
				PROBE_TRACEPORTSEL_TRACEPORTSEL_OFFSET,
				PROBE_TRACEPORTSEL_TRACEPORTSEL_WIDTH,
				probe->available_portsel);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;
#endif
	/**
	 * Register: FilterLut
	 */
	reg = probe->filter_lut = win2030_noc_new_probe_register("FilterLut",
				PROBE_FILTERLUT, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "FilterLut",
				PROBE_FILTERLUT_FILTERLUT_OFFSET,
				PROBE_FILTERLUT_FILTERLUT_WIDTH, NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	/**
	 * Register: StatPeriod
	 */
	reg = probe->stat_period = win2030_noc_new_probe_register("StatPeriod",
				PROBE_STATPERIOD, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "StatPeriod",
				PROBE_STATPERIOD_STATPERIOD_OFFSET,
				PROBE_STATPERIOD_STATPERIOD_WIDTH, NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	/**
	 * Register: StatGo
	 */
	reg = probe->stat_go = win2030_noc_new_probe_register("StatGo",
				PROBE_STATGO, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "StatGo",
				PROBE_STATGO_STATGO_OFFSET,
				PROBE_STATGO_STATGO_WIDTH, NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	/**
	 * Register: StatAlarmMin
	 */
	reg = probe->stat_alarm_min = win2030_noc_new_probe_register("StatAlarmMin",
			PROBE_STATALARMMIN, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "StatAlarmMin",
				PROBE_STATALARMMIN_STATALARMMIN_OFFSET,
				PROBE_STATALARMMIN_STATALARMMIN_WIDTH, NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	/**
	 * Register: StatAlarmMinHigh
	 */
	reg = probe->stat_alarm_min_high = win2030_noc_new_probe_register("StatAlarmMinHigh",
			PROBE_STATALARMMIN_HIGH, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "StatAlarmMinHigh",
				PROBE_STATALARMMIN_HIGH_STATALARMMIN_HIGH_OFFSET,
				PROBE_STATALARMMIN_HIGH_STATALARMMIN_HIGH_WIDTH, NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	/**
	 * Register: StatAlarmMax
	 */
	reg = probe->stat_alarm_max = win2030_noc_new_probe_register("StatAlarmMax",
				PROBE_STATALARMMAX, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "StatAlarmMax",
				PROBE_STATALARMMAX_STATALARMMAX_OFFSET,
				PROBE_STATALARMMAX_STATALARMMAX_WIDTH, NULL);

	/**
	 * Register: StatAlarmStatus
	 */
	reg = probe->stat_alarm_status = win2030_noc_new_probe_register("StatAlarmStatus",
			PROBE_STATALARMSTATUS, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "StatAlarmStatus",
				PROBE_STATALARMSTATUS_STATALARMSTATUS_OFFSET,
				PROBE_STATALARMSTATUS_STATALARMSTATUS_WIDTH,
				NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	/**
	 * Register: StatAlarmEn
	 */
	reg = probe->stat_alarm_en = win2030_noc_new_probe_register("StatAlarmEn",
			PROBE_STATALARMEN, probe);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "StatAlarmEn",
				PROBE_STATALARMEN_STATALARMEN_OFFSET,
				PROBE_STATALARMEN_STATALARMEN_WIDTH, win2030_noc_enable_enum);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	return 0;
}

static int win2030_noc_parse_dts_qoscfg(struct device *_dev,
				char *propname,
				struct regcfg **config)
{
	int def_len;
	struct property *prop;
	struct device_node *np = _dev->of_node;

	prop = of_find_property(np, propname, &def_len);
	if (prop != NULL) {
		unsigned i;
		struct regcfg *reg;

		*config = (struct regcfg *)
			devm_kzalloc(_dev,
					sizeof(struct regcfg), GFP_KERNEL);
		if (*config == NULL) {
			dev_err(_dev, "%s: Memory allocation failed!",
					__func__);
			BUG();
		}
		INIT_LIST_HEAD(&(*config)->list);

		for (i = 0; i < (def_len / sizeof(u32)); i += 2) {
			reg = (struct regcfg *)
				devm_kzalloc(_dev,
				sizeof(struct regcfg), GFP_KERNEL);
			if (reg == NULL) {
				dev_err(_dev, "%s: Memory allocation failed!",
						__func__);
				BUG();
			}
			of_property_read_u32_index(np, propname, i,
					&reg->offset);
			of_property_read_u32_index(np, propname, i + 1,
					&reg->value);

			list_add_tail(&reg->list,
					&(*config)->list);
		}
	} else
		*config = NULL;

	return *config == NULL ? -EINVAL : 0;
}

static int win2030_noc_qos_create_register(struct device *_dev,
		void __iomem *base, struct dev_qos_cfg *qos)
{
	struct win2030_register *reg;
	struct win2030_bitfield *bf;

	/**
	 * Register: Priority
	 */
	reg = win2030_new_register(_dev, "Priority", base, GPU_QOS_GEN_PRIORITY, 32);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "P0", GPU_QOS_GEN_PRIORITY_P0_OFFSET,
		GPU_QOS_GEN_PRIORITY_P0_WIDTH, NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "P1", GPU_QOS_GEN_PRIORITY_P1_OFFSET,
		GPU_QOS_GEN_PRIORITY_P1_WIDTH, NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	list_add_tail(&reg->parent_link, &qos->register_link);

	/**
	 * Register: Mode
	 */
	reg = win2030_new_register(_dev, "Mode", base, GPU_QOS_GEN_MODE, 32);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "Mode", GPU_QOS_GEN_MODE_MODE_OFFSET,
		GPU_QOS_GEN_MODE_MODE_WIDTH, win2030_noc_qos_mode_enum);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;
	list_add_tail(&reg->parent_link, &qos->register_link);

	/**
	 * Register: Bandwidth
	 */
	reg = win2030_new_register(_dev, "Bandwidth", base, GPU_QOS_GEN_BANDWIDTH, 32);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "Bandwidth", GPU_QOS_GEN_BANDWIDTH_BANDWIDTH_OFFSET,
		GPU_QOS_GEN_BANDWIDTH_BANDWIDTH_WIDTH, NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;
	list_add_tail(&reg->parent_link, &qos->register_link);

	/**
	 * Register: Saturation
	 */
	reg = win2030_new_register(_dev, "Saturation", base, GPU_QOS_GEN_SATURATION, 32);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "Saturation", GPU_QOS_GEN_SATURATION_SATURATION_OFFSET,
		GPU_QOS_GEN_SATURATION_SATURATION_WIDTH, NULL);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;
	list_add_tail(&reg->parent_link, &qos->register_link);

	/**
	 * Register: ExtControl
	 */
	reg = win2030_new_register(_dev, "ExtControl", base, GPU_QOS_GEN_EXTCONTROL, 32);
	if (IS_ERR_OR_NULL(reg))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "SocketEn", GPU_QOS_GEN_EXTCONTROL_SOCKETQOSEN_OFFSET,
		GPU_QOS_GEN_EXTCONTROL_SOCKETQOSEN_WIDTH, win2030_noc_enable_enum);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "ExtThrEn", GPU_QOS_GEN_EXTCONTROL_EXTTHREN_OFFSET,
		GPU_QOS_GEN_EXTCONTROL_EXTTHREN_WIDTH, win2030_noc_enable_enum);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "IntClkEn", GPU_QOS_GEN_EXTCONTROL_INTCLKEN_OFFSET,
		GPU_QOS_GEN_EXTCONTROL_INTCLKEN_WIDTH, win2030_noc_enable_enum);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;

	bf = win2030_new_bitfield(reg, "ExtLimitEn", GPU_QOS_GEN_EXTCONTROL_EXTLIMITEN_OFFSET,
		GPU_QOS_GEN_EXTCONTROL_EXTLIMITEN_WIDTH, win2030_noc_enable_enum);
	if (IS_ERR_OR_NULL(bf))
		return -EINVAL;
	list_add_tail(&reg->parent_link, &qos->register_link);

	return 0;
}

static int win2030_noc_parse_dts_qoslist(struct device *_dev)
{
	int def_len;
	struct property *prop;
	struct device_node *np = _dev->of_node;
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	u32 qos_base;

	INIT_LIST_HEAD(&noc_device->qos_list);
	prop = of_find_property(np, "eswin,qos-configs", &def_len);
	if (prop != NULL) {
		char str[64] = "\0";
		struct dev_qos_cfg *devqos;
		int i, ncfg = of_property_count_strings(np, "eswin,qos-configs");

		for (i = 0; i < ncfg; i++) {
			int ret = 0;
			devqos = (struct dev_qos_cfg *)devm_kzalloc(_dev, sizeof(struct dev_qos_cfg), GFP_KERNEL);
			if (devqos == NULL) {
				dev_err(_dev, "%s: Memory allocation failed!", __func__);
				BUG();
			}
			INIT_LIST_HEAD(&devqos->register_link);
			of_property_read_string_index(np, "eswin,qos-configs", i, &devqos->name);

			/* test if controlled by noc driver */
			sprintf(str, "eswin,%s-qos-owner", devqos->name);
			if (of_property_read_bool(np, str))
				devqos->noc_owner = 1;
			else
				devqos->noc_owner = 0;

			sprintf(str, "eswin,%s-qos-base", devqos->name);
			ret = of_property_read_u32(np, str, &qos_base);
			if (ret) {
				dev_info(_dev, "%s could not get reg base\n", str);
				devm_kfree(_dev, devqos);
				return ret;
			} else {
				devqos->hw_base = ioremap(qos_base, 0x100);
				ret = win2030_noc_qos_create_register(_dev, devqos->hw_base, devqos);
				if (ret) {
					dev_info(_dev, "%s could not create qos register\n", str);
					devm_kfree(_dev, devqos);
					return ret;
				}
			}

			sprintf(str, "eswin,%s-qos-settings", devqos->name);
			ret = win2030_noc_parse_dts_qoscfg(_dev, str,
					&devqos->config);
			if (ret) {
				dev_info(_dev, "%s not add to list\n", str);
				devm_kfree(_dev, devqos);
				return -EINVAL;
			} else {
				dev_info(_dev, "%s added to list(%d)\n",
						str, devqos->noc_owner);
				list_add_tail(&devqos->list, &noc_device->qos_list);
			}
		}
	} else {
		dev_info(_dev, "no QoS config list\n");
	}
	return 0;
}
#define 	NOC_DEV_NUM		10

static struct win2030_noc_device *noc_dev_glob[NOC_DEV_NUM] = {NULL, NULL};
static int noc_idev_glob;
/*
	notice: the "is_enalbe" flag only affect the NOC internal priority.
	when is_enalbe == true, the NOC qos will be set according to the dts config.
	when is_enalbe == false, the NOC qos will be fair, but the IP qos config will still pass to ddr controller.
	If you want completely fair mode, set bypass mode in noc dts node and set the same qos priority in the IP config.
*/
void win2030_noc_qos_set(const char *name, bool is_enalbe)
{
	struct dev_qos_cfg *qos = NULL;
	struct regcfg *reg = NULL;
	int idev = 0;

	for (idev = 0; idev < NOC_DEV_NUM; idev++) {
		struct win2030_noc_device *noc_dev = noc_dev_glob[idev];

		if (!noc_dev)
			continue;
		list_for_each_entry(qos, &noc_dev->qos_list, list) {
			if (strcmp(qos->name, name) == 0) {
				if (qos->config) {
					dev_info(noc_dev->dev, "%s QoS config %s\n",
						is_enalbe ? "Enable" : "Disable", qos->name);
					list_for_each_entry(reg, &qos->config->list, list) {
						iowrite32(reg->value, qos->hw_base + reg->offset);
						/*when disable qos, set the mode register to bypass and disable socket*/
						if (false == is_enalbe) {
							if (0xC == reg->offset) {
								iowrite32(0x2, qos->hw_base + reg->offset);
							}
							if (0x18 == reg->offset) {
								iowrite32(0x0, qos->hw_base + reg->offset);
							}
						}
					}
				}
				return;
			}
		}
	}
	pr_err("QoS config %s not found\n", name);
}
EXPORT_SYMBOL(win2030_noc_qos_set);

int noc_device_qos_set(struct win2030_noc_device *noc_device, bool is_enalbe)
{
	struct dev_qos_cfg *qos = NULL;

	if (!noc_device) {
		dev_err(noc_device->dev, "%s NULL noc device\n", __func__);
		return -ENXIO;
	}
	list_for_each_entry(qos, &noc_device->qos_list, list) {
		if (qos->noc_owner) {
			win2030_noc_qos_set(qos->name, is_enalbe);
		}
	}
	return 0;
}

/**
 * Parse dts to get number of probes and number of filters and counters per probe
 * @param _dev
 * @return
 */
static int win2030_noc_parse_dts(struct device *_dev)
{
	struct device_node *np = _dev->of_node;
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);

	noc_device->trap_on_error = of_property_read_bool(np, "errors,trap");

	dev_info(_dev, "error policy: %s\n", noc_device->trap_on_error ? "trap" : "print");

	win2030_noc_parse_dts_qoslist(_dev);
	noc_dev_glob[noc_idev_glob] = noc_device;
	noc_idev_glob++;
	return 0;
}

static int win2030_noc_register_probe_common(struct device_node *np,
	struct device *_dev, struct win2030_noc_probe **_probe)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct win2030_noc_counter *counters;
	int ret, j;
	struct win2030_noc_probe *probe;

	/* Initialise probes */
	probe = devm_kcalloc(_dev, 1, sizeof(struct win2030_noc_probe), GFP_KERNEL);
	probe->hw_base = of_iomap(np, 0);
	if (IS_ERR_OR_NULL(probe->hw_base)) {
		return -EINVAL;
	}
	INIT_LIST_HEAD(&probe->register_link);

	probe->dev = _dev;
	probe->id = np->name;

	probe->clock = of_clk_get_by_name(np, "clk");
	if (IS_ERR_OR_NULL(probe->clock)) {
		dev_dbg(probe->dev, "Error %px while getting clock of probe %s\n",
			probe->clock, probe->id);
		return -ENXIO;
	}

	if (!IS_ERR_OR_NULL(probe->clock)) {
		ret = clk_prepare_enable(probe->clock);
		if (ret) {
			dev_err(probe->dev, "Error %d while enabling clock of probe %s\n",
				ret, probe->id);
			return ret;
		}
	}
	if (!IS_ERR_OR_NULL(probe->clock))
		dev_dbg(probe->dev, "Clock of probe %s rate %lu\n",
				probe->id, clk_get_rate(probe->clock));

	probe->stat_irq = of_irq_get_byname(np, "stat");
	if (probe->stat_irq < 0) {
		dev_err(_dev, "Error %d while extracting stat alarm irq\n",
			(int)probe->stat_irq);
		ret = probe->stat_irq;
		return ret;

	}
	ret = devm_request_irq(_dev, probe->stat_irq, win2030_noc_stat_irq,
			IRQF_SHARED, "noc stat", probe);
	if (ret) {
		dev_err(_dev, "Error while installing stat irq %d\n",
			(int)probe->stat_irq);
		return ret;
	}
	list_add_tail(&probe->link, &noc_device->probes);

	/*initialize register*/
	ret = win2030_noc_init_probe(noc_device, probe, np);
	if (ret)
		return -EINVAL;

	/* initialize counters */
	ret = of_property_read_u32(np, "counter,nr", &probe->nr_counters);
	if (ret) {
		dev_err(_dev, "\"counter,nr\" property missing");
		return -EINVAL;
	}
	counters = devm_kcalloc(_dev, probe->nr_counters,
			sizeof(struct win2030_noc_counter), GFP_KERNEL);
	if (!counters)
		return -ENOMEM;

	probe->counters = counters;
	for (j = 0; j < probe->nr_counters; j++) {
		ret = win2030_noc_init_counter(&counters[j], probe, j);
		if (ret)
			return ret;
	}
	*_probe = probe;
	return 0;
}

static int win2030_noc_register_packet_probe(struct device_node *np,
		struct device *_dev)
{
	int ret, j;
	struct win2030_noc_filter *filters;
	struct win2030_noc_probe *probe;
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);

	ret = win2030_noc_register_probe_common(np, _dev, &probe);
	if (ret) {
		return ret;
	}
	probe->type = probe_t_pkt;

	/* Initialise filters */
	ret = of_property_read_u32(np, "filter,nr", &probe->nr_filters);
	if (ret) {
		dev_err(_dev, "\"filter,nr\" property missing");
		return -EINVAL;
	}
	filters = devm_kcalloc(_dev, probe->nr_filters,
			sizeof(struct win2030_noc_filter), GFP_KERNEL);
	if (!filters)
		return -ENOMEM;

	for (j = 0; j < probe->nr_filters; j++) {
		ret = win2030_noc_init_filter(noc_device, &filters[j],
					probe, j);
		if (ret)
			return ret;
	}
	probe->filters = filters;
	return 0;
}

static int win2030_noc_register_trans_probe(struct device_node *np,
			struct device *_dev)
{
	int ret, i;
	struct win2030_noc_trans_filter *filters;
	struct win2030_noc_trans_profiler *profilers;
	struct win2030_noc_probe *probe;
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct device_node *child = NULL;
	void __iomem *hw_base;

	ret = win2030_noc_register_probe_common(np, _dev, &probe);
	if (0 != ret) {
		return ret;
	}
	probe->type = probe_t_trans;

	/* Initialise filters */
	i = 0;
	ret = of_property_read_u32(np, "filter,nr", &probe->nr_filters);
	if (ret) {
		dev_err(_dev, "\"filter,nr\" property missing");
		return -EINVAL;
	}
	filters = devm_kcalloc(_dev, probe->nr_filters,
			sizeof(struct win2030_noc_trans_filter), GFP_KERNEL);
	if (!filters)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, "eswin,win2xxx-noc-trans-filter")) {
			if (!of_device_is_available(child)) {
				probe->nr_filters--;
				continue;
			}
			hw_base = of_iomap(child, 0);
			if (IS_ERR_OR_NULL(hw_base)) {
				return -EINVAL;
			}
			ret = win2030_noc_init_trans_filter(noc_device,
					&filters[i],
					hw_base,
					probe, i);
			if (ret)
				return ret;
			i++;
		}
	}
	probe->trans_filters = filters;

	/* Initialise profilers */
	i = 0;
	ret = of_property_read_u32(np, "profiler,nr", &probe->nr_profilers);
	if (ret) {
		dev_err(_dev, "\"profiler,nr\" property missing");
		return -EINVAL;
	}
	profilers = devm_kcalloc(_dev, probe->nr_profilers,
			sizeof(struct win2030_noc_trans_profiler), GFP_KERNEL);
	if (!profilers)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, "eswin,win2xxx-noc-trans-profiler")) {
			hw_base = of_iomap(child, 0);
			if (IS_ERR_OR_NULL(hw_base)) {
				return -EINVAL;
			}
			ret = win2030_noc_init_trans_profiler(noc_device,
					&profilers[i],
					hw_base,
					probe, i);
			if (ret)
				return ret;
			i++;
		}
	}
	probe->trans_profilers = profilers;

	return 0;
}

/**
 * Create probes, filters and counters registers
 * @param _dev
 * @return
 */
static int win2030_noc_init_device(struct device *_dev)
{
	int ret;
	struct device_node *child = NULL;

	for_each_child_of_node(_dev->of_node, child) {
		if (of_device_is_compatible(child, "eswin,win2xxx-noc-packet-probe")) {
			ret = win2030_noc_register_packet_probe(child, _dev);
			if (0 != ret) {
				dev_err(_dev, "Error while register packet probe\n");
				return ret;
			}
		}
	}

	for_each_child_of_node(_dev->of_node, child) {
		if (of_device_is_compatible(child, "eswin,win2xxx-noc-trans-probe")) {
			ret = win2030_noc_register_trans_probe(child, _dev);
			if (0 != ret) {
				dev_err(_dev, "Error while register trans probe\n");
				return ret;
			}
		}
	}
	return 0;
}

static int win2030_noc_probe(struct platform_device *pdev)
{
	struct device *mydev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct win2030_noc_device *noc_device;
	int ret;
	struct resource *res;

	dev_info(mydev, "Begin initialization\n");
	noc_device = devm_kcalloc(mydev, 1,
		sizeof(struct win2030_noc_device), GFP_KERNEL);
	if (!noc_device)
		return -ENOMEM;

	noc_device->dev = mydev;
	dev_set_drvdata(mydev, noc_device);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(mydev, "Error while get mem resource\n");
		return -ENODEV;
	}
	noc_device->hw_base = devm_ioremap_resource(&pdev->dev, res);;
	if (IS_ERR_OR_NULL(noc_device->hw_base)) {
		dev_err(mydev, "Error while mapping %s reg base 0x%llx!\n",
			np->name, res->start);
		ret = -EINVAL;
		goto free_noc_device;
	}
	INIT_LIST_HEAD(&noc_device->err_queue);
	INIT_LIST_HEAD(&noc_device->registers);
	INIT_LIST_HEAD(&noc_device->probes);
	spin_lock_init(&noc_device->lock);

	ret = win2030_get_err_registers(mydev, &noc_device->registers);
	if (ret) {
		dev_err(mydev, "Error while gettings registers\n");
		goto put_clock;
	}

	/* Get these parameters from the dts */
	ret = win2030_noc_parse_dts(mydev);
	if (ret) {
		dev_err(mydev, "Parsing Dts failed\n");
		goto put_clock;
	}

	ret = win2030_noc_init_device(mydev);
	if (ret) {
		dev_err(mydev, "Failed to init internal NoC modelling\n");
		goto put_clock;
	}

	ret = win2030_noc_error_init(mydev);
	if (ret) {
		dev_err(mydev, "Initialisation of error handling failed\n");
		goto put_clock;
	}

	if (IS_ERR_OR_NULL(win2030_noc_ctrl.win2030_noc_root_debug_dir)) {
		win2030_noc_ctrl.win2030_noc_root_debug_dir = debugfs_create_dir("noc", NULL);
		if (IS_ERR_OR_NULL(win2030_noc_ctrl.win2030_noc_root_debug_dir)) {
			ret = -ENOENT;
			goto free_noc_device;
		}
		INIT_LIST_HEAD(&win2030_noc_ctrl.sbm_link);
	}
	ret = win2030_noc_init_sideband_mgr(mydev);
	if (ret) {
		dev_err(mydev, "Error while gettings registers\n");
		goto put_clock;
	}

	ret = win2030_noc_stat_init(mydev);
	if (ret)
		dev_err(mydev, "No NOC sniffer or NOC sniffer init failure\n");
	else {
		ret = win2030_noc_stat_debugfs_init(mydev);
		if (ret)
			dev_err(mydev,
				"Initialisation of NOC sniffer sysfs failed\n");
	}
	ret = win2030_noc_prof_init(mydev);
	if (ret)
		dev_err(mydev, "No NOC profiler or NOC profiler init failure\n");
	else {
		ret = win2030_noc_prof_debugfs_init(mydev);
		if (ret)
			dev_err(mydev,
				"Initialisation of NOC profiler sysfs failed\n");
	}
	ret = win2030_noc_debug_init(mydev);
	if (0 != ret) {
		dev_err(mydev, "Error %d while init debug\n", ret);
		return ret;
	}
	dev_info(mydev, "initialization sucess\n");

	return 0;
put_clock:
free_noc_device:
	return ret;
}

static int win2030_noc_remove(struct platform_device *pdev)
{
	struct device *mydev = &pdev->dev;
	struct win2030_noc_device *noc_device = dev_get_drvdata(mydev);
	struct win2030_noc_error *noc_err = NULL, *tmp_err = NULL;
	struct win2030_register *reg = NULL, *tmp_reg = NULL;
	struct win2030_noc_probe *probe = NULL;
	struct win2030_noc_stat_measure *stat_measure, *tmp_stat_measure = NULL;
	struct win2030_noc_stat_probe *stat_probe = NULL, *tmp_stat_probe = NULL;
	struct win2030_noc_prof_probe *prof_probe = NULL, *tmp_prof_probe = NULL;
	struct win2030_noc_prof_measure *prof_measure, *tmp_prof_measure = NULL;

	dev_info(mydev, "Removing %s\n", dev_name(mydev));

	list_for_each_entry_safe(stat_probe, tmp_stat_probe,
				 &noc_device->stat->probe, link) {
		list_for_each_entry_safe(stat_measure, tmp_stat_measure,
					 &stat_probe->measure, link) {
			list_del(&stat_measure->link);
			kfree(stat_measure);
		}
		list_del(&stat_probe->link);
		kfree(stat_probe);
	}

	list_for_each_entry_safe(prof_probe, tmp_prof_probe,
				 &noc_device->prof->prof_probe, link) {
		list_for_each_entry_safe(prof_measure, tmp_prof_measure,
					 &prof_probe->measure, link) {
			list_del(&prof_measure->link);
			kfree(prof_measure);
		}
		list_del(&prof_probe->link);
		kfree(prof_probe);
	}

	list_for_each_entry_safe(noc_err, tmp_err,
				&noc_device->err_queue, link) {
		list_del(&noc_err->link);
		kfree(noc_err);
	}

	list_for_each_entry_safe(reg, tmp_reg, &noc_device->registers, link) {
		list_del(&reg->link);
		win2030_free_register(reg);
	}

	list_for_each_entry(probe, &noc_device->probes, link) {
		win2030_noc_remove_probe(probe);
	}

	kfree(noc_device->stat);
	kfree(noc_device->prof);
	return 0;
}
#ifdef CONFIG_PM
static int noc_resume(struct device *dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(dev);
	void __iomem *base = noc_device->hw_base;

	noc_device_qos_set(noc_device, true);

	while (ioread32(ERRLOG_0_ERRVLD(base))) {
		dev_err(dev, "NoC Error pending during resume\n");
		win2030_noc_get_error(noc_device);
	}

	iowrite32(1, ERRLOG_0_FAULTEN(base));

	return 0;
}
static int noc_suspend(struct device *dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(dev);
	void __iomem *base = noc_device->hw_base;

	iowrite32(0, ERRLOG_0_FAULTEN(base));

	while (ioread32(ERRLOG_0_ERRVLD(base))) {
		dev_err(dev, "NoC Error pending during suspend\n");
		win2030_noc_get_error(noc_device);
	}

	return 0;
}
static const struct dev_pm_ops win2030_noc_driver_pm_ops = {

	.suspend = noc_suspend,
	.resume = noc_resume,

};
#endif

static struct platform_driver win2030_noc_driver = {
	.probe = win2030_noc_probe,
	.remove = win2030_noc_remove,
	.driver = {
		.name = "win2030-noc",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &win2030_noc_driver_pm_ops,
#endif
		.of_match_table = of_match_ptr(win2030_noc_of_match),},
};

static int __init win2030_noc_init(void)
{
	return platform_driver_register(&win2030_noc_driver);
}

static void __exit win2030_noc_exit(void)
{
	platform_driver_unregister(&win2030_noc_driver);
}

subsys_initcall(win2030_noc_init);
module_exit(win2030_noc_exit);

MODULE_AUTHOR("huangyifeng@eswincomputing.com");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");
