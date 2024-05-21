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
static ssize_t noc_prof_run_write(struct file *filp, const char __user *ubuf,
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
		noc_device->prof->run = false;	/* FIXME spinlock? */
		/*wait for the latest measure to be finished*/
		msleep(5);
		win2030_noc_prof_reset_statistics(noc_device->dev);
	} else if (buf[0] == '1') {
		if (true != noc_device->prof->run) {
			noc_device->prof->run = true;
			/* trigger measurements */
			ret = win2030_noc_prof_trigger(noc_device->dev);
			WARN_ON(ret != 0);
		}
	} else
		return -EFAULT;

	*ppos += cnt;

	return cnt;
}

/**
 * Called when reading run file
 */
static ssize_t noc_prof_run_read(struct file *filp, char __user *ubuf,
				 size_t cnt, loff_t *ppos)
{
#define RUN_STR_SIZE 11
	struct win2030_noc_device *noc_device = filp->private_data;
	char buf[RUN_STR_SIZE];
	int r;

	r = snprintf(buf, RUN_STR_SIZE, "%i\n", noc_device->prof->run);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}

/**
 * Called when writing config file
 */
static ssize_t noc_prof_config_write(struct file *filp,
				     const char __user *ubuf, size_t cnt,
				     loff_t *ppos)
{
	char buf[SIZE_SMALL_BUF] = {0};
	char *bin;
	int value;
	int ret;
	char bin_name[20] = {0};
	int i;

	if (cnt > SIZE_SMALL_BUF)
		cnt = SIZE_SMALL_BUF - 1;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	*ppos += cnt;
	for (i = 0; i < 3; i++) {
		sprintf(bin_name, "latency_bin%d:", i);
		bin = strstr(buf, bin_name);
		if (bin) {
			ret = kstrtoint(bin + strlen(bin_name), 0, &value);
			if (!ret) {
				latency_bin[i] = value;
				return cnt;
			}
		}
	}
	pr_err("invalid config %s", buf);
	return cnt;
}

/**
 * Called when reading config file
 */
static ssize_t noc_prof_config_read(struct file *filp, char __user *ubuf,
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
static int noc_prof_readme_show(struct seq_file *m, void *p)
{
	seq_puts(m,
		 "How to use the NOC profiler:\nFile run: write '1' to enable "
		 "profiler (enabling profiler resets statistics),"
		 " write '0' to disable profiler.\n"
		 "File config: Not yet implemented.\n"
		 "File results: contain the results of measurement(each bin's percentage in the total traffic)"
		 "for all nodes defined in dts.\n");
	return 0;
}

/**
 * Called when reading results file.
 */
static ssize_t noc_prof_results_read(struct file *filp, char __user *ubuf,
				     size_t cnt, loff_t *ppos)
{
	struct win2030_noc_device *noc_device = filp->private_data;
	struct win2030_noc_probe *probe;
	struct win2030_noc_prof *prof;
	struct win2030_noc_prof_probe *prof_probe = NULL;
	struct win2030_noc_prof_measure *prof_measure = NULL;
	u64 meas_mean;
	int i, j;
	char *buf1;
	char buf2[SIZE_SMALL_BUF + 2];
	int r;
	ssize_t ret;
	int count;	/* Used to avoid write over buffer size */
	char latency_bin_name[4][20] = {{0}};
	char *pending_bin_name[] = {"0~2", "2~5","5~10", "10~"};
	unsigned long flags;

	if (noc_device == NULL)
		return -ENODEV;

	prof = noc_device->prof;
	if (!prof)
		return -ENODEV;

	buf1 = kzalloc(sizeof(*buf1) * SIZE_BIG_BUF, GFP_KERNEL);
	if (!buf1)
		return -ENODEV;

	r = snprintf(buf1, SIZE_BIG_BUF,"\t##### RESULTS #####\n\n");

	count = SIZE_BIG_BUF - strlen(buf1);
	list_for_each_entry(prof_probe, &prof->prof_probe, link) {
		probe = prof_probe->probe;
		list_for_each_entry(prof_measure, &prof_probe->measure, link) {
			if (count > 0) {
				r = snprintf(buf2, SIZE_SMALL_BUF,
					"Probe %s\n", prof_probe->probe->id);
				strncat(buf1, buf2, count);
				count -= strlen(buf2);
			}
			if (pending_t == prof_measure->type) {

			} else {
				sprintf(latency_bin_name[0], "\"0~%d\"", latency_bin[0]);
				sprintf(latency_bin_name[1], "\"%d~%d\"",latency_bin[0], latency_bin[1]);
				sprintf(latency_bin_name[2], "\"%d~%d\"",latency_bin[1], latency_bin[2]);
				sprintf(latency_bin_name[3], "\"%d~\"", latency_bin[2]);
			}
			spin_lock_irqsave(&prof_measure->lock, flags);
			for (j = 0; j < probe->nr_portsel; j++) {
				r = snprintf(buf2, SIZE_SMALL_BUF,
					"\tTracePort %s, NbOfMeasure=%i, type %s\n",
					prof_probe->probe->available_portsel[j],
					prof_measure->iteration[j],
					prof_measure->type == pending_t ? "pedning" : "latency");
				strncat(buf1, buf2, count);
				count -= strlen(buf2);
				if (count > 0) {
					if (prof_measure->iteration[j] != 0) {
						for (i = 0; i < WIN2030_NOC_BIN_CNT; i++) {
							meas_mean = prof_measure->mean[j][i];
							do_div(meas_mean, prof_measure->iteration[j]);
							r = snprintf(buf2, SIZE_SMALL_BUF,
								     "\t\t%s(cycles) percentage: min=%llu%%  max=%llu%%  mean=%llu%%  "
								     "current=%llu%% \n\n",
								     pending_t == prof_measure->type ? pending_bin_name[i] : latency_bin_name[i],
								     prof_measure->min[j][i],
								     prof_measure->max[j][i],
								     meas_mean,
								     prof_measure->now[j][i]);
							strncat(buf1, buf2, count);
							count -= strlen(buf2);
						}
					} else {
						r = snprintf(buf2, SIZE_SMALL_BUF,
						     "\t\t No results\n\n");
						strncat(buf1, buf2, count);
						count -= strlen(buf2);
					}
				}
			}
			spin_unlock_irqrestore(&prof_measure->lock, flags);
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
static int noc_prof_results_open(struct inode *inode, struct file *file)
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
static int noc_prof_config_open(struct inode *inode, struct file *file)
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
static int noc_prof_run_open(struct inode *inode, struct file *file)
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
static int noc_prof_readme_open(struct inode *inode, struct file *file)
{
	return single_open(file, noc_prof_readme_show, inode->i_private);
}

static const struct file_operations noc_prof_results_fops = {
	.open = noc_prof_results_open,
	.read = noc_prof_results_read,
	.llseek = generic_file_llseek,
};

static const struct file_operations noc_prof_config_fops = {
	.open = noc_prof_config_open,
	.read = noc_prof_config_read,
	.write = noc_prof_config_write,
	.llseek = generic_file_llseek,
};

static const struct file_operations noc_prof_run_fops = {
	.open = noc_prof_run_open,
	.read = noc_prof_run_read,
	.write = noc_prof_run_write,
	.llseek = generic_file_llseek,
};

static const struct file_operations noc_prof_readme_fops = {
	.open = noc_prof_readme_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/**
 * Entry point for NOC profiler sysfs
 * @param _dev A reference to the device
 * @return 0
 */
int win2030_noc_prof_debugfs_init(struct device *_dev)
{
	struct win2030_noc_device *noc_device = dev_get_drvdata(_dev);
	struct device_node *np = _dev->of_node;
	struct dentry *dir, *d;
	char name[32];

	scnprintf(name, ARRAY_SIZE(name), "%s_prof", np->name);

	dir = debugfs_create_dir(name, win2030_noc_ctrl.win2030_noc_root_debug_dir);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	noc_device->prof->dir = dir;

	d = debugfs_create_file("results", S_IRUGO, dir, noc_device,
				&noc_prof_results_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);
	d = debugfs_create_file("config", S_IRUGO | S_IWUSR, dir, noc_device,
				&noc_prof_config_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);
	d = debugfs_create_file("run", S_IRUGO | S_IWUSR, dir, noc_device,
				&noc_prof_run_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);
	d = debugfs_create_file("readme.txt", S_IRUGO, dir, noc_device,
				&noc_prof_readme_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);

	return 0;
}
