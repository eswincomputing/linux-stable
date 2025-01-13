// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/elf.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/eswin-mailbox.h>
#include <linux/delay.h>
#include <linux/bitfield.h>
#include <linux/pm_runtime.h>
#include <nvdla_interface.h>
#include <nvdla_linux.h>
#include "hetero_ioctl.h"
#include <opendla.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include <dt-bindings/interconnect/eswin,win2030.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <linux/win2030_noc.h>
#include <linux/iommu.h>
#include <linux/es_iommu_rsv.h>
#include "dla_log.h"
#include "dla_engine.h"
#include "dla_engine_internal.h"
#include "dla_interface.h"
#include "npu_spram.h"
#include "dla_driver.h"
#include "npu_top_csr.h"
#include "dla_log.h"
#include "edma.h"
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include "debug.h"
#include "internal_interface.h"
#include "nvdla_lowlevel.h"
#include "npu_base_regs.h"
#include "conv_regs.h"
#include "hetero_host.h"
#include "hetero_ipc.h"
#include "nvdla_linux.h"
#include "dla_buffer.h"
#include "mailbox_regs.h"
#include "nvdla_proc.h"
#include <linux/eswin_npu.h>
#include <linux/dma-resv.h>
#include <linux/devfreq.h>
#include <linux/pm_opp.h>
#include <linux/units.h>

extern int frame_timeout;

MODULE_IMPORT_NS(DMA_BUF);
#define DRIVER_NAME "eswin_npu"

#define NPU_CORE_CLK_HIGHEST 1500000000
#define NPU_MIN_VOLTAGE 700000
#define DEVFREQ_VOLT_DELAY 50

int64_t dla_get_time_us(void)
{
	return 0;
}

int32_t dla_data_read(void *driver_context, void *task_data, void *handle,
					  uint16_t index, void *dst, uint32_t size, uint64_t offset)
{
	int32_t ret;
	void *ptr = NULL;
	struct dma_buf *buf;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	struct iosys_map map;
#else
	struct dma_buf_map map;
#endif
	int32_t fd;
	struct nvdla_task *task = (struct nvdla_task *)task_data;
	addrDesc_t *h;

	if (handle != NULL) {
		h = handle;
	} else {
		h = &task->addrlist->addrDesc[index];
	}

	fd = h->devBuf.memFd;

	buf = dma_buf_get(fd);
	if (IS_ERR(buf)) {
		pr_err("Failed get dma_buf for handle=%d\n", fd);
		return -EFAULT;
	}

	ret = dma_buf_begin_cpu_access(buf, DMA_BIDIRECTIONAL);
	if (ret) {
		dla_error("dma_buf_begin_cpu_access error\n");
		goto put_dma_buf;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	ret = dma_buf_vmap_unlocked(buf, &map);
#else
	ret = dma_buf_vmap(buf, &map);
#endif
	ptr = ret ? NULL : map.vaddr;
	if (!ptr) {
		pr_err("Failed to vmap dma_buf for fd=%d\n", fd);
		ret = -ENOMEM;
		goto end_cpu_access;
	}

	if ((buf->size < offset) || (buf->size - offset < size)) {
		dla_error("error:dma buf wrong!fd:%d offset=%lld dma_buf size:%ld size:%d\n",
				  fd, offset, buf->size, size);
		ret = -EFAULT;
	} else {
		memcpy(dst, (void *)(((uint8_t *)ptr) + offset), size);
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	dma_buf_vunmap_unlocked(buf, &map);
#else
	dma_buf_vunmap(buf, &map);
#endif

end_cpu_access:
	dma_buf_end_cpu_access(buf, DMA_BIDIRECTIONAL);
put_dma_buf:
	dma_buf_put(buf);
	return ret;
}

int32_t dla_data_get_fd(void *driver_context, void *task_data, void *handle,
			uint16_t index)
{
	int32_t fd;
	struct nvdla_task *task = (struct nvdla_task *)task_data;
	addrDesc_t *h;

	if (handle != NULL) {
		h = handle;
	} else {
		h = &task->addrlist->addrDesc[index];
	}

	fd = h->devBuf.memFd;

	return fd;
}

int32_t dla_data_get_vaddr(void *task_data, uint16_t index, void **vaddr)
{
	struct nvdla_task *task = (struct nvdla_task *)task_data;

	if (index >= task->addrlist->numAddress) {
		dla_error("index(%d) is large than task->addrlist_desc->numAddress(%d)\n",
				  index, task->addrlist->numAddress);
		return -EFAULT;
	}
	if (task->bobjs[index].vaddr == NULL) {
		dla_error("bobj of index(%d) not vmap!\n", index);
		return -EFAULT;
	}

	*vaddr = (uint8_t *)task->bobjs[index].vaddr + task->addrlist->addrDesc[index].devBuf.offset;
	dla_detail("index:%d offset:0x%llx fd:%lld virtAddr:%px\n", index,
		   task->addrlist->addrDesc[index].devBuf.offset,
		   task->addrlist->addrDesc[index].devBuf.memFd,
		   task->addrlist->addrDesc[index].virtAddr);

	return 0;
}

int32_t dla_get_dma_address(void *driver_context, void *task_data,
							int16_t index, void *dst_ptr, u32 *is_io_tensor)
{
	int32_t ret = 0;
	addrDesc_t *address;
	dma_addr_t *phys_addr = (dma_addr_t *)dst_ptr;
	struct nvdla_task *task = (struct nvdla_task *)task_data;
	if (index == -1 || index > task->addrlist->numAddress) {
		dla_error("dma address index is invalid, %d\n", index);
		return -EINVAL;
	}

	address = (addrDesc_t *)task->addrlist->addrDesc;

	if (address[index].memoryType != 0) {
		dla_error("memory handle type is error:index=%d,memory_type=%d\n",
				  index, address[index].memoryType);
		return -1;
	}

	if (io_tensor_record(task->executor, &address[index], is_io_tensor) > 0) {
		dla_info("io tensor detected index=%d,memory_type=%d flag=%d bind_id=%d\n",
				 index, address[index].memoryType, address[index].flag, address[index].bindId);
		*phys_addr = -1ull;
		return 1;
	}

	*phys_addr = task->bobjs[index].dma_addr;
	*phys_addr = *phys_addr + address[index].devBuf.offset;

	return ret;
}

int32_t dla_get_sram_address(void *driver_context, void *task_data,
				 int16_t index, uint64_t *dst_ptr,
				 u32 *is_io_tensor)
{
	int32_t ret = 0;
	addrDesc_t *address;
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)driver_context;
	struct nvdla_task *task = (struct nvdla_task *)task_data;

	if (index == -1 || index > task->addrlist->numAddress) {
		dla_error("dma address index is invalid, %d\n", index);
		return -EINVAL;
	}

	address = (addrDesc_t *)task->addrlist->addrDesc;
	if (address[index].memoryType != 1) {
		dla_error("memory handle type is error:index=%d,memory_type=%d\n",
				  index, address[index].memoryType);
		return -1;
	}

	if (io_tensor_record(task->executor, &address[index], is_io_tensor) > 0) {
		dla_debug("io tensor detected index=%d,memory_type=%d flag=%d bind_id=%d\n",
			index, address[index].memoryType, address[index].flag, address[index].bindId);
		*dst_ptr = -1ull;
		return 1;
	}
	*dst_ptr = nvdla_dev->spram_base_addr + address[index].devBuf.offset;

	return ret;
}

static const struct of_device_id edla_of_match[] = {
	{
		.compatible = "eswin,npu",
	},
	{},
};
MODULE_DEVICE_TABLE(of, edla_of_match);

void *npu_get_win_engine(void *arg_nvdla_dev)
{
	struct nvdla_device *nvdla_dev = arg_nvdla_dev;

	return nvdla_dev->win_engine;
}

irqreturn_t npu_mbox_irq(int irq, void *dev_id)
{
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)dev_id;
	msg_payload_t payload;
	u32 tiktok;
	u16 op_index;
	u32 stat;
	u32 data1;

	while (true) {
		*(u32 *)&payload = readl(nvdla_dev->mbox_rx_base + MBOX_NPU_RD_DATA0_OFFSET);
		data1 = readl(nvdla_dev->mbox_rx_base + MBOX_NPU_RD_DATA1_OFFSET);
		if (!data1) {
			break;
		}
		tiktok = payload.param & 0x1;
		op_index = payload.lparam;
		// notify data is retrieved by bit clear of data[63].
		writel(0x0, nvdla_dev->mbox_rx_base + MBOX_NPU_RD_DATA1_OFFSET);

		switch (payload.type) {
		case FRAME_DONE:
			stat = payload.param >> 1 & 0x1;
			mbx_irq_frame_done(nvdla_dev->win_engine, tiktok, stat, payload.lparam);
			break;
		case NOTIFY_OP_DONE:
			mbx_irq_op_done(nvdla_dev->win_engine, tiktok, op_index);
			break;
		case NOTIFY_EVENT_SINK_DONE:
			stat = payload.param >> 1 & 0xff;
			mbx_irq_event_sink_done(nvdla_dev->win_engine, tiktok, op_index, stat);
			break;
		default:
			dla_error("invalid payload.type= %hhu\n", payload.type);
			ASSERT(false);
		}
	}
	return IRQ_HANDLED;
}

static struct nvdla_device *static_nvdla_dev[2] = { NULL };

struct nvdla_device *get_nvdla_dev(int i)
{
	if (i < 0 || i > 2) {
		return NULL;
	}
	return static_nvdla_dev[i];
}

static int npu_set_freq_req(struct nvdla_device *nvdla_dev, struct npu_freq_param *tbl)
{
	struct clk *llc_parent;
	struct clk *npu_parent;
	unsigned long llc_rate, npu_rate;
	unsigned long rate;
	int ret;

	llc_parent = clk_get_parent(nvdla_dev->mux_u_npu_llclk_3mux1_gfree);
	if (!llc_parent) {
		dev_err(&nvdla_dev->pdev->dev, "get npu llc clock pareent err.\n");
		return -EINVAL;
	}
	npu_parent = clk_get_parent(nvdla_dev->mux_u_npu_core_3mux1_gfree);
	if (!npu_parent) {
		dev_err(&nvdla_dev->pdev->dev, "get npu core clock pareent err.\n");
		return -EINVAL;
	}

	llc_rate = clk_get_rate(nvdla_dev->mux_u_npu_llclk_3mux1_gfree);
	npu_rate = clk_get_rate(nvdla_dev->mux_u_npu_core_3mux1_gfree);

	ret = clk_set_parent(nvdla_dev->mux_u_npu_llclk_3mux1_gfree, tbl->llc_clk_parent);
	if (ret) {
		dev_err(&nvdla_dev->pdev->dev, "set npu llc clock parent err = %d.\n", ret);
		return -EINVAL;
	}

	ret = clk_set_parent(nvdla_dev->mux_u_npu_core_3mux1_gfree, tbl->npu_clk_parent);
	if (ret) {
		dev_err(&nvdla_dev->pdev->dev, "set npu core clock parent err = %d.\n", ret);
		goto err_npu_core;
	}

	mdelay(10);
	rate = clk_round_rate(nvdla_dev->llc_aclk, tbl->llc_rate);
	ret = clk_set_rate(nvdla_dev->llc_aclk, rate);
	if (ret) {
		dev_err(&nvdla_dev->pdev->dev, "failed to set npu llc clock rate: %lu, ret = %d.\n", rate, ret);
		goto err_llc_rate;
	}

	rate = clk_round_rate(nvdla_dev->core_clk, tbl->npu_rate);
	ret = clk_set_rate(nvdla_dev->core_clk, rate);
	if (ret != 0) {
		dev_err(&nvdla_dev->pdev->dev, "failed to set npu core_clk=%lu, ret = %d\n", rate, ret);
		goto err_npu_rate;
	}
	return 0;

err_npu_rate:
	clk_set_rate(nvdla_dev->llc_aclk, llc_rate);
err_llc_rate:
	clk_set_parent(nvdla_dev->mux_u_npu_core_3mux1_gfree, npu_parent);
err_npu_core:
	clk_set_parent(nvdla_dev->mux_u_npu_llclk_3mux1_gfree, llc_parent);
	return ret;
}

static int npu_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct nvdla_device *nvdla_dev = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long target_volt, target_rate;
	int ret;
	struct npu_freq_param *tbl = NULL;
	int highset_idx = nvdla_dev->freq_count - 1;
	int tbl_idx = 0;

	if (pm_runtime_status_suspended(dev)) {
		return 0;
	}

	opp = devfreq_recommended_opp(dev, freq, flags);
	if (IS_ERR(opp)) {
		return PTR_ERR(opp);
	}

	target_rate = dev_pm_opp_get_freq(opp);
	target_volt = dev_pm_opp_get_voltage(opp);
	dev_pm_opp_put(opp);

	if (target_rate == nvdla_dev->rate) {
		return 0;
	}
	mutex_lock(&nvdla_dev->devfreq_lock);

	for (int i = 0; i < nvdla_dev->freq_count; i++) {
		if (nvdla_dev->freq_tbl[i].npu_rate == target_rate && nvdla_dev->freq_tbl[i].valid == 1) {
			tbl = &nvdla_dev->freq_tbl[i];
			tbl_idx = i;
			break;
		}
	}

	if (!tbl) {
		dev_err(dev, "can't find suitable freq table\n");
		goto out;
	}

	if (target_rate > nvdla_dev->rate) { // rise freq
		ret = regulator_set_voltage(nvdla_dev->npu_regulator, nvdla_dev->freq_tbl[highset_idx].volt,
					nvdla_dev->freq_tbl[highset_idx].volt);
		if (ret) {
			dev_err(dev, "Cannot set voltage %d uV\n", nvdla_dev->freq_tbl[highset_idx].volt);
			goto out;
		}

		mdelay(DEVFREQ_VOLT_DELAY);
		ret = npu_set_freq_req(nvdla_dev, tbl);
		if (ret) {
			goto out;
		}
		mdelay(1);
		if (target_rate != nvdla_dev->freq_tbl[highset_idx].npu_rate) {
			ret = regulator_set_voltage(nvdla_dev->npu_regulator, tbl->volt, tbl->volt);
			if (ret) {
				dev_err(dev, "Cannot set voltage %d uV\n", tbl->volt);
				goto out;
			}
		}
	} else if (target_rate < nvdla_dev->rate) { // lower freq
		if (nvdla_dev->rate != nvdla_dev->freq_tbl[highset_idx].npu_rate) {
			ret = regulator_set_voltage(nvdla_dev->npu_regulator, nvdla_dev->freq_tbl[highset_idx].volt,
						nvdla_dev->freq_tbl[highset_idx].volt);
			if (ret) {
				dev_err(dev, "Cannot set voltage %d uV\n", nvdla_dev->freq_tbl[highset_idx].volt);
				goto out;
			}
			mdelay(DEVFREQ_VOLT_DELAY);
		}

		ret = npu_set_freq_req(nvdla_dev, tbl);
		if (ret) {
			goto out;
		}
		mdelay(1);
		ret = regulator_set_voltage(nvdla_dev->npu_regulator, tbl->volt, tbl->volt);
		if (ret) {
			dev_err(dev, "Cannot set voltage %d uV\n", tbl->volt);
			goto out;
		}
	}

	nvdla_dev->rate = tbl->npu_rate;
	nvdla_dev->volt = tbl->volt;
	nvdla_dev->freq_idx = tbl_idx;

	dev_info(dev, "devfreq set npu clk rate:%ld, llc clk rate:%ld, npu volt:%d\n",
			tbl->npu_rate, tbl->llc_rate, tbl->volt);

out:
	mutex_unlock(&nvdla_dev->devfreq_lock);

	return ret;
}

static int npu_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct nvdla_device *nvdla_dev = dev_get_drvdata(dev);

	*freq = nvdla_dev->rate;
	return 0;
}
static void eswin_exit(struct device *dev)
{

}

static int eswin_get_dev_status(struct device *dev, struct devfreq_dev_status *stat)
{
	struct nvdla_device *nvdla_dev = dev_get_drvdata(dev);

	stat->busy_time = 1024;
	stat->total_time = 1024;
	stat->current_frequency = nvdla_dev->rate;

	return 0;
}

static struct devfreq_dev_profile npu_devfreq_profile = {
	.initial_freq = NPU_CORE_CLK_HIGHEST,
	.timer = DEVFREQ_TIMER_DELAYED,
	.polling_ms = 1000,
	.target = npu_devfreq_target,
	.get_cur_freq = npu_devfreq_get_cur_freq,
	.get_dev_status = eswin_get_dev_status,
	.exit = eswin_exit,
	.is_cooling_device = true,
};
static struct devfreq_simple_ondemand_data ondemand_data =
{
	.upthreshold =80,
	.downdifferential=10,
};

static int npu_set_freq_table(struct nvdla_device *nvdla_dev)
{
	unsigned long freq = 0;
	unsigned long voltage;
	struct device *dev = &nvdla_dev->pdev->dev;
	struct dev_pm_opp *opp;
	int opp_count, i = 0;

	opp_count = dev_pm_opp_get_opp_count(dev);
	if (opp_count <= 0) {
		dev_info(dev, "Failed to get OPP count: %d\n", opp_count);

		nvdla_dev->freq_tbl = kmalloc(sizeof(struct npu_freq_param) * 2, GFP_KERNEL);
		if (!nvdla_dev->freq_tbl) {
			return -ENOMEM;
		}

		nvdla_dev->freq_count = 2;
		/* NPU 1G HZ, LLC 800M HZ */
		nvdla_dev->freq_tbl[0].npu_clk_parent = nvdla_dev->fixed_rate_clk_spll2_fout2;
		nvdla_dev->freq_tbl[0].npu_rate = 1040000000;
		nvdla_dev->freq_tbl[0].llc_clk_parent = nvdla_dev->fixed_rate_clk_spll0_fout1;
		nvdla_dev->freq_tbl[0].llc_rate = 800000000;
		nvdla_dev->freq_tbl[0].volt = 900000;
		nvdla_dev->freq_idx_1G = 0;
		nvdla_dev->freq_tbl[0].valid = 1;

		/* NPU 1.5G HZ, LLC 1.188G HZ*/
		nvdla_dev->freq_tbl[1].npu_clk_parent = nvdla_dev->fixed_rate_clk_spll1_fout1;
		nvdla_dev->freq_tbl[1].npu_rate = 1500000000;
		nvdla_dev->freq_tbl[1].llc_clk_parent = nvdla_dev->fixed_rate_clk_vpll_fout1;
		nvdla_dev->freq_tbl[1].llc_rate = 1188000000;
		nvdla_dev->freq_tbl[1].volt = 1050000;
		nvdla_dev->freq_tbl[1].valid = 1;

		return 0;
	}

	dev_dbg(dev, "get opp count:%d\n", opp_count);

	nvdla_dev->freq_tbl = kmalloc(sizeof(struct npu_freq_param) * opp_count, GFP_KERNEL);
	if (!nvdla_dev->freq_tbl)
		return -ENOMEM;

	nvdla_dev->freq_count = opp_count;

	while (i < opp_count) {
		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp)) {
			break;
		}

		voltage = dev_pm_opp_get_voltage(opp);
		dev_dbg(dev, "Frequency: %lu Hz, Voltage: %lu uV\n", freq, voltage);

		nvdla_dev->freq_tbl[i].npu_rate = freq;
		nvdla_dev->freq_tbl[i].volt = voltage;

		if (freq == 1500000000) {
			nvdla_dev->freq_tbl[i].npu_clk_parent = nvdla_dev->fixed_rate_clk_spll1_fout1;
			nvdla_dev->freq_tbl[i].llc_clk_parent = nvdla_dev->fixed_rate_clk_vpll_fout1;
			nvdla_dev->freq_tbl[i].llc_rate = 1188000000;
			nvdla_dev->freq_tbl[i].valid = 1;
		} else if (freq == 1188000000) {
			nvdla_dev->freq_tbl[i].npu_clk_parent = nvdla_dev->fixed_rate_clk_vpll_fout1;
			nvdla_dev->freq_tbl[i].llc_clk_parent = nvdla_dev->fixed_rate_clk_spll0_fout1;
			nvdla_dev->freq_tbl[i].llc_rate = 800000000;
			nvdla_dev->freq_tbl[i].valid = 1;
		} else if (freq == 1040000000) {
			nvdla_dev->freq_tbl[i].npu_clk_parent = nvdla_dev->fixed_rate_clk_spll2_fout2;
			nvdla_dev->freq_tbl[i].llc_clk_parent = nvdla_dev->fixed_rate_clk_spll0_fout1;
			nvdla_dev->freq_tbl[i].llc_rate = 800000000;
			nvdla_dev->freq_idx_1G = i;
			nvdla_dev->freq_tbl[i].valid = 1;
		} else if (freq == 750000000) {
			nvdla_dev->freq_tbl[i].npu_clk_parent = nvdla_dev->fixed_rate_clk_spll1_fout1;
			nvdla_dev->freq_tbl[i].llc_clk_parent = nvdla_dev->fixed_rate_clk_spll2_fout1;
			nvdla_dev->freq_tbl[i].llc_rate = 520000000;
			nvdla_dev->freq_tbl[i].valid = 1;
		} else if (freq == 520000000) {
			nvdla_dev->freq_tbl[i].npu_clk_parent = nvdla_dev->fixed_rate_clk_spll2_fout2;
			nvdla_dev->freq_tbl[i].llc_clk_parent = nvdla_dev->fixed_rate_clk_spll0_fout1;
			nvdla_dev->freq_tbl[i].llc_rate = 400000000;
			nvdla_dev->freq_tbl[i].valid = 1;
		}

		i++;
		freq++;

		dev_pm_opp_put(opp);
	}

	if (nvdla_dev->freq_tbl[opp_count-1].valid != 1 || nvdla_dev->freq_idx_1G == 0) {
		dev_err(dev, "opp table not include 1.5G or 1G freq\n");
		return -ENOMEM;
	}

	return 0;
}

void npu_devfreq_init(struct nvdla_device *nvdla_dev)
{
	struct device *dev = &nvdla_dev->pdev->dev;
	int err = 0;

	err = devm_pm_opp_of_add_table(dev);
	if (err) {
		dev_err(dev, "Failed to add OPP table, ret:%d.\n", err);
		return;
	}

	nvdla_dev->df = devm_devfreq_add_device(dev, &npu_devfreq_profile, DEVFREQ_GOV_SIMPLE_ONDEMAND, &ondemand_data);
	if (IS_ERR(nvdla_dev->df)) {
		err = PTR_ERR(nvdla_dev->df);
		dev_err(dev, "Failed to add devfreq device, ret:%d.\n", err);
		return;
	}
	/* Register opp_notifier to catch the change of OPP  ????*/
	err = devm_devfreq_register_opp_notifier(dev, nvdla_dev->df);
	if (err < 0) {
		dev_err(dev, "failed to register opp notifier, ret:%d\n", err);
		return;
	}

	err = dev_pm_qos_add_request(dev, &nvdla_dev->req_max_freq,
					DEV_PM_QOS_MAX_FREQUENCY, PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);
	if (err < 0) {
		dev_err(dev, "Failed to add QoS max freq request, ret:%d.\n", err);
	}
}

static int32_t  npu_probe_result = 0;

static int32_t edla_probe(struct platform_device *pdev)
{
	int32_t err = 0;
	struct resource *res;
	struct nvdla_device *nvdla_dev;
	struct device *dev = &pdev->dev;
	uint32_t version;

	dev_info(dev, "Eswin NPU version:%s.\n", NPU_VERSION);
#if SMALL_PEC_MAT
	dev_dbg(dev, "load npu driver with PEC2x2.\n");
#else
	dev_dbg(dev, "load npu driver with PEC4x8.\n");
#endif

	nvdla_dev = devm_kzalloc(dev, sizeof(*nvdla_dev), GFP_KERNEL);
	if (!nvdla_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, nvdla_dev);
	nvdla_dev->pdev = pdev;
	spin_lock_init(&nvdla_dev->nvdla_lock);
	mutex_init(&nvdla_dev->mapping_mutex);
	init_waitqueue_head(&nvdla_dev->event_wq);
	mutex_init(&nvdla_dev->devfreq_lock);

	nvdla_dev->npu_regulator = devm_regulator_get(dev, "npu");
	if (nvdla_dev->npu_regulator == NULL) {
		dev_err(dev, "cannot get npu regulator, error.\n");
		return -EINVAL;
	}
	nvdla_dev->is_low_freq = of_property_read_bool(pdev->dev.of_node, "apply_npu_1G_freq");

	err = of_property_read_u32(pdev->dev.of_node, "npu_def_high_vol", &nvdla_dev->npu_def_high_vol);
	if (err) {
		nvdla_dev->npu_def_high_vol = 0;
		err = 0;
	}

	err = regulator_enable(nvdla_dev->npu_regulator);
	if (err < 0)
	{
		dev_err(dev, "npu_regulator enble error:%d\n\r", err);
		nvdla_dev->npu_regulator = NULL;
		return err;
	}

	err = npu_dt_node_resources(nvdla_dev);
	if (err) {
		dev_err(dev, "error, get hw resource, ret=%d\n", err);
		platform_set_drvdata(pdev, NULL);
		regulator_disable(nvdla_dev->npu_regulator);
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no io resource\n");
		err = PTR_ERR(res);
		goto err_mem0;
	}

	npu_devfreq_init(nvdla_dev);

	err = npu_set_freq_table(nvdla_dev);
	if (err < 0) {
		dev_err(dev, "failed to set freq table\n");
		goto err_freq;
	}

	if (nvdla_dev->is_low_freq == 0) {
		if (nvdla_dev->npu_def_high_vol) {
			err = regulator_set_voltage(nvdla_dev->npu_regulator, nvdla_dev->npu_def_high_vol,
										nvdla_dev->npu_def_high_vol);
		} else {
			err = regulator_set_voltage(nvdla_dev->npu_regulator,
						nvdla_dev->freq_tbl[nvdla_dev->freq_count - 1].volt,
						nvdla_dev->freq_tbl[nvdla_dev->freq_count - 1].volt);
		}

		if (err != 0) {
			dev_err(dev, "error npu regulator volt:%duV ret:%d.\n",
				nvdla_dev->npu_def_high_vol ? : nvdla_dev->freq_tbl[nvdla_dev->freq_count - 1].volt, err);
			goto err_freq;
		}
		mdelay(10);
		err = npu_set_freq_req(nvdla_dev, &nvdla_dev->freq_tbl[nvdla_dev->freq_count - 1]);
	} else {
		err = regulator_set_voltage(nvdla_dev->npu_regulator,
						nvdla_dev->freq_tbl[nvdla_dev->freq_idx_1G].volt,
						nvdla_dev->freq_tbl[nvdla_dev->freq_idx_1G].volt);
		dev_dbg(dev, "name:%s, volt:%d, ret:%d\n", pdev->name,
				nvdla_dev->freq_tbl[nvdla_dev->freq_idx_1G].volt, err);
		mdelay(10);
		err = npu_set_freq_req(nvdla_dev, &nvdla_dev->freq_tbl[nvdla_dev->freq_idx_1G]);
	}

	if (err) {
		dev_err(&pdev->dev, "failed to set mux_u_npu_core_3mux1_gfree parent: %d\n", err);
		goto err_freq;
	}

	nvdla_dev->rate = clk_get_rate(nvdla_dev->core_clk);
	nvdla_dev->volt = regulator_get_voltage(nvdla_dev->npu_regulator);

	//npu configuration space, start from 0x51c00000
	nvdla_dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nvdla_dev->base)) {
		err = PTR_ERR(nvdla_dev->base);
		goto err_freq;
	}
	if (request_mem_region(E31_EMISSION_DTIM_BASE + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET,
						E31_EMISSION_DTIM_SIZE, "EMISSION_BASE") == NULL) {
		dev_err(&pdev->dev, "request_mem_region error\n");
		err = -EBUSY;
		goto err_freq;
	}
	nvdla_dev->emission_base = devm_ioremap(
		&pdev->dev, E31_EMISSION_DTIM_BASE + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET, E31_EMISSION_DTIM_SIZE);
	if (!nvdla_dev->emission_base) {
		dev_err(&pdev->dev, "ioremap error\n");
		err = -ENOMEM;
		goto err_iomap_emission;
	}

	if (request_mem_region(E31_PROGRAM_DTIM_BASE + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET, E31_PROGRAM_DTIM_SIZE,
						   "PROGRAM_BASE") == NULL) {
		dev_err(&pdev->dev, "request_mem_region error\n");
		err = -EBUSY;
		goto err_iomap_emission;
	}
	nvdla_dev->program_base = devm_ioremap(
		&pdev->dev, E31_PROGRAM_DTIM_BASE + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET, E31_PROGRAM_DTIM_SIZE);
	if (!nvdla_dev->program_base) {
		dev_err(&pdev->dev, "ioremap error\n");
		err = -ENOMEM;
		goto err_iomap_program;
	}

	nvdla_dev->uart_mutex_base = devm_ioremap(
		&pdev->dev, UART_MUTEX_BASE_ADDR + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET, UART_MUTEX_ADDR_SIZE);
	if (!nvdla_dev->uart_mutex_base) {
		dev_err(&pdev->dev, "ioremap error\n");
		err = -ENOMEM;
		goto err_iomap_program;
	}

	err = npu_enable_clock(nvdla_dev);
	if (err < 0) {
		dev_err(&pdev->dev, "npu enable clock err, ret = %d.\n", err);
		goto err_iomap_program;
	}

	err = npu_init_reset(nvdla_dev);
	if (err)
		goto err_init_reset;

	err = npu_init_mbox(nvdla_dev);
	if (err) {
		dev_err(&pdev->dev, "npu init mailbox error, ret = %d.\n", err);
		goto err_init_mbox;
	}
	npu_tbu_power(dev, true);

	nvdla_dev->e31_mmio_base = devm_ioremap(dev, NPU_CFG_BASE_ADDR + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET,
							NPU_CFG_ADDR_RANGE);
	if (!nvdla_dev->e31_mmio_base) {
		dla_error("Eswin e31 ioremap fail.\n");
		goto err_iomap_e31;
	}

	err = npu_e31_load_fw(nvdla_dev);
	if (err) {
		dev_err(&pdev->dev, "load e31 fw error.\n");
		goto err_load_firm;
	}

	/* Set dma_mask and coherent_dma_mask before perfroming spram buffer attachment*/
	err = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(41));
	if (err)
		dev_warn(dev, "Unable to set coherent mask\n");

	err = npu_spram_get(nvdla_dev);
	if (err) {
		dla_error("error get  spram.\n");
		goto err_spram;
	}
	version = dla_reg_read(nvdla_dev, 0x150000);
	dla_info("edla version: 0x%x\n", version);

	/* config streamID of NPU_DMA */
	npu_dma_sid_cfg(nvdla_dev->base, WIN2030_SID_NPU_DMA);

	npu_hw_init(nvdla_dev);
	err = edma_init(nvdla_dev);
	if (err) {
		dev_err(dev, "edma_init fail\n");
		goto err_edma_init;
	}
	err = npu_create_sysfs(pdev);
	if (err) {
		dev_err(&pdev->dev, "unable to create sysfs files\n");
	}

	err = win_engine_init(nvdla_dev, &nvdla_dev->win_engine);
	if (err) {
		dev_err(&pdev->dev, "failed to init win_engine\n");
		goto err_engine_init;
	}

	dev_info(&pdev->dev, "win_engine 0x%px\n", nvdla_dev->win_engine);

	nvdla_dev->pause_op_list = vmalloc(MAX_OP_NUM * sizeof(u16));
	static_nvdla_dev[nvdla_dev->numa_id] = nvdla_dev;

	pm_runtime_set_autosuspend_delay(&pdev->dev, 10000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	err = create_npu_dev(nvdla_dev->numa_id, nvdla_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to register npu device\n");
		goto err_create_dev;
	}
	pm_runtime_mark_last_busy(&pdev->dev);

	return err;
err_create_dev:
	vfree(nvdla_dev->pause_op_list);
	win_engine_destroy(nvdla_dev);
err_engine_init:
	edma_free(nvdla_dev);
err_edma_init:
	npu_spram_release(nvdla_dev);
err_spram:
err_load_firm:
err_iomap_e31:
	npu_tbu_power(dev, false);
err_init_mbox:
err_init_reset:
	npu_disable_clock(nvdla_dev);
err_iomap_program:
	release_mem_region(E31_PROGRAM_DTIM_BASE + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET, E31_PROGRAM_DTIM_SIZE);
err_iomap_emission:
	release_mem_region(E31_EMISSION_DTIM_BASE + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET, E31_EMISSION_DTIM_SIZE);
err_freq:
	devm_devfreq_unregister_opp_notifier(&nvdla_dev->pdev->dev, nvdla_dev->df);
	devm_devfreq_remove_device(&nvdla_dev->pdev->dev, nvdla_dev->df);
	dev_pm_qos_remove_request(&nvdla_dev->req_max_freq);
err_mem0:
	regulator_disable(nvdla_dev->npu_regulator);
	npu_put_dt_resources(nvdla_dev);
	npu_probe_result = err;
	return err;
}

static int32_t __exit edla_remove(struct platform_device *pdev)
{
	struct nvdla_device *nvdla_dev = dev_get_drvdata(&pdev->dev);
	int ret;

	if (nvdla_dev == NULL) {
		return 0;
	}

	ret = npu_hardware_reset(nvdla_dev);
	if (ret) {
		dla_error("hardware reset error, ret=%d.\n", ret);
		return -EIO;
	}

	destory_npu_dev(nvdla_dev->numa_id);
	npu_uninit_mbox(nvdla_dev);
	npu_dev_reset(nvdla_dev);

	/* reset the uart1 mutex lock */
	reset_uart_mutex(nvdla_dev);

	if (nvdla_dev->mbx_chan) {
		mbox_free_channel(nvdla_dev->mbx_chan);
	}
	win_engine_destroy(nvdla_dev);
	edma_free(nvdla_dev);
	release_mem_region(E31_EMISSION_DTIM_BASE + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET, E31_EMISSION_DTIM_SIZE);
	release_mem_region(E31_PROGRAM_DTIM_BASE + nvdla_dev->numa_id * NPU_DIE_REG_OFFSET, E31_PROGRAM_DTIM_SIZE);
	npu_spram_release(nvdla_dev);

	dev_pm_qos_remove_request(&nvdla_dev->req_max_freq);
	npu_tbu_power(&pdev->dev, false);
	devm_devfreq_unregister_opp_notifier(&nvdla_dev->pdev->dev, nvdla_dev->df);
	devm_devfreq_remove_device(&nvdla_dev->pdev->dev, nvdla_dev->df);
	ret = npu_disable_clock(nvdla_dev);
	npu_put_dt_resources(nvdla_dev);
	npu_remove_sysfs(pdev);
	regulator_disable(nvdla_dev->npu_regulator);
	pm_runtime_disable(&pdev->dev);

	if (nvdla_dev->pause_op_list) {
		vfree(nvdla_dev->pause_op_list);
		nvdla_dev->pause_op_list = NULL;
	}

	if (nvdla_dev->freq_tbl) {
		kfree(nvdla_dev->freq_tbl);
		nvdla_dev->freq_tbl = NULL;
	}
	return 0;
}

int __maybe_unused npu_runtime_suspend(struct device *dev)
{
	struct nvdla_device *ndev = dev_get_drvdata(dev);
	int ret = 0;

	dev_dbg(dev, "%s\n", __func__);
	if (!ndev) {
		dev_err(dev, "ndev is null.\n");
		return -EIO;
	}

	mutex_lock(&ndev->devfreq_lock);
	for (u32 i = 0; i < ndev->freq_count; i++) {
		if (ndev->freq_tbl[i].npu_rate == ndev->rate) {
			ndev->freq_idx = i;
			break;
		}
	}

	ret = npu_set_freq_req(ndev, &ndev->freq_tbl[0]);
	if (ret) {
		dev_err(dev, "npu_runtime_suspend set_freq err, ret = %d.\n", ret);
		mutex_unlock(&ndev->devfreq_lock);
		return ret;
	}
	ndev->rate = ndev->freq_tbl[0].npu_rate;

	ret = regulator_set_voltage(ndev->npu_regulator, NPU_MIN_VOLTAGE, NPU_MIN_VOLTAGE);
	if (ret) {
		dev_err(dev, "npu_runtime_suspend set_voltage err, ret = %d.\n", ret);
		mutex_unlock(&ndev->devfreq_lock);
		return ret;
	}

	npu_tbu_power(dev, false);
	npu_disable_clock(ndev);
	mutex_unlock(&ndev->devfreq_lock);

	dev_pm_qos_update_request(&ndev->req_max_freq, DIV_ROUND_UP(ndev->freq_tbl[0].npu_rate, HZ_PER_KHZ));

	return 0;
}

int __maybe_unused npu_runtime_resume(struct device *dev)
{
	struct nvdla_device *ndev = dev_get_drvdata(dev);
	int ret, volt;

	dev_dbg(dev, "%s\n", __func__);
	if (!ndev) {
		dev_err(dev, "ndev is null.\n");
		return -EIO;
	}
	ret = npu_enable_clock(ndev);
	if (ret) {
		dev_err(dev, "enable clock err, ret = %d.\n", ret);
		return ret;
	}
	npu_tbu_power(dev, true);

	mutex_lock(&ndev->devfreq_lock);
	volt = ndev->freq_tbl[ndev->freq_idx].volt;
	ret = regulator_set_voltage(ndev->npu_regulator, volt, volt);
	if (ret) {
		dev_err(dev, "npu_runtime_resume set_voltage err, ret = %d.\n", ret);
		mutex_unlock(&ndev->devfreq_lock);
		return ret;
	}

	mdelay(DEVFREQ_VOLT_DELAY);
	ret = npu_set_freq_req(ndev, &ndev->freq_tbl[ndev->freq_idx]);
	if (ret) {
		dev_err(dev, "npu_runtime_resume set_freq err, ret = %d.\n", ret);
		mutex_unlock(&ndev->devfreq_lock);
		return ret;
	}
	ndev->rate = ndev->freq_tbl[ndev->freq_idx].npu_rate;
	mutex_unlock(&ndev->devfreq_lock);

	dev_pm_qos_update_request(&ndev->req_max_freq, PM_QOS_MAX_FREQUENCY_DEFAULT_VALUE);

	return 0;
}

int __maybe_unused npu_suspend(struct device *dev)
{
	struct nvdla_device *nvdla_dev = dev_get_drvdata(dev);
	struct win_engine *engine = (struct win_engine *)nvdla_dev->win_engine;
	int ret;

	nvdla_dev->is_suspend = true;
	ret = wait_event_interruptible_timeout(nvdla_dev->event_wq,
		((engine->tiktok_frame[0] == NULL) && (engine->tiktok_frame[1] == NULL)),
		msecs_to_jiffies(frame_timeout));
	if (ret == 0) {
		dev_err(dev, "Timeout waiting for frame done\n");
		nvdla_dev->is_suspend = false;
		return -ETIMEDOUT;
	} else if (ret < 0) {
		dev_err(dev, "Wait error: %d\n", ret);
		nvdla_dev->is_suspend = false;
		return ret;
	}

	dev_dbg(dev, "%s\n", __func__);
	ret = npu_hardware_reset(NULL);
	if (ret) {
		dla_error("npu suspend err, ret=%d.\n", ret);
		return ret;
	}
	memset(engine->host_node, 0, sizeof(host_node_t));
	engine->tiktok = 0;

	npu_uninit_mbox(nvdla_dev);
	npu_dev_reset(nvdla_dev);
	npu_uninit_ipc(nvdla_dev);
	reset_uart_mutex(nvdla_dev);

	edma_free(nvdla_dev);

	if (!pm_runtime_status_suspended(dev)) {
		dev_dbg(dev, "disable clk\n");
		npu_tbu_power(dev, false);
		npu_disable_clock(nvdla_dev);
	}

	npu_dev_assert(nvdla_dev);
	regulator_disable(nvdla_dev->npu_regulator);
	mdelay(20);

	return 0;
}

int __maybe_unused npu_resume(struct device *dev)
{
	int ret;
	struct nvdla_device *ndev = dev_get_drvdata(dev);
	dev_dbg(dev, "%s\n", __func__);

	ret = regulator_enable(ndev->npu_regulator);
	if (ret < 0)	{
		dla_error("%s, %d regulator_enable eror\n", __func__, __LINE__);
		return ret;
	}
	mdelay(20);

	ret = npu_enable_clock(ndev);
	if (ret < 0) {
		dla_error("error enable clock, ret=%d.\n", ret);
		goto err_clk;
	}
	npu_tbu_power(dev, true);
	ret = npu_hardware_reset(ndev);
	if (ret) {
		dla_error("hardware reset error, ret=%d.\n", ret);
		goto err_reset;
	}

	ret = npu_dev_deassert(ndev);
	if (ret < 0) {
		goto err_reset;
	}
	ret = npu_init_mbox(ndev);
	if (ret) {
		dev_err(dev, "npu init mailbox error, ret = %d.\n", ret);
		goto err_init_mbox;
	}

	/* config streamID of NPU_DMA */

	ret = npu_e31_load_fw(ndev);
	if (ret) {
		dev_err(dev, "load e31 fw error.\n");
		goto err_load_firm;
	}
	npu_dma_sid_cfg(ndev->base, WIN2030_SID_NPU_DMA);
	npu_hw_init(ndev);
	ret = npu_init_ipc(ndev);
	if (ret) {
		dev_err(dev, "npu init ipc error.\n");
		goto err_ipc;
	}

	ret = edma_init(ndev);
	if (ret) {
		dev_err(dev, "edma_init fail\n");
		goto err_edma_init;
	}

	if (pm_runtime_status_suspended(dev)) {
		dev_dbg(dev, "npu is runtime suspended\n");
		npu_tbu_power(dev, false);
		npu_disable_clock(ndev);
	}

	ndev->is_suspend = false;
	npu_frame_schedule((struct win_engine *)ndev->win_engine);
	return 0;

err_edma_init:
	npu_uninit_ipc(ndev);
err_ipc:
err_load_firm:
	npu_uninit_mbox(ndev);
	npu_tbu_power(dev, false);
err_init_mbox:
	npu_dev_assert(ndev);
err_reset:
	npu_disable_clock(ndev);
err_clk:
	regulator_disable(ndev->npu_regulator);

	return ret;
}

static const struct dev_pm_ops npu_hw_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(npu_suspend, npu_resume)
	SET_RUNTIME_PM_OPS(npu_runtime_suspend, npu_runtime_resume, NULL)
};

static struct platform_driver edla_driver =
{
	.probe = edla_probe,
	.remove = edla_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(edla_of_match),
		.pm = pm_sleep_ptr(&npu_hw_pm_ops),
	},
};

static int __init npu_modules_init(void)
{
	int err;

	err = platform_driver_register(&edla_driver);
	if (err < 0) {
		dla_error("NPU:platform_register_drivers failed!err=%d\n", err);
		return err;
	}
	if (npu_probe_result < 0) {
		dla_error("NPU:npu_probe_result failed:%d\n", npu_probe_result);
		platform_driver_unregister(&edla_driver);
		return npu_probe_result;
	}
	err = npu_platform_init();
	if (err) {
		dla_error("npu platform init err, err=%d.\n", err);
		platform_driver_unregister(&edla_driver);
		return err;
	}
	npu_create_procfs();
	return 0;
}
module_init(npu_modules_init);

static void __exit npu_modules_exit(void)
{
	npu_remove_procfs();

	npu_platform_uninit();

	platform_driver_unregister(&edla_driver);

	dla_loop_buf_exit();
}
module_exit(npu_modules_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("EDLA");
MODULE_DESCRIPTION("Eswin Deep Learning Accelerator driver");
MODULE_VERSION(NPU_VERSION);
