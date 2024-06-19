// SPDX-License-Identifier: GPL-2.0-only
/*
 * ESWIN rerserved memory heap.
 * eswin_rsvmem_heap creates heap for the reserved memory that has compatible = "eswin-reserve-memory";
 * property and no-map property.
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/dma-map-ops.h>
#include <linux/eswin_rsvmem_common.h>
#include "../eswin_memblock.h"
#include "../es_buddy/es_buddy.h"
#include "include/uapi/linux/eswin_rsvmem_common.h"

static const unsigned int orders[] = {MAX_ORDER-1, 9, 0};
#define NUM_ORDERS ARRAY_SIZE(orders)

struct eswin_rsvmem_heap {
	struct eswin_heap *heap;
	struct mem_block *memblock;
};

struct eswin_rsvmem_heap_buffer {
	struct eswin_rsvmem_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct sg_table sg_table; // for buddy allocator
	struct page **pages;
	int vmap_cnt;
	void *vaddr;
	unsigned long fd_flags; // for vmap to determin the cache or non-cached mapping
};

struct eswin_heap_attachment {
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

static int eswin_rsvmem_heap_attach(struct dma_buf *dmabuf,
			   struct dma_buf_attachment *attachment)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;
	struct eswin_heap_attachment *a;
	struct sg_table *table;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = dup_sg_table(&buffer->sg_table);
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

static void eswin_rsvmem_heap_detach(struct dma_buf *dmabuf,
			    struct dma_buf_attachment *attachment)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;
	struct eswin_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(a->table);
	kfree(a->table);
	kfree(a);
}

static struct sg_table *eswin_rsvmem_heap_map_dma_buf(struct dma_buf_attachment *attachment,
					     enum dma_data_direction direction)
{
	struct eswin_heap_attachment *a = attachment->priv;
	struct sg_table *table =a->table;
	int ret;
	unsigned long attrs = DMA_ATTR_SKIP_CPU_SYNC;

	/* Skipt cache sync, since it takes a lot of time when import to device.
	*  It's the user's responsibility for guaranteeing the cache coherency by
	   flusing cache explicitly before importing to device.
	*/
	ret = dma_map_sgtable(attachment->dev, table, direction, attrs);

	if (ret)
		return ERR_PTR(-ENOMEM);
	a->mapped = true;
	return table;
}

static void eswin_rsvmem_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				   struct sg_table *table,
				   enum dma_data_direction direction)
{
	struct eswin_heap_attachment *a = attachment->priv;
	unsigned long attrs = DMA_ATTR_SKIP_CPU_SYNC;

	a->mapped = false;

	/* Skipt cache sync, since it takes a lot of time when unmap from device.
	*  It's the user's responsibility for guaranteeing the cache coherency after
	   the device has done processing the data.(For example, CPU do NOT read untill
	   the device has done)
	*/
	dma_unmap_sgtable(attachment->dev, table, direction, attrs);
}

static int eswin_rsvmem_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					     enum dma_data_direction direction)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	struct scatterlist *sg;
	int i;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	/* Since the cache sync was skipped when eswin_rsvmem_heap_map_dma_buf/eswin_rsvmem_heap_unmap_dma_buf,
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

static int eswin_rsvmem_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					   enum dma_data_direction direction)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	struct scatterlist *sg;
	int i;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	/* Since the cache sync was skipped while eswin_rsvmem_heap_map_dma_buf/eswin_rsvmem_heap_unmap_dma_buf,
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
static int eswin_rsvmem_sync_cache_internal(struct dma_buf *dmabuf, enum dma_data_direction direction)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	struct scatterlist *sg;
	int i;

	for_each_sg(table->sgl, sg, table->orig_nents, i)
		arch_sync_dma_for_device(sg_phys(sg), sg->length, direction);


	return 0;
}
#endif

static int eswin_rsvmem_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;
	struct eswin_heap *heap = buffer->heap->heap;
	struct sg_table *table = &buffer->sg_table;
	unsigned long addr = vma->vm_start;
	unsigned long pgoff = vma->vm_pgoff, mapsize = 0;
	unsigned long size_remaining = vma->vm_end - vma->vm_start;//vma_pages(vma);
	struct scatterlist *sg;
	struct page *page = NULL;
	unsigned int nents = 0;
	int i;
	int ret;
	const char *heap_name = NULL;

	/* Mapping secure_memory with cached proprty to user space for CPU is NOT permitted */
	heap_name = eswin_heap_get_name(heap);
	if (unlikely(!strncmp("secure_memory", heap_name, 13))) {
		if (!(vma->vm_flags & VM_NORESERVE))
			return -EPERM;
	}

	if ((vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) == 0)
		return -EINVAL;

	/* vm_private_data will be used by eswin-ipc-scpu.c.
	    ipc will import this dmabuf to get iova.
	*/
	vma->vm_private_data = dmabuf;

	/* support mman flag MAP_SHARED_VALIDATE | VM_NORESERVE, used to map uncached memory to user space.
	   Users should guarantee this buffer has been flushed to cache already.
	 */
	if (vma->vm_flags & VM_NORESERVE) {
		vm_flags_clear(vma, VM_NORESERVE);
		#ifndef QEMU_DEBUG
		vma->vm_page_prot = pgprot_dmacoherent(vma->vm_page_prot);
		#endif
		/* skip sync cache, users should guarantee the cache is clean after done using it in
		   cached mode(i.e, ES_SYS_Mmap(SYS_CACHE_MODE_CACHED))
		*/
	}
	pr_debug("%s, size_remaining:0x%lx, pgoff:0x%lx, dmabuf->size:0x%lx, start_phys:0x%llx\n",
		__func__, size_remaining, pgoff, dmabuf->size, sg_phys(table->sgl));
	for_each_sg(table->sgl, sg, table->orig_nents, i) {
		pr_debug("sgl:%d, phys:0x%llx\n", i, sg_phys(sg));
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

static void *eswin_rsvmem_heap_do_vmap(struct dma_buf *dmabuf)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;
	pgprot_t prot = PAGE_KERNEL;
	struct sg_table *table = &buffer->sg_table;
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

	/* The property of this dmabuf in kernel space is determined by heap alloc with fd_flag. */
	if (buffer->fd_flags & O_DSYNC) {
		#ifndef QEMU_DEBUG
		prot = pgprot_dmacoherent(PAGE_KERNEL);
		#endif
		pr_debug("%s syport uncached kernel dmabuf!, prot=0x%x\n", __func__, (unsigned int)pgprot_val(prot));
	}
	else {
		pr_debug("%s memport cached kernel dmabuf!\n", __func__);
	}

	vaddr = vmap(pages, npages, VM_MAP, prot);
	vfree(pages);

	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

static int eswin_rsvmem_heap_vmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;
	void *vaddr;
	int ret = 0;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		dma_buf_map_set_vaddr(map, buffer->vaddr);
		goto out;
	}

	vaddr = eswin_rsvmem_heap_do_vmap(dmabuf);
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

static void eswin_rsvmem_heap_vunmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
	dma_buf_map_clear(map);
}

static void eswin_rsvmem_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct eswin_rsvmem_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table;
	struct scatterlist *sg;
	int i;

	table = &buffer->sg_table;
	if (buffer->vmap_cnt > 0) {
		WARN(1, "%s: buffer still mapped in the kernel\n", __func__);
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}

	for_each_sgtable_sg(table, sg, i) {
		struct page *page = sg_page(sg);
		// pr_debug("%s:%d,page_size(page)=0x%lx, phys_addr=0x%llx\n",
			// __func__, __LINE__, page_size(page), page_to_phys(page));
		es_free_pages(buffer->heap->memblock, page);
	}
	sg_free_table(table);

	kfree(buffer);
}

static const struct dma_buf_ops eswin_rsvmem_heap_buf_ops = {
	.attach = eswin_rsvmem_heap_attach,
	.detach = eswin_rsvmem_heap_detach,
	.map_dma_buf = eswin_rsvmem_heap_map_dma_buf,
	.unmap_dma_buf = eswin_rsvmem_heap_unmap_dma_buf,
	.begin_cpu_access = eswin_rsvmem_dma_buf_begin_cpu_access,
	.end_cpu_access = eswin_rsvmem_dma_buf_end_cpu_access,
	.mmap = eswin_rsvmem_heap_mmap,
	.vmap = eswin_rsvmem_heap_vmap,
	.vunmap = eswin_rsvmem_heap_vunmap,
	.release = eswin_rsvmem_heap_dma_buf_release,
};

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

static struct dma_buf *eswin_rsvmem_heap_allocate(struct eswin_heap *heap,
					    unsigned long len,
					    unsigned long fd_flags,
					    unsigned long heap_flags)
{
	struct eswin_rsvmem_heap *rsvmem_heap = eswin_heap_get_drvdata(heap);
	struct eswin_rsvmem_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	unsigned long size_remaining = len;
	unsigned int max_order = orders[0];
	struct dma_buf *dmabuf;
	struct sg_table *table;
	struct scatterlist *sg;
	struct list_head pages;
	struct page *page, *tmp_page;
	int i, ret = -ENOMEM;
	const char *heap_name = NULL;

	/* Mapping secure_memory with cached proprty to kernel space for CPU is NOT permitted */
	heap_name = eswin_heap_get_name(rsvmem_heap->heap);
	if (unlikely(!strncmp("secure_memory", heap_name, 13))) {
		if (!(fd_flags & O_DSYNC))
			return ERR_PTR(-EINVAL);
	}

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->heap = rsvmem_heap;
	buffer->len = len;
	buffer->fd_flags = fd_flags;

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

		page = alloc_largest_available(rsvmem_heap->memblock, size_remaining, max_order);
		if (!page)
			goto free_buffer;

		list_add_tail(&page->lru, &pages);
		size_remaining -= page_size(page);
		max_order = compound_order(page);
		i++;
	}

	table = &buffer->sg_table;
	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_buffer;

	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	/* create the dmabuf */
	exp_info.exp_name = eswin_heap_get_name(heap);
	exp_info.ops = &eswin_rsvmem_heap_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = O_RDWR | O_CLOEXEC;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pages;
	}
	return dmabuf;

free_pages:
	for_each_sgtable_sg(table, sg, i) {
		struct page *p = sg_page(sg);

		es_free_pages(rsvmem_heap->memblock, p);
	}
	sg_free_table(table);
free_buffer:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		es_free_pages(rsvmem_heap->memblock, page);
	kfree(buffer);

	return ERR_PTR(ret);
}

static const struct eswin_heap_ops eswin_rsvmem_heap_ops = {
	.allocate = eswin_rsvmem_heap_allocate,
};

static int __add_eswin_rsvmem_heap(struct mem_block *memblock, void *data)
{
	struct eswin_rsvmem_heap *rsvmem_heap;
	struct eswin_heap_export_info exp_info;

	rsvmem_heap = kzalloc(sizeof(*rsvmem_heap), GFP_KERNEL);
	if (!rsvmem_heap)
		return -ENOMEM;
	rsvmem_heap->memblock = memblock;

	exp_info.name = eswin_rsvmem_get_name(memblock);
	exp_info.ops = &eswin_rsvmem_heap_ops;
	exp_info.priv = rsvmem_heap;

	rsvmem_heap->heap = eswin_heap_add(&exp_info);
	if (IS_ERR(rsvmem_heap->heap)) {
		int ret = PTR_ERR(rsvmem_heap->heap);

		kfree(rsvmem_heap);
		return ret;
	}

	pr_info("%s for %s successfully!\n", __func__, exp_info.name);

	return 0;
}

static char *es_heap_name_prefix[] = {
						"mmz_nid_",
						"secure_memory"
};
#define NUM_ESWIN_RSVMEM_HEAPS ARRAY_SIZE(es_heap_name_prefix)

static int do_add_eswin_rsvmem_heap(struct mem_block *memblock, void *data)
{
	int ret = 0;
	char *prefix = data;
	const char *rsvmem_name = eswin_rsvmem_get_name(memblock);

	if (strncmp(rsvmem_name, prefix, strlen(prefix)) == 0)
		ret = __add_eswin_rsvmem_heap(memblock, NULL);

	return ret;
}
static int add_eswin_rsvmem_heaps(void)
{
	int i;
	int ret;

	ret = eswin_heap_init();
	if (ret)
		return ret;

	for (i = 0; i < NUM_ESWIN_RSVMEM_HEAPS; i++) {
		eswin_rsvmem_for_each_block(do_add_eswin_rsvmem_heap, es_heap_name_prefix[i]);
	}

	return 0;
}

static int do_delete_eswin_rsvmem_heap(struct mem_block *memblock, void *data)
{
	int ret = 0;
	char *prefix = data;
	const char *rsvmem_name = eswin_rsvmem_get_name(memblock);

	if (strncmp(rsvmem_name, prefix, strlen(prefix)) == 0)
		ret = eswin_heap_delete_by_name(rsvmem_name);

	return ret;
}
static void __exit delete_eswin_rsvmem_heaps(void)
{
	int i;

	for (i = 0; i < NUM_ESWIN_RSVMEM_HEAPS; i++) {
		eswin_rsvmem_for_each_block(do_delete_eswin_rsvmem_heap, es_heap_name_prefix[i]);
	}
	eswin_heap_uninit();
}
module_init(add_eswin_rsvmem_heaps);
module_exit(delete_eswin_rsvmem_heaps);
MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL v2");
