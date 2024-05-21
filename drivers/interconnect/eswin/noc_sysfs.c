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

#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/interrupt.h>

#include "noc.h"

#define MAX_ENUM_SIZE 254

static int win2030_noc_user_flag_dump(char *buf,
	struct win2030_bitfield *bitfield, unsigned bf_value)
{
	int i;
	int count = 1;

	for (i = 0; i < bitfield->length; i++) {
		if (test_bit(i, (unsigned long *)&bf_value)) {
			strncat(buf, bitfield->lut[i], strlen(bitfield->lut[i]));
			strncat(buf, "|", 1);
			count += strlen(bitfield->lut[i]);
		}
	}
	count--;
	return count;
}

static void win2030_bitfield_debug_print(char *buf,
				struct win2030_bitfield *bitfield,
				unsigned value,
				const char *prefix)
{
	unsigned bf_value = (value & bitfield->mask)
		>> bitfield->offset;
	int count = 0;
	char buf2[SIZE_SMALL_BUF + 2];

	if (bitfield->lut) {
		if (!strcmp(bitfield->name, "User_flag")) {
			count = sprintf(buf2, "%s%s: ", prefix, bitfield->name);
			count += win2030_noc_user_flag_dump(buf2, bitfield, bf_value);
		} else {
			count = sprintf(buf2, "%s%s: %s", prefix,
				  bitfield->name,
				  bitfield->lut[bf_value]);
		}
	} else {
		count = sprintf(buf2, "%s%s: %x", prefix,
				  bitfield->name, bf_value);
	}
	strncat(buf, buf2, count);
	strncat(buf, "\n", 1);
	return;
}

static int win2030_register_debug_print(char *buf,
				struct win2030_register *reg,
				unsigned value,
				const char *prefix)
{
	struct win2030_bitfield *bf = NULL;
	char myprefix[64];
	int count = 0;
	char buf2[SIZE_SMALL_BUF + 2];

	scnprintf(myprefix, ARRAY_SIZE(myprefix), "\t%s", prefix);
	count = sprintf(buf2, "%s%s: %#x\n", prefix, reg->name, value);
	strncat(buf, buf2, count);

	list_for_each_entry(bf, &reg->bitfields, link) {
		win2030_bitfield_debug_print(buf, bf, value,
			myprefix);
	}
	return 0;
}

int noc_debug_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}
static int noc_err_get_bitfield_vaule(struct win2030_register *reg,
		int reg_vaule, char *bitfield_name)
{
	struct win2030_bitfield *bf = NULL;
	int bitfield_vaule = -1;

	list_for_each_entry(bf, &reg->bitfields, link) {
		if (!strcmp(bf->name, bitfield_name)) {
			bitfield_vaule = (reg_vaule & bf->mask) >> bf->offset;
			break;
		}
	}
	return bitfield_vaule;
}

static void win2030_noc_err_addr_dump(char *buf, struct win2030_noc_device *noc_device,
					struct win2030_noc_error *noc_err,
					int reg_index,
					const char *prefix)
{
	struct win2030_register *reg1, *reg3, *reg4;
	u32 vaule_init_flow, vaule_target_flow, vaule_target_sub_range;
	struct win2030_bitfield *bf = NULL;
	struct win2030_bitfield *bf_AbsoluteAddress;
	unsigned vaule_addr_msb;
	u64 vaule_addr;
	int i;
	unsigned vaule_err_RouteId;
	bool found = false;
	char myprefix[64];
	char buf2[SIZE_SMALL_BUF + 2];
	int count = 0;

	scnprintf(myprefix, ARRAY_SIZE(myprefix), "\t%s", prefix);

	reg3 = noc_device->error_registers[reg_index];

	/*Get the InitFlow, TargetFlow, TargetSubRange of the err*/
	vaule_err_RouteId = noc_err->err[reg3->aperture_link];

	reg1 = noc_device->error_registers[reg3->aperture_link];
	vaule_init_flow = noc_err_get_bitfield_vaule(reg1, vaule_err_RouteId, "InitFlow");
	vaule_target_flow = noc_err_get_bitfield_vaule(reg1, vaule_err_RouteId, "TargetFlow");
	vaule_target_sub_range = noc_err_get_bitfield_vaule(reg1, vaule_err_RouteId, "TargetSubRange");

	vaule_addr = noc_err->err[reg_index];

	if (-1 != reg3->msb_link) {
		/*if addr msb exist, get it*/
		vaule_addr_msb = noc_err->err[reg3->msb_link];
		reg4 = noc_device->error_registers[reg3->msb_link];
		list_for_each_entry(bf, &reg4->bitfields, link) {
			if (!strcmp(bf->name, "addr_msb")) {
				vaule_addr |= ((u64)(vaule_addr_msb & bf->mask)) << 32;
				break;
			}
		}
	}
	/*check if any recorded RouteId match the err pkt's RouteId*/
	list_for_each_entry(bf, &reg3->bitfields, link) {
		if (!strcmp(bf->name, "AbsoluteAddress")) {
			bf_AbsoluteAddress = bf;
			for (i = 0; i < bf_AbsoluteAddress->aperture_size; i++) {
				if (bf_AbsoluteAddress->target_sub_range[i] == vaule_target_sub_range
					&& bf_AbsoluteAddress->target_flow[i] == vaule_target_flow
					&& bf_AbsoluteAddress->init_flow[i] == vaule_init_flow) {
						found = true;
						break;
					}
			}
		}
	}

	if (true == found) {
		vaule_addr |= bf_AbsoluteAddress->aperture_base[i];
		count = sprintf(buf2, "%s%s: 0x%llx\n", prefix, bf_AbsoluteAddress->name, vaule_addr);
	} else {
		count = sprintf(buf2, "%s%s: 0x%llx\n", prefix, "OffsetAddr", vaule_addr);
	}
	strncat(buf, buf2, count);
	return;
}

int noc_error_dump(char *buf,
		struct win2030_noc_device *noc_device,
		struct win2030_noc_error *noc_err)
{
	unsigned i;
	unsigned err;
	struct win2030_register *reg;
	int count = 0;
	char buf2[SIZE_SMALL_BUF + 2];

	count = sprintf(buf2, "timestamp: %lld:\n", noc_err->timestamp);
	strncat(buf, buf2, count);
	for (i = 0; i < noc_device->error_logger_cnt; i++) {
		reg = noc_device->error_registers[noc_device->err_log_lut[i]];
		if (-1 != reg->aperture_link) {
			/*should ErrLog3 come here
			  get err_init_target_subrange form ErrLog1
			  if ErrLog4 exist, add it to the addr vaule
			*/
			win2030_noc_err_addr_dump(buf, noc_device, noc_err,
				noc_device->err_log_lut[i], "\t");
		}
		err = noc_err->err[noc_device->err_log_lut[i]];
		win2030_register_debug_print(buf, reg, err, "\t");
	}
	return 0;
}

static const char *win2030_bitfield_get_enum_from_value(struct win2030_bitfield *bf,
						      unsigned value)
{
	const char **lut = bf->lut;
	const char *mystr;
	unsigned i;

	if (lut == NULL)
		return ERR_PTR(-EINVAL);

	for (mystr = lut[0], i = 0; mystr; mystr = lut[++i])
		;

	if (value > i)
		return ERR_PTR(-EINVAL);

	return lut[value];
}

int win2030_bitfield_read(struct win2030_bitfield *bf, unsigned *value)
{
	struct win2030_register *reg = bf->parent;

	if (reg == NULL)
		return -EINVAL;

	if (reg->base == NULL)
		return -EINVAL;

	if (value == NULL)
		return -EINVAL;

	*value = ioread32(WIN2030_NOC_REG_ADDR(reg));
	*value &= bf->mask;
	*value >>= bf->offset;

	dev_dbg_once(reg->parent,
		"Read Register 0x%08x @0x%px (%s), bitfield (%s) "
		"val=0x%x, %d bit at offset %d, mask %x\n",
		ioread32(WIN2030_NOC_REG_ADDR(reg)), WIN2030_NOC_REG_ADDR(reg),
		reg->name, bf->name, *value, bf->length, bf->offset, bf->mask);

	return 0;
}

static const char *win2030_bitfield_read_enum(struct win2030_bitfield *bf)
{
	int ret;
	unsigned value;

	ret = win2030_bitfield_read(bf, &value);
	if (ret)
		return NULL;

	return win2030_bitfield_get_enum_from_value(bf, value);
}

static int win2030_bitfield_get_value_from_enum(struct win2030_bitfield *bf,
					      const char *str_value,
					      unsigned *value)
{
	const char **lut = bf->lut;
	const char *mystr;
	unsigned i;

	if (lut == NULL) {
		if (sscanf(str_value, "%x", value) == 1)
			return 0;

		return -EINVAL;
	}

	for (mystr = lut[0], i = 0; mystr; mystr = lut[++i]) {
		if (!(strcmp(str_value, mystr))) {
			*value = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int win2030_bitfield_write(struct win2030_bitfield *bf, unsigned value)
{
	unsigned long flags;
	unsigned reg_value;
	struct win2030_register *reg = bf->parent;

	if (reg == NULL)
		return -EINVAL;

	if (reg->base == NULL)
		return -EINVAL;

	/*if (value >= (BIT(bf->length)-1)) */
	/*      return -EINVAL; */

	spin_lock_irqsave(&reg->hw_lock, flags);
	reg_value = ioread32(WIN2030_NOC_REG_ADDR(reg));
	reg_value &= ~bf->mask;
	reg_value |= (value << bf->offset);
	iowrite32(reg_value, WIN2030_NOC_REG_ADDR(reg));
	spin_unlock_irqrestore(&reg->hw_lock, flags);

	dev_dbg(reg->parent,
		"Write Register 0x%08x @%p (%s), bitfield (%s) val=0x%x"
		", %d bit at offset %d, mask %x\n",
		reg_value, WIN2030_NOC_REG_ADDR(reg), reg->name, bf->name,
		value, bf->length, bf->offset, bf->mask);
	return 0;
}

static int win2030_bitfield_write_enum(struct win2030_bitfield *bf, const char *str)
{
	int value, ret;

	ret = win2030_bitfield_get_value_from_enum(bf, str, &value);
	if (ret)
		return ret;

	ret = win2030_bitfield_write(bf, value);

	return ret;
}

static int noc_debug_available_show(struct seq_file *m, void *p)
{
	const char **to_show = m->private;
	const char *str;
	int i = 0;
	for (str = to_show[0]; str; str = to_show[++i])
		seq_printf(m, "%s ", str);

	seq_puts(m, "\r\n");
	return 0;
}

static int noc_debug_available_open(struct inode *inode, struct file *file)
{
	return single_open(file, noc_debug_available_show, inode->i_private);
}

static ssize_t noc_debug_bf_write(struct file *filp, const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	struct win2030_bitfield *bf = filp->private_data;
	char buf[MAX_ENUM_SIZE + 1];
	int i;
	size_t ret;
	int err;

	ret = cnt;

	if (cnt > MAX_ENUM_SIZE)
		cnt = MAX_ENUM_SIZE;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	buf[cnt] = 0;

	/* strip ending whitespace. */
	for (i = cnt - 1; i > 0 && isspace(buf[i]); i--)
		buf[i] = 0;

	if (bf->lut) {
		err = win2030_bitfield_write_enum(bf, buf);
		if (err)
			return err;
	} else {
		unsigned value = 0;
		err = sscanf(buf, "%x\n", &value);
		/*if (err) */
		/*      return -ENODEV; */
		err = win2030_bitfield_write(bf, value);
		if (err)
			return -ENODEV;
	}
	*ppos += ret;

	return ret;
}

static ssize_t noc_debug_bf_read(struct file *filp, char __user *ubuf,
				 size_t cnt, loff_t *ppos)
{
	struct win2030_bitfield *bf = filp->private_data;
	char buf[MAX_ENUM_SIZE + 2];
	int r;
	const char *str;
	unsigned value;

	if (bf == NULL)
		return -ENODEV;

	if (bf->lut) {
		str = win2030_bitfield_read_enum(bf);
		if (IS_ERR_OR_NULL(str))
			str = "UNKNOWN BITFIELD VALUE";
		r = sprintf(buf, "%s\n", str);
	} else {
		r = win2030_bitfield_read(bf, &value);
		if (r)
			return -ENODEV;

		r = sprintf(buf, "%#x\n", value);
	}

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

static int noc_debug_error_show(struct seq_file *m, void *p)
{
	struct win2030_noc_device *noc_device = m->private;
	struct win2030_noc_error *noc_err = NULL;
	unsigned long flags;
	char *buf;

	buf = kzalloc(sizeof(*buf) * SIZE_BIG_BUF, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	spin_lock_irqsave(&noc_device->lock, flags);
	list_for_each_entry(noc_err, &noc_device->err_queue, link) {
		spin_unlock_irqrestore(&noc_device->lock, flags);
		noc_error_dump(buf, noc_device, noc_err);
		spin_lock_irqsave(&noc_device->lock, flags);
	}
	spin_unlock_irqrestore(&noc_device->lock, flags);
	seq_printf(m, "%s\n", buf);
	kfree(buf);
	return 0;
}

static int noc_debug_error_open(struct inode *inode, struct file *file)
{
	return single_open(file, noc_debug_error_show, inode->i_private);
}

/**
 * Called when writing run file
 */
static ssize_t noc_qos_run_write(struct file *filp, const char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	struct win2030_noc_device *noc_device = filp->private_data;
	char buf[SIZE_SMALL_BUF];
	unsigned ret;

	if (cnt > SIZE_SMALL_BUF)
		cnt = SIZE_SMALL_BUF - 1;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	if (buf[0] == '0') {
		/* FIXME spinlock? */
		if (false != noc_device->qos_run) {
			noc_device->qos_run = false;
			ret = noc_device_qos_set(noc_device, false);
			if (ret)
				dev_err(noc_device->dev, "Error when disable qos config!\n");
		}
	} else if (buf[0] == '1') {
		if (true != noc_device->qos_run) {
			noc_device->qos_run = true;
			ret = noc_device_qos_set(noc_device, true);
			if (ret)
				dev_err(noc_device->dev, "Error when enable qos config!\n");
		}
	} else
		return -EFAULT;

	*ppos += cnt;

	return cnt;
}

/**
 * Called when reading run file
 */
static ssize_t noc_qos_run_read(struct file *filp, char __user *ubuf,
				 size_t cnt, loff_t *ppos)
{
#define RUN_STR_SIZE 11
	struct win2030_noc_device *noc_device = filp->private_data;
	char buf[RUN_STR_SIZE];
	int r;

	r = snprintf(buf, RUN_STR_SIZE, "%i\n", noc_device->qos_run);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

/**
 * Called when opening run file
 * @param inode
 * @param file
 * @return
 */
static int noc_qos_run_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static const struct file_operations noc_debug_error_fops = {
	.open = noc_debug_error_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static const struct file_operations noc_debug_qos_fops = {
	.open = noc_qos_run_open,
	.read = noc_qos_run_read,
	.write = noc_qos_run_write,
	.llseek = generic_file_llseek,
};

static const struct file_operations noc_debug_fops = {
	.open = noc_debug_open,
	.read = noc_debug_bf_read,
	.write = noc_debug_bf_write,
	.llseek = generic_file_llseek,
};

static const struct file_operations noc_debug_available_fops = {
	.open = noc_debug_available_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

int win2030_noc_debug_reg_init(struct win2030_register *reg, struct dentry *dir)
{
	struct dentry *reg_dir;
	struct win2030_bitfield *bf = NULL;

	reg_dir = debugfs_create_dir(reg->name, dir);
	if (IS_ERR(reg_dir))
		return PTR_ERR(reg_dir);

	list_for_each_entry(bf, &reg->bitfields, link) {
		debugfs_create_file(bf->name,
				    S_IRUGO | S_IWUSR, reg_dir, bf,
				    &noc_debug_fops);
	}
	return 0;
}

int win2030_noc_packet_probe_debug_init(struct win2030_noc_probe *probe,
	struct dentry *probe_dir)

{
	int j;
	char name[64];
	struct win2030_register *reg;
	struct win2030_bitfield *bf = NULL;
	int ret = 0;

	for (j = 0; j < probe->nr_filters; j++) {
		struct win2030_noc_filter *filter;
		struct dentry *filter_dir;

		filter = &probe->filters[j];

		scnprintf(name, ARRAY_SIZE(name), "filter%d", j);
		filter_dir = debugfs_create_dir(name, probe_dir);
		if (IS_ERR(filter_dir))
			return PTR_ERR(filter_dir);

		reg = filter->route_id_base;
		list_for_each_entry(bf, &reg->bitfields, link) {
			if (j != 0)
				continue;
			if ((!(strcmp(bf->name, "InitFlow")))
			    || (!(strcmp(bf->name, "TargetFlow")))) {
				scnprintf(name, ARRAY_SIZE(name),
					  "available_%s_source_event", bf->name);
				debugfs_create_file(name, S_IRUGO,
					    filter_dir, bf->lut,
					    &noc_debug_available_fops);
			}
		}

		list_for_each_entry(reg, &filter->register_link, parent_link) {
			ret = win2030_noc_debug_reg_init(reg, filter_dir);
			if (ret < 0) {
				return ret;
			}
		}
	}
	return ret;
}

int win2030_noc_trans_probe_debug_init(struct win2030_noc_probe *probe,
	struct dentry *probe_dir)
{
	int j;
	char name[64];
	struct win2030_register *reg = NULL;
	int ret = 0;

	for (j = 0; j < probe->nr_filters; j++) {
		struct win2030_noc_trans_filter *filter;
		struct dentry *filter_dir;

		filter = &probe->trans_filters[j];

		scnprintf(name, ARRAY_SIZE(name), "filter%d", j);
		filter_dir = debugfs_create_dir(name, probe_dir);
		if (IS_ERR(filter_dir))
			return PTR_ERR(filter_dir);

		list_for_each_entry(reg, &filter->register_link, parent_link) {
			ret = win2030_noc_debug_reg_init(reg, filter_dir);
			if (ret < 0) {
				return ret;
			}
		}
	}

	for (j = 0; j < probe->nr_profilers; j++) {
		struct win2030_noc_trans_profiler *profiler;
		struct dentry *profiler_dir;

		profiler = &probe->trans_profilers[j];

		scnprintf(name, ARRAY_SIZE(name), "profiler%d", j);
		profiler_dir = debugfs_create_dir(name, probe_dir);
		if (IS_ERR(profiler_dir))
			return PTR_ERR(profiler_dir);

		list_for_each_entry(reg, &profiler->register_link, parent_link) {
			ret = win2030_noc_debug_reg_init(reg, profiler_dir);
			if (ret < 0) {
				return ret;
			}
		}
	}

	return ret;
}


/**
 * Create sysfs
 * @param _dev
 * @return
 */
int win2030_noc_debug_init(struct device *_dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct device_node *np = _dev->of_node;
	struct dentry *dir;
	char name[64];
	int j;
	struct win2030_noc_probe *probe = NULL;
	int ret;
	struct dev_qos_cfg *qos = NULL;
	struct dentry *dir_qos;
	struct dentry *dir_qos_module;
	struct win2030_register *reg = NULL;

	scnprintf(name, ARRAY_SIZE(name), "%s_debug", np->name);

	dir = debugfs_create_dir(name, win2030_noc_ctrl.win2030_noc_root_debug_dir);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	noc_device->dir = dir;

	debugfs_create_file("errors", S_IRUGO, dir, noc_device, &noc_debug_error_fops);

	list_for_each_entry(probe, &noc_device->probes, link) {
		struct dentry *probe_dir;

		scnprintf(name, ARRAY_SIZE(name), "probe_%s", probe->id);
		probe_dir = debugfs_create_dir(name, noc_device->dir);
		if (IS_ERR(probe_dir))
			return PTR_ERR(probe_dir);

		list_for_each_entry(reg, &probe->register_link, parent_link) {
			ret = win2030_noc_debug_reg_init(reg, probe_dir);
			if (ret < 0) {
				return ret;
			}
		}

		if (probe_t_pkt == probe->type) {
			ret = win2030_noc_packet_probe_debug_init(probe, probe_dir);
		} else {
			ret = win2030_noc_trans_probe_debug_init(probe, probe_dir);
		}
		if (0 != ret) {
			return ret;
		}
		for (j = 0; j < probe->nr_counters; j++) {
			struct win2030_noc_counter *cnt;
			struct dentry *dir_cnt;
			cnt = &probe->counters[j];

			scnprintf(name, ARRAY_SIZE(name), "counter%d", j);
			dir_cnt = debugfs_create_dir(name, probe_dir);
			if (IS_ERR(dir_cnt))
				return PTR_ERR(dir_cnt);

			/* We assume the counters are the same */
			if (j == 0) {
				/*
				scnprintf(name, ARRAY_SIZE(name),
					  "available_counter_port_selection");
				debugfs_create_file(name, S_IRUGO, probe_dir,
						    cnt->portsel->lut,
						    &noc_debug_available_fops);
				*/
				scnprintf(name, ARRAY_SIZE(name),
					  "available_counter_alarm_mode");
				debugfs_create_file(name, S_IRUGO, probe_dir,
						    cnt->alarm_mode->lut,
						    &noc_debug_available_fops);

				scnprintf(name, ARRAY_SIZE(name),
					  "available_counter_source_event");
				debugfs_create_file(name, S_IRUGO, probe_dir,
						    cnt->source_event->lut,
						    &noc_debug_available_fops);
			}
			list_for_each_entry(reg, &cnt->register_link, parent_link) {
				ret = win2030_noc_debug_reg_init(reg, dir_cnt);
				if (ret < 0) {
					return ret;
				}
			}
		}
	}
	if (!list_empty (&noc_device->qos_list)) {
		dir_qos = debugfs_create_dir("qos", noc_device->dir);
		if (IS_ERR(dir_qos))
			return PTR_ERR(dir_qos);

		debugfs_create_file("enable", S_IRUGO, dir_qos, noc_device, &noc_debug_qos_fops);
		list_for_each_entry(qos, &noc_device->qos_list, list) {
			dir_qos_module = debugfs_create_dir(qos->name, dir_qos);
			list_for_each_entry(reg, &qos->register_link, parent_link) {
				ret = win2030_noc_debug_reg_init(reg, dir_qos_module);
				if (ret < 0) {
					dev_err(_dev, "Error %d while init qos debug register\n", ret);
					return ret;
				}
			}
		}
	}

	ret = win2030_noc_sideband_mgr_debug_init(_dev);
	if (0 != ret) {
		dev_err(_dev, "Error %d while init sideband mgr debug\n", ret);
		return ret;
	}
	return 0;
}
