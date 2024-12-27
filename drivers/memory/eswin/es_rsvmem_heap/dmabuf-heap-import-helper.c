// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DMABUF heap helper APIs
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Min Lin <linmin@eswincomputing.com>
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/fcntl.h>
#include <linux/scatterlist.h>
#include <linux/miscdevice.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-heap.h>
#include <linux/dmabuf-heap-import-helper.h>

static struct device *split_dmabuf_dev;

struct drm_prime_member {
	struct dma_buf *dma_buf;
	uint64_t handle;

	struct rb_node dmabuf_rb;
	struct rb_node handle_rb;
};

struct dma_heap_attachment {
	struct device *dev;
	struct sg_table *table;
	struct list_head list;
	bool mapped;
};

struct dma_heap_attachment_cma {
	struct device *dev;
	struct sg_table table;
	struct list_head list;
	bool mapped;
};

static int dmabuf_heap_add_buf_handle(struct dmaheap_file_private *prime_fpriv,
				    struct dma_buf *dma_buf, uint64_t handle)
{
	struct drm_prime_member *member;
	struct rb_node **p, *rb;

	member = kmalloc(sizeof(*member), GFP_KERNEL);
	if (!member)
		return -ENOMEM;

	get_dma_buf(dma_buf);
	member->dma_buf = dma_buf;
	member->handle = handle;

	rb = NULL;
	p = &prime_fpriv->dmabufs.rb_node;
	while (*p) {
		struct drm_prime_member *pos;

		rb = *p;
		pos = rb_entry(rb, struct drm_prime_member, dmabuf_rb);
		if (dma_buf > pos->dma_buf)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
	}
	rb_link_node(&member->dmabuf_rb, rb, p);
	rb_insert_color(&member->dmabuf_rb, &prime_fpriv->dmabufs);

	rb = NULL;
	p = &prime_fpriv->handles.rb_node;
	while (*p) {
		struct drm_prime_member *pos;

		rb = *p;
		pos = rb_entry(rb, struct drm_prime_member, handle_rb);
		if (handle > pos->handle)
			p = &rb->rb_right;
		else
			p = &rb->rb_left;
	}
	rb_link_node(&member->handle_rb, rb, p);
	rb_insert_color(&member->handle_rb, &prime_fpriv->handles);

	return 0;
}

static int dmabuf_heap_lookup_buf_handle(struct dmaheap_file_private *prime_fpriv,
				       struct dma_buf *dma_buf,
				       uint64_t *handle)
{
	struct rb_node *rb;

	rb = prime_fpriv->dmabufs.rb_node;
	while (rb) {
		struct drm_prime_member *member;

		member = rb_entry(rb, struct drm_prime_member, dmabuf_rb);
		if (member->dma_buf == dma_buf) {
			*handle = member->handle;
			return 0;
		} else if (member->dma_buf < dma_buf) {
			rb = rb->rb_right;
		} else {
			rb = rb->rb_left;
		}
	}

	return -ENOENT;
}

static void _dmabuf_heap_remove_buf_handle(struct dmaheap_file_private *prime_fpriv,
					struct dma_buf *dma_buf)
{
	struct rb_node *rb;

	rb = prime_fpriv->dmabufs.rb_node;
	while (rb) {
		struct drm_prime_member *member;

		member = rb_entry(rb, struct drm_prime_member, dmabuf_rb);
		if (member->dma_buf == dma_buf) {
			rb_erase(&member->handle_rb, &prime_fpriv->handles);
			rb_erase(&member->dmabuf_rb, &prime_fpriv->dmabufs);

			dma_buf_put(dma_buf);
			kfree(member);
			return;
		} else if (member->dma_buf < dma_buf) {
			rb = rb->rb_right;
		} else {
			rb = rb->rb_left;
		}
	}
}

void common_dmabuf_heap_import_init(struct heap_root *root, struct device *dev)
{
	memset(root, 0, sizeof(*root));

	mutex_init(&root->lock);
	INIT_LIST_HEAD(&root->header);

	root->dev = dev;
}
EXPORT_SYMBOL(common_dmabuf_heap_import_init);

void common_dmabuf_heap_import_uninit(struct heap_root *root)
{
	struct heap_mem *h, *tmp;

	list_for_each_entry_safe(h, tmp, &root->header, list) {
		common_dmabuf_heap_release(h);
	}
}
EXPORT_SYMBOL(common_dmabuf_heap_import_uninit);

static struct heap_mem *dmabuf_heap_import(struct heap_root *root, int fd)
{
    struct dma_buf *dma_buf;
    struct dma_buf_attachment *attach;
    struct sg_table *sgt;

    uint64_t handle;
    struct heap_mem *heap_obj;
    int ret;

    /* get dmabuf handle */
    dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf))
		return ERR_CAST(dma_buf);

    mutex_lock(&root->lock);

    ret = dmabuf_heap_lookup_buf_handle(&root->fp, dma_buf, &handle);
    if (ret == 0) {
        heap_obj = (struct heap_mem *)handle;
        dma_buf_put(dma_buf);
        kref_get(&heap_obj->refcount);
        mutex_unlock(&root->lock);
	return heap_obj;
    }

    heap_obj = kzalloc(sizeof(*heap_obj), GFP_KERNEL);
    if (!heap_obj) {
        mutex_unlock(&root->lock);
        dma_buf_put(dma_buf);
        return ERR_PTR(-ENOMEM);
    }

    attach = dma_buf_attach(dma_buf, root->dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
        goto clean_up;
    }

    sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

    heap_obj->dbuf_fd = fd;
    heap_obj->dbuf = dma_buf;

    heap_obj->import_attach = attach;
    heap_obj->sgt = sgt;

	heap_obj->root = root;
	heap_obj->vaddr = NULL;
	heap_obj->dir = DMA_BIDIRECTIONAL;

	/* get_dma_buf was called in dmabuf_heap_add_buf_handle()*/
    ret = dmabuf_heap_add_buf_handle(&root->fp, dma_buf, (uint64_t)heap_obj);
    if (ret) {
        goto fail_add_handle;
    }

    kref_init(&heap_obj->refcount);

    list_add(&heap_obj->list, &root->header);

    mutex_unlock(&root->lock);

    dma_buf_put(dma_buf);

    return heap_obj;

fail_add_handle:
fail_detach:
    dma_buf_detach(dma_buf, attach);
clean_up:
    kfree(heap_obj);
	mutex_unlock(&root->lock);
    dma_buf_put(dma_buf);

    return ERR_PTR(ret);
}

static struct heap_mem *dmabuf_heap_import_with_dma_buf_st(struct heap_root *root, struct dma_buf *dma_buf)
{
    struct dma_buf_attachment *attach;
    struct sg_table *sgt;

    uint64_t handle;
    struct heap_mem *heap_obj;
    int ret;

    mutex_lock(&root->lock);

    ret = dmabuf_heap_lookup_buf_handle(&root->fp, dma_buf, &handle);
	if (ret == 0) {
        heap_obj = (struct heap_mem *)handle;
        kref_get(&heap_obj->refcount);
        mutex_unlock(&root->lock);
	return heap_obj;
    }

    heap_obj = kzalloc(sizeof(*heap_obj), GFP_KERNEL);
    if (!heap_obj) {
        mutex_unlock(&root->lock);
        return ERR_PTR(-ENOMEM);
    }

    attach = dma_buf_attach(dma_buf, root->dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
        goto clean_up;
    }

    sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto fail_detach;
	}

    heap_obj->dbuf_fd = -1;
    heap_obj->dbuf = dma_buf;

    heap_obj->import_attach = attach;
    heap_obj->sgt = sgt;

	heap_obj->root = root;
	heap_obj->vaddr = NULL;
	heap_obj->dir = DMA_BIDIRECTIONAL;

	/* get_dma_buf was called in dmabuf_heap_add_buf_handle()*/
    ret = dmabuf_heap_add_buf_handle(&root->fp, dma_buf, (uint64_t)heap_obj);
    if (ret) {
        goto fail_add_handle;
    }

    kref_init(&heap_obj->refcount);

    list_add(&heap_obj->list, &root->header);

    mutex_unlock(&root->lock);

    return heap_obj;

fail_add_handle:
fail_detach:
    dma_buf_detach(dma_buf, attach);
clean_up:
    kfree(heap_obj);
    mutex_unlock(&root->lock);

    return ERR_PTR(ret);
}

struct heap_mem *common_dmabuf_lookup_heapobj_by_fd(struct heap_root *root, int fd)
{
	int ret = 0;
	struct dma_buf *dma_buf;
	struct heap_mem *heap_obj;

	/* get dmabuf handle */
	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf))
		return NULL;

	mutex_lock(&root->lock);
	ret = dmabuf_heap_lookup_buf_handle(&root->fp, dma_buf, (uint64_t *)&heap_obj);
	mutex_unlock(&root->lock);

	dma_buf_put(dma_buf);
	if (0 == ret)
		return heap_obj;
	else
		return NULL;
}
EXPORT_SYMBOL(common_dmabuf_lookup_heapobj_by_fd);

struct heap_mem *common_dmabuf_lookup_heapobj_by_dma_buf_st(struct heap_root *root, struct dma_buf *dma_buf)
{
	int ret = 0;
	struct heap_mem *heap_obj;

	pr_debug("%s:dma_buf=0x%px, file_count=%ld\n",
		__func__, dma_buf, file_count(dma_buf->file));
	mutex_lock(&root->lock);
	ret = dmabuf_heap_lookup_buf_handle(&root->fp, dma_buf, (uint64_t *)&heap_obj);
	mutex_unlock(&root->lock);

	if (0 == ret)
		return heap_obj;
	else
		return NULL;
}
EXPORT_SYMBOL(common_dmabuf_lookup_heapobj_by_dma_buf_st);

struct heap_mem *common_dmabuf_heap_import_from_user(struct heap_root *root, int fd)
{
	return dmabuf_heap_import(root, fd);
}
EXPORT_SYMBOL(common_dmabuf_heap_import_from_user);

struct heap_mem *common_dmabuf_heap_import_from_user_with_dma_buf_st(struct heap_root *root, struct dma_buf *dma_buf)
{
	return dmabuf_heap_import_with_dma_buf_st(root, dma_buf);
}
EXPORT_SYMBOL(common_dmabuf_heap_import_from_user_with_dma_buf_st);

static void __common_dmabuf_heap_release(struct kref *kref)
{
	struct heap_root *root;
	struct heap_mem *heap_obj = container_of(kref, struct heap_mem, refcount);

	WARN_ON(!heap_obj);
	if (!heap_obj)
		return;

	root = heap_obj->root;
	WARN_ON(!mutex_is_locked(&root->lock));
	list_del(&heap_obj->list);

	common_dmabuf_heap_umap_vaddr(heap_obj);

	dma_buf_unmap_attachment(heap_obj->import_attach, heap_obj->sgt, heap_obj->dir);

	dma_buf_detach(heap_obj->dbuf, heap_obj->import_attach);

	/* dma_buf_put was called in _dmabuf_heap_remove_buf_handle()*/
	_dmabuf_heap_remove_buf_handle(&root->fp, heap_obj->dbuf);

	kfree(heap_obj);
}

void common_dmabuf_heap_release(struct heap_mem *heap_obj)
{
	struct heap_root *root = heap_obj->root;

	mutex_lock(&root->lock);
	kref_put(&heap_obj->refcount, __common_dmabuf_heap_release);
	mutex_unlock(&root->lock);
}
EXPORT_SYMBOL(common_dmabuf_heap_release);

void *common_dmabuf_heap_map_vaddr(struct heap_mem *heap_obj)
{
    struct dma_buf_map map;
    int ret;

	WARN_ON(!heap_obj);
	if (!heap_obj)
		return NULL;

    if (heap_obj->vaddr)
        return heap_obj->vaddr;

    ret = dma_buf_vmap(heap_obj->dbuf, &map);
    if (ret)
        return NULL;

    WARN_ON_ONCE(map.is_iomem);
    heap_obj->vaddr = map.vaddr;

    return heap_obj->vaddr;
}
EXPORT_SYMBOL(common_dmabuf_heap_map_vaddr);

void common_dmabuf_heap_umap_vaddr(struct heap_mem *heap_obj)
{
	struct dma_buf_map map;

	WARN_ON(!heap_obj);
	if (heap_obj && heap_obj->vaddr) {
        map.vaddr = heap_obj->vaddr;
        map.is_iomem = 0;
        dma_buf_vunmap(heap_obj->dbuf, &map);
        heap_obj->vaddr = NULL;
    }
}
EXPORT_SYMBOL(common_dmabuf_heap_umap_vaddr);

struct heap_mem *
common_dmabuf_heap_import_from_kernel(struct heap_root *root, char *name, size_t len, unsigned int fd_flags)
{
	int dbuf_fd;

	dbuf_fd = eswin_heap_kalloc(name, len, O_RDWR | fd_flags, 0);
	if (dbuf_fd < 0) {
        return ERR_PTR(dbuf_fd);
	}

	return dmabuf_heap_import(root, dbuf_fd);
}
EXPORT_SYMBOL(common_dmabuf_heap_import_from_kernel);

struct heap_mem *common_dmabuf_heap_rsv_iova_map(struct heap_root *root, int fd, dma_addr_t iova, size_t size)
{
	struct dma_buf *dma_buf;
	struct dma_buf_attachment *attach;
	struct dma_heap_attachment *a;

	uint64_t handle;
	struct heap_mem *heap_obj;
	ssize_t ret = 0;

	/* get dmabuf handle */
	dma_buf = dma_buf_get(fd);
	if (IS_ERR(dma_buf)) {
		return ERR_CAST(dma_buf);
	}

	mutex_lock(&root->lock);
	dev_dbg(root->dev, "%s, fd=%d, iova=0x%llx, size=0x%lx\n", __func__, fd, iova, size);

	ret = dmabuf_heap_lookup_buf_handle(&root->fp, dma_buf, &handle);
	if (ret == 0) {
		heap_obj = (struct heap_mem *)handle;
		dma_buf_put(dma_buf);
		kref_get(&heap_obj->refcount);
		mutex_unlock(&root->lock);
		return heap_obj;
	}

	heap_obj = kzalloc(sizeof(*heap_obj), GFP_KERNEL);
	if (!heap_obj) {
		mutex_unlock(&root->lock);
		dma_buf_put(dma_buf);
		return ERR_PTR(-ENOMEM);
	}

	attach = dma_buf_attach(dma_buf, root->dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		goto clean_up;
	}

	a = (struct dma_heap_attachment *)attach->priv;
	ret = iommu_rsv_iova_map_sgt(root->dev, iova, a->table, 0, size);
	dma_buf_detach(dma_buf, attach);
	if (ret < 0)
		goto clean_up;

	heap_obj->dbuf_fd = fd;
	heap_obj->dbuf = dma_buf;
	heap_obj->root = root;
	heap_obj->vaddr = NULL;
	heap_obj->dir = DMA_BIDIRECTIONAL;
	heap_obj->iova = iova;
	heap_obj->size = size;

	/* get_dma_buf was called in dmabuf_heap_add_buf_handle()*/
	ret = dmabuf_heap_add_buf_handle(&root->fp, dma_buf, (uint64_t)heap_obj);
		if (ret) {
		goto fail_add_handle;
	}
	/* get_dma_buf was called in dmabuf_heap_add_buf_handle(), need to put back since
	 * we don't want to hold the dma_buf in this API
	*/
	dma_buf_put(dma_buf);

	kref_init(&heap_obj->refcount);

	list_add(&heap_obj->list, &root->header);

	mutex_unlock(&root->lock);

	dma_buf_put(dma_buf);

	return heap_obj;

fail_add_handle:
	iommu_unmap_rsv_iova(root->dev, 0, iova, size);
clean_up:
	kfree(heap_obj);
	mutex_unlock(&root->lock);
	dma_buf_put(dma_buf);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL(common_dmabuf_heap_rsv_iova_map);

static void __common_dmabuf_heap_rsv_iova_unmap(struct kref *kref)
{
	struct heap_root *root;
	struct heap_mem *heap_obj = container_of(kref, struct heap_mem, refcount);

	WARN_ON(!heap_obj);
	if (!heap_obj)
		return;

	root = heap_obj->root;
	WARN_ON(!mutex_is_locked(&root->lock));
	list_del(&heap_obj->list);


	iommu_unmap_rsv_iova(root->dev, 0, heap_obj->iova, heap_obj->size);
	/* dma_buf_put will be  called in _dmabuf_heap_remove_buf_handle(),
	 * so, call get_dma_buf first
	*/
	get_dma_buf(heap_obj->dbuf);
	_dmabuf_heap_remove_buf_handle(&root->fp, heap_obj->dbuf);
	kfree(heap_obj);
}

void common_dmabuf_heap_rsv_iova_unmap(struct heap_mem *heap_obj)
{
	struct heap_root *root = heap_obj->root;

	mutex_lock(&root->lock);
	kref_put(&heap_obj->refcount, __common_dmabuf_heap_rsv_iova_unmap);
	mutex_unlock(&root->lock);
}
EXPORT_SYMBOL(common_dmabuf_heap_rsv_iova_unmap);

void common_dmabuf_heap_rsv_iova_uninit(struct heap_root *root)
{
	struct heap_mem *h, *tmp;

	list_for_each_entry_safe(h, tmp, &root->header, list) {
		common_dmabuf_heap_rsv_iova_unmap(h);
	}
}
EXPORT_SYMBOL(common_dmabuf_heap_rsv_iova_uninit);


struct eswin_split_attachment {
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

// #define PRINT_ORIGINAL_SPLITTERS 1
static int eswin_split_heap_attach(struct dma_buf *dmabuf,
			   struct dma_buf_attachment *attachment)
{
	struct eswin_split_buffer *buffer = dmabuf->priv;
	struct eswin_split_attachment *a;
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

static void eswin_split_heap_detach(struct dma_buf *dmabuf,
			    struct dma_buf_attachment *attachment)
{
	struct eswin_split_buffer *buffer = dmabuf->priv;
	struct eswin_split_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(a->table);
	kfree(a->table);
	kfree(a);
}

static struct sg_table *eswin_split_map_dma_buf(struct dma_buf_attachment *attachment,
					     enum dma_data_direction direction)
{
	struct eswin_split_attachment *a = attachment->priv;
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

static void eswin_spilt_unmap_dma_buf(struct dma_buf_attachment *attachment,
				   struct sg_table *table,
				   enum dma_data_direction direction)
{
	struct eswin_split_attachment *a = attachment->priv;
	unsigned long attrs = DMA_ATTR_SKIP_CPU_SYNC;

	a->mapped = false;

	/* Skipt cache sync, since it takes a lot of time when unmap from device.
	*  It's the user's responsibility for guaranteeing the cache coherency after
	   the device has done processing the data.(For example, CPU do NOT read untill
	   the device has done)
	*/
	dma_unmap_sgtable(attachment->dev, table, direction, attrs);
}

static int eswin_split_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					     enum dma_data_direction direction)
{
	struct eswin_split_buffer *buffer = dmabuf->priv;
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
	for_each_sg(table->sgl, sg, table->orig_nents, i)
		arch_sync_dma_for_cpu(sg_phys(sg), sg->length, direction);

	mutex_unlock(&buffer->lock);

	return 0;
}

static int eswin_split_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					   enum dma_data_direction direction)
{
	struct eswin_split_buffer *buffer = dmabuf->priv;
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
	for_each_sg(table->sgl, sg, table->orig_nents, i)
		arch_sync_dma_for_device(sg_phys(sg), sg->length, direction);
	mutex_unlock(&buffer->lock);

	return 0;
}

static int eswin_split_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct eswin_split_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	unsigned long addr = vma->vm_start;
	unsigned long pgoff = (table->sgl->offset >> PAGE_SHIFT);
	// unsigned long pgoff = (buffer->slice.offset >> PAGE_SHIFT);
	unsigned long size_remaining = vma->vm_end - vma->vm_start;//vma_pages(vma);
	struct sg_page_iter piter;
	int ret;

	/* Mapping secure_memory with cached proprty to user space for CPU is NOT permitted */
	if (unlikely(!strncmp("secure_memory", dmabuf->exp_name, 13))) {
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
		vma->vm_page_prot = pgprot_dmacoherent(vma->vm_page_prot);
		/* skip sync cache, users should guarantee the cache is clean after done using it in
		   cached mode(i.e, ES_SYS_Mmap(SYS_CACHE_MODE_CACHED))
		*/
	}
	pr_debug("%s, vma->vm_start:0x%lx, vma->vm_end:0x%lx\n", __func__, vma->vm_start, vma->vm_end);
	pr_debug("%s, size_remaining:0x%lx, pgoff:0x%lx, vma->vm_pgoff:0x%lx, dmabuf->size:0x%lx, start_phys:0x%llx, pfn sg_page(sg):0x%lx,sg->length=0x%x\n",
		__func__, size_remaining, pgoff, vma->vm_pgoff, dmabuf->size, sg_phys(table->sgl),page_to_pfn(sg_page(table->sgl)), table->sgl->length);

	for_each_sgtable_page(table, &piter, pgoff) {
		struct page *page = sg_page_iter_page(&piter);
		// pr_debug("addr:0x%lx, page_to_pfn:0x%lx\n", addr, page_to_pfn(page));
		ret = remap_pfn_range(vma, addr, page_to_pfn(page), PAGE_SIZE,
				      vma->vm_page_prot);
		if (ret)
			return ret;
		addr += PAGE_SIZE;
		if (addr >= vma->vm_end)
			return 0;
	}

	return 0;
}

static void *eswin_split_heap_do_vmap(struct dma_buf *dmabuf)
{
	struct eswin_split_buffer *buffer = dmabuf->priv;
	pgprot_t prot = PAGE_KERNEL;
	struct sg_table *table = &buffer->sg_table;
	int npages = PAGE_ALIGN(buffer->slice.len) / PAGE_SIZE;
	struct page **pages = vmalloc(sizeof(struct page *) * npages);
	struct page **tmp = pages;
	struct sg_page_iter piter;
	void *vaddr;
	unsigned long pgoff = (table->sgl->offset >> PAGE_SHIFT);

	if (!pages)
		return ERR_PTR(-ENOMEM);

	for_each_sgtable_page(table, &piter, pgoff) {
		WARN_ON(tmp - pages >= npages);
		*tmp++ = sg_page_iter_page(&piter);
	}

	/* The property of this dmabuf in kernel space is determined by heap alloc with fd_flag. */
	if (buffer->fd_flags & O_DSYNC) {
		prot = pgprot_dmacoherent(PAGE_KERNEL);
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

static int eswin_split_heap_vmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct eswin_split_buffer *buffer = dmabuf->priv;
	void *vaddr;
	int ret = 0;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		dma_buf_map_set_vaddr(map, buffer->vaddr);
		goto out;
	}

	vaddr = eswin_split_heap_do_vmap(dmabuf);
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

static void eswin_split_heap_vunmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct eswin_split_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
	dma_buf_map_clear(map);
}

/* split parent dmabuf, and generate a new child sgt */
static int eswin_get_split_dmabuf(struct eswin_split_buffer *split_buffer)
{
	struct dma_buf *par_dma_buf = split_buffer->dmabuf;
	u64 offset = split_buffer->slice.offset;
	size_t size = split_buffer->slice.len;
	 struct sg_table *splitted_sgt = &split_buffer->sg_table;
	 struct sg_table *orig_splitted_sgt = &split_buffer->orig_sg_table;
	struct dma_buf_attachment *attach;
	struct dma_heap_attachment *a;
	struct dma_heap_attachment_cma *cma_a;
	struct sg_table *par_origin_table;
	int out_nents[1];
	struct scatterlist *sg;
	struct scatterlist *split_sg;
	size_t size_remaining, len;
	int i;
	int ret = 0;

	if (split_dmabuf_dev == NULL) {
		pr_err("split_dmabuf_dev has not been created!!!\n");
		return -EINVAL;
	}

	get_dma_buf(par_dma_buf);
	attach = dma_buf_attach(par_dma_buf, split_dmabuf_dev);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		dma_buf_put(par_dma_buf);
		return ret;
	}

	if (unlikely(!strcmp("linux,cma", par_dma_buf->exp_name)))
	{
		cma_a = (struct dma_heap_attachment_cma *)attach->priv;
		par_origin_table = &cma_a->table;
	}
	else {
		a = (struct dma_heap_attachment *)attach->priv;
		par_origin_table = a->table;
	}

	#ifdef PRINT_ORIGINAL_SPLITTERS
	{
		pr_debug("%s:parent[0x%px]:sgt->orig_nents=%d, nents=%d, sg_nents(in) %d\n",
			__func__, par_dma_buf,
			par_origin_table->orig_nents, par_origin_table->nents,
			sg_nents(par_origin_table->sgl));
		for_each_sg(par_origin_table->sgl, sg, par_origin_table->orig_nents, i) {
			pr_debug("parent[0x%px]:orig[%d]:sg->offset=0x%x ,sg->length=0x%x, sg_dma_len=0x%x, sg_phys=0x%lx\n",
				par_dma_buf, i, sg->offset, sg->length, sg_dma_len(sg), (unsigned long)sg_phys(sg));
		}
	}
	#endif

	/* split unmaped/original sgt of parent */
	ret = sg_split(par_origin_table->sgl, 0, offset, 1, &size,
					&split_sg, &out_nents[0], GFP_KERNEL);
	if (ret) {
		pr_err("Failed to split from parents's sgt\n");
	}

	orig_splitted_sgt->orig_nents = out_nents[0];
	orig_splitted_sgt->nents = out_nents[0];
	orig_splitted_sgt->sgl = split_sg;

	splitted_sgt->orig_nents = out_nents[0];
	splitted_sgt->nents = out_nents[0];
	/* skip the first sgl if it's length is 0 */
	if (split_sg->length == 0) {
		splitted_sgt->sgl = sg_next(split_sg);
		splitted_sgt->orig_nents--;
		splitted_sgt->nents--;
	}
	else {
		splitted_sgt->sgl = split_sg;
	}
	/* Re-format the splitted sg list in the actual slice len */
	pr_debug("%s:%d, split[0x%px]:out_nents[0]=%d, Re-format the splitted sgl.\n", __func__, __LINE__, par_dma_buf, out_nents[0]);
	size_remaining = size;

	for_each_sgtable_sg(splitted_sgt, sg, i) {
		len = min_t(size_t, size_remaining, sg->length);
		size_remaining -= len;
		if (size_remaining == 0) {
			sg->length = len;
			sg_mark_end(sg);
			pr_debug("split[0x%px]:orig[%d]:sg->offset=0x%x ,sg->length=0x%x, sg_dma_len=0x%x, sg_phys=0x%lx\n",
				par_dma_buf, i, sg->offset, sg->length, sg_dma_len(sg), (unsigned long)sg_phys(sg));
			break;
		}
			pr_debug("split[0x%px]:orig[%d]:sg->offset=0x%x ,sg->length=0x%x, sg_dma_len=0x%x, sg_phys=0x%lx\n",
				par_dma_buf, i, sg->offset, sg->length, sg_dma_len(sg), (unsigned long)sg_phys(sg));
	}
	splitted_sgt->orig_nents = splitted_sgt->nents = i + 1;

	pr_debug("%s:split[0x%px]reformatted, splitted:sgt->orig_nents=%d, nents=%d, out_nents[0]=%d\n",
		__func__, par_dma_buf, splitted_sgt->orig_nents, splitted_sgt->nents, out_nents[0]);
	/* parent's dupped sgt table is useless*/
	dma_buf_detach(par_dma_buf, attach);
	dma_buf_put(par_dma_buf);

	return ret;
}

static void eswin_put_split_dmabuf(struct sg_table *splitted_sgt)
{
	kfree(splitted_sgt->sgl);
}

static void eswin_split_dma_buf_release(struct dma_buf *dmabuf)
{
	struct eswin_split_buffer *split_buffer = dmabuf->priv;
	struct sg_table *table = &split_buffer->orig_sg_table;;

	pr_debug("%s %d\n", __func__, __LINE__);
	/* free splitted sgt->sgl which was allocated during esw_common_dmabuf_split_export()*/
	eswin_put_split_dmabuf(table);

	/* put parent's dmabuf which was got during esw_common_dmabuf_split_export()*/
	dma_buf_put(split_buffer->dmabuf);

	/* free split buffer which was allocated during esw_common_dmabuf_split_export()*/
	kfree(split_buffer);
}

static const struct dma_buf_ops eswin_common_buf_ops = {
	.attach = eswin_split_heap_attach,
	.detach = eswin_split_heap_detach,
	.map_dma_buf = eswin_split_map_dma_buf,
	.unmap_dma_buf = eswin_spilt_unmap_dma_buf,
	.begin_cpu_access = eswin_split_dma_buf_begin_cpu_access,
	.end_cpu_access = eswin_split_dma_buf_end_cpu_access,
	.mmap = eswin_split_mmap,
	.vmap = eswin_split_heap_vmap,
	.vunmap = eswin_split_heap_vunmap,
	.release = eswin_split_dma_buf_release,
};


int esw_common_dmabuf_split_export(int par_dmabuf_fd, unsigned int offset, size_t len, int fd_flags, char *name)
{
	struct eswin_split_buffer *split_buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct sg_table *table;
	int fd;
	struct dma_buf *dmabuf;
	int ret = 0;

	split_buffer = kzalloc(sizeof(*split_buffer), GFP_KERNEL);
	if (!split_buffer)
		return -ENOMEM;

	INIT_LIST_HEAD(&split_buffer->attachments);
	mutex_init(&split_buffer->lock);

	split_buffer->dbuf_fd = par_dmabuf_fd;
	split_buffer->fd_flags = fd_flags;
	split_buffer->slice.offset = offset;
	split_buffer->slice.len = len;
	snprintf(split_buffer->name, sizeof(split_buffer->name), "%s", name);

	split_buffer->dmabuf = dma_buf_get(split_buffer->dbuf_fd);
	if (IS_ERR(split_buffer->dmabuf)) {
		kfree(split_buffer);
		return PTR_ERR(split_buffer->dmabuf);
	}

	pr_debug("%s:%d, par_dbuf_fd %d, input slice: oft=0x%llx, len=0x%lx\n", __func__, __LINE__,
		split_buffer->dbuf_fd, split_buffer->slice.offset, split_buffer->slice.len);

	split_buffer->slice.offset = PAGE_ALIGN(split_buffer->slice.offset);
	split_buffer->slice.len = PAGE_ALIGN(split_buffer->slice.len);


	table = &split_buffer->sg_table;
	ret = eswin_get_split_dmabuf(split_buffer);
	if (ret) {
		/* put back parent's dmabuf*/
		dma_buf_put(split_buffer->dmabuf);
		/* free split buffer */
		kfree(split_buffer);
		return ret;
	}
	/* create the dmabuf */
	exp_info.exp_name = split_buffer->name;
	exp_info.ops = &eswin_common_buf_ops;
	exp_info.size = split_buffer->slice.len;
	exp_info.flags = O_RDWR | O_CLOEXEC;
	exp_info.priv = split_buffer;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		eswin_put_split_dmabuf(table);
		/* put back parent's dmabuf*/
		dma_buf_put(split_buffer->dmabuf);
		/* free split buffer */
		kfree(split_buffer);
		return PTR_ERR(dmabuf);
	}

	fd = dma_buf_fd(dmabuf, split_buffer->fd_flags);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		/* put the splitted dmabuf, the eswin_split_dma_buf_release will be called,
		   the parent dmabuf will be put and the split_buffer will be free at that time */
	}
	return fd;
}

EXPORT_SYMBOL(esw_common_dmabuf_split_export);

/* create a misc device for attaching the parent dmabuf to get the parent's original sgt*/
#define DRIVER_NAME 	"split_dmabuf"

static int split_dmabuf_open(struct inode *inode, struct file *file)
{

	pr_debug("%s:%d, success!\n", __func__, __LINE__);

	return 0;
}

static int split_dmabuf_release(struct inode *inode, struct file *file)
{
	pr_debug("%s:%d, success!\n", __func__, __LINE__);

	return 0;
}

static struct file_operations split_dmabuf_fops = {
	.owner        = THIS_MODULE,
	.llseek        = no_llseek,
	.open        = split_dmabuf_open,
	.release    = split_dmabuf_release,
};

static struct miscdevice split_dmabuf_miscdev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DRIVER_NAME,
	.fops	= &split_dmabuf_fops,
};

static int __init split_dmabuf_init(void)
{
	int ret = 0;
	struct device *dev;

	ret = misc_register(&split_dmabuf_miscdev);
	if(ret) {
		pr_err ("cannot register miscdev (err=%d)\n", ret);
		return ret;
	}
	split_dmabuf_dev = split_dmabuf_miscdev.this_device;
	dev = split_dmabuf_dev;
	if (!dev->dma_mask) {
		dev->dma_mask = &dev->coherent_dma_mask;
	}
	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(48));
	if (ret) {
		pr_err("Unable to set coherent mask\n");
		goto deregister_dev;
	}

	return 0;

deregister_dev:
	misc_deregister(&split_dmabuf_miscdev);
	split_dmabuf_dev = NULL;
	return ret;
}

static void __exit split_dmabuf_exit(void)
{
	misc_deregister(&split_dmabuf_miscdev);
}

module_init(split_dmabuf_init);
module_exit(split_dmabuf_exit);


static int vmf_replace_pages(struct vm_area_struct *vma, unsigned long addr,
				struct page **pages, unsigned long num, pgprot_t prot)
{
	struct mm_struct *const mm = vma->vm_mm;
	unsigned long remaining_pages_total = num;
	unsigned long pfn;
	pte_t *pte, entry;
	spinlock_t *ptl;
	unsigned long newprot_val = pgprot_val(prot);
	pgprot_t new_prot;
	u32 i = 0;

	while (remaining_pages_total) {
		pte = get_locked_pte(mm, addr, &ptl);
		if (!pte)
			return VM_FAULT_OOM;

		entry = ptep_get(pte);
		pfn = page_to_pfn(pages[i]);
		pr_debug("page_to_pfn(pages[%d])=0x%lx, pte_pfn(entry)=0x%lx, pte_val(entry)=0x%lx\n",
			i, pfn, pte_pfn(entry), pte_val(entry));

		newprot_val = (pte_val(entry) & (~_PAGE_PFN_MASK)) | newprot_val;
		if (newprot_val == (pte_val(entry) & (~_PAGE_PFN_MASK)))
			goto SKIP_PAGE;

		new_prot = __pgprot(newprot_val);
		entry = mk_pte(pages[i], new_prot);
		pr_debug("page_to_pfn(pages[%d])=0x%lx, pte_pfn(entry)=0x%lx, modified pte_val(entry)=0x%lx\n",
			i, page_to_pfn(pages[i]), pte_pfn(entry), pte_val(entry));
		set_pte_at(vma->vm_mm, addr, pte, entry);
		update_mmu_cache(vma, addr, pte);

SKIP_PAGE:
		addr += PAGE_SIZE;
		pte_unmap_unlock(pte, ptl);
		remaining_pages_total--;
		i++;
	}

	return 0;
}

static int zap_and_replace_pages(struct vm_area_struct *vma, u64 addr, size_t len, pgprot_t prot)
{
	struct page **pages;
	u32 offset;
	unsigned long nr_pages;
	u64 first, last;
	u64 addr_aligned = ALIGN_DOWN(addr, PAGE_SIZE);
	u32 i;
	int ret = -ENOMEM;

	if (!len) {
		pr_err("invalid userptr size.\n");
		return -EINVAL;
	}
	/* offset into first page */
	offset = offset_in_page(addr);

	/* Calculate number of pages */
	first = (addr & PAGE_MASK) >> PAGE_SHIFT;
	last  = ((addr + len - 1) & PAGE_MASK) >> PAGE_SHIFT;
	nr_pages = last - first + 1;
	pr_debug("%s:%d, addr=0x%llx(addr_aligned=0x%llx), len=0x%lx, nr_pages=0x%lx(fist:0x%llx,last:0x%llx)\n",
		__func__, __LINE__, addr, addr_aligned, len, nr_pages, first, last);

	/* alloc array to storing the pages */
	pages = kvmalloc_array(nr_pages,
				   sizeof(struct page *),
				   GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	ret = get_user_pages_fast(addr_aligned,
				nr_pages,
				FOLL_FORCE | FOLL_WRITE,
				pages);

	if (ret != nr_pages) {
		nr_pages = (ret >= 0) ? ret : 0;
		pr_err("get_user_pages_fast, err=%d [0x%lx]\n",
			ret, nr_pages);
		ret = ret < 0 ? ret : -EINVAL;
		goto free_pages_list;
	}
	#if 0
	for (i = 0; i < nr_pages; i++) {
		pr_debug("page_to_pfn(pages[%i])=0x%lx\n", i, page_to_pfn(pages[i]));
	}
	#endif

	pr_debug("%s, vma->vm_start 0x%lx, vma->vm_end 0x%lx, (vm_end - vm_start) 0x%lx, vma->vm_pgoff 0x%lx, user_va 0x%llx, len 0x%lx, nr_pages 0x%lx\n",
		__func__, vma->vm_start, vma->vm_end,
		(vma->vm_end - vma->vm_start), vma->vm_pgoff, addr, len, nr_pages);

	/* construct new page table entry for the pages*/
	ret = vmf_replace_pages(vma, addr_aligned,
				pages, nr_pages, prot);
	if (ret) {
		pr_err("err %d, failed to vmf_replace_pages!!!\n", ret);
		ret = -EFAULT;
		goto free_user_pages;
	}

	/* Flush cache if the access to the user virtual address is uncached. */
	if (pgprot_val(prot) & _PAGE_UNCACHE) {
		for (i = 0; i < nr_pages; i++) {
			/* flush cache*/
			arch_sync_dma_for_device(page_to_phys(pages[i]), PAGE_SIZE, DMA_BIDIRECTIONAL);
			/* put page back */
			put_page(pages[i]);
		}
	}
	else {
		for (i = 0; i < nr_pages; i++) {
			/* put page back */
			put_page(pages[i]);
		}
	}

	kvfree(pages);

	return 0;

free_user_pages:
	for (i = 0; i < nr_pages; i++) {
		/* put page back */
		put_page(pages[i]);
	}
free_pages_list:
	kvfree(pages);

	return ret;
}

/**
 * remap_malloc_buf - remap a range of memory allocated by malloc() API from user space.
 * Normally, the CPU access to the user virtual address which is allocated by mallc() API is
 * through cache. This remap_malloc_buf() API is to re-construct the pte table entry for the
 * corresponding pages of the user virtual address as uncached memory, so that CPU access to
 * the virtual address is uncached.
 * @addr: virtual address which is got by malloc API from user space
 * @len: the length of the memory allocated by malloc API
 * @uncaced: if true, remap the memory as uncached, otherwise cached
 *
 * Return 0 if success.
 *
 * If uncached flag is true, the memory range of this virtual address will be flushed to make
 * sure all the dirty data is evicted.
 *
 */
int remap_malloc_buf(unsigned long addr, size_t len, bool uncaced)
{
	struct vm_area_struct *vma = NULL;
	struct mm_struct *mm = current->mm;
	pgprot_t prot;
	int ret = 0;

	if (!len) {
		pr_err("Invalid userptr size!!!\n");
		return -EINVAL;
	}

	mmap_read_lock(mm);
	vma = vma_lookup(mm, addr);
	if (!vma) {
		pr_err("%s, vma_lookup failed!\n", __func__);
		mmap_read_unlock(mm);
		return -EFAULT;
	}

	pgprot_val(prot) = 0;
	/* If true, add uncached property so that pfn_pte will use the pfn of system port to
	   constructs the page table entry.
	   Be carefull, do NOT change the value of the original vma->vm_page_prot*/
	if (uncaced)
		prot = pgprot_dmacoherent(prot);

	ret = zap_and_replace_pages(vma, addr, len, prot);
	mmap_read_unlock(mm);

	return ret;
}
EXPORT_SYMBOL_GPL(remap_malloc_buf);