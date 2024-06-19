// SPDX-License-Identifier: GPL-2.0-only
/*
 * ESWIN proc APIs for MMZ_VB
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    HuangYiFeng<huangyifeng@eswincomputing.com>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "include/linux/es_proc.h"

static struct list_head list;
static es_proc_entry_t *proc_entry = NULL;

static int es_seq_show(struct seq_file *s, void *p)
{
	es_proc_entry_t *oldsentry = s->private;
	es_proc_entry_t sentry;

	if (oldsentry == NULL) {
		pr_err("%s %d- parameter invalid!\n", __func__,__LINE__);
		return -1;
	}
	memset(&sentry, 0, sizeof(es_proc_entry_t));
	/* only these two parameters are used */
	sentry.seqfile = s;
	sentry.private = oldsentry->private;
	oldsentry->read(&sentry);
	return 0;
}

static ssize_t es_procwrite(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	es_proc_entry_t *item = pde_data(file_inode(file));

	if ((item != NULL) && (item->write != NULL)) {
		return item->write(item, buf, count, (long long *)ppos);
	}

	return -ENOSYS;
}

static int es_procopen(struct inode *inode, struct file *file)
{
	es_proc_entry_t *sentry = pde_data(inode);

	if ((sentry != NULL) && (sentry->open != NULL)) {
		sentry->open(sentry);
	}
	return single_open(file, es_seq_show, sentry);
}

static const struct proc_ops es_proc_ops = {
	.proc_open = es_procopen,
	.proc_read = seq_read,
	.proc_write = es_procwrite,
	.proc_lseek = seq_lseek,
	.proc_release = single_release
};

es_proc_entry_t *es_create_proc(const char *name, es_proc_entry_t *parent)
{
	struct proc_dir_entry *entry = NULL;
	es_proc_entry_t *sentry = NULL;

	sentry = kzalloc(sizeof(struct es_proc_dir_entry), GFP_KERNEL);
	if (sentry == NULL) {
		pr_err("%s %d - kmalloc failed!\n",__func__,__LINE__);
		return NULL;
	}

	strncpy(sentry->name, name, sizeof(sentry->name) - 1);

	if (parent == NULL) {
		entry = proc_create_data(name, 0, NULL, &es_proc_ops, sentry);
	} else {
		entry = proc_create_data(name, 0, parent->proc_dir_entry, &es_proc_ops, sentry);
	}
	if (entry == NULL) {
		pr_err("%s %d - create_proc_entry failed!\n",__func__,__LINE__);
		kfree(sentry);
		sentry = NULL;
		return NULL;
	}
	sentry->proc_dir_entry = entry;
	sentry->open = NULL;

	list_add_tail(&(sentry->node), &list);
	return sentry;
}

void es_remove_proc(const char *name, es_proc_entry_t *parent)
{
	struct es_proc_dir_entry *sproc = NULL;

	if (name == NULL) {
		pr_err("%s %d - parameter invalid!\n",__func__,__LINE__);
		return;
	}
	if (parent != NULL) {
		remove_proc_entry(name, parent->proc_dir_entry);
	} else {
		remove_proc_entry(name, NULL);
	}
	list_for_each_entry(sproc, &list, node) {
		if (strncmp(sproc->name, name, sizeof(sproc->name)) == 0) {
			list_del(&(sproc->node));
			break;
		}
	}
	if (sproc != NULL) {
		kfree(sproc);
	}
}

es_proc_entry_t *es_create_proc_entry(const char *name,
					  es_proc_entry_t *parent)
{
	parent = proc_entry;

	return es_create_proc(name, parent);
}
EXPORT_SYMBOL(es_create_proc_entry);

void es_remove_proc_entry(const char *name, es_proc_entry_t *parent)
{
	parent = proc_entry;
	es_remove_proc(name, parent);
	return;
}
EXPORT_SYMBOL(es_remove_proc_entry);

es_proc_entry_t *es_proc_mkdir(const char *name, es_proc_entry_t *parent)
{
	struct proc_dir_entry *proc = NULL;
	struct es_proc_dir_entry *sproc = NULL;

	sproc = kzalloc(sizeof(struct es_proc_dir_entry), GFP_KERNEL);
	if (sproc == NULL) {
		pr_err("%s %d - kmalloc failed!\n",__func__,__LINE__);
		return NULL;
	}

	strncpy(sproc->name, name, sizeof(sproc->name) - 1);

	if (parent != NULL) {
		proc = proc_mkdir_data(name, 0, parent->proc_dir_entry, sproc);
	} else {
		proc = proc_mkdir_data(name, 0, NULL, sproc);
	}
	if (proc == NULL) {
		pr_err("%s %d - proc_mkdir failed!\n",__func__,__LINE__);
		kfree(sproc);
		sproc = NULL;
		return NULL;
	}
	sproc->proc_dir_entry = proc;

	list_add_tail(&(sproc->node), &list);
	return sproc;
}
EXPORT_SYMBOL(es_proc_mkdir);

void es_remove_proc_root(const char *name, es_proc_entry_t *parent)
{
	struct es_proc_dir_entry *sproc = NULL;

	if (name == NULL) {
		pr_err("%s %d - parameter invalid!\n",__func__,__LINE__);
		return;
	}
	if (parent != NULL) {
		remove_proc_entry(name, parent->proc_dir_entry);
	} else {
		remove_proc_entry(name, NULL);
	}
	list_for_each_entry(sproc, &list, node) {
		if (strncmp(sproc->name, name, sizeof(sproc->name)) == 0) {
			list_del(&(sproc->node));
			break;
		}
	}
	if (sproc != NULL) {
		kfree(sproc);
	}
}

int es_seq_printf(es_proc_entry_t *entry, const char *fmt, ...)
{
	struct seq_file *s = (struct seq_file *)(entry->seqfile);
	va_list args;
	int r = 0;

	va_start(args, fmt);
	seq_vprintf(s, fmt, args);
	va_end(args);

	return r;
}
EXPORT_SYMBOL(es_seq_printf);

static int __init es_proc_init(void)
{
	INIT_LIST_HEAD(&list);
	proc_entry = es_proc_mkdir("umap", NULL);
	if (proc_entry == NULL) {
		pr_err("init, proc mkdir error!\n");
		return -EPERM;
	}
	return 0;
}

static void __exit es_proc_exit(void)
{
	es_remove_proc_root("umap", NULL);
}

module_init(es_proc_init);
module_exit(es_proc_exit);

MODULE_DESCRIPTION("ES Procfile Driver");
MODULE_AUTHOR("huangyifeng@eswincomputing.com");
MODULE_LICENSE("GPL v2");
