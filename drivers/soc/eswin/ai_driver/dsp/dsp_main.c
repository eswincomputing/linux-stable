// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/version.h>
#include <linux/atomic.h>
#include <linux/acpi.h>
#include <linux/completion.h>
#include <linux/delay.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 16, 0)
#include <linux/dma-mapping.h>
#else
#include <linux/dma-direct.h>
#endif
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)
#include <linux/of_reserved_mem.h>
#endif
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sort.h>
#include <asm/mman.h>
#include <asm/uaccess.h>
#include <asm/current.h>
#include <linux/dmapool.h>
#include <linux/debugfs.h>
#include <uapi/asm-generic/siginfo.h>
#include <linux/mailbox/eswin-mailbox.h>
#include <linux/dma-mapping.h>
#include "eswin-khandle.h"

#include "dsp_main.h"
#include "dsp_hw.h"
#include "dsp_ioctl.h"
#include "dsp_platform.h"
#include "dsp_proc.h"

#define MAX_NUM_PER_DIE 4
#define DRIVER_NAME "eswin-dsp"
#define DSP_SUBSYS_HILOAD_CLK 1040000000
#define DSP_SUBSYS_LOWLOAD_CLK 5200000

#define ES_DSP_DEFAULT_TIMEOUT (100 * 6)

#ifdef DEBUG
#pragma GCC optimize("O0")
#endif

int dsp_boot_firmware(struct es_dsp *dsp);

static int fw_timeout = ES_DSP_DEFAULT_TIMEOUT;
module_param(fw_timeout, int, 0644);
MODULE_PARM_DESC(fw_timeout, "Firmware command timeout in seconds.");

enum {
	LOOPBACK_NORMAL, /* normal work mode */
	LOOPBACK_NOIO, /* don't communicate with FW, but still load it and control DSP */
	LOOPBACK_NOMMIO, /* don't comminicate with FW or use DSP MMIO, but still load the FW */
	LOOPBACK_NOFIRMWARE, /* don't communicate with FW or use DSP MMIO, don't load the FW */
};
static int loopback = LOOPBACK_NORMAL;
module_param(loopback, int, 0644);
MODULE_PARM_DESC(loopback, "Don't use actual DSP, perform everything locally.");

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
#define devm_kmalloc devm_kzalloc
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 15, 0)
#define devm_kstrdup dsp_devm_kstrdup
static char *dsp_devm_kstrdup(struct device *dev, const char *s, gfp_t gfp)
{
	size_t size;
	char *buf;

	if (!s)
		return NULL;

	size = strlen(s) + 1;
	buf = devm_kmalloc(dev, size, gfp);
	if (buf)
		memcpy(buf, s, size);
	return buf;
}
#endif

int es_dsp_exec_cmd_timeout(void)
{
	return fw_timeout;
}

void __dsp_enqueue_task(struct es_dsp *dsp, dsp_request_t *req)
{
	struct prio_array *array = &dsp->array;
	unsigned long flags;

	list_add_tail(&req->pending_list, &array->queue[req->prio]);
	set_bit(req->prio, array->bitmap);
	dsp->wait_running++;
	array->queue_task_num[req->prio]++;
}

static void __dsp_dequeue_task(struct es_dsp *dsp)
{
	struct prio_array *array = &dsp->array;
	int idx;
	struct list_head *queue;
	dsp_request_t *req;

	BUG_ON(dsp->current_task != NULL);
	idx = sched_find_first_bit(array->bitmap);
	if (idx >= DSP_MAX_PRIO) {
		return;
	}
	queue = &array->queue[idx];
	req = list_entry(queue->next, dsp_request_t, pending_list);
	dsp->wait_running--;
	if (--array->queue_task_num[idx] == 0) {
		clear_bit(idx, array->bitmap);
	}
	list_del(&req->pending_list);
	dsp->current_task = req;
	/* Reset retry count. */
	dsp->task_reboot_cnt = 0;
}

static void __dsp_send_task(struct es_dsp *dsp)
{
	struct dsp_op_desc *opdesc;
	dsp_request_t *req;

	dsp_debug("%s.\n", __func__);

	req = dsp->current_task;
	BUG_ON(req == NULL);
	opdesc = (struct dsp_op_desc *)req->handle;
	strcpy(dsp->stats->last_op_name, opdesc->name);
	dsp->send_time = ktime_get_real_ns();
	dsp->stats->send_to_dsp_cnt++;

	if ((dsp_perf_enable || dsp->perf_enable) &&
	    dsp->op_idx < MAX_DSP_TASKS) {
		strcpy(dsp->op_perf[dsp->op_idx].OpName, opdesc->name);
	}

	if (req->allow_eval) {
		dsp->task_timer.expires = jiffies + fw_timeout * HZ;
		BUG_ON(timer_pending(&dsp->task_timer));
		add_timer(&dsp->task_timer);
	}

	if ((dsp_perf_enable || dsp->perf_enable) &&
	    dsp->op_idx < MAX_DSP_TASKS) {
		dsp->op_perf[dsp->op_idx].OpSendTaskCycle =
			0;  //get_perf_timer_cnt();
		dsp_debug("op_idx=%u OpSendTaskCycle=%u\n", dsp->op_idx,
			  dsp->op_perf[dsp->op_idx].OpSendTaskCycle);
	}
	es_dsp_send_irq(dsp->hw_arg, (void *)req);
}

void dsp_schedule_task(struct es_dsp *dsp)
{
	unsigned long flags;
	spin_lock_irqsave(&dsp->send_lock, flags);
	if (dsp->current_task == NULL) {
		__dsp_dequeue_task(dsp);
		if (dsp->current_task != NULL) {
			__dsp_send_task(dsp);
		}
	}
	spin_unlock_irqrestore(&dsp->send_lock, flags);
}

void dsp_complete_work(struct es_dsp *dsp, dsp_request_t *req)
{
	unsigned long flags;

	if (req->d2h_msg.status != 0) {
		dsp->stats->total_failed_cnt++;
	} else {
		dsp->stats->total_ok_cnt++;
	}
	if (req->cpl_handler != NULL) {
		req->cpl_handler(dsp->dev, req);
	}

	if ((dsp_perf_enable || dsp->perf_enable) &&
	    dsp->op_idx < MAX_DSP_TASKS) {
		dsp->op_perf[dsp->op_idx].OpEndCycle =
			0;  //get_perf_timer_cnt();
	}
}

static void es_dsp_drop_pending_task(struct es_dsp *dsp)
{
	unsigned long flags;

	spin_lock_irqsave(&dsp->send_lock, flags);
	while (true) {
		dsp_request_t *req;
		if (dsp->current_task == NULL) {
			spin_unlock_irqrestore(&dsp->send_lock, flags);
			break;
		}
		req = dsp->current_task;
		spin_unlock_irqrestore(&dsp->send_lock, flags);
		req->d2h_msg.status = -EIO;
		req->d2h_msg.return_value = -ENXIO;
		dsp_complete_work(dsp, req);
		spin_lock_irqsave(&dsp->send_lock, flags);
		dsp->current_task = NULL;
		__dsp_dequeue_task(dsp);
	}
}

static void dsp_process_expire_work(struct work_struct *work)
{
	struct es_dsp *dsp = container_of(work, struct es_dsp, expire_work);
	dsp_request_t *req;
	unsigned long flags;
	int ret;
	struct dsp_fw_state_t *dsp_fw_state =
		(struct dsp_fw_state_t *)dsp->dsp_fw_state_base;

	dsp_err("%s, %d, task timeout, dsp fw state=0x%x, excause=0x%x, ps=0x%x, pc=0x%x, dsp_task=0x%x"
		"npu_task=0x%x, func_state=0x%x\n",
		__func__, __LINE__, dsp_fw_state->fw_state,
		dsp_fw_state->exccause, dsp_fw_state->ps, dsp_fw_state->pc,
		dsp_fw_state->dsp_task_state, dsp_fw_state->npu_task_state,
		dsp_fw_state->func_state);

	if (dsp->stats->last_op_name) {
		dsp_err("%s, %d, op name = %s.\n", __func__, __LINE__,
			dsp->stats->last_op_name);
	}
	ret = es_dsp_reboot_core(dsp->hw_arg);
	if (ret < 0) {
		dsp_err("reboot dsp core failed.\n");
		atomic_set(&dsp->reboot_cycle, 0);
		dsp->off = true;
	} else {
		ret = dsp_boot_firmware(dsp);
		if (ret < 0) {
			dsp_err("restarting load firmware failed.\n");
			atomic_set(&dsp->reboot_cycle, 0);
			dsp->off = true;
		}
	}

	if (dsp->off) {
		es_dsp_drop_pending_task(dsp);
		return;
	}

	spin_lock_irqsave(&dsp->send_lock, flags);
	req = dsp->current_task;
	BUG_ON(req == NULL);
	if (dsp->task_reboot_cnt == 3) {
		dsp->current_task = NULL;
		spin_unlock_irqrestore(&dsp->send_lock, flags);
		req->d2h_msg.status = -EIO;
		req->d2h_msg.return_value = -ENXIO;
		dsp_err("dsp task timeout 3 times, so cannot do dsp this request work.\n");
		dsp_complete_work(dsp, req);
		dsp_schedule_task(dsp);
	} else {
		dsp->task_reboot_cnt++;
		__dsp_send_task(dsp);
		spin_unlock_irqrestore(&dsp->send_lock, flags);
	}
}

static void dsp_task_timer(struct timer_list *timer)
{
	struct es_dsp *dsp = container_of(timer, struct es_dsp, task_timer);

	dsp->stats->task_timeout_cnt++;
	schedule_work(&dsp->expire_work);
}

irqreturn_t dsp_irq_handler(void *msg_data, struct es_dsp *dsp)
{
	dsp_request_t *req;
	es_dsp_d2h_msg *msg = (es_dsp_d2h_msg *)msg_data;
	unsigned long flags;
	struct dsp_file *dsp_file = NULL;
	int ret = 1;

	dsp->stats->total_int_cnt++;
	if (msg->status == 0 && msg->return_value == DSP_CMD_READY) {
		dsp->off = false;
		wake_up_interruptible_nr(&dsp->hd_ready_wait, 1);
		dsp_info("%s, this is hardware sync.\n", __func__);
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&dsp->send_lock, flags);
	req = dsp->current_task;
	if (req == NULL) {
		spin_unlock_irqrestore(&dsp->send_lock, flags);
		dsp_err("error, this irq have no task request.\n");
		return IRQ_NONE;
	}

	if (req->allow_eval) {
		ret = del_timer(&dsp->task_timer);
		if (!ret) {
			dsp_err("%s, %d, task is now processing in timer.\n",
				__func__, __LINE__);
			spin_unlock_irqrestore(&dsp->send_lock, flags);
			return IRQ_NONE;
		}
	}

	spin_unlock_irqrestore(&dsp->send_lock, flags);
	BUG_ON(timer_pending(&dsp->task_timer));

	dsp->done_time = ktime_get_real_ns();
	dsp->stats->last_task_time = dsp->done_time - dsp->send_time;

	req->d2h_msg = *msg;
	dsp_complete_work(dsp, req);
	dsp->current_task = NULL;

	dsp_debug("%s, current task req = 0x%px.\n", __func__, req);
	dsp_info("op name:%s take time:%lld\n", dsp->stats->last_op_name,
		 dsp->stats->last_task_time);
	if (dsp->off == false) {
		dsp_schedule_task(dsp);
	}
	return IRQ_HANDLED;
}
EXPORT_SYMBOL(dsp_irq_handler);

static struct dsp_op_desc *load_oper_to_mem(struct es_dsp *dsp, char *op_dir,
					    char *op_name)
{
	struct dsp_op_desc *op;
	int len;
	int ret;

	len = strlen(op_name) + 1;
	op = kzalloc(sizeof(struct dsp_op_desc) + len, GFP_KERNEL);
	if (!op) {
		dsp_err("error alloc operator mem.\n");
		return NULL;
	}
	op->name = (char *)(op + 1);
	strcpy(op->name, op_name);

	op->dsp = dsp;
	op->op_dir = op_dir;

	ret = es_dsp_load_op(dsp->hw_arg, (void *)op);
	if (ret) {
		dsp_err("error op firmware.\n");
		goto err_load_firm;
	}
	dsp_send_invalid_code_seg(dsp->hw_arg, op);
	kref_init(&op->refcount);
	list_add(&op->entry, &dsp->all_op_list);
	dsp_debug("%s, done.\n", __func__);
	return op;
err_load_firm:
	kfree((void *)op);
	return NULL;
}

static struct dsp_op_desc *check_op_list(struct es_dsp *dsp, char *op_name)
{
	struct dsp_op_desc *op;

	list_for_each_entry(op, &dsp->all_op_list, entry) {
		if (strcmp(op->name, op_name) == 0) {
			dsp_debug("op %s have loaded into memory.\n", op_name);
			return op;
		}
	}
	return NULL;
}

static struct dsp_op_desc *dsp_get_op(struct es_dsp *dsp, char *op_dir,
				      char *op_name)
{
	struct dsp_op_desc *op;

	mutex_lock(&dsp->op_list_mutex);
	op = check_op_list(dsp, op_name);
	if (op) {
		kref_get(&op->refcount);
	} else {
		dsp_debug("op %s don't have load into mem.\n", op_name);
		op = load_oper_to_mem(dsp, op_dir, op_name);
		if (!op) {
			dsp_err("load operator %s, failed.\n", op_name);
		}
	}
	mutex_unlock(&dsp->op_list_mutex);
	return op;
}

int load_operator(struct device *dsp_dev, char *op_dir, char *op_name,
		  u64 *handle)
{
	struct dsp_op_desc *op;
	struct es_dsp *dsp = dev_get_drvdata(dsp_dev);

	if (!dsp) {
		dsp_err("load op, dsp device cannot find.\n");
		return -EIO;
	}

	op = dsp_get_op(dsp, op_dir, op_name);
	if (op == NULL) {
		return -EINVAL;
	}
	if (handle) {
		*handle = (u64)op;
	}
	return 0;
}
EXPORT_SYMBOL(load_operator);

void dsp_op_release(struct kref *kref)
{
	struct dsp_op_desc *op =
		container_of(kref, struct dsp_op_desc, refcount);
	struct es_dsp *dsp;
	if (!op) {
		dsp_err("%s, op is null, error.\n", __func__);
		return;
	}
	dsp = op->dsp;

	dsp_debug("%s, opname=%s, refcount=%d.\n", __func__, op->name,
		  kref_read(kref));

	list_del(&op->entry);
	if (op->op_shared_seg_ptr) {
		dma_free_coherent(dsp->dev, op->op_shared_seg_size,
				  op->op_shared_seg_ptr, op->iova_base);
		op->op_shared_seg_ptr = NULL;
	}

	kfree((void *)op);
	dsp_debug("%s, free mem ok.\n", __func__);
	return;
}

int unload_operator(struct device *dsp_dev, u64 handle)
{
	struct es_dsp *dsp = dev_get_drvdata(dsp_dev);
	struct dsp_op_desc *op;

	mutex_lock(&dsp->op_list_mutex);
	op = (struct dsp_op_desc *)handle;
	kref_put(&op->refcount, dsp_op_release);
	mutex_unlock(&dsp->op_list_mutex);
	return 0;
}
EXPORT_SYMBOL(unload_operator);

static void dsp_task_work(struct work_struct *work)
{
	struct es_dsp *dsp = container_of(work, struct es_dsp, task_work);
	unsigned long flags;

	while (true) {
		dsp_request_t *req;
		struct dsp_user_req_async *user_req;
		spin_lock_irqsave(&dsp->complete_lock, flags);
		if (list_empty(&dsp->complete_list)) {
			break;
		}
		req = list_first_entry(&dsp->complete_list, dsp_request_t,
				       cpl_list);
		list_del(&req->cpl_list);
		spin_unlock_irqrestore(&dsp->complete_lock, flags);
		user_req =
			container_of(req, struct dsp_user_req_async, dsp_req);
		if (user_req->req_cpl_handler) {
			user_req->req_cpl_handler(dsp->dev, req);
		}
	}
	spin_unlock_irqrestore(&dsp->complete_lock, flags);
}

/* 1. 如果任务已经执行过了prepare，那么就给dsp发送一个消息，去执行eval;
 * 2. 如果任务没有执行，那么就发送消息，让dsp core去执行prepare和eval，中间prepare执行后，不用等待。
 */

int start_eval(struct device *dsp_dev, dsp_request_t *req)
{
	return 0;
}
EXPORT_SYMBOL(start_eval);

void dsp_set_flat_func(struct es_dsp_flat1_desc *flat, u64 handle)
{
	struct dsp_op_desc *op;

	op = (struct dsp_op_desc *)handle;
	memcpy((void *)&flat->funcs, (void *)&op->funcs, sizeof(op->funcs));
}
EXPORT_SYMBOL(dsp_set_flat_func);
/* 把任务提交到dsp的任务队列上排队;
 * 1. 如果没有任务在运行，那么就运行该任务的prepare，并且告诉dsp core，需要在prepare进行等待，但是prepare不需要发送通知。
 * 2. 如果有任务在运行，那么就是挂接。
 */

int submit_task(struct device *dsp_dev, dsp_request_t *req)
{
	struct es_dsp *dsp = dev_get_drvdata(dsp_dev);
	struct dsp_op_desc *op;
	unsigned long flags;

	if (req->prio >= DSP_MAX_PRIO) {
		dsp_err("%s, dsp request prio = %d great max prio %d, error.\n",
			__func__, req->prio, DSP_MAX_PRIO);
		return -EINVAL;
	}

	op = (struct dsp_op_desc *)req->handle;
	if (!op) {
		dsp_err("%s, handle=0x%llx is invalid.\n", __func__,
			req->handle);
		return -EINVAL;
	}
	dsp_set_flat_func(req->flat_virt, req->handle);
	spin_lock_irqsave(&dsp->send_lock, flags);
	if (dsp->off) {
		spin_unlock_irqrestore(&dsp->send_lock, flags);
		dsp_err("es dsp off.\n");
		return -ENODEV;
	}
	__dsp_enqueue_task(dsp, req);
	spin_unlock_irqrestore(&dsp->send_lock, flags);

	dsp_schedule_task(dsp);
	dsp_debug("%s, done.\n", __func__);
	return 0;
}
EXPORT_SYMBOL(submit_task);

static struct es_dsp *g_es_dsp[2][4];

struct es_dsp *es_proc_get_dsp(int dieid, int dspid)
{
	if (dspid >= 4 || dieid >= 2) {
		return NULL;
	}

	return g_es_dsp[dieid][dspid];
}

/*
 * input: die_id, dspId, subscrib.
 * output: dsp_dev.
 * 注意: 很可能是, npu调用这个接口的时候, 我们的dsp驱动的probe还没有调用.这是可能的.
 * 所以需要返回EPROBE_DEFED.
 * */

static int check_device_node_status(u32 die_id, u32 dspid)
{
	int ret;
	struct device_node *node;
	u32 numa_id, pro_id;

	for_each_compatible_node(node, NULL, "eswin-dsp") {
		if (of_property_read_u32(node, "numa-node-id", &numa_id)) {
			dsp_err("%s, failed to get 'numa_id' property.\n",
				__func__);
			return -ENODEV;
		}
		if (numa_id != die_id) {
			continue;
		}

		if (of_property_read_u32(node, "process-id", &pro_id)) {
			dsp_err("%s, failed to get 'process-id' property\n",
				__func__);
			return -ENODEV;
		}

		if (pro_id != dspid) {
			continue;
		}

		if (of_device_is_available(node) == false) {
			printk("die%d, dsp core %d status is disabled.\n",
			       die_id, dspid);
			return -ENODEV;
		}

		return 0;
	}

	return -EIO;
}

int subscribe_dsp_device(u32 die_id, u32 dspId, struct device *subscrib,
			 struct device **dsp_dev)
{
	struct es_dsp *dsp;
	int ret;

	if (dspId >= 4 || die_id >= 2) {
		return -EINVAL;
	}
	if (!subscrib || !dsp_dev) {
		return -EINVAL;
	}

	ret = check_device_node_status(die_id, dspId);
	if (ret) {
		return -ENODEV;
	}

	if (g_es_dsp[die_id][dspId] == NULL) {
		dsp_err("%s, dsp die %d, dsp_core %d have not register.\n",
			__func__, die_id, dspId);
		return -EPROBE_DEFER;
	}
	dsp = g_es_dsp[die_id][dspId];

	*dsp_dev = dsp->dev;
	return 0;
}
EXPORT_SYMBOL(subscribe_dsp_device);

int unsubscribe_dsp_device(struct device *subscrib, struct device *dsp_dev)
{
	return 0;
}
EXPORT_SYMBOL(unsubscribe_dsp_device);

static inline void dsp_reset(struct es_dsp *dsp)
{
	es_dsp_reset(dsp->hw_arg);
}

static inline void dsp_halt(struct es_dsp *dsp)
{
	es_dsp_halt(dsp->hw_arg);
}

static inline void dsp_release(struct es_dsp *dsp)
{
	es_dsp_release(dsp->hw_arg);
}

static inline int dsp_set_rate(struct es_dsp *dsp, unsigned long rate)
{
	return es_dsp_set_rate(dsp->hw_arg, rate);
}

static int dsp_synchronize(struct es_dsp *dsp)
{
	return es_dsp_sync(dsp);
}

int dsp_boot_firmware(struct es_dsp *dsp)
{
	int ret;

	dsp_halt(dsp);
	dsp_reset(dsp);
	reset_uart_mutex((struct es_dsp_hw *)dsp->hw_arg);
	dsp->off = true;

	if (dsp->firmware_name) {
		if (loopback < LOOPBACK_NOFIRMWARE) {
			ret = dsp_request_firmware(dsp);
			if (ret < 0)
				return ret;
		}
	}
	dsp_release(dsp);
	if (loopback < LOOPBACK_NOIO) {
		ret = dsp_synchronize(dsp);
		if (ret < 0) {
			dsp_halt(dsp);
			dev_err(dsp->dev,
				"%s: couldn't synchronize with the DSP core\n",
				__func__);
			dsp_err("es dsp device will not use the DSP until the driver is rebound to this device\n");
			dsp->off = true;
			return ret;
		}
	}
	return 0;
}

int __maybe_unused dsp_suspend(struct device *dev)
{
	struct es_dsp *dsp = dev_get_drvdata(dev);
	int ret;
	dev_dbg(dsp->dev, "dsp generic suspend...\n");

	ret = es_dsp_pm_get_sync(dsp);
	if (ret < 0) {
		return ret;
	}
	dsp->off = true;

	if (dsp->current_task != NULL) {
		ret = wait_for_current_tsk_done(dsp);
		if (ret) {
			dsp_err("%s, %d, cannot wait for current task done, ret = %d.\n", __func__, __LINE__, ret);
			dsp->off = false;
			return ret;
		}
	}

	flush_work(&dsp->task_work);

	dsp_disable_irq(dsp);
	es_dsp_hw_uninit(dsp);

	dsp_release_firmware(dsp);
	dsp_halt(dsp);

	pm_runtime_mark_last_busy(dsp->dev);
	pm_runtime_put_noidle(dsp->dev);
	win2030_tbu_power(dsp->dev, false);
	es_dsp_clk_disable(dsp);
	dsp_disable_mbox_clock(dsp);
		dsp_debug("%s, %d, dsp core%d generic suspend done.\n", __func__,
		  __LINE__, dsp->process_id);
	return 0;
}

int __maybe_unused dsp_resume(struct device *dev)
{
	struct es_dsp *dsp = dev_get_drvdata(dev);
	int ret;

	dsp_debug("%s, dsp core%d generic resuming..\n\n", __func__,
		  dsp->process_id);

	ret = dsp_enable_mbox_clock(dsp);
	if (ret) {
		dsp_err("dsp resume mbox clock err.\n");
		return ret;
	}
	ret = es_dsp_clk_enable(dsp);
	if (ret < 0) {
		dev_err(dsp->dev, "couldn't enable DSP\n");
		goto out;
	}

	pm_runtime_get_noresume(dsp->dev);

	ret = win2030_tbu_power(dsp->dev, true);
	if (ret) {
		dsp_err("%s, %d, tbu power failed.\n", __func__, __LINE__, ret);
		goto err_tbu_power;
	}
	dsp_enable_irq(dsp);
	ret = es_dsp_hw_init(dsp);
	if (ret)
		goto err_hw_init;
	ret = dsp_boot_firmware(dsp);
	if (ret < 0) {
		dsp_err("load firmware failed, ret=%d.\n", ret);
		goto err_firm;
	}

	pm_runtime_mark_last_busy(dsp->dev);
	pm_runtime_put_autosuspend(dsp->dev);
	dsp_debug("dsp_core%d Generic resume ok, dsp->off=%d.\n",
		  dsp->process_id, dsp->off);
	dsp->off = false;
	dsp_schedule_task(dsp);
	return 0;
err_firm:
	es_dsp_hw_uninit(dsp);
err_hw_init:
	win2030_tbu_power(dsp->dev, false);
err_tbu_power:
	es_dsp_core_clk_disable(dsp);
out:
	dsp_disable_mbox_clock(dsp);
	return ret;
}

int __maybe_unused dsp_runtime_suspend(struct device *dev)
{
	struct es_dsp *dsp = dev_get_drvdata(dev);
	dsp_debug("%s, dsp core%d runtime suspend.\n", __func__,
		  dsp->process_id);

	win2030_tbu_power(dev, false);
	es_dsp_clk_disable(dsp);
	return 0;
}
EXPORT_SYMBOL(dsp_runtime_suspend);

int __maybe_unused dsp_runtime_resume(struct device *dev)
{
	struct es_dsp *dsp = dev_get_drvdata(dev);
	int ret = 0;

	if (dsp->off)
		goto out;

	dsp_debug("%s, dsp core%d runtime resumng.....\n\n", __func__,
		  dsp->process_id);

	ret = es_dsp_clk_enable(dsp);
	if (ret < 0) {
		dev_err(dsp->dev, "couldn't enable DSP\n");
		goto out;
	}
	win2030_tbu_power(dev, true);
	dsp_debug("dsp core%d, runtime resume ok.\n", dsp->process_id);
out:
	return ret;
}
EXPORT_SYMBOL(dsp_runtime_resume);

/**
 * Called when opening rate file
 * @param inode
 * @param file
 * @return
 */
static int dsp_rate_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

/**
 * Called when reading rate file
 */
static ssize_t dsp_rate_read(struct file *flip, char __user *ubuf, size_t cnt,
			     loff_t *ppos)
{
#define RUN_STR_SIZE 11
	struct es_dsp *dsp = flip->private_data;
	char buf[RUN_STR_SIZE];
	int r;

	r = snprintf(buf, RUN_STR_SIZE, "%ld\n", dsp->rate);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, r);
}
/**
 * Called when writing rate file
 */
static ssize_t dsp_rate_write(struct file *flip, const char __user *ubuf,
			      size_t cnt, loff_t *ppos)
{
#define SIZE_SMALL_BUF 256
	struct es_dsp *dsp = flip->private_data;
	char buf[SIZE_SMALL_BUF] = { 0 };
	unsigned ret;

	if (cnt > SIZE_SMALL_BUF)
		cnt = SIZE_SMALL_BUF - 1;

	if (copy_from_user(&buf, ubuf, cnt))
		return -EFAULT;

	if (0 == strncmp(buf, "5200000", strlen("5200000"))) {
		dsp->rate = DSP_SUBSYS_LOWLOAD_CLK; /* FIXME spinlock? */
	} else if (0 == strncmp(buf, "1040000000", strlen("1040000000"))) {
		dsp->rate = DSP_SUBSYS_HILOAD_CLK;
	} else {
		dev_err(dsp->dev, "invalid rate para %s\n", buf);
		return -EFAULT;
	}
	ret = dsp_set_rate(dsp, dsp->rate);
	if (0 != ret) {
		dev_err(dsp->dev, "failed to set rate to %ldHZ", dsp->rate);
	} else {
		dev_info(dsp->dev, "set rate to %ldHZ", dsp->rate);
	}
	*ppos += cnt;

	return cnt;
}

static const struct file_operations dsp_rate_fops = {
	.open = dsp_rate_open,
	.read = dsp_rate_read,
	.write = dsp_rate_write,
	.llseek = generic_file_llseek,
};
static int dsp_debug_init(struct es_dsp *dsp)
{
	struct dentry *dir, *d;
	char name[32];

	scnprintf(name, ARRAY_SIZE(name), "dsp_%d", dsp->nodeid);

	dir = debugfs_create_dir(name, NULL);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	d = debugfs_create_file("rate", S_IRUGO | S_IWUSR, dir, dsp,
				&dsp_rate_fops);
	if (IS_ERR(d))
		return PTR_ERR(d);

	dsp->debug_dentry = dir;
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id es_dsp_hw_match[] = {
	{
		.compatible = "eswin-dsp",
	},
	{},
};
MODULE_DEVICE_TABLE(of, es_dsp_hw_match);
#endif

static void dsp_init_prio_array(struct es_dsp *dsp)
{
	struct prio_array *array = &dsp->array;
	int i;

	dsp->current_task = NULL;
	for (i = 0; i < DSP_MAX_PRIO; i++) {
		INIT_LIST_HEAD(&array->queue[i]);
		clear_bit(i, array->bitmap);
		array->queue_task_num[i] = 0;
	}
	set_bit(DSP_MAX_PRIO, array->bitmap);
}

static int es_dsp_hw_probe(struct platform_device *pdev)
{
	int ret;
	char nodename[sizeof("es-dsp") + 3 * sizeof(int)];
	struct es_dsp *dsp;

	dsp = devm_kzalloc(&pdev->dev,
			   sizeof(*dsp) + sizeof(struct es_dsp_stats) +
				   OPERATOR_NAME_MAXLEN,
			   GFP_KERNEL);
	if (!dsp) {
		ret = -ENOMEM;
		return ret;
	}
	dsp->stats = (struct es_dsp_stats *)(dsp + 1);
	dsp->stats->last_op_name = (char *)((void *)dsp + sizeof(*dsp) +
					    sizeof(struct es_dsp_stats));
	dsp->dev = &pdev->dev;
	dsp->rate = DSP_SUBSYS_HILOAD_CLK;
	mutex_init(&dsp->lock);

	ret = dsp_alloc_hw(pdev, dsp);
	if (ret) {
		return ret;
	}
	platform_set_drvdata(pdev, dsp);
	ret = es_dsp_get_subsys(pdev, dsp);
	if (ret) {
		dsp_err("%s, %d, get subsys err, ret=%d.\n", __func__, __LINE__,
			ret);
		dsp_free_hw(dsp);
		return -ENXIO;
	}
	ret = dsp_get_resource(pdev, dsp);
	if (ret) {
		dsp_err("dsp clock init error.\n");
		goto err_clk_init;
	}

	/* setting for DMA check */
	ret = dma_set_mask(dsp->dev, DMA_BIT_MASK(32));
	if (ret) {
		WARN_ON_ONCE(!dsp->dev->dma_mask);
		goto err_dev;
	}

	ret = es_dsp_map_resource(dsp);
	if (ret < 0) {
		dsp_err("%s, %d, dsp map resource err, ret=%d.\n", __func__,
			__LINE__, ret);
		goto err_map_res;
	}
	init_waitqueue_head(&dsp->hd_ready_wait);
	INIT_WORK(&dsp->task_work, dsp_task_work);
	timer_setup(&dsp->task_timer, dsp_task_timer, 0);
	dsp->task_timer.expires = 0;
	spin_lock_init(&dsp->send_lock);
	dsp_init_prio_array(dsp);

	INIT_LIST_HEAD(&dsp->complete_list);
	INIT_LIST_HEAD(&dsp->all_op_list);
	mutex_init(&dsp->op_list_mutex);
	INIT_WORK(&dsp->expire_work, dsp_process_expire_work);
	spin_lock_init(&dsp->complete_lock);

	ret = dsp_enable_mbox_clock(dsp);
	if (ret < 0) {
		dsp_err("%s, %d, enable mbox clock err, ret = %d.\n", __func__,
			__LINE__, ret);
		goto err_mbox_clk;
	}
	ret = es_dsp_clk_enable(dsp);
	if (ret) {
		dsp_err("%s, %d, clock enbale error.\n", __func__, __LINE__,
			ret);
		goto err_dsp_clk;
	}
	ret = win2030_tbu_power(dsp->dev, true);
	if (ret) {
		dsp_err("%s, %d, tbu power failed.\n", __func__, __LINE__, ret);
		goto err_tbu_power;
	}

	ret = es_dsp_hw_init(dsp);
	if (ret)
		goto err_hw_init;

	pm_runtime_set_autosuspend_delay(dsp->dev, 5000);
	pm_runtime_use_autosuspend(dsp->dev);
	pm_runtime_set_active(dsp->dev);
	pm_runtime_enable(dsp->dev);
	pm_runtime_get_noresume(dsp->dev);
	ret = dsp_boot_firmware(dsp);
	if (ret < 0) {
		dsp_err("load firmware failed, ret=%d.\n", ret);
		goto err_firm;
	}

	dsp->nodeid = dsp->process_id + dsp->numa_id * MAX_NUM_PER_DIE;
	sprintf(nodename, "es-dsp%u", dsp->nodeid);

	dsp->miscdev = (struct miscdevice){
		.minor = MISC_DYNAMIC_MINOR,
		.name = devm_kstrdup(&pdev->dev, nodename, GFP_KERNEL),
		.nodename = devm_kstrdup(&pdev->dev, nodename, GFP_KERNEL),
		.fops = &dsp_fops,
	};

	ret = misc_register(&dsp->miscdev);
	if (ret < 0)
		goto err_pm_disable;

	g_es_dsp[dsp->numa_id][dsp->process_id] = dsp;

	dsp_debug_init(dsp);
	pm_runtime_mark_last_busy(dsp->dev);
	pm_runtime_put_autosuspend(dsp->dev);

	dsp_info("%s, probe successful.\n", __func__);
	return 0;

err_pm_disable:
err_firm:
	pm_runtime_put_noidle(dsp->dev);
	pm_runtime_disable(dsp->dev);
	pm_runtime_set_suspended(dsp->dev);
	pm_runtime_dont_use_autosuspend(dsp->dev);
	es_dsp_hw_uninit(dsp);
err_hw_init:
	win2030_tbu_power(dsp->dev, false);
err_tbu_power:
	es_dsp_clk_disable(dsp);
err_dsp_clk:
	dsp_disable_mbox_clock(dsp);
err_mbox_clk:
	es_dsp_unmap_resource(dsp);
err_map_res:
	dsp_put_resource(dsp);
err_dev:
err_mbx:
err_clk_init:
	es_dsp_put_subsys(dsp);
	dsp_free_hw(dsp);
	dev_err(&pdev->dev, "%s: ret = %d\n", __func__, ret);
	return ret;
}

static int es_dsp_hw_remove(struct platform_device *pdev)
{
	struct es_dsp *dsp = platform_get_drvdata(pdev);
	int ret;

	if (!dsp)
		return 0;
	dsp->off = true;
	debugfs_remove_recursive(dsp->debug_dentry);

	g_es_dsp[dsp->numa_id][dsp->process_id] = NULL;

	if (NULL != dsp->miscdev.this_device) {
		misc_deregister(&dsp->miscdev);
	}

	cancel_work_sync(&dsp->task_work);
	es_dsp_hw_uninit(dsp);

	pm_runtime_disable(dsp->dev);
	pm_runtime_set_suspended(dsp->dev);
	pm_runtime_dont_use_autosuspend(dsp->dev);
	dsp_release_firmware(dsp);

	dsp_halt(dsp);

	win2030_tbu_power(dsp->dev, false);

	es_dsp_clk_disable(dsp);
	dsp_disable_mbox_clock(dsp);
	es_dsp_unmap_resource(dsp);
	dsp_put_resource(dsp);
	es_dsp_put_subsys(dsp);
	dsp_free_hw(dsp);
	return 0;
}

static const struct dev_pm_ops es_dsp_hw_pm_ops = { SYSTEM_SLEEP_PM_OPS(
	dsp_suspend, dsp_resume) SET_RUNTIME_PM_OPS(dsp_runtime_suspend,
						    dsp_runtime_resume, NULL) };

static struct platform_driver es_dsp_hw_driver = {
	.probe   = es_dsp_hw_probe,
	.remove  = es_dsp_hw_remove,
	.driver  = {
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(es_dsp_hw_match),
		.pm = pm_ptr(&es_dsp_hw_pm_ops),
	},
};

static int __init es_dsp_module_init(void)
{
	int ret;

	ret = platform_driver_register(&es_dsp_hw_driver);
	if (ret) {
		dsp_err("cannot register platform drv\n");
		return ret;
	}

	ret = es_dsp_platform_init();
	if (ret) {
		dsp_err("es dsp platform init error.\n");
		platform_driver_unregister(&es_dsp_hw_driver);
		return ret;
	}

	es_dsp_init_proc();
	dsp_info("%s, ok.\n", __func__);
	return 0;
}

static void __exit es_dsp_module_exit(void)
{
	int ret;

	es_dsp_remove_proc();

	ret = es_dsp_platform_uninit();
	if (ret) {
		dsp_err("es dsp platform uninit error.\n");
		return;
	}
	platform_driver_unregister(&es_dsp_hw_driver);
}
module_init(es_dsp_module_init);
module_exit(es_dsp_module_exit);

MODULE_AUTHOR("Takayuki Sugawara");
MODULE_AUTHOR("Max Filippov");
MODULE_LICENSE("Dual MIT/GPL");
