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

#ifndef NOC_H_
#define NOC_H_

#define WIN2030_NOC_ERROR_REGISTERS_MAX 8
#define WIN2030_NOC_REG_ADDR(_reg)	((void __iomem *) (reg->base + reg->offset))
#define MAX_ITERATION		(1 << 28)	/* Arbitrary value */
#define DURATION		30
#define WIN2030_NOC_TRACE_PORT_MAX	3
#define WIN2030_NOC_BIN_CNT	4
#define SIZE_BIG_BUF	4096
#define SIZE_SMALL_BUF	1500

extern struct win2030_noc_control win2030_noc_ctrl;
extern int latency_bin[3];

struct win2030_bitfield {
	struct win2030_register *parent;
	const char *name;
	const char *description;
	unsigned offset;
	unsigned length;
	unsigned mask;
	const char **lut;
	int aperture_size;
	u64 *init_flow;
	u64 *target_flow;
	u64 *target_sub_range;
	u16 *aperture_idx;
	u64 *aperture_base;
	u32 sbm_id;
	struct list_head link;
	spinlock_t lock;
};

struct win2030_register {
	struct device *parent;
	void __iomem *base;
	const char *name;
	const char *description;
	unsigned offset;
	unsigned length;
	int aperture_link;
	int msb_link;
	struct list_head bitfields;
	struct list_head link;
	struct list_head parent_link;	//link register's parent(probe, filter or counter)
	spinlock_t lock;
	spinlock_t hw_lock;
};

struct win2030_noc_probe;

struct win2030_noc_stat_traceport_data {
	const char *name;
	u16 idx_trace_port_sel;
	char *init_flow_name;
	u8 init_flow_idx;
	unsigned init_flow_offset;
	unsigned init_flow_mask;
	char *target_flow_name;
	u8 target_flow_idx;
	unsigned target_flow_offset;
	unsigned target_flow_mask;
	int addrBase_low;
	int addrBase_high;
	int addrWindowSize;
	int Opcode;
	int Status;
	int Length;
	int Urgency;
};

/* One measure for the NOC sniffer */
struct win2030_noc_stat_measure {
	u64 min[WIN2030_NOC_TRACE_PORT_MAX];
	u64 max[WIN2030_NOC_TRACE_PORT_MAX];
	u64 mean[WIN2030_NOC_TRACE_PORT_MAX];
	u64 now[WIN2030_NOC_TRACE_PORT_MAX];
	unsigned iteration[WIN2030_NOC_TRACE_PORT_MAX];
	struct list_head link;
	spinlock_t lock;
	struct win2030_noc_stat_traceport_data data;
};

/* A set of statistics assigned to a probe
 * (as a probe can measure severals paths)
 */
struct win2030_noc_stat_probe {
	struct win2030_noc_probe *probe;
	struct list_head measure;
	struct list_head link;
	spinlock_t lock;
};

/* The main structure for the NOC sniffer */
struct win2030_noc_stat {
	struct list_head probe;
	struct dentry *dir;
	spinlock_t lock;
	bool run;
	unsigned clock_rate;
};

struct win2030_noc_filter {
	unsigned id;
	struct win2030_noc_probe *parent;
	struct win2030_register *route_id_base;
	struct win2030_bitfield *route_id_mask;
	struct win2030_bitfield *addr_base_low;
	struct win2030_bitfield *addr_base_high;
	struct win2030_bitfield *window_size;
	//struct win2030_register *security_base;
	//struct win2030_bitfield *security_mask;
	struct win2030_register *op_code;
	struct win2030_register *status;
	struct win2030_bitfield *length;
	struct win2030_bitfield *urgency;
	struct list_head register_link;
};

struct win2030_noc_trans_filter {
	unsigned id;
	struct win2030_noc_probe *parent;
	struct win2030_bitfield *mode;
	struct win2030_bitfield *addr_base_low;
	struct win2030_bitfield *addr_base_high;
	struct win2030_bitfield *window_size;
	struct win2030_register *op_code;
	struct win2030_bitfield *user_base;
	struct win2030_bitfield *user_mask;
	struct list_head register_link;
};

typedef enum {
	latency_t,
	pending_t,
}measure_type;

struct win2030_noc_prof_traceport_data {
	const char *name;
	int mode;
	int addrBase_low;
	int addrBase_high;
	int addrWindowSize;
	int Opcode;
};

/* One measure for the NOC prof */
struct win2030_noc_prof_measure {
	measure_type type;
	u64 min[WIN2030_NOC_TRACE_PORT_MAX][WIN2030_NOC_BIN_CNT];
	u64 max[WIN2030_NOC_TRACE_PORT_MAX][WIN2030_NOC_BIN_CNT];
	u64 mean[WIN2030_NOC_TRACE_PORT_MAX][WIN2030_NOC_BIN_CNT];
	u64 now[WIN2030_NOC_TRACE_PORT_MAX][WIN2030_NOC_BIN_CNT];
	unsigned iteration[WIN2030_NOC_TRACE_PORT_MAX];
	struct list_head link;
	spinlock_t lock;
	struct win2030_noc_prof_traceport_data data;
};

/* A set of statistics assigned to a probe
 * (as a probe can measure severals paths)
 */
struct win2030_noc_prof_probe {
	struct win2030_noc_probe *probe;
	struct list_head measure;
	struct list_head link;
	spinlock_t lock;
};

/* The main structure for the NOC profiler */
struct win2030_noc_prof {
	struct list_head prof_probe;
	struct dentry *dir;
	spinlock_t lock;
	bool run;
};

struct win2030_noc_trans_profiler {
	unsigned id;
	struct win2030_noc_probe *parent;
	struct win2030_bitfield *en;
	struct win2030_bitfield *mode;
	struct win2030_bitfield *observed_sel[WIN2030_NOC_TRACE_PORT_MAX];
	struct win2030_bitfield *n_tenure_lines[WIN2030_NOC_TRACE_PORT_MAX];
	struct win2030_bitfield *thresholds[WIN2030_NOC_TRACE_PORT_MAX][WIN2030_NOC_BIN_CNT];
	struct win2030_bitfield *over_flow_status;
	struct win2030_bitfield *over_flow_reset;
	struct win2030_bitfield *pending_event_mode;
	struct win2030_bitfield *pre_scaler;
	struct list_head register_link;
};

struct win2030_noc_counter {
	unsigned id;
	struct win2030_noc_probe *parent;
	struct win2030_bitfield *portsel;
	struct win2030_bitfield *alarm_mode;
	struct win2030_bitfield *source_event;
	struct win2030_bitfield *value;
	struct list_head register_link;
};

enum probe_t {
	probe_t_pkt = 0x8,
	probe_t_trans,
};

struct win2030_noc_probe {
	struct device *dev;
	const char *id;
	struct clk *clock;
	enum probe_t type;
	int nr_portsel;				/* How many trace port per probe */
	const char **available_portsel;
	struct win2030_register *main_ctl;
	struct win2030_register *cfg_ctl;
	//struct win2030_register *trace_port_sel;
	struct win2030_register *filter_lut;
	struct win2030_register *stat_period;
	struct win2030_register *stat_go;
	struct win2030_register *stat_alarm_max;
	struct win2030_register *stat_alarm_min;
	struct win2030_register *stat_alarm_min_high;
	struct win2030_register *stat_alarm_status;
	struct win2030_register *stat_alarm_en;
	struct list_head register_link;
	struct win2030_noc_device *parent;
	unsigned nr_counters;				/* How many counters per probe */
	struct win2030_noc_counter *counters;
	unsigned nr_filters;				/* How many filters per probe */
	struct win2030_noc_filter *filters;
	struct win2030_noc_trans_filter *trans_filters;
	unsigned nr_profilers;				/* How many profiler per probe */
	struct win2030_noc_trans_profiler *trans_profilers;
	struct tasklet_struct tasklet;
	struct list_head link;
	int 	stat_irq;
	void __iomem *hw_base;
};

struct dev_qos_cfg {
	struct list_head list;
	const char *name;
	void __iomem *hw_base;
	struct regcfg *config;
	int noc_owner;
	bool run;
	struct list_head register_link;
};

struct regcfg {
	struct list_head list;
	unsigned offset;
	unsigned value;
};

struct win2030_noc_device {
	struct device *dev;
	void __iomem *hw_base;
	int err_irq;
	struct list_head err_queue;
	spinlock_t lock;
	struct list_head registers;
	int error_logger_cnt;
	u32 *err_log_lut;
	struct win2030_register *error_registers[WIN2030_NOC_ERROR_REGISTERS_MAX];
	struct dentry *dir;
	struct win2030_noc_stat *stat;
	struct win2030_noc_prof *prof;
	struct list_head qos_list;
	int qos_run;
	bool trap_on_error;
	struct list_head probes;
	struct win2030_noc_sideband_magr *sbm;
};

struct win2030_noc_error {
	struct list_head link;
	u64 timestamp;
	unsigned err[WIN2030_NOC_ERROR_REGISTERS_MAX];
};

struct win2030_noc_sideband_magr {
	struct device 		*dev;
	void __iomem 		*hw_base;
	struct win2030_register *SenseIn0;
	struct list_head 	link;
};

struct win2030_noc_control {
	struct dentry *win2030_noc_root_debug_dir;
	struct list_head sbm_link;
};

int win2030_noc_debug_init(struct device *);
int win2030_noc_stat_init(struct device *);
int win2030_noc_prof_init(struct device *_dev);

void win2030_noc_stat_do_measure(struct win2030_noc_probe *);
void win2030_noc_prof_do_measure(struct win2030_noc_probe *probe);
int win2030_noc_stat_debugfs_init(struct device *);
int win2030_noc_prof_debugfs_init(struct device *_dev);
int win2030_noc_stat_trigger(struct device *_dev);
void win2030_noc_stat_reset_statistics(struct device *_dev);
int win2030_noc_prof_trigger(struct device *_dev);
void win2030_noc_prof_reset_statistics(struct device *_dev);


unsigned win2030_noc_reg_read(struct win2030_register *reg, unsigned mask,
			unsigned offset);
int win2030_noc_reg_write(struct win2030_register *reg, unsigned mask,
			unsigned offset, unsigned value);
int win2030_bitfield_read(struct win2030_bitfield *bf, unsigned *value);
int win2030_noc_init_sideband_mgr(struct device *_dev);
int win2030_noc_sideband_mgr_debug_init(struct device *_dev);
int win2030_noc_debug_reg_init(struct win2030_register *reg, struct dentry *dir);

struct win2030_register *win2030_new_register(struct device *parent,
			const char *name,
			void __iomem *base,
			unsigned offset,
			unsigned width);

const char **win2030_get_strings_from_dts(struct device *_dev,
			struct device_node *np,
			const char *property_name);

struct win2030_bitfield *win2030_new_bitfield(struct win2030_register *reg,
			 const char *name,
			 unsigned offset,
			 unsigned width,
			 const char **lut);

int noc_error_dump(char *buf,
		struct win2030_noc_device *noc_device,
		struct win2030_noc_error *noc_err);

int noc_device_qos_set(struct win2030_noc_device *noc_device, bool is_enalbe);

#endif /* NOC_H_ */
