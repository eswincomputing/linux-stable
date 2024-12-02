// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include "npu_spram.h"
#include "npu_base_regs.h"

// At least one way must be reserved for cache
#define MAX_CACHE_SIZE (2 * 0x100000)  //2MB
#define MAX_NUM_OF_WAY_SET_ASSOCIATIVE \
	16  //16-way set associative, i.e. 16 lines per set
#define SIZE_OF_PER_WAY (MAX_CACHE_SIZE / MAX_NUM_OF_WAY_SET_ASSOCIATIVE)
#define NPU_CACHE_LINE_SIZE 128

// 8MB/16way/256 = 2048 sets
// 4MB/16way/128 = 2048 sets
#define MAX_SETS \
	(MAX_CACHE_SIZE / MAX_NUM_OF_WAY_SET_ASSOCIATIVE / NPU_CACHE_LINE_SIZE)

//At least one way must be reserved for cache
#define SPRAM_WAYS (MAX_NUM_OF_WAY_SET_ASSOCIATIVE - 1)
#define SPRAM_SIZE                     \
	(SPRAM_WAYS * MAX_CACHE_SIZE / \
	 MAX_NUM_OF_WAY_SET_ASSOCIATIVE)  //4MB of MAX_CACHE_SIZE is used as spram

#define LLC_INTERLEAVE_ENABLE_BIT 0
const static uint32_t npu_llc_offset[2] = { 0x188000, 0x189000 };

enum coda_cache_reg {
	CODA_CACHE_REG_CCUTCR = 0x0000,
	CODA_CACHE_REG_CCUTAR = 0x0004,
	CODA_CACHE_REG_CCUCTCR = 0x0010,
	CODA_CACHE_REG_CCUCTAR = 0x0014,
	CODA_CACHE_REG_CCUCAOR = 0x0018,
	CODA_CACHE_REG_CCUSPCR0 = 0x0020,
	CODA_CACHE_REG_CCUSPCR1 = 0x0024,
	CODA_CACHE_REG_CCUSPBR0 = 0x0028,
	CODA_CACHE_REG_CCUSPBR1 = 0x002C,
	CODA_CACHE_REG_CCUWPCR00 = 0x0040,
	CODA_CACHE_REG_CCUWPCR10 = 0x0044,
	CODA_CACHE_REG_CCUWPCR01 = 0x0048,
	CODA_CACHE_REG_CCUWPCR11 = 0x004c,
	CODA_CACHE_REG_CCUCMCR = 0x0100,
	CODA_CACHE_REG_CCUCMAR = 0x0104,
	CODA_CACHE_REG_CCUCMLR0 = 0x0108,
	CODA_CACHE_REG_CCUCMLR1 = 0x010c,
	CODA_CACHE_REG_CCUCMLR2 = 0x0110,
	CODA_CACHE_REG_CCUCMDR = 0x0114,
	CODA_CACHE_REG_CCUCMWVR = 0x0118,
	CODA_CACHE_REG_CCUCECR = 0x0140,
	CODA_CACHE_REG_CCUCESR = 0x0144,
	CODA_CACHE_REG_CCUCESAR = 0x0148,
	CODA_CACHE_REG_CCUCELR0 = 0x014c,
	CODA_CACHE_REG_CCUCELR1 = 0x0150,
	CODA_CACHE_REG_CCUUEDR = 0x0154,
	CODA_CACHE_REG_CCUUEIR = 0x0158,
	CODA_CACHE_REG_CCUUESR = 0x015c,
	CODA_CACHE_REG_CCUUESAR = 0x0160,
	CODA_CACHE_REG_CCUUELR0 = 0x0164,
	CODA_CACHE_REG_CCUUELR1 = 0x0168,
	CODA_CACHE_REG_CCUIDR = 0x01c0,
	CODA_CACHE_REG_CCUCRTR = 0x01c4,
	CODA_CACHE_REG_CCUESR = 0x01c8,
	CODA_CACHE_REG_CCUEMR = 0x01cc,
	CODA_CACHE_REG_CCUEAR = 0x01d0,
};

void *spram_start = NULL;

void npu_llc_write(struct nvdla_device *dev, uint32_t device, uint32_t addr,
		   uint32_t value)
{
	dla_reg_write(dev, npu_llc_offset[device] + addr, value);
}

uint32_t npu_llc_read(struct nvdla_device *dev, uint32_t device, uint32_t addr)
{
	return dla_reg_read(dev, npu_llc_offset[device] + addr);
}

static int npu_llc_init(struct nvdla_device *dev, uint32_t spram_size,
			uint32_t device)
{
	uint32_t val = 0;
	uint32_t spram_num_of_ways;

	//At least one way must be reserved for cache,  and spramSzie must be intergral multiple of SIZE_OF_PER_WAY
	if ((spram_size > MAX_CACHE_SIZE) || (spram_size < SIZE_OF_PER_WAY) ||
	    (spram_size % SIZE_OF_PER_WAY)) {
		dev_err(&dev->pdev->dev, "Invalid spramSize\n");
		return -1;
	}

	spram_num_of_ways = spram_size / SIZE_OF_PER_WAY;
	npu_llc_write(dev, device, CODA_CACHE_REG_CCUCMWVR,
		      GENMASK_ULL(spram_num_of_ways - 1, 0));
	npu_llc_write(dev, device, CODA_CACHE_REG_CCUCMCR, 0x0);
	do {
		val = npu_llc_read(dev, device, CODA_CACHE_REG_CCUCMAR);
		msleep(1);
	} while (val & 0x1);

	npu_llc_write(dev, device, CODA_CACHE_REG_CCUCMCR, 0x10000);
	do {
		val = npu_llc_read(dev, device, CODA_CACHE_REG_CCUCMAR);
		msleep(1);
	} while (val & 0x1);

	npu_llc_write(dev, device, CODA_CACHE_REG_CCUSPBR0, 0);
	npu_llc_write(dev, device, CODA_CACHE_REG_CCUSPBR1, 0);

	npu_llc_write(dev, device, CODA_CACHE_REG_CCUSPCR0,
		      (spram_num_of_ways - 1)
			      << 16);  // num of  ways are used as spram
	/*number of cachelines, taking 2048 sets as an example*/
	npu_llc_write(dev, device, CODA_CACHE_REG_CCUSPCR1,
		      MAX_SETS * spram_num_of_ways - 1);
	val = npu_llc_read(dev, device, CODA_CACHE_REG_CCUSPCR0);
	val |= 0x1;  //enable Spram
	npu_llc_write(dev, device, CODA_CACHE_REG_CCUSPCR0, val);
	npu_llc_write(dev, device, CODA_CACHE_REG_CCUCTCR,
		      0x3);  // enable codacache ip lookups and fill
	npu_llc_write(dev, device, CODA_CACHE_REG_CCUUEDR,
		      0x3);  // enable codacache ip error detection
	npu_llc_write(dev, device, CODA_CACHE_REG_CCUCAOR,
		      0x4);  // enable codacache ip write allocation partial

	return 0;
}

static int npu_llc_interleave_enable(struct nvdla_device *nvdla_dev)
{
	uint32_t value;
	void *base_addr;
	struct device *dev = &nvdla_dev->pdev->dev;

	base_addr = ioremap(0x51810000 + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET, 0x500);
	if (!base_addr) {
		dev_err(dev, "ioremap error\n");
		return -1;
	}

	value = readl(base_addr + 0x324);
	value |= (1 << 0);

	writel(value, base_addr + 0x324);

	iounmap(base_addr);

	return 0;
}

int npu_spram_init(struct nvdla_device *nvdla_dev)
{
	struct resource res_spram;
	struct device_node *node;
	struct device *dev;

	dev = &nvdla_dev->pdev->dev;
	node = of_parse_phandle(dev->of_node, "spram-region", 0);
	if (IS_ERR(node)) {
		dev_err(dev, "Get phandle npu-spram error\n");
		return -ENODEV;
	}

	of_address_to_resource(node, 0, &res_spram);
	dev_info(dev, "spram start addr: 0x%llx, len: 0x%llx\n",
		 res_spram.start, resource_size(&res_spram));
	nvdla_dev->spram_base_addr = res_spram.start;

	if (npu_llc_interleave_enable(nvdla_dev) < 0) {
		dev_err(dev, "npu_llc_interleave_enable error\n");
		return -1;
	}

	if (npu_llc_init(nvdla_dev, MAX_CACHE_SIZE, 0) < 0) {
		dev_err(dev, "npu_llc_init0 error\n");
		return -1;
	}

	if (npu_llc_init(nvdla_dev, MAX_CACHE_SIZE, 1) < 0) {
		dev_err(dev, "npu_llc_init1 error\n");
		return -1;
	}

	spram_start =
		devm_ioremap(&nvdla_dev->pdev->dev, nvdla_dev->spram_base_addr, resource_size(&res_spram));
	if (IS_ERR(spram_start)) {
		dev_err(dev, "npu spram ioremap error\n");
		return -ENODEV;
	}

	return 0;
}

void npu_spram_read(uint32_t addr, uint32_t len, uint8_t *buffer)
{
	uint32_t i;

	for (i = 0; i < len; i++) {
		buffer[i] = readb(spram_start + addr + i);
	}
}

void npu_spram_write(uint32_t addr, uint32_t len, uint8_t *buffer)
{
	uint32_t i;

	for (i = 0; i < len; i++) {
		writeb(buffer[i], spram_start + addr + i);
	}
}
