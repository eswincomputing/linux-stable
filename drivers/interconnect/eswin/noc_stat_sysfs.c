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

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <asm/div64.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "noc.h"

/**
 * Called when writing run file
 */
static ssize_t noc_stat_run_write(struct file *filp, const char __user *ubuf,
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
		noc_device->stat->run = false;	/* FIXME spinlock? */
		/*wait for the latest measure to be finished*/
		msleep(5);
		win2030_noc_stat_reset_statistics(noc_device->dev);
	} else if (buf[0] == '1') {
		if (true != noc_device->stat->run) {
			noc_device->stat->run = true;
			win2030_noc_stat_reset_statistics(noc_device->dev);
			ret = win2030_noc_stat_trigger(noc_device->dev);
			if (ret)
				dev_err(noc_device->dev, "Error when trigger stat measures!\n");
		}
	} else
		return -EFAULT;

	*ppos += cnt;

	return cnt;
}

/**
 * Called when reading run file
 */
static ssize_t noc_stat_run_read(struct file *filp, char __user *ubuf,
				 size_t cnt, loff_t *ppos)
{
#define RUN_STR_SIZE 11
	struct win2030_noc_device *noc_device = filp->private_data;
	char buf[RUN_STR_SIZE];
	int r;

	r = snprintf(buf, RUN_STR_SIZE, "%i\n", noc_device->stat->run);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

/**
 * Called when writing config file
 */
static ssize_t noc_stat_config_write(struct file *filp,
				     const char __user *ubuf, size_t cnt,
				     loff_t *ppos)
{
	return 0;
}

/**
 * Called when reading config file
 */
static ssize_t noc_stat_config_read(struct file *filp, char __user *ubuf,
				    size_t cnt, loff_t *ppos)
{
	return 0;
}

/**
 * Called when reading readme file
 * @param m
 * @param p
 * @return
 */
static int noc_stat_readme_show(struct seq_file *m, void *p)
{
	seq_puts(m,
		 "How to use the NOC sniffer:\nFile run: write '1' to enable "
		 "sniffer (enabling sniffer resets statistics),"
		 " write '0' to disable sniffer.\n"
		 "File config: Not yet implemented.\n"
		 "File results: contain the results of measurement in KB/s "
		 "for all nodes defined in dts.\n");
	return 0;
}

/**
 * Called when reading results file.
 */
static ssize_t noc_stat_results_read(struct file *filp, char __user *ubuf,
				     size_t cnt, loff_t *ppos)
{
	struct win2030_noc_device *noc_device = filp->private_data;
	struct win2030_noc_probe *probe;
	struct win2030_noc_stat *stat;
	struct win2030_noc_stat_probe *stat_probe = NULL;
	struct win2030_noc_stat_measure *stat_measure = NULL;
	u64 meas_min, meas_max, meas_mean, meas_now;
	u64 clock_noc = 0;
	u64 period_cycle = int_pow(2, DURATION);
	int j;
	char *buf1;
	char buf2[SIZE_SMALL_BUF + 2];
	int r;
	ssize_t ret;
	int count;		/* Used to avoid write over buffer size */
	struct win2030_noc_stat_traceport_data *data;

	//extern int win2030_noc_get_error(struct win2030_noc_device *noc_device);
	//win2030_noc_get_error(noc_device);

	if (noc_device == NULL)
		return -ENODEV;

	stat = noc_device->stat;
	if (!stat)
		return -ENODEV;
	clock_noc = (u64) stat->clock_rate;

	buf1 = kzalloc(sizeof(*buf1) * SIZE_BIG_BUF, GFP_KERNEL);
	if (!buf1)
		return -ENODEV;

	r = snprintf(buf1, SIZE_BIG_BUF,"\t##### RESULTS #####\n\n");

	count = SIZE_BIG_BUF - strlen(buf1);
	list_for_each_entry(stat_probe, &stat->probe, link) {
		probe = stat_probe->probe;
		if (clock_noc == 0) {
			clock_noc = (u64) clk_get_rate(probe->clock);
		}
		list_for_each_entry(stat_measure, &stat_probe->measure, link) {
			data = &stat_measure->data;
			if (count > 0) {
				if (data->init_flow_name != NULL)
					r = snprintf(buf2, SIZE_SMALL_BUF,
						     "Probe %s, clk %llu, (%s): %s-->%s\n",
						     stat_probe->probe->id,
						     clock_noc,
						     stat_probe->probe->available_portsel[data->idx_trace_port_sel],
						     data->init_flow_name,
						     data->target_flow_name);
				else
					r = snprintf(buf2, SIZE_SMALL_BUF,
						     "Probe %s, clk %llu, (%s):\n",
						     stat_probe->probe->id,
						     clock_noc,
						     stat_probe->probe->available_portsel[data->idx_trace_port_sel]);

				strncat(buf1, buf2, count);
				count -= strlen(buf2);
			}
			for (j = 0; j < probe->nr_portsel; j++) {
				r = snprintf(buf2, SIZE_SMALL_BUF,
					"\tTracePort %s, NbOfMeasure=%i\n",
					stat_probe->probe->available_portsel[j],
					stat_measure->iteration[j]);
				strncat(buf1, buf2, count);
				count -= strlen(buf2);

				if (count > 0) {
					if (stat_measure->iteration[j] != 0) {
						meas_min = stat_measure->min[j];
						meas_max = stat_measure->max[j];
						meas_mean = stat_measure->mean[j];
						do_div(meas_mean,
						       stat_measure->iteration[j]);
						meas_now = stat_measure->now[j];
						r = snprintf(buf2, SIZE_SMALL_BUF,
							     "\t\t total=%llu (KB), period time %lld (us), total time %lld (s)\n"
							     "\t\t min=%llu (KB/s), max=%llu (KB/s) mean=%llu (KB/s) current=%llu (KB/s)\n\n",
						     stat_measure->mean[j] / 1000,
						     period_cycle * 1000 * 1000 / clock_noc,
						     (stat_measure->iteration[j] * period_cycle) / clock_noc,
						     ((meas_min * clock_noc) / (period_cycle * 1000)),
						     ((meas_max * clock_noc) / (period_cycle * 1000)),
						     ((meas_mean * clock_noc) / (period_cycle * 1000)),
						     ((meas_now * clock_noc) / (period_cycle * 1000)));
					} else {
						r = snprintf(buf2, SIZE_SMALL_BUF,
						     "\t\t No results\n\n");
					}
					strncat(buf1, buf2, count);
					count -= strlen(buf2);
				}
			}
		}
	}

	r = strlen(buf1) + 1;

	ret = simple_read_from_buffer(ubuf, cnt, ppos, buf1, r);

	kfree(buf1);
	return ret;

}

/**
 * Called when open results file
 * @param inode
 * @param file
 * @return
 */
static int noc_stat_results_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

/**
 * Called when opening config file
 * @param inode
 * @param file
 * @return
 */
static int noc_stat_config_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

/**
 * Called when opening run file
 * @param inode
 * @param file
 * @return
 */
static int noc_stat_run_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

/**
 * Called when reading readme file
 * @param inode
 * @param file
 * @return
 */
static int noc_stat_readme_open(struct inode *inode, struct file *file)
{
	return single_open(file, noc_stat_readme_show, inode->i_private);
}

static const struct file_operations noc_stat_results_fops = {
	.open = noc_stat_results_open,
	.read = noc_stat_results_read,
	.llseek = generic_file_llseek,
};

static const struct file_operations noc_stat_config_fops = {
	.open = noc_stat_config_open,
	.read = noc_stat_config_read,
	.write = noc_stat_config_write,
	.llseek = generic_file_llseek,
};

static const struct file_operations noc_stat_run_fops = {
	.open = noc_stat_run_open,
	.read = noc_stat_run_read,
	.write = noc_stat_run_write,
	.llseek = generic_file_llseek,
};

static const struct file_operations noc_stat_readme_fops = {
	.open = noc_stat_readme_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/**
 * Entry point for NOC sniffer sysfs
 * @param _dev A reference to the device
 * @return 0
 */
int win2030_noc_stat_debugfs_init(struct device *_dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct device_node *np = _dev->of_node;
	struct dentry *dir, *d;
	char name[32];

	scnprintf(name, ARRAY_SIZE(name), "%s_stat", np->name);

	dir = debugfs_create_dir(name, win2030_noc_ctrl.win2030_noc_root_debug_dir);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	noc_device->stat->dir = dir;

	d = debugfs_create_file("results", S_IRUGO, dir, noc_device,
				&noc_stat_results_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);
	d = debugfs_create_file("config", S_IRUGO | S_IWUSR, dir, noc_device,
				&noc_stat_config_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);
	d = debugfs_create_file("run", S_IRUGO | S_IWUSR, dir, noc_device,
				&noc_stat_run_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);
	d = debugfs_create_file("readme.txt", S_IRUGO, dir, noc_device,
				&noc_stat_readme_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);

	return 0;
}
