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
#include <linux/units.h>
#include "hetero_ioctl.h"
#include "internal_interface.h"
#include "hetero_perf.h"
#include "nvdla_lowlevel.h"

static struct proc_dir_entry *proc_esnpu;

static int npu_stat_show(struct seq_file *m, void *p)
{
	int i = 0;
	struct nvdla_device *ndev;
	emission_node_t *pemission_node;
	u64 total_hwexec_time = 0;
	u32 frame_start = 0;
	u64 gap_adjust = 0, delta_frame = 0;
	u32 curr_rtc = 0;
	uint64_t start_stat_time = ktime_get_real_ns();
	int ret;

	for (i = 0; i < 2; i++) {
		ndev = get_nvdla_dev(i);
		if (!ndev || !ndev->emission_base) {
			continue;
		}

		ret = npu_pm_get(ndev);
		if (ret < 0) {
			dla_error("pm get sync err, ret=%d.\n", ret);
			return ret;
		}

		gap_adjust = 0;
		pemission_node = (emission_node_t *)ndev->emission_base;
		total_hwexec_time = pemission_node->total_ran_time;
		frame_start = pemission_node->frame_start_ts;
		if(frame_start)
		{
			curr_rtc = get_perf_timer_cnt(i);
			if(curr_rtc > frame_start) {
				delta_frame = (curr_rtc - frame_start);
			} else {
				delta_frame = (-1U - frame_start + curr_rtc);
			}
			gap_adjust = delta_frame * 10000 / 495; // timer3 channel 7 clk 49.5MHz.
		}

		seq_printf(m, "npu%d %llu %llu %llu %llu\n",i, start_stat_time,
		           (total_hwexec_time * 10000) / 495 + gap_adjust,
		           (total_hwexec_time * 10000) / 495,
		           atomic64_read(&ndev->total_frame_done));

		npu_pm_put(ndev);
	}
	return 0;
}

static int npu_info_show(struct seq_file *m, void *p)
{
	int i;
	unsigned long flags;
	u32 task_count[2] = { 0 };
	u32 task_status[2] = { 0 };
	struct host_frame_desc *frame = NULL;
	struct nvdla_device *ndev;
	struct win_engine *engine;

	for (i = 0; i < 2; i++) {
		ndev = get_nvdla_dev(i);
		if (!ndev || !ndev->win_engine) {
			continue;
		}
		engine = (struct win_engine *)ndev->win_engine;
		spin_lock_irqsave(&engine->executor_lock, flags);
		task_status[i] = (!!engine->tiktok_frame[0]) + (!!engine->tiktok_frame[1]);
		list_for_each_entry(frame, &engine->sched_frame_list, sched_node)
		{
			task_count[i]++;
		}
		spin_unlock_irqrestore(&engine->executor_lock, flags);
	}

	seq_printf(m, "npu0 hw task:%u, queue task:%u\n",task_status[0], task_count[0]);
	seq_printf(m, "npu1 hw task:%u, queue task:%u\n",task_status[1], task_count[1]);
	return 0;
}

static int info_open(struct inode *inode, struct file *flip)
{
	return single_open(flip, npu_info_show, NULL);
}

static int stat_open(struct inode *inode, struct file *flip)
{
	return single_open(flip, npu_stat_show, NULL);
}

static int npu_conf_show(struct seq_file *m, void *p)
{
	int i = 0;
	unsigned long rate = 0, volt = 0, llc_rate;
	struct nvdla_device *ndev = NULL;

	for (i = 0; i < 2; i++)	{
		ndev = get_nvdla_dev(i);
		if (!ndev) {
			continue;
		}
		volt = regulator_get_voltage(ndev->npu_regulator);
		rate = clk_get_rate(ndev->core_clk);
		llc_rate = clk_get_rate(ndev->llc_aclk);

		seq_printf(m, "npu%d %lu %lu %lu \n", i, volt, rate, llc_rate);
	}
	return 0;
}

static int conf_open(struct inode *inode, struct file *flip)
{
	return single_open(flip, npu_conf_show, NULL);
}

static int npu_cus_volt_show(struct seq_file *m, void *p)
{
	struct nvdla_device *ndev = NULL;

	ndev = get_nvdla_dev(0);
	if (ndev) {
		seq_printf(m, "npu user define volt:%u\n", ndev->npu_def_high_volt);
	}

	return 0;
}

static int cus_volt_open(struct inode *inode, struct file *flip)
{
	return single_open(flip, npu_cus_volt_show, NULL);
}

static ssize_t cus_volt_write(struct file *flip, const char __user *buffer,
			  size_t size, loff_t *pos)
{
	char buf[32];
	long volt;
	struct nvdla_device *ndev;
	int ret, i;

	if (size >= sizeof(buf)) {
		dla_error("input size %ld error\n", size);
		return -EINVAL;
	}
	if (copy_from_user(buf, buffer, size)) {
		return -EFAULT;
	}
	buf[size] = '\0';

	if (kstrtol(buf, 10, &volt))
		return -EINVAL;

	pr_info("set volt = %ld\n", volt);

	if (volt == 0) {
		for (i = 0; i < 2; i++) {
			ndev = get_nvdla_dev(i);
			if (ndev) {
				ndev->npu_def_high_volt = 0;
			}
		}
		return size;
	}

	if (volt < 700000 || volt > 1100000) {
		dla_error("npu volt %ld is not correct, (700000 <= volt <= 1100000).\n", volt);
		return -EINVAL;
	}

	for (i = 0; i < 2; i++) {
		ndev = get_nvdla_dev(i);
		if (ndev) {
			ret = regulator_set_voltage(ndev->npu_regulator, volt - ndev->volt_step + 1, volt);
			if (ret) {
				dla_error("set npu%d voltage %lduV failed, err:%d\n", i, volt, ret);
				return -EINVAL;
			}
			ndev->npu_def_high_volt = volt;
		}
	}
	return size;
}

static int npu_maxfreq_show(struct seq_file *m, void *p)
{

	struct nvdla_device *ndev = NULL;

	ndev = get_nvdla_dev(0);
	if (ndev) {
		seq_printf(m, "npu max freq:%u\n", ndev->max_freq);
	}

	return 0;
}

static int maxfreq_open(struct inode *inode, struct file *flip)
{
	return single_open(flip, npu_maxfreq_show, NULL);
}


static ssize_t npu_maxfreq_write(struct file *flip, const char __user *buffer,
			  size_t size, loff_t *pos)
{
	char buf[32];
	long maxfreq;
	struct nvdla_device *ndev;
	int i, j;

	if (size >= sizeof(buf)) {
		dla_error("input size %ld error\n", size);
		return -EINVAL;
	}
	if (copy_from_user(buf, buffer, size)) {
		return -EFAULT;
	}
	buf[size] = '\0';

	if (kstrtol(buf, 10, &maxfreq))
		return -EINVAL;

	pr_info("set maxfreq = %ld\n", maxfreq);

	for (i = 0; i < 2; i++) {
		ndev = get_nvdla_dev(i);
		if (ndev) {
			for (j = 0; j < ndev->freq_count; j++) {
				if (ndev->freq_tbl[j].npu_rate == maxfreq) {
					break;
				}
			}

			if (j == ndev->freq_count) {
				dla_error("max_freq:%ld is invalid!\n", maxfreq);
				dla_error("npu support max_freq:\n");
				for (j = 0; j < ndev->freq_count; j++) {
					pr_info("%ld\n", ndev->freq_tbl[j].npu_rate);
				}
				return -EINVAL;
			}
			dev_pm_qos_update_request(&ndev->req_max_freq, DIV_ROUND_UP(maxfreq, HZ_PER_KHZ));
			ndev->max_freq = maxfreq;
		}
	}
	return size;
}

static int npu_idle_volt_show(struct seq_file *m, void *p)
{
	struct nvdla_device *ndev = NULL;

	ndev = get_nvdla_dev(0);
	if (ndev) {
		seq_printf(m, "npu idle volt:%u\n", ndev->idle_volt);
	}

	return 0;
}

static int idle_volt_open(struct inode *inode, struct file *flip)
{
	return single_open(flip, npu_idle_volt_show, NULL);
}

static ssize_t npu_idle_volt_write(struct file *flip, const char __user *buffer,
								   size_t size, loff_t *pos)
{
	char buf[32];
	long volt;
	struct nvdla_device *ndev;
	int i;

	if (size >= sizeof(buf)) {
		dla_error("input size %ld error\n", size);
		return -EINVAL;
	}
	if (copy_from_user(buf, buffer, size)) {
		return -EFAULT;
	}
	buf[size] = '\0';

	if (kstrtol(buf, 10, &volt))
		return -EINVAL;

	pr_info("set idle volt = %ld\n", volt);

	if (volt < 700000 || volt > 1100000) {
		dla_error("npu idle volt %ld is not correct, (700000 <= idle volt <= 1100000).\n", volt);
		return -EINVAL;
	}

	for (i = 0; i < 2; i++) {
		ndev = get_nvdla_dev(i);
		if (ndev) {
			ndev->idle_volt = volt;
		}
	}
	return size;
}

static struct proc_ops proc_info_fops = {
	.proc_open = info_open,
	.proc_read = seq_read,
	.proc_release = single_release,
};

static struct proc_ops proc_stat_fops = {
	.proc_open = stat_open,
	.proc_read = seq_read,
	.proc_release = single_release,
};

static struct proc_ops proc_conf_fops = {
	.proc_open = conf_open,
	.proc_read = seq_read,
	.proc_release = single_release,
};

static struct proc_ops proc_cus_volt_fops = {
	.proc_open = cus_volt_open,
	.proc_read = seq_read,
	.proc_release = single_release,
	.proc_write = cus_volt_write,
};

static struct proc_ops proc_maxfreq_fops = {
	.proc_open = maxfreq_open,
	.proc_read = seq_read,
	.proc_release = single_release,
	.proc_write = npu_maxfreq_write,
};

static struct proc_ops proc_idle_volt_fops = {
	.proc_open = idle_volt_open,
	.proc_read = seq_read,
	.proc_release = single_release,
	.proc_write = npu_idle_volt_write,
};

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

	if (!proc_create("stat", 0444, proc_esnpu, &proc_stat_fops)) {
		dla_error("error create proc npu stat file.\n");
		goto err_stat;
	}

	if (!proc_create("conf", 0444, proc_esnpu, &proc_conf_fops)) {
		dla_error("error create proc npu conf file.\n");
		goto err_conf;
	}

	if (!proc_create("cus_volt", 0444, proc_esnpu, &proc_cus_volt_fops)) {
		dla_error("error create proc volt file.\n");
		goto err_cus_volt;
	}

	if (!proc_create("maxfreq", 0444, proc_esnpu, &proc_maxfreq_fops)) {
		dla_error("error create proc maxfreq file.\n");
		goto err_maxfreq;
	}

	if (!proc_create("idle_volt", 0444, proc_esnpu, &proc_idle_volt_fops)) {
		dla_error("error create proc idle volt file.\n");
		goto err_idle_volt;
	}

	return 0;
err_idle_volt:
	remove_proc_entry("maxfreq", proc_esnpu);
err_maxfreq:
	remove_proc_entry("cus_volt", proc_esnpu);
err_cus_volt:
	remove_proc_entry("conf", proc_esnpu);
err_conf:
	remove_proc_entry("stat", proc_esnpu);
err_stat:
	remove_proc_entry("info", proc_esnpu);
err_info:
	remove_proc_entry("esnpu", NULL);
	return -1;
}

void npu_remove_procfs(void)
{
	remove_proc_entry("info", proc_esnpu);
	remove_proc_entry("conf", proc_esnpu);
	remove_proc_entry("stat", proc_esnpu);
	remove_proc_entry("cus_volt", proc_esnpu);
	remove_proc_entry("maxfreq", proc_esnpu);
	remove_proc_entry("idle_volt", proc_esnpu);
	remove_proc_entry("esnpu", NULL);
}
