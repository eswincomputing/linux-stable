// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include "dsp_main.h"

static struct proc_dir_entry *proc_es_dsp;
extern int dsp_log_level;
int dsp_perf_enable = 0;

// from eswin/dsp/framework/lsp/memmap.xmm .dram1.perfdata(0x2813ffc0)
#define DSP_PERF_START_ADDR 0x5b13ffc0
#define DIE_BASE_INTERVAL 0x20000000
#define DSP_CORE_INTERVAL 0x40000

void get_dsp_perf_info(es_dsp_perf_info *perf_info, int die_num, int dsp_num)
{
	struct es_dsp *dsp = NULL;
	void *iomem = NULL;
	unsigned long phys;

	memset((void *)perf_info, 0, sizeof(es_dsp_perf_info));
	dsp = es_proc_get_dsp(die_num, dsp_num);
	if (!dsp) {
		return;
	}

	phys = DSP_PERF_START_ADDR + die_num * DIE_BASE_INTERVAL +
	       DSP_CORE_INTERVAL * dsp_num;
	iomem = ioremap(phys, sizeof(es_dsp_perf_info));
	if (!iomem) {
		return;
	}
	memcpy((void *)perf_info, iomem, sizeof(es_dsp_perf_info));

	iounmap(iomem);
}

static int stats_show(struct seq_file *m, void *p)
{
	struct es_dsp *dsp;
	int i, j;
	dsp_request_t req;
	struct timespec64 ts;
	const int die_cnt = 2;
	const int dsp_cnt = 4;
	es_dsp_perf_info perf_info;
	int k;

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
