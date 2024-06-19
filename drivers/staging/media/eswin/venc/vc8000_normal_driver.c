// SPDX-License-Identifier: GPL-2.0
/****************************************************************************
 *
 *    The MIT License (MIT)
 *
 *    Copyright (C) 2014  VeriSilicon Microelectronics Co., Ltd.
 *
 *    Permission is hereby granted, free of charge, to any person obtaining a
 *    copy of this software and associated documentation files (the "Software"),
 *    to deal in the Software without restriction, including without limitation
 *    the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *    and/or sell copies of the Software, and to permit persons to whom the
 *    Software is furnished to do so, subject to the following conditions:
 *
 *    The above copyright notice and this permission notice shall be included in
 *    all copies or substantial portions of the Software.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *    DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 *    The GPL License (GPL)
 *
 *    Copyright (C) 2014  VeriSilicon Microelectronics Co., Ltd.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version 2
 *    of the License, or (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software Foundation,
 *    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *****************************************************************************
 *
 *    Note: This software is released under dual MIT and GPL licenses. A
 *    recipient may use this file under the terms of either the MIT license or
 *    GPL License. If you wish to use only one license not the other, you can
 *    indicate your decision by deleting one of the above license notices in your
 *    version of this file.
 *
 *****************************************************************************
 */

#include <linux/kernel.h>
#include <linux/module.h>
/* needed for __init,__exit directives */
#include <linux/init.h>
/* needed for remap_page_range
 * SetPageReserved
 * ClearPageReserved
 */
#include <linux/mm.h>
/* obviously, for kmalloc */
#include <linux/slab.h>
/* for struct file_operations, register_chrdev() */
#include <linux/fs.h>
/* standard error codes */
#include <linux/errno.h>

#include <linux/moduleparam.h>
/* request_irq(), free_irq() */
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <linux/semaphore.h>
#include <linux/spinlock.h>
/* needed for virt_to_phys() */
#include <asm/io.h>
#include <linux/pci.h>
#include <asm/uaccess.h>
#include <linux/ioport.h>

#include <asm/irq.h>

#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/delay.h>

/* device tree access */
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/dmabuf-heap-import-helper.h>

/* our own stuff */
#include "vc8000_driver.h"
#include "vc8000_normal_cfg.h"

#define LOG_TAG  VENC_DEV_NAME ":norm"
#include "vc_drv_log.h"

/********variables declaration related with race condition**********/

static struct semaphore enc_core_sem;
static DECLARE_WAIT_QUEUE_HEAD(hw_queue);
static DEFINE_SPINLOCK(owner_lock);
static DECLARE_WAIT_QUEUE_HEAD(enc_wait_queue);

/*------------------------------------------------------------------------
 *****************************PORTING LAYER********************************
 *--------------------------------------------------------------------------
 */
/*------------------------------END-------------------------------------*/

/***************************TYPE AND FUNCTION DECLARATION****************/

/* here's all the must remember stuff */
typedef struct {
	SUBSYS_DATA subsys_data; //config of each core,such as base addr, iosize,etc
	u32 hw_id; //VC8000E/VC8000EJ hw id to indicate project
	u32 subsys_id; //subsys id for driver and sw internal use
	u32 type_main_core;//VC8000E/VC8000EJ is the main core
	u32 is_valid; //indicate this subsys is hantro's core or not
	int pid[CORE_MAX]; //indicate which process is occupying the subsys
	u32 is_reserved[CORE_MAX]; //indicate this subsys is occupied by user or not
	u32 irq_received[CORE_MAX]; //indicate which core receives irq
	u32 irq_status[CORE_MAX]; //IRQ status of each core
	u32 job_id[CORE_MAX];
	char *buffer;
	unsigned int buffsize;

	volatile u8 *hwregs;
	struct fasync_struct *async_queue;
} hantroenc_t;

static int ReserveIO(void);
static void ReleaseIO(void);
//static void ResetAsic(hantroenc_t * dev);

#ifdef hantroenc_DEBUG
static void dump_regs(unsigned long data);
#endif

/* IRQ handler */
static irqreturn_t hantroenc_isr(int irq, void *dev_id);

/*********************local variable declaration*****************/
#ifndef VENC_CLASS_NAME
# define VENC_CLASS_NAME "es_venc_class"
#endif
static struct class *venc_class = NULL;
static unsigned long sram_base;
static unsigned int sram_size;
/* and this is our MAJOR; use 0 for dynamic allocation (recommended)*/
static int hantroenc_major;
static int total_subsys_num;
static int total_core_num;
static volatile unsigned int asic_status;
/* dynamic allocation*/
static hantroenc_t *hantroenc_data;

/******************************************************************************/
static int CheckEncIrq(hantroenc_t *dev, u32 *core_info, u32 *irq_status,
		       u32 *job_id)
{
	unsigned long flags;
	int rdy = 0;
	u8 core_type = 0;
	u8 subsys_idx = 0;

	core_type = (u8)(*core_info & 0x0F);
	subsys_idx = (u8)(*core_info >> 4);

	if (subsys_idx > total_subsys_num - 1) {
		*core_info = -1;
		*irq_status = 0;
		return 1;
	}

	spin_lock_irqsave(&owner_lock, flags);

	if (dev[subsys_idx].irq_received[core_type]) {
		/* reset the wait condition(s) */
		LOG_DBG("check subsys[%d][%d] irq ready\n", subsys_idx, core_type);
		//dev[subsys_idx].irq_received[core_type] = 0;
		rdy = 1;
		*core_info = subsys_idx;
		*irq_status = dev[subsys_idx].irq_status[core_type];
		if (job_id)
			*job_id = dev[subsys_idx].job_id[core_type];
	}

	spin_unlock_irqrestore(&owner_lock, flags);

	return rdy;
}

static unsigned int WaitEncReady(hantroenc_t *dev, u32 *core_info,
				 u32 *irq_status)
{
	LOG_DBG("%s\n", __func__);

	if (wait_event_interruptible(enc_wait_queue,
				     CheckEncIrq(dev, core_info, irq_status, NULL))) {
		LOG_DBG("ENC wait_event_interruptible interrupted\n");
		return -ERESTARTSYS;
	}

	return 0;
}

static int CheckEncIrqbyPolling(hantroenc_t *dev, u32 *core_info,
				u32 *irq_status, u32 *job_id)
{
	unsigned long flags;
	int rdy = 0;
	u8 core_type = 0;
	u8 subsys_idx = 0;
	u32 irq, hwId, majorId, wClr;
	unsigned long reg_offset = 0;
	u32 loop = 300;
	u32 interval = 10;
	u32 enable_status = 0;

	core_type = (u8)(*core_info & 0x0F);
	subsys_idx = (u8)(*core_info >> 4);

	if (subsys_idx > total_subsys_num - 1) {
		*core_info = -1;
		*irq_status = 0;
		return 1;
	}

	do {
		spin_lock_irqsave(&owner_lock, flags);
		if (loop % 30 == 0) {
			LOG_INFO("subsys=%d, core_type=%d, reserved=%s, irq_status=%x\n",
				subsys_idx, core_type,
				dev[subsys_idx].is_reserved[core_type] ? "yes" : "no",
				dev[subsys_idx].irq_status[core_type]);
		}
		if (dev[subsys_idx].is_reserved[core_type] == 0) {
			LOG_DBG("subsys[%d][%d]  is not reserved\n",
				subsys_idx, core_type);
			goto end_1;
		} else if (dev[subsys_idx].irq_received[core_type] &&
			   (dev[subsys_idx].irq_status[core_type] &
			    (ASIC_STATUS_FUSE_ERROR | ASIC_STATUS_HW_TIMEOUT |
			     ASIC_STATUS_BUFF_FULL | ASIC_STATUS_HW_RESET |
			     ASIC_STATUS_ERROR | ASIC_STATUS_FRAME_READY))) {
			rdy = 1;
			*core_info = subsys_idx;
			*irq_status = dev[subsys_idx].irq_status[core_type];
			*job_id = dev[subsys_idx].job_id[core_type];
			goto end_1;
		}

		reg_offset =
			dev[subsys_idx].subsys_data.core_info.offset[core_type];
		irq = (u32)ioread32(
			(void __iomem *)(dev[subsys_idx].hwregs + reg_offset + 0x04));
		if (loop % 30 == 0) {
			LOG_INFO("interrupt status=0x%08x\n", irq);
		}

		enable_status = (u32)ioread32(
			(void __iomem *)(dev[subsys_idx].hwregs + reg_offset + 20));

		if (irq & ASIC_STATUS_ALL) {
			LOG_DBG("check subsys[%d][%d] irq ready\n", subsys_idx,
			       core_type);
			if (irq & 0x20)
				iowrite32(0, (void __iomem *)(dev[subsys_idx].hwregs +
						      reg_offset + 0x14));

			/* clear all IRQ bits. (hwId >= 0x80006100) means IRQ is cleared
			 * by writing 1
			 */
			hwId = ioread32((void __iomem *)dev[subsys_idx].hwregs +
					reg_offset);
			majorId = (hwId & 0x0000FF00) >> 8;
			wClr = (majorId >= 0x61) ? irq : (irq & (~0x1FD));
			iowrite32(wClr, (void __iomem *)(dev[subsys_idx].hwregs +
						 reg_offset + 0x04));

			rdy = 1;
			*core_info = subsys_idx;
			*irq_status = irq;
			dev[subsys_idx].irq_received[core_type] = 1;
			dev[subsys_idx].irq_status[core_type] = irq;
			*job_id = dev[subsys_idx].job_id[core_type];
			goto end_1;
		}

		spin_unlock_irqrestore(&owner_lock, flags);
		mdelay(interval);
	} while (loop--);
	goto end_2;

end_1:
	spin_unlock_irqrestore(&owner_lock, flags);
end_2:
	return rdy;
}

static int CheckEncAnyIrqByPolling(hantroenc_t *dev, CORE_WAIT_OUT *out)
{
	u32 i;
	int rdy = 0;
	u32 core_info, irq_status, job_id;
	u32 core_type = CORE_VC8000E;

	for (i = 0; i < total_subsys_num; i++) {
		if (!(dev[i].subsys_data.core_info.type_info & (1 << core_type)))
			continue;

		core_info = ((i << 4) | core_type);
		if ((CheckEncIrqbyPolling(dev, &core_info, &irq_status, &job_id) == 1) &&
		    (core_info == i)) {
			out->job_id[out->irq_num] = job_id;
			out->irq_status[out->irq_num] = irq_status;
			//LOG_DBG("irq_status of subsys %d job_id %d is:%x\n",i,job_id,irq_status);
			out->irq_num++;
			rdy = 1;
		}
	}

	return rdy;
}

static unsigned int WaitEncAnyReadyByPolling(hantroenc_t *dev, CORE_WAIT_OUT *out)
{
	if (wait_event_interruptible(enc_wait_queue, CheckEncAnyIrqByPolling(dev, out))) {
		LOG_DBG("ENC wait_event_interruptible interrupted\n");
		return -ERESTARTSYS;
	}

	return 0;
}

static int CheckEncAnyIrq(hantroenc_t *dev, CORE_WAIT_OUT *out)
{
	u32 i;
	int rdy = 0;
	u32 core_info, irq_status, job_id;
	u32 core_type = CORE_VC8000E;

	for (i = 0; i < total_subsys_num; i++) {
		if (!(dev[i].subsys_data.core_info.type_info &
		      (1 << core_type)))
			continue;

		core_info = ((i << 4) | core_type);
		if ((CheckEncIrq(dev, &core_info, &irq_status,
					  &job_id) == 1) && core_info == i) {
			out->job_id[out->irq_num] = job_id;
			out->irq_status[out->irq_num] = irq_status;
			/*LOG_DBG("irq_status of subsys %d job_id %d is:%x\n",
			 *i,job_id,irq_status);
			 */
			out->irq_num++;
			rdy = 1;
		}
	}

	return rdy;
}

static unsigned int WaitEncAnyReady(hantroenc_t *dev, CORE_WAIT_OUT *out)
{
	if (wait_event_interruptible(enc_wait_queue,
				     CheckEncAnyIrq(dev, out))) {
		LOG_DBG("ENC wait_event_interruptible interrupted\n");
		return -ERESTARTSYS;
	}

	return 0;
}

static int CheckCoreOccupation(hantroenc_t *dev, u8 core_type)
{
	int ret = 0;
	unsigned long flags;

	// modified by Xiaogang.Li
	// core_type = (core_type == CORE_VC8000EJ ? CORE_VC8000E : core_type);

	spin_lock_irqsave(&owner_lock, flags);
	if (!dev->is_reserved[core_type]) {
		dev->is_reserved[core_type] = 1;
#ifndef MULTI_THR_TEST
		dev->pid[core_type] = current->pid;
#endif
		ret = 1;
		LOG_DBG("%s pid=%d\n", __func__, dev->pid[core_type]);
	}

	spin_unlock_irqrestore(&owner_lock, flags);

	return ret;
}

static int GetWorkableCore(hantroenc_t *dev, u32 *core_info, u32 *core_info_tmp)
{
	int ret = 0;
	u32 i = 0;
	u32 cores;
	u8 core_type = 0;
	u32 required_num = 0;
	static u32 reserved_job_id;
	unsigned long flags;
	/*input core_info[32 bit]: mode[1bit](1:all 0:specified)+amount[3bit]
	 *(the needing amount -1)+reserved+core_type[8bit]

     *output core_info[32 bit]: the reserved core info to user space and
	 *defined as below.
     *mode[1bit](1:all 0:specified)+amount[3bit](reserved total core num)+
	 *reserved+subsys_mapping[8bit]
	 */
	cores = *core_info;
	required_num = ((cores >> CORE_INFO_AMOUNT_OFFSET) & 0x7) + 1;
	core_type = (u8)(cores & 0xFF);

	if (*core_info_tmp == 0)
		*core_info_tmp = required_num << CORE_INFO_AMOUNT_OFFSET;
	else
		required_num = (*core_info_tmp >> CORE_INFO_AMOUNT_OFFSET);

	LOG_DBG("%s:required_num=%d,core_info=%x\n", __func__, required_num,
	       *core_info);

	if (required_num) {
		/* a valid free Core with specified core type */
		for (i = 0; i < total_subsys_num; i++) {
			if (dev[i].subsys_data.core_info.type_info & (1 << core_type)) {
				// modified by Xiaogang.Li
				// core_type = (core_type == CORE_VC8000EJ ? CORE_VC8000E : core_type);
				if (dev[i].is_valid && CheckCoreOccupation(&dev[i], core_type)) {
					*core_info_tmp =
						((((*core_info_tmp >> CORE_INFO_AMOUNT_OFFSET) - 1)
						  << CORE_INFO_AMOUNT_OFFSET) | (*core_info_tmp & 0x0FF));
					*core_info_tmp = (*core_info_tmp | (1 << i));
					if ((*core_info_tmp >> CORE_INFO_AMOUNT_OFFSET) == 0) {
						ret = 1;
						spin_lock_irqsave(&owner_lock, flags);
						*core_info = (reserved_job_id << 16) | (*core_info_tmp & 0xFF);
						dev[i].job_id[core_type] = reserved_job_id;
						/*maintain job_id in 16 bits for core_info can
						 *only save job_id in high 16 bits
						 */
						reserved_job_id = (reserved_job_id + 1) & 0xFFFF;
						spin_unlock_irqrestore(&owner_lock, flags);
						*core_info_tmp = 0;
						required_num = 0;
						break;
					}
				}
			}
		}
	} else
		ret = 1;

	LOG_DBG("*core_info = %x\n", *core_info);
	return ret;
}

static long ReserveEncoder(hantroenc_t *dev, u32 *core_info)
{
	u32 core_info_tmp = 0;
#ifdef MULTI_THR_TEST
	struct wait_list_node *wait_node;
	u32 start_id = 0;
#endif

	/*If HW resources are shared inter cores, just make sure only one is
	 *using the HW
	 */
	if (dev[0].subsys_data.cfg.resource_shared) {
		if (down_interruptible(&enc_core_sem))
			return -ERESTARTSYS;
	}

#ifdef MULTI_THR_TEST
	while (1) {
		start_id = request_wait_node(&wait_node, start_id);
		if (wait_node->sem_used == 1) {
			if (GetWorkableCore(dev, core_info, &core_info_tmp)) {
				down_interruptible(&wait_node->wait_sem);
				wait_node->sem_used = 0;
				wait_node->used_flag = 0;
				break;
			} else {
				start_id++;
			}
		} else {
			wait_node->wait_cond = *core_info;
			list_add_tail(&wait_node->wait_list, &reserve_header);
			down_interruptible(&wait_node->wait_sem);
			*core_info = wait_node->wait_cond;
			list_del(&wait_node->wait_list);
			wait_node->sem_used = 0;
			wait_node->used_flag = 0;
			break;
		}
	}
#else

	/* lock a core that has specified core id*/
	if (wait_event_interruptible(hw_queue,
				     GetWorkableCore(dev, core_info,
						     &core_info_tmp) != 0))
		return -ERESTARTSYS;
#endif
	return 0;
}

static void ReleaseEncoder(hantroenc_t *dev, u32 *core_info)
{
	unsigned long flags;
	u8 core_type = 0, subsys_idx = 0, unCheckPid = 0;

	unCheckPid = (u8)((*core_info) >> 31);
#ifdef MULTI_THR_TEST
	u32 release_ok = 0;
	struct list_head *node = NULL;
	struct wait_list_node *wait_node;
	u32 core_info_tmp = 0;
#endif
	subsys_idx = (u8)((*core_info & 0xF0) >> 4);
	core_type = (u8)(*core_info & 0x0F);

	LOG_DBG("%s:subsys_idx=%d,core_type=%x\n", __func__, subsys_idx,
	       core_type);
	/* release specified subsys and core type */

	if (dev[subsys_idx].subsys_data.core_info.type_info & (1 << core_type)) {
		// modified by Xiaogang.Li
		// core_type = (core_type == CORE_VC8000EJ ? CORE_VC8000E : core_type);
		spin_lock_irqsave(&owner_lock, flags);
		LOG_DBG("subsys[%d].pid[%d]=%d,current->pid=%d\n", subsys_idx,
		       core_type, dev[subsys_idx].pid[core_type], current->pid);
#ifdef MULTI_THR_TEST
		if (dev[subsys_idx].is_reserved[core_type])
#else
		if (dev[subsys_idx].is_reserved[core_type] &&
		    (dev[subsys_idx].pid[core_type] == current->pid ||
		     unCheckPid == 1))
#endif
		{
			dev[subsys_idx].pid[core_type] = -1;
			dev[subsys_idx].is_reserved[core_type] = 0;
			dev[subsys_idx].irq_received[core_type] = 0;
			dev[subsys_idx].irq_status[core_type] = 0;
			dev[subsys_idx].job_id[core_type] = 0;
			spin_unlock_irqrestore(&owner_lock, flags);
#ifdef MULTI_THR_TEST
			release_ok = 0;
			if (list_empty(&reserve_header)) {
				request_wait_sema(&wait_node);
				up(&wait_node->wait_sem);
			} else {
				list_for_each(node, &reserve_header) {
					wait_node = container_of(node, struct wait_list_node, wait_list);
					if ((GetWorkableCore(dev, &wait_node->wait_cond, &core_info_tmp)) &&
					    wait_node->sem_used == 0) {
						release_ok = 1;
						wait_node->sem_used = 1;
						up(&wait_node->wait_sem);
						break;
					}
				}
				if (release_ok == 0) {
					request_wait_sema(&wait_node);
					up(&wait_node->wait_sem);
				}
			}
#endif

		} else {
			if (dev[subsys_idx].pid[core_type] != current->pid &&
			    unCheckPid == 0) {
				LOG_INFO("WARNING:pid(%d) is trying to release core reserved by pid(%d)\n",
					current->pid, dev[subsys_idx].pid[core_type]);
			}
			spin_unlock_irqrestore(&owner_lock, flags);
		}
		//wake_up_interruptible_all(&hw_queue);
	}
#ifndef MULTI_THR_TEST
	wake_up_interruptible_all(&hw_queue);
#endif
	if (dev->subsys_data.cfg.resource_shared)
		up(&enc_core_sem);
}

static long hantroenc_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	int err = 0;
	unsigned int tmp;
	long retval = 0;

	// LOG_DBG("ioctl cmd %08ux\n", cmd);
	LOG_TRACE("ioctl cmd 0x%08x, type=%c, nr=%d\n", cmd, _IOC_TYPE(cmd), _IOC_NR(cmd));
	/* extract the type and number bitfields, and don't encode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
	if (_IOC_TYPE(cmd) != HANTRO_IOC_MAGIC)
		return -ENOTTY;
	if ((_IOC_TYPE(cmd) == HANTRO_IOC_MAGIC &&
	     _IOC_NR(cmd) > HANTRO_IOC_MAXNR))
		return -ENOTTY;

	/* the direction is a bitmask, and VERIFY_WRITE catches R/W
     * transfers. `Type' is user-oriented, while
     * access_ok is kernel-oriented, so the concept of "read" and
     * "write" is reversed
     */
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
	if (err)
		return -EFAULT;

	switch (cmd) {
	case HANTRO_IOCH_GET_VCMD_ENABLE: {
		__put_user(0, (unsigned long __user  *)arg);
		break;
	}

	case HANTRO_IOCH_GET_MMU_ENABLE: {
		__put_user(0, (unsigned int __user *)arg);
		break;
	}

	case HANTRO_IOCG_HWOFFSET: {
		u32 id;

		__get_user(id, (u32 __user *)arg);

		if (id >= total_subsys_num)
			return -EFAULT;
		__put_user(hantroenc_data[id].subsys_data.cfg.base_addr,
			   (unsigned long __user *)arg);
		break;
	}

	case HANTRO_IOCG_HWIOSIZE: {
		u32 id;
		u32 io_size;

		__get_user(id, (u32 __user *)arg);

		if (id >= total_subsys_num)
			return -EFAULT;
		io_size = hantroenc_data[id].subsys_data.cfg.iosize;
		__put_user(io_size, (u32 __user *)arg);

		return 0;
	}
	case HANTRO_IOCG_SRAMOFFSET:
		__put_user(sram_base, (unsigned long __user *)arg);
		break;
	case HANTRO_IOCG_SRAMEIOSIZE:
		__put_user(sram_size, (unsigned int __user *)arg);
		break;
	case HANTRO_IOCG_CORE_NUM:
		__put_user(total_subsys_num, (unsigned int __user *)arg);
		break;
	case HANTRO_IOCG_CORE_INFO: {
		u32 idx;
		SUBSYS_CORE_INFO in_data;

		retval = copy_from_user(&in_data, (void __user *)arg, sizeof(SUBSYS_CORE_INFO));
		if (retval) {
			LOG_DBG("copy_from_user failed, returned %li\n", retval);
			return -EFAULT;
		}

		idx = in_data.type_info;
		if (idx > total_subsys_num - 1)
			return -1;

		retval = copy_to_user((void __user *)arg,
			     &hantroenc_data[idx].subsys_data.core_info,
			     sizeof(SUBSYS_CORE_INFO));
		if (retval) {
			LOG_DBG("copy_to_user failed, returned %li\n", retval);
			return -EFAULT;
		}

		break;
	}
	case HANTRO_IOCH_ENC_RESERVE: {
		u32 core_info;
		int ret;

		LOG_DBG("Reserve ENC Cores\n");
		__get_user(core_info, (u32 __user *)arg);
		LOG_TRACE("Reserve ENC Core = 0x%x\n", core_info);
		ret = ReserveEncoder(hantroenc_data, &core_info);
		if (ret == 0)
			__put_user(core_info, (u32 __user *)arg);
		return ret;
	}
	case HANTRO_IOCH_ENC_RELEASE: {
		u32 core_info;

		__get_user(core_info, (u32 __user *)arg);
		LOG_TRACE("Release ENC Core = 0x%x\n", core_info);

		LOG_DBG("Release ENC Core\n");

		ReleaseEncoder(hantroenc_data, &core_info);

		break;
	}

	case HANTRO_IOCG_CORE_WAIT: {
		u32 core_info;
		u32 irq_status;

		__get_user(core_info, (u32 __user *)arg);

		tmp = WaitEncReady(hantroenc_data, &core_info, &irq_status);
		if (tmp == 0) {
			__put_user(irq_status, (unsigned int __user *)arg);
			return core_info; //return core_id
		} else {
			return -1;
		}

		break;
	}
	case HANTRO_IOCG_ANYCORE_WAIT_POLLING: {
		CORE_WAIT_OUT out;

		memset(&out, 0, sizeof(CORE_WAIT_OUT));

		tmp = WaitEncAnyReadyByPolling(hantroenc_data, &out);
		if (tmp == 0) {
			retval = copy_to_user((void __user *)arg, &out, sizeof(CORE_WAIT_OUT));
			if (retval) {
				LOG_DBG("copy_to_user failed, returned %li\n", retval);
				return -EFAULT;
			}

			return 0;
		} else
			return -1;

		break;
	}
	case HANTRO_IOCG_ANYCORE_WAIT: {
		CORE_WAIT_OUT out;

		memset(&out, 0, sizeof(CORE_WAIT_OUT));

		tmp = WaitEncAnyReady(hantroenc_data, &out);
		if (tmp == 0) {
			retval = copy_to_user((void __user *)arg, &out, sizeof(CORE_WAIT_OUT));
			if (retval) {
				LOG_DBG("copy_to_user failed, returned %li\n", retval);
				return -EFAULT;
			}

			return 0;
		} else
			return -1;

		break;
	}

	case HANTRO_IOCG_DUMP_ALL_REGISTERS: {
#if (OUTPUT_LOG_LEVEL & VC_LOG_LEVEL_TRACE)
		hantroenc_t *dev = (hantroenc_t *)filp->private_data;
#endif
		int i;
		unsigned long flags;

		spin_lock_irqsave(&owner_lock, flags);
		LOG_TRACE("Reg Dump Start <0x%px/0x%08x, 0x%08x>\n", (unsigned int *)dev->hwregs, (unsigned int)dev->subsys_data.cfg.base_addr, (unsigned int)dev->subsys_data.cfg.iosize);
		LOG_TRACE("Reg Offset Core0 <0x%08x, 0x%08x>\n", (unsigned int)dev->subsys_data.core_info.offset[0], (unsigned int)dev->subsys_data.core_info.regSize[0]);
		for (i = 0; i < 271*4; i += 4) {
			LOG_TRACE("\t swreg%d = %08X\n", i/4, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + i)));
		}
		LOG_TRACE("Reg Dump End\n");
		spin_unlock_irqrestore(&owner_lock, flags);
		break;
	}

	case HANTRO_IOCG_DUMP_KEY_REGISTERS: {
#if (OUTPUT_LOG_LEVEL & VC_LOG_LEVEL_TRACE)
		hantroenc_t *dev = (hantroenc_t *)filp->private_data;
#endif
		unsigned long flags;
		spin_lock_irqsave(&owner_lock, flags);

		/* partitial registers */
		LOG_TRACE("\t offset[CORE_ES_VENC] = %08X\n", (unsigned int)dev->subsys_data.core_info.offset[0]);
		LOG_TRACE("\t swreg%d = %08X\n", 0, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0])));
		LOG_TRACE("\t swreg%d = %08X\n", 1, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*1)));
		LOG_TRACE("\t swreg%d = %08X\n", 2, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*2)));
		LOG_TRACE("\t swreg%d = %08X\n", 3, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*3)));
		LOG_TRACE("\t swreg%d = %08X\n", 4, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*4)));
		LOG_TRACE("\t swreg%d = %08X\n", 5, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*5)));
		LOG_TRACE("\t swreg%d = %08X\n", 6, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*6)));
		LOG_TRACE("\t swreg%d = %08X\n", 7, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*7)));

		LOG_TRACE("\t swreg%d = %08X\n", 38, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*38)));
		LOG_TRACE("\t swreg%d = %08X\n", 80, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*80)));
		LOG_TRACE("\t swreg%d = %08X\n", 81, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*81)));

		LOG_TRACE("\t swreg%d = %08X\n", 246, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*246)));
		LOG_TRACE("\t swreg%d = %08X\n", 261, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[0] + 4*261)));


		LOG_TRACE("\t offset[CORE_AXIFE] = %08X\n", (unsigned int)dev->subsys_data.core_info.offset[CORE_AXIFE]);
		LOG_TRACE("\t swreg%d = %08X\n", 0, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[CORE_AXIFE])));
		LOG_TRACE("\t swreg%d = %08X\n", 10, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[CORE_AXIFE] + 4*10)));
		LOG_TRACE("\t swreg%d = %08X\n", 11, ioread32((void __iomem *)(dev->hwregs + dev->subsys_data.core_info.offset[CORE_AXIFE] + 4*11)));


		spin_unlock_irqrestore(&owner_lock, flags);
		break;
	}
#ifdef SUPPORT_DMA_HEAP
	case HANTRO_IOCH_DMA_HEAP_GET_IOVA: {
		struct dmabuf_cfg dbcfg;
		size_t buf_size = 0;
		struct heap_mem *hmem, *hmem_d1;
		struct dmabuf_priv *db_priv = (struct dmabuf_priv *)filp->private_data;

		if (copy_from_user(&dbcfg, (void __user *)arg, sizeof(struct dmabuf_cfg)) != 0)
			return -EFAULT;

		LOG_DBG("import dmabuf_fd = %d\n", dbcfg.dmabuf_fd);

		/* map the pha to dma addr(iova)*/
		hmem = common_dmabuf_heap_import_from_user(&db_priv->root, dbcfg.dmabuf_fd);
		if(IS_ERR(hmem)) {
			LOG_ERR("dmabuf-heap import from userspace failed\n");
			return -ENOMEM;
		}

		if (venc_pdev_d1) {
			hmem_d1 = common_dmabuf_heap_import_from_user(&db_priv->root_d1, dbcfg.dmabuf_fd);
			if(IS_ERR(hmem_d1)) {
				common_dmabuf_heap_release(hmem);
				LOG_ERR("dmabuf-heap alloc from userspace failed for d1\n");
				return -ENOMEM;
			}
		}

		/* get the size of the dmabuf allocated by dmabuf_heap */
		buf_size = common_dmabuf_heap_get_size(hmem);
		LOG_DBG("dmabuf info: CPU VA:0x%lx, PA:0x%lx, DMA addr(iova):0x%lx, size=0x%lx\n",
				(unsigned long)hmem->vaddr, (unsigned long)sg_phys(hmem->sgt->sgl), (unsigned long)sg_dma_address(hmem->sgt->sgl), (unsigned long)buf_size);

		dbcfg.iova = (unsigned long)sg_dma_address(hmem->sgt->sgl);
		if (venc_pdev_d1) {
			unsigned long iova_d1;

			iova_d1 = (unsigned long)sg_dma_address(hmem_d1->sgt->sgl);
			if (dbcfg.iova != iova_d1) {
				common_dmabuf_heap_release(hmem);
				common_dmabuf_heap_release(hmem_d1);
				LOG_ERR("VENC: IOVA addrs of d0 and d1 are not the same\n");
				return -EFAULT;
			}
		}

		retval = copy_to_user((u32 __user *)arg, &dbcfg, sizeof(struct dmabuf_cfg));
		if (retval) {
			common_dmabuf_heap_release(hmem);
			if (venc_pdev_d1)
				common_dmabuf_heap_release(hmem_d1);
			LOG_DBG("copy_to_user failed, returned %li\n", retval);
			return -EFAULT;
		}

		return 0;
	}
	case HANTRO_IOCH_DMA_HEAP_PUT_IOVA: {
		struct heap_mem *hmem, *hmem_d1;
		unsigned int dmabuf_fd;
		struct dmabuf_priv *db_priv = (struct dmabuf_priv *)filp->private_data;

		if (copy_from_user(&dmabuf_fd, (void __user *)arg, sizeof(int)) != 0)
			return -EFAULT;

		LOG_DBG("release dmabuf_fd = %d\n", dmabuf_fd);

		/* find the heap_mem */
		hmem = common_dmabuf_lookup_heapobj_by_fd(&db_priv->root, dmabuf_fd);
		if(IS_ERR(hmem)) {
			LOG_ERR("cannot find dmabuf-heap for dmabuf_fd %d\n", dmabuf_fd);
			return -ENOMEM;
		}

		if (venc_pdev_d1) {
			hmem_d1 = common_dmabuf_lookup_heapobj_by_fd(&db_priv->root_d1, dmabuf_fd);
			if (IS_ERR(hmem_d1)) {
				LOG_ERR("cannot find dmabuf-heap for dmabuf_fd %d on d1\n", dmabuf_fd);
				return -EFAULT;
			}
			common_dmabuf_heap_release(hmem_d1);
		}

		common_dmabuf_heap_release(hmem);
		return 0;
	}
#endif

	default: {
		return -1;
	}
	}
	return 0;
}

static int hantroenc_open(struct inode *inode, struct file *filp)
{
	int result = 0;
#ifdef SUPPORT_DMA_HEAP
	struct dmabuf_priv *db_priv;

	db_priv = kzalloc(sizeof(struct dmabuf_priv), GFP_KERNEL);
	if (!db_priv) {
		pr_err("%s: alloc failed\n", __func__);
		return -ENOMEM;
	}

	common_dmabuf_heap_import_init(&db_priv->root, &venc_pdev->dev);
	if (venc_pdev_d1) {
		common_dmabuf_heap_import_init(&db_priv->root_d1, &venc_pdev_d1->dev);
	}
	db_priv->dev = (void *)hantroenc_data;

	filp->private_data = (void *)db_priv;
#else
	hantroenc_t *dev = hantroenc_data;

	filp->private_data = (void *)dev;
#endif

	LOG_DBG("dev opened\n");
	return result;
}

static int hantroenc_release(struct inode *inode, struct file *filp)
{
#ifdef SUPPORT_DMA_HEAP
	struct dmabuf_priv *db_priv= (struct dmabuf_priv *)filp->private_data;
	hantroenc_t *dev = (hantroenc_t *)db_priv->dev;
#else
	hantroenc_t *dev = (hantroenc_t *)filp->private_data;
#endif
	u32 core_id = 0, i = 0;

#ifdef hantroenc_DEBUG
	dump_regs((unsigned long)dev); /* dump the regs */
#endif
	unsigned long flags;

	LOG_DBG("dev closed\n");

	for (i = 0; i < total_subsys_num; i++) {
		for (core_id = 0; core_id < CORE_MAX; core_id++) {
			spin_lock_irqsave(&owner_lock, flags);
			if (dev[i].is_reserved[core_id] == 1 &&
			    dev[i].pid[core_id] == current->pid) {
				dev[i].pid[core_id] = -1;
				dev[i].is_reserved[core_id] = 0;
				dev[i].irq_received[core_id] = 0;
				dev[i].irq_status[core_id] = 0;
				LOG_DBG("release reserved core\n");
			}
			spin_unlock_irqrestore(&owner_lock, flags);
		}
	}

	wake_up_interruptible_all(&hw_queue);

	if (dev->subsys_data.cfg.resource_shared)
		up(&enc_core_sem);

#ifdef SUPPORT_DMA_HEAP
	common_dmabuf_heap_import_uninit(&db_priv->root);
	if (venc_pdev_d1) {
		common_dmabuf_heap_import_uninit(&db_priv->root_d1);
	}
	kfree(db_priv);
#endif

	return 0;
}

/* VFS methods */
static struct file_operations hantroenc_fops = {
	.owner = THIS_MODULE,
	.open = hantroenc_open,
	.release = hantroenc_release,
	.unlocked_ioctl = hantroenc_ioctl,
	.fasync = NULL,
};

int hantroenc_normal_init(void)
{
	int result = 0, array_revise = 0;
	int i, j;

	// pr_info("[%s]build version: %s\n", VENC_DEV_NAME, ES_VENC_GIT_VER);
	total_subsys_num = sizeof(vc8000e_subsys_array) / sizeof(SUBSYS_CONFIG);
	for (i = 0; i < total_subsys_num; i++) {
		LOG_INFO("module init - subsys[%d] addr = %016lx\n", i,
			(unsigned long)vc8000e_subsys_array[i].base_addr);
		if (!vc8000e_subsys_array[i].base_addr) {
			array_revise++;
		}
	}
	total_subsys_num -= array_revise;
	LOG_INFO("total_subsys_num %d after revise.\n", total_subsys_num);

	hantroenc_data = vmalloc(sizeof(hantroenc_t) * total_subsys_num);
	if (!hantroenc_data)
		goto err_vmalloc;
	memset(hantroenc_data, 0, sizeof(hantroenc_t) * total_subsys_num);

	for (i = 0; i < total_subsys_num; i++) {
		hantroenc_data[i].subsys_data.cfg = vc8000e_subsys_array[i];
		hantroenc_data[i].async_queue = NULL;
		hantroenc_data[i].hwregs = NULL;
		hantroenc_data[i].subsys_id = i;
		// hantroenc_data[i].type_main_core = vc8000e_subsys_array[i].type_main_core;
		for (j = 0; j < CORE_MAX; j++)
			hantroenc_data[i].subsys_data.core_info.irq[j] = -1;
	}

	array_revise = 0;
	total_core_num = sizeof(vc8000e_core_array) / sizeof(CORE_CONFIG);
	for (i = 0; i < total_core_num; i++) {
		if (!vc8000e_core_array[i].reg_size) {
			array_revise++;
			continue;
		}

		hantroenc_data[vc8000e_core_array[i].subsys_idx].subsys_data.core_info.type_info |= (1 << (vc8000e_core_array[i].core_type));
		hantroenc_data[vc8000e_core_array[i].subsys_idx].subsys_data.core_info.offset[vc8000e_core_array[i].core_type] = vc8000e_core_array[i].offset;
		hantroenc_data[vc8000e_core_array[i].subsys_idx].subsys_data.core_info.regSize[vc8000e_core_array[i].core_type] = vc8000e_core_array[i].reg_size;
		hantroenc_data[vc8000e_core_array[i].subsys_idx].subsys_data.core_info.irq[vc8000e_core_array[i].core_type] = vc8000e_core_array[i].irq;
	}
	total_core_num -= array_revise;
	LOG_INFO("es_venc: total_core_num %d after revise.\n", total_core_num);

	result = register_chrdev(hantroenc_major, "es_venc", &hantroenc_fops);
	if (result < 0) {
		LOG_ERR("es_venc: unable to get major <%d>\n",
			hantroenc_major);
		goto err_vmalloc;
	} else if (result != 0) {
		/* this is for dynamic major */
		hantroenc_major = result;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
    venc_class = class_create(VENC_CLASS_NAME);
#else
    venc_class = class_create(THIS_MODULE, VENC_CLASS_NAME);
#endif
    if (IS_ERR(venc_class)) {
        venc_class = NULL;
        LOG_ERR("%s(%d): Failed to create the class.\n", __func__, __LINE__);
        goto err_register;
    }else{
        device_create(venc_class, NULL, MKDEV(hantroenc_major, 0), NULL, VENC_DEV_NAME);
        LOG_NOTICE("create device node-%s done\n", VENC_DEV_NAME);
    }

	result = ReserveIO();
	if (result < 0)
		goto err_register;

	//ResetAsic(hantroenc_data);  /* reset hardware */

	sema_init(&enc_core_sem, 1);

	/* get the IRQ line */
	for (i = 0; i < total_subsys_num; i++) {
		if (hantroenc_data[i].is_valid == 0)
			continue;
		for (j = 0; j < CORE_MAX; j++) {
			if (hantroenc_data[i].subsys_data.core_info.irq[j] !=
			    -1) {
				result = request_irq(
					hantroenc_data[i].subsys_data.core_info.irq[j],
					hantroenc_isr,
					IRQF_SHARED,
					VENC_NORMDRV_NAME,
					(void *)&hantroenc_data[i]);
				if (result == -EINVAL) {
					LOG_ERR("es_venc: Bad irq number or handler\n");
					ReleaseIO();
					goto err_register;
				} else if (result == -EBUSY) {
					LOG_ERR("es_venc: IRQ <%d> busy, change your config\n",
					       hantroenc_data[i].subsys_data.core_info.irq[j]);
					ReleaseIO();
					goto err_register;
				}
			} else {
				LOG_INFO("es_venc: IRQ not in use!\n");
			}
		}
	}
#ifdef MULTI_THR_TEST
	init_reserve_wait(total_subsys_num);
#endif
	LOG_INFO("es_venc: module inserted. Major <%d>\n", hantroenc_major);

	return 0;

err_register:
	unregister_chrdev(hantroenc_major, "es_venc");
	if (venc_class) {
        LOG_NOTICE("destroy device node - %s\n", VENC_DEV_NAME);
        device_destroy(venc_class, MKDEV(hantroenc_major, 0));
        class_destroy(venc_class);
        venc_class = NULL;
    }
err_vmalloc:
	if (hantroenc_data)
		vfree(hantroenc_data);
	LOG_INFO("es_venc: module not inserted\n");
	return result;
}

void hantroenc_normal_cleanup(void)
{
	int i = 0, j = 0;

	for (i = 0; i < total_subsys_num; i++) {
		if (hantroenc_data[i].is_valid == 0)
			continue;
		//writel(0, hantroenc_data[i].hwregs + 0x14); /* disable HW */
		//writel(0, hantroenc_data[i].hwregs + 0x04); /* clear enc IRQ */

		/* free the core IRQ */
		for (j = 0; j < total_core_num; j++) {
			if (hantroenc_data[i].subsys_data.core_info.irq[j] != -1) {
				free_irq(hantroenc_data[i].subsys_data.core_info.irq[j],
					 (void *)&hantroenc_data[i]);
			}
		}
	}

	ReleaseIO();
	vfree(hantroenc_data);

	unregister_chrdev(hantroenc_major, "es_venc");
	if (venc_class) {
		LOG_NOTICE("destroy device node - %s\n", VENC_DEV_NAME);
		device_destroy(venc_class, MKDEV(hantroenc_major, 0));
		class_destroy(venc_class);
		venc_class = NULL;
	}

	LOG_INFO("es_venc: module removed\n");
	return;
}

static int ReserveIO(void)
{
	u32 hwid;
	int i;
	u32 found_hw = 0, hw_cfg;
	// u32 VC8000E_core_idx;
	u32 type_info, core_idx; //VC8000E or VC8000EJ

	for (i = 0; i < total_subsys_num; i++) {
		if (!request_mem_region(
			    hantroenc_data[i].subsys_data.cfg.base_addr,
			    hantroenc_data[i].subsys_data.cfg.iosize,
			    VENC_NORMDRV_NAME)) {
			LOG_INFO("es_venc: failed to reserve HW regs\n");
			continue;
		}

		/* map device register */
		hantroenc_data[i].hwregs = (volatile u8 *)ioremap(
			hantroenc_data[i].subsys_data.cfg.base_addr,
			hantroenc_data[i].subsys_data.cfg.iosize);
		if (!hantroenc_data[i].hwregs) {
			LOG_INFO("es_venc: failed to ioremap HW regs\n");
			ReleaseIO();
			continue;
		}

		/*read hwid and check validness and store it*/
		// VC8000E_core_idx = GET_ENCODER_IDX(
		// 	hantroenc_data[0].subsys_data.core_info.type_info);
		// if (!(hantroenc_data[i].subsys_data.core_info.type_info &
		//       (1 << CORE_VC8000E)))
		// 	VC8000E_core_idx = CORE_CUTREE;
		// hwid = (u32)ioread32(
		// 	(void __iomem *)hantroenc_data[i].hwregs +
		// 	hantroenc_data[i].subsys_data.core_info.offset[VC8000E_core_idx]);
		// core_idx = hantroenc_data[i].type_main_core;
		type_info = hantroenc_data[i].subsys_data.core_info.type_info;
		core_idx = (type_info & (1 << CORE_VC8000EJ)) ? CORE_VC8000EJ : CORE_VC8000E;
		hwid = (u32)ioread32(
			(void __iomem *)hantroenc_data[i].hwregs +
			hantroenc_data[i].subsys_data.core_info.offset[core_idx]);
		LOG_INFO("%s:hwid=0x%08x\n",
			core_idx == CORE_VC8000EJ ? "es_vencj" : "es_venc", hwid);

		/* check for encoder HW ID */
		if (((((hwid >> 16) & 0xFFFF) !=
		      ((ENC_HW_ID1 >> 16) & 0xFFFF))) &&
		    ((((hwid >> 16) & 0xFFFF) !=
		      ((ENC_HW_ID2 >> 16) & 0xFFFF)))) {
			LOG_INFO("es_venc: HW not found at 0x%lx\n",
				hantroenc_data[i].subsys_data.cfg.base_addr);
#ifdef hantroenc_DEBUG
			dump_regs((unsigned long)&hantroenc_data);
#endif
			hantroenc_data[i].is_valid = 0;
			ReleaseIO();
			continue;
		}
		hantroenc_data[i].hw_id = hwid;
		hantroenc_data[i].is_valid = 1;
		found_hw = 1;

		// hw_cfg = (u32)ioread32(
		// 	(void __iomem *)hantroenc_data[i].hwregs +
		// 	hantroenc_data[i].subsys_data.core_info.offset[VC8000E_core_idx] + 320);
		hw_cfg = (u32)ioread32(
			(void __iomem *)hantroenc_data[i].hwregs +
			hantroenc_data[i].subsys_data.core_info.offset[core_idx] + 320);
		hantroenc_data[i].subsys_data.core_info.type_info &= 0xFFFFFFFC;
		if (hw_cfg & 0x88000000)
			hantroenc_data[i].subsys_data.core_info.type_info |= (1 << CORE_VC8000E);
		if (hw_cfg & 0x00008000)
			hantroenc_data[i].subsys_data.core_info.type_info |= (1 << CORE_VC8000EJ);
		LOG_INFO("es_venc[%d] hw_cfg=0x%08x, type_info=0x%08x\n",
			i, hw_cfg, hantroenc_data[i].subsys_data.core_info.type_info);
		LOG_INFO("es_venc[%d]: HW at base <0x%lx> with HWID <0x%08x>\n",
			i, hantroenc_data[i].subsys_data.cfg.base_addr, hwid);
	}

	if (found_hw == 0) {
		LOG_ERR("es_venc: NO ANY HW found!!\n");
		return -1;
	}

	return 0;
}

static void ReleaseIO(void)
{
	u32 i;

	for (i = 0; i <= total_subsys_num; i++) {
		if (hantroenc_data[i].is_valid == 0)
			continue;
		if (hantroenc_data[i].hwregs)
			iounmap((void __iomem *)hantroenc_data[i].hwregs);
		release_mem_region(hantroenc_data[i].subsys_data.cfg.base_addr,
				   hantroenc_data[i].subsys_data.cfg.iosize);
	}
}

static irqreturn_t hantroenc_isr(int irq, void *dev_id)
{
	unsigned int handled = 0;
	hantroenc_t *dev = (hantroenc_t *)dev_id;
	u32 irq_status;
	unsigned long flags;
	u32 core_type = 0, i = 0;
	unsigned long reg_offset = 0;
	u32 hwId, majorId, wClr;

	/*get core id by irq from subsys config*/
	for (i = 0; i < CORE_MAX; i++) {
		if (dev->subsys_data.core_info.irq[i] == irq) {
			core_type = i;
			reg_offset = dev->subsys_data.core_info.offset[i];
			break;
		}
	}

	/*If core is not reserved by any user, but irq is received, just clean it*/
	spin_lock_irqsave(&owner_lock, flags);
	if (!dev->is_reserved[core_type]) {
		LOG_DBG(
			"isr:received IRQ but core is not reserved!\n");
		irq_status = (u32)ioread32(
			(void __iomem *)(dev->hwregs + reg_offset + 0x04));
		if (irq_status & 0x01) {
			/*  Disable HW when buffer over-flow happen
			 *  HW behavior changed in over-flow
			 *    in-pass, HW cleanup HWIF_ENC_E auto
			 *    new version:  ask SW cleanup HWIF_ENC_E when buffer over-flow
			 */
			if (irq_status & 0x20)
				iowrite32(0, (void __iomem *)(dev->hwregs + reg_offset + 0x14));

			/* clear all IRQ bits. (hwId >= 0x80006100) means IRQ is cleared by writing 1 */
			hwId = ioread32((void __iomem *)dev->hwregs + reg_offset);
			majorId = (hwId & 0x0000FF00) >> 8;
			wClr = (majorId >= 0x61) ? irq_status : (irq_status & (~0x1FD));
			iowrite32(wClr, (void __iomem *)(dev->hwregs + reg_offset + 0x04));
		}
		spin_unlock_irqrestore(&owner_lock, flags);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&owner_lock, flags);

	LOG_DBG("isr:received IRQ!\n");
	irq_status = (u32)ioread32((void __iomem *)(dev->hwregs + reg_offset + 0x04));
	LOG_DBG("irq_status of subsys %d core %d is:%x\n",
		dev->subsys_id, core_type, irq_status);
	if (irq_status & 0x01) {
		/*  Disable HW when buffer over-flow happen
	     *  HW behavior changed in over-flow
	     *    in-pass, HW cleanup HWIF_ENC_E auto
	     *    new version:  ask SW cleanup HWIF_ENC_E when buffer over-flow
	     */
		if (irq_status & 0x20)
			iowrite32(0, (void __iomem *)(dev->hwregs + reg_offset + 0x14));

		/* clear all IRQ bits. (hwId >= 0x80006100) means IRQ is cleared by writing 1 */
		hwId = ioread32((void __iomem *)dev->hwregs + reg_offset);
		majorId = (hwId & 0x0000FF00) >> 8;
		wClr = (majorId >= 0x61) ? irq_status : (irq_status & (~0x1FD));
		iowrite32(wClr, (void __iomem *)(dev->hwregs + reg_offset + 0x04));

		spin_lock_irqsave(&owner_lock, flags);
		dev->irq_received[core_type] = 1;
		dev->irq_status[core_type] = irq_status & (~0x01);
		spin_unlock_irqrestore(&owner_lock, flags);

		wake_up_interruptible_all(&enc_wait_queue);
		handled++;
	}
	if (!handled) {
		LOG_DBG("IRQ received, but not hantro's!\n");
	}
	return IRQ_HANDLED;
}

#ifdef hantroenc_DEBUG
static void ResetAsic(hantroenc_t *dev)
{
	int i, n;

	for (n = 0; n < total_subsys_num; n++) {
		if (dev[n].is_valid == 0)
			continue;
		iowrite32(0, (void *)(dev[n].hwregs + 0x14));
		for (i = 4; i < dev[n].subsys_data.cfg.iosize; i += 4)
			iowrite32(0, (void *)(dev[n].hwregs + i));
	}
}

static void dump_regs(unsigned long data)
{
	hantroenc_t *dev = (hantroenc_t *)data;
	int i;

	LOG_DBG("Reg Dump Start\n");
	for (i = 0; i < dev->iosize; i += 4) {
		LOG_DBG("\toffset %02X = %08X\n", i, ioread32(dev->hwregs + i));
	}
	LOG_DBG("Reg Dump End\n");
}
#endif
