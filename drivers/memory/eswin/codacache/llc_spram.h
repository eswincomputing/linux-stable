/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defines for the LLC SPRAM driver
 */

#ifndef __LLC_SPRAM_H
#define __LLC_SPRAM_H
#include <linux/io.h>

// At least one way must be reserved for cache
#define MAX_CACHE_SIZE  (2 * 0x100000)  //2MB
#define MAX_NUM_OF_WAY_SET_ASSOCIATIVE  16  //16-way set associative, i.e. 16 lines per set
#define SIZE_OF_PER_WAY (MAX_CACHE_SIZE / MAX_NUM_OF_WAY_SET_ASSOCIATIVE)
#define CACHE_LINE_SIZE 128

// 8MB/16way/256 = 2048 sets
// 4MB/16way/128 = 2048 sets
#define MAX_SETS  (MAX_CACHE_SIZE / MAX_NUM_OF_WAY_SET_ASSOCIATIVE/CACHE_LINE_SIZE)

//At least one way must be reserved for cache
#define SPRAM_WAYS    (MAX_NUM_OF_WAY_SET_ASSOCIATIVE - 1)
#define SPRAM_SIZE    (SPRAM_WAYS * MAX_CACHE_SIZE / MAX_NUM_OF_WAY_SET_ASSOCIATIVE)  //4MB of MAX_CACHE_SIZE is used as spram

#define NPU_LLC0_OFFSET    0x188000
#define NPU_LLC1_OFFSET    0x189000


enum coda_cache_reg
{
	CODA_CACHE_REG_CCUTCR       = 0x0000,
	CODA_CACHE_REG_CCUTAR       = 0x0004,
	CODA_CACHE_REG_CCUCTCR      = 0x0010,
	CODA_CACHE_REG_CCUCTAR      = 0x0014,
	CODA_CACHE_REG_CCUCAOR      = 0x0018,
	CODA_CACHE_REG_CCUSPCR0     = 0x0020,
	CODA_CACHE_REG_CCUSPCR1     = 0x0024,
	CODA_CACHE_REG_CCUSPBR0     = 0x0028,
	CODA_CACHE_REG_CCUSPBR1     = 0x002C,
	CODA_CACHE_REG_CCUWPCR00    = 0x0040,
	CODA_CACHE_REG_CCUWPCR10    = 0x0044,
	CODA_CACHE_REG_CCUWPCR01    = 0x0048,
	CODA_CACHE_REG_CCUWPCR11    = 0x004c,
	CODA_CACHE_REG_CCUCMCR      = 0x0100,
	CODA_CACHE_REG_CCUCMAR      = 0x0104,
	CODA_CACHE_REG_CCUCMLR0     = 0x0108,
	CODA_CACHE_REG_CCUCMLR1     = 0x010c,
	CODA_CACHE_REG_CCUCMLR2     = 0x0110,
	CODA_CACHE_REG_CCUCMDR      = 0x0114,
	CODA_CACHE_REG_CCUCMWVR     = 0x0118,
	CODA_CACHE_REG_CCUCECR      = 0x0140,
	CODA_CACHE_REG_CCUCESR      = 0x0144,
	CODA_CACHE_REG_CCUCESAR     = 0x0148,
	CODA_CACHE_REG_CCUCELR0     = 0x014c,
	CODA_CACHE_REG_CCUCELR1     = 0x0150,
	CODA_CACHE_REG_CCUUEDR      = 0x0154,
	CODA_CACHE_REG_CCUUEIR      = 0x0158,
	CODA_CACHE_REG_CCUUESR      = 0x015c,
	CODA_CACHE_REG_CCUUESAR     = 0x0160,
	CODA_CACHE_REG_CCUUELR0     = 0x0164,
	CODA_CACHE_REG_CCUUELR1     = 0x0168,
	CODA_CACHE_REG_CCUIDR       = 0x01c0,
	CODA_CACHE_REG_CCUCRTR      = 0x01c4,
	CODA_CACHE_REG_CCUESR       = 0x01c8,
	CODA_CACHE_REG_CCUEMR       = 0x01cc,
	CODA_CACHE_REG_CCUEAR       = 0x01d0,
};
typedef enum {
	LLC_CACHE_NODE_0 = 0,
	LLC_CACHE_NODE_1,
	MAX_LLC_CACHE_NODE_NUM,
}onwhich_die_t;

struct llc_reg_base {
	void __iomem *CodaCache0_RegBase;
	void __iomem *CodaCache1_RegBase;
};

/* llc cache operation */
typedef  void (*llc_flush_all_t)(unsigned long start, unsigned long len);
struct llc_cache_ops {
		llc_flush_all_t llc_flush_all;
};

int llc_user_register(struct device *user_dev);
int npu_cfg_rst(int nid, bool enable);
int npu_core_rst(int nid, bool enable);
int llc_spram_avail_size(int nid, uint32_t *pSpramSize);

int llc_flush_operation(unsigned long start, unsigned long len);
#endif