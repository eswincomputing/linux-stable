/****************************************************************************
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 * The GPL License (GPL)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 *
 *****************************************************************************
 *
 * Note: This software is released under dual MIT and GPL licenses. A
 * recipient may use this file under the terms of either the MIT license or
 * GPL License. If you wish to use only one license not the other, you can
 * indicate your decision by deleting one of the above license notices in your
 * version of this file.
 *
 *****************************************************************************/

#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>

#include <linux/iommu.h>
#include <linux/mfd/syscon.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>

#include "dw200_fe.h"
#include "dw200_ioctl.h"
#include "vivdw200_irq_queue.h"
#include "dw200_dump.h"

#define VIVCAM_DWE_NAME "es_dewarp"
#define VIVCAM_DWE_MAXCNT 1

#define VSE_REG_INDEX (0)
#define DWE_REG_INDEX (1)

#define AWSMMUSID GENMASK(31, 24) // The sid of write operation
#define AWSMMUSSID GENMASK(23, 16) // The ssid of write operation
#define ARSMMUSID GENMASK(15, 8) // The sid of read operation
#define ARSMMUSSID GENMASK(7, 0) // The ssid of read operation

#define VVCAM_DW_CLK_HIGHEST 594000000
#define VVCAM_AXI_CLK_HIGHEST 800000000

#define VI_TOP_CLK 0x51030040
#define VI_TOP_CLK_ENABLE_VAL 0xFFFF

#define IS_BIT_SET(reg, bit) ((reg) & (1 << (bit)))
#define SET_BIT(reg, bit) ((reg) |= (1 << (bit)))

struct es_dewarp_driver_dev {
	struct cdev cdev;
	dev_t devt;
	struct class *class;
	struct mutex vvmutex;
	void *private;
	unsigned int irq_num;
	unsigned int irq_num_vse;
	bool irq_trigger;
	wait_queue_head_t irq_wait;

	wait_queue_head_t trigger_wq;
	atomic_t trigger_atom;
	int suspended;
};

struct es_dw200_private {
	struct es_dewarp_driver_dev *pdriver_dev;
	struct dw200_subdev dw200;
	// dwe info
	u64 lut_map_addr;
	atomic_t dwe_reserved_atom;
	// vse info
	atomic_t vse_reserved_atom;
	struct heap_root heap_root;
};

#ifdef ES_DW200_SDK
#define VSE_OFFLINE_MODE 0
#define ISP_ONLINE_VSE_MODE 1
#define DWE_ONLINE_VSE_MODE 2
#define ES_WAIT_TIMEOUT_MS msecs_to_jiffies(10000)
#define ES_BUSY 0
#define ES_IDLE 1
#define RET_HW_COMMOND 0x11
atomic_t vse_online_mode_atomic = ATOMIC_INIT(VSE_OFFLINE_MODE);
static DECLARE_WAIT_QUEUE_HEAD(dwe_irq_wait_q);
static DECLARE_WAIT_QUEUE_HEAD(vse_irq_wait_q);
static DEFINE_SPINLOCK(dwe_irq_lock);
static DEFINE_SPINLOCK(vse_irq_lock);
atomic_t dwe_irq_trigger_mis = ATOMIC_INIT(0);
atomic_t vse_irq_trigger_mis = ATOMIC_INIT(0);
static struct semaphore dwe_sem;
static struct semaphore vse_sem;
static DECLARE_WAIT_QUEUE_HEAD(dwe_reserve_wait_q);
static DECLARE_WAIT_QUEUE_HEAD(vse_reserve_wait_q);
static int dwe_status = ES_IDLE;
static int vse_status = ES_IDLE;
#endif // ES_DW200_SDK

static unsigned int es_dewarp_major = 0;
static unsigned int es_dewarp_minor = 0;
struct class *es_dewarp_class;
static unsigned int devise_register_index = 0;

static bool fe_enable = false;
module_param(fe_enable, bool, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(fe_enable, "FE(command buffer) enable for DW200");

static unsigned int dewarp_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;
	struct es_dw200_private *pes_dw200_priv = filp->private_data;
	struct es_dewarp_driver_dev *pdriver_dev = pes_dw200_priv->pdriver_dev;
	struct dw200_subdev *pdw200 =
		(struct dw200_subdev *)pdriver_dev->private;

	vivdw200_mis_list_t *Vse_pCList = &pdw200->vse_circle_list;
	vivdw200_mis_list_t *Dwe_pCList = &pdw200->dwe_circle_list;

	poll_wait(filp, &pdriver_dev->irq_wait, wait);

	// pr_info("poll dwe_irq %d\n", pdriver_dev->irq_trigger);

	if (pdriver_dev->irq_trigger) {
		mask |= POLLIN | POLLRDNORM;
		// pr_info("poll notify user space\n");
		pdriver_dev->irq_trigger = false;
	} else if ((Vse_pCList->pRead != Vse_pCList->pWrite) ||
		   (Dwe_pCList->pRead != Dwe_pCList->pWrite)) {
		mask |= POLLIN | POLLRDNORM;
	}

	return mask;
}

#ifdef ES_DW200_SDK
static int triggerDwe(struct es_dw200_private *pes_dw200_priv)
{
	struct dw200_subdev *pdw200_dev = &pes_dw200_priv->dw200;
	int ret = -1;

	pr_debug("%s, In \n", __FUNCTION__);
	ret = dwe_reset(pdw200_dev);

	ret = dwe_disable_irq(pdw200_dev);

	pr_debug("%s, set dwe's params\n", __FUNCTION__);
	ret = dwe_s_params(pdw200_dev);

	pr_debug("%s, set output buffer[%lld]\n", __FUNCTION__,
		 pdw200_dev->buf_info[DWE_OUTPUT_BUFFER_0].addr);
	ret = dwe_set_buffer(pdw200_dev,
			     pdw200_dev->buf_info[DWE_OUTPUT_BUFFER_0].addr);

	pr_debug("%s, set lut map addr:%lld \n", __FUNCTION__,
		 pes_dw200_priv->lut_map_addr);
	dwe_set_lut(pdw200_dev, pes_dw200_priv->lut_map_addr);

	pr_debug("%s, set input dma addr:%lld \n", __FUNCTION__,
		 pdw200_dev->buf_info[DWE_INPUT_BUFFER_0].addr);
	dwe_start_dma_read(pdw200_dev,
			   pdw200_dev->buf_info[DWE_INPUT_BUFFER_0].addr);

	ret = dwe_enable_bus(pdw200_dev, true);
	ret = dwe_start(pdw200_dev);

	pr_debug("%s, Out\n", __FUNCTION__);
	return 0;
}

static int triggerVse(struct es_dw200_private *pes_dw200_priv)
{
	struct dw200_subdev *pdw200_dev = &pes_dw200_priv->dw200;
	int ret = -1;
	u64 vse_output_addrs[3] = { 0 };
	int i = 0;

	pr_debug("%s, 1.reset vse\n", __FUNCTION__);
	ret = vse_reset(pdw200_dev);

	pr_debug("%s, 2.set vse's params\n", __FUNCTION__);
	ret = vse_s_params(pdw200_dev);

	for (i = 0; i < 3; i++) {
		vse_output_addrs[i] =
			pdw200_dev->buf_info[VSE_OUTPUT_BUFFER_0 + i].addr;
	}
	pr_debug("%s, 3.update output buffer[%lld, %lld, %lld]\n", __FUNCTION__,
		 vse_output_addrs[0], vse_output_addrs[1], vse_output_addrs[2]);
	vse_update_buffers(pdw200_dev, vse_output_addrs);

	pr_debug("%s, 4.set mi info\n", __FUNCTION__);
	vse_update_mi_info(pdw200_dev);

	pr_debug("%s, 5.mask vse irq \n", __FUNCTION__);
	vse_mask_irq(pdw200_dev, 0x00002000);

	pr_debug("%s, 6.set input dma buffer info: %lld n", __FUNCTION__,
		 pdw200_dev->buf_info[VSE_INPUT_BUFFER_0].addr);
	vse_start_dma_read(pdw200_dev,
			   pdw200_dev->buf_info[VSE_INPUT_BUFFER_0].addr);

	pr_debug("%s, 7.trigger ok.\n", __FUNCTION__);
	return 0;
}

static int triggerDweVse(struct es_dw200_private *pes_dw200_priv)
{
	struct dw200_subdev *pdw200_dev = &pes_dw200_priv->dw200;
	int ret = -1;
	u64 vse_output_addrs[3] = { 0 };
	int i = 0;

	pr_debug("%s, 1.reset \n", __FUNCTION__);
	ret = dwe_reset(pdw200_dev);
	ret = vse_reset(pdw200_dev);

	pr_debug("%s, 2.disable dwe Irq\n", __FUNCTION__);
	ret = dwe_disable_irq(pdw200_dev);

	pr_debug("%s, 3.set params and sleep 10ms\n", __FUNCTION__);
	ret = dwe_s_params(pdw200_dev);
	ret = vse_s_params(pdw200_dev);
	msleep(10);

	for (i = 0; i < 3; i++) {
		vse_output_addrs[i] =
			pdw200_dev->buf_info[VSE_OUTPUT_BUFFER_0 + i].addr;
	}
	pr_debug("%s, 3.update output buffer[%lld, %lld, %lld]\n", __FUNCTION__,
		 vse_output_addrs[0], vse_output_addrs[1], vse_output_addrs[2]);
	vse_update_buffers(pdw200_dev, vse_output_addrs);

	pr_debug("%s, 5.set vse mi info\n", __FUNCTION__);
	vse_update_mi_info(pdw200_dev);

	pr_debug("%s, 6.mask vse irq \n", __FUNCTION__);
	vse_mask_irq(pdw200_dev, 0x00007007);

	pr_debug("%s, 7.set dwe output buffer[%lld]\n", __FUNCTION__,
		 pdw200_dev->buf_info[DWE_OUTPUT_BUFFER_0].addr);
	ret = dwe_set_buffer(pdw200_dev,
			     pdw200_dev->buf_info[DWE_OUTPUT_BUFFER_0].addr);

	pr_debug("%s, 8.set dwe lut map addr[%lld]\n", __FUNCTION__,
		 pes_dw200_priv->lut_map_addr);
	dwe_set_lut(pdw200_dev, pes_dw200_priv->lut_map_addr);

	pr_debug("%s, 9.set dwe input dma addr:%lld \n", __FUNCTION__,
		 pdw200_dev->buf_info[DWE_INPUT_BUFFER_0].addr);
	dwe_start_dma_read(pdw200_dev,
			   pdw200_dev->buf_info[DWE_INPUT_BUFFER_0].addr);

	pr_debug("%s, 10.set dwe enable bus and start dwe\n", __FUNCTION__);
	ret = dwe_enable_bus(pdw200_dev, true);
	ret = dwe_start(pdw200_dev);
	return 0;
}

static long reserveDwe(struct es_dw200_private *pes_dw200_priv)
{
	pr_debug("%s before down_interruptible, sema.count: %d\n", __func__,
		 dwe_sem.count);
	/* reserve dwe */
	if (down_interruptible(&dwe_sem))
		return -ERESTARTSYS;
	pr_debug("%s after down_interruptible, sema.count: %d\n", __func__,
		 dwe_sem.count);

	/* lock a core that has specific format*/
	if (wait_event_interruptible(dwe_reserve_wait_q, dwe_status) != 0)
		return -ERESTARTSYS;
	dwe_status = ES_BUSY;
	atomic_inc(&pes_dw200_priv->dwe_reserved_atom);
	return dwe_status;
}

static void releaseDwe(struct es_dw200_private *pes_dw200_priv)
{
	if (atomic_read(&pes_dw200_priv->dwe_reserved_atom) > 0) {
		pr_debug("%s before up, sema.count: %d\n", __func__,
			 dwe_sem.count);
		dwe_status = ES_IDLE;
		up(&dwe_sem);
		wake_up_interruptible_all(&dwe_reserve_wait_q);
		atomic_dec(&pes_dw200_priv->dwe_reserved_atom);
		pr_debug("%s after up, sema.count: %d\n", __func__,
			 dwe_sem.count);
	} else {
		pr_err("no reserved resources, no release.\n");
	}
}

static long reserveVse(struct es_dw200_private *pes_dw200_priv)
{
	pr_debug("%s before down_interruptible, sema.count: %d\n", __func__,
		 vse_sem.count);
	/* reserve vse */
	if (down_interruptible(&vse_sem))
		return -ERESTARTSYS;
	pr_debug("%s after down_interruptible, sema.count: %d\n", __func__,
		 vse_sem.count);

	/* lock a core that has specific format*/
	if (wait_event_interruptible(vse_reserve_wait_q, vse_status) != 0)
		return -ERESTARTSYS;
	vse_status = ES_BUSY;
	atomic_inc(&pes_dw200_priv->vse_reserved_atom);
	return vse_status;
}

static void releaseVse(struct es_dw200_private *pes_dw200_priv)
{
	if (atomic_read(&pes_dw200_priv->vse_reserved_atom) > 0) {
		pr_debug("%s before up, sema.count: %d\n", __func__,
			 vse_sem.count);
		vse_status = ES_IDLE;
		up(&vse_sem);
		wake_up_interruptible_all(&vse_reserve_wait_q);
		atomic_dec(&pes_dw200_priv->vse_reserved_atom);
		pr_debug("%s after up, sema.count: %d\n", __func__,
			 vse_sem.count);
	} else {
		pr_err("no reserved resources, no release.\n");
	}
}

static int CheckIrq(int is_dwe)
{
	unsigned long flags;
	u32 mis = 0;
	int rdy = 0;
	if (is_dwe) {
		spin_lock_irqsave(&dwe_irq_lock, flags);
		mis = atomic_read(&dwe_irq_trigger_mis);
		if (mis) {
			rdy = 1;
		}
		spin_unlock_irqrestore(&dwe_irq_lock, flags);
	} else {
		spin_lock_irqsave(&vse_irq_lock, flags);
		mis = atomic_read(&vse_irq_trigger_mis);
		if (mis) {
			rdy = 1;
		}
		spin_unlock_irqrestore(&vse_irq_lock, flags);
	}
	return rdy;
}

irqreturn_t dwe_isr(int irq, void *dev_id)
{
	struct es_dewarp_driver_dev *pdriver_dev = dev_id;
	struct dw200_subdev *pdw200 =
		(struct dw200_subdev *)pdriver_dev->private;
	u32 dwe_mis = 0;
	unsigned long flags;

	spin_lock_irqsave(&dwe_irq_lock, flags);
	dwe_read_irq((struct dw200_subdev *)pdw200, &dwe_mis);
	dwe_mis = dwe_mis & 0x1;
	if (0 != dwe_mis) {
		dwe_clear_irq((struct dw200_subdev *)pdw200, dwe_mis << 24);
		atomic_set(&dwe_irq_trigger_mis, dwe_mis);
		wake_up_interruptible_all(&dwe_irq_wait_q);
		atomic_dec(&pdriver_dev->trigger_atom);
		wake_up_interruptible(&pdriver_dev->trigger_wq);
		spin_unlock_irqrestore(&dwe_irq_lock, flags);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&dwe_irq_lock, flags);
	return IRQ_NONE;
}

irqreturn_t vse_isr(int irq, void *dev_id)
{
	struct es_dewarp_driver_dev *pdriver_dev = dev_id;
	struct dw200_subdev *pdw200 =
		(struct dw200_subdev *)pdriver_dev->private;
	u32 vse_mis = 0;
	unsigned long flags;

	DEBUG_PRINT("%s enter\n", __func__);

	spin_lock_irqsave(&vse_irq_lock, flags);
	vse_read_irq((struct dw200_subdev *)pdw200, &vse_mis);
	DEBUG_PRINT(" %s vse mis 0x%08x\n", __func__, vse_mis);
	if (vse_mis) {
		vse_clear_irq((struct dw200_subdev *)pdw200, vse_mis);
		atomic_set(&vse_irq_trigger_mis, vse_mis);
		wake_up_interruptible_all(&vse_irq_wait_q);
		atomic_dec(&pdriver_dev->trigger_atom);
		wake_up_interruptible(&pdriver_dev->trigger_wq);
		spin_unlock_irqrestore(&vse_irq_lock, flags);
		DEBUG_PRINT("%s vse frame ready, vse mis 0x%08x\n", __func__,
			    vse_mis);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&vse_irq_lock, flags);
	return IRQ_NONE;
}

static long waitDweDone(struct es_dw200_private *pes_dw200_priv, long timeout)
{
	int ret = -1;
	u32 reg = 0;
	u32 irq_trigger = 0;
	struct dw200_subdev *pdwe_dev = &pes_dw200_priv->dw200;
	ret = wait_event_interruptible_timeout(dwe_irq_wait_q, CheckIrq(1),
					       timeout);
	// ret = wait_event_interruptible(dwe_irq_wait_q, atomic_read(&dwe_irq_trigger_mis));
	if (ret < 0) {
		pr_err("dwe wait_event_interruptible interrupted\n");
		ret = -ERESTARTSYS;
		return ret;
	} else if (ret == 0) {
		pr_err("Error: %s dwe process timeout %lu, ret:%d\n", __func__,
		       timeout, ret);
		printDweInfo(&pdwe_dev->dwe_info);

		// when dwe timeout, need wait vse idle,than do top reset
		pr_err("do dw200 top reset\n");
		wait_event_interruptible(vse_reserve_wait_q, vse_status);
		reset_control_reset(pdwe_dev->dw_crg.rstc_dwe);
		ret = -ETIMEDOUT;
	} else {
		dwe_disable_irq(pdwe_dev);
		dwe_enable_bus(pdwe_dev, false);
	}
	irq_trigger = atomic_read(&dwe_irq_trigger_mis);
	if (irq_trigger & INT_FRAME_DONE) {
		pr_debug("wait dwe done ok, irq_trigger:%08x\n", irq_trigger);
	}

	atomic_set(&dwe_irq_trigger_mis, 0);
	/*clean all irq*/
	dwe_clear_irq(pdwe_dev, INT_RESET_MASK);
	reg = dwe_read_reg(pdwe_dev, BUS_CTRL);
	dwe_write_reg(pdwe_dev, BUS_CTRL, reg & ~DEWRAP_BUS_CTRL_ENABLE_MASK);
	return ret;
}

static long waitVseDone(struct es_dw200_private *pes_dw200_priv, long timeout)
{
	int ret = -1;
	struct dw200_subdev *pdwe_dev = &pes_dw200_priv->dw200;

	ret = wait_event_interruptible_timeout(vse_irq_wait_q, CheckIrq(0),
					       timeout);
	// ret = wait_event_interruptible(vse_irq_wait_q, CheckIrq(0));
	if (ret < 0) {
		pr_err("wait_event_interruptible interrupted\n");
		ret = -ERESTARTSYS;
	} else if (ret == 0) {
		pr_err("Error: %s vse process timeout %lu, ret:%d\n", __func__,
		       timeout, ret);
		printVseInfo(&pdwe_dev->vse_info);
		readVseReg(pdwe_dev);
		// when vse timeout, need wait dwe idle,than do top reset
		pr_err("do dw200 top reset\n");
		wait_event_interruptible(dwe_reserve_wait_q, dwe_status);
		reset_control_reset(pdwe_dev->dw_crg.rstc_dwe);
		ret = -ETIMEDOUT;
	}
	atomic_set(&vse_irq_trigger_mis, 0);
	vse_write_reg(pdwe_dev, VSE_REG_MI_ICR, 0xffffffff);
	vse_write_reg(pdwe_dev, VSE_REG_MI_ICR1, 0xffffffff);
	/*stop the axi bus*/
	vse_write_reg(pdwe_dev, VSE_REG_MI0_BUS_ID, 0);
	vse_write_reg(pdwe_dev, VSE_REG_MI1_BUS_ID, 0);
	return ret;
}

long es_dw200_ioctl(struct es_dw200_private *pes_dw200_priv, unsigned int cmd,
		    void *arg)
{
	int ret = -1;
	struct dw200_subdev *pdwe_dev = &pes_dw200_priv->dw200;
	struct es_dewarp_driver_dev *pdriver_dev = pes_dw200_priv->pdriver_dev;
	u64 addr;

	switch (cmd) {
	// ES_DW200_SDK
	case DWEIOC_WAIT_DWE_DONE: {
		ret = waitDweDone(pes_dw200_priv, ES_WAIT_TIMEOUT_MS);
		return ret;
	}
	case DWEIOC_RESERVE: {
		ret = reserveDwe(pes_dw200_priv);
		return ret;
	}
	case DWEIOC_RELEASE: {
		releaseDwe(pes_dw200_priv);
		return 0;
	}
	case VSEIOC_WAIT_VSE_DONE: {
		ret = waitVseDone(pes_dw200_priv, ES_WAIT_TIMEOUT_MS);
		return ret;
	}
	case VSEIOC_RESERVE: {
		ret = reserveVse(pes_dw200_priv);
		return ret;
	}
	case VSEIOC_RELEASE: {
		releaseVse(pes_dw200_priv);
		return 0;
	}
	case VSEIOC_S_ONLINE_MODE: {
		u32 mode;
		viv_check_retval(copy_from_user(&mode, arg, sizeof(mode)));
		DEBUG_PRINT("%s: set vse online mode:%d\n", __func__, mode);
		if (mode > DWE_ONLINE_VSE_MODE || mode < VSE_OFFLINE_MODE) {
			pr_err("%s: set vse online mode:%d failed\n", __func__,
			       mode);
			return -EINVAL;
		}
		atomic_set(&vse_online_mode_atomic, mode);
		return 0;
	}
	case VSEIOC_G_ONLINE_MODE: {
		u32 mode = atomic_read(&vse_online_mode_atomic);
		DEBUG_PRINT("%s: get vse online mode:%d\n", __func__, mode);
		viv_check_retval(copy_to_user(arg, &mode, sizeof(mode)));
		return 0;
	}
	case DWEIOC_S_PARAMS:
		viv_check_retval(copy_from_user(&pdwe_dev->dwe_info, arg,
						sizeof(pdwe_dev->dwe_info)));
		pr_debug("set dwe's params\n");
		return 0;
	case DWEIOC_SET_LUT:
		viv_check_retval(copy_from_user(&pes_dw200_priv->lut_map_addr,
						arg, sizeof(u64)));
		pr_debug("set dwe lut addr:%lld\n",
			 pes_dw200_priv->lut_map_addr);
		return 0;
	case VSEIOC_S_PARAMS:
		viv_check_retval(copy_from_user(&pdwe_dev->vse_info, arg,
						sizeof(pdwe_dev->vse_info)));
		pr_debug("set vse's params\n");
		return 0;
	case DWIOC_ES_TRIGGER_VSE:
		// Read private data,and configure vse's regs,
		// and start hw
		pr_debug("trigger vse\n");
		atomic_inc(&pdriver_dev->trigger_atom);
		triggerVse(pes_dw200_priv);
		return 0;
	case DWIOC_ES_TRIGGER_DWE:
		// Read private data,and configure dwe's regs,
		// and start hw
		pr_debug("trigger dwe\n");
		atomic_inc(&pdriver_dev->trigger_atom);
		triggerDwe(pes_dw200_priv);
		return 0;
	case DWIOC_ES_TRIGGER_DWE_VSE:
		// Read private data,and configure regs,
		// and start hw
		pr_debug("trigger dwe online vse\n");
		atomic_inc(&pdriver_dev->trigger_atom);
		triggerDweVse(pes_dw200_priv);
		return 0;

	case DWEIOC_IMPORT_DMA_HEAP_BUF:
		viv_check_retval(copy_from_user(&addr, arg, sizeof(addr)));
		ret = dma_heap_import_from_user(pdwe_dev, addr);
		return ret;
	case DWEIOC_RELEASE_DMA_HEAP_BUF:
		viv_check_retval(copy_from_user(&addr, arg, sizeof(addr)));
		ret = dma_heap_iova_release(pdwe_dev, addr);
		return ret;

	case DWIOC_ES_SET_BUFFER:
		struct buffer_info buf;
		viv_check_retval(copy_from_user(&buf, arg, sizeof(buf)));
		memcpy(&pdwe_dev->buf_info[buf.buffer_type], &buf, sizeof(buf));
		DEBUG_PRINT(
			"set buf[%d]: offset(%d, %d, %d), stride(%d, %d, %d), addr:0x%llx\n",
			buf.buffer_type, buf.offset[0], buf.offset[1],
			buf.offset[2], buf.stride[0], buf.stride[1],
			buf.stride[2], buf.addr);
		return 0;

	default:
		pr_debug("hw command %d\n", cmd);
		return RET_HW_COMMOND;
	}
	return 0;
}
#else
irqreturn_t vivdw200_interrupt(int irq, void *dev_id)
{
	vivdw200_mis_t node;
	unsigned int dwe_mis, vse_mis;
	unsigned int dw200_fe_mis;
	struct es_dewarp_driver_dev *pdriver_dev = dev_id;
	struct dw200_subdev *pdw200 =
		(struct dw200_subdev *)pdriver_dev->private;

	int ret = 0;
	pr_info("%s enter\n", __func__);
	dwe_mis = 0;
	vse_mis = 0;
	dw200_fe_mis = 0;

	if (pdw200->fe.enable) {
		dw200_fe_mis = vse_read_reg(pdw200, DW200_REG_FE_MIS);
		if (dw200_fe_mis) {
			pr_info("%s fe mis 0x%08x\n", __func__, dw200_fe_mis);
			vse_write_reg(pdw200, DW200_REG_FE_ICR, dw200_fe_mis);
			if (dw200_fe_mis & 0x01) { // add mask to header file
				// pdw200->fe.refresh_part_regs.curr_cmd_num = 0;
				vse_write_reg(pdw200, DW200_REG_FE_CTL,
					      0x00000000); //!!!!!!!!!
				// TODO how to join into this status twice
				complete_all(&pdw200->fe.fe_completion);
				pdw200->fe.state = DW200_FE_STATE_READY;
				reinit_completion(&pdw200->fe.fe_completion);
			}
		}
	}

	dwe_read_irq((struct dw200_subdev *)pdw200, &dwe_mis);
	dwe_mis = dwe_mis & DWE_INT_STATUS;

	if (0 != dwe_mis) {
		// pr_info(" %s dwe mis 0x%08x\n", __func__, dwe_mis);
		dwe_clear_irq((struct dw200_subdev *)pdw200, dwe_mis << 24);

		node.val = dwe_mis;
		ret = vivdw200_write_circle_queue(&node,
						  &pdw200->dwe_circle_list);
		if (ret) {
			pr_err("vivdw200_write_circle_queue dwe error\n");
			return IRQ_NONE;
		}
		pdriver_dev->irq_trigger |= true;
	}

	vse_read_irq((struct dw200_subdev *)pdw200, &vse_mis);

	pr_info("dw200 irq: irq_src %d, dwe_mis 0x%x, vse_mis 0x%x\n", irq,
		dwe_mis, vse_mis);

	if (0 != vse_mis) {
		// pr_info(" %s vse mis 0x%08x\n", __func__, vse_mis);
		vse_clear_irq((struct dw200_subdev *)pdw200, vse_mis);
		node.val = vse_mis;
		ret = vivdw200_write_circle_queue(&node,
						  &pdw200->vse_circle_list);
		if (ret) {
			pr_err("vivdw200_write_circle_queue vse error\n");
			return IRQ_NONE;
		}
		pdriver_dev->irq_trigger |= true;
	}

	if (dwe_mis || vse_mis) {
		wake_up_interruptible(&pdriver_dev->irq_wait);
	} else {
		return IRQ_NONE;
	}

	pr_info("%s exit\n", __func__);
	return IRQ_HANDLED;
}
#endif // ES_DW200_SDK

static int dw200_reset_open(struct inode *inode, struct file *filp)
{
	filp->private_data = inode->i_private;

	return 0;
}

static ssize_t dw200_reset_write(struct file *filp, const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	uint32_t val = 0;
	struct device *dev;
	struct dw200_subdev *pdwe_dev = NULL;
	char str_buff[64] = { 0 };

	int ret = 0;
	struct regmap *regmap = NULL;
	int mmu_tbu0_vi_dw200_reg = 0;
	u32 rdwr_sid_ssid = 0;
	u32 sid = 0;
	struct iommu_fwspec *fwspec;

	pdwe_dev = filp->private_data;
	dev = pdwe_dev->dev;

	if (*ppos >= 64) {
		pr_err("%s: ppos out of range\n", __func__);
		return 0;
	}

	if (*ppos + count > 64) {
		count = 64 - *ppos;
	}

	if (copy_from_user(str_buff + *ppos, buffer, count) != 0) {
		pr_err("%s: copy_from_user error\n", __func__);
		return -EFAULT;
	}
	*ppos += count;

	val = (uint32_t)simple_strtoul(str_buff, NULL, 16);

	regmap = syscon_regmap_lookup_by_phandle(dev->of_node,
						 "eswin,vi_top_csr");
	if (IS_ERR(regmap)) {
		dev_dbg(dev, "No vi_top_csr phandle specified\n");
		return count;
	}

	/*reset vi dvp*/
	regmap_write(regmap, 0x470, 0x0);
	regmap_write(regmap, 0x470, 0x7);
	regmap_write(regmap, 0x474, 0x0);
	regmap_write(regmap, 0x474, 0x1);

	/* not behind smmu, use the default reset value(0x0) of the reg as streamID*/
	fwspec = dev_iommu_fwspec_get(dev);

	if (fwspec == NULL) {
		dev_dbg(dev,
			"dw200 is not behind smmu, skip configuration of sid\n");
		return count;
	}

	sid = fwspec->ids[0];

	ret = of_property_read_u32_index(dev->of_node, "eswin,vi_top_csr", 1,
					 &mmu_tbu0_vi_dw200_reg);
	if (ret) {
		dev_err(dev, "can't get dw200 sid cfg reg offset (%d)\n", ret);
		return ret;
	}

	/* make the reading sid the same as writing sid, ssid is fixed to zero */
	rdwr_sid_ssid = FIELD_PREP(AWSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(AWSMMUSSID, 0);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSSID, 0);
	regmap_write(regmap, mmu_tbu0_vi_dw200_reg, rdwr_sid_ssid);

	return count;
}

static int vvcam_dw200_smmu_sid_cfg(struct device *dev)
{
	int ret = 0;
	struct regmap *regmap = NULL;
	int mmu_tbu0_vi_dw200_reg = 0;
	u32 rdwr_sid_ssid = 0;
	u32 sid = 0;
	phandle phandle;
	struct device_node *vi_top_csr_np;

	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	/* not behind smmu, use the default reset value(0x0) of the reg as streamID*/
	if (fwspec == NULL) {
		dev_dbg(dev,
			"dw200 is not behind smmu, skip configuration of sid\n");
		return 0;
	}

	sid = fwspec->ids[0];

	if (of_property_read_u32_index(dev->of_node, "eswin,vi_top_csr", 0,
				       &phandle)) {
		dev_err(dev, "Failed to get csr node phandle\n");
		return -EINVAL;
	}

	vi_top_csr_np = of_find_node_by_phandle(phandle);
	if (!vi_top_csr_np) {
		dev_err(dev, "Failed to find vi_top_csr node\n");
		return -ENODEV;
	}

	regmap = device_node_to_regmap(vi_top_csr_np);
	if (IS_ERR(regmap)) {
		dev_dbg(dev, "No vi_top_csr phandle specified\n");
		of_node_put(vi_top_csr_np);
		return 0;
	}
	of_node_put(vi_top_csr_np);

	ret = of_property_read_u32_index(dev->of_node, "eswin,vi_top_csr", 1,
					 &mmu_tbu0_vi_dw200_reg);
	if (ret) {
		dev_err(dev, "can't get dw200 sid cfg reg offset (%d)\n", ret);
		return ret;
	}

	/* make the reading sid the same as writing sid, ssid is fixed to zero */
	rdwr_sid_ssid = FIELD_PREP(AWSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(AWSMMUSSID, 0);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSSID, 0);
	regmap_write(regmap, mmu_tbu0_vi_dw200_reg, rdwr_sid_ssid);

	ret = win2030_dynm_sid_enable(dev_to_node(dev));
	if (ret < 0)
		dev_err(dev, "failed to config dw200 streamID(%d)!\n", sid);
	else
		dev_dbg(dev, "success to config dw200 streamID(%d)!\n", sid);

	return ret;
}

static int dewarp_open(struct inode *inode, struct file *file)
{
	struct es_dewarp_driver_dev *pdriver_dev;
	struct dw200_subdev *pdw200;
	struct es_dw200_private *pes_dw200_priv;

	pes_dw200_priv = vmalloc(sizeof(struct es_dw200_private));
	if (pes_dw200_priv == NULL) {
		pr_err("%s:alloc struct vvcam_soc_driver_dev error\n",
		       __func__);
		return -ENOMEM;
	}
	memset(pes_dw200_priv, 0, sizeof(struct es_dw200_private));
	file->private_data = pes_dw200_priv;

	pes_dw200_priv->pdriver_dev =
		container_of(inode->i_cdev, struct es_dewarp_driver_dev, cdev);
	pdriver_dev = pes_dw200_priv->pdriver_dev;

	pdw200 = (struct dw200_subdev *)pdriver_dev->private;
	memcpy(&pes_dw200_priv->dw200, pdw200, sizeof(pes_dw200_priv->dw200));

	pm_runtime_get_sync(pdw200->dev);

	common_dmabuf_heap_import_init(&pes_dw200_priv->heap_root, pdw200->dev);
	pdw200->pheap_root = &pes_dw200_priv->heap_root;
	pes_dw200_priv->dw200.pheap_root = &pes_dw200_priv->heap_root;
	atomic_set(&pes_dw200_priv->dwe_reserved_atom, 0);
	atomic_set(&pes_dw200_priv->vse_reserved_atom, 0);
#ifndef ES_DW200_SDK
	vivdw200_create_circle_queue(&(pdw200->dwe_circle_list),
				     QUEUE_NODE_COUNT);
	vivdw200_create_circle_queue(&(pdw200->vse_circle_list),
				     QUEUE_NODE_COUNT);
#endif
	return 0;
};

static long dewarp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct es_dewarp_driver_dev *pdriver_dev;
	struct dw200_subdev *pdwe_dev;
	struct es_dw200_private *pes_dw200_priv;

	pes_dw200_priv = file->private_data;
	pdriver_dev = pes_dw200_priv->pdriver_dev;
	// pdriver_dev = file->private_data;
	if (pdriver_dev == NULL) {
		pr_err("%s:file private is null point error\n", __func__);
		return -ENOMEM;
	}

	pdwe_dev = pdriver_dev->private;
	pdwe_dev->pheap_root = &pes_dw200_priv->heap_root;
#ifdef ES_DW200_SDK
	ret = es_dw200_ioctl(pes_dw200_priv, cmd, (void *)arg);
	if (ret != RET_HW_COMMOND) {
		return ret;
	}
#endif // ES_DW200_SDK
	ret = dw200_priv_ioctl(pdwe_dev, cmd, (void *)arg);
	return ret;
};

static int vvcam_sys_reset_init(struct platform_device *pdev,
				dw_clk_rst_t *dw_crg)
{
	dw_crg->rstc_axi = devm_reset_control_get_shared(&pdev->dev, "axi");
	if (IS_ERR_OR_NULL(dw_crg->rstc_axi)) {
		dev_err(&pdev->dev, "Failed to get vi axi reset handle\n");
		return -EFAULT;
	}

	dw_crg->rstc_cfg = devm_reset_control_get_shared(&pdev->dev, "cfg");
	if (IS_ERR_OR_NULL(dw_crg->rstc_cfg)) {
		dev_err(&pdev->dev, "Failed to get vi cfg reset handle\n");
		return -EFAULT;
	}

	dw_crg->rstc_dwe = devm_reset_control_get_optional(&pdev->dev, "dwe");
	if (IS_ERR_OR_NULL(dw_crg->rstc_dwe)) {
		dev_err(&pdev->dev, "Failed to get dwe reset handle\n");
		return -EFAULT;
	}

	return 0;
}

#define VVCAM_CLK_GET_HANDLE(dev, clk_handle, clk_name)                     \
	{                                                                   \
		clk_handle = devm_clk_get(dev, clk_name);                   \
		if (IS_ERR(clk_handle)) {                                   \
			ret = PTR_ERR(clk_handle);                          \
			dev_err(dev, "failed to get dw %s: %d\n", clk_name, \
				ret);                                       \
			return ret;                                         \
		}                                                           \
	}

static int vvcam_sys_clk_init(struct platform_device *pdev,
			      dw_clk_rst_t *dw_crg)
{
	int ret;
	struct device *dev = &pdev->dev;

	VVCAM_CLK_GET_HANDLE(dev, dw_crg->aclk, "aclk");
	VVCAM_CLK_GET_HANDLE(dev, dw_crg->cfg_clk, "cfg_clk");
	VVCAM_CLK_GET_HANDLE(dev, dw_crg->dw_aclk, "dw_aclk");
	VVCAM_CLK_GET_HANDLE(dev, dw_crg->aclk_mux, "aclk_mux");
	VVCAM_CLK_GET_HANDLE(dev, dw_crg->dw_mux, "dw_mux");
	VVCAM_CLK_GET_HANDLE(dev, dw_crg->spll0_fout1, "spll0_fout1");
	VVCAM_CLK_GET_HANDLE(dev, dw_crg->vpll_fout1, "vpll_fout1");

	return 0;
}

static int vvcam_sys_reset_release(dw_clk_rst_t *dw_crg)
{
	int ret;

	ret = reset_control_deassert(dw_crg->rstc_cfg);
	WARN_ON(0 != ret);

	ret = reset_control_deassert(dw_crg->rstc_axi);
	WARN_ON(0 != ret);

	ret = reset_control_reset(dw_crg->rstc_dwe);
	WARN_ON(0 != ret);

	return 0;
}

#define VVCAM_SYS_CLK_PREPARE(clk)                                 \
	do {                                                       \
		if (clk_prepare_enable(clk)) {                     \
			pr_err("Failed to enable clk %px\n", clk); \
		}                                                  \
	} while (0)

static int vvcam_sys_clk_config(dw_clk_rst_t *dw_crg)
{
	int ret = 0;
	long rate;

	ret = clk_set_parent(dw_crg->aclk_mux, dw_crg->spll0_fout1);
	if (ret < 0) {
		pr_err("DW: failed to set aclk_mux parent: %d\n", ret);
		return ret;
	}

	ret = clk_set_parent(dw_crg->dw_mux, dw_crg->vpll_fout1);
	if (ret < 0) {
		pr_err("DW: failed to set dw_mux parent: %d\n", ret);
		return ret;
	}

	rate = clk_round_rate(dw_crg->aclk, VVCAM_AXI_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(dw_crg->aclk, rate);
		if (ret) {
			pr_err("DW: failed to set aclk: %d\n", ret);
			return ret;
		}
		pr_info("DW set aclk to %ldHZ\n", rate);
	}

	rate = clk_round_rate(dw_crg->dw_aclk, VVCAM_DW_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(dw_crg->dw_aclk, rate);
		if (ret) {
			pr_err("DW: failed to set vi_dig_dw: %d\n", ret);
			return ret;
		}
		pr_info("DW set vi_dig_dw to %ldHZ\n", rate);
	}
	return 0;
}

static int vvcam_sys_clk_prepare(dw_clk_rst_t *dw_crg)
{
	int ret = 0;
	VVCAM_SYS_CLK_PREPARE(dw_crg->aclk);
	VVCAM_SYS_CLK_PREPARE(dw_crg->cfg_clk);
	VVCAM_SYS_CLK_PREPARE(dw_crg->dw_aclk);
	ret = win2030_tbu_power(dw_crg->dev, true);
	if (ret) {
		pr_err("%s: DW tbu power up failed\n", __func__);
		return ret;
	}
	return 0;
}

static int vvcam_sys_clk_unprepare(dw_clk_rst_t *dw_crg)
{
	int ret = 0;
	//  tbu power down need enanle clk
	ret = win2030_tbu_power(dw_crg->dev, false);
	if (ret) {
		pr_err("dw tbu power down failed\n");
		return ret;
	}

	clk_disable_unprepare(dw_crg->dw_aclk);
	clk_disable_unprepare(dw_crg->cfg_clk);
	clk_disable_unprepare(dw_crg->aclk);

	return 0;
}

static int vvcam_reset_fini(dw_clk_rst_t *dw_crg)
{
	reset_control_assert(dw_crg->rstc_dwe);
	reset_control_assert(dw_crg->rstc_cfg);
	reset_control_assert(dw_crg->rstc_axi);
	return 0;
}

static int dewarp_release(struct inode *inode, struct file *file)
{
	u32 reg = 0;
	struct es_dewarp_driver_dev *pdriver_dev;
	struct dw200_subdev *pdw200;
	struct es_dw200_private *pes_dw200_priv;

	pes_dw200_priv = file->private_data;
	pdriver_dev = pes_dw200_priv->pdriver_dev;
	pdw200 = (struct dw200_subdev *)pdriver_dev->private;

	DEBUG_PRINT("enter %s\n", __func__);

	common_dmabuf_heap_import_uninit(&pes_dw200_priv->heap_root);
	pdw200->pheap_root = NULL;
	pes_dw200_priv->dw200.pheap_root = NULL;
#ifdef ES_DW200_SDK
	if (atomic_read(&pes_dw200_priv->vse_reserved_atom) > 0) {
		pr_err("%s, vse_reserved_cnt:%d\n", __func__,
		       atomic_read(&pes_dw200_priv->vse_reserved_atom));
		vse_write_reg(pdw200, VSE_REG_MI_ICR, 0xffffffff);
		vse_write_reg(pdw200, VSE_REG_MI_ICR1, 0xffffffff);
		/*stop the axi bus*/
		vse_write_reg(pdw200, VSE_REG_MI0_BUS_ID, 0);
		vse_write_reg(pdw200, VSE_REG_MI1_BUS_ID, 0);
		atomic_set(&vse_irq_trigger_mis, 0);
		while (atomic_read(&pes_dw200_priv->vse_reserved_atom) > 0) {
			releaseVse(pes_dw200_priv);
		}
	}

	if (atomic_read(&pes_dw200_priv->dwe_reserved_atom) > 0) {
		pr_err("%s, dwe_reserved_cnt:%d\n", __func__,
		       atomic_read(&pes_dw200_priv->dwe_reserved_atom));
		dwe_disable_irq(pdw200);
		dwe_enable_bus(pdw200, false);
		/*clean all irq*/
		dwe_clear_irq(pdw200, INT_RESET_MASK);
		reg = dwe_read_reg(pdw200, BUS_CTRL);
		dwe_write_reg(pdw200, BUS_CTRL,
			      reg & ~DEWRAP_BUS_CTRL_ENABLE_MASK);
		atomic_set(&dwe_irq_trigger_mis, 0);
		while (atomic_read(&pes_dw200_priv->dwe_reserved_atom) > 0) {
			releaseDwe(pes_dw200_priv);
		}
	}
#else
	/*clean all irq*/
	dwe_clear_irq(pdw200, INT_RESET_MASK);
	vse_write_reg(pdw200, VSE_REG_MI_ICR, 0xffffffff);
	vse_write_reg(pdw200, VSE_REG_MI_ICR1, 0xffffffff);
	/*vse_write_reg(pdw200, VSE_REG_MI_IMSC, 0);*/
	/*vse_write_reg(pdw200, VSE_REG_MI_IMSC1, 0);*/

	/*stop the axi bus*/
	vse_write_reg(pdw200, VSE_REG_MI0_BUS_ID, 0);
	vse_write_reg(pdw200, VSE_REG_MI1_BUS_ID, 0);

	reg = dwe_read_reg(pdw200, BUS_CTRL);
	dwe_write_reg(pdw200, BUS_CTRL, reg & ~DEWRAP_BUS_CTRL_ENABLE_MASK);

	/*destory circle queue*/
	vivdw200_destroy_circle_queue(&(pdw200->dwe_circle_list));
	vivdw200_destroy_circle_queue(&(pdw200->vse_circle_list));
#endif
	pm_runtime_put(pdw200->dev);
	pes_dw200_priv->pdriver_dev = NULL;
	vfree(pes_dw200_priv);

	DEBUG_PRINT("exit %s\n", __func__);
	return 0;
};

struct file_operations dw200_reset_fops = {
	.owner = THIS_MODULE,
	.open = dw200_reset_open,
	.write = dw200_reset_write,
};

struct file_operations es_dewarp_fops = {
	.owner = THIS_MODULE,
	.open = dewarp_open,
	.release = dewarp_release,
	.unlocked_ioctl = dewarp_ioctl,
	.poll = dewarp_poll,
};

static int es_dewarp_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct es_dewarp_driver_dev *pdriver_dev;
	struct dw200_subdev *pdwe_dev;
	char debug_dw200_reset[64] = "dw200_reset";
	void __iomem *vi_top_addr = ioremap(VI_TOP_CLK, sizeof(u32));
	u32 reg = 0;

	// DEBUG_PRINT("enter %s\n", __func__);

	if (pdev->id >= VIVCAM_DWE_MAXCNT) {
		pr_err("%s:pdev id is %d error\n", __func__, pdev->id);
		return -EINVAL;
	}

	pdriver_dev = devm_kzalloc(
		&pdev->dev, sizeof(struct es_dewarp_driver_dev), GFP_KERNEL);
	if (pdriver_dev == NULL) {
		pr_err("%s:alloc struct vvcam_soc_driver_dev error\n",
		       __func__);
		return -ENOMEM;
	}

	pdwe_dev = devm_kzalloc(&pdev->dev, sizeof(struct dw200_subdev),
				GFP_KERNEL);
	if (pdwe_dev == NULL) {
		pr_err("%s:alloc struct vvcam_soc_dev error\n", __func__);
		return -ENOMEM;
	}

	ret = vvcam_sys_reset_init(pdev, &pdwe_dev->dw_crg);
	if (ret) {
		pr_err("%s: DW reset init failed\n", __func__);
		return ret;
	}

	ret = vvcam_sys_clk_init(pdev, &pdwe_dev->dw_crg);
	if (ret) {
		pr_err("%s: DW clk init failed\n", __func__);
		return ret;
	}

	ret = vvcam_sys_clk_config(&pdwe_dev->dw_crg);
	if (ret) {
		pr_err("%s: DW clk prepare failed\n", __func__);
		return ret;
	}

	pdwe_dev->dw_crg.dev = &pdev->dev;
	ret = vvcam_sys_clk_prepare(&pdwe_dev->dw_crg);
	if (ret) {
		pr_err("%s: DW clk prepare failed\n", __func__);
		return ret;
	}

	ret = vvcam_sys_reset_release(&pdwe_dev->dw_crg);
	if (ret) {
		pr_err("%s: DW reset release failed\n", __func__);
		return ret;
	}

	// VI dw200 top clk enable:
	reg = ioread32(vi_top_addr);
	if (!IS_BIT_SET(reg, 2)) {
		SET_BIT(reg, 2);
		iowrite32(reg, vi_top_addr);
	}
	iounmap(vi_top_addr);

	/* DWE ioremap */
	pdwe_dev->dwe_base =
		devm_platform_ioremap_resource(pdev, DWE_REG_INDEX);
	if (IS_ERR(pdwe_dev->dwe_base))
		return PTR_ERR(pdwe_dev->dwe_base);

	/* VSE ioremap */
	pdwe_dev->vse_base =
		devm_platform_ioremap_resource(pdev, VSE_REG_INDEX);
	if (IS_ERR(pdwe_dev->vse_base)) {
		return PTR_ERR(pdwe_dev->vse_base);
	}

#ifdef DWE_REG_RESET
	pdwe_dev->dwe_reset = ioremap(DWE_REG_RESET, 4);
#endif
#ifdef DWE_REG_RESET
	pdwe_dev->vse_reset = ioremap(VSE_REG_RESET, 4);
#endif

	pdriver_dev->private = pdwe_dev;
	// pdwe_dev->id = pdev->id;
	pdwe_dev->dev = &pdev->dev;
	mutex_init(&pdriver_dev->vvmutex);
	platform_set_drvdata(pdev, pdriver_dev);

#ifdef ES_DW200_SDK
	pdriver_dev->irq_num = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, pdriver_dev->irq_num,
			       (irq_handler_t)dwe_isr,
			       IRQF_SHARED | IRQF_TRIGGER_RISING,
			       "vivdw200-dwe", (void *)pdriver_dev);
	if (ret != 0) {
		pr_err("%s:request irq error\n", __func__);
		return ret;
	}

	pdriver_dev->irq_num_vse = platform_get_irq(pdev, 1);
	ret = devm_request_irq(&pdev->dev, pdriver_dev->irq_num_vse,
			       (irq_handler_t)vse_isr,
			       IRQF_SHARED | IRQF_TRIGGER_RISING,
			       "vivdw200-vse", (void *)pdriver_dev);
	if (ret != 0) {
		pr_err("%s:request irq error\n", __func__);
		return ret;
	}
	sema_init(&dwe_sem, 1);
	sema_init(&vse_sem, 1);
#else

	pdriver_dev->irq_num = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, pdriver_dev->irq_num,
			       (irq_handler_t)vivdw200_interrupt,
			       IRQF_SHARED | IRQF_TRIGGER_RISING,
			       VIVCAM_DWE_NAME, (void *)pdriver_dev);
	if (ret != 0) {
		pr_err("%s:request irq error\n", __func__);
		return ret;
	}

	pdriver_dev->irq_num_vse = platform_get_irq(pdev, 1);
	ret = devm_request_irq(&pdev->dev, pdriver_dev->irq_num_vse,
			       (irq_handler_t)vivdw200_interrupt,
			       IRQF_SHARED | IRQF_TRIGGER_RISING,
			       VIVCAM_DWE_NAME, (void *)pdriver_dev);
	if (ret != 0) {
		pr_err("%s:request irq error\n", __func__);
		return ret;
	}
#endif

	init_waitqueue_head(&pdriver_dev->irq_wait);
	if (devise_register_index == 0) {
		if (es_dewarp_major == 0) {
			ret = alloc_chrdev_region(&pdriver_dev->devt, 0,
						  VIVCAM_DWE_MAXCNT,
						  VIVCAM_DWE_NAME);
			if (ret != 0) {
				pr_err("%s:alloc_chrdev_region error\n",
				       __func__);
				return ret;
			}
			es_dewarp_major = MAJOR(pdriver_dev->devt);
			es_dewarp_minor = MINOR(pdriver_dev->devt);
		} else {
			pdriver_dev->devt =
				MKDEV(es_dewarp_major, es_dewarp_minor);
			ret = register_chrdev_region(pdriver_dev->devt,
						     VIVCAM_DWE_MAXCNT,
						     VIVCAM_DWE_NAME);
			if (ret) {
				pr_err("%s:register_chrdev_region error\n",
				       __func__);
				return ret;
			}
		}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		es_dewarp_class = class_create(VIVCAM_DWE_NAME);
#else
		es_dewarp_class = class_create(THIS_MODULE, VIVCAM_DWE_NAME);
#endif
		if (IS_ERR(es_dewarp_class)) {
			pr_err("%s[%d]:class_create error!\n", __func__,
			       __LINE__);
			return -EINVAL;
		}
	}
	pdriver_dev->devt = MKDEV(es_dewarp_major, es_dewarp_minor);

	cdev_init(&pdriver_dev->cdev, &es_dewarp_fops);
	ret = cdev_add(&pdriver_dev->cdev, pdriver_dev->devt, 1);
	if (ret) {
		pr_err("%s[%d]:cdev_add error!\n", __func__, __LINE__);
		return ret;
	}
	pdriver_dev->class = es_dewarp_class;
	device_create(pdriver_dev->class, NULL, pdriver_dev->devt, pdriver_dev,
		      "%s", VIVCAM_DWE_NAME);

	pdwe_dev->fe.enable = fe_enable; // TODO: Get from input parameter
	if (pdwe_dev->fe.enable == true) {
		dw200_fe_init(pdwe_dev);
	}

	(void)vvcam_dw200_smmu_sid_cfg(&pdev->dev);

	pdwe_dev->dw200_reset = debugfs_create_file(
		debug_dw200_reset, 0644, NULL, pdwe_dev, &dw200_reset_fops);

	/* The code below assumes runtime PM to be disabled. */
	WARN_ON(pm_runtime_enabled(&pdev->dev));
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	atomic_set(&pdriver_dev->trigger_atom, 0);
	init_waitqueue_head(&pdriver_dev->trigger_wq);

	devise_register_index++;
	return ret;
}

static int es_dewarp_remove(struct platform_device *pdev)
{
	struct es_dewarp_driver_dev *pdriver_dev;
	struct dw200_subdev *pdwe_dev;

	devise_register_index--;
	pdriver_dev = platform_get_drvdata(pdev);

	pdwe_dev = (struct dw200_subdev *)pdriver_dev->private;
	if (pdwe_dev->fe.enable == true) {
		dw200_fe_destory(pdwe_dev);
	}
	debugfs_remove(pdwe_dev->dw200_reset);
	cdev_del(&pdriver_dev->cdev);
	device_destroy(pdriver_dev->class, pdriver_dev->devt);
	unregister_chrdev_region(pdriver_dev->devt, VIVCAM_DWE_MAXCNT);
	if (devise_register_index == 0) {
		class_destroy(pdriver_dev->class);
	}

	vvcam_reset_fini(&pdwe_dev->dw_crg);

	pm_runtime_dont_use_autosuspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static int dewarp_runtime_suspend(struct device *dev)
{
	struct es_dewarp_driver_dev *pdriver_dev = dev_get_drvdata(dev);
	struct dw200_subdev *pdwe_dev;

	pdwe_dev = (struct dw200_subdev *)pdriver_dev->private;
	return vvcam_sys_clk_unprepare(&pdwe_dev->dw_crg);
}

static int dewarp_runtime_resume(struct device *dev)
{
	struct es_dewarp_driver_dev *pdriver_dev = dev_get_drvdata(dev);
	struct dw200_subdev *pdwe_dev;

	pdwe_dev = (struct dw200_subdev *)pdriver_dev->private;
	return vvcam_sys_clk_prepare(&pdwe_dev->dw_crg);
}

static int dewarp_suspend(struct device *dev)
{
	struct es_dewarp_driver_dev *pdriver_dev = dev_get_drvdata(dev);
	struct dw200_subdev *pdwe_dev;
	int ret = 0;
	pdriver_dev->suspended = 0;

	pdwe_dev = (struct dw200_subdev *)pdriver_dev->private;

	if (pm_runtime_status_suspended(dev)) {
		return 0;
	}

	if (atomic_read(&pdriver_dev->trigger_atom)) {
		//dw200 is working wait done
		ret = wait_event_interruptible_timeout(
			pdriver_dev->trigger_wq,
			atomic_read(&pdriver_dev->trigger_atom) == 0,
			ES_WAIT_TIMEOUT_MS);

		if (ret == 0) {
			pr_err("Error: %s process timeout %lu, ret:%d\n",
			       __func__, ES_WAIT_TIMEOUT_MS, ret);
			atomic_dec(&pdriver_dev->trigger_atom);
			ret = -ETIMEDOUT;
			return ret;
		} else if (ret < 0) {
			pr_err("interrupted !!!\n");
			atomic_dec(&pdriver_dev->trigger_atom);
			ret = -ERESTARTSYS;
			return ret;
		}
	}
	pdriver_dev->suspended = 1;
	return vvcam_sys_clk_unprepare(&pdwe_dev->dw_crg);
}

static int dewarp_resume(struct device *dev)
{
	struct es_dewarp_driver_dev *pdriver_dev = dev_get_drvdata(dev);
	struct dw200_subdev *pdwe_dev;

	pdwe_dev = (struct dw200_subdev *)pdriver_dev->private;

	if (pm_runtime_status_suspended(dev)) {
		return 0;
	}

	if (pdriver_dev->suspended) {
		return vvcam_sys_clk_prepare(&pdwe_dev->dw_crg);
	}

	return 0;
}

static const struct dev_pm_ops dewarp_pm_ops = {
	SET_RUNTIME_PM_OPS(dewarp_runtime_suspend, dewarp_runtime_resume, NULL)
		SET_SYSTEM_SLEEP_PM_OPS(dewarp_suspend, dewarp_resume)
};

#define DEV_NAME "dewarp-dri"
static const struct of_device_id dw200_of_id_table[] = {
	{
		.compatible = "eswin,dewarp",
	},
	{}
};
MODULE_DEVICE_TABLE(of, dw200_of_id_table);

static struct platform_driver viv_platform_driver = {
    .probe = es_dewarp_probe,
    .remove = es_dewarp_remove,
    .driver =
        {
            .owner = THIS_MODULE,
            .name = DEV_NAME,
            .of_match_table = dw200_of_id_table,
            .pm = &dewarp_pm_ops,
        },
};
module_platform_driver(viv_platform_driver);

MODULE_DESCRIPTION("DWE");
MODULE_LICENSE("GPL");
