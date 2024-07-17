// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/dma-heap.h>
#include <linux/dma-direct.h>
#include <linux/dsp_dma_buf.h>
#include <linux/eswin_rsvmem_common.h>

MODULE_IMPORT_NS(DMA_BUF);

dma_addr_t dev_mem_attach(struct dma_buf *buf, struct device *dev,
			  enum dma_data_direction direc,
			  struct dma_buf_attachment **attachment)
{
	struct sg_table *table;
	dma_addr_t reg;
	unsigned int size;
	struct dma_buf_attachment *attach;
	struct scatterlist *sg = NULL;
	u64 addr;
	int len;
	int i = 0;

	attach = dma_buf_attach(buf, dev);
	if (IS_ERR(attach)) {
		pr_err("%s, dma buf attach error.\n", __func__);
		return 0;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	table = dma_buf_map_attachment_unlocked(attach, direc);
#else
	table = dma_buf_map_attachment(attach, direc);
#endif
	if (IS_ERR(table)) {
		pr_err("%s, dma buf map attachment error.\n", __func__);
		dma_buf_detach(buf, attach);
		return 0;
	}

	/*
	 * workaround: caching sgt table in attachment.
	 */
	if (!attach->dmabuf->ops->cache_sgt_mapping) {
		attach->sgt = table;
	}

	for_each_sgtable_dma_sg(table, sg, i) {
		addr = sg_dma_address(sg);
		len = sg_dma_len(sg);
		pr_debug("%s, i=%d, sg addr=0x%llx, len=0x%x, phyaddr=0x%llx.\n",
		       __func__, i, addr, len, dma_to_phys(dev, addr));
	}

	reg = sg_dma_address(table->sgl);
	size = sg_dma_len(table->sgl);
	if (attachment != NULL) {
		*attachment = attach;
	}

	return reg;
}
EXPORT_SYMBOL(dev_mem_attach);

int dev_mem_detach(struct dma_buf_attachment *attach,
		   enum dma_data_direction direction)
{
	struct sg_table *table;

	if (!attach->dmabuf->ops->cache_sgt_mapping) {
		table = attach->sgt;
		attach->sgt = NULL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	dma_buf_unmap_attachment_unlocked(attach, table, DMA_BIDIRECTIONAL);
#else
	dma_buf_unmap_attachment(attach, table, DMA_BIDIRECTIONAL);
#endif
	dma_buf_detach(attach->dmabuf, attach);
	return 0;
}
EXPORT_SYMBOL(dev_mem_detach);

// this interface only for  x86 emulation
int dev_mem_alloc(size_t size, enum es_malloc_policy policy,
		  struct dma_buf **buf)
{
	int fd;
	struct dma_buf *dma_buf;

	switch (policy) {
	case ES_MEM_ALLOC_RSV:
		fd = dma_heap_kalloc("reserved", size, O_RDWR, 0);
		break;
	case ES_MEM_ALLOC_NORMAL:
		fd = dma_heap_kalloc("system", size, O_RDWR, 0);
		break;
	case ES_MEM_ALLOC_NORMAL_COHERENT:
		fd = dma_heap_kalloc("system", size, O_RDWR | O_DSYNC, 0);
		break;
	case ES_MEM_ALLOC_CMA:
		fd = dma_heap_kalloc("linux,cma", size, O_RDWR, 0);
		break;
	case ES_MEM_ALLOC_CMA_COHERENT:
		fd = dma_heap_kalloc("linux,cma", size, O_RDWR  | O_DSYNC, 0);
		break;
	case ES_MEM_ALLOC_CMA_LLC:
		fd = dma_heap_kalloc("linux,cma", size, O_RDWR | O_SYNC, 0);
		break;
	case ES_MEM_ALLOC_SPRAM_DIE0:
		fd = dma_heap_kalloc("llc_spram0", size, O_RDWR, 1);
		break;
	case ES_MEM_ALLOC_SPRAM_DIE1:
		fd = dma_heap_kalloc("llc_spram1", size, O_RDWR, 1);
		break;
	default:
		pr_err("policy = %d not supported.\n", policy);
		return -EINVAL;
	}
	if (fd < 0) {
		return fd;
	}
	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf)) {
		return -EINVAL;
	}
	if (buf != NULL) {
		*buf = dma_buf;
	}

	return fd;
}
EXPORT_SYMBOL(dev_mem_alloc);

int dev_mem_free(struct dma_buf *buf)
{
	if (buf != NULL) {
		dma_buf_put(buf);
	}
	return 0;
}
EXPORT_SYMBOL(dev_mem_free);

void *dev_mem_vmap(struct dev_buffer_desc *buffer)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    struct iosys_map map;
#else
	struct dma_buf_map map;
#endif
	int ret;

	if (!buffer) {
		return NULL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	ret = dma_buf_vmap_unlocked(buffer->buf, &map);
#else
	ret = dma_buf_vmap(buffer->buf, &map);
#endif
	if (ret) {
		return NULL;
	}
	return map.vaddr;
}
EXPORT_SYMBOL(dev_mem_vmap);

void dev_mem_vunmap(struct dev_buffer_desc *buffer, void *virt)
{

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    struct iosys_map map;
#else
	struct dma_buf_map map;
#endif
	if (!buffer || !virt) {
		return;
	}

	map.vaddr = virt;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	dma_buf_vunmap_unlocked(buffer->buf, &map);
#else
	dma_buf_vunmap(buffer->buf, &map);
#endif
}
EXPORT_SYMBOL(dev_mem_vunmap);

MODULE_LICENSE("GPL");
