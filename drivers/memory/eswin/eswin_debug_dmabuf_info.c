/*
 * this program is mostly using when allocating memory from eswin_rsvmem and an
 * out-of-memory occurs, will dump all dmabuf info to /proc/eswin/dmabuf_info.
 *
 * Copyright 2023, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>

MODULE_IMPORT_NS(DMA_BUF);
extern void dma_buf_call_show(struct seq_file *s);

static struct proc_dir_entry *entry;
static DEFINE_MUTEX(lock);
static bool has_dumped;
static const size_t BUF_SZ = 4 * 1024 * 1024;
static struct seq_file seq = { .buf = NULL, .size = BUF_SZ };

/* outer user call this function dump all dmabuf info when mmz OOM */
void eswin_dmabuf_dump_info(void)
{
	mutex_lock(&lock);
	if (!has_dumped) {
		seq.count = 0;
		dma_buf_call_show(&seq);
		has_dumped = true;
	}
	mutex_unlock(&lock);
}
EXPORT_SYMBOL(eswin_dmabuf_dump_info);

static int debug_dmabuf_show(struct seq_file *m, void *v)
{
	mutex_lock(&lock);
	if (!has_dumped) {
		seq.count = 0;
		dma_buf_call_show(&seq);
	} /* user read never change has_dumped */

	if (seq.count != 0) {
		seq_write(m, seq.buf, seq.count);
		if (seq_has_overflowed(&seq))
			seq_puts(m, "\nWarning: 4 M buffer limit reached, output maybe truncated\n");
	}
	mutex_unlock(&lock);
	return 0;
}

static int debug_dmabuf_open(struct inode *i, struct file *f)
{
	return single_open(f, debug_dmabuf_show, NULL);
}

/* user write only clear flag has_dumped */
static ssize_t debug_dmabuf_write(struct file *f, const char __user *ub,
			      size_t cnt, loff_t *p)
{
	mutex_lock(&lock);
	has_dumped = false;
	mutex_unlock(&lock);
	return cnt;
}

static const struct proc_ops debug_dmabuf_ops = {
	.proc_open    = debug_dmabuf_open,
	.proc_read    = seq_read,
	.proc_write   = debug_dmabuf_write,
	.proc_lseek   = seq_lseek,
	.proc_release = single_release,
};

static int __init debug_dmabuf_init(void)
{
	seq.buf = vmalloc(BUF_SZ);
	if (!seq.buf)
		return -ENOMEM;

	entry = proc_create("eswin/dmabuf_info", 0444, NULL, &debug_dmabuf_ops);
	if (!entry) {
		vfree(seq.buf);
		return -ENOMEM;
	}

	return 0;
}

static void __exit debug_dmabuf_exit(void)
{
	proc_remove(entry);
	vfree(seq.buf);
}

module_init(debug_dmabuf_init);
module_exit(debug_dmabuf_exit);
MODULE_LICENSE("GPL v2");