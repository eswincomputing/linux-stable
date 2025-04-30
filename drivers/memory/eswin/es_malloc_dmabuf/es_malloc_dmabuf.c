// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN Malloc To DMA-BUF Driver
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
 * Authors: Yuyang Cong <congyuyang@eswincomputing.com>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/dma-buf.h>
#include <uapi/linux/dma-heap.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/types.h>
#include <linux/sched/mm.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <linux/dma-map-ops.h>
#include <linux/eswin_rsvmem_common.h>
#include <linux/scatterlist.h>
#include <linux/vmalloc.h>

#include <uapi/linux/malloc_dmabuf.h>

#define DRIVER_NAME "malloc_dmabuf"

struct malloc_dmabuf_attachment {
    struct device *dev;
    struct sg_table *table;
    struct list_head list;
    bool mapped;
};
struct malloc_dmabuf_priv {
    struct page **pages;
    unsigned int nr_pages;
    unsigned int offset_in_first_page;
    size_t size;
    struct mutex lock;
    struct sg_table sg_table;
    bool sg_init;
    struct list_head attachments;
    struct list_head list;
    void *vaddr;
    int vmap_cnt;
    unsigned long fd_flags;  /* Used to determine vmap properties, e.g., O_DSYNC for uncached */
};

static struct sg_table *dup_sg_table(struct sg_table *orig)
{
    struct sg_table *new_table;
    int ret, i;
    struct scatterlist *sg, *new_sg;

    new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
    if (!new_table)
        return ERR_PTR(-ENOMEM);

    ret = sg_alloc_table(new_table, orig->nents, GFP_KERNEL);
    if (ret) {
        kfree(new_table);
        return ERR_PTR(-ENOMEM);
    }

    new_sg = new_table->sgl;
	for_each_sgtable_sg(orig, sg, i) {
		sg_set_page(new_sg, sg_page(sg), sg->length, sg->offset);
		new_sg = sg_next(new_sg);
	}

    return new_table;
}


static int malloc_dmabuf_attach(struct dma_buf *dmabuf,
                                struct dma_buf_attachment *attachment)
{
    struct malloc_dmabuf_priv *buffer = dmabuf->priv;
    struct malloc_dmabuf_attachment *a;
    struct sg_table *table;

    if (!buffer->sg_init) {
        pr_err("No sg_table in buffer priv when attach!\n");
        return -EINVAL;
    }

    a = kzalloc(sizeof(*a), GFP_KERNEL);
    if (!a)
        return -ENOMEM;

    table = dup_sg_table(&buffer->sg_table);
    if (IS_ERR(table)) {
        kfree(a);
        return PTR_ERR(table);
    }

    a->table = table;
    a->dev = attachment->dev;
    a->mapped = false;
    INIT_LIST_HEAD(&a->list);

    attachment->priv = a;

    mutex_lock(&buffer->lock);
    list_add(&a->list, &buffer->attachments);
    mutex_unlock(&buffer->lock);

    pr_debug("%s: device %s attached\n", __func__, dev_name(attachment->dev));

    return 0;
}

static void malloc_dmabuf_detach(struct dma_buf *dmabuf,
                                 struct dma_buf_attachment *attachment)
{
    struct malloc_dmabuf_priv *buffer = dmabuf->priv;
    struct malloc_dmabuf_attachment *a = attachment->priv;

    mutex_lock(&buffer->lock);
    list_del(&a->list);
    mutex_unlock(&buffer->lock);

    sg_free_table(a->table);
    kfree(a->table);
    kfree(a);

    pr_debug("%s: device %s detached\n", __func__, dev_name(attachment->dev));
}

static struct sg_table *malloc_dmabuf_map_dma_buf(struct dma_buf_attachment *attach,
                                                  enum dma_data_direction dir)
{
    struct malloc_dmabuf_attachment *a = attach->priv;
    struct sg_table *table = a->table;
    int ret;

    ret = dma_map_sgtable(attach->dev, table, dir, 0);
    if (ret)
        return ERR_PTR(-ENOMEM);

    a->mapped = true;
    return table;
}

static void malloc_dmabuf_unmap_dma_buf(struct dma_buf_attachment *attach,
                                        struct sg_table *sgt,
                                        enum dma_data_direction dir)
{
    struct malloc_dmabuf_attachment *a = attach->priv;

    a->mapped = false;

    dma_unmap_sgtable(attach->dev, sgt, dir, 0);
}

static void malloc_dmabuf_release(struct dma_buf *dmabuf)
{
    struct malloc_dmabuf_priv *buffer = dmabuf->priv;

    if (buffer->pages && buffer->nr_pages > 0) {
        unpin_user_pages(buffer->pages, buffer->nr_pages);
    }
    
    if (buffer->sg_init) {
        sg_free_table(&buffer->sg_table);
        buffer->sg_init = false;
    }

    kfree(buffer->pages);
    kfree(buffer);
}

static int malloc_dmabuf_begin_cpu_access(struct dma_buf *dmabuf,
                                          enum dma_data_direction direction)
{
    struct malloc_dmabuf_priv *buffer = dmabuf->priv;
    struct sg_table *table = &buffer->sg_table;
    struct scatterlist *sg;
    int i;

    mutex_lock(&buffer->lock);

    if (buffer->vmap_cnt) {
        invalidate_kernel_vmap_range(buffer->vaddr, buffer->size);
    }
        
#ifndef QEMU_DEBUG
    /* Cache sync since we used DMA_ATTR_SKIP_CPU_SYNC */
    for_each_sg(table->sgl, sg, table->nents, i)
        arch_sync_dma_for_cpu(sg_phys(sg), sg->length, direction);
#endif
    mutex_unlock(&buffer->lock);
    return 0;
}

static int malloc_dmabuf_end_cpu_access(struct dma_buf *dmabuf,
                                        enum dma_data_direction direction)
{
    struct malloc_dmabuf_priv *buffer = dmabuf->priv;
    struct sg_table *table = &buffer->sg_table;
    struct scatterlist *sg;
    int i;

    mutex_lock(&buffer->lock);

    if (buffer->vmap_cnt){
        flush_kernel_vmap_range(buffer->vaddr, buffer->size);
    }

#ifndef QEMU_DEBUG
    /* Sync back for device after CPU access */
    for_each_sg(table->sgl, sg, table->nents, i) {
        arch_sync_dma_for_device(sg_phys(sg), sg->length, direction);
    }
#endif
    mutex_unlock(&buffer->lock);
    return 0;
}


static void *malloc_dmabuf_do_vmap(struct dma_buf *dmabuf)
{
    struct malloc_dmabuf_priv *buffer = dmabuf->priv;
    pgprot_t prot = PAGE_KERNEL;
    struct sg_table *table = &buffer->sg_table;
    int npages = buffer->nr_pages;
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

    if (buffer->fd_flags & O_DSYNC) {
#ifndef QEMU_DEBUG
        prot = pgprot_dmacoherent(PAGE_KERNEL);
#endif
        pr_debug("%s: using uncached mapping, prot=0x%x\n", __func__, (unsigned int)pgprot_val(prot));
    }
    else {
        pr_debug("%s: using cached mapping, prot=0x%x\n", __func__, (unsigned int)pgprot_val(prot));
    }

    vaddr = vmap(pages, npages, VM_MAP, prot);
    vfree(pages);

    if (!vaddr)
        return ERR_PTR(-ENOMEM);

    return vaddr;
}

static int malloc_dmabuf_vmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
    struct malloc_dmabuf_priv *buffer = dmabuf->priv;
    void *vaddr;
    int ret = 0;

    mutex_lock(&buffer->lock);
    if (buffer->vmap_cnt > 0) {
        buffer->vmap_cnt++;
        dma_buf_map_set_vaddr(map, buffer->vaddr + buffer->offset_in_first_page);
        goto out;
    }

    vaddr = malloc_dmabuf_do_vmap(dmabuf);
    if (IS_ERR(vaddr)) {
        ret = PTR_ERR(vaddr);
        goto out;
    }
    buffer->vaddr = vaddr;
    buffer->vmap_cnt++;
    dma_buf_map_set_vaddr(map, buffer->vaddr + buffer->offset_in_first_page);

out:
    mutex_unlock(&buffer->lock);
    return ret;
}


static void malloc_dmabuf_vunmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
    struct malloc_dmabuf_priv *buffer = dmabuf->priv;
    void *vaddr;
    vaddr = buffer->vaddr;
    mutex_lock(&buffer->lock);
    if (--buffer->vmap_cnt == 0) {
        vunmap(vaddr);
        buffer->vaddr = NULL;
    }
    mutex_unlock(&buffer->lock);
    dma_buf_map_clear(map);
}

static int malloc_dmabuf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
    struct malloc_dmabuf_priv *buffer = dmabuf->priv;
    struct sg_table *sgt = &buffer->sg_table;
    unsigned long addr = vma->vm_start;
    struct scatterlist *sg;
    struct page *page = NULL;
    unsigned long pgoff = vma->vm_pgoff, mapsize = 0;
    unsigned long size_remaining = vma->vm_end - vma->vm_start;//vma_pages(vma);
    unsigned int nents = 0;
    int i, ret = 0;

    if ((vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) == 0)
        return -EINVAL;

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

    for_each_sg(sgt->sgl, sg, sgt->orig_nents, i) {
        pr_debug("sgl:%d, phys:0x%llx, sg->length:%d\n", i, sg_phys(sg), sg->length);

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

        addr += PAGE_ALIGN(mapsize);
        size_remaining -= PAGE_ALIGN(mapsize);
        if (size_remaining == 0){
            return 0;
        }
    }

    return 0;
}

static const struct dma_buf_ops malloc_dmabuf_ops = {
    .attach = malloc_dmabuf_attach,
    .detach = malloc_dmabuf_detach,
    .map_dma_buf = malloc_dmabuf_map_dma_buf,
    .unmap_dma_buf = malloc_dmabuf_unmap_dma_buf,
    .release = malloc_dmabuf_release,
    .vmap = malloc_dmabuf_vmap,
    .vunmap = malloc_dmabuf_vunmap,
    .begin_cpu_access = malloc_dmabuf_begin_cpu_access,
    .end_cpu_access = malloc_dmabuf_end_cpu_access,
    .mmap = malloc_dmabuf_mmap,
};

static int build_sg_table_for_priv(struct malloc_dmabuf_priv *buffer)
{
    int ret;

    ret = sg_alloc_table_from_pages(&buffer->sg_table,
                                    buffer->pages, buffer->nr_pages,
                                    buffer->offset_in_first_page, buffer->size,
                                    GFP_KERNEL);
    if (ret == 0) {
        buffer->sg_init = true;
        return 0;
    }

    pr_err("Failed to build sg_table, ret=%d\n", ret);
    return ret;
}

static int esw_convert_userptr_to_dmabuf(unsigned long arg)
{
    struct esw_userptr u;
    struct malloc_dmabuf_priv *buffer;
    struct dma_buf *dmabuf;
    struct mm_struct *mm = current->mm;
    DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
    unsigned long start, end;
    int nr_pages;
    int pinned, fd, ret;

    if (copy_from_user(&u, (void __user *)arg, sizeof(u)))
        return -EFAULT;

    if (!access_ok((void __user *)(uintptr_t)u.user_ptr, u.length))
        return -EFAULT;

    if (u.length == 0)
        return -EINVAL;

    start = (u.user_ptr & PAGE_MASK);
    end = ((u.user_ptr + u.length - 1) & PAGE_MASK);
    nr_pages = (int)((end - start) / PAGE_SIZE + 1);
    u.aligned_length = nr_pages << PAGE_SHIFT;

    buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
    if (!buffer)
        return -ENOMEM;

    buffer->pages = kcalloc(nr_pages, sizeof(struct page *), GFP_KERNEL);
    if (!buffer->pages) {
        kfree(buffer);
        return -ENOMEM;
    }

    INIT_LIST_HEAD(&buffer->attachments);
    mutex_init(&buffer->lock);

    mmap_read_lock(mm);
    pinned = pin_user_pages(start, nr_pages, FOLL_WRITE | FOLL_LONGTERM, buffer->pages);
    mmap_read_unlock(mm);

    if (pinned < nr_pages) {
        if (pinned > 0) {
            unpin_user_pages(buffer->pages, pinned);
        }
        kfree(buffer->pages);
        kfree(buffer);
        return -EFAULT;
    }

    buffer->nr_pages = nr_pages;
    buffer->offset_in_first_page = offset_in_page(u.user_ptr);
    buffer->size = u.aligned_length;
    buffer->fd_flags = u.fd_flags;

    ret = build_sg_table_for_priv(buffer);
    if (ret) {
        unpin_user_pages(buffer->pages, buffer->nr_pages);
        kfree(buffer->pages);
        kfree(buffer);
        return ret;
    }

    exp_info.ops = &malloc_dmabuf_ops;
    exp_info.size = buffer->size;
    exp_info.flags = O_RDWR | O_CLOEXEC;
    exp_info.priv = buffer;

    dmabuf = dma_buf_export(&exp_info);
    if (IS_ERR(dmabuf)) {
        ret = PTR_ERR(dmabuf);
        sg_free_table(&buffer->sg_table);
        unpin_user_pages(buffer->pages, buffer->nr_pages);
        kfree(buffer->pages);
        kfree(buffer);
        return ret;
    }

    fd = dma_buf_fd(dmabuf, O_CLOEXEC);
    if (fd < 0) {
        dma_buf_put(dmabuf);
        return fd;
    }

    u.dma_fd = fd;
    u.offset_in_first_page = buffer->offset_in_first_page;
    if (copy_to_user((void __user *)arg, &u, sizeof(u))) {
        dma_buf_put(dmabuf);
        return -EFAULT;
    }

    return 0;
}

static int esw_get_dmabuf_info(unsigned long arg)
{
    struct esw_userptr u;
    struct dma_buf *dmabuf;
    struct malloc_dmabuf_priv *buffer;
    int ret = 0;

    if (copy_from_user(&u, (void __user *)arg, sizeof(u)))
        return -EFAULT;

    dmabuf = dma_buf_get(u.dma_fd);
    if (IS_ERR(dmabuf)) {
        ret = PTR_ERR(dmabuf);
        return ret;
    }

    if (dmabuf->ops == &malloc_dmabuf_ops) {
        buffer = (struct malloc_dmabuf_priv *)dmabuf->priv;
        u.mem_type = true;
        u.offset_in_first_page = buffer->offset_in_first_page;
    } else {
        u.mem_type = false;
        u.offset_in_first_page = 0;
    }

    if (copy_to_user((void __user *)arg, &u, sizeof(u))) {
        dma_buf_put(dmabuf);
        return -EFAULT;
    }

    dma_buf_put(dmabuf);
    return 0;
}

static long malloc_dmabuf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0;

    switch (cmd) {
    case ESW_CONVERT_USERPTR_TO_DMABUF:
        ret = esw_convert_userptr_to_dmabuf(arg);
        break;
    case ESW_GET_DMABUF_INFO:
        ret = esw_get_dmabuf_info(arg);
        break;
    default:
        pr_err(DRIVER_NAME ": invalid cmd=0x%x\n", cmd);
        ret = -EINVAL;
        break;
    }

    return ret;
}

static int malloc_dmabuf_open(struct inode *inode, struct file *file)
{
    pr_debug("%s:%d, success!\n", __func__, __LINE__);
    return 0;
}

static int malloc_dmabuf_release_fops(struct inode *inode, struct file *file)
{
    pr_debug("%s:%d, success!\n", __func__, __LINE__);
    return 0;
}

static const struct file_operations malloc_dmabuf_fops = {
    .owner          = THIS_MODULE,
    .open           = malloc_dmabuf_open,
    .release        = malloc_dmabuf_release_fops,
    .unlocked_ioctl = malloc_dmabuf_ioctl,
    .llseek         = no_llseek,
};

static struct miscdevice malloc_dmabuf_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = DRIVER_NAME,
    .mode = 0666,
    .fops  = &malloc_dmabuf_fops,
};

static int __init malloc_dmabuf_init(void)
{
    int ret;

    ret = misc_register(&malloc_dmabuf_miscdev);
    if (ret) {
        pr_err(DRIVER_NAME ": failed to register misc device: %d\n", ret);
        return ret;
    }

    return ret;
}

static void __exit malloc_dmabuf_exit(void)
{
    misc_deregister(&malloc_dmabuf_miscdev);
}

module_init(malloc_dmabuf_init);
module_exit(malloc_dmabuf_exit);

MODULE_AUTHOR("Yuyang Cong <congyuyang@eswincomputing.com>");
MODULE_DESCRIPTION("Malloc To DMA-BUF Driver");
MODULE_LICENSE("GPL v2");