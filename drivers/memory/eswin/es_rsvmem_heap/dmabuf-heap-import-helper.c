// SPDX-License-Identifier: GPL-2.0-only
/*
 * ESWIN DMABUF heap helper APIs
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#include <linux/export.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/scatterlist.h>
#include <linux/dma-heap.h>
#include <linux/dmabuf-heap-import-helper.h>

struct drm_prime_member {
	struct dma_buf *dma_buf;
	uint64_t handle;

	struct rb_node dmabuf_rb;
	struct rb_node handle_rb;
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

struct esw_exp_attachment {
	struct heap_mem *hmem;
	struct sg_table table;
	struct device *dev;
	struct heap_root root;
};

// #define PRINT_ORIGINAL_SPLITTERS 1
static int esw_common_heap_attach(struct dma_buf *dmabuf,
			   struct dma_buf_attachment *attachment)
{
	struct esw_export_buffer_info *buffer = dmabuf->priv;
	struct esw_exp_attachment *a;
	int out_mapped_nents[1];
	int ret = 0;
	struct sg_table *sgt = NULL;
	struct scatterlist *sg;
	int i;
	size_t size, len;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	a->dev = attachment->dev;

	common_dmabuf_heap_import_init(&a->root, a->dev);
	a->hmem = common_dmabuf_heap_import_from_user(&a->root, buffer->dbuf_fd);
	if (IS_ERR(a->hmem))
		return PTR_ERR(a->hmem);

	ret = sg_split(a->hmem->sgt->sgl, a->hmem->sgt->nents, buffer->slice.offset, 1, &buffer->slice.len,
					&a->table.sgl, &out_mapped_nents[0], GFP_KERNEL);
	if (ret) {
		common_dmabuf_heap_release(a->hmem);
		kfree(a);
		return ret;
	}
	a->table.nents = out_mapped_nents[0];
	a->table.orig_nents = out_mapped_nents[0];
	sgt = &a->table;
	#ifdef PRINT_ORIGINAL_SPLITTERS
	{
		pr_info("%s:orig:sgt->orig_nents=%d, out_mapped_nents[0]=%d\n",
			__func__, sgt->orig_nents, out_mapped_nents[0]);
		for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
			pr_info("orig[%d]:sg->length=0x%x, sg_dma_len=0x%x, sg_phys=0x%lx\n",
				i, sg->length, sg_dma_len(sg), (unsigned long)sg_phys(sg));
		}
	}
	#endif
	/* Re-format the splitted sg list in the actual slice len */
	{
		size = buffer->slice.len;
		for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
			if (sg->length >= size) {
				sg->length = size;
				sg_dma_len(sg) = size;
				sg_mark_end(sg);
				pr_debug("refmt[%d]:sg->length=0x%x, sg_dma_len=0x%x, sg_phys=0x%lx\n",
					i, sg->length, sg_dma_len(sg), (unsigned long)sg_phys(sg));
				break;
			}
			len = min_t(size_t, size, sg->length);
			size -= len;
			pr_debug("refmt[%d]:sg->length=0x%x, sg_dma_len=0x%x, sg_phys=0x%lx\n",
				i, sg->length, sg_dma_len(sg), (unsigned long)sg_phys(sg));
		}
		sgt->orig_nents = sgt->nents = i + 1;
	}

	attachment->priv = a;

	return ret;
}

static void esw_common_heap_detach(struct dma_buf *dmabuf,
			    struct dma_buf_attachment *attachment)
{
	struct esw_exp_attachment *a = attachment->priv;

	kfree(a->table.sgl);
	common_dmabuf_heap_release(a->hmem);
	common_dmabuf_heap_import_uninit(&a->root);
	kfree(a);
}

static struct sg_table *esw_common_map_dma_buf(struct dma_buf_attachment *attachment,
					     enum dma_data_direction direction)
{
	struct esw_exp_attachment *a = attachment->priv;
	return &a->table;
}

static void esw_common_unmap_dma_buf(struct dma_buf_attachment *attachment,
				   struct sg_table *table,
				   enum dma_data_direction direction)
{
}

static int esw_common_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct esw_export_buffer_info *buffer = dmabuf->priv;
	// printk("%s enter\n", __func__);
	return dma_buf_mmap(buffer->dmabuf, vma, buffer->slice.offset >> PAGE_SHIFT);
}

static int esw_common_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
					     enum dma_data_direction direction)
{
	struct esw_export_buffer_info *buffer = dmabuf->priv;
	return dma_buf_begin_cpu_access(buffer->dmabuf, direction);
}

static int esw_common_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					   enum dma_data_direction direction)
{
	struct esw_export_buffer_info *buffer = dmabuf->priv;
	return dma_buf_end_cpu_access(buffer->dmabuf, direction);
}

static int esw_common_heap_vmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct esw_export_buffer_info *buffer = dmabuf->priv;
	struct dma_buf_map pmap;
	int ret;

	ret = dma_buf_vmap(buffer->dmabuf, &pmap);

	map->is_iomem = false;
	map->vaddr_iomem = pmap.vaddr_iomem + buffer->slice.offset;

	return ret;
}

static void esw_common_heap_vunmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct esw_export_buffer_info *buffer = dmabuf->priv;
	struct dma_buf_map pmap = *map;

	pmap.vaddr_iomem -= buffer->slice.offset;
	dma_buf_vunmap(buffer->dmabuf, &pmap);
}

static void esw_common_dma_buf_release(struct dma_buf *dmabuf)
{
	struct esw_export_buffer_info *buffer = dmabuf->priv;

	// printk("%s %d\n", __func__, __LINE__);

	dma_buf_put(buffer->dmabuf);
	kfree(buffer);
}

static const struct dma_buf_ops esw_common_buf_ops = {
	.attach = esw_common_heap_attach,
	.detach = esw_common_heap_detach,
	.map_dma_buf = esw_common_map_dma_buf,
	.unmap_dma_buf = esw_common_unmap_dma_buf,
	.begin_cpu_access = esw_common_dma_buf_begin_cpu_access,
	.end_cpu_access = esw_common_dma_buf_end_cpu_access,
	.mmap = esw_common_mmap,
	.vmap = esw_common_heap_vmap,
	.vunmap = esw_common_heap_vunmap,
	.release = esw_common_dma_buf_release,
};

int esw_common_dmabuf_split_export(int dbuf_fd, unsigned int offset, size_t len, int fd_flags, char *name)
{
	struct esw_export_buffer_info *buffer_info;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	int fd;
	struct dma_buf *dmabuf;

	buffer_info = kzalloc(sizeof(*buffer_info), GFP_KERNEL);
	if (!buffer_info)
		return -ENOMEM;

	buffer_info->dbuf_fd = dbuf_fd;
	buffer_info->fd_flags = fd_flags;
	buffer_info->slice.offset = offset;
	buffer_info->slice.len = len;
	snprintf(buffer_info->name, sizeof(buffer_info->name), "%s", name);

	buffer_info->dmabuf = dma_buf_get(buffer_info->dbuf_fd);
	if (IS_ERR(buffer_info->dmabuf))
		return PTR_ERR(buffer_info->dmabuf);

//	printk("input slice: oft=0x%d, len=%lu\n", buffer_info->slice.offset, buffer_info->slice.len);

	buffer_info->slice.offset = PAGE_ALIGN(buffer_info->slice.offset);
	buffer_info->slice.len = PAGE_ALIGN(buffer_info->slice.len);

//	printk("align slice: oft=0x%d, len=%lu\n", buffer_info->slice.offset, buffer_info->slice.len);

	/* create the dmabuf */
	exp_info.exp_name = buffer_info->name;
	exp_info.ops = &esw_common_buf_ops;
	exp_info.size = buffer_info->slice.len;
	exp_info.flags = buffer_info->fd_flags;
	exp_info.priv = buffer_info;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		return PTR_ERR(dmabuf);
	}

	fd = dma_buf_fd(dmabuf, buffer_info->fd_flags);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		/* put the splitted dmabuf, the esw_common_dma_buf_release will be called,
		   the parent dmabuf will be put and the buffer_info will be free at that time */
	}
	return fd;
}

EXPORT_SYMBOL(esw_common_dmabuf_split_export);
