// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __DSP_MAIN_H__
#define __DSP_MAIN_H__

#include <linux/completion.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/irqreturn.h>
#include <linux/dma-buf.h>
#include "eswin-khandle.h"

#include "es_dsp_internal.h"
#include "dsp_hw_if.h"
#include "dsp_hw.h"
#include "dsp_log.h"
#include "dsp_perf.h"
#include "dsp_mailbox.h"

#if DSP_ENV_SIM
static inline ES_U32 get_perf_timer_cnt()
{
	return 0;
}
#else
#include "eswin_timer.h"
#endif

extern int dsp_perf_enable;

#define DSP_MAX_PRIO 20
#define BITMAP_SIZE \
	((((DSP_MAX_PRIO + 1 + 7) / 8) + sizeof(long) - 1) / sizeof(long))

struct device;
struct firmware;
struct dsp_hw_ops;
struct dsp_allocation_pool;

struct es_dsp;

enum {
	DSP_FILE_HANDLE_MAGIC = 1,
	DSP_USER_HANDLE_MAGIC,
	DSP_REQ_HANDLE_MAGIC,
	DSP_DMABUF_HANDLE_MAGIC,
};
/**
*	This represent a operator in kernel. Every operator have one struct dsp_op_desc.
*/
struct dsp_op_desc {
	char *name;
	char *op_dir;
	struct list_head entry;
	u32 op_shared_seg_size;
	phys_addr_t op_shared_seg_addr;
	void *op_shared_seg_ptr;
	struct kref refcount;
	struct es_dsp *dsp;

	struct operator_funcs funcs;
	dma_addr_t iova_base;
};

struct dsp_user {
	struct dsp_op_desc *op;
	struct dsp_file *dsp_file;
	struct khandle h;
};

struct dsp_user_req_async {
	struct es_dsp *es_dsp;
	dsp_request_t dsp_req;
	struct dsp_dma_buf **dma_entry;
	u32 dma_buf_count;
	struct dsp_user *user;
	struct khandle handle;
	cpl_handler_fn req_cpl_handler;
	/* async_ll, cbarg, callback and dsp_file Can Only use for Lowlevel interface*/
	struct list_head async_ll;
	struct dsp_file *dsp_file;
	bool need_notify;
	u64 cbarg;
	u64 callback;
};

struct prio_array {
	unsigned int nr_active;
	unsigned long bitmap[BITMAP_SIZE];
	struct list_head queue[DSP_MAX_PRIO];
	unsigned long queue_task_num[DSP_MAX_PRIO];
};

struct es_dsp_stats {
	char *last_op_name;
	int total_ok_cnt;
	int total_failed_cnt;
	int total_int_cnt;
	int send_to_dsp_cnt;
	int task_timeout_cnt;
	u64 last_task_time;
};

struct es_dsp {
	struct device *dev;
	const char *firmware_name;
	const struct firmware *firmware;
	struct miscdevice miscdev;
	void *hw_arg;
	u32 process_id;
	u32 numa_id;
	struct prio_array array;
	spinlock_t send_lock;
	int wait_running;
	dsp_request_t *current_task;
	int task_reboot_cnt;

	u64 sram_phy_addr;
	u32 sram_dma_addr;
	struct dma_buf_attachment *sram_attach;

	wait_queue_head_t hd_ready_wait;
	struct work_struct task_work;

	struct es_dsp_stats *stats;
	void __iomem *perf_reg_base;
	u32 __iomem *dsp_fw_state_base;
	void __iomem *flat_base;

	struct timer_list task_timer;
	struct work_struct expire_work;

	struct platform_device *mbox_pdev;
	struct clk *mbox_pclk;
	struct clk *mbox_pclk_device;
	struct reset_control *mbox_rst;
	struct reset_control *mbox_rst_device;
	void __iomem *mbox_rx_base;
	void __iomem *mbox_tx_base;
	int mbox_lock_bit;
	int mbox_irq_bit;
	int mbox_irq;
	spinlock_t mbox_lock;
	u32 device_uart;
	struct resource *mbox_tx_res;
	struct resource *mbox_rx_res;

	u64 send_time;
	u64 done_time;

	struct mutex op_list_mutex;
	struct list_head all_op_list;

	spinlock_t complete_lock;
	struct list_head complete_list;
	atomic_t reboot_cycle;

	bool off;
	int nodeid;
	void *firmware_addr;
	dma_addr_t firmware_dev_addr;
	void __iomem *uart_mutex_base;

	struct mutex lock;
	unsigned long rate;
	struct dentry *debug_dentry;

	u32 perf_enable;
	int op_idx;
	dsp_kmd_perf_t op_perf[MAX_DSP_TASKS];
	dsp_fw_perf_t op_fw_perf[MAX_DSP_TASKS];
};

#define DSP_FIRMWARE_IOVA 0xfe000000
#define DSP_FIRMWARE_IOVA_SIZE 0x400000

/* 0xFF9B_0000 -- 0xFFFB_0000 for SPAD */
#define DSP_IDDR_IOVA 0xff9b0000
#define DSP_IDDR_IOVA_SIZE 0x600000

#define DSP_DEVICE_AUX_E31_IOVA 0xfffb0000  // unused iova
#define DSP_DEVICE_AUX_E31_IOVA_SIZE 0x1000

#define DSP_PTS_IOVA 0xfffb1000
#define DSP_PTS_IOVA_SIZE 0x8000

#define DSP_DEVICE_U84_TO_MCU_MBX ESWIN_MAILBOX_DSP_TO_E31_REG_BASE
#define U84_TO_MCU_IOVA_SIZE 0x1000

#define DSP_DEVICE_IOVA 0xfffba000
#define DSP_DEVICE_IOVA_SIZE (0xfffe0000 - DSP_DEVICE_IOVA)

/*0xfffe_0000 - */
#define DSP_DEVICE_UART_IOVA 0xffff0000
#define DSP_DEVICE_UART_MUTEX_IOVA 0xffffa000
#define DSP_DEVICE_MBX_TX_IOVA ESWIN_MAILBOX_DSP_TO_U84_REG_BASE
#define DSP_DEVICE_MBX_RX_IOVA ESWIN_MAILBOX_U84_TO_DSP_REG_BASE
#define DSP_DEVICE_EACH_IOVA_SIZE 0x10000
#define DSP_DEVICE_UART_IOVA_SIZE 0xa000
#define DSP_DEVICE_UART_MUTEX_IOVA_SIZE 0x1000

#define UART_MUTEX_BASE_ADDR 0x51820000
#define UART_MUTEX_UNIT_OFFSET 4

irqreturn_t dsp_irq_handler(void *, struct es_dsp *dsp);
int dsp_runtime_suspend(struct device *dev);
int dsp_runtime_resume(struct device *dev);
void dsp_op_release(struct kref *kref);

int es_dsp_exec_cmd_timeout(void);
struct es_dsp *es_proc_get_dsp(int dieid, int dspid);
void __dsp_enqueue_task(struct es_dsp *dsp, dsp_request_t *req);
void dsp_schedule_task(struct es_dsp *dsp);
#endif
