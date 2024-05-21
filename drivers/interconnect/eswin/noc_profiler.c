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

void zebu_stop(void);

int latency_bin[3] = {256, 320, 384};

/**
 * Configure NOC registers from a prof_measure
 * @param noc_device A reference to the device
 * @param prof_probe The prof_probe to find the measure
 * @return 0 if ok, -EINVAL otherwise
 */
static int win2030_noc_prof_launch_measure(struct win2030_noc_device *noc_device,
				     struct win2030_noc_prof_probe *prof_probe)
{
	struct win2030_noc_prof_measure *prof_measure;
	struct win2030_noc_probe *probe;
	int i, ret = 0;
	struct win2030_noc_trans_profiler *profiler;
	struct win2030_noc_prof_traceport_data *data;

	prof_measure = list_first_entry(&prof_probe->measure,
				struct win2030_noc_prof_measure, link);

	data = &prof_measure->data;
	dev_dbg_once(noc_device->dev, "measurment %s begin, type %s\n", data->name,
		pending_t == prof_measure->type ? "pending" : "latency");

	//zebu_stop();
	probe = prof_probe->probe;

	profiler = &probe->trans_profilers[0];

	/*
	  Disable probe
	  This will reset all fileds of register MailCtl
	*/
	ret = win2030_noc_reg_write(probe->cfg_ctl, PROBE_CFGCTL_GLOBALEN_MASK,
				  PROBE_CFGCTL_GLOBALEN_OFFSET, 0);
	while (win2030_noc_reg_read(probe->cfg_ctl, PROBE_CFGCTL_ACTIVE_WIDTH,
				  PROBE_CFGCTL_ACTIVE_OFFSET) != 0) {
		;
	}
	/* --> to program the transaction filter,
	*/
	for (i = 0; i < probe->nr_filters; i++) {
		ret |= win2030_noc_reg_write(probe->trans_filters[i].mode->parent,
			PROBE_TRANS_FILTERS_MODE_FILTERS_MODE_MASK,
			PROBE_TRANS_FILTERS_MODE_FILTERS_MODE_OFFSET,
			data->mode);

		ret |= win2030_noc_reg_write(probe->trans_filters[i].addr_base_low->parent,
			PROBE_TRANS_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_MASK,
			PROBE_TRANS_FILTERS_ADDRBASE_LOW_FILTERS_ADDRBASE_LOW_OFFSET,
			data->addrBase_low);

		ret |= win2030_noc_reg_write(probe->trans_filters[i].addr_base_high->parent,
			PROBE_TRANS_FILTERS_ADDRBASE_HIGH_FILTERS_ADDRBASE_HIGH_MASK,
			PROBE_TRANS_FILTERS_ADDRBASE_HIGH_FILTERS_ADDRBASE_HIGH_OFFSET,
			data->addrBase_high);

		ret |=
			win2030_noc_reg_write(probe->trans_filters[i].window_size->parent,
			PROBE_FILTERS_WINDOWSIZE_FILTERS_WINDOWSIZE_MASK,
			PROBE_FILTERS_WINDOWSIZE_FILTERS_WINDOWSIZE_OFFSET,
			data->addrWindowSize);

		ret |= win2030_noc_reg_write(probe->trans_filters[i].op_code,
			PROBE_TRANS_FILTERS_OPCODE_RDEN_MASK,
			PROBE_TRANS_FILTERS_OPCODE_RDEN_OFFSET,
			FIELD_GET(GENMASK(0, 0), data->Opcode));

		ret |= win2030_noc_reg_write(probe->trans_filters[i].op_code,
			PROBE_TRANS_FILTERS_OPCODE_WREN_MASK,
			PROBE_TRANS_FILTERS_OPCODE_WREN_OFFSET,
			FIELD_GET(GENMASK(1, 1), data->Opcode));

	}

	/*--> to program the transaction profiler unit*/
	/* Disable profiler */
	ret |= win2030_noc_reg_write(profiler->en->parent,
			PROBE_TRANS_PROFILER_EN_PROFILER_EN_MASK,
			PROBE_TRANS_PROFILER_EN_PROFILER_EN_OFFSET,
			0);

	/* Select TracePort*/
	for (i = 0; i < probe->nr_portsel; i++) {
		ret |= win2030_noc_reg_write(profiler->observed_sel[i]->parent,
			PROBE_TRACEPORTSEL_TRACEPORTSEL_MASK,
			PROBE_TRACEPORTSEL_TRACEPORTSEL_OFFSET,
			i);
	}
	if (pending_t == prof_measure->type) {
		/*Set Profiler Mode to Pending*/
		ret |= win2030_noc_reg_write(profiler->mode->parent,
			PROBE_TRACEPORTSEL_TRACEPORTSEL_MASK,
			PROBE_TRACEPORTSEL_TRACEPORTSEL_OFFSET,
			1);
	} else {
		/*Set Profiler Mode to Delay*/
		ret |= win2030_noc_reg_write(profiler->mode->parent,
				PROBE_TRACEPORTSEL_TRACEPORTSEL_MASK,
				PROBE_TRACEPORTSEL_TRACEPORTSEL_OFFSET,
				0);
	}

	/* Each line has 16 counters*/
	for (i = 0; i < probe->nr_portsel - 1; i++) {
		ret |= win2030_noc_reg_write(profiler->n_tenure_lines[i]->parent,
			PROBE_TRANS_PROFILER_N_TENURE_LINES_PROFILER_N_TENURE_LINES_MASK,
			PROBE_TRANS_PROFILER_N_TENURE_LINES_PROFILER_N_TENURE_LINES_OFFSET,
			2);
	}
	if (pending_t == prof_measure->type) {
		/* Thresholds*/
		for (i = 0; i < probe->nr_portsel; i++) {
			/*
			   for read/wrtie transration, the pending binis 2/5/10/10 up cycles
			*/
			ret |= win2030_noc_reg_write(profiler->thresholds[i][0]->parent,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_MASK,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_OFFSET,
					2);
			ret |= win2030_noc_reg_write(profiler->thresholds[i][1]->parent,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_MASK,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_OFFSET,
					5);
			ret |= win2030_noc_reg_write(profiler->thresholds[i][2]->parent,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_MASK,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_OFFSET,
					10);
		}
	} else {
		/* Thresholds*/
		for (i = 0; i < probe->nr_portsel; i++) {
			/*
			   for read/wrtie transration, the delay bin is 30/100/200/200 up cycles
			*/
			ret |= win2030_noc_reg_write(profiler->thresholds[i][0]->parent,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_MASK,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_OFFSET,
					latency_bin[0]);
			ret |= win2030_noc_reg_write(profiler->thresholds[i][1]->parent,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_MASK,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_OFFSET,
					latency_bin[1]);
			ret |= win2030_noc_reg_write(profiler->thresholds[i][2]->parent,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_MASK,
					PROBE_TRANS_PROFILER_THRESHOLDS_PROFILER_THRESHOLDS_OFFSET,
					latency_bin[2]);
		}
	}

	/*--> to program the packet probe statistics subsystemt*/
	for (i = 0; i < probe->nr_counters; i++) {
		/*maping counters to count profiler outputs*/
		ret |= win2030_noc_reg_write(probe->counters[i].source_event->parent,
			PROBE_COUNTERS_SRC_INTEVENT_MASK,
			PROBE_COUNTERS_SRC_INTEVENT_OFFSET,
			0x20 + i);

		/*set counters alarm mode to Min,
		  If the value of the counter is less than the StatAlarmMin register at the
		  dump period, the StatAlarmStatus bit is set
		*/
		ret |= win2030_noc_reg_write(probe->counters[i].alarm_mode->parent,
			PROBE_COUNTERS_ALARMMODE_COUNTERS_ALARMMODE_MASK,
			PROBE_COUNTERS_ALARMMODE_COUNTERS_ALARMMODE_OFFSET,
			1);
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

	/* Set period to max value */
	ret |= win2030_noc_reg_write(probe->stat_period,
			PROBE_STATPERIOD_STATPERIOD_MASK,
			PROBE_STATPERIOD_STATPERIOD_OFFSET,
			DURATION);

	/* Enable Alarm */
	ret |= win2030_noc_reg_write(probe->main_ctl,
			PROBE_MAINCTL_ALARMEN_MASK,
			PROBE_MAINCTL_ALARMEN_OFFSET,
			1);

	/* Set field StatEn of register MainCtl to 1 to enable the statistics counters to count delay events.*/
	ret |= win2030_noc_reg_write(probe->main_ctl,
			PROBE_MAINCTL_STATEN_MASK,
			PROBE_MAINCTL_STATEN_OFFSET,
			1);

	/*--> to enable the transaction probe*/
	/* Set field GlobalEn of register CfgCtl to 1 to enable the statistics counters. */
	ret |= win2030_noc_reg_write(probe->cfg_ctl,
			   PROBE_CFGCTL_GLOBALEN_MASK,
			   PROBE_CFGCTL_GLOBALEN_OFFSET,
			   1);

	/* Set register En to 1 to enable the transaction profiling counters.. */
	ret |= win2030_noc_reg_write(profiler->en->parent,
			PROBE_TRANS_PROFILER_EN_PROFILER_EN_MASK,
			PROBE_TRANS_PROFILER_EN_PROFILER_EN_OFFSET,
			1);
	return ret;
}

void win2030_noc_prof_reset_statistics(struct device *_dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct win2030_noc_prof_probe *prof_probe = NULL;
	struct win2030_noc_probe *noc_probe = NULL;
	struct win2030_noc_prof_measure *prof_measure = NULL;
	struct win2030_noc_prof *noc_prof = noc_device->prof;
	int i, j;
	unsigned long flags;

	list_for_each_entry(prof_probe, &noc_prof->prof_probe, link) {
		noc_probe = prof_probe->probe;
		list_for_each_entry(prof_measure, &prof_probe->measure, link) {
			for (j = 0; j < noc_probe->nr_portsel; j++) {
				spin_lock_irqsave(&prof_measure->lock, flags);
				prof_measure->iteration[j] = 0;
				for (i = 0; i < WIN2030_NOC_BIN_CNT; i++) {
					prof_measure->min[j][i] = -1;
					prof_measure->max[j][i] = 0;
					prof_measure->now[j][i] = 0;
					prof_measure->mean[j][i] = 0;
				}
				spin_unlock_irqrestore(&prof_measure->lock, flags);
			}
		}
	}
}

/**
 * trigger all noc prof measurment for all probes
 * @param _dev A reference to the device
 * @return 0
 */
int win2030_noc_prof_trigger(struct device *_dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct win2030_noc_prof_probe *prof_probe = NULL;
	struct win2030_noc_prof *noc_prof = noc_device->prof;
	unsigned ret;

	//zebu_stop();
	/* Loop over all probes */
	list_for_each_entry(prof_probe, &noc_prof->prof_probe, link) {
		/* lunch the first measurement */
		ret = win2030_noc_prof_launch_measure(noc_device, prof_probe);
		if (ret) {
			dev_err(noc_device->dev,
				"Error when trigger prof measures, probe %s!\n",
				prof_probe->probe->id);
			return ret;
		}
	}
	return 0;
}

/**
 * Create a new prof_measure from parsed dts entry
 * @param idx_trace_port_sel First entry in the dts line latency,x as index
 * @return The created win2030_noc_prof_measure object
 */
static struct win2030_noc_prof_measure *win2030_noc_prof_crate_measure(
				struct win2030_noc_prof_probe *prof_probe)
{
	struct win2030_noc_prof_measure *prof_measure;
	unsigned long flags;

	prof_measure = kzalloc(sizeof(struct win2030_noc_prof_measure),
			       GFP_KERNEL);
	if (!prof_measure)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&prof_measure->link);
	spin_lock_init(&prof_measure->lock);

	spin_lock_irqsave(&prof_probe->lock, flags);
	list_add_tail(&prof_measure->link, &prof_probe->measure);
	spin_unlock_irqrestore(&prof_probe->lock, flags);

	return prof_measure;
}

/**
 * Create a new prof_probe. A prof_probe contains all measurements which
 * are linked to the same probe
 * @param noc_prof the prof_probe will be attached to noc_prof
 * @param probe the prof_probe will refer to probe
 * @return The created xgold_noc_stat_probe object
 */
static struct win2030_noc_prof_probe *win2030_noc_prof_new_probe(struct win2030_noc_prof
						   *noc_prof,
						   struct win2030_noc_probe
						   *probe)
{
	struct win2030_noc_prof_probe *prof_probe;
	unsigned long flags;

	prof_probe = kzalloc(sizeof(struct win2030_noc_prof_probe), GFP_KERNEL);
	if (!prof_probe)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&prof_probe->measure);
	INIT_LIST_HEAD(&prof_probe->link);
	spin_lock_init(&prof_probe->lock);

	spin_lock_irqsave(&noc_prof->lock, flags);
	list_add_tail(&prof_probe->link, &noc_prof->prof_probe);
	spin_unlock_irqrestore(&noc_prof->lock, flags);
	prof_probe->probe = probe;

	return prof_probe;
}

enum {
	FIELD_ID_PORT = 0,
	FIELD_ID_MODE,
	FIELD_ID_ADDR,
	FIELD_ID_SIZE,
	FIELD_ID_OPCODE,
	FIELD_ID_MAX,
};

static int win2030_noc_prof_parse_traceport_field(struct device *dev,
	const char *str, int field_id,
	struct win2030_noc_prof_traceport_data *data)
{
	u64 addr;
	u64 size;
	int ret;

	switch (field_id) {
	case FIELD_ID_MODE:
		if (0 == strcmp(str, "Mode:latency")) {
			data->mode = 1;
		} else if (0 == strcmp(str, "Mode:handshake")) {
			data->mode = 0;
		} else {
			dev_err(dev, "get unknown mode data %s when parsing traceport dts!\n", str);
			return -EINVAL;
		}
		break;
	case FIELD_ID_ADDR:
		ret = kstrtou64(&str[strlen("AddrBase:")], 0, &addr);
		if (ret) {
			dev_err(dev, "get unknown addr data %s when parsing traceport dts!\n", str);
			return ret;
		}
		data->addrBase_low = FIELD_GET(GENMASK(31, 0), addr);
		data->addrBase_high = FIELD_GET(GENMASK(40, 32), addr);
		break;
	case FIELD_ID_SIZE:
		ret = kstrtou64(&str[strlen("AddrSize:")], 0, &size);
		if (ret) {
			dev_err(dev, "get unknown mask data %s when parsing traceport dts!\n", str);
			return ret;
		}
		data->addrWindowSize = size;
		data->addrWindowSize &= PROBE_FILTERS_WINDOWSIZE_FILTERS_WINDOWSIZE_MASK;
		break;
	case FIELD_ID_OPCODE:
		if (0 == strcmp(str, "Opcode:RdWr")) {
			data->Opcode = 3;
		} else if (0 == strcmp(str, "Opcode:Rd")) {
			data->Opcode = 1;
		} else if (0 == strcmp(str, "Opcode:Wr")) {
			data->Opcode = 2;
		} else {
			dev_err(dev, "get unknown Opcode data %s when parsing traceport dts!\n", str);
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
		break;
	}
	return 0;
}

static int win2030_noc_prof_parse_traceport_dts(struct win2030_noc_device *noc_device,
	struct device_node *np, const char *name, struct win2030_noc_prof_traceport_data *data)
{
	struct property *prop;
	struct device *dev = noc_device->dev;
	int lg;
	int index = 0;
	const char *str[FIELD_ID_MAX] = {NULL}, *s;
	int ret;
	const char *field[] = {"TracePort:", "Mode:", "AddrBase:", "AddrSize:", "Opcode:"};
	int i, j;

	/* Count number of strings per measurement
	  (normally 1: TracePortSel)
	 */
	lg = of_property_count_strings(np, name);
	if (lg > FIELD_ID_MAX || lg < 1)
		return -EINVAL; /* Invalid data, let's skip this measurement */

	of_property_for_each_string(np, name, prop, s)
		str[index++] = s;

	data->name = &str[0][strlen("TracePort:")];
	/*if user not set, default set as below
		Mode: 1, which means latency
		addrBase: 0, which means all address
		addrWindowSize : 0, which means all range
		Opcode: 3, which means enable both read and write
	*/
	data->mode = 1;
	data->addrBase_low = 0;
	data->addrBase_high = 0;
	data->addrWindowSize = 0x3f;
	data->Opcode = 3;

	for (i = 1; i < lg; i++) {
		for (j = 0; j < ARRAY_SIZE(field); j++) {
			if (0 == strncmp(str[i], field[j], strlen(field[j]))) {
				ret = win2030_noc_prof_parse_traceport_field(dev, str[i], j, data);
				if (ret) {
					dev_err(dev, "error when parsing traceport dts, ret %d!\n", ret);
					return ret;
				}
			}
		}
	}
	dev_dbg(dev, "get traceport data, name: %s\n"
		"\t\t\t\t Mode: %d, addrBase_low: 0x%x, addrBase_high: 0x%x\n"
		"\t\t\t\t addrWindowSize: 0x%x, Opcode: 0x%x\n",data->name, data->mode, data->addrBase_low,
		data->addrBase_high, data->addrWindowSize, data->Opcode);
	return 0;
}

/**
 * From one line of dts, add a new prof_measure and a prof_probe (if needed)
 * @param noc_device A ref to the device
 * @param np A ref to the dts node
 * @param name the line of the dts
 * @param type profiler measurment type
 * @return
 */
static int win2030_noc_prof_new_measure(struct win2030_noc_device *noc_device,
				 struct device_node *np, const char *name, measure_type type)
{
	int ret;
	struct win2030_noc_prof_probe *prof_probe = NULL;
	struct win2030_noc_prof *noc_prof = noc_device->prof;
	struct win2030_noc_probe *noc_probe = NULL;
	struct win2030_noc_prof_measure *prof_measure;
	struct win2030_noc_prof_traceport_data data;
	bool found;

	ret = win2030_noc_prof_parse_traceport_dts(noc_device, np, name, &data);
	if (ret) {
		dev_err(noc_device->dev, "error when parsing traceport dts, ret %d!\n", ret);
		return ret;
	}

	/* Search which probe can be used first value in dts */
	found = false;
	list_for_each_entry(noc_probe, &noc_device->probes, link) {
		if (strcmp(data.name, noc_probe->id) == 0) {
			found = true;
			break;
		}
	}

	if (false == found)
		return -EINVAL;	/* Invalid data, let's skip this measurement. */

	/* Look if the prof probe was already existing */
	found = false;
	list_for_each_entry(prof_probe, &noc_prof->prof_probe, link) {
		if (prof_probe->probe == noc_probe) {
			found = true;
			break;
		}
	}
	if (false == found) {
		/* Not found, we must create a prof_probe */
		prof_probe = win2030_noc_prof_new_probe(noc_prof, noc_probe);
		if (IS_ERR(prof_probe))
			goto error;

	}
	/* create a new measure */
	prof_measure = win2030_noc_prof_crate_measure(prof_probe);
	if (IS_ERR(prof_measure))
		goto error;

	prof_measure->type = type;
	memcpy(&prof_measure->data, &data, sizeof(struct win2030_noc_prof_traceport_data));
	return 0;
error:
	return -ENOMEM;
}

/**
 * Entry point for the NOC profiler
 * @param _dev A reference to the device
 * @return 0 if ok, if error returns 0 but print an error message
 */
int win2030_noc_prof_init(struct device *_dev)
{
	struct win2030_noc_prof *noc_prof;
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct device_node *np = _dev->of_node;
	int latency_idx = 0;
	int pending_idx = 0;
	bool ret;
	char mystr[32];
	int err;

	/* Create a prof device. */
	noc_prof = kzalloc(sizeof(struct win2030_noc_prof), GFP_KERNEL);
	if (!noc_prof)
		return -ENOMEM;
	noc_device->prof = noc_prof;
	noc_prof->run = false;
	spin_lock_init(&noc_prof->lock);
	INIT_LIST_HEAD(&noc_prof->prof_probe);

	/* Parse dts and build matrix node */
	do {
		scnprintf(mystr, ARRAY_SIZE(mystr), "latency,%d", latency_idx);
		ret = of_property_read_bool(np, mystr);
		if (ret == true) {
			latency_idx++;
			err = win2030_noc_prof_new_measure(noc_device, np, mystr, latency_t);
			if (err)
				dev_err(_dev, "Error when adding latency measurment: %s!\n", mystr);
		} else {
			scnprintf(mystr, ARRAY_SIZE(mystr), "pending,%d", pending_idx);
			ret = of_property_read_bool(np, mystr);
			if (ret == true) {
				pending_idx++;
				err = win2030_noc_prof_new_measure(noc_device, np, mystr, pending_t);
				if (err)
					dev_err(_dev, "Error when adding pending measurment: %s!\n", mystr);
			}
		}
	} while (ret == true);

	/* If no prof line in dts then we can release the noc_prof object */
	if (latency_idx == 0 && pending_idx == 0)
		goto no_profiler;

	/* Trigger measurement */
	if (noc_prof->run) {
		ret = win2030_noc_prof_trigger(_dev);
		if (ret)
			dev_err(_dev,
				"Error when configuring prof measures!\n");
	}
	return 0;

no_profiler:
	kfree(noc_prof);
	return -EINVAL;
}

/**
 * Interrupt processing when a measurement is done
 * @param noc_device A reference to the device
 * @param probe The probe for which a measurement is done
 */
void win2030_noc_prof_do_measure(struct win2030_noc_probe *probe)
{
	struct win2030_noc_device *noc_device = probe->parent;
	struct win2030_noc_prof_probe *prof_probe = NULL;
	struct win2030_noc_prof_measure *prof_measure;
	unsigned counter[WIN2030_NOC_BIN_CNT] = {0};
	unsigned ret;
	unsigned long flags;
	int i, j;
	u64 total = 0;
	int percentage = 0;

	/* Find which prof_probe is linked to the probe
	 * which generated the interrupt
	 */
	list_for_each_entry(prof_probe, &noc_device->prof->prof_probe, link) {
		if (prof_probe->probe == probe)
			break;
	}

	/* Take measure object */
	prof_measure = list_first_entry(&prof_probe->measure,
				struct win2030_noc_prof_measure, link);
	for (j = 0; j < probe->nr_portsel; j++) {
		total = 0;
		for (i = 0; i < WIN2030_NOC_BIN_CNT; i++) {
			/**
			  Read HW counters
			*/
			ret = win2030_bitfield_read(probe->counters[j * 4 + i].value, &counter[i]);
			WARN_ON(0 != ret);
			total += counter[i];
		}
		spin_lock_irqsave(&prof_measure->lock, flags);
		if (0 == total) {
			/*if total == 0, each bin's percentage will be 0*/
			total = (-1U);
		} else {
			/*
			   only update iteration when there are traffic,
			   otherwize the mean value will be calculate by a wrong iteration number.
			*/
			if (prof_measure->iteration[j] != MAX_ITERATION) {
				prof_measure->iteration[j] += 1;
			} else {
				/*OverFlow*/
				dev_info(noc_device->dev, "TracePort %s iteration overflow, reset measure statistics!\n",
					prof_probe->probe->available_portsel[j]);
				prof_measure->iteration[j] = 1;
				for (i = 0; i < WIN2030_NOC_BIN_CNT; i++) {
					prof_measure->mean[j][i] = 0;
				}
			}
		}
		for (i = 0; i < WIN2030_NOC_BIN_CNT; i++) {
			/* calculate each bin's percentage in the total traffic*/
			percentage = counter[i] * 100 / total;
			/* Update prof_measure statistics */
			if (percentage > prof_measure->max[j][i]) {
				prof_measure->max[j][i] = percentage;
			}
			if (percentage < prof_measure->min[j][i]) {
				prof_measure->min[j][i] = percentage;
			}
			prof_measure->now[j][i] = percentage;
			prof_measure->mean[j][i] += percentage;
		}
		spin_unlock_irqrestore(&prof_measure->lock, flags);
	}

	/* Program next measure */
	if (noc_device->prof->run == true) {
		/* Move to the next measure */
		spin_lock_irqsave(&prof_probe->lock, flags);
		list_move(&prof_measure->link, &prof_probe->measure);
		spin_unlock_irqrestore(&prof_probe->lock, flags);

		/* Program it */
		ret = win2030_noc_prof_launch_measure(noc_device, prof_probe);
		if (ret)
			dev_err(noc_device->dev, "Error when configuring prof measures!\n");
	}
}
