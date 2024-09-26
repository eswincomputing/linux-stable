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
#include <linux/mailbox/eswin-mailbox.h>
#include <asm/cacheflush.h>
#include <linux/regmap.h>
#include <linux/pm_runtime.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include <linux/clk.h>
#include <linux/slab.h>

#include "dsp_ioctl.h"
#include "dsp_main.h"

#define DRIVER_NAME "eswin-dsp"
#define FIRMWARE_SIZE_MAX (1024 * 1024 * 4)

enum dsp_irq_mode {
	DSP_IRQ_NONE,
	DSP_IRQ_LEVEL,
	DSP_IRQ_MAX,
};

struct es_dsp_resource {
	u32 process_id;
	u32 numa_id;
	char *firmware_name;
};

struct es_dsp_hw {
	struct es_dsp *es_dsp;
	struct platform_device *pdev;
	spinlock_t send_lock;
	dsp_request_t *emu_irq_task;
	wait_queue_head_t emu_irq_wait;
	atomic_t emu_task_count;
	es_dsp_h2d_msg msg;
};

void dsp_send_invalid_code_seg(struct es_dsp_hw *hw, struct dsp_op_desc *op)
{
}

void es_dsp_send_irq(struct es_dsp_hw *hw, dsp_request_t *req)
{
	es_dsp_h2d_msg msg;

	msg.command = DSP_CMD_FLAT1;
	msg.allow_eval = req->allow_eval;
	msg.poll_mode = req->poll_mode;
	msg.sync_cache = req->sync_cache;
	msg.iova_ptr = req->dsp_flat1_iova;

	if (hw->emu_irq_task == NULL && atomic_read(&hw->emu_task_count) == 0) {
		dsp_debug("%s, wake up emu_irq_wait.\n", __func__);
		hw->emu_irq_task = req;
		hw->msg = msg;

		atomic_add(1, &hw->emu_task_count);
		wake_up_interruptible_nr(&hw->emu_irq_wait, 1);
	}
}

int es_dsp_reboot_core(struct es_dsp_hw *hw)
{
	struct task_struct *task;
	struct es_dsp *dsp = hw->es_dsp;

	hw->emu_irq_task = NULL;
	atomic_set(&hw->emu_task_count, 0);
	dsp->off = true;
	for_each_process(task) {
		if (strcmp(task->comm, "eic7700_dsp_fw") == 0) {
			send_sig(SIGKILL, task, 1);
		}
	}
	return -1;
}

int es_dsp_load_op(struct es_dsp_hw *hw, void *op_ptr)
{
	struct dsp_op_desc *op = (struct dsp_op_desc *)op_ptr;

	dsp_debug("this is x86 emu.\n");
	op->op_shared_seg_ptr = NULL;
	strcpy(op->funcs.op_name, op->name);
	return 0;
}

static long dsp_ioctl_wait_irq(struct file *flip,
			       struct dsp_ioctl_task __user *arg)
{
	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;
	struct es_dsp_hw *hw = dsp->hw_arg;
	unsigned long flags;
	struct es_dsp_flat1_desc *flat_desc;
	struct dsp_ioctl_task task;
	es_dsp_h2d_msg msg;

	spin_lock_irqsave(&hw->send_lock, flags);

	while (hw->emu_irq_task == NULL) {
		spin_unlock_irqrestore(&hw->send_lock, flags);

		if (wait_event_interruptible(hw->emu_irq_wait,
					     hw->emu_irq_task != NULL)) {
			dsp_debug(
				"wait event wake up from interrupt, pls restart, pending=0x%x\n",
				signal_pending(current));
			return -ERESTARTSYS;
		}

		spin_lock_irqsave(&hw->send_lock, flags);
	}
	dsp_debug("%s, arg:%px\n", __func__, arg);
	flat_desc = hw->emu_irq_task->flat_virt;
	task.msg = hw->msg;
	dsp_debug("%s, emu_task addr=%px\n", __func__, hw->emu_irq_task);
	dsp_debug("%s, get oper name=%s.\n", __func__,
		  flat_desc->funcs.op_name);

	task.msg.iova_ptr = virt_to_phys((void *)flat_desc);
	task.flat_size = hw->emu_irq_task->flat_size;

	dsp_debug("%s, task flat phys=0x%x, size=0x%x\n", __func__,
		  task.msg.iova_ptr, task.flat_size);
	hw->emu_irq_task = NULL;
	if (copy_to_user((void __user *)arg, (void *)&task,
			 sizeof(struct dsp_ioctl_task))) {
		spin_unlock_irqrestore(&hw->send_lock, flags);
		dsp_err("copy to user failed.\n");
		return -EFAULT;
	}
	spin_unlock_irqrestore(&hw->send_lock, flags);
	return 0;
}

static long dsp_ioctl_send_ack(struct file *flip, unsigned long arg)
{
	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;
	struct es_dsp_hw *hw = dsp->hw_arg;
	struct eswin_mbox_msg msg;
	unsigned long flags;
	int ret;

	dsp_debug("send ack to.\n");
	if (copy_from_user((void *)&msg, (void *)arg,
			   sizeof(struct eswin_mbox_msg))) {
		return -EFAULT;
	}

	atomic_dec_if_positive(&hw->emu_task_count);
	ret = dsp_irq_handler(&msg, dsp);
	return (ret == IRQ_HANDLED) ? 0 : ret;
}

static int dsp_cma_check(struct cma *cma, void *data)
{
	struct dsp_cma_info *tmp;
	if (data == NULL) {
		dsp_err("data is null.\n");
		return 0;
	}

	tmp = (struct dsp_cma_info *)data;
	if (cma != NULL) {
		if (strcmp(cma_get_name(cma), "reserved") != 0) {
			return 0;
		}

		tmp->base = cma_get_base(cma);
		tmp->size = cma_get_size(cma);
		return 1;
	}
	return 0;
}

static long dsp_ioctl_get_cma_info(struct file *flip, unsigned long arg)
{
	struct dsp_cma_info info;
	int ret;

	ret = cma_for_each_area(dsp_cma_check, &info);
	if (!ret) {
		dsp_err("cannot find cma info.\n");
		return -ENODEV;
	}

	if (copy_to_user((void __user *)arg, &info, sizeof(info))) {
		dsp_err("%s, copy to user, err.\n", __func__);
		return -EFAULT;
	}
	return 0;
}

long es_dsp_hw_ioctl(struct file *flip, unsigned int cmd, unsigned long arg)
{
	long retval;

	switch (cmd) {
	case DSP_IOCTL_WAIT_IRQ:
		retval = dsp_ioctl_wait_irq(
			flip, (struct dsp_ioctl_task __user *)arg);
		break;
	case DSP_IOCTL_SEND_ACK:
		retval = dsp_ioctl_send_ack(flip, arg);
		break;
	case DSP_IOCTL_GET_CMA_INFO:
		retval = dsp_ioctl_get_cma_info(flip, arg);
		break;
	default:
		dsp_err("%s, error cmd = 0x%x.\n", __func__, cmd);
		retval = -EINVAL;
		break;
	}
	return retval;
}

int es_dsp_enable(struct es_dsp_hw *hw)
{
	return 0;
}
void es_dsp_disable(struct es_dsp_hw *hw)
{
}
void es_dsp_reset(struct es_dsp_hw *hw)
{
}
void es_dsp_halt(struct es_dsp_hw *hw)
{
}
void es_dsp_release(struct es_dsp_hw *hw)
{
}
long es_dsp_set_rate(struct es_dsp_hw *hw, unsigned long rate)
{
	return 0;
}
int es_dsp_sync(struct es_dsp *dsp)
{
	dsp->off = false;
	return 0;
}

int es_dsp_load_fw_segment(struct es_dsp_hw *hw, const void *image,
			   Elf32_Phdr *phdr)
{
	return 0;
}
int dsp_request_firmware(struct es_dsp *dsp)
{
	return 0;
}
void dsp_release_firmware(struct es_dsp *dsp)
{
	return;
}

int es_dsp_pm_get_sync(struct es_dsp *dsp)
{
	return 0;
}

void es_dsp_pm_put_sync(struct es_dsp *dsp)
{
	return;
}
static int es_dsp_unload_op(struct es_dsp_hw *hw, void *op)
{
}

void dsp_free_flat_mem(struct es_dsp *dsp, u32 size, void *cpu,
		       dma_addr_t dma_addr)
{
	dma_free_coherent(dsp->dev, size, cpu, dma_addr);
}

void *dsp_alloc_flat_mem(struct es_dsp *dsp, u32 dma_len, dma_addr_t *dma_addr)
{
	void *flat = NULL;
	flat = dma_alloc_coherent(dsp->dev, dma_len, dma_addr, GFP_KERNEL);
	return flat;
}

int es_dsp_core_clk_enable(struct es_dsp_hw *hw)
{
	return 0;
}
int es_dsp_core_clk_disable(struct es_dsp_hw *hw)
{
	return 0;
}

int es_dsp_clk_enable(struct es_dsp *dsp)
{
	return 0;
}
void es_dsp_clk_disable(struct es_dsp *dsp)
{
	return;
}

void reset_uart_mutex(struct es_dsp_hw *hw)
{
}

int dsp_enable_irq(struct es_dsp *dsp)
{
	return 0;
}

void dsp_disable_irq(struct es_dsp *dsp)
{

}

void dsp_disable_mbox_clock(struct es_dsp *dsp)
{

}

int dsp_enable_mbox_clock(struct es_dsp *dsp)
{
	return 0;
}

int es_dsp_core_clk_enable(struct es_dsp *dsp)
{
	return 0;
}
int es_dsp_core_clk_disable(struct es_dsp *dsp)
{
	return 0;
}

void dsp_put_resource(struct es_dsp *dsp)
{
	return;
}
int dsp_get_resource(struct platform_device *pdev, struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;
	struct es_dsp_resource *res;

	res = (struct es_dsp_resource *)dev_get_platdata(&pdev->dev);
	dsp->firmware_name =
		res->firmware_name;  //(char *)dev_get_platdata(&pdev->dev);
	dsp->process_id = res->process_id;
	dsp->numa_id = res->numa_id;
	dsp_info("the firmware name=%s.\n", dsp->firmware_name);
	dsp_info("%s, process_id=%d, numa_id=%d.\n", __func__, dsp->process_id,
		 dsp->numa_id);

	return 0;
}

int es_dsp_get_subsys(struct platform_device *pdev, struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;
	hw->pdev = pdev;
	hw->es_dsp = dsp;
	return 0;
}
int es_dsp_hw_init(struct platform_device *pdev, struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;

	if (!hw) {
		return -ENOMEM;
	}

	init_waitqueue_head(&hw->emu_irq_wait);
	atomic_set(&hw->emu_task_count, 0);
	spin_lock_init(&hw->send_lock);

	dsp->firmware_addr = dma_alloc_coherent(dsp->dev, FIRMWARE_SIZE_MAX,
						&dsp->firmware_dev_addr,
						GFP_KERNEL);
	if (IS_ERR_OR_NULL(dsp->firmware_addr)) {
		dev_err(dsp->dev, "failed to alloc firmware memory\n");
		return -ENOMEM;
	}

	return 0;
}

int dsp_get_mbx_node(struct platform_device *pdev)
{
	return 0;
}

void es_dsp_hw_uninit(struct es_dsp *dsp)
{
	if (dsp->firmware_addr != NULL)
		dma_free_coherent(dsp->dev, FIRMWARE_SIZE_MAX,
				  dsp->firmware_addr, dsp->firmware_dev_addr);

	return;
}

#define MAX_DSP_NUM 2
#define FIRM_NAME_MAX 30

static struct platform_device *es_dsp_hw_pdev[MAX_DSP_NUM];

int es_dsp_platform_init(void)
{
	int ret;
	int i;
	char *firm_name[MAX_DSP_NUM];
	struct es_dsp_resource res;

	for (i = 0; i < MAX_DSP_NUM; i++) {
		es_dsp_hw_pdev[i] = platform_device_alloc(DRIVER_NAME, i);
		if (!es_dsp_hw_pdev[i]) {
			i--;
			while (i >= 0) {
				platform_device_put(es_dsp_hw_pdev[i--]);
			}
			ret = -EIO;
			goto err_dev_alloc;
		}
		es_dsp_hw_pdev[i]->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	}

	for (i = 0; i < MAX_DSP_NUM; i++) {
		res.process_id = i;
		res.numa_id = 0;
		dsp_info("%s, proces_id=%d.\n", __func__, res.process_id);
		firm_name[i] = kzalloc(FIRM_NAME_MAX, GFP_KERNEL);
		if (!firm_name[i]) {
			ret = -ENOMEM;
			goto err_add_pdata;
		}
		snprintf(firm_name[i], FIRM_NAME_MAX, "DSP_%d.exe", i);

		res.firmware_name = firm_name[i];
		ret = platform_device_add_data(es_dsp_hw_pdev[i], (void *)&res,
					       sizeof(res));
		if (ret) {
			goto err_add_pdata;
		}
	}

	for (i = 0; i < MAX_DSP_NUM; i++) {
		ret = platform_device_add(es_dsp_hw_pdev[i]);
		if (ret < 0) {
			i--;
			while (i >= 0) {
				platform_device_del(es_dsp_hw_pdev[i]);
			}
			goto err_add_dev;
		}
	}
	dsp_info("%s, successfully new arch\n", __func__);
	return 0;

err_add_dev:
err_add_pdata:
	for (i = 0; i < MAX_DSP_NUM; i++) {
		kfree(firm_name[i]);
	}

	for (i = 0; i < MAX_DSP_NUM; i++) {
		platform_device_put(es_dsp_hw_pdev[i]);
	}
err_dev_alloc:
	return ret;
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

int wait_for_current_tsk_done(struct es_dsp *dsp)
{
	return 0;
}

void dsp_free_hw(struct es_dsp *dsp)
{
	struct es_dsp_hw *hw = (struct es_dsp_hw *)dsp->hw_arg;
	if (!hw) {
		return;
	}

	devm_free(dsp->dev, hw);
	dsp->hw_arg = NULL;
	return;
}

int es_dsp_platform_uninit(void)
{
	int i;
	struct es_dsp_resource *res;

	for (i = 0; i < MAX_DSP_NUM; i++) {
		res = (struct es_dsp_resource *)dev_get_platdata(
			&es_dsp_hw_pdev[i]->dev);
		if (es_dsp_hw_pdev[i]) {
			platform_device_del(es_dsp_hw_pdev[i]);
			es_dsp_hw_pdev[i] = NULL;
		}
		kfree((void *)res->firmware_name);
	}
	dsp_info("%s, ok.\n", __func__);
	return 0;
}
