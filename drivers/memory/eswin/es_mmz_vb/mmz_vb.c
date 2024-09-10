// SPDX-License-Identifier: GPL-2.0-only
/*
 * ESWIN MMZ VB driver. MMZ VB stands for Media Memory Zone Video Buffer.
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/dma-map-ops.h>
#include <linux/highmem.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/dmaengine.h>
#include <linux/uaccess.h>	/* For copy_to_user/put_user/... */
#include <linux/of_address.h>
#include <crypto/hash.h>
#include <linux/delay.h>
#include <linux/hashtable.h>
#include <linux/es_proc.h>
#include <linux/dmabuf-heap-import-helper.h>
#include "include/linux/mmz_vb_type.h" /*must include before es_vb_user.h*/
#include <uapi/linux/es_vb_user.h>
#include <uapi/linux/mmz_vb.h>
#include "include/linux/mmz_vb.h"

MODULE_IMPORT_NS(DMA_BUF);

#define DRIVER_NAME 	"mmz_vb"
#define MMZ_VB_DMABUF_NAME		"mmz_vb_dmabuf"
#define MMZ_VB_DMABUF_SPLITTED_NAME	"mmz_vb_dmabuf_splitted"

#define vb_fmt(fmt)	"[%s-MMZ_VB]: " fmt
#define info_fmt(fmt)	vb_fmt("%s[%d]: " fmt), "INFO",	\
		        __func__, __LINE__
#define dbg_fmt(fmt)	vb_fmt("%s[%d]: " fmt), "DEBUG",	\
		        __func__, __LINE__
#define err_fmt(fmt)	vb_fmt("%s[%d]: " fmt), "ERROR",	\
		        __func__, __LINE__

#define vb_info(fmt, args...) \
	do {							\
		printk(KERN_INFO info_fmt(fmt), ##args);	\
	} while (0)

#define vb_debug(fmt, args...) \
	do {							\
		printk(KERN_DEBUG dbg_fmt(fmt), ##args);	\
	} while (0)
#define vb_err(fmt, args...) \
	do {							\
		printk(KERN_ERR err_fmt(fmt), ##args);		\
	} while (0)

static struct device *mmz_vb_dev;
static struct mmz_vb_priv *g_mmz_vb_priv = NULL;

struct mmz_vb_priv {
	struct device *dev;
	struct esVB_K_MMZ_S partitions;
	atomic_t allocBlkcnt;   /*total block allocated*/
	struct hlist_head ht[VB_UID_MAX][ES_VB_MAX_MOD_POOL];
	struct rw_semaphore pool_lock[VB_UID_MAX];
	unsigned long cfg_flag[VB_UID_MAX];	/*flag for pVbConfig*/
	struct esVB_CONFIG_S *pVbConfig[VB_UID_MAX];
	struct mutex cfg_lock[VB_UID_MAX];
};

#define do_vb_pool_size(pPool)	(pPool->poolCfg.blkCnt * pPool->poolCfg.blkSize)
static int vb_find_pool_by_id_unlock(VB_POOL poolId, struct esVB_K_POOL_INFO_S **ppPool);
static int vb_find_pool_by_id(VB_POOL poolId, struct esVB_K_POOL_INFO_S **ppPool);
static int vb_pool_size(VB_POOL poolId, u64 *pPoolSize);
// static int vb_flush_pool(struct esVB_FLUSH_POOL_CMD_S *flushPoolCmd);
static int vb_get_block(struct esVB_GET_BLOCK_CMD_S *getBlkCmd, struct esVB_K_BLOCK_INFO_S **ppBlk);
static void vb_release_block(struct esVB_K_BLOCK_INFO_S *pBlk);
static int vb_pool_get_free_block_cnt_unlock(struct esVB_K_POOL_INFO_S *pool);
static int vb_is_splitted_blk(int fd, bool *isSplittedBlk);
static int vb_blk_to_pool(struct esVB_BLOCK_TO_POOL_CMD_S *blkToPoolCmd);
static int vb_get_blk_offset(struct esVB_GET_BLOCKOFFSET_CMD_S *getBlkOffsetCmd);
static int vb_split_dmabuf(struct esVB_SPLIT_DMABUF_CMD_S *splitDmabufCmd);
static int vb_get_dmabuf_refcnt(struct esVB_DMABUF_REFCOUNT_CMD_S *getDmabufRefCntCmd);
static int vb_retrieve_mem_node(struct esVB_RETRIEVE_MEM_NODE_CMD_S *retrieveMemNodeCmd);
static int vb_get_dmabuf_size(struct esVB_DMABUF_SIZE_CMD_S *getDmabufSizeCmd);
static int mmz_vb_pool_exit(void);
static int mmz_vb_init_memory_region(void);

/**
 * vb dmabuf releated struct
 *
 */
struct mmz_vb_buffer {
	struct esVB_K_BLOCK_INFO_S *pBlk;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct sg_table *table; // for buddy allocator
	struct page **pages;
	int vmap_cnt;
	void *vaddr;
};

struct mmz_vb_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
	bool mapped;
};

static struct sg_table *dup_sg_table(struct sg_table *table)
{
	struct sg_table *new_table;
	int ret, i;
	struct scatterlist *sg, *new_sg;

	new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(new_table, table->orig_nents, GFP_KERNEL);
	if (ret) {
		kfree(new_table);
		return ERR_PTR(-ENOMEM);
	}

	new_sg = new_table->sgl;
	for_each_sgtable_sg(table, sg, i) {
		sg_set_page(new_sg, sg_page(sg), sg->length, sg->offset);
		new_sg = sg_next(new_sg);
	}

	return new_table;
}

static int mmz_vb_attach(struct dma_buf *dmabuf,
			   struct dma_buf_attachment *attachment)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;
	struct mmz_vb_attachment *a;
	struct sg_table *table;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = dup_sg_table(buffer->table);
	if (IS_ERR(table)) {
		kfree(a);
		return -ENOMEM;
	}

	a->table = table;
	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);
	a->mapped = false;

	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void mmz_vb_detach(struct dma_buf *dmabuf,
			    struct dma_buf_attachment *attachment)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;
	struct mmz_vb_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(a->table);
	kfree(a->table);
	kfree(a);
}

static struct sg_table *mmz_vb_map_dma_buf(struct dma_buf_attachment *attachment,
					     enum dma_data_direction direction)
{
	struct mmz_vb_attachment *a = attachment->priv;
	struct sg_table *table =a->table;
	int ret;

	/* Skipt cache sync, since it takes a lot of time when import to device.
	*  It's the user's responsibility for guaranteeing the cache coherency by
	   flusing cache explicitly before importing to device.
	*/
	ret = dma_map_sgtable(attachment->dev, table, direction, DMA_ATTR_SKIP_CPU_SYNC);

	if (ret)
		return ERR_PTR(-ENOMEM);
	a->mapped = true;
	return table;
}

static void mmz_vb_unmap_dma_buf(struct dma_buf_attachment *attachment,
				   struct sg_table *table,
				   enum dma_data_direction direction)
{
	struct mmz_vb_attachment *a = attachment->priv;

	a->mapped = false;

	/* Skipt cache sync, since it takes a lot of time when unmap from device.
	*  It's the user's responsibility for guaranteeing the cache coherency after
	   the device has done processing the data.(For example, CPU do NOT read untill
	   the device has done)
	*/
	dma_unmap_sgtable(attachment->dev, table, direction, DMA_ATTR_SKIP_CPU_SYNC);
}

static int mmz_vb_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					     enum dma_data_direction direction)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;
	struct sg_table *table = buffer->table;
	struct scatterlist *sg;
	int i;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	/* Since the cache sync was skipped when mmz_vb_map_dma_buf/mmz_vb_unmap_dma_buf,
	   So force cache sync here when user call ES_SYS_MemFlushCache, eventhough there
	   is no device attached to this dmabuf.
	*/
	#ifndef QEMU_DEBUG
	for_each_sg(table->sgl, sg, table->orig_nents, i)
		arch_sync_dma_for_cpu(sg_phys(sg), sg->length, direction);
	#endif
	mutex_unlock(&buffer->lock);

	return 0;
}

static int mmz_vb_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					   enum dma_data_direction direction)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;
	struct sg_table *table = buffer->table;
	struct scatterlist *sg;
	int i;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	/* Since the cache sync was skipped while mmz_vb_map_dma_buf/mmz_vb_unmap_dma_buf,
	   So force cache sync here when user call ES_SYS_MemFlushCache, eventhough there
	   is no device attached to this dmabuf.
	*/
	#ifndef QEMU_DEBUG
	for_each_sg(table->sgl, sg, table->orig_nents, i)
		arch_sync_dma_for_device(sg_phys(sg), sg->length, direction);
	#endif
	mutex_unlock(&buffer->lock);

	return 0;
}

#if 0
static int mmz_vb_sync_cache_internal(struct dma_buf *dmabuf, enum dma_data_direction direction)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;
	struct sg_table *table = buffer->table;
	struct scatterlist *sg;
	int i;

	for_each_sg(table->sgl, sg, table->orig_nents, i)
		arch_sync_dma_for_device(sg_phys(sg), sg->length, direction);


	return 0;
}
#endif

static int mmz_vb_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;
	struct sg_table *table = buffer->table;
	unsigned long addr = vma->vm_start;
	unsigned long pgoff = vma->vm_pgoff, mapsize = 0;
	unsigned long size_remaining = vma->vm_end - vma->vm_start;
	struct scatterlist *sg;
	struct page *page = NULL;
	unsigned int nents = 0;
	int i;
	int ret;

	if ((vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) == 0)
		return -EINVAL;

	/* vm_private_data will be used by eswin-ipc-scpu.c.
	    ipc will import this dmabuf to get iova.
	*/
	vma->vm_private_data = dmabuf;

	/* support mman flag MAP_SHARED_VALIDATE | VM_NORESERVE, used to map uncached memory to user space.
	   The cache needs to be flush first since there might be dirty data in cache.
	 */
	if (vma->vm_flags & VM_NORESERVE) {
		vm_flags_clear(vma, VM_NORESERVE);
		#ifndef QEMU_DEBUG
		vma->vm_page_prot = pgprot_dmacoherent(vma->vm_page_prot);
		#endif
		/* skip sync cache, users should guarantee the cache is clean after done using it in
		   cached mode(i.e, ES_SYS_Mmap(SYS_CACHE_MODE_CACHED))
		*/
		#if 0
		pr_debug("%s uncached user memory, flush cache firstly!\n", __func__);
		if (mmz_vb_sync_cache_internal(dmabuf, DMA_TO_DEVICE)) {
			vb_err("%s, failed to flush cache!\n",__func__);
			return -EINVAL;
		}
		#endif
	}
	pr_debug("%s, size_remaining:0x%lx, pgoff:0x%lx, dmabuf->size:0x%lx, start_phys:0x%llx\n",
		__func__, size_remaining, pgoff, dmabuf->size, sg_phys(table->sgl));
	for_each_sg(table->sgl, sg, table->orig_nents, i) {
		pr_debug("sgl:%d, phys:0x%llx, length:0x%x\n", i, sg_phys(sg), sg->length);
		if (pgoff >= (sg->length >> PAGE_SHIFT)) {
			pgoff -= (sg->length >> PAGE_SHIFT);
			continue;
		}

		page = sg_page(sg);
		if (nents == 0) {
			mapsize = sg->length - (pgoff << PAGE_SHIFT);
			mapsize = min(size_remaining, mapsize);
			ret = remap_pfn_range(vma, addr, page_to_pfn(page) + pgoff, mapsize,
					vma->vm_page_prot);
			pr_debug("nents:%d, sgl:%d, pgoff:0x%lx, mapsize:0x%lx, phys:0x%llx\n",
				nents, i, pgoff, mapsize, pfn_to_phys(page_to_pfn(page) + pgoff));
		}
		else {
			mapsize = min((unsigned int)size_remaining, (sg->length));
			ret = remap_pfn_range(vma, addr, page_to_pfn(page), mapsize,
					vma->vm_page_prot);
			pr_debug("nents:%d, sgl:%d, mapsize:0x%lx, phys:0x%llx\n", nents, i, mapsize, page_to_phys(page));
		}
		pgoff = 0;
		nents++;

		if (ret)
			return ret;

		addr += mapsize;
		size_remaining -= mapsize;
		if (size_remaining == 0)
			return 0;
	}

	return 0;
}

static void *mmz_vb_do_vmap(struct dma_buf *dmabuf)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;
	struct esVB_K_POOL_INFO_S *pool = buffer->pBlk->pool;
	pgprot_t prot = PAGE_KERNEL;
	struct sg_table *table = buffer->table;
	int npages = PAGE_ALIGN(buffer->len) / PAGE_SIZE;
	struct page **pages = vmalloc(sizeof(struct page *) * npages);
	struct page **tmp = pages;
	struct sg_page_iter piter;
	void *vaddr;

	if (!pages)
		return ERR_PTR(-ENOMEM);

	for_each_sgtable_page(table, &piter, 0) {
		WARN_ON(tmp - pages >= npages);
		*tmp++ = sg_page_iter_page(&piter);
	}

	/* The property of this dmabuf in kernel space is determined by SYS_CACHE_MODE_E of the pool . */
	if (pool->poolCfg.enRemapMode == SYS_CACHE_MODE_NOCACHE) {
		pr_debug("%s uncached kernel buffer!\n", __func__);
		#ifndef QEMU_DEBUG
		prot = pgprot_dmacoherent(PAGE_KERNEL);
		#endif
	}
	else {
		pr_debug("%s cached kernel buffer!\n", __func__);
	}

	vaddr = vmap(pages, npages, VM_MAP, prot);
	vfree(pages);

	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

static int mmz_vb_vmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;
	void *vaddr;
	int ret = 0;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		dma_buf_map_set_vaddr(map, buffer->vaddr);
		goto out;
	}

	vaddr = mmz_vb_do_vmap(dmabuf);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto out;
	}
	buffer->vaddr = vaddr;
	buffer->vmap_cnt++;
	dma_buf_map_set_vaddr(map, buffer->vaddr);
out:
	mutex_unlock(&buffer->lock);

	return ret;
}

static void mmz_vb_vunmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
	dma_buf_map_clear(map);
}

static void mmz_vb_dma_buf_release(struct dma_buf *dmabuf)
{
	struct mmz_vb_buffer *buffer = dmabuf->priv;

	if (buffer->vmap_cnt > 0) {
		WARN(1, "%s: buffer still mapped in the kernel\n", __func__);
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}

	/* release block. In fact, release block to pool */
	vb_release_block(buffer->pBlk);

	kfree(buffer);
}

static const struct dma_buf_ops mmz_vb_buf_ops = {
	.attach = mmz_vb_attach,
	.detach = mmz_vb_detach,
	.map_dma_buf = mmz_vb_map_dma_buf,
	.unmap_dma_buf = mmz_vb_unmap_dma_buf,
	.begin_cpu_access = mmz_vb_dma_buf_begin_cpu_access,
	.end_cpu_access = mmz_vb_dma_buf_end_cpu_access,
	.mmap = mmz_vb_mmap,
	.vmap = mmz_vb_vmap,
	.vunmap = mmz_vb_vunmap,
	.release = mmz_vb_dma_buf_release,
};

static const unsigned int orders[] = {MAX_ORDER-1, 9, 0};
#define NUM_ORDERS ARRAY_SIZE(orders)

static struct page *alloc_largest_available(struct mem_block *memblock,
						unsigned long size,
						unsigned int max_order)
{
	struct page *page;
	int i;

	for (i = 0; i < NUM_ORDERS; i++) {
		if (size <  (PAGE_SIZE << orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		page = es_alloc_pages(memblock, orders[i]);
		if (!page)
			continue;
		return page;
	}
	return NULL;
}

static int vb_blk_pages_allocate(struct mem_block *memblock, struct esVB_K_BLOCK_INFO_S *blocks, unsigned long len)
{
	unsigned long size_remaining = len;
	unsigned int max_order = orders[0];
	struct sg_table *table;
	struct scatterlist *sg;
	struct list_head pages;
	struct page *page, *tmp_page;
	int i, ret = -ENOMEM;

	INIT_LIST_HEAD(&pages);
	i = 0;
	while (size_remaining > 0) {
		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by SIGKILL
		 */
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto free_buffer;
		}

		page = alloc_largest_available(memblock, size_remaining, max_order);
		if (!page)
			goto free_buffer;

		list_add_tail(&page->lru, &pages);
		size_remaining -= page_size(page);
		max_order = compound_order(page);
		i++;
		// pr_debug("page_size(page)=0x%lx, phys_addr=0x%llx, max_order=%d\n",
			// page_size(page), page_to_phys(page), max_order);

	}

	table = &blocks->sg_table;
	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_buffer;

	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	return 0;

free_buffer:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		es_free_pages(memblock, page);


	return ret;
}

static void vb_blk_pages_release(struct mem_block *memblock, struct esVB_K_BLOCK_INFO_S *blocks)
{
	struct sg_table *table;
	struct scatterlist *sg;
	int i;

	table = &blocks->sg_table;
	for_each_sgtable_sg(table, sg, i) {
		struct page *page = sg_page(sg);
		// pr_debug("%s:%d,page_size(page)=0x%lx, phys_addr=0x%llx\n",
			// __func__, __LINE__, page_size(page), page_to_phys(page));
		es_free_pages(memblock, page);
	}
	sg_free_table(table);

}

static int vb_blk_dmabuf_alloc(struct esVB_GET_BLOCK_CMD_S *getBlkCmd)
{
	struct mmz_vb_buffer *buffer;
	struct esVB_K_BLOCK_INFO_S *pBlk;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	size_t size;
	struct dma_buf *dmabuf;
	#ifdef MMZ_VB_DMABUF_MEMSET
	struct sg_table *table;
	struct scatterlist *sg;
	int i;
	#endif
	int fd;
	int ret = -ENOMEM;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);

	ret = vb_get_block(getBlkCmd, &pBlk);
	/* try to get a required block from pool */
	if (ret) {
		vb_err("failed to get block from pool!!!\n");
		goto free_buffer;
	}

	size = pBlk->pool->poolCfg.blkSize;
	buffer->len = size;
	buffer->table = &pBlk->sg_table;
	#ifdef MMZ_VB_DMABUF_MEMSET
	/*TODO: Clear the pages, sg_virt dose not work because vb memory is reserved as no-map!!!!*/
	#if 0
	{
		table = buffer->table;
		for_each_sg(table->sgl, sg, table->orig_nents, i)
			memset(sg_virt(sg), 0, sg->length);
	}
	#endif
	#endif
	buffer->pBlk = pBlk;

	/* create the dmabuf */
	exp_info.exp_name = MMZ_VB_DMABUF_NAME;
	exp_info.ops = &mmz_vb_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = O_RDWR | O_CLOEXEC;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto release_block;
	}

	fd = dma_buf_fd(dmabuf, MMZ_VB_VALID_FD_FLAGS);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
		return fd;
	}

	getBlkCmd->getBlkResp.fd = fd;
	getBlkCmd->getBlkResp.actualBlkSize = size;
	getBlkCmd->getBlkResp.nr = pBlk->nr;
	return 0;

release_block:
	vb_release_block(pBlk);
free_buffer:
	kfree(buffer);

	return ret;
}

static int vb_ioctl_get_blk(void __user *user_getBlkCmd)
{
	int ret = 0;
	struct esVB_GET_BLOCK_CMD_S *getBlkCmd;

	getBlkCmd = kzalloc(sizeof(*getBlkCmd), GFP_KERNEL);
	if (!getBlkCmd) {
		return -ENOMEM;
	}
	if (copy_from_user(getBlkCmd, user_getBlkCmd, sizeof(*getBlkCmd))) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = vb_blk_dmabuf_alloc(getBlkCmd);
	if (ret) {
		goto out_free;
	}

	if (copy_to_user(user_getBlkCmd, getBlkCmd, sizeof(*getBlkCmd)))
		ret = -EFAULT;

out_free:
	kfree(getBlkCmd);
	return ret;
}

static int vb_ioctl_pool_size(void __user *user_getPoolSizeCmd)
{
	int ret = 0;
	struct esVB_GET_POOLSIZE_CMD_S *getPoolSizeCmd;

	getPoolSizeCmd = kzalloc(sizeof(*getPoolSizeCmd), GFP_KERNEL);
	if (!getPoolSizeCmd) {
		return -ENOMEM;
	}
	if (copy_from_user(getPoolSizeCmd, user_getPoolSizeCmd, sizeof(*getPoolSizeCmd))) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = vb_pool_size(getPoolSizeCmd->poolId, &getPoolSizeCmd->poolSize);
	if (ret) {
		goto out_free;
	}

	if (copy_to_user(user_getPoolSizeCmd, getPoolSizeCmd, sizeof(*getPoolSizeCmd)))
		ret = -EFAULT;

out_free:
	kfree(getPoolSizeCmd);
	return ret;
}
#if 0
static int vb_ioctl_flush_pool(void __user *user_flushPoolCmd)
{
	int ret = 0;
	struct esVB_FLUSH_POOL_CMD_S *flushPoolCmd;

	flushPoolCmd = kzalloc(sizeof(*flushPoolCmd), GFP_KERNEL);
	if (!flushPoolCmd) {
		return -ENOMEM;
	}
	if (copy_from_user(flushPoolCmd, user_flushPoolCmd, sizeof(*flushPoolCmd))) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = vb_flush_pool(flushPoolCmd);
	if (ret) {
		goto out_free;
	}

	if (copy_to_user(user_flushPoolCmd, flushPoolCmd, sizeof(*flushPoolCmd)))
		ret = -EFAULT;

out_free:
	kfree(flushPoolCmd);
	return ret;
}
#endif

static int vb_ioctl_blk_to_pool(void __user *user_blkToPoolCmd)
{
	int ret = 0;
	struct esVB_BLOCK_TO_POOL_CMD_S *blkToPoolCmd;

	blkToPoolCmd = kzalloc(sizeof(*blkToPoolCmd), GFP_KERNEL);
	if (!blkToPoolCmd) {
		return -ENOMEM;
	}
	if (copy_from_user(blkToPoolCmd, user_blkToPoolCmd, sizeof(*blkToPoolCmd))) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = vb_blk_to_pool(blkToPoolCmd);
	if (ret)
		goto out_free;

	if (copy_to_user(user_blkToPoolCmd, blkToPoolCmd, sizeof(*blkToPoolCmd)))
		ret = -EFAULT;

out_free:
	kfree(blkToPoolCmd);
	return ret;
}

static int vb_ioctl_get_blk_offset(void __user *user_getBlkOffsetCmd)
{
	int ret = 0;
	struct esVB_GET_BLOCKOFFSET_CMD_S *getBlkOffsetCmd;

	getBlkOffsetCmd = kzalloc(sizeof(*getBlkOffsetCmd), GFP_KERNEL);
	if (!getBlkOffsetCmd) {
		return -ENOMEM;
	}
	if (copy_from_user(getBlkOffsetCmd, user_getBlkOffsetCmd, sizeof(*getBlkOffsetCmd))) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = vb_get_blk_offset(getBlkOffsetCmd);
	if (ret) {
		goto out_free;
	}

	if (copy_to_user(user_getBlkOffsetCmd, getBlkOffsetCmd, sizeof(*getBlkOffsetCmd)))
		ret = -EFAULT;

out_free:
	kfree(getBlkOffsetCmd);
	return ret;
}

static int vb_ioctl_split_dmabuf(void __user *user_splitDmabufCmd)
{
	int ret = 0;
	struct esVB_SPLIT_DMABUF_CMD_S *splitDmabufCmd;

	splitDmabufCmd = kzalloc(sizeof(*splitDmabufCmd), GFP_KERNEL);
	if (!splitDmabufCmd) {
		return -ENOMEM;
	}
	if (copy_from_user(splitDmabufCmd, user_splitDmabufCmd, sizeof(*splitDmabufCmd))) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = vb_split_dmabuf(splitDmabufCmd);
	if (ret) {
		goto out_free;
	}

	if (copy_to_user(user_splitDmabufCmd, splitDmabufCmd, sizeof(*splitDmabufCmd)))
		ret = -EFAULT;

out_free:
	kfree(splitDmabufCmd);
	return ret;
}

static int vb_ioctl_get_dmabuf_refcnt(void __user *user_getDmabufRefCntCmd)
{
	int ret = 0;
	struct esVB_DMABUF_REFCOUNT_CMD_S *getDmabufRefCntCmd;

	getDmabufRefCntCmd = kzalloc(sizeof(*getDmabufRefCntCmd), GFP_KERNEL);
	if (!getDmabufRefCntCmd) {
		return -ENOMEM;
	}
	if (copy_from_user(getDmabufRefCntCmd, user_getDmabufRefCntCmd, sizeof(*getDmabufRefCntCmd))) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = vb_get_dmabuf_refcnt(getDmabufRefCntCmd);
	if (ret)
		goto out_free;

	if (copy_to_user(user_getDmabufRefCntCmd, getDmabufRefCntCmd, sizeof(*getDmabufRefCntCmd)))
		ret = -EFAULT;

out_free:
	kfree(getDmabufRefCntCmd);
	return ret;
}

static int vb_ioctl_retrieve_mem_node(void __user *user_retrieveMemNodeCmd)
{
	int ret = 0;
	struct esVB_RETRIEVE_MEM_NODE_CMD_S *retrieveMemNodeCmd;

	retrieveMemNodeCmd = kzalloc(sizeof(*retrieveMemNodeCmd), GFP_KERNEL);
	if (!retrieveMemNodeCmd) {
		return -ENOMEM;
	}
	if (copy_from_user(retrieveMemNodeCmd, user_retrieveMemNodeCmd, sizeof(*retrieveMemNodeCmd))) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = vb_retrieve_mem_node(retrieveMemNodeCmd);
	if (ret)
		goto out_free;

	if (copy_to_user(user_retrieveMemNodeCmd, retrieveMemNodeCmd, sizeof(*retrieveMemNodeCmd)))
		ret = -EFAULT;

out_free:
	kfree(retrieveMemNodeCmd);
	return ret;
}

static int vb_ioctl_get_dmabuf_size(void __user *user_getDmabufSizeCmd)
{
	int ret = 0;
	struct esVB_DMABUF_SIZE_CMD_S *getDmabufSizeCmd;

	getDmabufSizeCmd = kzalloc(sizeof(*getDmabufSizeCmd), GFP_KERNEL);
	if (!getDmabufSizeCmd) {
		return -ENOMEM;
	}
	if (copy_from_user(getDmabufSizeCmd, user_getDmabufSizeCmd, sizeof(*getDmabufSizeCmd))) {
		ret = -EFAULT;
		goto out_free;
	}

	ret = vb_get_dmabuf_size(getDmabufSizeCmd);
	if (ret)
		goto out_free;

	if (copy_to_user(user_getDmabufSizeCmd, getDmabufSizeCmd, sizeof(*getDmabufSizeCmd)))
		ret = -EFAULT;

out_free:
	kfree(getDmabufSizeCmd);
	return ret;
}

static int mmz_vb_assign_pool_id(struct esVB_K_POOL_INFO_S *pool)
{
	int ret = 0;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;

	down_write(&partitions->idr_lock);
	ret = idr_alloc(&partitions->pool_idr, pool, 0, VB_K_POOL_MAX_ID,
			GFP_KERNEL);
	if (ret >= 0) {
		pool->poolId = ret;
	}
	up_write(&partitions->idr_lock);

	return ret < 0 ? ret : 0;
}

static int mmz_vb_remove_pool_id(struct esVB_K_POOL_INFO_S *pool, bool is_lock)
{
	int ret = 0;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;

	if (is_lock) {
		down_write(&partitions->idr_lock);
	}

	idr_remove(&partitions->pool_idr, pool->poolId);

	if (is_lock) {
		up_write(&partitions->idr_lock);
	}
	return ret < 0 ? ret : 0;
}

static int mmz_pool_insert_list(struct esVB_K_POOL_INFO_S *pool, enum esVB_UID_E uid)
{
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;

	if (uid <= VB_UID_PRIVATE || uid >= VB_UID_MAX) {
		dev_err(mmz_vb_dev, "%s %d, invalid uid %d\n",__func__,__LINE__, uid);
		return -EINVAL;
	}
	down_write(&vb_priv->pool_lock[uid]);
	hash_add(vb_priv->ht[uid], &pool->node, pool->poolCfg.blkSize);
	up_write(&vb_priv->pool_lock[uid]);
	return 0;
}

static struct mem_block *vb_get_memblock(const char *memBlkName)
{
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;
	struct mem_block *memblock = NULL, *rsvmem_block = NULL;
	int i;

	for (i = 0; i < partitions->partCnt; i++) {
		rsvmem_block = partitions->mem_blocks[i];
		if (!strcmp(memBlkName, rsvmem_block->name)){
			memblock = rsvmem_block;
			break;
		}
	}

	return memblock;
}

static int mmz_vb_do_create_pool(struct esVB_POOL_CONFIG_S *pool_cfg,
	struct esVB_K_POOL_INFO_S **pool_out)
{
	int i;
	int ret = 0;
	struct esVB_K_POOL_INFO_S *pool;
	struct esVB_K_BLOCK_INFO_S *blocks;
	struct mem_block *memblock = NULL;
	const char *memBlkName = pool_cfg->mmzName;
	size_t size;

	// 0.find the memblock
	memblock = vb_get_memblock(memBlkName);
	if (NULL == memblock) {
		vb_err("%s NOT found!\n", memBlkName);
		return -EINVAL;
	}

	// 1.init pool
	pool = devm_kzalloc(mmz_vb_dev, sizeof(struct esVB_K_POOL_INFO_S), GFP_KERNEL);
	if (!pool) {
		dev_err(mmz_vb_dev, "%s %d, faild to alloc pool cb\n",__func__,__LINE__);
		ret = -ENOMEM;
		goto out;
	}

	pool->blocks = vmalloc(sizeof(struct esVB_K_BLOCK_INFO_S) * pool_cfg->blkCnt);
	if (!pool->blocks) {
		dev_err(mmz_vb_dev, "%s %d, faild to alloc blocks cb\n",__func__,__LINE__);
		ret = -ENOMEM;
		goto out_free_pool;
	}

	spin_lock_init(&pool->lock);
	memcpy(&pool->poolCfg, pool_cfg, sizeof(struct esVB_POOL_CONFIG_S));

	pool->bitmap = bitmap_zalloc(pool_cfg->blkCnt, GFP_KERNEL);
	if (!pool->bitmap) {
		dev_err(mmz_vb_dev, "%s %d, faild to alloc bitmap\n",__func__,__LINE__);
		ret = -ENOMEM;
		goto out_free_block_arrays;
	}

	// 2. make blkSize align
	size = PAGE_ALIGN(pool_cfg->blkSize);
	/* If len >= 1MB, align len with 2M to improve performance of SMMU */
	if (size/(PAGE_SIZE << 8)) {
		size = ALIGN(size, (PAGE_SIZE << 9));
	}
	pool_cfg->blkSize = size;
	pool->poolCfg.blkSize = pool_cfg->blkSize;
	dev_dbg(mmz_vb_dev, "blkSize(0x%llx) from pool creation is "
		"aligned to 0x%lx to improve performance.\n",
		pool_cfg->blkSize, size);

	// 3. alloc pages for blocks
	for (i = 0; i < pool_cfg->blkCnt; i++) {
		blocks = &pool->blocks[i];
		blocks->nr = i;
		blocks->pool = pool;
		ret = vb_blk_pages_allocate(memblock, blocks, pool_cfg->blkSize);
		if (ret) {
			while (--i >= 0) {
				vb_blk_pages_release(memblock, &pool->blocks[i]);
			}
			dev_err(mmz_vb_dev, "%s %d, faild to alloc block page!\n", __func__,__LINE__);
			ret = -ENOMEM;
			goto out_free_bitmap;
		}
	}
	// 4. everthing is ok, add pool to idr
	ret = mmz_vb_assign_pool_id(pool);
	if (0 != ret) {
		dev_err(mmz_vb_dev, "%s %d, faild to assign pool id\n",__func__,__LINE__);
		ret = -EINVAL;
		goto out_free_block_pages;
	}
	*pool_out = pool;
	return ret;

out_free_block_pages:
	for (i = 0; i < pool_cfg->blkCnt; i++) {
		vb_blk_pages_release(memblock, &pool->blocks[i]);
	}
out_free_bitmap:
	bitmap_free(pool->bitmap);;
out_free_block_arrays:
	vfree(pool->blocks);
out_free_pool:
	devm_kfree(mmz_vb_dev, pool);
out:
	return ret;
}

static int vb_pool_config_check(struct esVB_POOL_CONFIG_S *pool_cfg)
{
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct mem_block *memblock = NULL;
	const char *memBlkName = pool_cfg->mmzName;
	unsigned long numFreePages = 0;
	u64 req_size;

	if (NULL == vb_priv) {
		return 0;
	}

	// find the memblock
	memblock = vb_get_memblock(memBlkName);
	if (NULL == memblock) {
		vb_err("%s NOT found!\n", memBlkName);
		return -EINVAL;
	}

	req_size = pool_cfg->blkCnt * PAGE_ALIGN(pool_cfg->blkSize);
	numFreePages = es_num_free_pages(memblock);
	if (numFreePages < (req_size >> PAGE_SHIFT)) {
		dev_err(mmz_vb_dev, "%s %d, (%s)out of memory, request pool size %llu "
			"free %ld!\n",
			__func__,__LINE__,
			memBlkName, req_size, (numFreePages << PAGE_SHIFT));
		return -ENOMEM;
	}

	return 0;
}

static int vb_ioctl_create_pool(void __user *user_cmd)
{
	int ret = 0;
	struct esVB_CREATE_POOL_CMD_S cmd;
	struct esVB_CREATE_POOL_REQ_S *req;
	struct esVB_CREATE_POOL_RESP_S *rsp;
	struct esVB_POOL_CONFIG_S *pool_cfg;
	struct esVB_K_POOL_INFO_S *pool = NULL;

	if (copy_from_user(&cmd, user_cmd, sizeof(cmd))) {
		ret = -EFAULT;
		goto out_free;
	}
	req = &cmd.PoolReq;
	pool_cfg = &req->req;
	ret = vb_pool_config_check(pool_cfg);
	if (ret) {
		goto out_free;
	}
	ret = mmz_vb_do_create_pool(pool_cfg, &pool);
	if (ret) {
		goto out_free;
	}
	pool->enVbUid = VB_UID_PRIVATE;
	rsp = &cmd.PoolResp;
	rsp->PoolId = pool->poolId;
	dev_dbg(mmz_vb_dev, "[%s %d]:create pool, PoolId %d!\n",__func__,__LINE__, rsp->PoolId);
	if (copy_to_user(user_cmd, &cmd, sizeof(cmd)))
		ret = -EFAULT;
	else
		ret = 0;

out_free:
	return ret;
}

/**
 * mmz_vb_do_destory_pool - do the pool destory operation
 * @pool:	The pool
 * @is_lock:	when set true, will lock the idr when remove the idr id.
 * @is_force:	when set true, will still destory the bool even the bitmap is not empty.
 */
static int mmz_vb_do_destory_pool(struct esVB_K_POOL_INFO_S *pool, bool is_lock, bool is_force)
{
	struct esVB_POOL_CONFIG_S *poolCfg = &pool->poolCfg;
	const char *memBlkName = poolCfg->mmzName;
	struct mem_block *memblock = NULL;
	struct esVB_K_BLOCK_INFO_S *blocks = NULL;
	int ret = 0;
	int i;

	// find the memblock
	memblock = vb_get_memblock(memBlkName);
	if (NULL == memblock) {
		vb_err("%s NOT found!\n", memBlkName);
		return -EINVAL;
	}

	if (!bitmap_empty(pool->bitmap, pool->poolCfg.blkCnt)) {
		if (true == is_force) {
			dev_info(mmz_vb_dev, "%s %d, non-empty pool, still destory it!\n",__func__,__LINE__);
		} else {
			dev_dbg(mmz_vb_dev, "%s %d, non-empty pool, can not destory!\n",__func__,__LINE__);
			ret = -ENOTEMPTY;
			goto out;
		}
	}


	blocks = pool->blocks;
	for (i = 0; i < poolCfg->blkCnt; i++) {
		vb_blk_pages_release(memblock, &blocks[i]);
	}
	mmz_vb_remove_pool_id(pool, is_lock);
	if (pool->enVbUid >= VB_UID_COMMON && pool->enVbUid < VB_UID_MAX) {
		hash_del(&pool->node);
	}
	bitmap_free(pool->bitmap);
	vfree(pool->blocks);
out:
	if (0 == ret) {
		devm_kfree(mmz_vb_dev, pool);
	}
	return ret;
}

static int vb_ioctl_destory_pool(void __user *user_cmd)
{
	int ret = 0;
	struct esVB_K_POOL_INFO_S *pool = NULL;
	struct esVB_DESTORY_POOL_CMD_S cmd;
	struct esVB_DESTORY_POOL_REQ_S *req = NULL;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;

	if (copy_from_user(&cmd, user_cmd, sizeof(cmd))) {
		return -EFAULT;
	}
	req = &cmd.req;
	dev_dbg(mmz_vb_dev, "[%s %d]:destory pool, PoolId %d!\n",__func__,__LINE__, req->PoolId);
	down_write(&partitions->idr_lock);
	ret = vb_find_pool_by_id_unlock(req->PoolId, &pool);
	if (ret) {
		up_write(&partitions->idr_lock);
		dev_err(mmz_vb_dev, "%s %d, faild to find pool, PoolId %d\n",
			__func__,__LINE__, req->PoolId);
		return ret;
	}
	ret = mmz_vb_do_destory_pool(pool, false, false);
	if (-ENOTEMPTY == ret) {
		set_bit(MMZ_VB_POOL_FLAG_DESTORY, &pool->flag);
		up_write(&partitions->idr_lock);
		dev_dbg(mmz_vb_dev, "%s %d, pool %d not empty, waiting to destory\n",
			__func__,__LINE__, req->PoolId);
		return 0;
	} else if (ret) {
		up_write(&partitions->idr_lock);
		dev_err(mmz_vb_dev, "%s %d, faild to destory pool, PoolId %d\n",
			__func__,__LINE__, req->PoolId);
		return ret;
	}
	up_write(&partitions->idr_lock);
	return 0;
}

/*check whether the VbConfig is legal*/
static int vb_config_check(struct esVB_CONFIG_S *pVbConfig)
{
	int i;
	struct esVB_POOL_CONFIG_S *pool_cfg = NULL;
	int ret;

	if (pVbConfig->poolCnt > ES_VB_MAX_MOD_POOL) {
		dev_err(mmz_vb_dev, "%s %d, poolCnt %d exceed the limit %d!\n",
			__func__,__LINE__, pVbConfig->poolCnt, ES_VB_MAX_MOD_POOL);
		return -EINVAL;
	}
	for (i = 0; i < pVbConfig->poolCnt; i++) {
		pool_cfg = &pVbConfig->poolCfgs[i];
		ret = vb_pool_config_check(pool_cfg);
		if (0 != ret) {
			return ret;
		}
	}
	return 0;
}

static int vb_ioctl_set_config(void __user *user_cmd)
{
	struct esVB_SET_CFG_CMD_S *cmd;
	struct esVB_SET_CFG_REQ_S *req;
	enum esVB_UID_E enVbUid;
	struct esVB_CONFIG_S *pVbConfig = NULL;
	struct esVB_CONFIG_S *vb_cfg = NULL;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	int ret = 0;

	cmd = devm_kzalloc(mmz_vb_dev, sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		dev_err(mmz_vb_dev, "%s %d, uid %d, failed to alloc memory!\n",
			__func__,__LINE__, enVbUid);
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(cmd, user_cmd, sizeof(*cmd))) {
		ret = -EFAULT;
		goto out_free_cmd;
	}
	req = &cmd->CfgReq;
	enVbUid = req->uid;
	pVbConfig = &req->cfg;
	if (enVbUid <= VB_UID_PRIVATE || enVbUid >= VB_UID_MAX) {
		dev_err(mmz_vb_dev, "%s %d, invaild uid %d!\n", __func__,__LINE__, enVbUid);
		ret = -EFAULT;
		goto out_free_cmd;
	}
	ret = vb_config_check(pVbConfig);
	if (ret) {
		dev_err(mmz_vb_dev, "%s %d, uid %d, vbConfig check fail!\n",
			__func__,__LINE__, enVbUid);
		goto out_free_cmd;
	}
	mutex_lock(&vb_priv->cfg_lock[enVbUid]);
	if (NULL != vb_priv->pVbConfig[enVbUid]) {
		if (test_bit(MMZ_VB_CFG_FLAG_INIT, &vb_priv->cfg_flag[enVbUid])) {
			dev_err(mmz_vb_dev, "%s %d, uid %d cfg already exist and init!\n",
				__func__,__LINE__, enVbUid);
			ret = -EFAULT;
			goto out_unlock;
		} else {
			/*release the old config*/
			devm_kfree(mmz_vb_dev, vb_priv->pVbConfig[enVbUid]);
			vb_priv->pVbConfig[enVbUid] = NULL;
		}
	}
	vb_cfg = devm_kzalloc(mmz_vb_dev, sizeof(struct esVB_CONFIG_S), GFP_KERNEL);
	if (!vb_cfg) {
		dev_err(mmz_vb_dev, "%s %d, uid %d, failed to alloc memory!\n",
			__func__,__LINE__, enVbUid);
		ret =  -ENOMEM;
		goto out_unlock;
	}
	memcpy(vb_cfg, pVbConfig, sizeof(struct esVB_CONFIG_S));
	vb_priv->pVbConfig[enVbUid] = vb_cfg;
out_unlock:
	mutex_unlock(&vb_priv->cfg_lock[enVbUid]);
out_free_cmd:
	devm_kfree(mmz_vb_dev, cmd);
out:
	return ret;
}

static int vb_ioctl_get_config(void __user *user_cmd)
{
	struct esVB_GET_CFG_CMD_S *cmd;
	struct esVB_GET_CFG_REQ_S *req;
	struct esVB_GET_CFG_RSP_S *rsp;
	enum esVB_UID_E enVbUid;
	struct esVB_CONFIG_S *vb_cfg = NULL;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	int ret = 0;

	cmd = devm_kzalloc(mmz_vb_dev, sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		dev_err(mmz_vb_dev, "%s %d, uid %d, failed to alloc memory!\n",
			__func__,__LINE__, enVbUid);
		ret = -ENOMEM;
		goto out;
	}

	if (copy_from_user(cmd, user_cmd, sizeof(*cmd))) {
		ret = -EFAULT;
		goto out_free_cmd;
	}
	req = &cmd->req;
	enVbUid = req->uid;
	if (enVbUid <= VB_UID_PRIVATE || enVbUid >= VB_UID_MAX) {
		dev_err(mmz_vb_dev, "%s %d, invaild uid %d!\n",__func__,__LINE__, enVbUid);
		ret = -EFAULT;
		goto out_free_cmd;
	}
	mutex_lock(&vb_priv->cfg_lock[enVbUid]);
	vb_cfg = vb_priv->pVbConfig[enVbUid];
	if (NULL == vb_cfg) {
		dev_dbg(mmz_vb_dev, "%s %d, uid %d cfg not exist!\n", __func__,__LINE__, enVbUid);
		ret = -EFAULT;
		goto out_unlock;
	}
	rsp = &cmd->rsp;
	memcpy(&rsp->cfg, vb_cfg, sizeof(struct esVB_CONFIG_S));
	if (copy_to_user(user_cmd, cmd, sizeof(*cmd))) {
		ret = -EFAULT;
		goto out_unlock;
	}
out_unlock:
	mutex_unlock(&vb_priv->cfg_lock[enVbUid]);
out_free_cmd:
	devm_kfree(mmz_vb_dev, cmd);
out:
	return ret;
}

static int vb_ioctl_init_config(void __user *user_cmd)
{
	VB_INIT_CFG_CMD_S cmd;
	VB_INIT_CFG_REQ_S *req = NULL;
	enum esVB_UID_E enVbUid;
	struct esVB_CONFIG_S *vb_cfg = NULL;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	int i;
	int ret = 0;
	struct esVB_POOL_CONFIG_S *pool_cfg;
	struct esVB_K_POOL_INFO_S *pool[ES_VB_MAX_MOD_POOL] = {NULL};

	if (copy_from_user(&cmd, user_cmd, sizeof(cmd))) {
		ret = -EFAULT;
		goto out;
	}
	req = &cmd.req;
	enVbUid = req->uid;

	if (enVbUid < VB_UID_COMMON || enVbUid >= VB_UID_MAX) {
		dev_err(mmz_vb_dev, "%s %d, invaild uid %d!\n",__func__,__LINE__, enVbUid);
		ret = -EFAULT;
		goto out;
	}
	mutex_lock(&vb_priv->cfg_lock[enVbUid]);
	vb_cfg = vb_priv->pVbConfig[enVbUid];
	if (NULL == vb_cfg) {
		dev_err(mmz_vb_dev, "%s %d, uid %d cfg not exist!\n", __func__,__LINE__, enVbUid);
		ret = -EFAULT;
		goto out_unlock;
	}
	if (test_bit(MMZ_VB_CFG_FLAG_INIT, &vb_priv->cfg_flag[enVbUid])) {
		dev_err(mmz_vb_dev, "%s %d, uid %d cfg already initialized!\n", __func__,__LINE__, enVbUid);
		ret = -EPERM;
		goto out_unlock;
	}

	for (i = 0; i < vb_cfg->poolCnt; i++) {
		pool_cfg = &vb_cfg->poolCfgs[i];
		ret = mmz_vb_do_create_pool(pool_cfg, &pool[i]);
		if (0 != ret) {
			while(--i >= 0) {
				ret = mmz_vb_do_destory_pool(pool[i], true, false);
				if (ret) {
					dev_err(mmz_vb_dev, "%s %d, faild to destory pool!\n",
						__func__,__LINE__);
				}
			}
			dev_err(mmz_vb_dev, "%s %d, faild to create pool!\n",__func__, __LINE__);
			goto out_unlock;
		}
		mmz_pool_insert_list(pool[i], enVbUid);
		pool[i]->enVbUid = enVbUid;
	}
	set_bit(MMZ_VB_CFG_FLAG_INIT, &vb_priv->cfg_flag[enVbUid]);
out_unlock:
	mutex_unlock(&vb_priv->cfg_lock[enVbUid]);
out:
	return ret;
}

static int vb_ioctl_uninit_config(void __user *user_cmd)
{
	struct esVB_UNINIT_CFG_CMD_S cmd;
	enum esVB_UID_E enVbUid;
	int ret;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_POOL_INFO_S *pool = NULL;
	unsigned long bkt = 0;
	struct hlist_node *tmp_node = NULL;

	if (copy_from_user(&cmd, user_cmd, sizeof(cmd))) {
		return -EFAULT;
	}
	enVbUid = cmd.req.uid;

	if (enVbUid <= VB_UID_PRIVATE || enVbUid >= VB_UID_MAX) {
		dev_err(mmz_vb_dev, "%s %d, invaild uid %d!\n",__func__,__LINE__, enVbUid);
		return -EFAULT;
	}
	mutex_lock(&vb_priv->cfg_lock[enVbUid]);
	if (!test_bit(MMZ_VB_CFG_FLAG_INIT, &vb_priv->cfg_flag[enVbUid])) {
		dev_err(mmz_vb_dev, "%s %d, uid %d cfg not initialized!\n", __func__,__LINE__, enVbUid);
		mutex_unlock(&vb_priv->cfg_lock[enVbUid]);
		return -EINVAL;
	}
	mutex_unlock(&vb_priv->cfg_lock[enVbUid]);

	down_write(&vb_priv->pool_lock[enVbUid]);
	hash_for_each_safe(vb_priv->ht[enVbUid], bkt, tmp_node, pool, node) {
		ret = mmz_vb_do_destory_pool(pool, true, false);
		if (ret) {
			dev_err(mmz_vb_dev, "%s %d, faild to destory pool, PoolId %d, enVbUid %d\n",
				__func__,__LINE__, pool->poolId, enVbUid);
			up_write(&vb_priv->pool_lock[enVbUid]);
			return ret;
		}
	}
	up_write(&vb_priv->pool_lock[enVbUid]);

	mutex_lock(&vb_priv->cfg_lock[enVbUid]);
	devm_kfree(mmz_vb_dev, vb_priv->pVbConfig[enVbUid]);
	vb_priv->pVbConfig[enVbUid] = NULL;
	clear_bit(MMZ_VB_CFG_FLAG_INIT, &vb_priv->cfg_flag[enVbUid]);
	mutex_unlock(&vb_priv->cfg_lock[enVbUid]);
	return 0;
}

static long mmz_vb_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp;

	argp = (void __user *)arg;
	switch (cmd) {
	case MMZ_VB_IOCTL_GET_BLOCK:
		return vb_ioctl_get_blk(argp);
	case MMZ_VB_IOCTL_CREATE_POOL:
		return vb_ioctl_create_pool(argp);
	case MMZ_VB_IOCTL_DESTORY_POOL:
		return vb_ioctl_destory_pool(argp);
	case MMZ_VB_IOCTL_SET_CFG:
		return vb_ioctl_set_config(argp);
	case MMZ_VB_IOCTL_GET_CFG:
		return vb_ioctl_get_config(argp);
	case MMZ_VB_IOCTL_INIT_CFG:
		return vb_ioctl_init_config(argp);
	case MMZ_VB_IOCTL_UNINIT_CFG:
		return vb_ioctl_uninit_config(argp);
	case MMZ_VB_IOCTL_POOL_SIZE:
		return vb_ioctl_pool_size(argp);
#if 0
	case MMZ_VB_IOCTL_FLUSH_POOL:
		return vb_ioctl_flush_pool(argp);
#endif
	case MMZ_VB_IOCTL_BLOCK_TO_POOL:
		return vb_ioctl_blk_to_pool(argp);
	case MMZ_VB_IOCTL_GET_BLOCK_OFFSET:
		return vb_ioctl_get_blk_offset(argp);
	case MMZ_VB_IOCTL_SPLIT_DMABUF:
		return vb_ioctl_split_dmabuf(argp);
	case MMZ_VB_IOCTL_DMABUF_REFCOUNT:
		return vb_ioctl_get_dmabuf_refcnt(argp);
	case MMZ_VB_IOCTL_RETRIEVE_MEM_NODE:
		return vb_ioctl_retrieve_mem_node(argp);
	case MMZ_VB_IOCTL_DMABUF_SIZE:
		return vb_ioctl_get_dmabuf_size(argp);
	default:
		pr_debug("Invalid IOCTL CMD!!!\n");
		return -EINVAL;
	}
	pr_debug("%s:%d, success!\n", __func__, __LINE__);
	return ret;
}

static int mmz_vb_open(struct inode *inode, struct file *file)
{

	pr_debug("%s:%d, success!\n", __func__, __LINE__);

	return 0;
}

static int mmz_vb_release(struct inode *inode, struct file *file)
{
	pr_debug("%s:%d, success!\n", __func__, __LINE__);

	return 0;
}

/* mem = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, addr_pha);
*  vma->vm_pgoff indicats the pool ID
*/
static int mmz_vb_mmap_pool(struct file *file, struct vm_area_struct *vma)
{
	int ret = 0;
	size_t size = vma->vm_end - vma->vm_start;
	VB_POOL poolId = (VB_POOL)vma->vm_pgoff;
	unsigned long addr = vma->vm_start;
	struct esVB_K_POOL_INFO_S *pPool = NULL;
	struct esVB_K_BLOCK_INFO_S *pBlk = NULL;
	u64 poolSize, blkSize;
	u32 i;

	ret = vb_find_pool_by_id(poolId, &pPool);
	if (ret)
		return ret;

	poolSize = do_vb_pool_size(pPool);
	/* is the mmap size equal to poolSize? */
	if (size != poolSize)
		return -EINVAL;

	/* pool is mmapped as uncached memory */
	#ifndef QEMU_DEBUG
	vma->vm_page_prot = pgprot_dmacoherent(vma->vm_page_prot);
	#endif

	blkSize = pPool->poolCfg.blkSize;
	pBlk = pPool->blocks;
	for (i = 0; i < pPool->poolCfg.blkCnt; i++) {
		struct sg_table *table = &pBlk->sg_table;

		/* mmap for one block */
		struct scatterlist *sg;
		int j;

		for_each_sg(table->sgl, sg, table->orig_nents, j) {
			struct page *page = sg_page(sg);
			ret = remap_pfn_range(vma, addr, page_to_pfn(page), sg->length,
					vma->vm_page_prot);
			if (ret)
				return ret;
			addr += sg->length;
			if (addr >= vma->vm_end)
				return 0;
		}
		pBlk++;
	}

	pr_debug("%s:%d, success!\n", __func__, __LINE__);

	return ret;
}

static struct file_operations mmz_vb_fops = {
	.owner        = THIS_MODULE,
	.llseek        = no_llseek,
	.unlocked_ioctl = mmz_vb_unlocked_ioctl,
	.open        = mmz_vb_open,
	.release    = mmz_vb_release,
	.mmap	= mmz_vb_mmap_pool,
};

static struct miscdevice mmz_vb_miscdev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DRIVER_NAME,
	.fops	= &mmz_vb_fops,
};

static char es_mmz_name_prefix[] = "mmz_nid_";
static int mmz_vb_init_partitions(void)
{
	int ret = 0;
	struct mmz_vb_priv *mmz_vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions;

	if (NULL == mmz_vb_priv)
		return -EFAULT;

	partitions = &mmz_vb_priv->partitions;;
	init_rwsem(&partitions->idr_lock);
	idr_init(&partitions->pool_idr);

	partitions->partCnt = mmz_vb_init_memory_region();
	if (partitions->partCnt == 0) {
		vb_err("No VB memory block was found or correctly initialized!\n");
		ret = -EFAULT;
	}

	return ret;
}

static int mmz_vb_idr_iterate_show(int id, void *p, void *data)
{
	struct esVB_K_POOL_INFO_S *pool = (struct esVB_K_POOL_INFO_S *)p;
	struct esVB_POOL_CONFIG_S *pool_cfg;
	es_proc_entry_t *s = (es_proc_entry_t *)data;

	spin_lock(&pool->lock);
	pool_cfg = &pool->poolCfg;
	es_seq_printf(s, "\t Uid %d, PoolId %d, blkSize 0x%llx, blkCnt %d, "
		"RemapMode %d, mmzName %s, allocated blkCnt %d\n\r", pool->enVbUid,
		pool->poolId, pool_cfg->blkSize, pool_cfg->blkCnt,
		pool_cfg->enRemapMode, pool_cfg->mmzName,
		pool_cfg->blkCnt - vb_pool_get_free_block_cnt_unlock(pool));
	spin_unlock(&pool->lock);
	return 0;
}

static int mmz_vb_proc_show(es_proc_entry_t *s)
{
	int i;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;
	unsigned long numFreePages = 0;
	struct mem_block *memblock = NULL, *rsvmem_block = NULL;
	int ret;

	es_seq_printf(s, "\nModule: [VB], Build Time[xx]\n");
	/*
	   use es_seq_printf to show more debug info
	*/
	es_seq_printf(s, "-----MMZ REGION CONFIG-----\n\r");
	for (i = 0; i < partitions->partCnt; i++) {
		rsvmem_block = partitions->mem_blocks[i];
		memblock = vb_get_memblock(rsvmem_block->name);
		if (NULL == memblock) {
			vb_err("%s NOT found!\n", rsvmem_block->name);
			return -EINVAL;
		}
		numFreePages = es_num_free_pages(memblock);
		es_seq_printf(s, "\tmemblock: %s, total size(0x%lx), free mem size(0x%lx)\n\r",
				rsvmem_block->name,
				memblock->page_num << PAGE_SHIFT,
				numFreePages << PAGE_SHIFT);
	}
	es_seq_printf(s, "-----POOL CONFIG-----\n\r");
	ret = idr_for_each(&partitions->pool_idr, mmz_vb_idr_iterate_show, s);
	if (ret) {
		dev_err(mmz_vb_dev, "%s %d, failed to iterate vb pool ret %d\n",
			__func__,__LINE__, ret);
		return ret;
	}
	return 0;
}

int mmz_vb_proc_store(struct es_proc_dir_entry *entry, const char *buf,
		int count, long long *ppos)
{
	int ret;

	ret = mmz_vb_pool_exit();
	if (0 != ret) {
		dev_err(mmz_vb_dev, "%s %d, failed to release vb pool "
			"when exit, ret %d\n", __func__,__LINE__, ret);
	}
	return count;
}

void mmz_vb_vb_dbg_init(void)
{
	es_proc_entry_t *proc = NULL;

	proc = es_create_proc_entry(PROC_ENTRY_VB, NULL);

	if (proc == NULL) {
		vb_err("Kernel: Register vb proc failed!\n");
		return;
	}
	proc->read = mmz_vb_proc_show;
	/*NULL means use the default routine*/
	proc->write = mmz_vb_proc_store;
	proc->open = NULL;
}

static int __init mmz_vb_init(void)
{
	int i;
	int ret = 0;
	struct device *dev;

	g_mmz_vb_priv = kzalloc(sizeof(struct mmz_vb_priv), GFP_KERNEL);
	if (!g_mmz_vb_priv) {
		vb_err("Failed to alloc priv data for mmz_vb driver!!!\n");
		return -ENOMEM;
	}
	ret = misc_register(&mmz_vb_miscdev);
	if(ret) {
		vb_err ("cannot register miscdev (err=%d)\n", ret);
		goto free_vb_priv;
	}
	mmz_vb_dev = mmz_vb_miscdev.this_device;
	g_mmz_vb_priv->dev = mmz_vb_dev;
	for (i = 0; i < VB_UID_MAX; i++) {
		hash_init(g_mmz_vb_priv->ht[i]);
		mutex_init(&g_mmz_vb_priv->cfg_lock[i]);
		init_rwsem(&g_mmz_vb_priv->pool_lock[i]);
	}

	ret = mmz_vb_init_partitions();
	if (ret) {
		goto deregister_vb;
	}
	atomic_set(&g_mmz_vb_priv->allocBlkcnt, 0);

	mmz_vb_vb_dbg_init();

	dev = mmz_vb_dev;
	if (!dev->dma_mask) {
		dev->dma_mask = &dev->coherent_dma_mask;
	}
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (ret)
		vb_err("Unable to set coherent mask\n");
	return 0;

deregister_vb:
	misc_deregister(&mmz_vb_miscdev);
free_vb_priv:
	kfree(g_mmz_vb_priv);
	return ret;
}

static int mmz_vb_pool_exit(void)
{
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;
	struct esVB_K_POOL_INFO_S *pool = NULL;
	int ret = 0;
	u32 id = 0;
	int i;

	down_write(&partitions->idr_lock);
	idr_for_each_entry(&partitions->pool_idr, pool, id) {
		ret = mmz_vb_do_destory_pool(pool, false, true);
		if (ret) {
			dev_err(mmz_vb_dev, "%s %d, failed to destory vb pool, ret %d\n",
				__func__,__LINE__, ret);
			continue;
		}
	}

	up_write(&partitions->idr_lock);

	atomic_set(&vb_priv->allocBlkcnt, 0);
	for (i = 0; i < VB_UID_MAX; i++) {
		if (NULL != vb_priv->pVbConfig[i]) {
			devm_kfree(mmz_vb_dev, vb_priv->pVbConfig[i]);
			vb_priv->pVbConfig[i] = NULL;
		}
	}
	memset(vb_priv->cfg_flag, 0, sizeof(unsigned long) * VB_UID_MAX);
	return 0;
}

static void __exit mmz_vb_exit(void)
{
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	int ret = 0;

	ret = mmz_vb_pool_exit();
	if (0 != ret) {
		dev_err(mmz_vb_dev, "%s %d, failed to release vb pool "
			"when exit, ret %d\n", __func__,__LINE__, ret);
	}
	es_remove_proc_entry(PROC_ENTRY_VB, NULL);
	misc_deregister(&mmz_vb_miscdev);
	kfree(vb_priv);
}

module_init(mmz_vb_init);
module_exit(mmz_vb_exit);

MODULE_DESCRIPTION("MMZ VB Driver");
MODULE_AUTHOR("Lin MIn <linmin@eswincomputing.com>");
MODULE_LICENSE("GPL v2");


static int vb_find_pool_by_id_unlock(VB_POOL poolId, struct esVB_K_POOL_INFO_S **ppPool)
{
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;
	struct esVB_K_POOL_INFO_S *pool = NULL;

	pool = idr_find(&partitions->pool_idr, poolId);
	if (!pool) {
		dev_err(mmz_vb_dev, "%s %d, faild to find pool by id %d\n",
			__func__,__LINE__, poolId);
		return -EINVAL;
	}
	*ppPool = pool;
	return 0;
}

static int vb_find_pool_by_id(VB_POOL poolId, struct esVB_K_POOL_INFO_S **ppPool)
{
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;
	int ret;

	down_read(&partitions->idr_lock);
	ret = vb_find_pool_by_id_unlock(poolId, ppPool);
	up_read(&partitions->idr_lock);
	return ret;
}

static int vb_pool_size(VB_POOL poolId, u64 *pPoolSize)
{
	int ret = 0;
	struct esVB_K_POOL_INFO_S *pPool;

	ret = vb_find_pool_by_id(poolId, &pPool);
	if (ret) {
		vb_info("failed to find pool %d\n", poolId);
		return ret;
	}

	*pPoolSize = do_vb_pool_size(pPool);

	return ret;
}
#if 0
static int vb_flush_pool(struct esVB_FLUSH_POOL_CMD_S *flushPoolCmd)
{
	int ret = 0;
	struct esVB_K_POOL_INFO_S *pPool = NULL;
	struct esVB_K_BLOCK_INFO_S *pBlk = NULL;
	u64 blkSize, poolSize = 0;
	u64 offset_inPool = 0, offset_inBlk = 0, size, left_size = 0;
	u64 phy_addr;
	u32 i;

	ret = vb_find_pool_by_id(flushPoolCmd->poolId, &pPool);
	if (ret) {
		vb_info("%s,failed to find pool %d\n", __func__, flushPoolCmd->poolId);
		return ret;
	}

	poolSize = do_vb_pool_size(pPool);
	if ((flushPoolCmd->offset + flushPoolCmd->size - 1) >= poolSize)
		return -EINVAL;

	// find the block according to the offset
	blkSize = pPool->poolCfg.blkSize;
	pBlk = pPool->blocks;
	left_size = flushPoolCmd->size;
	for (i = 0; i < pPool->poolCfg.blkCnt; i++) {
		if ((offset_inPool + blkSize -1) >= flushPoolCmd->offset)
			break;
		offset_inPool += blkSize;
		pBlk++;
	}
	offset_inBlk = flushPoolCmd->offset - offset_inPool;
	for (; i < pPool->poolCfg.blkCnt; i++) {
		struct page *page = pBlk->cma_pages;
		size = min(left_size, (blkSize - offset_inBlk));
		phy_addr = page_to_phys(page) + offset_inBlk;
		arch_sync_dma_for_device(phy_addr, size, DMA_TO_DEVICE);
		left_size -= size;
		if (left_size == 0)
			break;
		pBlk++;
		offset_inBlk = 0;
	}

	return ret;
}
#endif
static int vb_pool_get_free_block_cnt_unlock(struct esVB_K_POOL_INFO_S *pool)
{
	int count = 0;
	int start = 0;
	struct esVB_POOL_CONFIG_S *pool_cfg = &pool->poolCfg;
	int nr;

	while (true) {
		nr = find_next_zero_bit(pool->bitmap, pool_cfg->blkCnt, start);
		if (likely(nr < pool_cfg->blkCnt)) {
			count++;
			start = nr + 1;
		} else {
			break;
		}
	}
	return count;
}

static int vb_pool_get_block(struct esVB_K_POOL_INFO_S *pool,
	struct esVB_K_BLOCK_INFO_S **ppBlk)
{
	unsigned int nr = -1U;
	struct esVB_POOL_CONFIG_S *pool_cfg;
	int ret = -EINVAL;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;

	spin_lock(&pool->lock);
	pool_cfg = &pool->poolCfg;
	nr = find_next_zero_bit(pool->bitmap, pool_cfg->blkCnt, 0);
	if (likely(nr < pool_cfg->blkCnt)) {
		ret = 0;
		*ppBlk = &pool->blocks[nr];
		bitmap_set(pool->bitmap, nr, 1);
		if (atomic_inc_return(&vb_priv->allocBlkcnt) == 1) {
			__module_get(THIS_MODULE);
		}
	} else {
		dev_warn(mmz_vb_dev, "%s %d, pool %d used up, blkSize 0x%llx,"
			"blkCnt 0x%x\n",__func__,__LINE__, pool->poolId,
			pool_cfg->blkSize, pool_cfg->blkCnt);
	}
	spin_unlock(&pool->lock);
	return ret;
}

static int vb_get_block(struct esVB_GET_BLOCK_CMD_S *getBlkCmd,
	struct esVB_K_BLOCK_INFO_S **ppBlk)
{
	int ret = -EINVAL;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;
	struct esVB_GET_BLOCK_REQ_S *req = &getBlkCmd->getBlkReq;
	struct esVB_K_POOL_INFO_S *pool = NULL, *pool_tmp = NULL;
	struct esVB_POOL_CONFIG_S *pool_cfg;
	unsigned long bkt = 0;

	if (VB_UID_PRIVATE == req->uid) {
		down_read(&partitions->idr_lock);
		ret = vb_find_pool_by_id_unlock(req->poolId, &pool);
		if (ret) {
			up_read(&partitions->idr_lock);
			dev_err(mmz_vb_dev, "%s %d, failed to find pool by id %d!\n",__func__,__LINE__, req->poolId);
			return -EINVAL;
		}
		if (test_bit(MMZ_VB_POOL_FLAG_DESTORY, &pool->flag)) {
			up_read(&partitions->idr_lock);
			dev_err(mmz_vb_dev, "%s %d, pool %d is in destory state, not allow "
				"to alloc block!\n",__func__,__LINE__, req->poolId);
			return -ENOTSUPP;
		}
		pool_cfg = &pool->poolCfg;
		if (req->blkSize > pool_cfg->blkSize) {
			up_read(&partitions->idr_lock);
			dev_err(mmz_vb_dev, "%s %d, pool blkSize 0x%llx is "
				"smaller than request size 0x%llx\n",__func__,__LINE__,
				pool_cfg->blkSize, req->blkSize);
			return -EINVAL;
		}
		ret = vb_pool_get_block(pool, ppBlk);
		up_read(&partitions->idr_lock);
	} else if (req->uid >= VB_UID_COMMON && req->uid < VB_UID_MAX) {
		down_read(&vb_priv->pool_lock[req->uid]);
		/*try to get block for the exact block size */
		hash_for_each_possible(vb_priv->ht[req->uid], pool, node, PAGE_ALIGN(req->blkSize)) {
			pool_cfg = &pool->poolCfg;
			if (PAGE_ALIGN(req->blkSize) == pool_cfg->blkSize &&
				!strcmp(req->mmzName, pool_cfg->mmzName)) {
					ret = vb_pool_get_block(pool, ppBlk);
					if (0 == ret) {
						break;
					}
			}
		}
		/*try to get block from the pool whose block size > req->blkSize*/
		if (0 != ret) {
			hash_for_each(vb_priv->ht[req->uid], bkt, pool, node) {
				pool_cfg = &pool->poolCfg;
				if (req->blkSize < pool_cfg->blkSize &&
					!strcmp(req->mmzName, pool_cfg->mmzName)) {
						/*get the pool size which is closest to the req->blkSize*/
						if ((NULL == pool_tmp || (pool_tmp->poolCfg.blkSize > pool->poolCfg.blkSize))
							&& vb_pool_get_free_block_cnt_unlock(pool)) {
								pool_tmp = pool;
						}
				}
			}
			if (NULL != pool_tmp) {
				ret = vb_pool_get_block(pool_tmp, ppBlk);
			}
		}
		up_read(&vb_priv->pool_lock[req->uid]);
	} else {
        dev_err(mmz_vb_dev, "%s %d, invaild uid %d\n",__func__,__LINE__, req->uid);
    }
	return ret;
}

static void vb_release_block(struct esVB_K_BLOCK_INFO_S *pBlk)
{
	struct esVB_K_POOL_INFO_S *pool;
	struct mmz_vb_priv *vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &vb_priv->partitions;
	bool need_destory = false;
	struct rw_semaphore *lock;
	int ret;

	pool = pBlk->pool;

	lock = VB_UID_PRIVATE == pool->enVbUid ? \
		&partitions->idr_lock : &vb_priv->pool_lock[pool->enVbUid];
	/*
	  usually we don't need to destory pool here.
	  so just get read lock first.
	*/
	down_read(lock);
	spin_lock(&pool->lock);
	bitmap_clear(pool->bitmap, pBlk->nr, 1);
	if (bitmap_empty(pool->bitmap, pool->poolCfg.blkCnt) &&
		test_bit(MMZ_VB_POOL_FLAG_DESTORY, &pool->flag)) {
			need_destory = true;
	}
	spin_unlock(&pool->lock);
	up_read(lock);
	if (atomic_dec_return(&vb_priv->allocBlkcnt) == 0) {
		module_put(THIS_MODULE);
	}

	if (true == need_destory) {
		down_write(lock);
		ret = mmz_vb_do_destory_pool(pool, false, false);
		if (ret) {
			dev_err(mmz_vb_dev, "%s %d, faild to destory pool, enVbUid %d, PoolId %d, ret %d\n",
				__func__,__LINE__, pool->enVbUid, pool->poolId, ret);
		}
		up_write(lock);
	}
}

static int vb_is_splitted_blk(int fd, bool *isSplittedBlk)
{
	int ret = 0;
	struct dma_buf *dmabuf;

	/* get dmabuf handle */
	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		return -EINVAL;
	}

	if (strncmp(dmabuf->exp_name, MMZ_VB_DMABUF_NAME, sizeof(MMZ_VB_DMABUF_NAME))) {
		vb_err("It's NOT a mmz_vb buffer!!!\n");
		dma_buf_put(dmabuf);
		return -EINVAL;
	}

	if (!strncmp(dmabuf->exp_name, MMZ_VB_DMABUF_SPLITTED_NAME, sizeof(MMZ_VB_DMABUF_SPLITTED_NAME)))
		*isSplittedBlk = true;
	else
		*isSplittedBlk = false;

	dma_buf_put(dmabuf);
	return ret;
}

static int vb_blk_to_pool(struct esVB_BLOCK_TO_POOL_CMD_S *blkToPoolCmd)
{
	int ret = 0;
	struct dma_buf *dmabuf;
	struct mmz_vb_buffer *buffer;
	struct esw_export_buffer_info *splittedBuffer;
	struct dma_buf *blkDmabuf;
	bool isSplittedBlk;

	/* get dmabuf handle */
	dmabuf = dma_buf_get(blkToPoolCmd->fd);
	if (IS_ERR(dmabuf)) {
		return -EINVAL;
	}

	ret = vb_is_splitted_blk(blkToPoolCmd->fd, &isSplittedBlk);
	if (ret) {
		dma_buf_put(dmabuf);
		ret = -EINVAL;
		goto out_put_dmabuf;
	}

	if (true == isSplittedBlk) { // This is a splitted block
		splittedBuffer = dmabuf->priv;
		blkDmabuf = dma_buf_get(splittedBuffer->dbuf_fd);
		if (IS_ERR(blkDmabuf)) {
			ret = -EINVAL;
			goto out_put_dmabuf;
		}
		buffer = blkDmabuf->priv;
		blkToPoolCmd->poolId = buffer->pBlk->pool->poolId;
		dma_buf_put(blkDmabuf);
	}
	else {	// This is a real block
		buffer = dmabuf->priv;
		blkToPoolCmd->poolId = buffer->pBlk->pool->poolId;
	}

out_put_dmabuf:
	dma_buf_put(dmabuf);
	return ret;
}

static int vb_get_blk_offset(struct esVB_GET_BLOCKOFFSET_CMD_S *getBlkOffsetCmd)
{
	int ret = 0;
	struct dma_buf *dmabuf;
	struct mmz_vb_buffer *buffer;
	struct esw_export_buffer_info *splittedBuffer;
	struct dma_buf *blkDmabuf;
	__u64 blkSize, offsetInPool;

	bool isSplittedBlk;

	dmabuf = dma_buf_get(getBlkOffsetCmd->fd);
	if (IS_ERR(dmabuf)) {
		return -EINVAL;
	}

	ret = vb_is_splitted_blk(getBlkOffsetCmd->fd, &isSplittedBlk);
	if (ret) {
		ret = -EINVAL;
		goto out_put_dmabuf;
	}

	if (true == isSplittedBlk) { // It's a splitted block
		splittedBuffer = dmabuf->priv;
		blkDmabuf = dma_buf_get(splittedBuffer->dbuf_fd);
		if (IS_ERR(blkDmabuf)) {
			ret = -EINVAL;
			goto out_put_dmabuf;
		}
		buffer = blkDmabuf->priv;
		blkSize = buffer->len;
		offsetInPool = blkSize * buffer->pBlk->nr + splittedBuffer->slice.offset;
		dma_buf_put(blkDmabuf);
	}
	else { // It's a real block
		buffer = dmabuf->priv;
		blkSize = buffer->len;
		offsetInPool = blkSize * buffer->pBlk->nr;
	}
	getBlkOffsetCmd->offset = offsetInPool;

out_put_dmabuf:
	dma_buf_put(dmabuf);

	return ret;
}

static int vb_split_dmabuf(struct esVB_SPLIT_DMABUF_CMD_S *splitDmabufCmd)
{
	int ret = 0;
	struct dma_buf *dmabuf;
	char splittedBuffer_ExpName[ES_MAX_MMZ_NAME_LEN];
	int i;

	if (!splitDmabufCmd->len)
		return -EINVAL;

	/* get dmabuf handle */
	dmabuf = dma_buf_get(splitDmabufCmd->fd);
	if (IS_ERR(dmabuf)) {
		return -EINVAL;
	}

	if (strstr(dmabuf->exp_name, "_splitted")) { // It's a splitted dmabuf already, can't be splitted further
		vb_err("Can't split a splitted buffer!!!\n");
		ret = -EINVAL;
		goto out_put_dmabuf;
	}

	/* offset and len must be paged aligned */
	if (!PAGE_ALIGNED(splitDmabufCmd->offset) || !PAGE_ALIGNED(splitDmabufCmd->len)) {
		vb_err("splitted offset or len is not page aligned!!!\n");
		ret = -EINVAL;
		goto out_put_dmabuf;
	}

	if (splitDmabufCmd->offset + splitDmabufCmd->len > dmabuf->size) {
		vb_err("Splitted offset(0x%llx)+len(0x%llx) exceed the size(0x%llx) of the original buffer!!!\n",
			splitDmabufCmd->offset, splitDmabufCmd->len, (__u64)dmabuf->size);
		ret = -EINVAL;
		goto out_put_dmabuf;
	}

	/* Apend "_splitted" to the splitted buffer expname, so that it is identified by the suffix */
	i = snprintf(splittedBuffer_ExpName, sizeof(splittedBuffer_ExpName), "%s_splitted", dmabuf->exp_name);
	if (i > sizeof(splittedBuffer_ExpName)) {
		vb_err("Length of name(%d) for the the splitted buffer exceed the max name len(%ld)!!!\n",
			i, sizeof(splittedBuffer_ExpName));
		ret = -EINVAL;
		goto out_put_dmabuf;
	}

	splitDmabufCmd->slice_fd = esw_common_dmabuf_split_export(splitDmabufCmd->fd, splitDmabufCmd->offset,
						splitDmabufCmd->len, dmabuf->file->f_flags, splittedBuffer_ExpName);
	if (splitDmabufCmd->slice_fd < 0) {
		vb_err("Failed to split buffer, errVal %d\n", splitDmabufCmd->slice_fd);
		ret = -EFAULT;
		goto out_put_dmabuf;
	}

out_put_dmabuf:
	dma_buf_put(dmabuf);

	return ret;
}

static int vb_get_dmabuf_refcnt(struct esVB_DMABUF_REFCOUNT_CMD_S *getDmabufRefCntCmd)
{
	int ret = 0;
	struct dma_buf *dmabuf;

	/* get dmabuf handle */
	dmabuf = dma_buf_get(getDmabufRefCntCmd->fd);
	if (IS_ERR(dmabuf)) {
		return -EINVAL;
	}

	/* minus 1 because it was +1 by dma_buf_get */
	getDmabufRefCntCmd->refCnt = file_count(dmabuf->file) - 1;

	dma_buf_put(dmabuf);
	return ret;
}

#define PAGE_IN_SPRAM_DIE0(page) ((page_to_phys(page)>=0x59000000) && (page_to_phys(page)<0x59400000))
#define PAGE_IN_SPRAM_DIE1(page) ((page_to_phys(page)>=0x79000000) && (page_to_phys(page)<0x79400000))
static int do_vb_retrive_mem_node(struct dma_buf *dmabuf, int *nid)
{
	int ret = 0;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct page *page = NULL;

	get_dma_buf(dmabuf);
	attach = dma_buf_attach(dmabuf, mmz_vb_dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		/* put dmabuf back */
		dma_buf_put(dmabuf);
		return ret;
	}

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		dma_buf_detach(dmabuf, attach);
		dma_buf_put(dmabuf);
		return ret;
	}

	page = sg_page(sgt->sgl);
	if (unlikely(PAGE_IN_SPRAM_DIE0(page))) {
		*nid = 0;
	}
	else if(unlikely(PAGE_IN_SPRAM_DIE1(page))) {
		*nid = 1;
	}
	else
		*nid = page_to_nid(page);

	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
	/* detach */
	dma_buf_detach(dmabuf, attach);
	/* put dmabuf back */
	dma_buf_put(dmabuf);

	pr_debug("%s, mem node is %d\n", __func__, *nid);
	return ret;
}

static int vb_retrieve_mem_node(struct esVB_RETRIEVE_MEM_NODE_CMD_S *retrieveMemNodeCmd)
{
	int ret = 0;
	struct dma_buf *dmabuf;
	struct vm_area_struct *vma = NULL;
	vm_flags_t vm_flags;
	struct mm_struct *mm = current->mm;
	u64 vaddr;

	/* If cpu_vaddr is NULL, then try to retrieve mem node id by fd */
	if (retrieveMemNodeCmd->cpu_vaddr == NULL) {
		/* get dmabuf handle */
		dmabuf = dma_buf_get(retrieveMemNodeCmd->fd);
		if (IS_ERR(dmabuf)) {
			return PTR_ERR(dmabuf);
		}

		ret = do_vb_retrive_mem_node(dmabuf, &retrieveMemNodeCmd->numa_node);
		/* put dmabuf back */
		dma_buf_put(dmabuf);
	}
	else {
		vaddr = (u64)retrieveMemNodeCmd->cpu_vaddr;
		mmap_read_lock(mm);
		vma = vma_lookup(mm, vaddr & PAGE_MASK);
		if (!vma) {
			pr_err("Failed to vma_lookup!\n");
			return -EFAULT;
		}
		vm_flags = vma->vm_flags;
		mmap_read_unlock(mm);

		if (!(vm_flags & (VM_IO | VM_PFNMAP)) || (NULL == vma->vm_private_data)) {
			pr_debug("This vaddr is NOT mmapped with VM_PFNMAP!\n");
			return -EFAULT;
		}
		dmabuf = vma->vm_private_data;
		ret = do_vb_retrive_mem_node(dmabuf, &retrieveMemNodeCmd->numa_node);
	}

	return ret;
}

static int vb_get_dmabuf_size(struct esVB_DMABUF_SIZE_CMD_S *getDmabufSizeCmd)
{
	int ret = 0;
	struct dma_buf *dmabuf;

	/* get dmabuf handle */
	dmabuf = dma_buf_get(getDmabufSizeCmd->fd);
	if (IS_ERR(dmabuf)) {
		return -EINVAL;
	}

	/* minus 1 because it was +1 by dma_buf_get */
	getDmabufSizeCmd->size = dmabuf->size;

	dma_buf_put(dmabuf);

	return ret;
}

static int mmz_vb_init_memory_region(void)
{
	struct mmz_vb_priv *mmz_vb_priv = g_mmz_vb_priv;
	struct esVB_K_MMZ_S *partitions = &mmz_vb_priv->partitions;
	int nid, part = 0;
	int partitionID = 0;
	char blkName[BLOCK_MAX_NAME];
	struct mem_block *memblock = NULL;

	for (nid = 0; nid < 2; nid++) {
		for (part = 0; part < 2; part++) {
			snprintf(blkName, sizeof(blkName), "%s%d_part_%d", es_mmz_name_prefix, nid, part);
			memblock = eswin_rsvmem_get_memblock(blkName);
			if (memblock) {
				partitions->mem_blocks[partitionID] = memblock;
				dev_info(mmz_vb_dev, "%s was found successfully\n", blkName);
				partitionID++;
			}
			else {
				dev_dbg(mmz_vb_dev, "%s was NOT found\n", blkName);
			}
		}
	}

	/* Indicate how many VB memory block have been correctly initialized */
	return partitionID;
}
