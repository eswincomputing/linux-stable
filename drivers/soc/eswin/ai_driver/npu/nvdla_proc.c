// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe root complex driver
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
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include "hetero_ioctl.h"
#include "internal_interface.h"
#include "hetero_perf.h"

static struct proc_dir_entry *proc_esnpu;
static int g_perf = 0;
static s16 *g_cfg_seq[2][NUM_OP_TYPE];
static u16 total_op_num[2];
static u16 op_num[2][NUM_OP_TYPE];
static spinlock_t proc_lock[2];
static host_node_t *g_host_node[2] = { NULL };
static wait_queue_head_t g_perf_wait_list[2];
static u32 g_stat_titok[2] = { -1 };
static u32 g_die_perf[2] = { 0 };

void handle_perf_switch(struct nvdla_device *ndev, bool enable)
{
	struct win_engine *engine;
	unsigned long flags;

	spin_lock_irqsave(&proc_lock[ndev->numa_id], flags);

	engine = (struct win_engine *)ndev->win_engine;
	engine->perf_switch = enable ? 1 : 0;
	g_perf = engine->perf_switch;
	if (g_perf == 0) {
		memset(g_cfg_seq[ndev->numa_id][IDX_START], 0,
		       sizeof(s16) * MAX_OP_NUM);
		memset(op_num[ndev->numa_id], 0, sizeof(u16) * NUM_OP_TYPE);
		g_host_node[ndev->numa_id] = NULL;
		g_stat_titok[ndev->numa_id] = -1;
		dla_debug("perf switch to off.\n");
	}
	spin_unlock_irqrestore(&proc_lock[ndev->numa_id], flags);
}

int get_perf_data(struct nvdla_device *ndev)
{
#if NPU_PERF_STATS > 1
	int j, k;
	int tiktok = -1;
	npu_e31_perf_t *op_stats;
	s16 op_idx;
	struct win_engine *engine;
	int numa_id = ndev->numa_id;
	unsigned long flags;

	engine = (struct win_engine *)ndev->win_engine;
	if (!engine->perf_switch) {
		dla_error("%s, %d, perf not turn on.\n", __func__, __LINE__);
		return -EFAULT;
	}

	if (g_stat_titok[numa_id] == -1) {
		wait_event_interruptible(g_perf_wait_list[numa_id],
					 g_stat_titok[numa_id] != -1);
	}

	spin_lock_irqsave(&proc_lock[numa_id], flags);
	tiktok = g_stat_titok[numa_id];
	op_stats = g_host_node[numa_id]->model_stat[tiktok].op_stats;
	for (j = 0; j < NUM_OP_TYPE; j++) {
		for (k = 0; k < op_num[numa_id][j]; k++) {
			op_idx = g_cfg_seq[numa_id][j][k];
			op_stats[op_idx].Die = numa_id;
		}
	}
	dla_debug("get tiktok=%u perf data.\n", tiktok);
	memcpy((npu_e31_perf_t *)engine->perf_data_buf, op_stats,
	       sizeof(npu_e31_perf_t) * MAX_OP_NUM);
	g_stat_titok[numa_id] = -1;
	memset(g_host_node[numa_id]->model_stat[tiktok].op_stats, 0,
	       sizeof(npu_e31_perf_t) * MAX_OP_NUM);
	spin_unlock_irqrestore(&proc_lock[numa_id], flags);
#endif
	return 0;
}

void refresh_op_statistic(struct win_executor *executor,
			  struct win_engine *engine, u32 tiktok)
{
	int numa_id;
	struct nvdla_device *ndev;
	int i;
	unsigned long flags;

	ndev = engine->nvdla_dev;
	numa_id = ndev->numa_id;
	dla_debug("%s, %d, numa_id=%d, tiktok=%d.\n", __func__, __LINE__,
		  numa_id, tiktok);
	spin_lock_irqsave(&proc_lock[numa_id], flags);
	g_cfg_seq[numa_id][IDX_EDMA] = g_cfg_seq[numa_id][IDX_START];
	g_cfg_seq[numa_id][IDX_CONV] =
		g_cfg_seq[numa_id][IDX_EDMA] + executor->op_num[IDX_EDMA];
	g_cfg_seq[numa_id][IDX_SDP] =
		g_cfg_seq[numa_id][IDX_CONV] + executor->op_num[IDX_CONV];
	g_cfg_seq[numa_id][IDX_PDP] =
		g_cfg_seq[numa_id][IDX_SDP] + executor->op_num[IDX_SDP];
	g_cfg_seq[numa_id][IDX_RUBIK] =
		g_cfg_seq[numa_id][IDX_PDP] + executor->op_num[IDX_PDP];
	g_cfg_seq[numa_id][IDX_KMD_DSP0] =
		g_cfg_seq[numa_id][IDX_RUBIK] + executor->op_num[IDX_RUBIK];
	g_cfg_seq[numa_id][IDX_KMD_DSP1] = g_cfg_seq[numa_id][IDX_KMD_DSP0] +
					   executor->op_num[IDX_KMD_DSP0];
	g_cfg_seq[numa_id][IDX_KMD_DSP2] = g_cfg_seq[numa_id][IDX_KMD_DSP1] +
					   executor->op_num[IDX_KMD_DSP1];
	g_cfg_seq[numa_id][IDX_KMD_DSP3] = g_cfg_seq[numa_id][IDX_KMD_DSP2] +
					   executor->op_num[IDX_KMD_DSP2];
	g_cfg_seq[numa_id][IDX_EVENT_SINK] = g_cfg_seq[numa_id][IDX_KMD_DSP3] +
					     executor->op_num[IDX_KMD_DSP3];
	g_cfg_seq[numa_id][IDX_EVENT_SOURCE] =
		g_cfg_seq[numa_id][IDX_EVENT_SINK] +
		executor->op_num[IDX_EVENT_SINK];

	for (i = 0; i < NUM_OP_TYPE; i++) {
		op_num[numa_id][i] = executor->op_num[i];
		if (executor->op_num[i] != 0) {
			memcpy(g_cfg_seq[numa_id][i], executor->cfg_seq[i],
			       sizeof(s16) * executor->op_num[i]);
		}
	}
	total_op_num[numa_id] = executor->total_op_num;
	g_host_node[numa_id] = engine->host_node;
	g_stat_titok[numa_id] = tiktok;
	g_die_perf[numa_id] = 1;
	dla_debug("refresh tiktok=%u perf data done.\n", tiktok);
	spin_unlock_irqrestore(&proc_lock[numa_id], flags);
	wake_up_interruptible(&g_perf_wait_list[numa_id]);
}

static char *pcer_str(u8 pcer)
{
	switch (pcer) {
	case IDX_NONE:
		return "none";
	case IDX_EDMA:
		return "EDMA";
	case IDX_CONV:
		return "CONV";
	case IDX_SDP:
		return "SDP";
	case IDX_PDP:
		return "PDP";
	case IDX_RUBIK:
		return "RUBIK";
	case IDX_EVENT_SINK:
		return "EVENT_SINK";
	case IDX_KMD_DSP0:
		return "IDX_KMD_DSP0";
	case IDX_KMD_DSP1:
		return "IDX_KMD_DSP1";
	case IDX_KMD_DSP2:
		return "IDX_KMD_DSP2";
	case IDX_KMD_DSP3:
		return "IDX_KMD_DSP3";
	case IDX_EVENT_SOURCE:
		return "EVENT_SOURCE";
	default:
		return "FAIL";
	}
	return "FAIL";
}

static int npu_info_show(struct seq_file *m, void *p)
{
#if NPU_PERF_STATS > 1
	int i, j, k;
	int tiktok;
	npu_e31_perf_t *op_stat;
	s16 op_idx;

	if (g_perf == 0) {
		seq_printf(
			m,
			"The perf is not turned on, pls first turn on the perf.\n");
		return 0;
	}

	seq_printf(
		m,
		"--------------------NPU PERF STATISTIC BEGIN--------------------\n");
	seq_printf(m, "\n");
	seq_printf(m, "\t%-4s\t %-4s\t %-4s\t %-8s\t %-10s\t %-10s\t %-10s\n",
		   "Die", "OpNum", "OpIndex", "OpType", "StartCycle",
		   "EndCycle", "Elapsed(ns)");
	for (i = 0; i < 2; i++) {
		u32 start = 0xffffffff, end = 0;

		if (!g_die_perf[i]) {
			continue;
		}

		spin_lock(&proc_lock[i]);
		tiktok = g_stat_titok[i];
		op_stat = g_host_node[i]->model_stat[tiktok].op_stats;
		for (j = 0; j < NUM_OP_TYPE; j++) {
			for (k = 0; k < op_num[i][j]; k++) {
				op_idx = g_cfg_seq[i][j][k];

				if (start > op_stat[op_idx].OpEvalStartCycle) {
					start = op_stat[op_idx].OpEvalStartCycle;
				}
				if (end < op_stat[op_idx].OpEvalEndCycle) {
					end = op_stat[op_idx].OpEvalEndCycle;
				}
				seq_printf(
					m,
					"\t%-4d\t %-4d\t %-4d\t %-8s\t %10u\t %10u\t %10u\n",
					i, k, op_idx, pcer_str(j),
					op_stat[op_idx].OpEvalStartCycle,
					op_stat[op_idx].OpEvalEndCycle,
					op_stat[op_idx].OpEvalEndCycle -
						op_stat[op_idx]
							.OpEvalStartCycle);
			}
		}
		spin_unlock(&proc_lock[i]);
		seq_printf(m, "\n");
	}
	seq_printf(
		m,
		"--------------------NPU PERF STATISTIC END--------------------\n");
#endif
	return 0;
}

static int info_open(struct inode *inode, struct file *flip)
{
	return single_open(flip, npu_info_show, NULL);
}

static int perf_show(struct seq_file *m, void *p)
{
	struct nvdla_device *ndev;
	struct win_engine *engine;
	int i;

	seq_printf(m,
		   "--------------------NPU Perf Info--------------------\n");
	seq_printf(m, "\n");
	seq_printf(m, "    DieId    Status    Value\n");
	for (i = 0; i < 2; i++) {
		ndev = get_nvdla_dev(i);
		if (!ndev) {
			seq_printf(m, "    %d       %s     %d\n", i, "No DIE",
				   0);
		} else {
			engine = (struct win_engine *)ndev->win_engine;
			seq_printf(m, "    %d       %s     %d\n", i, "ONLINE",
				   engine->perf_switch);
		}
	}
	seq_printf(m, "\n");
	return 0;
}

static int perf_open(struct inode *inode, struct file *flip)
{
	return single_open(flip, perf_show, NULL);
}

static ssize_t perf_write(struct file *flip, const char __user *buf,
			  size_t size, loff_t *pos)
{
	u16 data;
	struct nvdla_device *ndev;
	int i;
	u8 value;
	bool enable;

	if (size > 2) {
		return -EINVAL;
	}
	if (copy_from_user(&data, buf, size)) {
		return -EFAULT;
	}
	value = data & 0xff;

	value -= '0';
	if (!(value == 1 || value == 0)) {
		dla_error("%s, %d, data=%d is not correct, pls use 1 or 0.\n",
			  __func__, __LINE__, value);
		return -EINVAL;
	}
	enable = value ? 1 : 0;

	for (i = 0; i < 2; i++) {
		ndev = get_nvdla_dev(i);
		if (ndev) {
			handle_perf_switch(ndev, enable);
		}
	}
	return size;
}

static struct proc_ops proc_info_fops = {
	.proc_open = info_open,
	.proc_read = seq_read,
	.proc_release = single_release,
};

static struct proc_ops proc_perf_fops = {
	.proc_open = perf_open,
	.proc_read = seq_read,
	.proc_release = single_release,
	.proc_write = perf_write,
};

static void *tmp = NULL;

int npu_create_procfs(void)
{
	proc_esnpu = proc_mkdir("esnpu", NULL);
	if (proc_esnpu == NULL) {
		dla_error("create proc esnpu dir err.\n");
		return -ENOMEM;
	}

	if (!proc_create("info", 0644, proc_esnpu, &proc_info_fops)) {
		dla_error("error create proc npu info file.\n");
		goto err_info;
	}

	if (!proc_create("perf", 0644, proc_esnpu, &proc_perf_fops)) {
		dla_error("error create proc npu perf file.\n");
		goto err_perf;
	}
	spin_lock_init(&proc_lock[0]);
	spin_lock_init(&proc_lock[1]);
	init_waitqueue_head(&g_perf_wait_list[0]);
	init_waitqueue_head(&g_perf_wait_list[1]);

	tmp = kzalloc(sizeof(s16) * MAX_OP_NUM * 2, GFP_KERNEL);
	if (!tmp) {
		goto err_mem;
	}
	g_cfg_seq[0][IDX_START] = tmp;
	g_cfg_seq[1][IDX_START] = tmp + sizeof(s16) * MAX_OP_NUM;
	return 0;

err_mem:
	remove_proc_entry("perf", proc_esnpu);
err_perf:
	remove_proc_entry("info", proc_esnpu);
err_info:
	remove_proc_entry("esnpu", NULL);
	return -1;
}

void npu_remove_procfs(void)
{
	if (tmp != NULL) {
		kfree(tmp);
	}
	remove_proc_entry("info", proc_esnpu);
	remove_proc_entry("perf", proc_esnpu);
	remove_proc_entry("esnpu", NULL);
}
