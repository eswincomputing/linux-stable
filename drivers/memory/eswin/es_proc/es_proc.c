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
#include <linux/es_proc.h>

static LIST_HEAD(list);
static es_proc_entry_t *proc_entry = NULL;
static DEFINE_MUTEX(proc_lock);
static DEFINE_MUTEX(list_lock);

es_proc_entry_t *_es_proc_mkdir(const char *name, umode_t mode, es_proc_entry_t *parent);
static int es_seq_show(struct seq_file *s, void *p)
{
	es_proc_entry_t *oldsentry = s->private;
	es_proc_entry_t sentry;

	if (oldsentry == NULL) {
		pr_err("parameter invalid!\n");
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

es_proc_entry_t *es_create_proc(const char *name, umode_t mode, es_proc_entry_t *parent)
{
	struct proc_dir_entry *entry = NULL;
	es_proc_entry_t *sentry = NULL;

	sentry = kzalloc(sizeof(struct es_proc_dir_entry), GFP_KERNEL);
	if (sentry == NULL) {
		pr_err("kmalloc failed!\n");
		return NULL;
	}

	strncpy(sentry->name, name, sizeof(sentry->name) - 1);
	pr_debug("create file %s with mode %o\n", name, (unsigned)mode);
	if (parent == NULL) {
		entry = proc_create_data(name, mode, NULL, &es_proc_ops, sentry);
	} else {
		entry = proc_create_data(name, mode, parent->proc_dir_entry, &es_proc_ops, sentry);
	}
	if (entry == NULL) {
		pr_err("create_proc_entry failed!\n");
		kfree(sentry);
		sentry = NULL;
		return NULL;
	}
	sentry->proc_dir_entry = entry;
	sentry->open = NULL;
	sentry->parent = parent;

	list_add_tail(&(sentry->node), &list);
	return sentry;
}

es_proc_entry_t *es_create_proc_entry(const char *name, umode_t mode,
					  es_proc_entry_t *parent)
{
	mutex_lock(&proc_lock);
	if (!proc_entry )
		proc_entry = _es_proc_mkdir("eswin", 0755, NULL);
	mutex_unlock(&proc_lock);
	if (!proc_entry )
		return NULL;

	/* If NULL, create entry under the eswin dir, otherwise under the specified parent*/
	if (parent == NULL)
		parent = proc_entry;

	return es_create_proc(name, mode, parent);
}
EXPORT_SYMBOL(es_create_proc_entry);

void _es_remove_proc_entry(const char *name, es_proc_entry_t *parent)
{
	struct es_proc_dir_entry *sproc, *tmp;
	struct es_proc_dir_entry *child;
	bool found = false;

	if (name == NULL) {
		pr_err("parameter invalid!\n");
		return;
	}
	mutex_lock(&list_lock);
	list_for_each_entry_safe(sproc, tmp, &list, node) {
		if (strncmp(sproc->name, name, sizeof(sproc->name)) != 0)
			continue;
		// got target dir, then checking if it's contains any child node
		list_for_each_entry(child, &list, node) {
			if (child->parent == sproc) {
				// some node's parent point to target entry, do not remove.
				pr_err("Error: directory %s is not empty!\n", name);
				mutex_unlock(&list_lock);
				return;
			}
		}

		// remove
		found = true;
		remove_proc_entry(name, parent == NULL ? NULL : parent->proc_dir_entry);
		list_del(&sproc->node);
		kfree(sproc);
		break;
	}

	mutex_unlock(&list_lock);
	if (found)
		pr_debug("entry %s with parent=%s removed\n", name, parent ? parent->name : "eswin");
	else
		pr_err("entry %s with parent=%s not found\n", name, parent ? parent->name : "eswin");
}

void es_remove_proc_entry(const char *name, es_proc_entry_t *parent)
{
	mutex_lock(&proc_lock);
	if (WARN_ON(!proc_entry))
		pr_err("remove %s before es_proc initialize\n", name);
	mutex_unlock(&proc_lock);

	/* If NULL, remove entry under the eswin dir, otherwise under the specified parent*/
	if (parent == NULL)
		parent = proc_entry;

	return _es_remove_proc_entry(name, parent);
}
EXPORT_SYMBOL(es_remove_proc_entry);

es_proc_entry_t *_es_proc_mkdir(const char *name, umode_t mode, es_proc_entry_t *parent)
{
	struct proc_dir_entry *proc = NULL;
	struct es_proc_dir_entry *sproc = NULL;

	sproc = kzalloc(sizeof(struct es_proc_dir_entry), GFP_KERNEL);
	if (sproc == NULL) {
		pr_err("kmalloc failed!\n");
		return NULL;
	}

	strncpy(sproc->name, name, sizeof(sproc->name) - 1);

	if (parent != NULL) {
		proc = proc_mkdir_data(name, mode, parent->proc_dir_entry, sproc);
	} else {
		proc = proc_mkdir_data(name, mode, NULL, sproc);
	}
	if (proc == NULL) {
		pr_err("proc_mkdir failed!\n");
		kfree(sproc);
		sproc = NULL;
		return NULL;
	}
	sproc->proc_dir_entry = proc;
	sproc->parent = parent;
	INIT_LIST_HEAD(&sproc->node);

	mutex_lock(&list_lock);
	list_add_tail(&(sproc->node), &list);
	mutex_unlock(&list_lock);
	return sproc;
}

es_proc_entry_t *es_proc_mkdir(const char *name, umode_t mode, es_proc_entry_t *parent)
{
	mutex_lock(&proc_lock);
	if (!proc_entry )
		proc_entry = _es_proc_mkdir("eswin", 0755, NULL);
	mutex_unlock(&proc_lock);
	if (!proc_entry )
		return NULL;

	/* If NULL, create entry under the eswin dir, otherwise under the specified parent*/
	if (parent == NULL)
		parent = proc_entry;

	return  _es_proc_mkdir(name, mode, parent);
}
EXPORT_SYMBOL(es_proc_mkdir);

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

