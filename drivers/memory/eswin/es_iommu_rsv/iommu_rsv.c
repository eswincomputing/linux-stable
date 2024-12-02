// SPDX-License-Identifier: GPL-2.0-only
/*
 * A fairly generic DMA-API to IOMMU-API glue layer.
 *
 * Copyright (C) 2014-2015 ARM Ltd.
 *
 * based in part on arch/arm/mm/dma-mapping.c:
 * Copyright (C) 2000-2004 Russell King
 */
#include <linux/version.h>
#include <linux/acpi_iort.h>
#include <linux/atomic.h>
#include <linux/crash_dump.h>
#include <linux/device.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/gfp.h>
#include <linux/huge_mm.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/spinlock.h>
#include <linux/swiotlb.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/kallsyms.h>
#include <linux/sched/mm.h>

#define BUF_SIZE 0x300000

#define IOVA_FQ_SIZE 256

/* Timeout (in ms) after which entries are flushed from the queue */
#define IOVA_FQ_TIMEOUT 10

/* Flush queue entry for deferred flushing */
struct iova_fq_entry
{
	unsigned long iova_pfn;
	unsigned long pages;
	struct list_head freelist;
	u64 counter; /* Flush counter when this entry was added */
};

/* Per-CPU flush queue structure */
struct iova_fq
{
	struct iova_fq_entry entries[IOVA_FQ_SIZE];
	unsigned int head, tail;
	spinlock_t lock;
};

enum iommu_dma_cookie_type
{
	IOMMU_DMA_IOVA_COOKIE,
	IOMMU_DMA_MSI_COOKIE,
};

struct iommu_dma_cookie
{
	enum iommu_dma_cookie_type type;
	union
	{
		struct
		{
			struct iova_domain iovad;

			struct iova_fq __percpu *fq; /* Flush queue */
			/* Number of TLB flushes that have been started */
			atomic64_t fq_flush_start_cnt;
			/* Number of TLB flushes that have been finished */
			atomic64_t fq_flush_finish_cnt;
			/* Timer to regularily empty the flush queues */
			struct timer_list fq_timer;
			/* 1 when timer is active, 0 when not */
			atomic_t fq_timer_on;
		};
		/* Trivial linear page allocator for IOMMU_DMA_MSI_COOKIE */
		dma_addr_t msi_iova;
	};
	struct list_head msi_page_list;

	/* Domain for flush queue callback; NULL if flush queue not in use */
	struct iommu_domain *fq_domain;
};

static void *(*es_common_pages_remap)(struct page **pages, size_t size, pgprot_t prot, const void *caller) = NULL;
static void (*__iommu_dma_unmap)(struct device *dev, dma_addr_t dma_addr, size_t size) = NULL;

static void (*__iommu_dma_free)(struct device *dev, size_t size, void *cpu_add) = NULL;
static void (*dma_prep_coherent)(struct page *page, size_t size) = NULL;
static unsigned long (*klp_name)(const char *name) = NULL;

static struct iova_domain *es_get_iovad(struct iommu_domain *domain)
{
	struct iova_domain *iovad;

	iovad = (struct iova_domain *)(&(domain->iova_cookie->iovad));
	return iovad;
}

static int dma_info_to_prot(enum dma_data_direction dir, bool coherent,
							unsigned long attrs)
{
	int prot = coherent ? IOMMU_CACHE : 0;

	if (attrs & DMA_ATTR_PRIVILEGED)
		prot |= IOMMU_PRIV;

	switch (dir)
	{
	case DMA_BIDIRECTIONAL:
		return prot | IOMMU_READ | IOMMU_WRITE;
	case DMA_TO_DEVICE:
		return prot | IOMMU_READ;
	case DMA_FROM_DEVICE:
		return prot | IOMMU_WRITE;
	default:
		return 0;
	}
}

static void __iommu_dma_free_pages(struct page **pages, int count)
{
	while (count--)
		__free_page(pages[count]);
	kvfree(pages);
}

static struct page **__iommu_dma_alloc_pages(struct device *dev,
											 unsigned int count, unsigned long order_mask, gfp_t gfp)
{
	struct page **pages;
	unsigned int i = 0, nid = dev_to_node(dev);

	order_mask &= (2U << MAX_ORDER) - 1;
	if (!order_mask)
		return NULL;

	pages = kvcalloc(count, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return NULL;

	/* IOMMU can map any pages, so himem can also be used here */
	gfp |= __GFP_NOWARN | __GFP_HIGHMEM;

	/* It makes no sense to muck about with huge pages */
	gfp &= ~__GFP_COMP;

	while (count)
	{
		struct page *page = NULL;
		unsigned int order_size;

		/*
		 * Higher-order allocations are a convenience rather
		 * than a necessity, hence using __GFP_NORETRY until
		 * falling back to minimum-order allocations.
		 */
		// 2U << __fls(count): 得到count的mask， 比如count为0b1000, mask就是0b1111.
		// mask和order_mask相与，取出最最高bit，就是针对当前count可以分配内存的最大block size。
		for (order_mask &= (2U << __fls(count)) - 1;
			 order_mask; order_mask &= ~order_size)
		{
			unsigned int order = __fls(order_mask);
			gfp_t alloc_flags = gfp;

			order_size = 1U << order;
			if (order_mask > order_size)
				alloc_flags |= __GFP_NORETRY;
			page = alloc_pages_node(nid, alloc_flags, order);
			if (!page)
				continue;
			if (order)
				split_page(page, order);
			break;
		}
		if (!page)
		{
			__iommu_dma_free_pages(pages, i);
			return NULL;
		}
		count -= order_size;
		while (order_size--)
			pages[i++] = page++;
	}
	return pages;
}

static struct page **iommu_rsv_iova_alloc_pages(struct device *dev, unsigned long iova, unsigned long size, struct sg_table *sgt, gfp_t gfp, unsigned long attrs)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	struct iova_domain *iovad = es_get_iovad(domain); //&domain->iova_cookie->iovad;
	bool coherent = dev_is_dma_coherent(dev);
	int ioprot = dma_info_to_prot(DMA_BIDIRECTIONAL, coherent, attrs);
	struct page **pages = NULL;
	int ret;
	unsigned int alloc_sizes = domain->pgsize_bitmap;
	unsigned int count, min_size;

	min_size = alloc_sizes & -alloc_sizes;
	if (min_size < PAGE_SIZE)
	{
		min_size = PAGE_SIZE;
		alloc_sizes |= PAGE_SIZE;
	}
	else
	{
		size = ALIGN(size, min_size);
	}
	count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	pages = __iommu_dma_alloc_pages(dev, count, alloc_sizes >> PAGE_SHIFT, gfp);
	if (pages == NULL)
	{
		dev_err(dev, "alloc pages failed.\n");
		return NULL;
	}
	size = iova_align(iovad, size);

	if (sg_alloc_table_from_pages(sgt, pages, count, 0, size, GFP_KERNEL))
	{
		goto out_free_iova;
	}

	if (!(ioprot & IOMMU_CACHE))
	{
		struct scatterlist *sg;
		int i;

		for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
			dma_prep_coherent(sg_page(sg), sg->length);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    ret = iommu_map_sg(domain, iova, sgt->sgl, sgt->orig_nents, ioprot, GFP_KERNEL);
#else
	ret = iommu_map_sg(domain, iova, sgt->sgl, sgt->orig_nents, ioprot);
#endif
	if (ret < size)
	{
		dev_err(dev, "%s %d fail %d\n", __func__, __LINE__, ret);
		goto out_free_sg;
	}
	return pages;

out_free_sg:
	sg_free_table(sgt);
out_free_iova:
	__iommu_dma_free_pages(pages, count);
	return NULL;
}

static void *iommu_rsv_iova_alloc_remap(struct device *dev, dma_addr_t iova, unsigned long size, gfp_t gfp, unsigned long attrs)
{

	struct page **pages = NULL;
	struct sg_table sgt;
	void *cpu_addr;

	pages = iommu_rsv_iova_alloc_pages(dev, iova, size, &sgt, gfp, attrs);
	if (pages == NULL)
	{
		dev_err(dev, "alloc pages failed.\n");
		return NULL;
	}

	sg_free_table(&sgt);
	cpu_addr = es_common_pages_remap(pages, size, pgprot_dmacoherent(PAGE_KERNEL), __builtin_return_address(0));
	if (!cpu_addr)
		goto out_unmap;
	return cpu_addr;

out_unmap:
	__iommu_dma_unmap(dev, iova, size);
	__iommu_dma_free_pages(pages, PAGE_ALIGN(size) >> PAGE_SHIFT);
	return NULL;
}

void *iommu_map_rsv_iova(struct device *dev, dma_addr_t iova, unsigned long size, gfp_t gfp, unsigned long attrs)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	phys_addr_t phys;

	if (!size)
	{
		dev_err(dev, "%s, error, size is 0.\n", __func__);
		return NULL;
	}

	phys = iommu_iova_to_phys(domain, iova);
	if (phys != 0)
	{
		dev_err(dev, "%s, iova=0x%llx, have exits phys=0x%llx\n", __func__, iova, phys);
		return NULL;
	}

	gfp &= ~(__GFP_DMA | GFP_DMA32 | __GFP_HIGHMEM);
	gfp |= __GFP_ZERO;

	if (gfpflags_allow_blocking(gfp))
	{
		return iommu_rsv_iova_alloc_remap(dev, iova, size, gfp, attrs);
	}
	return NULL;
}
EXPORT_SYMBOL(iommu_map_rsv_iova);

int iommu_map_rsv_iova_with_phys(struct device *dev, dma_addr_t iova, unsigned long size, phys_addr_t paddr, unsigned long attrs)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	struct iova_domain *iovad = es_get_iovad(domain);
	bool coherent = dev_is_dma_coherent(dev);
	int ioprot = dma_info_to_prot(DMA_BIDIRECTIONAL, coherent, attrs);
	size_t iova_off;
	unsigned int min_pagesz;
	phys_addr_t phys;

	if (!size)
	{
		dev_err(dev, "%s, error, size is 0.\n", __func__);
		return -EINVAL;
	}
	phys = iommu_iova_to_phys(domain, iova);
	if (phys != 0)
	{
		dev_err(dev, "%s, iova=0x%llx, have exits phys=0x%llx\n", __func__, iova, paddr);
		return -EINVAL;
	}
	/* find out the minimum page size supported */
	min_pagesz = 1 << __ffs(domain->pgsize_bitmap);

	/*
	 *	 * both the virtual address and the physical one, as well as
	 *		 * the size of the mapping, must be aligned (at least) to the
	 *		 * size of the smallest page supported by the hardware
	 */

	if (!IS_ALIGNED(iova | paddr | size, min_pagesz))
	{
		pr_err("unaligned: iova 0x%llx pa %pa size 0x%zx min_pagesz 0x%x\n", iova, &paddr, size, min_pagesz);
		return -EINVAL;
	}

	iova_off = iova_offset(iovad, phys);
	size = iova_align(iovad, size + iova_off);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    if (iommu_map(domain, iova, paddr - iova_off, size, ioprot, GFP_ATOMIC))
#else
	if (iommu_map_atomic(domain, iova, paddr - iova_off, size, ioprot))
#endif
	{
		free_iova_fast(iovad, iova_pfn(iovad, iova), size >> iova_shift(iovad));
		return -EIO;
	}
	return 0;
}
EXPORT_SYMBOL(iommu_map_rsv_iova_with_phys);

int iommu_unmap_rsv_iova(struct device *dev, void *cpu_addr, dma_addr_t iova, unsigned long size)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	struct iova_domain *iovad = es_get_iovad(domain);
	phys_addr_t phys;

	phys = iommu_iova_to_phys(domain, iova);
	if (phys == 0)
	{
		dev_dbg(dev, "%s, iova=0x%llx, have not maps\n", __func__, iova);
		return -EFAULT;
	}

	if (!size)
	{
		return -EINVAL;
	}
	size = iova_align(iovad, size);
	iommu_unmap(domain, iova, size);

	if (cpu_addr != NULL && __iommu_dma_free)
	{
		__iommu_dma_free(dev, size, cpu_addr);
	}
	return 0;
}
EXPORT_SYMBOL(iommu_unmap_rsv_iova);

ssize_t iommu_rsv_iova_map_sgt(struct device *dev, dma_addr_t iova, struct sg_table *sgt, unsigned long attrs, size_t buf_size)
{
	struct iommu_domain *domain = iommu_get_domain_for_dev(dev);
	bool coherent = dev_is_dma_coherent(dev);
	int ioprot = dma_info_to_prot(DMA_BIDIRECTIONAL, coherent, attrs);
	struct scatterlist *sg;
	phys_addr_t phys;
	int i;
	size_t size = 0;
	ssize_t ret = 0;

	phys = iommu_iova_to_phys(domain, iova);
	if (phys != 0) {
		if (phys != sg_phys(sgt->sgl)) {
			dev_err(dev, "%s, iova=0x%llx, have exits phys=0x%llx\n", __func__, iova, phys);
			return -EINVAL;
		}
		else {
			dev_dbg(dev, "%s, iova=0x%llx mapped!\n", __func__, iova);
			return ret;
		}
	}
#if 0
	if (!(ioprot & IOMMU_CACHE))
	{
		int i;

		for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
			dma_prep_coherent(sg_page(sg), sg->length);
	}
#endif
	for_each_sg(sgt->sgl, sg, sgt->orig_nents, i)
		size += sg->length;

	if (size != buf_size) {
		dev_err(dev, "%s %d fail, sg size 0x%lx != buf_size 0x%lx\n", __func__, __LINE__, size, buf_size);
		return -EINVAL;
	}

	ret = iommu_map_sg(domain, iova, sgt->sgl, sgt->orig_nents, ioprot, GFP_KERNEL);
	if (ret < 0 || ret < size)
	{
		dev_err(dev, "%s %d fail, ret %ld, size %ld\n", __func__, __LINE__, ret, size);
		goto out;
	}

	return ret;
out:
	if (ret != -ENOMEM && ret != -EREMOTEIO)
		return -EINVAL;

	return ret;
}
EXPORT_SYMBOL(iommu_rsv_iova_map_sgt);

static void eswin_iommu_put_func(void)
{
	if (__iommu_dma_free)
	{
		__iommu_dma_free = NULL;
	}

	if (es_common_pages_remap)
	{
		es_common_pages_remap = NULL;
	}

	if (__iommu_dma_unmap)
	{
		__iommu_dma_unmap = NULL;
	}

	if (dma_prep_coherent)
	{
		dma_prep_coherent = NULL;
	}
}

void *kaddr_lookup_name(const char *fname_raw)
{
    int i;
    unsigned long kaddr;
    char *fname;
    char fname_lookup[KSYM_SYMBOL_LEN];

    fname = kzalloc(strlen(fname_raw)+5, GFP_KERNEL);
    if (!fname) {
        return NULL;
    }
    strcpy(fname, fname_raw);
    strcat(fname, "+0x0");
    fname[strlen(fname_raw) + 4] = '\0';

    kaddr = (unsigned long) &sprint_symbol;
    kaddr &= 0xfffffffffffff000;

    for (i = 0x0 ; i < 0x1000000; i++ )
    {
        sprint_symbol(fname_lookup, kaddr);

        if (strncmp(fname_lookup, fname, strlen(fname)) == 0)
        {
            kfree(fname);
            return (void *)kaddr;
        }
        kaddr += 0x2;
    }

    kaddr = (unsigned long) &sprint_symbol;
    kaddr &= 0xfffffffffffff000;

    for (i = 0x1000000; i >= 0 && kaddr; i--) {
        sprint_symbol(fname_lookup, kaddr);
        if (strncmp(fname_lookup, fname, strlen(fname)) == 0)
        {
            kfree(fname);
            return (void *)kaddr;
        }
        kaddr -= 0x2;
    }
    kfree(fname);
    return NULL;
}

static int __init eswin_iommu_rsv_init(void)
{

    klp_name = kaddr_lookup_name("kallsyms_lookup_name");
    if (klp_name == NULL) {
        printk("cannot lookup name.\n");
        return -EINVAL;
    }
    __iommu_dma_free = (void *)klp_name("__iommu_dma_free");
	if (!__iommu_dma_free)
	{
		goto err;
	}

	es_common_pages_remap = (void *)klp_name("dma_common_pages_remap");
	if (!es_common_pages_remap)
	{
		goto err;
	}

	__iommu_dma_unmap = (void *)klp_name("__iommu_dma_unmap");
	if (!__iommu_dma_unmap)
	{
		goto err;
	}

	dma_prep_coherent = (void *)klp_name("arch_dma_prep_coherent");
	if (!dma_prep_coherent) {
        goto err;
    }

	return 0;
err:
	eswin_iommu_put_func();
	return -EINVAL;
}

static void __exit eswin_iommu_rsv_exit(void)
{
	eswin_iommu_put_func();
}

module_init(eswin_iommu_rsv_init);
module_exit(eswin_iommu_rsv_exit);

MODULE_LICENSE("GPL");
