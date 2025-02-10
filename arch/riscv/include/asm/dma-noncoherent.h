/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 Renesas Electronics Corp.
 */

#ifndef __ASM_DMA_NONCOHERENT_H
#define __ASM_DMA_NONCOHERENT_H

#include <linux/dma-direct.h>

/*
 * struct riscv_nonstd_cache_ops - Structure for non-standard CMO function pointers
 *
 * @wback: Function pointer for cache writeback
 * @inv: Function pointer for invalidating cache
 * @wback_inv: Function pointer for flushing the cache (writeback + invalidating)
 */
struct riscv_nonstd_cache_ops {
	void (*wback)(phys_addr_t paddr, size_t size);
	void (*inv)(phys_addr_t paddr, size_t size);
	void (*wback_inv)(phys_addr_t paddr, size_t size);
};

#if IS_ENABLED(CONFIG_ARCH_ESWIN_EIC770X_SOC_FAMILY)
typedef enum {
	FLAT_DDR_MEM = 0,
	INTERLEAVE_DDR_MEM,
	SPRAM,
} eic770x_memory_type_t;

void _do_arch_sync_cache_all(int nid, eic770x_memory_type_t mem_type);
#endif

extern struct riscv_nonstd_cache_ops noncoherent_cache_ops;

void riscv_noncoherent_register_cache_ops(const struct riscv_nonstd_cache_ops *ops);


static inline void arch_get_mem_node_and_type(unsigned long pfn, int *nid, eic770x_memory_type_t *p_mem_type)
{
#ifdef CONFIG_NUMA
	if (unlikely(PFN_IN_SPRAM_DIE0(pfn))) {
		*nid = 0;
		*p_mem_type = SPRAM;
	}
	else if(unlikely(PFN_IN_SPRAM_DIE1(pfn))) {
		*nid = 1;
		*p_mem_type = SPRAM;
	}
	else {
		*nid = pfn_to_nid(pfn);
		if (CHECK_MEMORY_RANGE_OPFUNC(pfn, MEM, INTPART0) || CHECK_MEMORY_RANGE_OPFUNC(pfn, MEM, INTPART1))
			*p_mem_type = INTERLEAVE_DDR_MEM;
		else
			*p_mem_type = FLAT_DDR_MEM;
	}
#else
	if (unlikely(PFN_IN_SPRAM_DIE0(pfn))) {
		*nid = 0;
		*p_mem_type = SPRAM;
	}
	else {
		*nid = 0;
		*p_mem_type = FLAT_DDR_MEM;
	}
#endif
}
#endif	/* __ASM_DMA_NONCOHERENT_H */
