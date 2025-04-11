// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI common driver
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Eswin VI team
 */

#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-dma-sg.h>
#include <linux/of_platform.h>
#include "dev.h"
#include "common.h"

static void es_dvp2axi_init_dummy_vb2(struct es_dvp2axi_device *dev,
				struct es_dvp2axi_dummy_buffer *buf)
{
	unsigned long attrs = buf->is_need_vaddr ? 0 : DMA_ATTR_NO_KERNEL_MAPPING;

	memset(&buf->vb2_queue, 0, sizeof(struct vb2_queue));
	memset(&buf->vb, 0, sizeof(struct vb2_buffer));
	buf->vb2_queue.gfp_flags = GFP_KERNEL | GFP_DMA32;
	buf->vb2_queue.dma_dir = DMA_BIDIRECTIONAL;
	if (dev->hw_dev->is_dma_contig)
		attrs |= DMA_ATTR_FORCE_CONTIGUOUS;
	buf->vb2_queue.dma_attrs = attrs;
	buf->vb.vb2_queue = &buf->vb2_queue;
}

int es_dvp2axi_alloc_buffer(struct es_dvp2axi_device *dev,
		       struct es_dvp2axi_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;
	struct sg_table	 *sg_tbl;
	void *mem_priv;
	int ret = 0;

	if (!buf->size) {
		ret = -EINVAL;
		goto err;
	}

	es_dvp2axi_init_dummy_vb2(dev, buf);

	buf->size = PAGE_ALIGN(buf->size);
	mem_priv = g_ops->alloc(&buf->vb, dev->hw_dev->dev, buf->size);
	if (IS_ERR_OR_NULL(mem_priv)) {
		ret = -ENOMEM;
		goto err;
	}

	buf->mem_priv = mem_priv;
	if (dev->hw_dev->is_dma_sg_ops) {
		sg_tbl = (struct sg_table *)g_ops->cookie(&buf->vb, mem_priv);
		buf->dma_addr = sg_dma_address(sg_tbl->sgl);
		g_ops->prepare(mem_priv);
	} else {
		buf->dma_addr = *((dma_addr_t *)g_ops->cookie(&buf->vb, mem_priv));
	}
	if (buf->is_need_vaddr)
		buf->vaddr = g_ops->vaddr(&buf->vb, mem_priv);
	if (buf->is_need_dbuf) {
		buf->dbuf = g_ops->get_dmabuf(&buf->vb, mem_priv, O_RDWR);
		if (buf->is_need_dmafd) {
			buf->dma_fd = dma_buf_fd(buf->dbuf, O_CLOEXEC);
			if (buf->dma_fd < 0) {
				dma_buf_put(buf->dbuf);
				ret = buf->dma_fd;
				goto err;
			}
			get_dma_buf(buf->dbuf);
		}
	}
	v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
		 "%s buf:0x%x~0x%x size:%d\n", __func__,
		 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size, buf->size);
	return ret;
err:
	dev_err(dev->dev, "%s failed ret:%d\n", __func__, ret);
	return ret;
}

void es_dvp2axi_free_buffer(struct es_dvp2axi_device *dev,
			struct es_dvp2axi_dummy_buffer *buf)
{
	const struct vb2_mem_ops *g_ops = dev->hw_dev->mem_ops;

	if (buf && buf->mem_priv) {
		v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
			 "%s buf:0x%x~0x%x\n", __func__,
			 (u32)buf->dma_addr, (u32)buf->dma_addr + buf->size);
		if (buf->dbuf)
			dma_buf_put(buf->dbuf);
		g_ops->put(buf->mem_priv);
		buf->size = 0;
		buf->dbuf = NULL;
		buf->vaddr = NULL;
		buf->mem_priv = NULL;
		buf->is_need_dbuf = false;
		buf->is_need_vaddr = false;
		buf->is_need_dmafd = false;
		buf->is_free = true;
	}
}

static int es_dvp2axi_alloc_page_dummy_buf(struct es_dvp2axi_device *dev, struct es_dvp2axi_dummy_buffer *buf)
{
	struct es_dvp2axi_hw *hw = dev->hw_dev;
	u32 i, n_pages = PAGE_ALIGN(buf->size) >> PAGE_SHIFT;
	struct page *page = NULL, **pages = NULL;
	struct sg_table *sg = NULL;
	int ret = -ENOMEM;

	page = alloc_pages(GFP_KERNEL | GFP_DMA32, 0);
	if (!page)
		goto err;

	pages = kvmalloc_array(n_pages, sizeof(struct page *), GFP_KERNEL);
	if (!pages)
		goto free_page;
	for (i = 0; i < n_pages; i++)
		pages[i] = page;

	sg = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!sg)
		goto free_pages;
	ret = sg_alloc_table_from_pages(sg, pages, n_pages, 0,
					n_pages << PAGE_SHIFT, GFP_KERNEL);
	if (ret)
		goto free_sg;

	ret = dma_map_sg(hw->dev, sg->sgl, sg->nents, DMA_BIDIRECTIONAL);
	buf->dma_addr = sg_dma_address(sg->sgl);
	buf->mem_priv = sg;
	buf->pages = pages;
	v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
		 "%s buf:0x%x map cnt:%d size:%d\n", __func__,
		 (u32)buf->dma_addr, ret, buf->size);
	return 0;
free_sg:
	kfree(sg);
free_pages:
	kvfree(pages);
free_page:
	__free_pages(page, 0);
err:
	return ret;
}

static void es_dvp2axi_free_page_dummy_buf(struct es_dvp2axi_device *dev, struct es_dvp2axi_dummy_buffer *buf)
{
	struct sg_table *sg = buf->mem_priv;

	if (!sg)
		return;
	dma_unmap_sg(dev->hw_dev->dev, sg->sgl, sg->nents, DMA_BIDIRECTIONAL);
	sg_free_table(sg);
	kfree(sg);
	__free_pages(buf->pages[0], 0);
	kvfree(buf->pages);
	buf->mem_priv = NULL;
	buf->pages = NULL;
}

int es_dvp2axi_alloc_common_dummy_buf(struct es_dvp2axi_device *dev, struct es_dvp2axi_dummy_buffer *buf)
{
	struct es_dvp2axi_hw *hw = dev->hw_dev;
	int ret = 0;

	mutex_lock(&hw->dev_lock);
	if (buf->mem_priv)
		goto end;

	if (buf->size == 0)
		goto end;

	if (hw->iommu_en) {
		ret = es_dvp2axi_alloc_page_dummy_buf(dev, buf);
		goto end;
	}

	ret = es_dvp2axi_alloc_buffer(dev, buf);
	if (!ret)
		v4l2_dbg(1, es_dvp2axi_debug, &dev->v4l2_dev,
			 "%s buf:0x%x size:%d\n", __func__,
			 (u32)buf->dma_addr, buf->size);
end:
	if (ret < 0)
		v4l2_err(&dev->v4l2_dev, "%s failed:%d\n", __func__, ret);
	mutex_unlock(&hw->dev_lock);
	return ret;
}

void es_dvp2axi_free_common_dummy_buf(struct es_dvp2axi_device *dev, struct es_dvp2axi_dummy_buffer *buf)
{
	struct es_dvp2axi_hw *hw = dev->hw_dev;

	mutex_lock(&hw->dev_lock);

	if (hw->iommu_en)
		es_dvp2axi_free_page_dummy_buf(dev, buf);
	else
		es_dvp2axi_free_buffer(dev, buf);
	mutex_unlock(&hw->dev_lock);
}

struct es_dvp2axi_shm_data {
	void *vaddr;
	int vmap_cnt;
	int npages;
	struct page *pages[];
};

static struct sg_table *es_dvp2axi_shm_map_dma_buf(struct dma_buf_attachment *attachment,
					enum dma_data_direction dir)
{
	struct es_dvp2axi_shm_data *data = attachment->dmabuf->priv;
	struct sg_table *table;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);

	sg_alloc_table_from_pages(table, data->pages, data->npages, 0,
				  data->npages << PAGE_SHIFT, GFP_KERNEL);

	dma_map_sgtable(attachment->dev, table, dir, DMA_ATTR_SKIP_CPU_SYNC);

	return table;
}

static void es_dvp2axi_shm_unmap_dma_buf(struct dma_buf_attachment *attachment,
			      struct sg_table *table,
			      enum dma_data_direction dir)
{
	dma_unmap_sgtable(attachment->dev, table, dir, DMA_ATTR_SKIP_CPU_SYNC);
	sg_free_table(table);
	kfree(table);
}

static int es_dvp2axi_shm_vmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	struct es_dvp2axi_shm_data *data = dma_buf->priv;

	data->vaddr = vmap(data->pages, data->npages, VM_MAP, PAGE_KERNEL);
	data->vmap_cnt++;
	iosys_map_set_vaddr(map, data->vaddr);
	return 0;
}

static void es_dvp2axi_shm_vunmap(struct dma_buf *dma_buf, struct iosys_map *map)
{
	struct es_dvp2axi_shm_data *data = dma_buf->priv;

	vunmap(data->vaddr);
	data->vaddr = NULL;
	data->vmap_cnt--;
}

static int es_dvp2axi_shm_mmap(struct dma_buf *dma_buf, struct vm_area_struct *vma)
{
	struct es_dvp2axi_shm_data *data = dma_buf->priv;
	unsigned long vm_start = vma->vm_start;
	int i;

	for (i = 0; i < data->npages; i++) {
		remap_pfn_range(vma, vm_start, page_to_pfn(data->pages[i]),
				PAGE_SIZE, vma->vm_page_prot);
		vm_start += PAGE_SIZE;
	}

	return 0;
}

static int es_dvp2axi_shm_begin_cpu_access(struct dma_buf *dmabuf, enum dma_data_direction dir)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *table;

	attachment = list_first_entry(&dmabuf->attachments, struct dma_buf_attachment, node);
	table = attachment->priv;
	dma_sync_sg_for_cpu(NULL, table->sgl, table->nents, dir);

	return 0;
}

static int es_dvp2axi_shm_end_cpu_access(struct dma_buf *dmabuf, enum dma_data_direction dir)
{
	struct dma_buf_attachment *attachment;
	struct sg_table *table;

	attachment = list_first_entry(&dmabuf->attachments, struct dma_buf_attachment, node);
	table = attachment->priv;
	dma_sync_sg_for_device(NULL, table->sgl, table->nents, dir);

	return 0;
}

static void es_dvp2axi_shm_release(struct dma_buf *dma_buf)
{
	struct es_dvp2axi_shm_data *data = dma_buf->priv;

	if (data->vmap_cnt) {
		WARN(1, "%s: buffer still mapped in the kernel\n", __func__);
		es_dvp2axi_shm_vunmap(dma_buf, NULL);
	}
	kfree(data);
}

static const struct dma_buf_ops es_dvp2axi_shm_dmabuf_ops = {
	.map_dma_buf = es_dvp2axi_shm_map_dma_buf,
	.unmap_dma_buf = es_dvp2axi_shm_unmap_dma_buf,
	.release = es_dvp2axi_shm_release,
	.mmap = es_dvp2axi_shm_mmap,
	.vmap = es_dvp2axi_shm_vmap,
	.vunmap = es_dvp2axi_shm_vunmap,
	.begin_cpu_access = es_dvp2axi_shm_begin_cpu_access,
	.end_cpu_access = es_dvp2axi_shm_end_cpu_access,
};

static struct dma_buf *es_dvp2axi_shm_alloc(struct esisp_thunderboot_shmem *shmem)
{
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	struct es_dvp2axi_shm_data *data;
	int i, npages;

	npages = PAGE_ALIGN(shmem->shm_size) / PAGE_SIZE;
	data = kmalloc(sizeof(*data) + npages * sizeof(struct page *), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);
	data->vmap_cnt = 0;
	data->npages = npages;
	for (i = 0; i < npages; i++)
		data->pages[i] = phys_to_page(shmem->shm_start + i * PAGE_SIZE);

	exp_info.ops = &es_dvp2axi_shm_dmabuf_ops;
	exp_info.size = npages * PAGE_SIZE;
	exp_info.flags = O_RDWR;
	exp_info.priv = data;

	dmabuf = dma_buf_export(&exp_info);

	return dmabuf;
}

int es_dvp2axi_alloc_reserved_mem_buf(struct es_dvp2axi_device *dev, struct es_dvp2axi_rx_buffer *buf)
{
	struct es_dvp2axi_dummy_buffer *dummy = &buf->dummy;

	dummy->dma_addr = dev->resmem_pa + dummy->size * buf->buf_idx;
	if (dummy->dma_addr + dummy->size > dev->resmem_pa + dev->resmem_size)
		return -EINVAL;
	buf->dbufs.dma = dummy->dma_addr;
	buf->dbufs.is_resmem = true;
	buf->shmem.shm_start = dummy->dma_addr;
	buf->shmem.shm_size = dummy->size;
	dummy->dbuf = es_dvp2axi_shm_alloc(&buf->shmem);
	if (dummy->is_need_vaddr) {
		struct iosys_map map;

		dummy->dbuf->ops->vmap(dummy->dbuf, &map);
		dummy->vaddr = map.vaddr;
	}
	return 0;
}

void es_dvp2axi_free_reserved_mem_buf(struct es_dvp2axi_device *dev, struct es_dvp2axi_rx_buffer *buf)
{
	struct es_dvp2axi_dummy_buffer *dummy = &buf->dummy;
	struct media_pad *pad = NULL;
	struct v4l2_subdev *sd;

	if (buf->dummy.is_free)
		return;

	if (dev->rdbk_debug)
		v4l2_info(&dev->v4l2_dev,
			  "free reserved mem addr 0x%x\n",
			  (u32)dummy->dma_addr);
	if (dev->sditf[0]) {
		if (dev->sditf[0]->is_combine_mode)
			pad = media_pad_remote_pad_first(&dev->sditf[0]->pads[1]);
		else
			pad = media_pad_remote_pad_first(&dev->sditf[0]->pads[0]);
	} else {
		v4l2_info(&dev->v4l2_dev,
			  "not find sditf\n");
		return;
	}
	if (pad) {
		sd = media_entity_to_v4l2_subdev(pad->entity);
	} else {
		v4l2_info(&dev->v4l2_dev,
			  "not find remote pad\n");
		return;
	}
	if (buf->dbufs.is_init)
		v4l2_subdev_call(sd, core, ioctl,
				 ESISP_VICAP_CMD_RX_BUFFER_FREE, &buf->dbufs);
	if (dummy->is_need_vaddr)
		dummy->dbuf->ops->vunmap(dummy->dbuf, NULL);
	buf->dummy.is_free = true;
}

