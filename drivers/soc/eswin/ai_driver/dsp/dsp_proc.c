// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
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
#include <linux/iommu.h>
#include "dsp_main.h"

static struct proc_dir_entry *proc_es_dsp;
extern int dsp_log_level;
int dsp_perf_enable = 0;

void get_dsp_perf_info(es_dsp_perf_info *perf_info, int die_num, int dsp_num)
{
	struct es_dsp *dsp = NULL;

	memset((void *)perf_info, 0, sizeof(es_dsp_perf_info));
	dsp = es_proc_get_dsp(die_num, dsp_num);
	if (!dsp) {
		return;
	}
	memcpy((void *)perf_info, dsp->perf_reg_base, sizeof(es_dsp_perf_info));
}

static char *fw_state[] = {
	"Not_ready",
	"Alive",
	"Hang",
};

static char *func_state[] = {
	"No_task", "prep_run", "prep_done", "eval_run", "eval_done",
};

static int stats_show(struct seq_file *m, void *p)
{
	struct es_dsp *dsp;
	int i, j;
	dsp_request_t req;
	dsp_request_t *myreq;
	struct timespec64 ts;
	const int die_cnt = 2;
	const int dsp_cnt = 4;
	es_dsp_perf_info perf_info;
	int k;
	u32 state, cause, ps, pc;
	u32 fw_val, npu_task, dsp_task, func_val;
	struct dsp_fw_state_t *dsp_fw_state;
	struct iommu_domain *domain;
	phys_addr_t phys;
	seq_printf(
		m,
		"--------------------------------DSP PARAM INFO----------------------------------\n");
	seq_printf(m, "    DieId    CoreId     Enable   CmdTOut(s)\n");
	for (j = 0; j < die_cnt; j++) {
		for (i = 0; i < dsp_cnt; i++) {
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				seq_printf(
					m,
					"    %5d       %5d       %s        %5d\n",
					j, i, "NO", 0);
			} else {
				seq_printf(
					m,
					"    %5d       %5d       %s        %5d\n",
					j, i, "Yes", es_dsp_exec_cmd_timeout());
			}
		}
	}

	seq_printf(m, "\n");
	seq_printf(
		m,
		"--------------------------DSP FW STATE--------------------------------------\n");
	seq_printf(m,
		   "  %-5s %-6s\t %-8s\t %-8s   %-8s"
		   "   %-8s\t    %-8s   %-8s   %-10s\n",
		   "DieId", "CoreId", "fw_state", "cause", "ps", "pc",
		   "npu_task", "dsp_task", "func_state");
	for (j = 0; j < die_cnt; j++) {
		for (i = 0; i < dsp_cnt; i++) {
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				continue;
			}
			if (!dsp->dsp_fw_state_base) {
				continue;
			}
			dsp_fw_state =
				(struct dsp_fw_state_t *)dsp->dsp_fw_state_base;
			cause = dsp_fw_state->exccause;
			ps = dsp_fw_state->ps;
			pc = dsp_fw_state->pc;
			fw_val = dsp_fw_state->fw_state;
			dsp_task = dsp_fw_state->dsp_task_state;
			npu_task = dsp_fw_state->npu_task_state;
			func_val = dsp_fw_state->func_state;
			seq_printf(
				m,
				"  %-5d %-6d\t %-8s\t 0x%-8x 0x%-8x 0x%-8x\t %-8s %-8s %-10s\n",
				j, i, fw_state[fw_val], cause, ps, pc,
				npu_task ? "run" : "no_task",
				dsp_task ? "run" : "no_task",
				func_state[func_val]);
		}
	}

	seq_printf(
		m,
		"--------------------------dsp hw flat content--------------------------------------\n");
	for (j = 0; j < die_cnt; j++) {
		struct dsp_op_desc *opdesc;
		struct dsp_hw_flat_test *hw_flat;
		for (i = 0; i < dsp_cnt; i++) {
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				continue;
			}
			domain = iommu_get_domain_for_dev(dsp->dev);

			hw_flat = dsp->flat_base;
			if (hw_flat->flat_iova == 0) {
				continue;
			}
			phys = iommu_iova_to_phys(domain, hw_flat->flat_iova);
			seq_printf(m, "die=%d, core=%d, flat iova = 0x%x, phys=0x%lx.\n", j, i, hw_flat->flat_iova, phys);
			seq_printf(m, "num_buf=%d, input_idx=%d, out_idx=%d.\n", hw_flat->num_buffer, hw_flat->input_index, hw_flat->output_index);
		}
	}
	seq_printf(
		m,
		"--------------------------DSP Current Task--------------------------------------\n");

	for (j = 0; j < die_cnt; j++) {
		struct dsp_op_desc *opdesc;
		for (i = 0; i < dsp_cnt; i++) {
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				continue;
			}
			if (!dsp->current_task) {
				continue;
			}
			myreq = dsp->current_task;
			opdesc = (struct dsp_op_desc *)myreq->handle;
			seq_printf(m, "die=%d, dspcore=%d, opname=%s", j, i, opdesc->name);
			seq_printf(
				m,
				"\tdie=%d core=%d flat_iova=%x num_buf=%x input_idx=%d output_idx=%d\n", j, i, myreq->dsp_flat1_iova, myreq->flat_virt->num_buffer, myreq->flat_virt->input_index,
				myreq->flat_virt->output_index);
			domain = iommu_get_domain_for_dev(dsp->dev);
			seq_printf(m, "domain_type=0x%x.\n", domain->type);
			for (k = 0; k < myreq->flat_virt->num_buffer; k++) {
				phys = iommu_iova_to_phys(domain, myreq->flat_virt->buffers[k].addr);
				seq_printf(m, "buffer[%d]=0x%x. size=0x%x, phys=0x%llx\n", k, myreq->flat_virt->buffers[k].addr, myreq->flat_virt->buffers[k].size, phys);
			}
		}
	}


	seq_printf(m, "\n");
	seq_printf(
		m,
		"--------------------------------DSP RUNTIME INFO------------------------------------\n");
	seq_printf(
		m,
		"\t%-8s\t %-8s\t %-13s\t %-13s\t %-16s\t %-15s\t %-15s\t %-20s\t %-15s\n",
		"DieId", "CoreId", "TotalIntCnt", "SendToDspCnt",
		"FinishedTaskCnt", "FailedTaskCnt", "TimeOutTaskCnt",
		"PendingTaskCnt", "LTaskRunTm");
	for (j = 0; j < die_cnt; j++) {
		for (i = 0; i < dsp_cnt; i++) {
			struct es_dsp_stats *stats;
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				seq_printf(
					m,
					"\t%-8d\t %-8d\t %-13d\t %-13d\t %-16d\t %-15d\t %-15d\t %-20d\t %-15d\n",
					j, i, 0, 0, 0, 0, 0, 0, 0);
				continue;
			}
			stats = dsp->stats;
			ts = ns_to_timespec64(stats->last_task_time);
			seq_printf(
				m,
				"\t%-8d\t %-8d\t %-13d\t %-13d\t %-16d\t %-15d\t %-15d\t %-20d\t %lldms%ldns\n",
				j, i, stats->total_int_cnt,
				stats->send_to_dsp_cnt, stats->total_ok_cnt,
				stats->total_failed_cnt,
				stats->task_timeout_cnt, dsp->wait_running,
				ts.tv_sec * 1000, ts.tv_nsec);
		}
	}

	seq_printf(m, "\n");
	seq_printf(
		m,
		"--------------------------------DSP TASK INFO------------------------------------\n");
	seq_printf(m, "\t%-8s\t %-8s\t %-8s\t %-8s\t %-12s\t %-12s\n", "DieId",
		   "CoreId", "TaskCnt", "InvaldCmdCnt", "SendPrepareNpu",
		   "SendEvalToNpu");
	for (j = 0; j < die_cnt; j++) {
		for (i = 0; i < dsp_cnt; i++) {
			get_dsp_perf_info(&perf_info, j, i);
			dsp = es_proc_get_dsp(j, i);

			if (dsp == NULL) {
				seq_printf(
					m,
					"\t%-8d\t %-8d\t %-8d\t %-8d\t %-12d\t %-12d\n",
					j, i, 0, 0, 0, 0);
			} else {
				seq_printf(
					m,
					"\t%-8d\t %-8d\t %-8d\t %-8d\t %-12d\t %-12d\n",
					j, i, perf_info.task_cnt,
					perf_info.invalid_cmd_cnt,
					perf_info.send_prepare_to_npu,
					perf_info.send_eval_to_npu);
			}
		}
	}

#if BUILD_RELEASE > 1
	seq_printf(m, "\n");
	seq_printf(
		m,
		"--------------------------------DSP HW PERF INFO------------------------------------\n");
	seq_printf(
		m,
		"\t%-8s\t %-8s\t %-10s\t %-10s\t %-10s\t %-10s\t %-10s\t %-8s\t %-8s\t %-8s\n",
		"DieId", "CoreId", "TaskName", "StartTm", "PrepSTm", "PrepETm",
		"EvalSTm", "EvalETm", "IPCSTm", "EndTm");
	for (j = 0; j < die_cnt; j++) {
		for (i = 0; i < dsp_cnt; i++) {
			get_dsp_perf_info(&perf_info, j, i);
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				seq_printf(
					m,
					"\t%-8d\t %-8d\t %-10s\t %-10u\t %-10u\t %-10u\t %-10u\t %-8u\t %-8u\t %-8u\n",
					j, i, "NULL", 0, 0, 0, 0, 0, 0, 0);
				continue;
			} else {
				seq_printf(
					m,
					"\t%-8d\t %-8d\t %-10s\t %-10u\t %-10u\t %-10u\t %-10u\t %-8u\t %-8u\t %-8u\n",
					j, i, dsp->stats->last_op_name,
					perf_info.flat1_start_time,
					perf_info.prepare_start_time,
					perf_info.prepare_end_time,
					perf_info.eval_start_time,
					perf_info.eval_end_time,
					perf_info.notify_start_time,
					perf_info.flat1_end_time);
			}
		}
	}
#endif

	seq_printf(m, "\n");
	seq_printf(
		m,
		"--------------------------------DSP INVOKE INFO------------------------------------\n");
	seq_printf(m, "\t%-8s\t %-8s\t %-8s\t %-15s\t %-10s\t %-8s\t %-10s\n",
		   "DieId", "CoreId", "Pri", "TaskName", "TaskHnd", "TaskStat",
		   "TaskRunTm");
	for (j = 0; j < die_cnt; j++) {
		for (i = 0; i < dsp_cnt; i++) {
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				seq_printf(
					m,
					"\t%-8d\t %-8d\t %-8d\t %-15s\t %-10d\t %-8s\t %-10d\n",
					j, i, 0, "NULL", 0, "NULL", 0);
				continue;
			}

			if (dsp->current_task == NULL) {
				seq_printf(
					m,
					"\t%-8d\t %-8d\t %-8d\t %-15s\t %-10d\t %-8s\t %-10d\n",
					j, i, 0, "NULL", 0, "NULL", 0);
				continue;
			}
			memcpy(&req, dsp->current_task, sizeof(req));
			ts = ns_to_timespec64(ktime_get_real_ns() -
					      dsp->send_time);
			seq_printf(
				m,
				"\t%-8d\t %-8d\t %-8d\t %-15s\t %-10llx\t %-8s\t %lldms%ldns\n",
				j, i, req.prio, dsp->stats->last_op_name,
				req.handle, "eval", ts.tv_sec * 1000,
				ts.tv_nsec);
		}
	}
	seq_printf(m, "\n");
	return 0;
}

static int proc_stats_release(struct inode *inode, struct file *file)
{
	const int die_cnt = 2;
	const int dsp_cnt = 4;
	int i, j;
	struct es_dsp *dsp;

	for (j = 0; j < die_cnt; j++) {
		for (i = 0; i < dsp_cnt; i++) {
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				continue;
			}
			es_dsp_pm_put_sync(dsp);
		}
	}
	return 0;
}

static int proc_stats_open(struct inode *inode, struct file *file)
{
	int ret;
	const int die_cnt = 2;
	const int dsp_cnt = 4;
	int i, j;
	struct es_dsp *dsp;

	for (j = 0; j < die_cnt; j++) {
		for (i = 0; i < dsp_cnt; i++) {
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				continue;
			}
			ret = es_dsp_pm_get_sync(dsp);
			if (ret < 0) {
				dsp_err("%s, %d, get dsp die = %d, core = %d pm err.\n",
					__func__, __LINE__, j, i);
				goto err;
			}
		}
	}
	return single_open(file, stats_show, NULL);

err:
	for (j; j >= 0; j--) {
		for (i -= 1; i >= 0; i--) {
			dsp = es_proc_get_dsp(j, i);
			if (dsp == NULL) {
				continue;
			}
			es_dsp_pm_put_sync(dsp);
		}
	}
	return ret;
}

static int debug_show(struct seq_file *m, void *p)
{
	seq_printf(m, "\n");
	seq_printf(
		m,
		"--------------------DSP DRIVER DEBUG INFO--------------------\n");
	seq_printf(m, "Debug    Value\n");
	seq_printf(m, "%s        %d\n", dsp_log_level ? "ON" : "OFF",
		   dsp_log_level);
	seq_printf(m, "\n");

	return 0;
}

static int debug_open(struct inode *inode, struct file *flip)
{
	return single_open(flip, debug_show, NULL);
}

static ssize_t debug_write(struct file *flip, const char __user *buf,
			   size_t size, loff_t *pos)
{
	u16 value;

	if (size > 2) {
		return -EINVAL;
	}

	if (copy_from_user(&value, buf, size)) {
		return -EFAULT;
	}

	dsp_log_level = (value - '0') & 0xf;
	printk("set dsp driver debug as value(%d) to %d\n", value,
	       dsp_log_level);

	return size;
}

static int perf_show(struct seq_file *m, void *p)
{
	seq_printf(m, "\n");
	seq_printf(
		m,
		"--------------------DSP DRIVER PERF INFO--------------------\n");
	seq_printf(m, "Perf    Value\n");
	seq_printf(m, "%s        %d\n", dsp_perf_enable ? "ON" : "OFF",
		   dsp_perf_enable);
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
	u16 value;

	if (size > 2) {
		return -EINVAL;
	}

	if (copy_from_user(&value, buf, size)) {
		return -EFAULT;
	}

	dsp_perf_enable = (value - '0') & 0xf;
	printk("set dsp driver perf as value(%d) to %d\n", value,
	       dsp_perf_enable);

	return size;
}

static struct proc_ops proc_stats_fops = {
	.proc_open = proc_stats_open,
	.proc_read = seq_read,
	.proc_release = proc_stats_release,
};

static struct proc_ops proc_debug_fops = {
	.proc_open = debug_open,
	.proc_read = seq_read,
	.proc_release = single_release,
	.proc_write = debug_write,
};

static struct proc_ops proc_perf_fops = {
	.proc_open = perf_open,
	.proc_read = seq_read,
	.proc_release = single_release,
	.proc_write = perf_write,
};

int es_dsp_init_proc(void)
{
	proc_es_dsp = proc_mkdir("esdsp", NULL);
	if (proc_es_dsp == NULL) {
		dsp_err("proc mkdir esdsp dir err.\n");
		return -ENOMEM;
	}
	if (!proc_create("info", 0644, proc_es_dsp, &proc_stats_fops)) {
		goto err;
	}
	if (!proc_create("debug", 0644, proc_es_dsp, &proc_debug_fops)) {
		dsp_err("error create proc dsp debug file.\n");
		goto err;
	}
	if (!proc_create("perf", 0644, proc_es_dsp, &proc_perf_fops)) {
		dsp_err("error create proc dsp perf file.\n");
		goto err;
	}
	return 0;

err:
	remove_proc_subtree("esdsp", NULL);
	return -1;
}

void es_dsp_remove_proc(void)
{
	remove_proc_subtree("esdsp", NULL);
}
