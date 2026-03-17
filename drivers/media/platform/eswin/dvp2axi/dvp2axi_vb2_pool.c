// SPDX-License-Identifier: GPL-2.0
/*
 * DVP2AXI Driver - VB2 Pool (Pre-allocated per-buffer coherent)
 *
 * Copyright (C) 2026 Beijing ESWIN Computing Technology Co., Ltd.
 *
 * feature:
 *   1. alloc dma coherent buffer
 *   2. free buffer to pool
 *   3. pool auto grow
 *   4. sysfs to adjust pool_size / pool_count / auto_grow / max_total
 *   5. write pool_size to trigger resize buffer: free previous buffers, alloc new
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "dvp2axi_vb2.h"

#define AUTO_GROW_DEFAULT	4
#define MAX_TOTAL_DEFAULT	64

static inline struct dvp2axi_mem_pool *kobj_to_pool(struct kobject *kobj)
{
	return container_of(kobj, struct dvp2axi_mem_pool, kobj);
}

static struct dvp2axi_mem_block *_block_create(struct dvp2axi_mem_pool *pool,
					       size_t size)
{
	struct dvp2axi_mem_block *block;
	void *vaddr;
	dma_addr_t dma_addr;

	vaddr = dma_alloc_coherent(pool->dev, size, &dma_addr, GFP_KERNEL);
	if (!vaddr)
		return NULL;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (!block) {
		dma_free_coherent(pool->dev, size, vaddr, dma_addr);
		return NULL;
	}

	block->vaddr = vaddr;
	block->dma_addr = dma_addr;
	block->size = size;
	block->pool = pool;
	block->is_dynamic = false;
	return block;
}

static void _block_destroy(struct dvp2axi_mem_block *block)
{
	dma_free_coherent(block->pool->dev, block->size,
			  block->vaddr, block->dma_addr);
	kfree(block);
}

struct dvp2axi_mem_pool *dvp2axi_mem_pool_create(struct device *dev,
						  const char *name,
						  size_t buf_size,
						  unsigned int num_bufs)
{
	struct dvp2axi_mem_pool *pool;
	unsigned int i;

	dev_info(dev, "Creating pool '%s': %u x %zuMB\n",
		 name, num_bufs, buf_size / (1024 * 1024));

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	buf_size = ALIGN(buf_size, PAGE_SIZE);
	pool->dev = dev;
	pool->name = name;
	pool->buf_size = buf_size;
	pool->auto_grow = AUTO_GROW_DEFAULT;
	pool->max_total = MAX_TOTAL_DEFAULT;
	spin_lock_init(&pool->lock);
	INIT_LIST_HEAD(&pool->free_list);
	INIT_LIST_HEAD(&pool->used_list);

	for (i = 0; i < num_bufs; i++) {
		struct dvp2axi_mem_block *block;

		block = _block_create(pool, buf_size);
		if (!block) {
			dev_err(dev, "Alloc buffer %u/%u failed\n", i, num_bufs);
			goto err_free_all;
		}
		list_add_tail(&block->list, &pool->free_list);
		pool->num_total++;
		pool->num_free++;
	}

	dev_info(dev, "Pool '%s' created: %u buffers (%zuMB each)\n",
		 name, pool->num_total, buf_size / (1024 * 1024));
	return pool;

err_free_all:
	dvp2axi_mem_pool_destroy(pool);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_create);

void dvp2axi_mem_pool_destroy(struct dvp2axi_mem_pool *pool)
{
	struct dvp2axi_mem_block *block = NULL, *tmp = NULL;
	unsigned int leaked = 0;

	if (!pool)
		return;

	dvp2axi_mem_pool_sysfs_cleanup(pool);

	dev_info(pool->dev, "Destroying pool '%s'\n", pool->name);

	list_for_each_entry_safe(block, tmp, &pool->free_list, list) {
		list_del(&block->list);
		_block_destroy(block);
	}

	list_for_each_entry_safe(block, tmp, &pool->used_list, list) {
		dev_warn(pool->dev, "Leaked: dma=%pad, size=%zu\n",
			 &block->dma_addr, block->size);
		list_del(&block->list);
		_block_destroy(block);
		leaked++;
	}

	dev_info(pool->dev, "Pool '%s' destroyed: leaked=%u\n",
		 pool->name, leaked);
	kfree(pool);
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_destroy);

unsigned int dvp2axi_mem_pool_grow(struct dvp2axi_mem_pool *pool,
				   unsigned int count)
{
	struct dvp2axi_mem_block *block;
	unsigned long flags;
	unsigned int i, added = 0;

	if (!pool || count == 0)
		return 0;

	for (i = 0; i < count; i++) {
		block = _block_create(pool, pool->buf_size);
		if (!block) {
			dev_warn(pool->dev, "Grow: alloc %u/%u failed\n",
				 i, count);
			break;
		}
		spin_lock_irqsave(&pool->lock, flags);
		list_add_tail(&block->list, &pool->free_list);
		pool->num_total++;
		pool->num_free++;
		spin_unlock_irqrestore(&pool->lock, flags);
		added++;
	}

	if (added)
		dev_info(pool->dev, "Pool '%s' grew +%u → %u total\n",
			 pool->name, added, pool->num_total);
	return added;
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_grow);

unsigned int dvp2axi_mem_pool_shrink(struct dvp2axi_mem_pool *pool,
				     unsigned int count)
{
	struct dvp2axi_mem_block *block = NULL, *tmp = NULL;
	unsigned long flags;
	unsigned int removed = 0;

	if (!pool || count == 0)
		return 0;

	spin_lock_irqsave(&pool->lock, flags);
	list_for_each_entry_safe_reverse(block, tmp, &pool->free_list, list) {
		if (removed >= count)
			break;
		list_del(&block->list);
		pool->num_total--;
		pool->num_free--;
		spin_unlock_irqrestore(&pool->lock, flags);
		_block_destroy(block);
		removed++;
		spin_lock_irqsave(&pool->lock, flags);
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	if (removed < count)
		dev_warn(pool->dev,
			 "Shrink: only %u/%u were free\n", removed, count);

	if (removed)
		dev_info(pool->dev, "Pool '%s' shrank -%u → %u total\n",
			 pool->name, removed, pool->num_total);
	return removed;
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_shrink);

/**
 * dvp2axi_mem_pool_resize() - resize buffer and rebuild pool
 * @pool:     pool
 * @new_size: new buffer size
 *
 * First release the old buffer（free_list + used_list,
 * then reallocate according to the new size
 * 
 * Must be called after all buffers have been returned（used_list is empty），
 * otherwise, it returns -EBUSY.
 */
int dvp2axi_mem_pool_resize(struct dvp2axi_mem_pool *pool, size_t new_size)
{
	struct dvp2axi_mem_block *block = NULL, *tmp = NULL;
	unsigned long flags;
	unsigned int target_count;
	unsigned int i;

	if (!pool || new_size == 0)
		return -EINVAL;

	if (new_size == pool->buf_size)
		return 0;

	new_size = ALIGN(new_size, PAGE_SIZE);

	spin_lock_irqsave(&pool->lock, flags);
	if (!list_empty(&pool->used_list)) {
		unsigned int in_use = 0;
		list_for_each_entry(block, &pool->used_list, list)
			in_use++;
		spin_unlock_irqrestore(&pool->lock, flags);
		dev_warn(pool->dev,
			 "Resize: %u buffers still in use, cannot resize\n",
			 in_use);
		return -EBUSY;
	}
	target_count = pool->num_total;
	spin_unlock_irqrestore(&pool->lock, flags);

	dev_info(pool->dev,
		 "Resizing pool '%s': %zuMB → %zuMB, count=%u\n",
		 pool->name,
		 pool->buf_size / (1024 * 1024),
		 new_size / (1024 * 1024),
		 target_count);

	spin_lock_irqsave(&pool->lock, flags);
	list_for_each_entry_safe(block, tmp, &pool->free_list, list) {
		list_del(&block->list);
		spin_unlock_irqrestore(&pool->lock, flags);
		_block_destroy(block);
		spin_lock_irqsave(&pool->lock, flags);
	}
	pool->num_total = 0;
	pool->num_free = 0;
	spin_unlock_irqrestore(&pool->lock, flags);

	pool->buf_size = new_size;

	for (i = 0; i < target_count; i++) {
		block = _block_create(pool, new_size);
		if (!block) {
			dev_err(pool->dev,
				"Resize: alloc %u/%u failed, rolling back\n",
				i, target_count);
			pool->num_total = i;
			pool->num_free = i;
			return -ENOMEM;
		}
		spin_lock_irqsave(&pool->lock, flags);
		list_add_tail(&block->list, &pool->free_list);
		pool->num_total++;
		pool->num_free++;
		spin_unlock_irqrestore(&pool->lock, flags);
	}

	pool->peak_used = 0;

	dev_info(pool->dev,
		 "Pool '%s' resized: %u x %zuMB\n",
		 pool->name, pool->num_total,
		 pool->buf_size / (1024 * 1024));

	return 0;
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_resize);

struct dvp2axi_mem_block *dvp2axi_mem_pool_alloc(struct dvp2axi_mem_pool *pool,
						  size_t size)
{
	struct dvp2axi_mem_block *block;
	unsigned long flags;

	if (!pool)
		return ERR_PTR(-EINVAL);

	size = ALIGN(size, PAGE_SIZE);

	/* size > buf_size: alloc new buffer which not add into pool  */
	if (size > pool->buf_size) {
		dev_info(pool->dev,
			 "size %zu > pool %zu, dynamic alloc\n",
			 size, pool->buf_size);

		block = _block_create(pool, size);
		if (!block)
			return ERR_PTR(-ENOMEM);
		block->is_dynamic = true;

		spin_lock_irqsave(&pool->lock, flags);
		list_add(&block->list, &pool->used_list);
		spin_unlock_irqrestore(&pool->lock, flags);

		atomic_inc(&pool->alloc_count);
		atomic_inc(&pool->dynamic_count);
		return block;
	}

	spin_lock_irqsave(&pool->lock, flags);

	if (list_empty(&pool->free_list)) {
		if (pool->auto_grow > 0 &&
		    (pool->max_total == 0 ||
		     pool->num_total < pool->max_total)) {
			unsigned int grow_n = pool->auto_grow;

			spin_unlock_irqrestore(&pool->lock, flags);

			dev_info(pool->dev,
				 "Pool exhausted, auto-grow %u (max=%u)\n",
				 grow_n, pool->max_total);

			if (dvp2axi_mem_pool_grow(pool, grow_n) > 0) {
				spin_lock_irqsave(&pool->lock, flags);
				if (!list_empty(&pool->free_list))
					goto got_buffer;
				spin_unlock_irqrestore(&pool->lock, flags);
			}
		}
		return ERR_PTR(-ENOMEM);
	}

got_buffer:
	block = list_first_entry(&pool->free_list,
				 struct dvp2axi_mem_block, list);
	list_move(&block->list, &pool->used_list);
	pool->num_free--;

	if (pool->num_total - pool->num_free > pool->peak_used)
		pool->peak_used = pool->num_total - pool->num_free;

	spin_unlock_irqrestore(&pool->lock, flags);

	atomic_inc(&pool->alloc_count);
	return block;
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_alloc);

void dvp2axi_mem_pool_free(struct dvp2axi_mem_block *block)
{
	struct dvp2axi_mem_pool *pool;
	unsigned long flags;

	if (!block || !block->pool)
		return;

	pool = block->pool;

	if (block->is_dynamic) {
		spin_lock_irqsave(&pool->lock, flags);
		list_del(&block->list);
		spin_unlock_irqrestore(&pool->lock, flags);

		_block_destroy(block);
		atomic_inc(&pool->free_count);
		atomic_dec(&pool->dynamic_count);
		return;
	}

	spin_lock_irqsave(&pool->lock, flags);
	list_move(&block->list, &pool->free_list);
	pool->num_free++;
	spin_unlock_irqrestore(&pool->lock, flags);

	atomic_inc(&pool->free_count);
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_free);

/* ============================================================
 * sysfs
 * ============================================================ */

static ssize_t pool_count_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	return sprintf(buf, "%u\n", pool->num_total);
}

static ssize_t pool_count_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t count)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	unsigned int target;

	if (kstrtouint(buf, 10, &target))
		return -EINVAL;

	if (target == pool->num_total)
		return count;

	if (target > pool->num_total) {
		dvp2axi_mem_pool_grow(pool, target - pool->num_total);
	} else {
		dvp2axi_mem_pool_shrink(pool, pool->num_total - target);
	}

	return count;
}

static ssize_t pool_free_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	return sprintf(buf, "%u\n", pool->num_free);
}

static ssize_t pool_size_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	return sprintf(buf, "%zu\n", pool->buf_size);
}

static ssize_t pool_size_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	size_t new_size;
	int ret;

	if (kstrtoul(buf, 10, &new_size))
		return -EINVAL;

	if (new_size == pool->buf_size)
		return count;

	ret = dvp2axi_mem_pool_resize(pool, new_size);
	if (ret)
		return ret;

	return count;
}

static ssize_t pool_peak_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	return sprintf(buf, "%u\n", pool->peak_used);
}

static ssize_t auto_grow_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	return sprintf(buf, "%u\n", pool->auto_grow);
}

static ssize_t auto_grow_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	unsigned int val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	pool->auto_grow = val;
	return count;
}

static ssize_t max_total_show(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	return sprintf(buf, "%u\n", pool->max_total);
}

static ssize_t max_total_store(struct kobject *kobj,
			       struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	struct dvp2axi_mem_pool *pool = kobj_to_pool(kobj);
	unsigned int val;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	pool->max_total = val;
	return count;
}

static struct kobj_attribute pool_count_attr =
	__ATTR(pool_count, 0644, pool_count_show, pool_count_store);
static struct kobj_attribute pool_free_attr =
	__ATTR(pool_free, 0444, pool_free_show, NULL);
static struct kobj_attribute pool_size_attr =
	__ATTR(pool_size, 0644, pool_size_show, pool_size_store);
static struct kobj_attribute pool_peak_attr =
	__ATTR(pool_peak, 0444, pool_peak_show, NULL);
static struct kobj_attribute auto_grow_attr =
	__ATTR(auto_grow, 0644, auto_grow_show, auto_grow_store);
static struct kobj_attribute max_total_attr =
	__ATTR(max_total, 0644, max_total_show, max_total_store);

static struct attribute *pool_sysfs_attrs[] = {
	&pool_count_attr.attr,
	&pool_free_attr.attr,
	&pool_size_attr.attr,
	&pool_peak_attr.attr,
	&auto_grow_attr.attr,
	&max_total_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pool_sysfs);

static void pool_kobj_release(struct kobject *kobj)
{

}

static struct kobj_type pool_ktype = {
	.release	= pool_kobj_release,
	.sysfs_ops	= &kobj_sysfs_ops,
	.default_groups	= pool_sysfs_groups,
};

int dvp2axi_mem_pool_sysfs_init(struct dvp2axi_mem_pool *pool,
				struct kobject *parent_kobj)
{
	int ret;

	if (!pool || !parent_kobj)
		return -EINVAL;

	ret = kobject_init_and_add(&pool->kobj, &pool_ktype,
				   parent_kobj, "pool");
	if (ret) {
		kobject_put(&pool->kobj);
		return ret;
	}

	dev_info(pool->dev, "Pool sysfs at %s/pool/\n",
		 kobject_name(parent_kobj));
	return 0;
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_sysfs_init);

void dvp2axi_mem_pool_sysfs_cleanup(struct dvp2axi_mem_pool *pool)
{
	if (!pool)
		return;
	if (pool->kobj.state_initialized)
		kobject_put(&pool->kobj);
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_sysfs_cleanup);

void dvp2axi_mem_pool_dump_stats(struct dvp2axi_mem_pool *pool)
{
	if (!pool)
		return;

	dev_info(pool->dev,
		 "Pool '%s': total=%u free=%u peak=%u dynamic=%d "
		 "buf_size=%zuMB auto_grow=%u max=%u\n",
		 pool->name, pool->num_total, pool->num_free,
		 pool->peak_used, atomic_read(&pool->dynamic_count),
		 pool->buf_size / (1024 * 1024),
		 pool->auto_grow, pool->max_total);
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_dump_stats);

size_t dvp2axi_mem_pool_get_free(struct dvp2axi_mem_pool *pool)
{
	return pool ? pool->num_free : 0;
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_get_free);

size_t dvp2axi_mem_pool_get_used(struct dvp2axi_mem_pool *pool)
{
	return pool ? (pool->num_total - pool->num_free) : 0;
}
EXPORT_SYMBOL_GPL(dvp2axi_mem_pool_get_used);
