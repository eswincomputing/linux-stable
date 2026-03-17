// SPDX-License-Identifier: GPL-2.0
/*
 * DVP2AXI Driver - VB2 Memory Operations (Per-buffer coherent allocation)
 *
 * Copyright (C) 2026 Beijing ESWIN Computing Technology Co., Ltd.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-memops.h>
#include "dvp2axi_vb2.h"
#include "hw.h"

/* ============================================================
 * Buffer
 * ============================================================ */
struct dvp2axi_vb2_buf {
	struct device		*dev;
	void			*vaddr;
	unsigned long		size;
	dma_addr_t		dma_addr;
	enum dma_data_direction	dma_dir;

	struct dvp2axi_mem_block	*block;
	struct dvp2axi_mem_pool		*pool;

	struct vb2_vmarea_handler	handler;
	refcount_t			refcount;
	struct vb2_buffer		*vb;
	struct sg_table			*sgt_base;

	struct dma_buf_attachment	*db_attach;
};

/* ============================================================
 * vb2_mem_ops implement
 * ============================================================ */
static void dvp2axi_vb2_put(void *buf_priv)
{
	struct dvp2axi_vb2_buf *buf = buf_priv;

	if (!buf)
		return;

	if (!refcount_dec_and_test(&buf->refcount))
		return;

	dev_dbg(buf->dev, "Freeing buffer: vaddr=%p, size=%lu\n",
		buf->vaddr, buf->size);

	if (buf->sgt_base) {
		sg_free_table(buf->sgt_base);
		kfree(buf->sgt_base);
	}

	if (buf->block)
		dvp2axi_mem_pool_free(buf->block);

	put_device(buf->dev);
	kfree(buf);
}

static void *dvp2axi_vb2_alloc(struct vb2_buffer *vb,
			       struct device *dev,
			       unsigned long size)
{
	struct es_dvp2axi_hw *ddev = dev_get_drvdata(dev);
	struct dvp2axi_mem_pool *pool = ddev->mem_pool;
	struct dvp2axi_mem_block *block;
	struct dvp2axi_vb2_buf *buf;

	dev_dbg(dev, "Allocating buffer: size=%lu\n", size);

	if (!pool) {
		dev_err(dev, "Memory pool not available\n");
		return ERR_PTR(-EINVAL);
	}

	block = dvp2axi_mem_pool_alloc(pool, size);
	if (IS_ERR(block)) {
		dev_err(dev, "Failed to allocate %lu bytes\n", size);
		return (void *)block;
	}

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf) {
		dvp2axi_mem_pool_free(block);
		return ERR_PTR(-ENOMEM);
	}

	buf->dev = get_device(dev);
	buf->pool = pool;
	buf->block = block;
	buf->size = size;
	buf->vaddr = block->vaddr;
	buf->dma_addr = block->dma_addr;
	buf->dma_dir = DMA_FROM_DEVICE;
	buf->vb = vb;

	refcount_set(&buf->refcount, 1);
	buf->handler.refcount = &buf->refcount;
	buf->handler.put = dvp2axi_vb2_put;
	buf->handler.arg = buf;

	dev_dbg(dev, "Allocated: vaddr=%p, dma=%pad, size=%lu\n",
		buf->vaddr, &buf->dma_addr, buf->size);

	return buf;
}

static void *dvp2axi_vb2_vaddr(struct vb2_buffer *vb, void *buf_priv)
{
	struct dvp2axi_vb2_buf *buf = buf_priv;

	if (buf->vaddr)
		return buf->vaddr;

	if (buf->db_attach) {
		struct iosys_map map;
		if (!dma_buf_vmap(buf->db_attach->dmabuf, &map))
			buf->vaddr = map.vaddr;
		return buf->vaddr;
	}

	return NULL;
}

static void *dvp2axi_vb2_cookie(struct vb2_buffer *vb, void *buf_priv)
{
	struct dvp2axi_vb2_buf *buf = buf_priv;
	return &buf->dma_addr;
}

static unsigned int dvp2axi_vb2_num_users(void *buf_priv)
{
	struct dvp2axi_vb2_buf *buf = buf_priv;
	return refcount_read(&buf->refcount);
}

static void dvp2axi_vb2_prepare(void *buf_priv)
{
	/* coherent memory: do nothing */
}

static void dvp2axi_vb2_finish(void *buf_priv)
{
	/* coherent memory: do nothing */
}

/*
 * dvp2axi_vb2_mmap() - mmap
 */
static int dvp2axi_vb2_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct dvp2axi_vb2_buf *buf = buf_priv;
	int ret;

	ret = dma_mmap_coherent(buf->dev, vma, buf->vaddr,
				buf->dma_addr, buf->size);
	if (ret) {
		dev_err(buf->dev, "dma_mmap_coherent failed: %d\n", ret);
		return ret;
	}

	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_private_data = &buf->handler;
	vma->vm_ops = &vb2_common_vm_ops;
	vma->vm_ops->open(vma);

	dev_dbg(buf->dev, "mmap: dma=%pad, size=%lu\n",
		&buf->dma_addr, buf->size);

	return 0;
}

/*
 * The following implementation is from vb2_mem_ops_contig.
 */
static struct sg_table *dvp2axi_vb2_get_base_sgt(struct dvp2axi_vb2_buf *buf)
{
	struct sg_table *sgt;
	int ret;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	ret = dma_get_sgtable(buf->dev, sgt, buf->vaddr,
			      buf->dma_addr, buf->size);
	if (ret < 0) {
		dev_err(buf->dev, "dma_get_sgtable failed: %d\n", ret);
		kfree(sgt);
		return NULL;
	}

	return sgt;
}

/* DMABUF ops (from videobuf2-dma-contig) */
struct dvp2axi_dc_attachment {
	struct sg_table sgt;
	enum dma_data_direction dma_dir;
};

static int dvp2axi_dc_dmabuf_ops_attach(struct dma_buf *dbuf,
	struct dma_buf_attachment *dbuf_attach)
{
	struct dvp2axi_dc_attachment *attach;
	struct dvp2axi_vb2_buf *buf = dbuf->priv;
	unsigned int i;
	struct scatterlist *rd, *wr;
	struct sg_table *sgt;
	int ret;

	attach = kzalloc(sizeof(*attach), GFP_KERNEL);
	if (!attach)
		return -ENOMEM;

	sgt = &attach->sgt;
	ret = sg_alloc_table(sgt, buf->sgt_base->orig_nents, GFP_KERNEL);
	if (ret) {
		kfree(attach);
		return -ENOMEM;
	}

	rd = buf->sgt_base->sgl;
	wr = sgt->sgl;
	for (i = 0; i < sgt->orig_nents; ++i) {
		sg_set_page(wr, sg_page(rd), rd->length, rd->offset);
		rd = sg_next(rd);
		wr = sg_next(wr);
	}

	attach->dma_dir = DMA_NONE;
	dbuf_attach->priv = attach;
	return 0;
}

static void dvp2axi_dc_dmabuf_ops_detach(struct dma_buf *dbuf,
	struct dma_buf_attachment *db_attach)
{
	struct dvp2axi_dc_attachment *attach = db_attach->priv;
	struct sg_table *sgt;

	if (!attach)
		return;

	sgt = &attach->sgt;
	if (attach->dma_dir != DMA_NONE)
		dma_unmap_sgtable(db_attach->dev, sgt, attach->dma_dir,
				  DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(sgt);
	kfree(attach);
	db_attach->priv = NULL;
}

static struct sg_table *dvp2axi_dc_dmabuf_ops_map(
	struct dma_buf_attachment *db_attach, enum dma_data_direction dma_dir)
{
	struct dvp2axi_dc_attachment *attach = db_attach->priv;
	struct sg_table *sgt;

	sgt = &attach->sgt;
	if (attach->dma_dir == dma_dir)
		return sgt;

	if (attach->dma_dir != DMA_NONE) {
		dma_unmap_sgtable(db_attach->dev, sgt, attach->dma_dir,
				  DMA_ATTR_SKIP_CPU_SYNC);
		attach->dma_dir = DMA_NONE;
	}

	if (dma_map_sgtable(db_attach->dev, sgt, dma_dir,
			    DMA_ATTR_SKIP_CPU_SYNC)) {
		pr_err("failed to map scatterlist\n");
		return ERR_PTR(-EIO);
	}

	attach->dma_dir = dma_dir;
	return sgt;
}

static void dvp2axi_dc_dmabuf_ops_unmap(struct dma_buf_attachment *db_attach,
	struct sg_table *sgt, enum dma_data_direction dma_dir)
{
}

static void dvp2axi_dc_dmabuf_ops_release(struct dma_buf *dbuf)
{
	dvp2axi_vb2_put(dbuf->priv);
}

static int dvp2axi_dc_dmabuf_ops_begin_cpu_access(struct dma_buf *dbuf,
	enum dma_data_direction direction)
{
	return 0;
}

static int dvp2axi_dc_dmabuf_ops_end_cpu_access(struct dma_buf *dbuf,
	enum dma_data_direction direction)
{
	return 0;
}

static int dvp2axi_dc_dmabuf_ops_vmap(struct dma_buf *dbuf, struct iosys_map *map)
{
	struct dvp2axi_vb2_buf *buf = dbuf->priv;
	void *vaddr;

	vaddr = dvp2axi_vb2_vaddr(buf->vb, buf);
	if (!vaddr)
		return -EINVAL;

	iosys_map_set_vaddr(map, vaddr);
	return 0;
}

static int dvp2axi_dc_dmabuf_ops_mmap(struct dma_buf *dbuf,
	struct vm_area_struct *vma)
{
	return dvp2axi_vb2_mmap(dbuf->priv, vma);
}

static const struct dma_buf_ops dvp2axi_dc_dmabuf_ops = {
	.attach		= dvp2axi_dc_dmabuf_ops_attach,
	.detach		= dvp2axi_dc_dmabuf_ops_detach,
	.map_dma_buf	= dvp2axi_dc_dmabuf_ops_map,
	.unmap_dma_buf	= dvp2axi_dc_dmabuf_ops_unmap,
	.begin_cpu_access = dvp2axi_dc_dmabuf_ops_begin_cpu_access,
	.end_cpu_access	= dvp2axi_dc_dmabuf_ops_end_cpu_access,
	.vmap		= dvp2axi_dc_dmabuf_ops_vmap,
	.mmap		= dvp2axi_dc_dmabuf_ops_mmap,
	.release	= dvp2axi_dc_dmabuf_ops_release,
};

static struct dma_buf *dvp2axi_vb2_get_dmabuf(struct vb2_buffer *vb,
					      void *buf_priv,
					      unsigned long flags)
{
	struct dvp2axi_vb2_buf *buf = buf_priv;
	struct dma_buf *dbuf;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	exp_info.ops = &dvp2axi_dc_dmabuf_ops;
	exp_info.size = buf->size;
	exp_info.flags = flags;
	exp_info.priv = buf;

	if (!buf->sgt_base)
		buf->sgt_base = dvp2axi_vb2_get_base_sgt(buf);

	if (WARN_ON(!buf->sgt_base))
		return NULL;

	dbuf = dma_buf_export(&exp_info);
	if (IS_ERR(dbuf))
		return NULL;

	refcount_inc(&buf->refcount);
	return dbuf;
}

static void dvp2axi_vb2_put_userptr(void *buf_priv)
{
	struct dvp2axi_vb2_buf *buf = buf_priv;
	struct sg_table *sgt = buf->sgt_base;
	struct page **pages;

	if (sgt) {
		pages = frame_vector_pages(buf->db_attach ?
					   NULL : NULL);
		if (buf->dma_dir == DMA_FROM_DEVICE ||
		    buf->dma_dir == DMA_BIDIRECTIONAL) {
			for_each_sgtable_page(sgt, NULL, 0)
				; /* nop */
		}
		dma_unmap_sgtable(buf->dev, sgt, buf->dma_dir,
				  DMA_ATTR_SKIP_CPU_SYNC);
		sg_free_table(sgt);
		kfree(sgt);
	}

	put_device(buf->dev);
	kfree(buf);
}

static void *dvp2axi_vb2_get_userptr(struct vb2_buffer *vb, struct device *dev,
				unsigned long vaddr, unsigned long size)
{
	struct dvp2axi_vb2_buf *buf;
	struct sg_table *sgt;
	unsigned long contig_size;
	unsigned long dma_align = dma_get_cache_alignment();
	int ret;

	if (!IS_ALIGNED(vaddr | size, dma_align))
		return ERR_PTR(-EINVAL);

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dev = get_device(dev);
	buf->dma_dir = vb->vb2_queue->dma_dir;
	buf->vb = vb;
	buf->size = size;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto fail_buf;
	}

	ret = dma_get_sgtable(buf->dev, sgt,
			      phys_to_virt(__pfn_to_phys(vaddr >> PAGE_SHIFT)),
			      __pfn_to_phys(vaddr >> PAGE_SHIFT), size);
	if (ret)
		goto fail_sgt;

	if (dma_map_sgtable(buf->dev, sgt, buf->dma_dir, 0)) {
		ret = -EIO;
		goto fail_sgt_init;
	}

	contig_size = sg_dma_len(sgt->sgl);
	if (contig_size < size) {
		ret = -EFAULT;
		goto fail_map_sg;
	}

	buf->dma_addr = sg_dma_address(sgt->sgl);
	buf->vaddr = phys_to_virt(__pfn_to_phys(vaddr >> PAGE_SHIFT));
	buf->sgt_base = sgt;

	return buf;

fail_map_sg:
	dma_unmap_sgtable(buf->dev, sgt, buf->dma_dir, 0);
fail_sgt_init:
	sg_free_table(sgt);
fail_sgt:
	kfree(sgt);
fail_buf:
	put_device(buf->dev);
	kfree(buf);
	return ERR_PTR(ret);
}

static int dvp2axi_vb2_map_dmabuf(void *mem_priv)
{
	struct dvp2axi_vb2_buf *buf = mem_priv;
	struct sg_table *sgt;
	unsigned long contig_size;

	if (WARN_ON(!buf->db_attach))
		return -EINVAL;

	if (buf->dma_addr)
		return 0;

	sgt = dma_buf_map_attachment(buf->db_attach, buf->dma_dir);
	if (IS_ERR(sgt))
		return -EINVAL;

	contig_size = sg_dma_len(sgt->sgl);
	if (contig_size < buf->size) {
		dma_buf_unmap_attachment(buf->db_attach, sgt, buf->dma_dir);
		return -EFAULT;
	}

	buf->dma_addr = sg_dma_address(sgt->sgl);
	buf->sgt_base = sgt;
	buf->vaddr = NULL;

	return 0;
}

static void dvp2axi_vb2_unmap_dmabuf(void *mem_priv)
{
	struct dvp2axi_vb2_buf *buf = mem_priv;

	if (WARN_ON(!buf->db_attach))
		return;

	if (buf->vaddr) {
		struct iosys_map map = IOSYS_MAP_INIT_VADDR(buf->vaddr);
		dma_buf_vunmap(buf->db_attach->dmabuf, &map);
		buf->vaddr = NULL;
	}

	if (buf->sgt_base) {
		dma_buf_unmap_attachment(buf->db_attach,
					buf->sgt_base, buf->dma_dir);
		buf->sgt_base = NULL;
	}

	buf->dma_addr = 0;
}

static void dvp2axi_vb2_detach_dmabuf(void *mem_priv)
{
	struct dvp2axi_vb2_buf *buf = mem_priv;

	if (buf->dma_addr)
		dvp2axi_vb2_unmap_dmabuf(buf);

	dma_buf_detach(buf->db_attach->dmabuf, buf->db_attach);
	kfree(buf);
}

static void *dvp2axi_vb2_attach_dmabuf(struct vb2_buffer *vb, struct device *dev,
				  struct dma_buf *dbuf, unsigned long size)
{
	struct dvp2axi_vb2_buf *buf;
	struct dma_buf_attachment *dba;

	if (dbuf->size < size)
		return ERR_PTR(-EFAULT);

	if (WARN_ON(!dev))
		return ERR_PTR(-EINVAL);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	buf->dev = get_device(dev);
	buf->vb = vb;

	dba = dma_buf_attach(dbuf, buf->dev);
	if (IS_ERR(dba)) {
		put_device(buf->dev);
		kfree(buf);
		return dba;
	}

	buf->dma_dir = vb->vb2_queue->dma_dir;
	buf->size = size;
	buf->db_attach = dba;

	return buf;
}

const struct vb2_mem_ops dvp2axi_vb2_mem_ops = {
	.alloc		= dvp2axi_vb2_alloc,
	.put		= dvp2axi_vb2_put,
	.get_dmabuf	= dvp2axi_vb2_get_dmabuf,
	.cookie		= dvp2axi_vb2_cookie,
	.vaddr		= dvp2axi_vb2_vaddr,
	.mmap		= dvp2axi_vb2_mmap,
	.get_userptr	= dvp2axi_vb2_get_userptr,
	.put_userptr	= dvp2axi_vb2_put_userptr,
	.prepare	= dvp2axi_vb2_prepare,
	.finish		= dvp2axi_vb2_finish,
	.map_dmabuf	= dvp2axi_vb2_map_dmabuf,
	.unmap_dmabuf	= dvp2axi_vb2_unmap_dmabuf,
	.attach_dmabuf	= dvp2axi_vb2_attach_dmabuf,
	.detach_dmabuf	= dvp2axi_vb2_detach_dmabuf,
	.num_users	= dvp2axi_vb2_num_users,
};
EXPORT_SYMBOL_GPL(dvp2axi_vb2_mem_ops);
