// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN video decoder driver
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
 */
#include "vdec_allocator.h"
#include <linux/version.h>
#include <linux/scatterlist.h>

#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/cache.h>
#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include "hantrodec.h"
#include "bidirect_list.h"

#define LOG_TAG DEC_DEV_NAME ":alloc"
#include "vc_drv_log.h"

#if (LINUX_VERSION_CODE > KERNEL_VERSION(4, 20, 17) &&  \
    !defined(CONFIG_ARCH_NO_SG_CHAIN)) ||               \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0) &&   \
     (defined(ARCH_HAS_SG_CHAIN) || defined(CONFIG_ARCH_HAS_SG_CHAIN)))
# define gcdUSE_LINUX_SG_TABLE_API   1
#else
# define gcdUSE_LINUX_SG_TABLE_API   0
#endif

#define gcdSUPPRESS_OOM_MESSAGE 1

#if gcdSUPPRESS_OOM_MESSAGE
# define gcdNOWARN __GFP_NOWARN
#else
# define gcdNOWARN        0
#endif

#define gcdUSING_PFN_FOLLOW 0

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
# define current_mm_mmap_sem         current->mm->mmap_lock
#else
# define current_mm_mmap_sem         current->mm->mmap_sem
#endif

#ifndef untagged_addr
# define untagged_addr(addr)         (addr)
#endif

const static int g_iommu = 1;

enum um_desc_type {
    UM_PHYSICAL_MAP,
    UM_PAGE_MAP,
    UM_PFN_MAP,
};

/* Descriptor of a user memory imported. */
struct um_desc {
    int                     type;

    union {
        /* UM_PHYSICAL_MAP. */
        unsigned long       physical;

        /* UM_PAGE_MAP. */
        struct {
            struct page   **pages;
        };

        /* UM_PFN_MAP. */
        struct {
            unsigned long  *pfns;
            int            *refs;
            int            pfns_valid;
        };
    };

    struct sg_table         sgt;

    /* contiguous chunks, does not include padding pages. */
    int                     chunk_count;

    unsigned long           vm_flags;
    unsigned long           user_vaddr;
    size_t                  size;
    unsigned long           offset;
    dma_addr_t              dmaHandle;

    size_t                  pageCount;
    size_t                  extraPage;
    unsigned int            alloc_from_res;
    struct device           *dev;
};

static int _UserMemoryCache(struct um_desc *um, ES_CACHE_OPERATION Operation);

static void *alloc_memory(size_t bytes)
{
    if (bytes > PAGE_SIZE)
        return vzalloc(bytes);
    else
        return kzalloc(bytes, GFP_KERNEL | __GFP_NOWARN);
}

static void free_memory(void *ptr)
{
    if (is_vmalloc_addr(ptr))
        vfree(ptr);
    else
        kfree(ptr);
}

#if 0
static int import_physical_map(int iommu, struct device *dev,
                               struct um_desc *um, unsigned long phys)
{
    um->type = UM_PHYSICAL_MAP;
    um->physical = phys & PAGE_MASK;
    um->chunk_count = 1;

    if (iommu) {
        dma_addr_t dmaHandle;
        size_t size = um->size + (phys & (~PAGE_MASK));
        unsigned long pfn = phys >> PAGE_SHIFT;

        if (pfn_valid(pfn))
            dmaHandle = dma_map_page(dev, pfn_to_page(pfn),
                                     0, size, DMA_BIDIRECTIONAL);
        else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
            dmaHandle = dma_map_resource(dev, pfn << PAGE_SHIFT,
                                         size, DMA_BIDIRECTIONAL, 0);
#else
            dmaHandle = dma_map_page(dev, pfn_to_page(pfn),
                                     0, size, DMA_BIDIRECTIONAL);
#endif

        if (dma_mapping_error(dev, dmaHandle))
            return -ENOMEM;

        um->dmaHandle = dmaHandle;
    }

    return 0;
}
#endif

static int
import_page_map(struct device *dev, struct um_desc *um,
                unsigned long addr, size_t page_count, size_t size, unsigned long flags)
{
    int i;
    int result;
    struct page **pages;
#if 0
    if ((addr & (cache_line_size() - 1)) || (size & (cache_line_size() - 1))) {
        /* Not cpu cacheline size aligned, can not support. */
        LOG_ERR("Not cpu cacheline size aligned, can not support. addr: %lx, size: %lu, cache_line_size: %d\n",
            addr, size, cache_line_size());
        return -EINVAL;
    }
#endif
    pages = alloc_memory(page_count * sizeof(void *));
    if (!pages)
        return -ENOMEM;

    down_read(&current_mm_mmap_sem);

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 6, 0)
    result = pin_user_pages(addr & PAGE_MASK, page_count,
#elif LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0)
    result = get_user_pages(current, current->mm, addr & PAGE_MASK, page_count,
#else
    result = get_user_pages(addr & PAGE_MASK, page_count,
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0) || defined(CONFIG_PPC)
                            (flags & VM_WRITE) ? FOLL_WRITE : 0,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 1)
                            (flags & VM_WRITE) ? 1 : 0, 0,
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 168)
                            (flags & VM_WRITE) ? FOLL_WRITE : 0,
#else
                            (flags & VM_WRITE) ? 1 : 0, 0,
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,4,0)
                            pages);
#else
                            pages, NULL);
#endif


    up_read(&current_mm_mmap_sem);

    if (result < page_count) {
        for (i = 0; i < result; i++) {
            if (pages[i])
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 6, 0)
                unpin_user_page(pages[i]);
#else
                put_page(pages[i]);
#endif
        }

        LOG_ERR("%s:%d:: result: %d, page count: %lu, addr: %lx\n", __func__, __LINE__, result, page_count, addr);
        free_memory(pages);
        return -ENODEV;
    }

    um->chunk_count = 1;
    for (i = 1; i < page_count; i++) {
        if (page_to_pfn(pages[i]) != page_to_pfn(pages[i - 1]) + 1)
            ++um->chunk_count;
    }

    if (!um->alloc_from_res) {
#if gcdUSE_LINUX_SG_TABLE_API
        result = sg_alloc_table_from_pages(&um->sgt, pages, page_count,
                                           addr & ~PAGE_MASK, size, GFP_KERNEL | gcdNOWARN);

#else
        result = alloc_sg_list_from_pages(&um->sgt.sgl, pages, page_count,
                                          addr & ~PAGE_MASK, size, &um->sgt.nents);

        um->sgt.orig_nents = um->sgt.nents;
#endif
        if (unlikely(result < 0)) {
            LOG_WARN("%s: sg_alloc_table_from_pages failed\n", __func__);
            goto error;
        }

        //result = dma_map_sgtable(dev, &um->sgt, DMA_BIDIRECTIONAL, 0);
        result = dma_map_sg(dev, um->sgt.sgl, um->sgt.nents, DMA_BIDIRECTIONAL);
        if (g_iommu) {
            um->dmaHandle = sg_dma_address(um->sgt.sgl);
        }

        dma_sync_sg_for_cpu(dev, um->sgt.sgl, um->sgt.nents, DMA_FROM_DEVICE);
    }

    um->type = UM_PAGE_MAP;
    um->pages = pages;

    return 0;

error:
    for (i = 0; i < page_count; i++) {
        if (pages[i]) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 6, 0)
            unpin_user_page(pages[i]);
#else
            put_page(pages[i]);
#endif
        }
    }
#if gcdUSE_LINUX_SG_TABLE_API
    sg_free_table(&um->sgt);
#else
    kfree(um->sgt.sgl);
#endif

    free_memory(pages);

    return result;
}


static int
import_pfn_map(struct device *dev, struct um_desc *um,
               unsigned long addr, size_t pfn_count)
{
    int i;
    struct vm_area_struct *vma;
    unsigned long *pfns;
    int *refs;
    struct page **pages = NULL;
    int result = 0;
    size_t pageCount = 0;
    unsigned int data = 0;

    if (!current->mm)
        return -ENOTTY;

    down_read(&current_mm_mmap_sem);
    vma = find_vma(current->mm, addr);
#if !gcdUSING_PFN_FOLLOW && (LINUX_VERSION_CODE < KERNEL_VERSION(6, 5, 0))
    up_read(&current_mm_mmap_sem);
#endif

    if (!vma)
        return -ENOTTY;

    pfns = (unsigned long *)alloc_memory(pfn_count * sizeof(unsigned long));

    if (!pfns)
        return -ENOMEM;

    refs = (int *)alloc_memory(pfn_count * sizeof(int));

    if (!refs) {
        free_memory(pfns);
        return -ENOMEM;
    }

    pages = alloc_memory(pfn_count * sizeof(void *));
    if (!pages) {
        free_memory(pfns);
        free_memory(refs);
        return -ENOMEM;
    }

    for (i = 0; i < pfn_count; i++) {
#if gcdUSING_PFN_FOLLOW || (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0))
        int ret = 0;
        ret = follow_pfn(vma, addr, &pfns[i]);
        if (ret < 0) {
            /* Case maybe provides unmapped addr. */
            data = *(unsigned int *)addr;
            ret = follow_pfn(vma, addr, &pfns[i]);

            if (ret < 0) {
                up_read(&current_mm_mmap_sem);
                goto err;
            }
        }
#else
        /* protect pfns[i] */
        spinlock_t  *ptl;
        pgd_t       *pgd;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
        p4d_t       *p4d;
#endif
        pud_t       *pud;
        pmd_t       *pmd;
        pte_t       *pte;

        pgd = pgd_offset(current->mm, addr);
        if (pgd_none(*pgd) || pgd_bad(*pgd))
            goto err;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
        p4d = p4d_offset(pgd, addr);
        if (p4d_none(READ_ONCE(*p4d)))
            goto err;

        pud = pud_offset(p4d, addr);
# elif (defined(CONFIG_X86)) && LINUX_VERSION_CODE >= KERNEL_VERSION (4, 12, 0)
        pud = pud_offset((p4d_t *)pgd, addr);
# elif (defined(CONFIG_CPU_CSKYV2)) && LINUX_VERSION_CODE >= KERNEL_VERSION (4, 11, 0)
        pud = pud_offset((p4d_t *)pgd, addr);
# else
        pud = pud_offset(pgd, addr);
# endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0) */
        if (pud_none(*pud) || pud_bad(*pud))
            goto err;

        pmd = pmd_offset(pud, addr);
        if (pmd_none(*pmd) || pmd_bad(*pmd))
            goto err;

        //TODO: cannot be compiled for ko
        pte = pte_offset_map_lock(current->mm, pmd, addr, &ptl);

        if (!pte_present(*pte)) {
            if (pte)
                pte_unmap_unlock(pte, ptl);

            /* Case maybe provides unmapped addr. */
            data = *(unsigned int*)addr;
            pte = pte_offset_map_lock(current->mm, pmd, addr, &ptl);
            if (!pte_present(*pte)) {
                pte_unmap_unlock(pte, ptl);
                goto err;
            }
        }

        pfns[i] = pte_pfn(*pte);
        pte_unmap_unlock(pte, ptl);
#endif
        /* Advance to next. */
        addr += PAGE_SIZE;
    }
#if gcdUSING_PFN_FOLLOW || (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 5, 0))
    up_read(&current_mm_mmap_sem);
#endif

    for (i = 0; i < pfn_count; i++) {
        if (pfn_valid(pfns[i])) {
            struct page *page = pfn_to_page(pfns[i]);

            refs[i] = get_page_unless_zero(page);
            pages[i] = page;
            pageCount++;
        }
    }

    um->chunk_count = 1;
    for (i = 1; i < pfn_count; i++) {
        if (pfns[i] != pfns[i - 1] + 1)
            ++um->chunk_count;
    }

    um->pfns_valid = 0;
    if (pageCount == pfn_count && !um->alloc_from_res) {
#if gcdUSE_LINUX_SG_TABLE_API
        result = sg_alloc_table_from_pages(&um->sgt, pages, pfn_count, addr & ~PAGE_MASK,
                                           pfn_count * PAGE_SIZE, GFP_KERNEL | gcdNOWARN);

#else
        result = alloc_sg_list_from_pages(&um->sgt.sgl, pages, pfn_count, addr & ~PAGE_MASK,
                                          pfn_count * PAGE_SIZE, &um->sgt.nents);

        um->sgt.orig_nents = um->sgt.nents;
#endif
        if (unlikely(result < 0)) {
            LOG_WARN("%s: sg_alloc_table_from_pages failed\n", __func__);
            goto err;
        }

        result = dma_map_sg(dev, um->sgt.sgl, um->sgt.nents, DMA_TO_DEVICE);

        if (unlikely(result != um->sgt.nents)) {
#if gcdUSE_LINUX_SG_TABLE_API
            sg_free_table(&um->sgt);
#else
            kfree(um->sgt.sgl);
#endif
            LOG_WARN("%s: dma_map_sg failed\n", __func__);
            goto err;
        }

        if (g_iommu)
            um->dmaHandle = sg_dma_address(um->sgt.sgl);

        um->pfns_valid = 1;
    }

    free_memory(pages);
    pages = NULL;

    um->type = UM_PFN_MAP;
    um->pfns = pfns;
    um->refs = refs;
    return 0;

err:
    free_memory(pfns);
    free_memory(refs);
    free_memory(pages);

    return -ENOTTY;
}

static int
_Import(void* Memory, struct device *dev, size_t Size, struct um_desc *UserMemory)
{
    int status = 0;
    unsigned long vm_flags = 0;
    struct vm_area_struct *vma = NULL;
    size_t start, end, memory;
    int result = 0;
    size_t extraPage, pageCount;

    /* Verify the arguments. */
    if (Memory == NULL || Size == 0) {
        LOG_ERR("invalid param, Memory=%px, Size=%lu\n", Memory, Size);
        return -EINVAL;
    }

    memory = untagged_addr((size_t)Memory);

    /* Get the number of required pages. */
    end = (memory + Size + PAGE_SIZE - 1) >> PAGE_SHIFT;
    start = memory >> PAGE_SHIFT;
    pageCount = end - start;

    /* Allocate extra page to avoid cache overflow */
    extraPage = 2;

    LOG_DBG("%s(%d): pageCount: %lu. extraPage: %lu, Memory=%px Size=%lu\n",
                   __func__, __LINE__, pageCount, extraPage, Memory, Size);

    /* Overflow. */
    if ((memory + Size) < memory) {
        LOG_ERR("overflow, %d", __LINE__);
        return -EINVAL;
    }

    if (memory) {
        size_t vaddr = memory;

        down_read(&current_mm_mmap_sem);
        vma = find_vma(current->mm, memory);
        up_read(&current_mm_mmap_sem);

        if (!vma) {
            /* No such memory, or across vmas. */
            status = -EINVAL;
            LOG_ERR("No such memory, or across vmas\n");
            goto OnError;
        }

#ifdef CONFIG_ARM
        /* coherent cache in case vivt or vipt-aliasing cache. */
        __cpuc_flush_user_range(memory, memory + Size, vma->vm_flags);
#endif

        vm_flags = vma->vm_flags;
        vaddr = vma->vm_end;

        down_read(&current_mm_mmap_sem);
        while (vaddr < memory + Size) {
            vma = find_vma(current->mm, vaddr);

            if (!vma) {
                /* No such memory. */
                up_read(&current_mm_mmap_sem);
                status = -EINVAL;
                LOG_ERR("No such memory\n");
                goto OnError;
            }

            if ((vma->vm_flags & VM_PFNMAP) != (vm_flags & VM_PFNMAP)) {
                /*
                 * Can not support different map type:
                 * both PFN and PAGE detected.
                 */
                up_read(&current_mm_mmap_sem);
                status = -1;
                LOG_ERR("Can not support different map type\n");
                goto OnError;
            }

            vaddr = vma->vm_end;
        }
        up_read(&current_mm_mmap_sem);
    }

    if (vm_flags & VM_PFNMAP)
        result = import_pfn_map(dev, UserMemory, memory, pageCount);
    else
        result = import_page_map(dev, UserMemory, memory, pageCount, Size, vm_flags);


    if (result < 0) {
        status = result;
        goto OnError;
    }

    UserMemory->vm_flags = vm_flags;
    UserMemory->user_vaddr = (unsigned long)Memory;
    UserMemory->size = Size;
    UserMemory->offset = (memory & ~PAGE_MASK);
    UserMemory->pageCount = pageCount;
    UserMemory->extraPage = extraPage;
    UserMemory->dev = dev;

    /* Success. */
    LOG_DBG("%s: success, user memory: %px, size: %lu, dma handle: %px, dev: %px\n",
        __func__, Memory, Size, (void*)UserMemory->dmaHandle, dev);
    return 0;

OnError:
    LOG_ERR("%s: return %d, user memory: %px, size: %lu, dev: %px\n",
        __func__, status, Memory, Size, dev);
    return status;
}

#if 0
static int _GetUserMemroyPages(struct um_desc *userMemory) {
    return userMemory->pageCount + userMemory->extraPage;
}

static int _UserMemoryContiguous(struct um_desc *userMemory) {
    return (userMemory->chunk_count == 1) ? 1 : 0;
}
#endif

static int
_UserMemoryAttach(struct device *dev, void* memory, size_t size, struct um_desc** umd)
{
    int status;
    struct um_desc *userMemory = NULL;

    /* Handle is meangless for this importer. */
    if (!memory || !size || !umd) {
        LOG_ERR("%s: invalid parameters\n", __func__);
        return -EINVAL;
    }

    userMemory = alloc_memory(sizeof(struct um_desc));
    if (!userMemory) {
        status = -ENOMEM;
        LOG_ERR("alloc memory um_desc failed\n");
        goto OnError;
    }

    status = _Import(memory, dev, size, userMemory);
    if (status) {
        goto OnError;
    }

    *umd = userMemory;
    return 0;

OnError:
    if (userMemory != NULL)
        free_memory(userMemory);

    return status;
}


static void
release_physical_map(struct um_desc *um)
{
    struct device *dev = um->dev;
    if (g_iommu) {
        unsigned long pfn = um->physical >> PAGE_SHIFT;
        size_t size = um->size + um->offset;

        if (pfn_valid(pfn))
            dma_unmap_page(dev, um->dmaHandle, size, DMA_BIDIRECTIONAL);
        else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 9, 0)
            dma_unmap_resource(dev, um->dmaHandle, size, DMA_BIDIRECTIONAL, 0);
#else
            dma_unmap_page(dev, um->dmaHandle, size, DMA_BIDIRECTIONAL);
#endif

        um->dmaHandle = 0;
    }
}

static void
release_page_map(struct um_desc *um)
{
    int i;
    struct device *dev = um->dev;

    dma_sync_sg_for_device(dev, um->sgt.sgl, um->sgt.nents, DMA_TO_DEVICE);
    dma_sync_sg_for_cpu(dev, um->sgt.sgl, um->sgt.nents, DMA_FROM_DEVICE);
    dma_unmap_sg(dev, um->sgt.sgl, um->sgt.nents, DMA_FROM_DEVICE);

    um->dmaHandle = 0;

#if gcdUSE_LINUX_SG_TABLE_API
    sg_free_table(&um->sgt);
#else
    kfree(um->sgt.sgl);
#endif

    for (i = 0; i < um->pageCount; i++) {
        if (!PageReserved(um->pages[i]))
            SetPageDirty(um->pages[i]);

#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 6, 0)
        unpin_user_page(um->pages[i]);
#else
        put_page(um->pages[i]);
#endif
    }

    free_memory(um->pages);
}

static void
release_pfn_map(struct um_desc *um)
{
    int i;
    struct device *dev = um->dev;

    if (um->pfns_valid) {
        dma_unmap_sg(dev, um->sgt.sgl, um->sgt.nents, DMA_FROM_DEVICE);

#if gcdUSE_LINUX_SG_TABLE_API
        sg_free_table(&um->sgt);
#else
        kfree(um->sgt.sgl);
#endif
    }

    um->dmaHandle = 0;

    for (i = 0; i < um->pageCount; i++) {
        if (pfn_valid(um->pfns[i])) {
            struct page *page = pfn_to_page(um->pfns[i]);

            if (!PageReserved(page))
                SetPageDirty(page);

            if (um->refs[i])
                put_page(page);
        }
    }

    free_memory(um->pfns);
    free_memory(um->refs);
}

static void
_UserMemoryFree(struct um_desc* userMemory)
{
    if (userMemory) {
        switch (userMemory->type) {
        case UM_PHYSICAL_MAP:
            release_physical_map(userMemory);
            break;
        case UM_PAGE_MAP:
            release_page_map(userMemory);
            break;
        case UM_PFN_MAP:
            release_pfn_map(userMemory);
            break;
        }
        LOG_DBG("%s: dmaHandle %llx, size %lu\n", __func__, userMemory->dmaHandle, userMemory->size);
        free_memory(userMemory);
    }
}

int vdec_attach_user_memory(struct file *filp, struct device *dev, user_memory_desc *desc) {
    int ret = 0;
    bi_list_node* node = NULL;
    struct um_desc* userMemory = NULL;
    struct filp_priv *fp_priv = NULL;

    if (!filp || !filp->private_data) {
        LOG_ERR("%s:%d:: invalid param\n", __func__, __LINE__);
        return -EINVAL;
    }

    node = bi_list_create_node();
    if (!node) {
        LOG_ERR("create list node failed\n");
        return -ENOMEM;
    }

    ret = _UserMemoryAttach(dev, desc->memory, desc->size, &userMemory);
    if (!ret) {
        node->data = (void*)userMemory;
        fp_priv = (struct filp_priv*)filp->private_data;
        bi_list_insert_node_tail(&fp_priv->user_memory_list, node);

        LOG_DBG("attach success, user memory: %px, size: %lu; output dmaHandle: %llx, size: %lu, dev: %px\n",
            desc->memory, desc->size, userMemory->dmaHandle, userMemory->size, userMemory->dev);
        desc->memory = (void*)userMemory->dmaHandle;
        desc->size = userMemory->size;
    } else {
        bi_list_free_node(node);
        LOG_ERR("%s failed, ret=%d\n", __func__, ret);
    }

    return ret;
}

bi_list_node *vdec_find_node(bi_list *list, user_memory_desc *desc) {
    bi_list_node *node = NULL;
    struct um_desc* userMemory = NULL;

    node = list->head;
    while (node) {
        userMemory = (struct um_desc*)node->data;
        if (userMemory->user_vaddr == (unsigned long)desc->memory) {
            break;
        }

        node = node->next;
    }
    return node;
}

int vdec_detach_user_memory(struct file *filp, user_memory_desc *desc) {
    bi_list_node* node = NULL;
    struct filp_priv *fp_priv = NULL;

    if (!filp || !filp->private_data) {
        LOG_ERR("%s:%d:: invalid param\n", __func__, __LINE__);
        return -EINVAL;
    }

    fp_priv = (struct filp_priv*)filp->private_data;
    node = vdec_find_node(&fp_priv->user_memory_list, desc);
    if (node) {
        _UserMemoryFree((struct um_desc*)node->data);
        bi_list_remove_node(&fp_priv->user_memory_list, node);
        return 0;
    } else {
        LOG_DBG("%d: no memory node found, addr: %px, size: %lu\n", __LINE__, desc->memory, desc->size);
    }

    return -EFAULT;
}

void vdec_clean_user_memory(bi_list *list) {
    bi_list_node *node = NULL;
    bi_list_node *cur_node;

    if (!list) {
        LOG_ERR("%s:%d:: invalid param\n", __func__, __LINE__);
        return;
    }

    node = list->head;
    while (node) {
        _UserMemoryFree((struct um_desc*)node->data);
        cur_node = node;
        node = node->next;
        bi_list_free_node(cur_node);
    }
    list->head = list->tail = NULL;
}

static inline void
_MemoryBarrier(void)
{
#if defined(CONFIG_ARM) && (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
    dsb();
#else
    /* memory barrier */
    mb();
#endif
}

static int
_UserMemoryCache(struct um_desc *um, ES_CACHE_OPERATION Operation)
{
    enum dma_data_direction dir;
    struct device *dev = um->dev;

    if (um->type == UM_PHYSICAL_MAP || um->alloc_from_res) {
        _MemoryBarrier();
        return 0;
    }

    if (um->type == UM_PFN_MAP && um->pfns_valid == 0) {
        _MemoryBarrier();
        return 0;
    }

#ifdef CONFIG_ARM
    /* coherent cache in case vivt or vipt-aliasing cache. */
    __cpuc_flush_user_range(um->user_vaddr, um->user_vaddr + um->size, um->vm_flags);
#endif

    LOG_DBG("sync cache, dev: %px, dmaHandle: %px, size: %lu, alloc: %u, opr: %d\n",
        um->dev, (void*)um->dmaHandle, um->size, um->alloc_from_res, Operation);
    switch (Operation) {
    case ES_CACHE_CLEAN:
        dir = DMA_TO_DEVICE;
        dma_sync_sg_for_device(dev, um->sgt.sgl, um->sgt.nents, dir);
        break;
    case ES_CACHE_FLUSH:
        dir = DMA_TO_DEVICE;
        dma_sync_sg_for_device(dev, um->sgt.sgl, um->sgt.nents, dir);
        dir = DMA_FROM_DEVICE;
        dma_sync_sg_for_cpu(dev, um->sgt.sgl, um->sgt.nents, dir);
        break;
    case ES_CACHE_INVALIDATE:
        dir = DMA_FROM_DEVICE;
        dma_sync_sg_for_cpu(dev, um->sgt.sgl, um->sgt.nents, dir);
        break;
    default:
        LOG_WARN("%d: invalid operatoin %d\n", __LINE__, Operation);
        return -EINVAL;
    }

    return 0;
}

int vdec_sync_user_memory_cache(struct file *filp, user_memory_desc *desc, ES_CACHE_OPERATION opr) {
    bi_list_node* node = NULL;
    struct filp_priv *fp_priv = NULL;
    int ret = -EFAULT;

    if (!filp || !filp->private_data) {
        LOG_ERR("%s:%d:: invalid param\n", __func__, __LINE__);
        return -EINVAL;
    }

    LOG_DBG("%s: memory=%px, size=%lu, nid: %u\n", __func__, desc->memory, desc->size, desc->nid);
    fp_priv = (struct filp_priv*)filp->private_data;
    node = vdec_find_node(&fp_priv->user_memory_list, desc);
    if (node) {
        ret = _UserMemoryCache((struct um_desc*)node->data, opr);
    } else {
        LOG_DBG("%d: no memory node found, addr: %px, size: %lu\n", __LINE__, desc->memory, desc->size);
    }

    return ret;
}
