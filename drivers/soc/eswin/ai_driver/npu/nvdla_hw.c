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
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <drm/drm_prime.h>
#include <linux/mailbox_client.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <nvdla_interface.h>
#include <nvdla_linux.h>
#include <linux/reset.h>
#include <linux/pm_runtime.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include <dt-bindings/interconnect/eswin,win2030.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <linux/win2030_noc.h>
#include <linux/version.h>
#include <asm/cacheflush.h>
#include <linux/iommu.h>
#include <linux/es_iommu_rsv.h>
#include "dla_interface.h"
// TODO(yuzaiqiang) The header files dla_interface.h and llc_spram.h both define the same
// macro CACHE_LINE_SIZE, resulting in a riscv compilation error.
// To resolve the compilation issue, we are temporarily using '#undef
#undef CACHE_LINE_SIZE
#include <llc_spram.h>
#include "dla_log.h"
#include "dla_engine.h"
#include "dla_engine_internal.h"
#include "npu_spram.h"
#include "dla_driver.h"
#include "npu_top_csr.h"
#include "edma.h"
#include "debug.h"
#include "internal_interface.h"
#include "conv_regs.h"
#include "hetero_host.h"
#include "dla_buffer.h"
#include "nvdla_lowlevel.h"
#include "mailbox_regs.h"

struct reg_desc_t {
	unsigned long addr;
	unsigned long len_or_val;
};

typedef union {
	struct {
		u32 cvif_noc_arqos : 4;
		u32 cvif_noc_awqos : 4;
		u32 e31_noc_arqos : 4;
		u32 e31_noc_awqos : 4;
		u32 edma_noc_arqos : 4;
		u32 edma_noc_awqos : 4;
		u32 mclf_noc_arqos : 4;
		u32 mclf_noc_awqos : 4;
	};
	u32 dw;
} npu_qos_0;

typedef union {
	struct {
		u32 edma_qos_sel : 1;
		u32 llc0_qos_sel : 1;
		u32 llc1_qos_sel : 1;
		u32 Reserved0 : 5;
		u32 npu_lnoc_llc1_reg_arqos : 4;
		u32 npu_lnoc_llc1_reg_awqos : 4;
		u32 Reserved1 : 16;
	};
	u32 dw;
} npu_qos_sel;

static struct reg_desc_t reg_desc_hd;

void npu_hw_init(struct nvdla_device *nvdla_dev);
#define NPU_TOP_NPU_CSR_OFFSET 0x198000UL

void npu_dma_sid_cfg(void __iomem *npu_subsys_base, u32 sid)
{
	u32 rdwr_sid_ssid = 0;
	u32 rsidval = 0;
	u32 wsidval = 0;
	/* make the reading sid the same as writing sid, and ssid is fixed to zero */
	rdwr_sid_ssid |= FIELD_PREP(NPU_DMA_SID, sid);
	rdwr_sid_ssid |= FIELD_PREP(NPU_DMA_SSID, 0);
	writel(rdwr_sid_ssid, npu_subsys_base + NPU_TOP_NPU_CSR_OFFSET +
				      NPU_DMA_MMU_RID_REG_OFFSET);
	writel(rdwr_sid_ssid, npu_subsys_base + NPU_TOP_NPU_CSR_OFFSET +
				      NPU_DMA_MMU_WID_REG_OFFSET);

	rsidval = readl(npu_subsys_base + NPU_TOP_NPU_CSR_OFFSET +
			NPU_DMA_MMU_RID_REG_OFFSET);
	wsidval = readl(npu_subsys_base + NPU_TOP_NPU_CSR_OFFSET +
			NPU_DMA_MMU_WID_REG_OFFSET);

	dla_info(
		"%s, NPU_TOP_CSR_OFFSET=0x%lx, npu_dma: rsid=0x%x, wsid=0x%x\n",
		__FUNCTION__, NPU_TOP_NPU_CSR_OFFSET, rsidval, wsidval);
}

void npu_tbu_power(struct device *dev, bool flag)
{
	win2030_tbu_power(dev, flag);
}

void *npu_alloc_dma_addr(struct win_executor *executor, size_t size,
			 dma_addr_t *dma_handle, int i, gfp_t gfp)
{
	struct nvdla_device *nvdla_dev = executor->engine->nvdla_dev;
	struct device *dev = &nvdla_dev->pdev->dev;

	return dma_alloc_coherent(dev, size, dma_handle, gfp);
}

void npu_free_dma_addr(struct win_executor *executor, int i)
{
	struct nvdla_device *nvdla_dev = executor->engine->nvdla_dev;
	struct device *dev = &nvdla_dev->pdev->dev;

	dma_free_coherent(dev, executor->prog_data_size[i],
			  executor->prog_data_buf_bobj[i],
			  executor->dma_addr[i]);
}

static void npu_e31_hw_lock_reset(struct nvdla_device *ndev)
{
#define MUTEX_BASE_ADDR 0x51820000
#define MUTEX_UNIT_SIZE 4

	uint32_t ret_token_id = 0;
	unsigned long hw_lock_addr = ndev->numa_id * NPU_DIE_REG_OFFSET + MUTEX_BASE_ADDR + 1 * MUTEX_UNIT_SIZE;
	void *hw_lock_virt_addr = NULL;

	hw_lock_virt_addr = devm_ioremap(&ndev->pdev->dev, hw_lock_addr, 8);

	ret_token_id = readl(hw_lock_virt_addr);

	if (ret_token_id != 0) {
		writel(0, hw_lock_virt_addr);
	}

	devm_iounmap(&ndev->pdev->dev, hw_lock_virt_addr);

	return;
}

/*/sys/devices/platform/soc/51c00000.nvdla-controller/reg*/

int npu_clk_reset_print(struct platform_device *pdev, int numa_id)
{
	void *reset_base_addr;
	uint32_t reg_val1, reg_val2, reg_val3, reg_val5;

	reset_base_addr = devm_ioremap(&pdev->dev, NPU_CFG_BASE_ADDR + numa_id * NPU_DIE_REG_OFFSET, 0x500);
	if (IS_ERR(reset_base_addr)) {
		dev_err(&pdev->dev, "reset base addr ioremap error\n");
		return -ENODEV;
	}

	reg_val1 = readl(reset_base_addr + NPU_ACLK_CTRL);

	//npu_llc_clken
	reg_val2 = readl(reset_base_addr + NPU_LLC_CTRL);

	//npu_core_clken
	reg_val3 = readl(reset_base_addr + NPU_CORE_CTRL);

	//npu_xxx_rstn
	reg_val5 = readl(reset_base_addr + NPU_RST_CTRL);

	dla_debug(
		"[0x178]=0x%08x [0x17c]=0x%08x [0x180]=0x%08x [0x418]=0x%08x\n",
		reg_val1, reg_val2, reg_val3, reg_val5);

	devm_iounmap(&pdev->dev, reset_base_addr);

	return 0;
}

static int npu_e31_dev_reset(struct nvdla_device *nvdla_dev)
{
	int ret = 0;

	/*reset e31 core*/
	ret = reset_control_assert(nvdla_dev->rstc_e31_core);
	WARN_ON(0 != ret);

	/*reset e31 uart hw mutext*/
	npu_e31_hw_lock_reset(nvdla_dev);
	return 0;
}

int npu_dev_reset(struct nvdla_device *nvdla_dev)
{
	int ret = 0;

	/*reset npu e31*/
	npu_e31_dev_reset(nvdla_dev);

	msleep(10);
	/*reset npu core*/
	ret = npu_core_rst(nvdla_dev->numa_id, false);
	if (ret) {
		dev_err(&nvdla_dev->pdev->dev, "npu_core_rst fail,error: %d.\n",
			ret);
		return ret;
	}

	/*reset npu cfg*/
	ret = npu_cfg_rst(nvdla_dev->numa_id, false);
	if (ret) {
		dev_err(&nvdla_dev->pdev->dev, "npu_core_rst fail,error: %d.\n",
			ret);
		return ret;
	}
	msleep(10);

	ret = npu_cfg_rst(nvdla_dev->numa_id, true);
	if (ret) {
		dev_err(&nvdla_dev->pdev->dev, "npu_cfg_rst fail,error: %d\n",
			ret);
		return ret;
	}

	ret = npu_core_rst(nvdla_dev->numa_id, true);
	if (ret) {
		dev_err(&nvdla_dev->pdev->dev, "npu_core_rst fail,error: %d\n",
			ret);
		return ret;
	}

	reset_control_deassert(nvdla_dev->rstc_e31_core);

	return 0;
}


int npu_dev_assert(struct nvdla_device *nvdla_dev)
{
	int ret = 0;

	ret = reset_control_assert(nvdla_dev->rstc_e31_core);
	WARN_ON(0 != ret);

	/*reset npu core*/
	ret = npu_core_rst(0, false);
	if (ret) {
		dev_err(&nvdla_dev->pdev->dev, "npu_core_rst fail,error: %d.\n",
			ret);
		return ret;
	}

	/*reset npu cfg*/
	ret = npu_cfg_rst(0, false);
	if (ret) {
		dev_err(&nvdla_dev->pdev->dev, "npu_core_rst fail,error: %d.\n",
			ret);
		return ret;
	}

	return 0;
}



int npu_init_reset(struct nvdla_device *nvdla_dev)
{
	struct platform_device *pdev = nvdla_dev->pdev;

	npu_dev_reset(nvdla_dev);
	npu_clk_reset_print(pdev, nvdla_dev->numa_id);

	return 0;
}

static ssize_t show_reg_val(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	void *base_addr;
	uint32_t reg_val;

	if (reg_desc_hd.addr == 0) {
		sprintf(buf, "err:addr not telled!\n");
		goto out;
	}

	base_addr = ioremap(reg_desc_hd.addr, 4);
	if (IS_ERR(base_addr)) {
		sprintf(buf, "err:base addr ioremap error!\n");
		goto out;
	}

	reg_val = readl(base_addr);

	printk("[0x%lx]=0x%08x\n", reg_desc_hd.addr, reg_val);

	sprintf(buf, "[0x%lx]=0x%08x\n", reg_desc_hd.addr, reg_val);

	iounmap(base_addr);

out:
	return strlen(buf);
}

static ssize_t store_reg_val(struct device *d, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	char *p_addr, *p_len;
	void *v_addr;
	int ret = 0;
	int i = 0;
	char buf_in[128] = { 0 };
	uint32_t reg_val;

	if (count == 0 || count > 128) {
		printk("err:count=%ld\n", count);
		return count;
	}

	memcpy(buf_in, buf, count);
	if (buf_in[count - 1] == '\n') {
		buf_in[count - 1] = 0;
	}

	p_addr = strstr(buf_in, "0x");
	if (p_addr == NULL) {
		p_addr = strstr(buf_in, "0X");
	}
	if (p_addr == NULL) {
		dla_error("can not find addr_val!\n");
		return count;
	}

	p_len = strstr(p_addr, " ");
	if (p_len == NULL) {
		dla_error("can not find len!\n");
		return count;
	}
	*p_len = 0;
	p_len++;

	printk("p_addr=%s!\n", p_addr);
	printk("p_len=%s!\n", p_len);

	ret = kstrtoul(p_addr, 0, &reg_desc_hd.addr);
	if (ret) {
		printk("%s is not in hex or decimal form.\n", p_addr);
		return count;
	}

	ret = kstrtoul(p_len, 0, &reg_desc_hd.len_or_val);
	if (ret) {
		printk("%s is not in hex or decimal form.\n", p_len);
		return count;
	}
	printk("input 0x%0lx--0x%lx\n", reg_desc_hd.addr,
	       reg_desc_hd.len_or_val);

	if (buf_in[0] == 'w') {
		v_addr = ioremap(reg_desc_hd.addr, 4);
		if (IS_ERR(v_addr)) {
			dla_error("ioremap error\n");
		}
		writel(reg_desc_hd.len_or_val, v_addr);
		msleep(5);
		reg_val = readl(v_addr);
		printk("read: 0x%0lx--0x%0x\n", reg_desc_hd.addr, reg_val);

		iounmap(v_addr);
	} else if (buf_in[0] == 'r') {
		v_addr = ioremap(reg_desc_hd.addr, reg_desc_hd.len_or_val);
		if (IS_ERR(v_addr)) {
			printk("ioremap error\n");
		}
		for (i = 0; i < reg_desc_hd.len_or_val; i++) {
			reg_val = readl(v_addr + i * 4);
			printk("read: 0x%0lx--0x%0x\n",
			       reg_desc_hd.addr + i * 4, reg_val);
		}
		iounmap(v_addr);
	} else {
		printk("format err.\n");
		memset(&reg_desc_hd, 0, sizeof(reg_desc_hd));
	}

	return count;
}

int npu_clk_reset_print(struct platform_device *pdev, int numa_id);

int dla_noc_sideband_query(void)
{
	int ret = 0;
	int noc_falut = 0;

	ret = win2030_noc_sideband_mgr_query(SBM_NPU_SNOC_SP0);
	if (ret != 1) {
		dla_error("warning:SBM_NPU_SNOC_SP0 state:%d\n", ret);
		noc_falut = -1;
	}

	ret = win2030_noc_sideband_mgr_query(SBM_NPU_SNOC_SP1);
	if (ret != 1) {
		dla_error("warning:SBM_NPU_SNOC_SP1 state:%d\n", ret);
		noc_falut = -1;
	}

	ret = win2030_noc_sideband_mgr_query(SBM_SNOC_NPU);
	if (ret != 1) {
		dla_error("warning:SBM_SNOC_NPU state:%d\n", ret);
		noc_falut = -1;
	}

	ret = win2030_noc_sideband_mgr_query(SBM_CNOC_NPU);
	if (ret != 1) {
		dla_error("warning:SBM_CNOC_NPU state:%d\n", ret);
		noc_falut = -1;
	}
	return noc_falut;
}

static int npu_restart_init(struct nvdla_device *nvdla_dev)
{
	int ret;

	npu_dma_sid_cfg(nvdla_dev->base, WIN2030_SID_NPU_DMA);
	npu_hw_init(nvdla_dev);
	ret = npu_e31_load_fw(nvdla_dev);

	return ret;
}

int npu_hardware_reset(struct nvdla_device *nvdla_dev)
{
	int try_cnt = 10;
	int ret = 0;

	while (--try_cnt) {
		ret = dla_noc_sideband_query();
		if (ret) {
			msleep(200);
			continue;
		} else {
			break;
		}
	}

	if ((try_cnt == 0) && (ret != 0)) {
		dla_error("err:npu noc is busy,reset failed.\n");
		return -1;
	}
	npu_dev_reset(nvdla_dev);
	return 0;
}

static ssize_t store_reset_hand(struct device *d, struct device_attribute *attr,
				const char *buf, size_t count)
{
	char buf_in[128] = { 0 };
	char *ptr = NULL;
	int is_reset = 0;
	struct nvdla_device *nvdla_dev = dev_get_drvdata(d);
	struct win_engine *engine = npu_get_win_engine(nvdla_dev);

	int ret, i;

	if (count == 0 || count > 128) {
		dla_error("err:count=%ld\n", count);
		return count;
	}

	memcpy(buf_in, buf, count);

	ptr = strstr(buf_in, "reset");
	if (ptr != NULL) {
		is_reset = 1;
	}

	ptr = strstr(buf_in, "npu");
	mutex_lock(&engine->reset_mutex);
	if (ptr != NULL) {
		if (is_reset == 1) {
			engine->engine_is_alive = false;
			npu_drop_all_frame(nvdla_dev, false);
			memset(engine->host_node, 0, sizeof(host_node_t));
			memset(nvdla_dev->emission_base, 0,
			       E31_EMISSION_DTIM_SIZE);
			memset(nvdla_dev->program_base, 0,
			       E31_PROGRAM_DTIM_SIZE);

			engine->tiktok = 0;

			ret = npu_hardware_reset(nvdla_dev);
			if (ret) {
				dla_error("hardware reset err, ret=%d.\n", ret);
				goto exit;
			}

			npu_tbu_power(&nvdla_dev->pdev->dev, true);

			clk_prepare_enable(nvdla_dev->mbox_pclk);
			clk_prepare_enable(nvdla_dev->mbox_pclk_device);
			reset_control_reset(nvdla_dev->mbox_rst_device);
			reset_control_reset(nvdla_dev->mbox_rst);
			writel(0x1,
			       nvdla_dev->mbox_rx_base + MBOX_NPU_INT_OFFSET);
			writel(ESWIN_MAILBOX_NPU_LOCK_BIT,
			       nvdla_dev->mbox_rx_base + MBOX_NPU_WR_LOCK);
			npu_restart_init(nvdla_dev);

			host_ipc_initialize(
				(u64)((struct win_engine
					       *)(nvdla_dev->win_engine))
					->host_node,
				(u32)((struct win_engine
					       *)(nvdla_dev->win_engine))
					->host_node_iova,
				(u64)nvdla_dev->emission_base,
				(u64)nvdla_dev->program_base);

			reset_uart_mutex(nvdla_dev);
			for (i = 0; i < NUM_NPU_CORES; i++) {
				/* initialize the node id for all e31 */
				io_write((u8 *)nvdla_dev->e31_mmio_base +
						 NPU_CPU_OFFSET +
						 NPU_CPU_SIZE * i +
						 NPU_DTIM_OFFSET +
						 ADDR_OFFSET(cpu_node_t,
							     node_id),
					 i);
				activate_system(nvdla_dev->e31_mmio_base, i);
				if (i == 0) {
					msleep(3);
				}
			}

			ret = check_system_activated(nvdla_dev->e31_mmio_base);
			if (ret == false) {
				ret = check_system_activated(
					nvdla_dev->e31_mmio_base);
			}
			if (nvdla_dev->e31_fw_virt_base) {
				iommu_unmap_rsv_iova(
					d, nvdla_dev->e31_fw_virt_base,
					nvdla_dev->e31_nim_iova,
					nvdla_dev->e31_fw_size);
			}
			if (ret == false) {
				dla_error("e31 not bootup, error.\n");
				goto exit;
			}
			engine->engine_is_alive = true;
		} else {
			dla_error("err:input err.\n");
		}
		goto exit;
	}

exit:
	npu_clk_reset_print(nvdla_dev->pdev, nvdla_dev->numa_id);
	mutex_unlock(&engine->reset_mutex);
	return count;
}

static ssize_t npu_drop_frame(struct device *d, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct nvdla_device *nvdla_dev = dev_get_drvdata(d);

	if (!nvdla_dev) {
		return -ENODEV;
	}
	npu_drop_all_frame(nvdla_dev, true);
	return count;
}

static ssize_t npu_dump_dtim(struct device *d, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct nvdla_device *nvdla_dev = dev_get_drvdata(d);
	struct win_engine *engine;
	int i;

	if (!nvdla_dev) {
		return -ENODEV;
	}
	engine = npu_get_win_engine(nvdla_dev);
	if (engine == NULL) {
		return -ENODEV;
	}

	if (engine->master_mem) {
		memcpy(engine->master_mem, engine->master_shm,
		       E31_EMISSION_DTIM_SIZE);
	}

	if (engine->aux_mem) {
		memcpy(engine->aux_mem, engine->aux_shm, E31_PROGRAM_DTIM_SIZE);
	}
	for (i = 0; i < NUM_MAJOR_CORES; i++) {
		if (engine->major_mem[i]) {
			memcpy(engine->major_mem[i], engine->major_shm[i],
			       E31_MAJOR_DTIM_SIZE);
		}
	}
	dump_dtim_to_file(engine, 3);
	return count;
}

static DEVICE_ATTR(reg, 0644, show_reg_val, store_reg_val);
static DEVICE_ATTR(reset, 0644, NULL, store_reset_hand);
static DEVICE_ATTR(drop_frame, 0644, NULL, npu_drop_frame);
static DEVICE_ATTR(dump_dtim, 0644, NULL, npu_dump_dtim);

static const struct attribute *npu_attributes[] = {
	&dev_attr_reg.attr, &dev_attr_reset.attr, &dev_attr_drop_frame.attr,
	&dev_attr_dump_dtim.attr, NULL
};

int npu_create_sysfs(struct platform_device *pdev)
{
	int ret;

	ret = sysfs_create_files(&pdev->dev.kobj, npu_attributes);
	return ret;
}

int npu_remove_sysfs(struct platform_device *pdev)
{
	sysfs_remove_files(&pdev->dev.kobj, npu_attributes);
	return 0;
}

int npu_spram_get(struct nvdla_device *nvdla_dev)
{
	struct dla_buffer_object *spram_bobj = NULL;
	uint32_t drv_spram_size;
	int err = 0;
	int numa_id = nvdla_dev->numa_id;

	err = llc_user_register(&nvdla_dev->pdev->dev);
	if (err) {
		dla_error("%s %d llc_user_register failed!,err=%d\n", __func__,
			  __LINE__, err);
		return -1;
	}

	err = llc_spram_avail_size(nvdla_dev->numa_id, &drv_spram_size);
	if (err) {
		dla_error("%s %d llc_spram_avail_size failed!,err=%d\n",
			  __func__, __LINE__, err);
		return -1;
	}

	if (of_property_read_u32(nvdla_dev->pdev->dev.of_node, "spram-size",
				 &nvdla_dev->spram_size)) {
		dla_error("%s %d get spram_size failed!\n", __func__, __LINE__);
		return -1;
	}

	if (drv_spram_size < nvdla_dev->spram_size) {
		dla_error(
			"%s %d spram size(0x%x) from spram driver is smaller than size(0x%x) in dts\n",
			__func__, __LINE__, drv_spram_size,
			nvdla_dev->spram_size);
		return -1;
	}
	dla_info("spram_size=0x%x\n", nvdla_dev->spram_size);

	spram_bobj = dla_alloc_dmabuf(nvdla_dev->spram_size,
				      numa_id ? ES_MEM_ALLOC_SPRAM_DIE1 : ES_MEM_ALLOC_SPRAM_DIE0);
	if (spram_bobj < 0) {
		dla_error(
			"spram_dma_fd dev_mem_alloc failed!,spram_size=0x%x\n",
			nvdla_dev->spram_size);
		return -1;
	}

	err = dla_attach_dmabuf(spram_bobj, &nvdla_dev->pdev->dev);
	if (err) {
		dla_error("dla_attach_dmabuf failed!,err=%d\n", err);
		dla_release_bobj(spram_bobj);
		return err;
	}
	dla_debug("%s, %d, spram file count=%ld.\n", __func__, __LINE__,
		  file_count(spram_bobj->dmabuf->file));
	nvdla_dev->spram_base_addr = sg_phys(spram_bobj->attach->sgt->sgl);
	dla_debug("spram phy_addr=0x%llx\n", nvdla_dev->spram_base_addr);

	nvdla_dev->spram_bobj = spram_bobj;

	return 0;
}

int npu_spram_release(struct nvdla_device *nvdla_dev)
{
	dla_debug("%s, %d, spram file count=%ld.\n", __func__, __LINE__,
		  file_count(nvdla_dev->spram_bobj->dmabuf->file));
	dla_release_bobj(nvdla_dev->spram_bobj);

	return 0;
}

int send_mbx_msg_to_e31(struct win_engine *engine, msg_payload_t payload)
{
	unsigned long flags;
	struct nvdla_device *ndev = (struct nvdla_device *)engine->nvdla_dev;
	int count = 0;
	u32 tmp_data;

	spin_lock_irqsave(&ndev->mbox_lock, flags);
	while (true) {
		if (count > 3) {
			spin_unlock_irqrestore(&ndev->mbox_lock, flags);
			return -EBUSY;
		}
		writel(ndev->mbox_tx_lock_bit,
		       ndev->mbox_tx_base + MBOX_NPU_WR_LOCK);
		if (!(readl(ndev->mbox_tx_base + MBOX_NPU_WR_LOCK) &
		      ndev->mbox_tx_lock_bit) ||
		    (readl(ndev->mbox_tx_base + MBOX_NPU_FIFO_OFFSET) &
		     BIT_ULL(0))) {
			udelay(10);
			count++;
			continue;
		}
		break;
	}

	tmp_data = ((u32)payload.type | (u32)payload.param << 8 |
		    (u32)payload.lparam << 16);
	writel(tmp_data, ndev->mbox_tx_base + MBOX_NPU_WR_DATA0_OFFSET);

	tmp_data = (u32)BIT(31);
	writel(tmp_data, ndev->mbox_tx_base + MBOX_NPU_WR_DATA1_OFFSET);

	writel(ndev->mbox_tx_irq_bit, ndev->mbox_tx_base + MBOX_NPU_INT_OFFSET);

	writel(0x0, ndev->mbox_tx_base + MBOX_NPU_WR_LOCK);
	spin_unlock_irqrestore(&ndev->mbox_lock, flags);

	return 0;
}

static int npu_enable_mbox_clock(struct nvdla_device *ndev)
{
	int ret;
	ret = clk_prepare_enable(ndev->mbox_pclk);
	if (ret) {
		dev_err(&ndev->pdev->dev,
			"failed to enable host mailbox pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(ndev->mbox_pclk_device);
	if (ret) {
		dev_err(&ndev->pdev->dev,
			"failed to enable device mailbox pclk: %d\n", ret);
		clk_disable_unprepare(ndev->mbox_pclk);
	}
	return ret;
}

static int npu_disable_mbox_clock(struct nvdla_device *ndev)
{
	clk_disable_unprepare(ndev->mbox_pclk_device);
	clk_disable_unprepare(ndev->mbox_pclk);
	return 0;
}

int npu_init_mbox(struct nvdla_device *nvdla_dev)
{
	reset_control_reset(nvdla_dev->mbox_rst);
	reset_control_reset(nvdla_dev->mbox_rst_device);

	writel(0x1, nvdla_dev->mbox_rx_base + MBOX_NPU_INT_OFFSET);
	writel(ESWIN_MAILBOX_NPU_LOCK_BIT,
	       nvdla_dev->mbox_rx_base + MBOX_NPU_WR_LOCK);

	spin_lock_init(&nvdla_dev->mbox_lock);
	dla_debug("%s, done\n", __func__);
	return 0;
}

int npu_uninit_mbox(struct nvdla_device *ndev)
{
	int ret;
	ret = reset_control_assert(ndev->mbox_rst);
	WARN_ON(ret != 0);
	ret = reset_control_assert(ndev->mbox_rst_device);
	dla_info("npu_uninit_mbox done.\n");
	return 0;
}

int npu_put_dt_resources(struct nvdla_device *ndev)
{
	struct platform_device *mbox_pdev = ndev->mbox_pdev;

	devm_free_irq(&mbox_pdev->dev, ndev->mbox_irq, ndev);

	clk_put(ndev->mbox_pclk);
	clk_put(ndev->mbox_pclk_device);

	reset_control_put(ndev->mbox_rst_device);
	reset_control_put(ndev->mbox_rst);

	dev_set_drvdata(&mbox_pdev->dev, NULL);
	ndev->mbox_pdev = NULL;
	return 0;
}

int npu_dt_node_resources(struct nvdla_device *nvdla_dev)
{
	struct platform_device *pdev = nvdla_dev->pdev;
	struct device_node *mbox_node = NULL;
	struct platform_device *mbox_pdev = NULL;
	int ret;
	struct resource *res;

	if (of_property_read_u32(pdev->dev.of_node, "numa-node-id",
				 &nvdla_dev->numa_id)) {
		nvdla_dev->numa_id = 0;
	}

	nvdla_dev->e31_core_clk = devm_clk_get(&pdev->dev, "e31_core_clk");
	if (IS_ERR(nvdla_dev->e31_core_clk)) {
		ret = PTR_ERR(nvdla_dev->e31_core_clk);
		nvdla_dev->e31_core_clk = NULL;
		dev_err(&pdev->dev, "failed to get core_clk: %d\n", ret);
		return ret;
	}
	nvdla_dev->core_clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(nvdla_dev->core_clk)) {
		ret = PTR_ERR(nvdla_dev->core_clk);
		nvdla_dev->core_clk = NULL;
		dev_err(&pdev->dev, "failed to get core clk: %d.\n", ret);
		return ret;
	}
	nvdla_dev->cfg_clk = devm_clk_get(&pdev->dev, "cfg_clk");
	if (IS_ERR(nvdla_dev->cfg_clk)) {
		ret = PTR_ERR(nvdla_dev->cfg_clk);
		nvdla_dev->cfg_clk = NULL;
		dev_err(&pdev->dev, "failed to get cfg clk: %d.\n", ret);
		return ret;
	}
	nvdla_dev->mux_u_npu_core_3mux1_gfree = devm_clk_get(&pdev->dev, "mux_u_npu_core_3mux1_gfree");
	if (IS_ERR(nvdla_dev->mux_u_npu_core_3mux1_gfree)) {
		ret = PTR_ERR(nvdla_dev->mux_u_npu_core_3mux1_gfree);
		nvdla_dev->mux_u_npu_core_3mux1_gfree = NULL;
		dev_err(&pdev->dev, "failed to get mux_u_npu_core_3mux1_gfree clk: %d.\n", ret);
		return ret;
	}

	nvdla_dev->fixed_rate_clk_spll2_fout2 = devm_clk_get(&pdev->dev, "fixed_rate_clk_spll2_fout2");
	if (IS_ERR(nvdla_dev->fixed_rate_clk_spll2_fout2)) {
		ret = PTR_ERR(nvdla_dev->fixed_rate_clk_spll2_fout2);
		nvdla_dev->fixed_rate_clk_spll2_fout2 = NULL;
		dev_err(&pdev->dev, "failed to get fixed_rate_clk_spll2_fout2 clk: %d.\n", ret);
		return ret;
	}

	nvdla_dev->fixed_rate_clk_spll1_fout1 = devm_clk_get(&pdev->dev, "fixed_rate_clk_spll1_fout1");
	if (IS_ERR(nvdla_dev->fixed_rate_clk_spll1_fout1)) {
		ret = PTR_ERR(nvdla_dev->fixed_rate_clk_spll1_fout1);
		nvdla_dev->fixed_rate_clk_spll1_fout1 = NULL;
		dev_err(&pdev->dev, "failed to get fixed_rate_clk_spll1_fout1 clk: %d.\n", ret);
		return ret;
	}

	//nvdla_dev->rstc_e31_core = devm_reset_control_get_optional_exclusive(
	nvdla_dev->rstc_e31_core = devm_reset_control_get_optional(
		&pdev->dev, "e31_core");
	if (IS_ERR_OR_NULL(nvdla_dev->rstc_e31_core)) {
		dev_err(&nvdla_dev->pdev->dev,
			"Failed to e31_core reset handle\n");
		return -EFAULT;
	}

	if (device_property_read_string(&pdev->dev, "firmware-name", &nvdla_dev->e31_fw_name)) {
		dev_err(&nvdla_dev->pdev->dev, "Failed to get e31 firmware name\n");
		return -EFAULT;
	}

	mbox_node = of_parse_phandle(pdev->dev.of_node, "npu_mbox", 0);
	if (mbox_node == NULL) {
		dev_err(&pdev->dev, "npu node have not mailbox node, err.\n");
		return -EINVAL;
	}

	mbox_pdev = of_find_device_by_node(mbox_node);
	if (mbox_pdev == NULL) {
		dev_err(&pdev->dev, "cannot find npu mbox platform dev, err\n");
		return -EINVAL;
	}
	of_node_put(mbox_node);
	nvdla_dev->mbox_pdev = mbox_pdev;

	//platform_set_drvdata(mbox_pdev, nvdla_dev);
	/* use eswin mailbox to send msg and receive msg */
	if (of_property_read_u32(mbox_node, "lock-bit",
				 &nvdla_dev->mbox_tx_lock_bit)) {
		dev_err(&pdev->dev, "failed to get lock_bit: %d\n", ret);
		return -ENXIO;
	}

	if (of_property_read_u32(mbox_node, "irq-bit",
				 &nvdla_dev->mbox_tx_irq_bit)) {
		dev_err(&mbox_pdev->dev, "failed to get irq_bit: %d\n", ret);
		return -ENXIO;
	}

	res = platform_get_resource(mbox_pdev, IORESOURCE_MEM, 0);
	if (!res) {
		return -ENODEV;
	}
	nvdla_dev->mbox_tx_base =
		devm_ioremap(&mbox_pdev->dev, res->start, resource_size(res));
	if (IS_ERR(nvdla_dev->mbox_tx_base)) {
		return PTR_ERR(nvdla_dev->mbox_tx_base);
	}

	res = platform_get_resource(mbox_pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -ENODEV;

	nvdla_dev->mbox_rx_base =
		devm_ioremap(&mbox_pdev->dev, res->start, resource_size(res));
	if (IS_ERR(nvdla_dev->mbox_rx_base))
		return PTR_ERR(nvdla_dev->mbox_rx_base);
	nvdla_dev->mbox_pclk =
		devm_clk_get(&mbox_pdev->dev, "pclk_mailbox_host");
	if (IS_ERR(nvdla_dev->mbox_pclk)) {
		ret = PTR_ERR(nvdla_dev->mbox_pclk);
		dev_err(&mbox_pdev->dev,
			"failed to get host mailbox clock: %d\n", ret);
		return ret;
	}
	nvdla_dev->mbox_pclk_device =
		devm_clk_get(&mbox_pdev->dev, "pclk_mailbox_device");
	if (IS_ERR(nvdla_dev->mbox_pclk_device)) {
		ret = PTR_ERR(nvdla_dev->mbox_pclk_device);
		dev_err(&mbox_pdev->dev,
			"failed to get device mailbox clock: %d\n", ret);
		return ret;
	}

	nvdla_dev->mbox_rst = devm_reset_control_get_optional(
		&mbox_pdev->dev, "rst");
	if (IS_ERR(nvdla_dev->mbox_rst)) {
		ret = -ENODEV;
		dev_err(&mbox_pdev->dev, "failed to get rst controller.\n");
		return ret;
	}
	nvdla_dev->mbox_rst_device = devm_reset_control_get_optional(
		&mbox_pdev->dev, "rst_device");
	if (IS_ERR(nvdla_dev->mbox_rst_device)) {
		ret = PTR_ERR(nvdla_dev->mbox_rst_device);
		return ret;
	}
	nvdla_dev->mbox_irq = platform_get_irq(mbox_pdev, 0);
	if (nvdla_dev->mbox_irq < 0) {
		ret = nvdla_dev->mbox_irq;
		return ret;
	}
	ret = devm_request_threaded_irq(&mbox_pdev->dev, nvdla_dev->mbox_irq,
					npu_mbox_irq, NULL, IRQF_ONESHOT,
					dev_name(&mbox_pdev->dev), nvdla_dev);
	if (ret < 0) {
		return ret;
	}

	return 0;
}
#define NPU_TOP_S_BASE_ADDR_1 0x198000UL
#define NPU_MAC_S_BASE_ADDR_1 0x190000UL
#define NPU_RMDA_WRAPPER_S_BASE_ADDR_1 0x128000UL

void npu_hw_init(struct nvdla_device *nvdla_dev)
{
	pp_status_t pp_status;
	npu_qos_0 qos_0;
	npu_qos_sel qos_sel;
	/* reset the uart mutex lock */
	reset_uart_mutex(nvdla_dev);

	dla_reg_write(nvdla_dev, NPU_TOP_S_BASE_ADDR_1 + NPU_TOP_CTRL, 0);
	dla_reg_write(nvdla_dev, NPU_TOP_S_BASE_ADDR_1 + NPU_TOP_INT_ENABLE, 0);

	dla_disable_intr(nvdla_dev, -1u);

	pp_status.value = dla_reg_read(nvdla_dev,
				       NPU_MAC_S_BASE_ADDR_1 + MAC_S_PP_STATUS);
	dla_reg_write(nvdla_dev, NPU_MAC_S_BASE_ADDR_1 + MAC_S_POINTER_FLAG, 0);

	pp_status.value =
		dla_reg_read(nvdla_dev, NPU_RMDA_WRAPPER_S_BASE_ADDR_1 +
						RDMA_WRAPPER_S_PP_STATUS);
	dla_reg_write(nvdla_dev,
		      NPU_RMDA_WRAPPER_S_BASE_ADDR_1 +
			      RDMA_WRAPPER_S_POINTER_FLAG,
		      0);

	//config qos for edma
	qos_0.dw = dla_reg_read(nvdla_dev, NPU_TOP_S_BASE_ADDR_1 + NPU_QOS_0);
	qos_0.edma_noc_arqos = 2;
	qos_0.edma_noc_awqos = 0;
	dla_reg_write(nvdla_dev, NPU_TOP_S_BASE_ADDR_1 + NPU_QOS_0, qos_0.dw);

	qos_sel.dw =
		dla_reg_read(nvdla_dev, NPU_TOP_S_BASE_ADDR_1 + NPU_QOS_SEL);
	qos_sel.edma_qos_sel = 1;
	dla_reg_write(nvdla_dev, NPU_TOP_S_BASE_ADDR_1 + NPU_QOS_SEL,
		      qos_sel.dw);
}

int npu_disable_clock(struct nvdla_device *ndev)
{
	npu_disable_mbox_clock(ndev);
	clk_disable_unprepare(ndev->e31_core_clk);

	clk_disable_unprepare(ndev->core_clk);

	clk_disable_unprepare(ndev->cfg_clk);
	return 0;
}

int npu_enable_clock(struct nvdla_device *ndev)
{
	int ret;

	ret = clk_prepare_enable(ndev->cfg_clk);
	if (ret) {
		dla_error("failed to enable cfg_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(ndev->core_clk);
	if (ret) {
		dla_error("failed to enable core_clk: %d\n", ret);
		goto core_clk;
	}

	ret = clk_prepare_enable(ndev->e31_core_clk);
	if (ret < 0) {
		dla_error("npu enable e31 core clk err.\n");
		goto err_e31_clk;
	}
	ret = npu_enable_mbox_clock(ndev);
	if (ret < 0) {
		dla_error("npu enable mbox clock failed.\n");
		goto err_mbox_clk;
	}
	return 0;
err_mbox_clk:
	clk_disable_unprepare(ndev->e31_core_clk);
err_e31_clk:
	clk_disable_unprepare(ndev->core_clk);
core_clk:
	clk_disable_unprepare(ndev->cfg_clk);
	return ret;
}

int npu_platform_init(void)
{
	return 0;
}

int npu_pm_get(struct nvdla_device *ndev)
{
	int ret;

	if (ndev == NULL) {
		return -EINVAL;
	}
	ret = pm_runtime_resume_and_get(&ndev->pdev->dev);
	if (ret < 0) {
		return ret;
	}
	return 0;
}

int npu_pm_put(struct nvdla_device *ndev)
{
	if (ndev == NULL) {
		return -EINVAL;
	}
	pm_runtime_mark_last_busy(&ndev->pdev->dev);
	pm_runtime_put_autosuspend(&ndev->pdev->dev);
	return 0;
}

int npu_platform_uninit(void)
{
	return 0;
}

int npu_hetero_cmd(struct nvdla_device *nvdla_dev, struct win_ioctl_args *args)
{
	return -EINVAL;
}
