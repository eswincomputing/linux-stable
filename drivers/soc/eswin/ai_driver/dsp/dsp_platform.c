// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox_controller.h>
#include <linux/mailbox/eswin-mailbox.h>
#include <asm/cacheflush.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include "es_iommu_rsv.h"
#include "eswin-dsp-subsys.h"

#include "dsp_platform.h"
#include "dsp_firmware.h"
#include "dsp_ioctl.h"
#include "dsp_main.h"

#define DRIVER_NAME "eswin-dsp"

/*
	dsp subsys registers
*/

#define REG_OFFSET_DSP_START 0x00
#define REG_OFFSET_DSP_STAT 0x04
#define REG_OFFSET_DSP_PRID 0x08
#define REG_OFFSET_DSP_RESET 0x18
#define REG_OFFSET_DSP_DYARID 0x504
#define REG_OFFSET_DSP_DYAWID 0x508
#define REG_OFFSET_USR_CONF0 0x1c

// bit definitions for REG_OFFSET_DSP_STAT
#define DSP_STAT_REG_BIT_STAT_VECTOR_SEL BIT_ULL(0)
#define DSP_STAT_REG_BIT_ARID_DYNM_EN BIT_ULL(4)
#define DSP_STAT_REG_BITS_ARMMUSID_MASK GENMASK(23, 16)

// bit definitions for REG_OFFSET_DSP_PRID
#define DSP_PRID_REG_BIT_PRID_MASK GENMASK(15, 0)

// bit definitions for REG_OFFSET_DSP_RESET
#define DSP_RESET_REG_BIT_RUNSTALL_ON_RESET BIT_ULL(0)
#define DSP_RESET_REG_BIT_CORE_RESET BIT_ULL(1)
#define DSP_RESET_REG_BIT_DEBUG_RESET BIT_ULL(2)

// bit definitions for REG_OFFSET_DSP_DYAWID
#define DSP_DYAWID_REG_BITS_ARMMUSID_MASK GENMASK(23, 16)
#define DSP_DYAWID_REG_BITS_AWMMUSID_MASK GENMASK(23, 16)

#define REG_DEFAULT_SIZE 0x10000
#define REG_OFFSET_SIZE 0x20
#define REG_OFFSET_SIZE_8 0x8

#define REG_OFFSET(reg, pro_id) (reg + (REG_OFFSET_SIZE * pro_id))

#define REG_OFFSET_8(reg, pro_id) (reg + (REG_OFFSET_SIZE_8 * pro_id))

/*
	aon syscon registers
*/
#define REG_OFFSET_SYSCON_DSP_CFG 0x330

// bit definitions for REG_OFFSET_SYSCON_DSP_CFG
#define SCU_DSPT_DIV_SEL BIT_ULL(19)
#define SCU_DSPT_DIV_EN BIT_ULL(23)

// from eswin/dsp/framework/lsp/memmap.xmm .dram1.perfdata(0x2813ffc8)
#define DSP_FLAT_ADDR 0x5b13fe70
#define DSP_FW_STATE_ADDR 0x5b13ffb0
#define DSP_PERF_START_ADDR 0x5b13ffc8
#define DIE_BASE_INTERVAL 0x20000000
#define DSP_CORE_INTERVAL 0x40000

enum dsp_irq_mode {
	DSP_IRQ_NONE,
	DSP_IRQ_LEVEL,
	DSP_IRQ_MAX,
};

struct es_dsp_hw {
	struct es_dsp *es_dsp;
	void __iomem *dbg_reg_base;
	struct regmap *map;
	struct regmap *con_map;
	struct clk *aclk;
	struct es_dsp_subsys *subsys;
	struct dma_pool *flat_dma_pool;

	dma_addr_t pts_iova;
	u32 pts_iova_size;
	u32 pts_phys_base;

	dma_addr_t iddr_iova;
	u32 iddr_size;
	void *iddr_ptr;

	struct platform_device *pdev;
	/* how IRQ is used to notify the device of incoming data */
	enum dsp_irq_mode device_irq_mode;
	/*
	 * device IRQ#
	 * dsp tx mailbox reg base
	 * dsp tx mailbox wr lock
	 * dsp rx mailbox reg base
	 * dsp tx mailbox int bit
	 * dsp send to mcu tx mailbox reg base
	 */
	u32 device_irq[6];
	enum dsp_irq_mode host_irq_mode;
	dma_addr_t device_uart_base;
	dma_addr_t device_uart_mutex_base;
	dma_addr_t mailbox_tx_reg_base;
	dma_addr_t mailbox_rx_reg_base;
	dma_addr_t mailbox_mcu_reg_base;

	void __iomem *uart_mutex_base;
};

int es_dsp_core_clk_enable(struct es_dsp *dsp)
{
	int ret;
	u32 val;
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	regmap_read(hw->map, REG_OFFSET_USR_CONF0, &val);
	dsp_debug("%s, %d, original usr conf0 val=0x%x.\n", __func__, __LINE__,
		  val);

	val |= (3 << (dsp->process_id * 2));
	ret = regmap_write(hw->map, REG_OFFSET_USR_CONF0, val);
	regmap_read(hw->map, REG_OFFSET_USR_CONF0, &val);
	dsp_debug("%s, %d, val=0x%x.\n", __func__, __LINE__, val);
	return ret;
}

int es_dsp_core_clk_disable(struct es_dsp *dsp)
{
	int ret;
	u32 val;
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	regmap_read(hw->map, REG_OFFSET_USR_CONF0, &val);
	dsp_debug("%s, %d, original val=0x%x.\n", __func__, __LINE__, val);

	val &= ~(3 << (dsp->process_id * 2));
	ret = regmap_write(hw->map, REG_OFFSET_USR_CONF0, val);
	regmap_read(hw->map, REG_OFFSET_USR_CONF0, &val);
	dsp_debug("%s, %d, val=0x%x.\n", __func__, __LINE__, val);
	return ret;
}

void es_dsp_reset(struct es_dsp_hw *hw)
{
	struct es_dsp *dsp = hw->es_dsp;
	int ret;

	if (NULL == dsp) {
		dsp_err("%s %d: failed to reset device\n", __func__, __LINE__);
	} else {
		ret = regmap_set_bits(hw->map,
				      REG_OFFSET(REG_OFFSET_DSP_RESET,
						 dsp->process_id),
				      DSP_RESET_REG_BIT_DEBUG_RESET);
		WARN_ON(0 != ret);

		ret = regmap_set_bits(hw->map,
				      REG_OFFSET(REG_OFFSET_DSP_RESET,
						 dsp->process_id),
				      DSP_RESET_REG_BIT_CORE_RESET);
		WARN_ON(0 != ret);
		mdelay(20);

		/* set processor id */
		ret = regmap_write_bits(
			hw->map,
			REG_OFFSET(REG_OFFSET_DSP_PRID, dsp->process_id),
			DSP_PRID_REG_BIT_PRID_MASK, dsp->process_id);
		WARN_ON(0 != ret);

		/* set reset vector */
		ret = regmap_write(hw->map,
				   REG_OFFSET(REG_OFFSET_DSP_START,
					      dsp->process_id),
				   dsp->firmware_dev_addr);
		WARN_ON(0 != ret);
		ret = regmap_set_bits(hw->map,
				      REG_OFFSET(REG_OFFSET_DSP_STAT,
						 dsp->process_id),
				      DSP_STAT_REG_BIT_STAT_VECTOR_SEL);
		WARN_ON(0 != ret);

		/* dereset dsp core */
		ret = regmap_clear_bits(hw->map,
					REG_OFFSET(REG_OFFSET_DSP_RESET,
						   dsp->process_id),
					DSP_RESET_REG_BIT_CORE_RESET);
		WARN_ON(0 != ret);

		/* set smmu id */
		ret = regmap_write_bits(
			hw->map,
			REG_OFFSET_8(REG_OFFSET_DSP_DYAWID, dsp->process_id),
			DSP_DYAWID_REG_BITS_AWMMUSID_MASK,
			(WIN2030_SID_DSP_0 + dsp->process_id) << 16);
		WARN_ON(0 != ret);

		ret = regmap_write_bits(
			hw->map,
			REG_OFFSET_8(REG_OFFSET_DSP_DYARID, dsp->process_id),
			DSP_DYAWID_REG_BITS_ARMMUSID_MASK,
			(WIN2030_SID_DSP_0 + dsp->process_id) << 16);
		WARN_ON(0 != ret);

		ret = win2030_dynm_sid_enable(dev_to_node(dsp->dev));
		WARN_ON(0 != ret);

		ret = regmap_write(hw->con_map, 0x330, 0xF0F0);
		WARN_ON(0 != ret);

		/* dereset dsp debug */
		ret = regmap_clear_bits(hw->map,
					REG_OFFSET(REG_OFFSET_DSP_RESET,
						   dsp->process_id),
					DSP_RESET_REG_BIT_DEBUG_RESET);
		WARN_ON(0 != ret);

		dev_info(dsp->dev, "reset device, ret %d\n", ret);
	}
	return;
}

int es_dsp_clk_disable(struct es_dsp *dsp)
{
	int ret;
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;
	bool enabled;
	u32 val;

	ret = es_dsp_core_clk_disable(dsp);
	if (ret) {
		dsp_debug("%s, %d, dsp core clk disable err, ret=%d.\n",
			  __func__, __LINE__, ret);
		return ret;
	}
	enabled = __clk_is_enabled(hw->subsys->aclk);
	regmap_read(hw->map, REG_OFFSET_USR_CONF0, &val);
	dsp_debug("%s, %d, enabled=%d, val=0x%x.\n", __func__, __LINE__,
		  enabled, val);

	if ((enabled == true && val == 0)) {
		dsp_debug("%s, %d, disable aclk.\n", __func__, __LINE__);
		clk_disable_unprepare(hw->subsys->aclk);
	}
	clk_disable_unprepare(hw->subsys->cfg_clk);
	dsp_debug("%s, %d, done.\n", __func__, __LINE__);
	return ret;
}
int es_dsp_clk_enable(struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;
	int ret;
	bool enabled;

	ret = clk_prepare_enable(hw->subsys->cfg_clk);
	if (ret) {
		dev_err(dsp->dev, "failed to enable cfg clk, ret=%d.\n", ret);
		return ret;
	}
	enabled = __clk_is_enabled(hw->subsys->aclk);
	if (!enabled) {
		ret = clk_prepare_enable(hw->subsys->aclk);
		if (ret) {
			dev_err(dsp->dev, "failed to enable aclk: %d\n", ret);
			return ret;
		}
	}

	ret = es_dsp_core_clk_enable(dsp);
	if (ret) {
		if (!enabled) {
			clk_disable_unprepare(hw->subsys->aclk);
		}
		return ret;
	}

	dsp_debug("enable dsp clock ok.\n");
	return 0;
}

int es_dsp_set_rate(struct es_dsp_hw *hw, unsigned long rate)
{
	struct es_dsp *dsp = hw->es_dsp;
	int ret;

	if (NULL == dsp) {
		dsp_err("%s %d: failed to get device\n", __func__, __LINE__);
		return -ENXIO;
	}
	rate = clk_round_rate(hw->aclk, rate);
	if (rate > 0) {
		ret = clk_set_rate(hw->aclk, rate);
		if (ret) {
			dev_err(dsp->dev, "failed to set aclk: %d\n", ret);
			return ret;
		}
	}
	dev_info(dsp->dev, "set device rate to %ldHZ\n", rate);
	return 0;
}

void es_dsp_halt(struct es_dsp_hw *hw)
{
	struct es_dsp *dsp = hw->es_dsp;
	int ret;

	if (NULL == dsp) {
		dsp_err("%s %d: failed to halt device\n", __func__, __LINE__);
	} else {
		ret = regmap_set_bits(hw->map,
				      REG_OFFSET(REG_OFFSET_DSP_RESET,
						 dsp->process_id),
				      DSP_RESET_REG_BIT_RUNSTALL_ON_RESET);
		WARN_ON(0 != ret);

		dev_info(dsp->dev, "halt device, ret %d\n", ret);
	}
	return;
}

void es_dsp_release(struct es_dsp_hw *hw)
{
	struct es_dsp *dsp = hw->es_dsp;
	int ret;
	unsigned int val;

	if (NULL == dsp) {
		dsp_err("%s %d: failed to release device\n", __func__,
			__LINE__);
	} else {
		ret = regmap_clear_bits(hw->map,
					REG_OFFSET(REG_OFFSET_DSP_RESET,
						   dsp->process_id),
					DSP_RESET_REG_BIT_RUNSTALL_ON_RESET);
		WARN_ON(0 != ret);
		regmap_read(hw->map,
			    REG_OFFSET(REG_OFFSET_DSP_RESET, dsp->process_id),
			    &val);

		dev_info(dsp->dev, "release device, ret %d, val=0x%x\n", ret,
			 val);
	}

	return;
}

int wait_for_current_tsk_done(struct es_dsp *dsp)
{
	const int sleep_retries = 5;
	const int wake_retries = 20;
	int i, j;

	for (i = 0; i < sleep_retries; i++) {
		for (j = 0; j < wake_retries; j++) {
			if (dsp->current_task == NULL) {
				break;
			}
			usleep_range(100, 5000);
		}

		if (j < wake_retries) {
			return 0;
		}
	}
	dsp_err("%s, %d, Timeout for wait current task done.\n", __func__, __LINE__);
	return -ETIMEDOUT;
}

static int check_dsp_fw_state(struct es_dsp *dsp)
{
	struct dsp_fw_state_t *dsp_state = (struct dsp_fw_state_t *)dsp->dsp_fw_state_base;
	if (dsp_state == NULL) {
		return 0;
	}
	dsp_err("%s, %d, exccause=0x%x, ps=0x%x, pc=0x%x.\n", __func__, __LINE__, dsp_state->exccause, dsp_state->ps, dsp_state->pc);
	dsp_err("%s, %d, fw_state=%d, npu_state=%d, dsp_state=%d, func_state=%d.\n", __func__, __LINE__, dsp_state->fw_state, dsp_state->npu_task_state, dsp_state->dsp_task_state, dsp_state->func_state);
	return 0;
}

static int dsp_send_msg_by_mbx(struct es_dsp *dsp, void *data)
{
	unsigned long flags;
	u32 tmp_data;
	int count = 0;
	struct eswin_mbox_msg *msg = (struct eswin_mbox_msg *)data;

	spin_lock_irqsave(&dsp->mbox_lock, flags);
	// TX FIFO FULL?
	while (true) {
		if (count > 3) {
			spin_unlock_irqrestore(&dsp->mbox_lock, flags);
			dsp_err("%s, %d, tx mbxlock = 0x%x, fifo status=0x%x.\n", __func__, __LINE__, readl(dsp->mbox_tx_base + ESWIN_MBOX_WR_LOCK),
					readl(dsp->mbox_tx_base + ESWIN_MBOX_FIFO_STATUS));
			check_dsp_fw_state(dsp);
			return -EBUSY;
		}
		writel(dsp->mbox_lock_bit,
		       dsp->mbox_tx_base + ESWIN_MBOX_WR_LOCK);
		if (!(readl(dsp->mbox_tx_base + ESWIN_MBOX_WR_LOCK) &
		      dsp->mbox_lock_bit) ||
		    (readl(dsp->mbox_tx_base + ESWIN_MBOX_FIFO_STATUS) &
		     BIT_ULL(0))) {
			udelay(10);
			count++;
			continue;
		}
		break;
	}

	tmp_data = (u32)msg->data;
	writel(tmp_data, dsp->mbox_tx_base + ESWIN_MBOX_WR_DATA0);

	tmp_data = (u32)(msg->data >> 32) | BIT(31);
	writel(tmp_data, dsp->mbox_tx_base + ESWIN_MBOX_WR_DATA1);
	// 写中断enable bit.

	writel(dsp->mbox_irq_bit, dsp->mbox_tx_base + ESWIN_MBOX_INT_CTRL);

	writel(0x0, dsp->mbox_tx_base + ESWIN_MBOX_WR_LOCK);
	spin_unlock_irqrestore(&dsp->mbox_lock, flags);
	return 0;
}

void dsp_send_invalid_code_seg(struct es_dsp_hw *hw, struct dsp_op_desc *op)
{
	es_dsp_h2d_msg h2d_msg;
	int ret;

	dsp_debug("request dsp invalid code seg addr:%x,size:%d\n",
		  op->iova_base, op->op_shared_seg_size);
	h2d_msg.command = DSP_CMD_INVALID_ICACHE;
	h2d_msg.size = op->op_shared_seg_size >> DSP_2M_SHIFT;
	h2d_msg.iova_ptr = op->iova_base;
	dsp_debug("DSP, send inv iov=0x%x.\n", op->iova_base);
	ret = dsp_send_msg_by_mbx(hw->es_dsp, (void *)&h2d_msg);
	if (ret < 0) {
		dev_err(NULL, "Failed to send message via mailbox\n");
	}
	return;
}

int es_dsp_send_irq(struct es_dsp_hw *hw, dsp_request_t *req)
{
	es_dsp_h2d_msg h2d_msg;
	int ret;
	h2d_msg.command = DSP_CMD_FLAT1;
	h2d_msg.allow_eval = req->allow_eval;
	h2d_msg.poll_mode = req->poll_mode;
	h2d_msg.sync_cache = req->sync_cache;
	h2d_msg.iova_ptr = req->dsp_flat1_iova;
	dsp_debug("DSP, send irq iova=0x%x.\n", req->dsp_flat1_iova);
	ret = dsp_send_msg_by_mbx(hw->es_dsp, (void *)&h2d_msg);
	if (ret < 0) {
		dev_err(NULL, " dsp mailbox send message busy, try again.\n");
	}
	return ret;
}

/*
	获取elf段对应的cpu虚拟地址
*/
static void *translate_to_cpu_va(struct es_dsp *dsp, Elf32_Phdr *phdr)
{
	if ((long)dsp->firmware_addr > (long)dsp->firmware_dev_addr) {
		return (void *)((long)phdr->p_paddr + (long)dsp->firmware_addr -
				(long)dsp->firmware_dev_addr);
	} else {
		return (void *)((long)phdr->p_paddr -
				((long)dsp->firmware_dev_addr -
				 (long)dsp->firmware_addr));
	}
}

static phys_addr_t translate_to_cpu_pa(struct es_dsp *dsp, Elf32_Phdr *phdr)
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

/*
	将elf段加载到DDR
*/
static int load_segment_to_sysmem(struct es_dsp *dsp, Elf32_Phdr *phdr)
{
	void *va = translate_to_cpu_va(dsp, phdr);

	memcpy(va, (void *)dsp->firmware->data + phdr->p_offset, phdr->p_memsz);
	return 0;
}

/*
 * 将elf段加载到DSP local memory
 *
 */
static int load_segment_to_iomem(struct es_dsp *dsp, Elf32_Phdr *phdr)
{
	phys_addr_t pa = translate_to_cpu_pa(dsp, phdr);
	void __iomem *p = ioremap(pa, phdr->p_memsz);

	if (!p) {
		dev_err(dsp->dev, "couldn't ioremap %pap x 0x%08x\n", &pa,
			(u32)phdr->p_memsz);
		return -EINVAL;
	}
	// writel_relaxed(0x8000, hw->dbg_reg_base + 0x668);

	memcpy_toio(p, (void *)dsp->firmware->data + phdr->p_offset,
		    ALIGN(phdr->p_filesz, 4));

	memset_io(p + ALIGN(phdr->p_filesz, 4), 0,
		  ALIGN(phdr->p_memsz - ALIGN(phdr->p_filesz, 4), 4));
	iounmap(p);
	return 0;
}

int es_dsp_load_fw_segment(struct es_dsp_hw *hw, const void *image,
			   Elf32_Phdr *phdr)
{
	void *va = NULL;
	phys_addr_t pa = 0;
	struct es_dsp *dsp = hw->es_dsp;

	if ((long)phdr->p_paddr >= (long)dsp->firmware_dev_addr) {
		va = translate_to_cpu_va(dsp, phdr);
		if (IS_ERR(va)) {
			dev_err(dsp->dev,
				"device smmu address 0x%08x could not be "
				"mapped to host virtual address",
				(u32)phdr->p_paddr);
			return -EINVAL;
		}
		dsp_info(
			"esdsp, loading segment (device 0x%08x) to virtual 0x%px\n",
			(u32)phdr->p_paddr, va);
		return load_segment_to_sysmem(dsp, phdr);
	} else {
		pa = translate_to_cpu_pa(dsp, phdr);
		if (pa == (phys_addr_t)OF_BAD_ADDR) {
			dev_err(dsp->dev,
				"device address 0x%08x could not be "
				"mapped to host physical address, pa 0x%llx",
				(u32)phdr->p_paddr, pa);
			return -EINVAL;
		}

		dsp_info(
			"esdsp,loading segment (device 0x%08x) to phy address 0x%llx\n",
			(u32)phdr->p_paddr, pa);
		return load_segment_to_iomem(dsp, phdr);
	}
}

int es_dsp_reboot_core(struct es_dsp_hw *hw)
{
	int ret;
	struct es_dsp_subsys *subsys = hw->subsys;

	if (!subsys || !subsys->dsp_subsys_status) {
		return -EINVAL;
	}

	ret = subsys->dsp_subsys_status();
	if (ret <= 0) {
		dsp_err("dsp subsys keep busing in 3 seconds, fail to restarting firmware.\n");
		ret = -EIO;
		return ret;
	}

	return 0;
}

int es_dsp_load_op(struct es_dsp_hw *hw, void *op_ptr)
{
	struct dsp_op_desc *op = (struct dsp_op_desc *)op_ptr;
	if (!op_ptr || !hw->es_dsp) {
		dsp_err("invalid parameter for load op");
		return -ENOMEM;
	}
	return dsp_load_op_file(hw->es_dsp, op);
}

int es_dsp_sync(struct es_dsp *dsp)
{
	int ret;

	msleep(1);
	ret = wait_event_timeout(dsp->hd_ready_wait, dsp->off != true,
				 msecs_to_jiffies(3000));
	if (!ret) {
		dsp_err("DSP cannot deliver ready cmd during sync.\n");
		return -ENODEV;
	}

	return 0;
}

static irqreturn_t dsp_mbox_irq(int irq, void *dev_id)
{
	u32 data0, data1;
	u64 message;
	struct es_dsp *dsp = (struct es_dsp *)dev_id;
	int ret;

	while (true) {
		data0 = readl(dsp->mbox_rx_base + ESWIN_MBOX_RD_DATA0);
		data1 = readl(dsp->mbox_rx_base + ESWIN_MBOX_RD_DATA1);
		if (!data1) {
			break;
		}
		message = ((u64)data1 << 32 | data0);
		writel(0x0, dsp->mbox_rx_base + ESWIN_MBOX_RD_DATA1);

		ret = dsp_irq_handler((void *)&message, dsp);
	}
	return IRQ_HANDLED;
}

void reset_uart_mutex(struct es_dsp_hw *hw)
{
	if (hw->uart_mutex_base) {
		writel(0, hw->uart_mutex_base + UART_MUTEX_UNIT_OFFSET);
	} else {
		dev_err(&hw->pdev->dev, "uart mutex addr is NULL\n");
	}
}

static void hw_uart_uninit(struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	if (hw->device_uart_base != 0) {
		iommu_unmap_rsv_iova(dsp->dev, NULL, hw->device_uart_base,
				     DSP_DEVICE_UART_IOVA_SIZE);
		hw->device_uart_base = 0;
	}

	if (hw->device_uart_mutex_base != 0) {
		iommu_unmap_rsv_iova(dsp->dev, NULL, hw->device_uart_mutex_base,
				     DSP_DEVICE_UART_MUTEX_IOVA_SIZE);
		hw->device_uart_mutex_base = 0;
	}
}

static inline int init_hw_uart(struct es_dsp_hw *hw)
{
	int ret;
	struct es_dsp *dsp = hw->es_dsp;

	ret = iommu_map_rsv_iova_with_phys(dsp->dev,
					   (dma_addr_t)DSP_DEVICE_UART_IOVA,
					   DSP_DEVICE_UART_IOVA_SIZE,
					   dsp->device_uart, IOMMU_MMIO);
	if (ret != 0) {
		dev_err(dsp->dev, "uart iommu map error\n");
		return ret;
	}
	hw->device_uart_base = DSP_DEVICE_UART_IOVA;

	ret = iommu_map_rsv_iova_with_phys(
		dsp->dev, (dma_addr_t)DSP_DEVICE_UART_MUTEX_IOVA,
		DSP_DEVICE_UART_MUTEX_IOVA_SIZE, UART_MUTEX_BASE_ADDR,
		IOMMU_MMIO);
	if (ret != 0) {
		dev_err(dsp->dev, "uart mutex iommu map error\n");
		goto err_mutex;
	}
	hw->device_uart_mutex_base = DSP_DEVICE_UART_MUTEX_IOVA;


	return ret;
err_iomap:
	iommu_unmap_rsv_iova(dsp->dev, NULL, hw->device_uart_mutex_base,
			     DSP_DEVICE_UART_MUTEX_IOVA_SIZE);
	hw->device_uart_mutex_base = 0;
err_mutex:
	iommu_unmap_rsv_iova(dsp->dev, NULL, hw->device_uart_base,
			     DSP_DEVICE_UART_IOVA_SIZE);
	hw->device_uart_base = 0;
err:
	return ret;
}

void dsp_uninit_mbox(struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	if (hw->mailbox_tx_reg_base != 0) {
		iommu_unmap_rsv_iova(dsp->dev, NULL, hw->mailbox_tx_reg_base,
				     DSP_DEVICE_EACH_IOVA_SIZE);
		hw->mailbox_tx_reg_base = 0;
	}

	if (hw->mailbox_rx_reg_base != 0) {
		iommu_unmap_rsv_iova(dsp->dev, NULL, hw->mailbox_rx_reg_base,
				     DSP_DEVICE_EACH_IOVA_SIZE);
		hw->mailbox_rx_reg_base = 0;
	}

	if (hw->mailbox_mcu_reg_base != 0) {
		iommu_unmap_rsv_iova(dsp->dev, NULL, hw->mailbox_mcu_reg_base,
				     U84_TO_MCU_IOVA_SIZE);
		hw->mailbox_mcu_reg_base = 0;
	}
}

int dsp_enable_mbox_clock(struct es_dsp *dsp)
{
	int ret;
	ret = clk_prepare_enable(dsp->mbox_pclk);
	if (ret) {
		dev_err(dsp->dev, "failed to enable host mailbox pclk: %d\n",
			ret);
		goto err_pclk;
	}
	ret = clk_prepare_enable(dsp->mbox_pclk_device);
	if (ret) {
		dev_err(dsp->dev,
			"failed to enable device mailbox pclk: %d\n", ret);
		goto err_pd_clock;
	}
	return 0;
err_pd_clock:
	clk_disable_unprepare(dsp->mbox_pclk);
err_pclk:
return ret;
}

void dsp_disable_mbox_clock(struct es_dsp *dsp)
{
	clk_disable_unprepare(dsp->mbox_pclk_device);
	clk_disable_unprepare(dsp->mbox_pclk);
}
static inline int init_hw_mailbox(struct es_dsp_hw *hw)
{
	int ret = 0;
	struct es_dsp *dsp = hw->es_dsp;

	reset_control_reset(dsp->mbox_rst);
	reset_control_reset(dsp->mbox_rst_device);
	spin_lock_init(&dsp->mbox_lock);

	dsp_info("%s: dev: irq num %d, tx phy base 0x%08x, tx wr lock 0x%08x,"
		 " rx phy base 0x%08x, irq bit 0x%08x\n",
		 __func__, hw->device_irq[0], hw->device_irq[1],
		 hw->device_irq[2], hw->device_irq[3], hw->device_irq[4]);

	ret = iommu_map_rsv_iova_with_phys(dsp->dev,
					   (dma_addr_t)DSP_DEVICE_MBX_TX_IOVA,
					   DSP_DEVICE_EACH_IOVA_SIZE,
					   hw->device_irq[1], IOMMU_MMIO);
	if (ret != 0) {
		hw->mailbox_tx_reg_base = 0;
		dsp_err("%s, mbox tx failed.\n", __func__);
		return ret;
	}

	hw->mailbox_tx_reg_base = DSP_DEVICE_MBX_TX_IOVA;

	ret = iommu_map_rsv_iova_with_phys(dsp->dev,
					   (dma_addr_t)DSP_DEVICE_MBX_RX_IOVA,
					   DSP_DEVICE_EACH_IOVA_SIZE,
					   hw->device_irq[3], IOMMU_MMIO);
	if (ret != 0) {
		hw->mailbox_rx_reg_base = 0;
		goto err_mbx_rx;
	}

	hw->mailbox_rx_reg_base = DSP_DEVICE_MBX_RX_IOVA;
	writel(dsp->mbox_irq_bit, dsp->mbox_tx_base + ESWIN_MBOX_INT_CTRL);

	ret = iommu_map_rsv_iova_with_phys(
		dsp->dev, (dma_addr_t)DSP_DEVICE_U84_TO_MCU_MBX,
		U84_TO_MCU_IOVA_SIZE, hw->device_irq[5], IOMMU_MMIO);
	if (ret != 0) {
		hw->mailbox_mcu_reg_base = 0;
		goto err_mbx_mcu;
	}
	hw->mailbox_mcu_reg_base = DSP_DEVICE_U84_TO_MCU_MBX;
	return 0;

err_mbx_mcu:
	iommu_unmap_rsv_iova(dsp->dev, NULL, hw->mailbox_rx_reg_base,
			     DSP_DEVICE_EACH_IOVA_SIZE);
	hw->mailbox_rx_reg_base = 0;
err_mbx_rx:
	iommu_unmap_rsv_iova(dsp->dev, NULL, hw->mailbox_tx_reg_base,
			     DSP_DEVICE_EACH_IOVA_SIZE);
	hw->mailbox_tx_reg_base = 0;
	return ret;
}

int dsp_get_mbx_node(struct platform_device *pdev)
{
	struct device_node *dsp_mbox = NULL;
	struct platform_device *mbox_pdev = NULL;
	struct es_dsp *dsp = platform_get_drvdata(pdev);
	int ret;
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	dsp_mbox = of_parse_phandle(pdev->dev.of_node, "dsp_mbox", 0);
	if (dsp_mbox == NULL) {
		dev_err(&pdev->dev, "dsp node have not mailbox node, err.\n");
		return -EINVAL;
	}

	mbox_pdev = of_find_device_by_node(dsp_mbox);
	if (mbox_pdev == NULL) {
		dev_err(&pdev->dev, "cannot find dsp mbox platform dev, err\n");
		return -EINVAL;
	}
	of_node_put(dsp_mbox);
	dsp->mbox_pdev = mbox_pdev;

	if (of_property_read_u32(mbox_pdev->dev.of_node, "lock-bit",
				 &dsp->mbox_lock_bit)) {
		dev_err(&pdev->dev, "failed to get lock_bit: %d\n", ret);
		return -ENXIO;
	}

	if (of_property_read_u32(mbox_pdev->dev.of_node, "irq-bit",
				 &dsp->mbox_irq_bit)) {
		dev_err(&pdev->dev, "failed to get irq_bit: %d\n", ret);
		return -ENXIO;
	}
	dsp->mbox_tx_res = platform_get_resource(mbox_pdev, IORESOURCE_MEM, 0);
	if (!dsp->mbox_tx_res) {
		dsp_err("error get dsp mbox tx mem.\n");
		return -ENOMEM;
	}

	dsp->mbox_rx_res = platform_get_resource(mbox_pdev, IORESOURCE_MEM, 1);
	if (!dsp->mbox_rx_res) {
		return -ENODEV;
	}

	dsp->mbox_pclk = devm_clk_get(&mbox_pdev->dev, "pclk_mailbox_host");
	if (IS_ERR(dsp->mbox_pclk)) {
		ret = PTR_ERR(dsp->mbox_pclk);
		dev_err(&pdev->dev, "failed to get host mailbox clock: %d\n",
			ret);
		return -ENODEV;
	}

	platform_set_drvdata(mbox_pdev, dsp);
	dsp->mbox_pclk_device = devm_clk_get(&mbox_pdev->dev, "pclk_mailbox_device");
	if (IS_ERR(dsp->mbox_pclk_device)) {
		ret = PTR_ERR(dsp->mbox_pclk_device);
		dev_err(&pdev->dev, "failed to get device mailbox clock: %d\n",
			ret);
		return ret;
	}
	dsp->mbox_rst = devm_reset_control_get_optional_exclusive(&mbox_pdev->dev, "rst");
	if (IS_ERR(dsp->mbox_rst)) {
		ret = PTR_ERR(dsp->mbox_rst);
		dev_err(&pdev->dev, "failed to get rst controller.\n");
		return ret;;
	}
	dsp->mbox_rst_device = devm_reset_control_get_optional_exclusive(&mbox_pdev->dev,
							       "rst_device");
	if (IS_ERR(dsp->mbox_rst_device)) {
		ret = PTR_ERR(dsp->mbox_rst_device);
		dev_err(&pdev->dev, "failed to get rst_device controller.\n");
		return ret;
	}
	dsp->mbox_irq = platform_get_irq(mbox_pdev, 0);
	if (dsp->mbox_irq < 0) {
		return dsp->mbox_irq;
	}

	ret = devm_request_threaded_irq(&mbox_pdev->dev, dsp->mbox_irq, dsp_mbox_irq,
					NULL, IRQF_ONESHOT,
					dev_name(&mbox_pdev->dev), dsp);
	if (ret < 0) {
		return ret;
	}

	ret = device_property_read_u32_array(&pdev->dev, "device-irq",
					     hw->device_irq,
					     ARRAY_SIZE(hw->device_irq));
	return 0;
}

int dsp_put_resource(struct es_dsp *dsp)
{
	if (dsp == NULL) {
		return -EINVAL;
	}
	clk_put(dsp->mbox_pclk);
	clk_put(dsp->mbox_pclk_device);
	reset_control_put(dsp->mbox_rst);
	reset_control_put(dsp->mbox_rst_device);
	free_irq(dsp->mbox_irq, dsp);
	dev_set_drvdata(&dsp->mbox_pdev->dev, NULL);
	dsp->mbox_pdev = NULL;
	return 0;
}

int dsp_get_resource(struct platform_device *pdev, struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;
	int ret;

	ret = dsp_get_mbx_node(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "get dsb mailbox node err.\n");
		return ret;
	}
	ret = device_property_read_u32(&pdev->dev, "process-id",
				       &(dsp->process_id));
	if (0 != ret) {
		dev_err(&pdev->dev, "failed to init process id\n");
		return ret;
	}
	dev_dbg(&pdev->dev, "dsp processor id=%d.\n", dsp->process_id);
	ret = device_property_read_u32(&pdev->dev, "numa-node-id",
				       &(dsp->numa_id));
	if (0 != ret) {
		dev_err(&pdev->dev, "failed to get numa node id\n");
		return ret;
	}
	dev_dbg(&pdev->dev, "dsp numa_id = %d.\n", dsp->numa_id);
	/* get aclk */
	hw->aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(hw->aclk)) {
		ret = PTR_ERR(hw->aclk);
		dev_err(&pdev->dev, "failed to get aclk: %ld\n", ret);
		return ret;
	}

	ret = device_property_read_string(dsp->dev, "firmware-name",
					  &dsp->firmware_name);
	if (ret == -EINVAL || ret == -ENODATA) {
		dev_dbg(dsp->dev,
			"no firmware-name property, not loading firmware");
	} else if (ret < 0) {
		dev_err(dsp->dev, "invalid firmware name (%d)", ret);
		return ret;
	}

	/* get uart reg base */
	ret = device_property_read_u32(&pdev->dev, "device-uart", &dsp->device_uart);
	if (0 != ret) {
		dev_err(&hw->pdev->dev, "Failed to get uart reg base\n");
		return ret;
	}
	return ret;
}

static long eswin_dsp_init_hw(struct es_dsp_hw *hw, int mem_idx)
{
	long ret = 0;
	struct es_dsp *dsp = hw->es_dsp;

	/* init uart */
	ret = init_hw_uart(hw);
	if (0 != ret) {
		dev_err(dsp->dev, "failed to init uart\n");
		goto err_uart;
	}
	/* init mailbox */
	ret = init_hw_mailbox(hw);
	if (0 != ret) {
		dev_err(dsp->dev, "failed to init mailbox\n");
		goto err_mb;
	}

	dev_dbg(dsp->dev,
		"host_irq_mode %d, proid=%d, numa_id=%d. "
		"uart base 0x%llx, mbox_tx_base 0x%llx, mbox_rx_base 0x%llx\n",
		hw->host_irq_mode, dsp->process_id, dsp->numa_id,
		hw->device_uart_base, hw->mailbox_tx_reg_base,
		hw->mailbox_rx_reg_base);

	return 0;
err_mb:
	hw_uart_uninit(dsp);
err_uart:
	return ret;
}

int es_dsp_pm_get_sync(struct es_dsp *dsp)
{
	int rc;

	rc = pm_runtime_resume_and_get(dsp->dev);
	if (rc < 0)
		return rc;

	return 0;
}

void es_dsp_pm_put_sync(struct es_dsp *dsp)
{
	pm_runtime_mark_last_busy(dsp->dev);
	pm_runtime_put_autosuspend(dsp->dev);
	return;
}

long es_dsp_hw_ioctl(struct file *flip, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static void es_dsp_low_level_uninit(struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	reset_uart_mutex((struct es_dsp_hw *)dsp->hw_arg);

	if (dsp->mbox_pdev) {
		dsp_uninit_mbox(dsp);
	}

	hw_uart_uninit(dsp);

}

void dsp_free_flat_mem(struct es_dsp *dsp, u32 size, void *cpu,
		       dma_addr_t dma_addr)
{
	struct es_dsp_hw *hw = dsp->hw_arg;
	dma_pool_free(hw->flat_dma_pool, cpu, dma_addr);
}

void *dsp_alloc_flat_mem(struct es_dsp *dsp, u32 dma_len, dma_addr_t *dma_addr)
{
	struct es_dsp_hw *hw = dsp->hw_arg;
	void *flat = NULL;
	flat = dma_pool_alloc(hw->flat_dma_pool, GFP_KERNEL, dma_addr);
	return flat;
}

void es_dsp_put_subsys(struct es_dsp *dsp)
{
	struct es_dsp_subsys *subsys;
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	if (!hw) {
		dsp_err("%s, %d, hw is null, err.\n", __func__, __LINE__);
		return;
	}
	subsys = hw->subsys;
	put_device(&subsys->pdev->dev);
	module_put(subsys->pdev->dev.driver->owner);
	return;
}

int es_dsp_get_subsys(struct platform_device *pdev, struct es_dsp *dsp)
{
	struct device *parent;
	struct es_dsp_subsys *subsys;
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	parent = pdev->dev.parent;
	subsys = dev_get_drvdata(parent);
	if (IS_ERR_OR_NULL(subsys)) {
		return -EPROBE_DEFER;
	}
	if (!try_module_get(subsys->pdev->dev.driver->owner)) {
		dsp_err("error try get dsp subsys module.\n");
		return -EIO;
	}

	get_device(&subsys->pdev->dev);

	hw->map = subsys->map;
	hw->con_map = subsys->con_map;
	hw->dbg_reg_base = subsys->con_reg_base;
	hw->subsys = subsys;
	return 0;
}



int es_dsp_map_resource(struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;
	int ret;
	unsigned long base;

	hw->flat_dma_pool = dma_pool_create("dsp_flat_dma", dsp->dev,
					    sizeof(struct es_dsp_flat1_desc) +
						    sizeof(es_dsp_buffer) *
							    BUFFER_CNT_MAXSIZE,
					    64, 0);
	if (!hw->flat_dma_pool) {
		dsp_err("cat not create flat dma pool.\n");
		ret = -ENOMEM;
		return ret;
	}

	dsp->mbox_tx_base =
		devm_ioremap(&dsp->mbox_pdev->dev, dsp->mbox_tx_res->start, resource_size(dsp->mbox_tx_res));
	if (IS_ERR(dsp->mbox_tx_base)) {
		dsp_err("ioremap for dsp mbox tx register.\n");
		return -EIO;
	}

	dsp->mbox_rx_base =
		devm_ioremap(&dsp->mbox_pdev->dev, dsp->mbox_rx_res->start, resource_size(dsp->mbox_rx_res));
	if (IS_ERR(dsp->mbox_rx_base)) {
		return -EIO;
	}

	hw->uart_mutex_base =
		ioremap(UART_MUTEX_BASE_ADDR, DSP_DEVICE_UART_MUTEX_IOVA_SIZE);
	if (!hw->uart_mutex_base) {
		dev_err(&hw->pdev->dev, "ioremap error\n");
		ret = -ENOMEM;
		return ret;
	}

	base = DSP_PERF_START_ADDR + dsp->numa_id * DIE_BASE_INTERVAL +
	       DSP_CORE_INTERVAL * dsp->process_id;

	dsp->perf_reg_base = devm_ioremap(dsp->dev, base, sizeof(es_dsp_perf_info));
	if (!dsp->perf_reg_base) {
		dev_err(dsp->dev, "ioremap for perf reg err.\n");
		return -ENOMEM;
	}

	base = DSP_FW_STATE_ADDR + dsp->numa_id * DIE_BASE_INTERVAL +
	       DSP_CORE_INTERVAL * dsp->process_id;
	dsp->dsp_fw_state_base = devm_ioremap(dsp->dev, base, 0x18);
	if (!dsp->dsp_fw_state_base) {
		dev_err(dsp->dev, "ioremap for dsp fw state err.\n");
		return -ENOMEM;
	}

	base = DSP_FLAT_ADDR + dsp->numa_id * DIE_BASE_INTERVAL +
	       DSP_CORE_INTERVAL * dsp->process_id;
	dsp->flat_base = devm_ioremap(dsp->dev, base, 0x140);
	if (!dsp->flat_base) {
		dev_err(dsp->dev, "ioremap for dsp flat base err.\n");
		return -ENOMEM;
	}

	return 0;
}

int es_dsp_unmap_resource(struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	if (hw->flat_dma_pool != NULL) {
		dma_pool_destroy(hw->flat_dma_pool);
		hw->flat_dma_pool = NULL;
	}

	if (dsp->mbox_rx_base != NULL) {
		devm_iounmap(&dsp->mbox_pdev->dev, dsp->mbox_rx_base);
		dsp->mbox_rx_base = NULL;
	}
	if (dsp->mbox_tx_base != NULL) {
		devm_iounmap(&dsp->mbox_pdev->dev, dsp->mbox_tx_base);
		dsp->mbox_tx_base = NULL;
	}
	if (hw->uart_mutex_base != NULL) {

		iounmap(hw->uart_mutex_base);
		hw->uart_mutex_base = NULL;
	}
	return 0;
}

int es_dsp_hw_init(struct es_dsp *dsp)
{
	int ret;
	struct device *parent;
	struct es_dsp_subsys *subsys;
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	dev_info(dsp->dev, "\n\ndsp hw init begin\n");

	if (!hw)
		return -ENOMEM;

	hw->iddr_iova = DSP_IDDR_IOVA;
	hw->iddr_size = DSP_IDDR_IOVA_SIZE;
	hw->iddr_ptr = iommu_map_rsv_iova(dsp->dev, hw->iddr_iova,
					  hw->iddr_size, GFP_KERNEL, 0);
	if (!hw->iddr_ptr) {
		dsp_err("Err, map iddr iova.\n");
		ret = -ENOMEM;
		goto err_iddr;
	}

	ret = eswin_dsp_init_hw(hw, 0);
	if (ret < 0) {
		dsp_err("%s, eswin_dsp_init_hw failed, ret=%d.\n", __func__,
			ret);
		goto err_init_hw;
	}

	dsp->firmware_dev_addr = DSP_FIRMWARE_IOVA;
	dsp->firmware_addr =
		iommu_map_rsv_iova(dsp->dev, (dma_addr_t)dsp->firmware_dev_addr,
				   DSP_FIRMWARE_IOVA_SIZE, GFP_KERNEL, 0);

	if (IS_ERR_OR_NULL(dsp->firmware_addr)) {
		dsp_err("failed to alloc firmware memory\n");
		ret = -ENOMEM;
		goto err_map_firm;
	}

	hw->pts_iova = DSP_PTS_IOVA;
	hw->pts_iova_size = DSP_PTS_IOVA_SIZE;
	hw->pts_phys_base = 0x51840000;
	ret = iommu_map_rsv_iova_with_phys(dsp->dev, (dma_addr_t)DSP_PTS_IOVA,
					   DSP_PTS_IOVA_SIZE, 0x51840000,
					   IOMMU_MMIO);
	if (ret != 0) {
		dev_err(dsp->dev, "iommu map dsp pts phy error.\n");
		hw->pts_iova = 0;
		goto err;
	}
	es_dsp_set_rate(hw, dsp->rate);
	dev_dbg(dsp->dev, "firmware-name:%s.\n", dsp->firmware_name);
	return 0;
err:
	iommu_unmap_rsv_iova(dsp->dev, dsp->firmware_addr,
			     dsp->firmware_dev_addr, DSP_FIRMWARE_IOVA_SIZE);
	dsp->firmware_addr = NULL;

err_map_firm:
	es_dsp_low_level_uninit(dsp);
err_init_hw:
	iommu_unmap_rsv_iova(dsp->dev, hw->iddr_ptr, hw->iddr_iova,
			     hw->iddr_size);
	hw->iddr_ptr = NULL;
err_iddr:
	return ret;
}

void dsp_disable_irq(struct es_dsp *dsp)
{
	if (dsp->mbox_irq) {
		disable_irq(dsp->mbox_irq);
	}
	synchronize_irq(dsp->mbox_irq);
}

int dsp_enable_irq(struct es_dsp *dsp)
{
	if (dsp->mbox_irq) {
		enable_irq(dsp->mbox_irq);
	}
}

void es_dsp_hw_uninit(struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;
	struct es_dsp_subsys *subsys = hw->subsys;

	if (hw->iddr_ptr != NULL) {
		iommu_unmap_rsv_iova(dsp->dev, hw->iddr_ptr, hw->iddr_iova,
				     hw->iddr_size);
		hw->iddr_ptr = NULL;
	}

	if (dsp->firmware_addr != NULL) {
		iommu_unmap_rsv_iova(dsp->dev, dsp->firmware_addr,
				     dsp->firmware_dev_addr,
				     DSP_FIRMWARE_IOVA_SIZE);
	}

	es_dsp_low_level_uninit(dsp);

	if (hw->pts_iova != 0) {
		iommu_unmap_rsv_iova(dsp->dev, NULL, hw->pts_iova,
				     hw->pts_iova_size);
		hw->pts_iova = 0;
	}
}

void dsp_free_hw(struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;
	if (!hw) {
		return;
	}

	devm_kfree(dsp->dev, hw);
	dsp->hw_arg = NULL;
	return;
}

int dsp_alloc_hw(struct platform_device *pdev, struct es_dsp *dsp)
{
	struct es_dsp_hw *hw =
		devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	if (!hw) {
		dsp_err("%s, %d, alloc hw err.\n", __func__, __LINE__);
		return -ENOMEM;
	}

	dsp->hw_arg = hw;
	hw->pdev = pdev;
	hw->es_dsp = dsp;
	return 0;
}

int es_dsp_platform_init(void)
{
	return 0;
}

int es_dsp_platform_uninit(void)
{
	return 0;
}
