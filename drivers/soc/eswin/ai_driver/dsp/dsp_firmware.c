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

#include <linux/dma-mapping.h>
#include <linux/elf.h>
#include <linux/firmware.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include "dsp_main.h"
#include "dsp_firmware.h"

#ifndef OF_BAD_ADDR
#define OF_BAD_ADDR (-1ul)
#endif
struct es_dsp_hw;
int es_dsp_load_fw_segment(struct es_dsp_hw *hw, const void *image,
			   Elf32_Phdr *phdr);

static phys_addr_t dsp_translate_to_cpu(struct es_dsp *dsp, Elf32_Phdr *phdr)
{
#if IS_ENABLED(CONFIG_OF)
	phys_addr_t res;
	__be32 addr = cpu_to_be32((u32)phdr->p_paddr);
	struct device_node *node = of_get_next_child(dsp->dev->of_node, NULL);

	if (!node)
		node = dsp->dev->of_node;

	res = of_translate_address(node, &addr);

	if (node != dsp->dev->of_node)
		of_node_put(node);
	return res;
#else
	return phdr->p_paddr;
#endif
}

static int dsp_load_segment_to_sysmem(struct es_dsp *dsp, Elf32_Phdr *phdr)
{
	phys_addr_t pa = dsp_translate_to_cpu(dsp, phdr);
	struct page *page = pfn_to_page(__phys_to_pfn(pa));
	size_t page_offs = pa & ~PAGE_MASK;
	size_t offs;

	for (offs = 0; offs < phdr->p_memsz; ++page) {
		void *p = kmap(page);
		size_t sz;

		if (!p)
			return -ENOMEM;

		page_offs &= ~PAGE_MASK;
		sz = PAGE_SIZE - page_offs;

		if (offs < phdr->p_filesz) {
			size_t copy_sz = sz;

			if (phdr->p_filesz - offs < copy_sz)
				copy_sz = phdr->p_filesz - offs;

			copy_sz = ALIGN(copy_sz, 4);
			memcpy(p + page_offs,
			       (void *)dsp->firmware->data + phdr->p_offset +
				       offs,
			       copy_sz);
			page_offs += copy_sz;
			offs += copy_sz;
			sz -= copy_sz;
		}

		if (offs < phdr->p_memsz && sz) {
			if (phdr->p_memsz - offs < sz)
				sz = phdr->p_memsz - offs;

			sz = ALIGN(sz, 4);
			memset(p + page_offs, 0, sz);
			page_offs += sz;
			offs += sz;
		}
		kunmap(page);
	}
	dma_sync_single_for_device(dsp->dev, pa, phdr->p_memsz, DMA_TO_DEVICE);
	return 0;
}

static int dsp_load_segment_to_iomem(struct es_dsp *dsp, Elf32_Phdr *phdr)
{
	phys_addr_t pa = dsp_translate_to_cpu(dsp, phdr);
	void __iomem *p = ioremap(pa, phdr->p_memsz);

	if (!p) {
		dev_err(dsp->dev, "couldn't ioremap %pap x 0x%08x\n", &pa,
			(u32)phdr->p_memsz);
		return -EINVAL;
	}

	memcpy_toio(p, (void *)dsp->firmware->data + phdr->p_offset,
		    ALIGN(phdr->p_filesz, 4));

	memset_io(p + ALIGN(phdr->p_filesz, 4), 0,
		  ALIGN(phdr->p_memsz - ALIGN(phdr->p_filesz, 4), 4));

	iounmap(p);
	return 0;
}

static int dsp_load_segment(struct es_dsp *dsp, Elf32_Phdr *phdr)
{
	phys_addr_t pa;

	pa = dsp_translate_to_cpu(dsp, phdr);
	if (pa == (phys_addr_t)OF_BAD_ADDR) {
		dev_err(dsp->dev,
			"device address 0x%08x could not be mapped to host physical address",
			(u32)phdr->p_paddr);
		return -EINVAL;
	}
	dsp_info("loading segment (device 0x%08x) to physical %pap\n",
		 (u32)phdr->p_paddr, &pa);

	if (pfn_valid(__phys_to_pfn(pa)))
		return dsp_load_segment_to_sysmem(dsp, phdr);
	else
		return dsp_load_segment_to_iomem(dsp, phdr);
}

static int dsp_prepare_firmware(struct es_dsp *dsp)
{
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)dsp->firmware->data;

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG)) {
		dev_err(dsp->dev, "bad firmware ELF magic\n");
		return -EINVAL;
	}

	if (ehdr->e_type != ET_EXEC) {
		dev_err(dsp->dev, "bad firmware ELF type\n");
		return -EINVAL;
	}

	if (ehdr->e_machine != 94) {
		dev_err(dsp->dev, "bad firmware ELF machine\n");
		return -EINVAL;
	}

	if (ehdr->e_phoff >= dsp->firmware->size ||
	    ehdr->e_phoff + ehdr->e_phentsize * ehdr->e_phnum >
		    dsp->firmware->size) {
		dev_err(dsp->dev, "bad firmware ELF PHDR information\n");
		return -EINVAL;
	}

	return 0;
}

static int dsp_load_firmware(struct es_dsp *dsp)
{
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)dsp->firmware->data;
	int i;
	int rc;

	for (i = 0; i < ehdr->e_phnum; ++i) {
		Elf32_Phdr *phdr = (void *)dsp->firmware->data + ehdr->e_phoff +
				   i * ehdr->e_phentsize;

		/* Only load non-empty loadable segments, R/W/X */
		if (!(phdr->p_type == PT_LOAD &&
		      (phdr->p_flags & (PF_X | PF_R | PF_W)) &&
		      phdr->p_memsz > 0))
			continue;

		if (phdr->p_offset >= dsp->firmware->size ||
		    phdr->p_offset + phdr->p_filesz > dsp->firmware->size) {
			dev_err(dsp->dev,
				"bad firmware ELF program header entry %d\n",
				i);
			return -EINVAL;
		}

		dsp_info("%s loading segment %d\n", __func__, i);

		rc = es_dsp_load_fw_segment(
			dsp->hw_arg, (const void *)dsp->firmware->data, phdr);

		if (rc != 0) {
			rc = dsp_load_segment(dsp, phdr);
		}

		if (rc < 0)
			return rc;
	}
	return 0;
}

void dsp_release_firmware(struct es_dsp *dsp)
{
	if (dsp->firmware) {
		release_firmware(dsp->firmware);
		dsp->firmware = NULL;
	}
}

int dsp_request_firmware(struct es_dsp *dsp)
{
	int ret;

	if (!dsp->firmware) {
		ret = request_firmware(&dsp->firmware, dsp->firmware_name,
				       dsp->dev);
		dsp_debug("%s, after request_firmware, ret=%d\n", __func__,
			  ret);
		if (ret < 0)
			return ret;

		ret = dsp_prepare_firmware(dsp);
		if (ret < 0)
			goto err;
	}

	ret = dsp_load_firmware(dsp);
err:
	if (ret < 0)
		dsp_release_firmware(dsp);
	return ret;
}

MODULE_LICENSE("GPL");
