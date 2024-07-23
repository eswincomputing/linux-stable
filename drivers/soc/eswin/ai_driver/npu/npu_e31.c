// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/elf.h>
#include <linux/of_platform.h>
#include <linux/dma-heap.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/iommu.h>
#include <linux/es_iommu_rsv.h>
#include <asm/io.h>
#include "dla_log.h"
#include "hetero_host.h"
#include "dla_driver.h"

#ifdef DUMP_NIM
static void dump_data(const void *buf, const u32 len)
{
	int i = 0;
	dla_debug("=======================\n");
	for (i = 0; i < len; i++) {
		if (i % 16 == 0) {
			dla_debug("\n0x%04x: ", i);
		}
		dla_debug("%02x ", ((char *)buf)[i]);
	}
	dla_debug("\n");
}
#endif

#define AMMUSSID GENMASK(27, 8)	 // The ssid of write and read operation
#define AMMUSID GENMASK(7, 0)  // The sid of write and read operation

#define E31_MMU_RID_REG_OFFSET 0x3c
#define E31_MMU_WID_REG_OFFSET 0x40
#define E31_STREAMID_CFG_OFFSET 0x108

static inline void npu_e31_sid_cfg(void __iomem *npu_subsys_base, u32 sid)
{
	u32 rdwr_sid_ssid = 0;
	u32 rsidval = 0;
	u32 wsidval = 0;
	/* make the reading sid the same as writing sid, and ssid is fixed to zero */
	rdwr_sid_ssid |= FIELD_PREP(AMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(AMMUSSID, 0);
	writel(rdwr_sid_ssid,
	       npu_subsys_base + NPU_TOP_CSR_OFFSET + E31_MMU_RID_REG_OFFSET);
	writel(rdwr_sid_ssid,
	       npu_subsys_base + NPU_TOP_CSR_OFFSET + E31_MMU_WID_REG_OFFSET);

	rsidval = readl(npu_subsys_base + NPU_TOP_CSR_OFFSET +
			E31_MMU_RID_REG_OFFSET);
	wsidval = readl(npu_subsys_base + NPU_TOP_CSR_OFFSET +
			E31_MMU_WID_REG_OFFSET);

	dla_debug(
		"%s, NPU_TOP_CSR_OFFSET=0x%x, npu_e31: rsid=0x%x, wsid=0x%x\n",
		__func__, NPU_TOP_CSR_OFFSET, rsidval, wsidval);
}

#if NPU_DEV_SIM == NPU_REAL_ENV
static void npu_check_mcu_active(struct nvdla_device *nvdla_dev)
{
	int i;
	for (i = 0; i < NUM_NPU_CORES; i++) {
		/* initialize the node id for all e31 */
		io_write((u8 *)nvdla_dev->e31_mmio_base + NPU_CPU_OFFSET +
				 NPU_CPU_SIZE * i + NPU_DTIM_OFFSET +
				 ADDR_OFFSET(cpu_node_t, node_id),
			 i);
		activate_system(nvdla_dev->e31_mmio_base, i);
		if (i == 0) {
			msleep(3);
		}
	}

	check_system_activated(nvdla_dev->e31_mmio_base);
}

#endif

#define NPU_E31_FW_RSV_IOVA 0x80000000

int npu_e31_load_fw(struct platform_device *pdev, void __iomem *e31_mmio_base)
{
	int retval = 0;
	int err = 0;
	const struct firmware *e31_fw;
	dma_addr_t nim_iova;
	char *fw_virt_base;
	u32 boot_len = 0;
	u8 *e31_boot_virt_base;
	u32 offset;
	u32 boot_dma_addr;
	u32 streamid_cfg;
	struct nvdla_device *nvdla_dev = dev_get_drvdata(&pdev->dev);

	/* config streamid of npu-e31 */
	streamid_cfg = readl(e31_mmio_base + NPU_CTRL_OFFSET +
			     E31_STREAMID_CFG_OFFSET);
	streamid_cfg &= ~(1 << 3);
	writel(streamid_cfg,
	       e31_mmio_base + NPU_CTRL_OFFSET + E31_STREAMID_CFG_OFFSET);
	npu_e31_sid_cfg(e31_mmio_base, WIN2030_SID_NPU_DMA);

	err = request_firmware(&e31_fw, "eic7700_e31_fw", &pdev->dev);
	if (err < 0) {
		dla_error("Eswin e31 request fw error.\n");
		return -EINVAL;
	}

	nim_iova = NPU_E31_FW_RSV_IOVA;
	fw_virt_base = iommu_map_rsv_iova(&pdev->dev, nim_iova, e31_fw->size,
					  GFP_KERNEL, IOMMU_MMIO);
	if (!fw_virt_base) {
		dla_error("iommu map rsv iova for e31 fw error.\n");
		retval = -ENOMEM;
		goto err_map_iova;
	}

	dla_debug("%s, fw_base=0x%px, iova=0x%llx, size=0x%lx.\n", __func__,
		  fw_virt_base, nim_iova, e31_fw->size);
	//copy fw from user context to kernel space
	memcpy((char *)fw_virt_base, (char *)e31_fw->data, e31_fw->size);

	e31_boot_virt_base = find_boot_firmware(fw_virt_base, &boot_len);
	offset = e31_boot_virt_base - (u8 *)fw_virt_base;
	boot_dma_addr = (u32)((size_t)nim_iova + offset);
	dla_debug("fw_virt_base=0x%px, boot_fw=0x%px, boot_dma=%0x, offset=%u.",
		  fw_virt_base, e31_boot_virt_base, boot_dma_addr, offset);

	dma_sync_single_for_device(&pdev->dev, nim_iova, e31_fw->size,
				   DMA_BIDIRECTIONAL);

	load_firmware_to_conv_cpus(fw_virt_base, boot_dma_addr, e31_mmio_base);

	nvdla_dev->e31_fw_virt_base = fw_virt_base;
	nvdla_dev->e31_nim_iova = nim_iova;
	nvdla_dev->e31_fw_size = e31_fw->size;
	retval = 0;
#if NPU_DEV_SIM == NPU_REAL_ENV
	npu_check_mcu_active(nvdla_dev);
#endif

	if (nvdla_dev->e31_fw_virt_base) {
		iommu_unmap_rsv_iova(&pdev->dev, nvdla_dev->e31_fw_virt_base,
				     nvdla_dev->e31_nim_iova,
				     nvdla_dev->e31_fw_size);
		nvdla_dev->e31_fw_virt_base = NULL;
	}

	dla_debug("npu e31 load firmware done.\n");
err_map_iova:
	release_firmware(e31_fw);
	return retval;
}
