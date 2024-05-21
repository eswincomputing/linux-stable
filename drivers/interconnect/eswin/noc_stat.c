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

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/bitfield.h>

#include "noc.h"
#include "noc_regs.h"

//#pragma GCC optimize("O0")

/**
 * Create a new stat_measure from parsed dts entry
 * @param stat_probe
 * @return The created win2030_noc_stat_measure object
 */
static struct win2030_noc_stat_measure *new_stat_measure(
				struct win2030_noc_stat_probe *stat_probe)
{
	struct win2030_noc_stat_measure *stat_measure;
	unsigned long flags;

	stat_measure = kzalloc(sizeof(struct win2030_noc_stat_measure),
			       GFP_KERNEL);
	if (!stat_measure)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&stat_measure->link);
	spin_lock_init(&stat_measure->lock);

	spin_lock_irqsave(&stat_probe->lock, flags);
	list_add_tail(&stat_measure->link, &stat_probe->measure);
	spin_unlock_irqrestore(&stat_probe->lock, flags);

	return stat_measure;
}

/**
 * Create a new stat_probe. A stat_probe contains all measurements which
 * are linked to the same probe
 * @param noc_stat the stat_probe will be attached to noc_stat
 * @param probe the stat_probe will refer to probe
 * @return The created xgold_noc_stat_probe object
 */
static struct win2030_noc_stat_probe *new_stat_probe(struct win2030_noc_stat
						   *noc_stat,
						   struct win2030_noc_probe
						   *probe)
{
	struct win2030_noc_stat_probe *stat_probe;
	unsigned long flags;

	stat_probe = kzalloc(sizeof(struct win2030_noc_stat_probe), GFP_KERNEL);
	if (!stat_probe)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&stat_probe->measure);
	INIT_LIST_HEAD(&stat_probe->link);
	spin_lock_init(&stat_probe->lock);

	spin_lock_irqsave(&noc_stat->lock, flags);
	list_add_tail(&stat_probe->link, &noc_stat->probe);
	spin_unlock_irqrestore(&noc_stat->lock, flags);
	stat_probe->probe = probe;

	return stat_probe;
}

enum {
	FIELD_ID_TRACE_PORT = 0,
	FIELD_ID_INIT_FLOW,
	FIELD_ID_TARGET_FLOW,
	FIELD_ID_ADDR_BASE,
	FIELD_ID_ADDR_SIZE,
	FIELD_ID_OPCODE,
	FIELD_ID_STATUS,
	FIELD_ID_LENGTH,
	FIELD_ID_URGENCY,
	FIELD_ID_MAX,
};

static int win2030_noc_stat_parse_traceport_field(struct win2030_noc_probe *probe,
	struct device *dev, const char *str, int field_id,
	struct win2030_noc_stat_traceport_data *data)
{
	int i;
	u64 vaule;
	int ret;
	struct win2030_register *reg;
	struct win2030_bitfield *bf = NULL;
	const char *str_tmp;
	bool found = false;

	switch (field_id) {
	case FIELD_ID_INIT_FLOW:
		str_tmp = &str[strlen("InitFlow:")];
		reg = probe->filters[0].route_id_base;
		found = false;
		list_for_each_entry(bf, &reg->bitfields, link) {
			if (strcmp(bf->name, "InitFlow") == 0) {
				i = 0;
				while (bf->lut[i] != NULL) {
					if (str_tmp && strcmp(str_tmp, bf->lut[i]) == 0) {
						data->init_flow_name = kstrdup(str_tmp, GFP_KERNEL);
						data->init_flow_idx = i;
						data->init_flow_mask = bf->mask;
						data->init_flow_offset = bf->offset;
						found = true;
						break;
					}
					i += 1;
				}
			}
		}
		if (false == found) {
			dev_err(dev, "get unknown InitFlow data %s when parsing traceport dts!\n", str);
			return -EINVAL;
		}
		break;
	case FIELD_ID_TARGET_FLOW:
		str_tmp = &str[strlen("TargetFlow:")];
		reg = probe->filters[0].route_id_base;
		found = false;
		list_for_each_entry(bf, &reg->bitfields, link) {
			if (strcmp(bf->name, "TargetFlow") == 0) {
				i = 0;
				while (bf->lut[i] != NULL) {
					if (str_tmp && strcmp(str_tmp, bf->lut[i]) == 0) {
						data->target_flow_name = kstrdup(str_tmp, GFP_KERNEL);
						data->target_flow_idx = i;
						data->target_flow_mask = bf->mask;
						data->target_flow_offset = bf->offset;
						found = true;
						break;
					}
					i += 1;
				}
			}
		}
		if (false == found) {
			dev_err(dev, "get unknown TargetFlow data %s when parsing traceport dts!\n", str);
			return -EINVAL;
		}
		break;
	case FIELD_ID_ADDR_BASE:
		ret = kstrtou64(&str[strlen("AddrBase:")], 0, &vaule);
		if (ret) {
			dev_err(dev, "get unknown AddrBase data %s when parsing traceport dts!\n", str);
			return ret;
		}
		data->addrBase_low = FIELD_GET(GENMASK(31, 0), vaule);
		data->addrBase_high = FIELD_GET(GENMASK(40, 32), vaule);
		break;
	case FIELD_ID_ADDR_SIZE:
		ret = kstrtou64(&str[strlen("AddrSize:")], 0, &vaule);
		if (ret) {
			dev_err(dev, "get unknown AddrSize data %s when parsing traceport dts!\n", str);
			return ret;
		}
		data->addrWindowSize = vaule;
		/*should not exceed the max vaule*/
		data->addrWindowSize &= PROBE_FILTERS_WINDOWSIZE_FILTERS_WINDOWSIZE_MASK;
		break;
	case FIELD_ID_OPCODE:
		if (0 == strcmp(str, "Opcode:RdWrLockUrg")) {
			data->Opcode = 0xf;
		} else if (0 == strcmp(str, "Opcode:WrLockUrg")) {
			data->Opcode = 0xe;
		} else if (0 == strcmp(str, "Opcode:RdLockUrg")) {
			data->Opcode = 0xd;
		} else if (0 == strcmp(str, "Opcode:LockUrg")) {
			data->Opcode = 0xc;
		} else if (0 == strcmp(str, "Opcode:RdWrUrg")) {
			data->Opcode = 0xb;
		} else if (0 == strcmp(str, "Opcode:WrUrg")) {
			data->Opcode = 0xa;
		} else if (0 == strcmp(str, "Opcode:RdUrg")) {
			data->Opcode = 0x9;
		} else if (0 == strcmp(str, "Opcode:Urg")) {
			data->Opcode = 0x8;
		} else if (0 == strcmp(str, "Opcode:RdWrLock")) {
			data->Opcode = 0x7;
		} else if (0 == strcmp(str, "Opcode:WrLock")) {
			data->Opcode = 0x6;
		} else if (0 == strcmp(str, "Opcode:RdLock")) {
			data->Opcode = 0x5;
		} else if (0 == strcmp(str, "Opcode:Lock")) {
			data->Opcode = 0x4;
		} else if (0 == strcmp(str, "Opcode:RdWr")) {
			data->Opcode = 0x3;
		} else if (0 == strcmp(str, "Opcode:Wr")) {
			data->Opcode = 0x2;
		} else if (0 == strcmp(str, "Opcode:Rd")) {
			data->Opcode = 0x1;
		} else {
			dev_err(dev, "get unknown Opcode data %s when parsing traceport dts!\n", str);
			return -EINVAL;
		}
		break;

	case FIELD_ID_STATUS:
		if (0 == strcmp(str, "Status:ReqRsp")) {
			data->Status = 3;
		} else if (0 == strcmp(str, "Status:Req")) {
			data->Status = 1;
		} else if (0 == strcmp(str, "Status:Rsp")) {
			data->Status = 2;
		} else {
			dev_err(dev, "get unknown Status data %s when parsing traceport dts!\n", str);
			return -EINVAL;
		}
		break;

	case FIELD_ID_LENGTH:
		ret = kstrtou64(&str[strlen("Length:")], 0, &vaule);
		if (ret) {
			dev_err(dev, "get unknown length data %s when parsing traceport dts!\n", str);
			return ret;
		}
		data->Length = ilog2(vaule);
		data->Length &= PROBE_FILTERS_LENGTH_FILTERS_LENGTH_MASK;
		break;

	case FIELD_ID_URGENCY:
		ret = kstrtou64(&str[strlen("Urgency:")], 0, &vaule);
		if (ret) {
			dev_err(dev, "unknown length data %s when parsing traceport dts!\n", str);
			return ret;
		}
		data->Urgency = vaule;
		data->Urgency &= PROBE_FILTERS_URGENCY_FILTERS_URGENCY_MASK;
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static int win2030_noc_stat_parse_traceport_dts(struct win2030_noc_probe **probe_out,
	struct win2030_noc_device *noc_device, struct device_node *np,
	const char *name, struct win2030_noc_stat_traceport_data *data)
{
	struct property *prop;
	struct device *dev = noc_device->dev;
	int lg;
	int index = 0;
	const char *str[FIELD_ID_MAX] = {NULL}, *s;
	int ret;
	const char *field[] = {"TracePort:", "InitFlow:", "TargetFlow:",
				"AddrBase:", "AddrSize:", "Opcode:", "Status:", "Length:", "Urgency:"};
	int i, j;
	bool found = false;
	struct win2030_noc_probe *probe = NULL;
	const char *str_tmp;

	/* Count number of strings per measurement
	  (normally 1: TracePortSel)
	 */
	lg = of_property_count_strings(np, name);
	if (lg > FIELD_ID_MAX || lg < 1)
		return -EINVAL; /* Invalid data, let's skip this measurement */

	of_property_for_each_string(np, name, prop, s)
		str[index++] = s;

	str_tmp = &str[0][strlen("TracePort:")];
	list_for_each_entry(probe, &noc_device->probes, link) {
		for (j = 0; probe->available_portsel[j] != NULL; j++) {
			if (str_tmp && strcmp(str_tmp, probe->available_portsel[j]) == 0)
				break;
		}
		if (probe->available_portsel[j] != NULL) {
			found = true;
			break;
		}
	}
	if (false == found)
		return -EINVAL;	/* Invalid data, let's skip this measurement. */

	*probe_out = probe;
	data->name = str_tmp;
	data->idx_trace_port_sel = j;

	/*if user not set, default set as below
		route_id_mask: -1, which means all flow
		target_flow_idx: -1, which means all flow
		addrBase: 0, which means all address
		addrWindowSize : 0, which means all range
		Opcode: 0xf, which means enable Rd,Wr,Lock,Urg
		Status:0x3,which means Req,Rsp
		Length:0xf, which means the max packets length could be 0x10000
		Urgency:0x0, which means all urgency level be selected
	*/
	data->init_flow_name = NULL;
	data->init_flow_idx = -1;
	data->init_flow_offset = -1;
	data->init_flow_mask = -1;
	data->target_flow_name = NULL;
	data->target_flow_idx = -1;
	data->target_flow_offset = -1;
	data->target_flow_mask = -1;
	data->addrBase_low = 0;
	data->addrBase_high = 0;
	data->addrWindowSize = 0x3f;
	data->Opcode = 0xf;
	data->Status = 0x3;
	data->Length = 0xF;
	data->Urgency = 0x0;

	for (i = 1; i < lg; i++) {
		for (j = 0; j < ARRAY_SIZE(field); j++) {
			if (0 == strncmp(str[i], field[j], strlen(field[j]))) {
				ret = win2030_noc_stat_parse_traceport_field(probe, dev, str[i], j, data);
				if (ret) {
					dev_err(dev, "error when parsing traceport dts, ret %d!\n", ret);
					return ret;
				}
			}
		}
	}

	dev_dbg(dev, "get traceport data, name: %s\n"
		"\t\t\t\t init_flow: %s init_flow_idx: 0x%x, init_flow_offset: 0x%x, init_flow_mask: 0x%x\n"
		"\t\t\t\t target_flow: %s, target_flow_idx: 0x%x, target_flow_offset: 0x%x, target_flow_mask: 0x%x\n"
		"\t\t\t\t addrBase_low: 0x%x, addrBase_high: 0x%x, addrWindowSize: 0x%x\n"
		"\t\t\t\t Opcode: 0x%x, Status: 0x%x, Length: 0x%llx, Urgency: 0x%x!\n",
		data->name,
		data->init_flow_name == NULL ? "not set" : data->init_flow_name, data->init_flow_idx, data->init_flow_offset,data->init_flow_mask,
		data->target_flow_name == NULL ? "not set" : data->target_flow_name, data->target_flow_idx, data->target_flow_offset, data->target_flow_mask,
		data->addrBase_low, data->addrBase_high, data->addrWindowSize, data->Opcode, data->Status,
		int_pow(2, data->Length), data->Urgency);

	return 0;
}

/**
 * From one line of dts, add a new stat_measure and a stat_probe (if needed)
 * @param noc_device A ref to the device
 * @param np A ref to the dts node
 * @param name the line of the dts
 * @return
 */
static int new_probe_and_measure(struct win2030_noc_device *noc_device,
				 struct device_node *np, const char *name)
{
	struct win2030_noc_stat_probe *stat_probe, *entry = NULL;
	struct win2030_noc_stat *noc_stat = noc_device->stat;
	struct win2030_noc_probe *probe = NULL;
	struct win2030_noc_stat_measure *stat_measure;
	struct win2030_noc_stat_traceport_data data;
	bool found = false;
	int ret;

	ret = win2030_noc_stat_parse_traceport_dts(&probe, noc_device, np, name, &data);
	if (ret) {
		return ret;
	}

	/* Look if the probe was already existing in noc_stat */
	if (!list_empty(&noc_stat->probe)) {
		found = false;
		list_for_each_entry(entry, &noc_stat->probe, link) {
			if (entry->probe == probe) {
				/* It is found, create a new measure */
				stat_measure = new_stat_measure(entry);
				if (IS_ERR(stat_measure))
					goto error;
				found = true;
				break;
			}
		}
		if (found == false) {
			/* Not found, we must create a stat_probe */
			stat_probe = new_stat_probe(noc_stat, probe);
			if (IS_ERR(stat_probe))
				goto error;
			/* Then add the measurement */
			stat_measure = new_stat_measure(stat_probe);
			if (IS_ERR(stat_measure))
				goto free_stat_probe;
		}
	} else {
		/* No stat_probe created up to now, let's create one. */
		stat_probe = new_stat_probe(noc_stat, probe);
		if (IS_ERR(stat_probe))
			goto error;
		/* Then add the measurement */
		stat_measure = new_stat_measure(stat_probe);
		if (IS_ERR(stat_measure))
			goto free_stat_probe;
	}
	memcpy(&stat_measure->data, &data, sizeof(struct win2030_noc_stat_traceport_data));
	return 0;

free_stat_probe:
	kfree(stat_probe);
error:
	return -ENOMEM;
}

/**
 * Return the value of a bitfield
 * @param bf the bitfield to read
 * @return the value of the bitfield
 */
static unsigned win2030_noc_bf_read(struct win2030_bitfield *bf)
{
	struct win2030_register *reg;
	unsigned bf_value;

	reg = bf->parent;
	bf_value = ioread32(WIN2030_NOC_REG_ADDR(reg));
	bf_value &= bf->mask;
	bf_value >>= bf->offset;

	return bf_value;
}

/**
 * Write some bits in a register
 * @param reg The register to write
 * @param mask The mask for the bits (shifted)
 * @param offset The offset of the bits
 * @param value The value to write
 * @return 0 if ok, -EINVAL if value is not valid
 */
int win2030_noc_reg_write(struct win2030_register *reg, unsigned mask,
			       unsigned offset, unsigned value)
{
	unsigned reg_value;
	unsigned long flags;

	if (value > (mask >> offset)) {
		dev_err(reg->parent, "reg_write, error para, name %s, value 0x%x, mask 0x%x, offset 0x%x!\n",
			reg->name, value, mask, offset);
		return -EINVAL;
	}
	spin_lock_irqsave(&reg->hw_lock, flags);
	reg_value = ioread32(WIN2030_NOC_REG_ADDR(reg));
	reg_value &= ~mask;
	reg_value |= (value << offset);
	iowrite32(reg_value, WIN2030_NOC_REG_ADDR(reg));
	spin_unlock_irqrestore(&reg->hw_lock, flags);

	return 0;
}

/**
 * Read some bits in a register
 * @param reg The register to read
 * @param mask The mask for the bits (shifted)
 * @param offset The offset of the bits
 * @return the read value
 */
unsigned win2030_noc_reg_read(struct win2030_register *reg, unsigned mask,
				   unsigned offset)
{
	unsigned reg_value;

	reg_value = ioread32(WIN2030_NOC_REG_ADDR(reg));
	reg_value &= ~mask;
	reg_value >>= offset;

	return reg_value;
}

int win2030_noc_stat_packet_probe_launch_measure(
			struct win2030_noc_device *noc_device,
			struct win2030_noc_stat_probe *stat_probe)
{
	struct win2030_noc_stat_measure *stat_measure;
	struct win2030_noc_probe *probe;
	int i, ret = 0;
	struct win2030_noc_stat_traceport_data *data;

	stat_measure = list_first_entry(&stat_probe->measure,
					struct win2030_noc_stat_measure, link);
	data = &stat_measure->data;

	probe = stat_probe->probe;

	/* Set Filters */
	for (i = 0; i < probe->nr_filters; i++) {
		/*Address*/
		ret |=
		    win2030_noc_reg_write(probe->filters[i].addr_base_low->parent,
			PROBE_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_MASK,
			PROBE_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_OFFSET,
			data->addrBase_low);

		ret |=
		    win2030_noc_reg_write(probe->filters[i].addr_base_high->parent,
			PROBE_FILTERS_ADDRBASE_HIGH_FILTERS_ADDRBASE_HIGH_MASK,
			PROBE_FILTERS_ADDRBASE_HIGH_FILTERS_ADDRBASE_HIGH_OFFSET,
			data->addrBase_high);

		/*Windowsize*/
		ret |=
		    win2030_noc_reg_write(probe->filters[i].window_size->parent,
			PROBE_FILTERS_WINDOWSIZE_FILTERS_WINDOWSIZE_MASK,
			PROBE_FILTERS_WINDOWSIZE_FILTERS_WINDOWSIZE_OFFSET,
			data->addrWindowSize);

		/*opcode*/
		ret |=
		    win2030_noc_reg_write(probe->filters[i].op_code, 0xf, 0, data->Opcode);

		/*status*/
		ret |=
		    win2030_noc_reg_write(probe->filters[i].status, 0x3, 0, data->Status);

		/*length*/
		ret |=
		    win2030_noc_reg_write(probe->filters[i].length->parent,
				PROBE_FILTERS_LENGTH_FILTERS_LENGTH_MASK,
				PROBE_FILTERS_LENGTH_FILTERS_LENGTH_OFFSET,
				data->Length);

		/*urgency*/
		ret |=
		    win2030_noc_reg_write(probe->filters[i].urgency->parent,
				PROBE_FILTERS_URGENCY_FILTERS_URGENCY_MASK,
				PROBE_FILTERS_URGENCY_FILTERS_URGENCY_OFFSET,
				data->Urgency);
	}

	if (data->init_flow_idx != 0xff) {
		ret |= win2030_noc_reg_write(probe->filters[0].route_id_mask->parent,
					   data->init_flow_mask, data->init_flow_offset,
					   (data->init_flow_mask >> data->init_flow_offset));

		ret |= win2030_noc_reg_write(probe->filters[0].route_id_base,
					   data->init_flow_mask, data->init_flow_offset,
					   data->init_flow_idx);
	}
	if (data->target_flow_idx != 0xff) {
		ret |= win2030_noc_reg_write(probe->filters[0].route_id_mask->parent,
					   data->target_flow_mask, data->target_flow_offset,
					   (data->target_flow_mask >> data->target_flow_offset));

		ret |= win2030_noc_reg_write(probe->filters[0].route_id_base,
					   data->target_flow_mask, data->target_flow_offset,
					   data->target_flow_idx);
	}
	//if (data->init_flow_idx != 0xff || data->target_flow_idx != 0xff) {
		/* Enable Filter LUT */
		/* To select only filter 0 */
		ret |= win2030_noc_reg_write(probe->filter_lut,
				PROBE_FILTERLUT_FILTERLUT_MASK,
				PROBE_FILTERLUT_FILTERLUT_OFFSET,
				0x1);
	//}
	/* Enable Alarm and statistics */
	ret |= win2030_noc_reg_write(probe->main_ctl, PROBE_MAINCTL_STATEN_MASK,
				PROBE_MAINCTL_STATEN_OFFSET, 1);

	ret |= win2030_noc_reg_write(probe->main_ctl, PROBE_MAINCTL_ALARMEN_MASK,
				PROBE_MAINCTL_ALARMEN_OFFSET, 1);

	/* Set period to max value */
	ret |= win2030_noc_reg_write(probe->stat_period,
				PROBE_STATPERIOD_STATPERIOD_MASK,
				PROBE_STATPERIOD_STATPERIOD_OFFSET,
				DURATION);

	/* Select sources for counter0 and 1 */
	//if (data->init_flow_idx != 0xff || data->target_flow_idx != 0xff) {
		ret |= win2030_noc_reg_write(probe->main_ctl,
				PROBE_MAINCTL_FILT_BYTE_ALWAYS_CAHINABLE_EN_MASK,
				PROBE_MAINCTL_FILT_BYTE_ALWAYS_CAHINABLE_EN_OFFSET,
				1);

		ret |=
		    win2030_noc_reg_write(probe->counters[0].source_event->parent,
				PROBE_COUNTERS_SRC_INTEVENT_MASK,
				/*FILT_BYTE = 0x14 */
				PROBE_COUNTERS_SRC_INTEVENT_OFFSET,
				0x14);
		ret |=
		    win2030_noc_reg_write(probe->counters[1].source_event->parent,
				PROBE_COUNTERS_SRC_INTEVENT_MASK,
				/* chain */
				PROBE_COUNTERS_SRC_INTEVENT_OFFSET,
				0x10);
	#if 0
	} else {
		ret |= win2030_noc_reg_write(probe->main_ctl,
				PROBE_MAINCTL_FILT_BYTE_ALWAYS_CAHINABLE_EN_MASK,
				PROBE_MAINCTL_FILT_BYTE_ALWAYS_CAHINABLE_EN_OFFSET,
				0);
		ret |=
		    win2030_noc_reg_write(probe->counters[0].source_event->parent,
				PROBE_COUNTERS_SRC_INTEVENT_MASK,
				/* BYTE */
				PROBE_COUNTERS_SRC_INTEVENT_OFFSET,
				0x8);
		ret |=
		    win2030_noc_reg_write(probe->counters[1].source_event->parent,
				PROBE_COUNTERS_SRC_INTEVENT_MASK,
				/* chain */
				PROBE_COUNTERS_SRC_INTEVENT_OFFSET,
				0x10);
	}
	#endif
	/* Set alarm mode */
	/* min */
	ret |= win2030_noc_reg_write(probe->counters[0].alarm_mode->parent,
			   PROBE_COUNTERS_ALARMMODE_COUNTERS_ALARMMODE_MASK,
			   PROBE_COUNTERS_ALARMMODE_COUNTERS_ALARMMODE_OFFSET,
			   1);
	return ret;
}

int win2030_noc_stat_tranc_probe_launch_measure(
			struct win2030_noc_device *noc_device,
			struct win2030_noc_stat_probe *stat_probe)
{
	struct win2030_noc_stat_measure *stat_measure;
	struct win2030_noc_probe *probe;
	int i, ret = 0;

	stat_measure = list_first_entry(&stat_probe->measure,
					struct win2030_noc_stat_measure, link);

	probe = stat_probe->probe;

	/* Reset Filters */
	for (i = 0; i < probe->nr_filters; i++) {
		/*select all kinds opcode*/
		ret |=
		    win2030_noc_reg_write(probe->trans_filters[i].op_code, 0xf, 0, 0xf);
	}

	/* Enable Alarm and statistics */
	ret |= win2030_noc_reg_write(probe->main_ctl, PROBE_MAINCTL_STATEN_MASK,
			PROBE_MAINCTL_STATEN_OFFSET, 1);

	ret |= win2030_noc_reg_write(probe->main_ctl, PROBE_MAINCTL_ALARMEN_MASK,
			PROBE_MAINCTL_ALARMEN_OFFSET, 1);

	/* Set period to max value */
	ret |= win2030_noc_reg_write(probe->stat_period,
			PROBE_STATPERIOD_STATPERIOD_MASK,
			PROBE_STATPERIOD_STATPERIOD_OFFSET,
			DURATION);

	/* map counters to trace port*/
	ret |= win2030_noc_reg_write(probe->main_ctl,
			PROBE_MAINCTL_FILT_BYTE_ALWAYS_CAHINABLE_EN_MASK,
			PROBE_MAINCTL_FILT_BYTE_ALWAYS_CAHINABLE_EN_OFFSET,
			1);

	for (i = 0; i < probe->nr_portsel; i++) {
		ret |=
		    win2030_noc_reg_write(probe->counters[i * 2].source_event->parent,
					PROBE_COUNTERS_SRC_INTEVENT_MASK,
					/* BYTE */
					PROBE_COUNTERS_SRC_INTEVENT_OFFSET,
					0x8);
		/* Set alarm mode min */
		ret |=
		    win2030_noc_reg_write(probe->counters[i * 2].alarm_mode->parent,
				   PROBE_COUNTERS_ALARMMODE_COUNTERS_ALARMMODE_MASK,
				   PROBE_COUNTERS_ALARMMODE_COUNTERS_ALARMMODE_OFFSET,
				   1);
		ret |=
		    win2030_noc_reg_write(probe->counters[i * 2 + 1].source_event->parent,
					PROBE_COUNTERS_SRC_INTEVENT_MASK,
					/* chain */
					PROBE_COUNTERS_SRC_INTEVENT_OFFSET,
					0x10);
	}
	return ret;
}

/**
 * Configure NOC registers from a stat_measure
 * @param noc_device A reference to the device
 * @param stat_probe The stat_probe to find the measure
 * @return 0 if ok, -EINVAL otherwise
 */

static int win2030_noc_stat_launch_measure(struct win2030_noc_device *noc_device,
				     struct win2030_noc_stat_probe *stat_probe)
{
	struct win2030_noc_stat_measure *stat_measure;
	struct win2030_noc_probe *probe;
	int ret = 0;

	stat_measure = list_first_entry(&stat_probe->measure,
					struct win2030_noc_stat_measure, link);

	dev_dbg_once(noc_device->dev, "stat measurment %s begin\n", stat_measure->data.name);
	probe = stat_probe->probe;
	/* Disable NOC */
	ret = win2030_noc_reg_write(probe->cfg_ctl, PROBE_CFGCTL_GLOBALEN_MASK,
				  PROBE_CFGCTL_GLOBALEN_OFFSET, 0);
	while (win2030_noc_reg_read(probe->cfg_ctl, PROBE_CFGCTL_ACTIVE_WIDTH,
				  PROBE_CFGCTL_ACTIVE_OFFSET) != 0)
		;
#if 0
	/*there is only one probe point, so there is no need to choose probe point*/
	if (stat_measure->idx_init_flow != 0xff) {
		/* Select probe TracePortSel if need filter*/
		ret |= win2030_noc_reg_write(probe->trace_port_sel,
				   PROBE_TRACEPORTSEL_TRACEPORTSEL_MASK,
				   PROBE_TRACEPORTSEL_TRACEPORTSEL_OFFSET,
				   stat_measure->idx_trace_port_sel);
	} else {
		/* Select counter PortSel if no need filter*/
		ret |= win2030_noc_reg_write(probe->counters[0].portsel->parent,
			   PROBE_COUNTERS_PORTSEL_COUNTERS_PORTSEL_MASK,
			   PROBE_COUNTERS_PORTSEL_COUNTERS_PORTSEL_OFFSET,
			   stat_measure->idx_trace_port_sel);
	}
#endif
	if (probe_t_pkt == probe->type) {
		ret |= win2030_noc_stat_packet_probe_launch_measure(noc_device, stat_probe);
	} else if (probe_t_trans == probe->type) {
		ret |= win2030_noc_stat_tranc_probe_launch_measure(noc_device, stat_probe);
	}

	/* Set alarm min value */
	ret |= win2030_noc_reg_write(probe->stat_alarm_min,
			   PROBE_STATALARMMIN_STATALARMMIN_MASK,
			   PROBE_STATALARMMIN_STATALARMMIN_OFFSET,
			   0xfffffff);

	ret |= win2030_noc_reg_write(probe->stat_alarm_min_high,
			   PROBE_STATALARMMIN_HIGH_STATALARMMIN_HIGH_MASK,
			   PROBE_STATALARMMIN_HIGH_STATALARMMIN_HIGH_OFFSET,
			   0xfffffff);

	/* Enable */
	ret |= win2030_noc_reg_write(probe->stat_alarm_en,
			   PROBE_STATALARMEN_STATALARMEN_MASK,
			   PROBE_STATALARMEN_STATALARMEN_OFFSET,
			   1);

	ret |= win2030_noc_reg_write(probe->cfg_ctl,
			   PROBE_CFGCTL_GLOBALEN_MASK,
			   PROBE_CFGCTL_GLOBALEN_OFFSET,
			   1);
	return ret;
}

void win2030_noc_stat_reset_statistics(struct device *_dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct win2030_noc_stat_probe *stat_probe = NULL;
	struct win2030_noc_stat_measure *stat_measure = NULL;
	unsigned long flags;
	int j;
	struct win2030_noc_probe *probe;

	list_for_each_entry(stat_probe, &noc_device->stat->probe, link) {
		probe = stat_probe->probe;
		list_for_each_entry(stat_measure, &stat_probe->measure, link) {
			for (j = 0; j < probe->nr_portsel; j++) {
				spin_lock_irqsave(&stat_measure->lock, flags);
				stat_measure->min[j] = -1;
				stat_measure->max[j] = 0;
				stat_measure->now[j] = 0;
				stat_measure->iteration[j] = 0;
				stat_measure->mean[j] = 0;
				spin_unlock_irqrestore(&stat_measure->lock,
						       flags);
			}

		}
	}
}

/**
 * Program all noc registers for all probes
 * @param _dev A reference to the device
 * @return 0
 */
int win2030_noc_stat_trigger(struct device *_dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct win2030_noc_stat_probe *stat_probe = NULL;
	struct win2030_noc_stat *noc_stat = noc_device->stat;
	struct list_head *ptr = NULL;
	unsigned ret;

	/* Loop over all probes */
	list_for_each(ptr, &noc_stat->probe) {
		stat_probe = list_entry(ptr, struct win2030_noc_stat_probe, link);
		/* Program first measurement */
		ret = win2030_noc_stat_launch_measure(noc_device, stat_probe);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 * Entry point for the NOC sniffer
 * @param _dev A reference to the device
 * @return 0 if ok, if error returns 0 but print an error message
 */
int win2030_noc_stat_init(struct device *_dev)
{
	struct win2030_noc_stat *noc_stat;
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct device_node *np = _dev->of_node;
	int stat_idx = 0;
	bool ret;
	char mystr[32];
	int err;

	/* Create a stat device. */
	noc_stat = kzalloc(sizeof(struct win2030_noc_stat), GFP_KERNEL);
	if (!noc_stat)
		return -ENOMEM;
	noc_device->stat = noc_stat;
	noc_stat->run = false;
	spin_lock_init(&noc_stat->lock);
	INIT_LIST_HEAD(&noc_stat->probe);

	/* Parse dts and build matrix node */
	do {
		scnprintf(mystr, ARRAY_SIZE(mystr), "stat,%d", stat_idx);
		ret = of_property_read_bool(np, mystr);
		if (ret == true) {
			stat_idx++;
			err = new_probe_and_measure(noc_device, np, mystr);
			if (err)
				dev_err(_dev,
					"Error when adding measure: %s!\n",
					mystr);
		}
	} while (ret == true);

	/* If no stat line in dts then we can release the noc_stat object */
	if (stat_idx == 0)
		goto no_sniffer;

	ret = of_property_read_u32(np, "clock,rate", &noc_stat->clock_rate);
	if (ret) {
		dev_err(_dev,
			"\"clock,rate\" property missing. Will use clk_get_rate() "
			"to discover clock rate.");
	}

	/* Trigger measurement */
	if (noc_stat->run) {
		ret = win2030_noc_stat_trigger(_dev);
		if (ret)
			dev_err(_dev,
				"Error when trigger stat measures!\n");
	}
	return 0;

no_sniffer:
	kfree(noc_stat);
	return -EINVAL;
}

/**
 * Interrupt processing when a measurement is done
 * @param noc_device A reference to the device
 * @param probe The probe for which a measurement is done
 */
void win2030_noc_stat_do_measure(struct win2030_noc_probe *probe)
{
	struct win2030_noc_device *noc_device = probe->parent;
	struct win2030_noc_stat_probe *stat_probe = NULL;
	struct win2030_noc_stat_measure *stat_measure;
	u64 counter = 0;
	unsigned ret;
	unsigned long flags;
	int j;

	/* Find which stat_probe is linked to the probe
	 * which generated the interrupt
	 */
	list_for_each_entry(stat_probe, &noc_device->stat->probe, link) {
		if (stat_probe->probe == probe)
			break;
	}

	/* Take measure object */
	stat_measure = list_first_entry(&stat_probe->measure,
				struct win2030_noc_stat_measure, link);

	for (j = 0; j < probe->nr_portsel; j++) {
		/* Read HW counters */
		counter = win2030_noc_bf_read(probe->counters[j * 2  + 1].value);
		counter = ((counter << 32)
			   | (win2030_noc_bf_read(probe->counters[j * 2].value)));

		/* Update stat_measure statistics */
		if (counter > stat_measure->max[j * 2]) {
			spin_lock_irqsave(&stat_measure->lock, flags);
			stat_measure->max[j * 2] = counter;
			spin_unlock_irqrestore(&stat_measure->lock, flags);
		}
		if (counter < stat_measure->min[j * 2]) {
			spin_lock_irqsave(&stat_measure->lock, flags);
			stat_measure->min[j * 2] = counter;
			spin_unlock_irqrestore(&stat_measure->lock, flags);
		}
		stat_measure->now[j * 2] = counter;
		/*if no traffic, don't update iteration and mean value*/
		if (0 == counter) {
			continue;
		}
		if (stat_measure->iteration[j * 2] != MAX_ITERATION) {
			spin_lock_irqsave(&stat_measure->lock, flags);
			stat_measure->iteration[j * 2] += 1;
			spin_unlock_irqrestore(&stat_measure->lock, flags);

			spin_lock_irqsave(&stat_measure->lock, flags);
			stat_measure->mean[j * 2] += counter;
			spin_unlock_irqrestore(&stat_measure->lock, flags);
		} else {
			dev_info(noc_device->dev, "TracePort %s iteration overflow, reset measure statistics!\n",
				stat_probe->probe->available_portsel[j]);
			spin_lock_irqsave(&stat_measure->lock, flags);
			stat_measure->iteration[j * 2] = 1;
			stat_measure->mean[j * 2] = 0;
			spin_unlock_irqrestore(&stat_measure->lock, flags);
		}
	}
	/* Program next measure */
	if (noc_device->stat->run == true) {
		/* Move to the next measure */
		spin_lock_irqsave(&stat_probe->lock, flags);
		list_move(&stat_measure->link, &stat_probe->measure);
		spin_unlock_irqrestore(&stat_probe->lock, flags);

		/* Program it */
		ret = win2030_noc_stat_launch_measure(noc_device, stat_probe);
		if (ret)
			dev_err(noc_device->dev,
				"Error when launch stat measures!\n");
	}
}
