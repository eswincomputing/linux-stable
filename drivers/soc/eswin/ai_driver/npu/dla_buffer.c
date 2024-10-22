// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe root complex driver
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
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

#include "dla_driver.h"
#include "dla_engine.h"
#include "dla_log.h"
#include "dla_buffer.h"
#include "hetero_ioctl.h"
#include "internal_interface.h"

struct dla_buffer_object *dla_alloc_dmabuf(size_t size,
					   enum es_malloc_policy policy)
{
	struct dla_buffer_object *bobj = NULL;
	int ret = 0;

	bobj = kzalloc(sizeof(struct dla_buffer_object), GFP_KERNEL);
	if (bobj == NULL) {
		dla_error("err:malloc dla_buffer_object failed!\n");
		return NULL;
	}

	ret = dev_mem_alloc(size, policy, &bobj->dmabuf);
	if (ret < 0) {
		dla_error("err:dev_mem_alloc failed!\n");
		kfree(bobj);
		return NULL;
	}

	bobj->fd = ret;
	bobj->size = size;

	return bobj;
}

int dla_attach_dmabuf(struct dla_buffer_object *bobj, struct device *attach_dev)
{
	dma_addr_t dma_addr = 0;

	if (!bobj) {
		dla_error("err:bobj is NULL!\n");
		return -EINVAL;
	}

	if (!attach_dev) {
		dla_error("err:attach_dev is NULL!\n");
		return -EINVAL;
	}

	if (!bobj->dmabuf) {
		dla_error("err:bobj->dmabuf is NULL!\n");
		return -EINVAL;
	}

	dma_addr = dev_mem_attach(bobj->dmabuf, attach_dev, DMA_BIDIRECTIONAL,
				  &bobj->attach);
	if (dma_addr == 0) {
		dla_error("err:dev_mem_attach failed!\n");
		return -ENOMEM;
	}

	bobj->dma_addr = dma_addr;

	return 0;
}

int dla_detach_dmabuf(struct dla_buffer_object *bobj)
{
	if (!bobj) {
		dla_error("err:bobj is NULL!\n");
		return -EINVAL;
	}

	if (!bobj->attach) {
		dla_error("err:bobj->attach is NULL!\n");
		return -EINVAL;
	}

	dev_mem_detach(bobj->attach, DMA_BIDIRECTIONAL);
	bobj->attach = NULL;
	bobj->sg = NULL;

	return 0;
}

void *dla_dmabuf_vmap(struct dla_buffer_object *bobj)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	struct iosys_map map;
#else
	struct dma_buf_map map;
#endif
	int ret;

	if (!bobj) {
		dla_error("err:bobj is NULL!\n");
		return NULL;
	}

	if (bobj->vaddr)
		return bobj->vaddr;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	ret = dma_buf_vmap_unlocked(bobj->dmabuf, &map);
#else
	ret = dma_buf_vmap(bobj->dmabuf, &map);
#endif

	if (ret)
		return NULL;

	WARN_ON_ONCE(map.is_iomem);
	bobj->vaddr = map.vaddr;

	return bobj->vaddr;
}

void dla_dmabuf_vunmap(struct dla_buffer_object *bobj)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	struct iosys_map map;
#else
	struct dma_buf_map map;
#endif

	WARN_ON(!bobj);
	if (bobj && bobj->vaddr) {
		map.vaddr = bobj->vaddr;
		map.is_iomem = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		dma_buf_vunmap_unlocked(bobj->dmabuf, &map);
#else
		dma_buf_vunmap(bobj->dmabuf, &map);
#endif
		bobj->vaddr = NULL;
	}
}

void dla_release_bobj(struct dla_buffer_object *bobj)
{
	if (!bobj) {
		dla_error("err:bobj is NULL!\n");
		return;
	}

	if (bobj->vaddr) {
		dla_dmabuf_vunmap(bobj);
	}

	if (bobj->attach) {
		dla_detach_dmabuf(bobj);
	}

	if (bobj->dmabuf) {
		dev_mem_free(bobj->dmabuf);
	}

	kfree(bobj);
	bobj = NULL;
}

struct dla_buffer_object *dla_import_fd_to_device(int fd,
						  struct device *attach_dev)
{
	struct dma_buf *dma_buf;
	struct dla_buffer_object *obj;
	int ret;

	/* get dmabuf handle */
	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf))
		return ERR_CAST(dma_buf);

	obj = kzalloc(sizeof(struct dla_buffer_object), GFP_KERNEL);
	if (obj == NULL) {
		dla_error("err:malloc dla_buffer_object failed!\n");
		ret = -ENOMEM;
		goto fail;
	}

	obj->dmabuf = dma_buf;
	obj->fd = fd;
	obj->size = dma_buf->size;

	ret = dla_attach_dmabuf(obj, attach_dev);
	if (ret < 0) {
		dla_error("err:dla_attach_dmabuf failed!\n");
		goto fail;
	}

	return obj;

fail:
	if (obj) {
		kfree(obj);
	}
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}

int dla_mapdma_buf_to_dev(int fd, struct dla_buffer_object *obj,
			  struct device *attach_dev)
{
	struct dma_buf *dma_buf;
	int ret;

	if (obj == NULL) {
		dla_error("param obj is null, failed!\n");
		return -EINVAL;
	}
	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf)) {
		return -EIO;
	}
	obj->dmabuf = dma_buf;
	obj->fd = fd;
	obj->size = dma_buf->size;

	ret = dla_attach_dmabuf(obj, attach_dev);
	if (ret < 0) {
		dla_error("err:dla_attach_dmabuf failed!\n");
		goto fail;
	}
	return 0;
fail:
	dma_buf_put(dma_buf);
	return ret;
}

void dla_unmapdma_buf_to_dev(struct dla_buffer_object *bobj)
{
	if (!bobj) {
		dla_error("err:bobj is NULL!\n");
		return;
	}

	if (bobj->vaddr) {
		dla_dmabuf_vunmap(bobj);
	}

	if (bobj->attach) {
		dla_detach_dmabuf(bobj);
	}

	if (bobj->dmabuf) {
		dev_mem_free(bobj->dmabuf);
	}
}

int dla_import_dmabuf_from_model(struct user_model *model)
{
	addrDesc_t *address;
	int num_address;
	struct dla_buffer_object *bobjs;
	struct nvdla_device *nvdla_dev;
	struct dma_buf *dma_buf;
	int ret = 0;
	int i = 0;

	num_address = model->mem_handles.addrlist->numAddress;
	address = model->mem_handles.addrlist->addrDesc;
	nvdla_dev = model->nvdla_dev;

	dla_detail("num_address=%d\n", num_address);
	bobjs = kzalloc(num_address * sizeof(struct dla_buffer_object),
			GFP_KERNEL);
	if (bobjs == NULL) {
		dla_error("err:bobjs is NULL,no memory!\n");
		return -ENOMEM;
	}

	for (i = 0; i < num_address; i++) {
		bobjs[i].fd = address[i].devBuf.memFd;
		dla_detail("i=%d bobjs[i].fd=%d\n", i, bobjs[i].fd);
		if (bobjs[i].fd <= 0) {
			continue;
		}

		dma_buf = dma_buf_get(bobjs[i].fd);
		if (IS_ERR(dma_buf)) {
			ret = PTR_ERR(dma_buf);
			dla_error("err:dma_buf_get failed!i:%d fd:%d, ret:%d\n",
				  i, bobjs[i].fd, ret);
			goto fail;
		}

		dla_detail("i=%d bobjs[i].fd=%d,dma_buf=0x%px counter=%lld\n",
			   i, bobjs[i].fd, dma_buf,
			   dma_buf->file->f_count.counter);
		bobjs[i].dmabuf = dma_buf;
		ret = dla_attach_dmabuf(&bobjs[i], &nvdla_dev->pdev->dev);
		if (ret < 0) {
			dla_error("err:dla_attach_dmabuf failed!\n");
			dma_buf_put(dma_buf);
			bobjs[i].fd = -1;
			goto fail;
		}
	}

	model->mem_handles.bobjs = bobjs;

	return ret;
fail:

	for (i = 0; i < num_address; i++) {
		if (bobjs[i].fd > 0) {
			dla_detach_dmabuf(&bobjs[i]);
			if (bobjs[i].dmabuf) {
				dma_buf_put(bobjs[i].dmabuf);
			}
		}
	}

	kfree(bobjs);

	return ret;
}

int dla_detach_dmabuf_from_model(struct user_model *model)
{
	int num_address;
	struct dla_buffer_object *bobjs;
	int ret = 0;
	int i = 0;

	num_address = model->mem_handles.addrlist->numAddress;
	dla_detail("num_address=%d\n", num_address);

	bobjs = model->mem_handles.bobjs;

	for (i = 0; i < num_address; i++) {
		dla_detail("i=%d bobjs[i].fd=%d\n", i, bobjs[i].fd);
		if (bobjs[i].fd > 0) {
			dla_detach_dmabuf(&bobjs[i]);
			if (bobjs[i].dmabuf) {
				dma_buf_put(bobjs[i].dmabuf);
			}
		}
	}

	kfree(bobjs);

	return ret;
}

int dla_dmabuf_vmap_for_model(struct user_model *model)
{
	int num_address;
	struct dla_buffer_object *bobjs;
	int ret = 0;
	int i = 0;

	num_address = model->mem_handles.addrlist->numAddress;
	bobjs = model->mem_handles.bobjs;

	for (i = 0; i < num_address; i++) {
		dla_detail("i=%d bobjs[i].fd=%d\n", i, bobjs[i].fd);
		if (bobjs[i].fd <= 0) {
			continue;
		}
		if (dla_dmabuf_vmap(&bobjs[i]) == NULL) {
			dla_error("err:i=%d bobjs[i].fd=%d vmap failed\n", i,
				  bobjs[i].fd);
			ret = -1;
			goto fail;
		}
	}

	return ret;
fail:

	for (i = 0; i < num_address; i++) {
		if (bobjs[i].fd > 0) {
			if (bobjs[i].vaddr) {
				dla_dmabuf_vunmap(&bobjs[i]);
			}
		}
	}

	return ret;
}

void dla_dmabuf_vunmap_for_model(struct user_model *model)
{
	int num_address;
	struct dla_buffer_object *bobjs;
	int i = 0;

	num_address = model->mem_handles.addrlist->numAddress;
	bobjs = model->mem_handles.bobjs;

	for (i = 0; i < num_address; i++) {
		if (bobjs[i].fd > 0 && bobjs[i].vaddr != NULL) {
			dla_dmabuf_vunmap(&bobjs[i]);
		}
	}
}
