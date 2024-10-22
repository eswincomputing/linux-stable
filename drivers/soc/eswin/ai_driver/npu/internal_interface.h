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

#ifndef __INTERNAL_INTERFACE__
#define __INTERNAL_INTERFACE__
#include <linux/version.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/irqreturn.h>
#include <nvdla_linux.h>
#include "eswin-khandle.h"
#include <linux/timer.h>
#include "hetero_ioctl.h"
#include "dla_engine.h"
#include "dla_log.h"
#include "edma.h"
#include "conv.h"
#include "hetero_common.h"
#include "hetero_ipc.h"
#include "dla_buffer.h"
#include <linux/workqueue.h>

struct host_frame_desc;

/**************** hardware_context.c (nvdla_core_callback.c) ****************/
void *npu_get_win_engine(void *nvdla_dev);
int npu_spram_get(struct nvdla_device *nvdla_dev);
int npu_spram_release(struct nvdla_device *nvdla_dev);
int32_t dla_data_get_fd(void *driver_context, void *task_data, void *handle,
			uint16_t index);

static inline void reset_uart_mutex(struct nvdla_device *nvdla_dev)
{
	if (nvdla_dev->uart_mutex_base) {
		io_write(nvdla_dev->uart_mutex_base + UART_MUTEX_UNIT_OFFSET,
			 0);
	} else {
		dev_err(&nvdla_dev->pdev->dev, "uart mutex addr is NULL\n");
	}
}

/**************** hardware_context.c (nvdla_core_callback.c) ****************/

/**************** user_context.c ****************/
#define MAX_EVENT_SINK_SAVE_NUM 64

#define NPU_RT_MUTX_IDLE 0x0
#define NPU_RT_MUTX_LOCKED 0x1
#define NPU_RT_MUTX_FRAME_DONE 0x2

typedef struct _event_desc {
	spinlock_t spinlock;
	u16 produce_idx;
	u16 consumer_idx;
	u16 len;
	s16 event_sinks[MAX_EVENT_SINK_SAVE_NUM];
} event_desc_t;

struct user_context {
	struct khandle handle;
	bool uctx_is_alive;
	spinlock_t model_lock;
	atomic_t lock_status;
	struct mutex dma_lock;
	struct xarray buf_xrray;
	event_desc_t event_desc;
	struct nvdla_device *ndev;
};

struct user_model {
	struct khandle handle;
	struct user_context *uctx;
	struct nvdla_task mem_handles;
	struct dla_task task;
	struct dla_network_desc *network;
	void *executor;
	void *engine;  //win_engine.
	void *nvdla_dev;
	struct dma_buf *dma_buf_address_list;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	struct iosys_map dma_buf_map_address_list;
#else
	struct dma_buf_map dma_buf_map_address_list;
#endif
	hetero_ipc_frame_t e31_frame_info;
	/* frame counter: Only commit == done could release model */
	s64 frame_commit_cnt;
	s64 frame_done_cnt;

	struct dla_buffer_object *model_bobj;
	modelShmDesc_t *model_shm;
};
struct win_engine;

int create_npu_dev(int node_id, struct nvdla_device *nvdla_dev);
void destory_npu_dev(int node_id);
int npu_hetero_cmd(struct nvdla_device *nvdla_dev, struct win_ioctl_args *args);
void hetero_send_frame_to_npu(u8 tiktok, struct host_frame_desc *f);
struct nvdla_device *get_nvdla_dev(int i);
void hetero_send_event_source_req(u8 tiktok, u16 op_index);

int send_mbx_msg_to_e31(struct win_engine *engine, msg_payload_t);
/**************** user_context.c ****************/

/**************** dfx.c ****************/

/**************** engine_context.c ****************/
struct processors_interface {
	/* Processors status */
	const char *name;
	uint8_t op_type;
	/**
	 * TODO: need ops: valid-model->validate operation
	 */
	int (*tensor_unfold)(struct win_executor *executor, int op_idx,
			     union dla_operation_container *operation_desc,
			     union dla_surface_container *surface_desc,
			     void *tensor, int idx);
	int (*prepare_prog_data)(struct win_executor *executor, int rdma,
				 int tensor_idx, u16 op_idx,
				 union dla_operation_container *op_desc,
				 union dla_surface_container *surface_desc);
	int (*rdma_check)(struct dla_processor_group *group,
			  union dla_operation_container *op,
			  union dla_surface_container *surface);
};

struct dump_file_work_t {
	int tiktok;
	struct host_frame_desc *f;
	struct work_struct work;
};

struct dump_op_work_t {
	int tiktok;
	u16 op_index;
	struct host_frame_desc *f;
	struct work_struct work;
};

enum {
	NPU_UCTX_KHANDLE_MAGIC = 1,
	NPU_MODEL_KHANDLE_MAGIC,
	NPU_FRAME_KHANDLE_MAGIC,
	NPU_DMABUF_HANDLE_MAGIC,
};

struct win_engine {
	struct processors_interface *processors[HW_OP_NUM];
	struct semaphore runtime_sem;
	void *master_shm;
	void *master_mem;
	void *aux_shm;
	void *aux_mem;
	void *major_shm[NUM_MAJOR_CORES];
	void *major_mem[NUM_MAJOR_CORES];
	void *nvdla_dev;
	struct mutex reset_mutex;
	/* Current executor */
	void *cur[NUM_TIKTOK];
	/* Next executor to use, Protected by executor_lock */
	spinlock_t executor_lock;
	bool perf_switch;
	void *perf_data_buf;
	struct timer_list timer[NUM_TIKTOK];
	atomic_t is_sending;

	struct list_head sched_frame_list;
	struct host_frame_desc *tiktok_frame[NUM_TIKTOK];
	u32 tiktok;
	u32 frame_seq;

	bool engine_is_alive;
	struct workqueue_struct *work_queue;
	struct dump_file_work_t dump_file_work[NUM_TIKTOK];

	struct workqueue_struct *dump_op_work_queue;
	struct dump_op_work_t dump_op_work[NUM_OP_TYPE];

	struct host_frame_desc *frame_done;
	struct work_struct frame_done_work;
	u8 *is_event_source_done[NUM_TIKTOK];

	struct work_struct complete_work;
	spinlock_t complete_lock;
	struct list_head frame_complete_list;

	host_node_t *host_node;
	dma_addr_t host_node_iova;
	struct device *dsp_dev[DSP_MAX_CORE_NUM];
};

enum frame_state_list {
	frame_state_done = 0xd,
	frame_state_working = 0xe,
	frame_state_queued = -1,
	frame_state_orphan = 0xfe,
};

#define ES_TASK_MAX_FD_CNT 10

struct host_frame_desc {
	struct khandle handle;
	struct list_head sched_node;
	u16 input_num;
	u16 output_num;
	addrDesc_t *io_tensor_list;
	u64 *io_tensors_addr_list;
	npu_io_tensor_t io_tensor;
	//for e31
	u32 tiktok;
	bool dump_dtim;
	struct list_head complete_entry;

	struct win_executor *executor;
	void *model;

	s32 state;
	s32 frame_idx;
	wait_queue_head_t frame_done;
	struct host_frame_desc *next;
	u8 *is_event_source_done;
	struct dsp_dma_buf *dsp_io_dmabuf[DSP_MAX_CORE_NUM]
					 [DSP_KERNEL_MAX_INOUT_TENSOR_NUM];

	struct dla_buffer_object *input_bobj[ES_TASK_MAX_FD_CNT];
	struct dla_buffer_object *output_bobj[ES_TASK_MAX_FD_CNT];
} __attribute__((aligned(sizeof(u32))));

void npu_frame_done_process(struct host_frame_desc *f);

static inline void npu_set_u16_bit(int nr, u16 *addr)
{
	addr[nr / 16] |= (1UL << (nr % 16));
}

static inline void npu_set_u64_bit(int nr, u64 *addr)
{
	addr[nr / 64] |= (1ULL << (nr % 64));
}

typedef struct _edma_tensor_t {
	u32 src_is_io_tensor;
	u32 dst_is_io_tensor;
	struct hw_desc hw;
	struct hw_desc_src src;
	struct hw_desc_dst dst;
} edma_tensor_t;

typedef struct _conv_tensor_t {
	//struct conv_host2dev_t h2m_msg;
	u32 ifmap_is_io_tensor;
	u32 ofmap_is_io_tensor;
	u64 ifmap_base_addr;
	u64 ofmap_base_addr;
} __attribute__((aligned(64))) conv_tensor_t;

typedef struct _dsp_tensor_t {
	u32 have_unfold;

	u32 src_is_io_tensor[DSP_KERNEL_MAX_INOUT_TENSOR_NUM];
	u32 dst_is_io_tensor[DSP_KERNEL_MAX_INOUT_TENSOR_NUM];
	u64 src_base_addr[DSP_KERNEL_MAX_INOUT_TENSOR_NUM];
	u64 dst_base_addr[DSP_KERNEL_MAX_INOUT_TENSOR_NUM];
	u64 handle;
	u32 flat1_size;
	u32 flat1_addr_offset;
	u32 flat1_dma_addr;
	void *flat1_vaddr;
	u32 data_dma_addr;
} __attribute__((aligned(64))) dsp_tensor_t;

typedef struct _sdp_tensor_t {
	u32 src_is_io_tensor;
	u32 dst_is_io_tensor;
	u32 x1_is_io_tensor;
	u32 x2_is_io_tensor;
	u32 y_is_io_tensor;
	u64 no_fly_src_addr;
	u64 out_dma_dst_addr;
	u64 x1_addr;
	u64 x2_addr;
	u64 y_addr;

	u8 fly;
	u8 out_dma_ena;
	u8 x1_rdma_ena;
	u8 x2_rdma_ena;
	u8 y_rdma_ena;
} sdp_tensor_t;

typedef struct _pdp_tensor_t {
	u32 input_is_io_tensor;
	u32 output_is_io_tensor;
	u64 input_address;
	u64 output_address;
} pdp_tensor_t;

typedef struct _rubik_tensor_t {
	u32 input_is_io_tensor;
	u32 output_is_io_tensor;
	u64 input_address;
	u64 output_address;
} rubik_tensor_t;

typedef struct _event_sink_tensor_t {
	u32 input_is_io_tensor;
} event_sink_tensor_t;

typedef struct _event_source_tensor_t {
	u32 input_is_io_tensor;
} event_source_tensor_t;

enum executor_state_list {
	executor_state_live = 0xaa,
	executor_state_dead = 0xfe,
};

struct io_mem_info {
	int io_cnt;
	u32 *offset;
	u64 *virt;
};

struct win_executor {
	uint8_t *dependency_count;
	uint16_t num_proc_hwl;
	int32_t status;
	void *model;
	s32 state;
	struct dla_task *task;
	struct dla_network_desc *network;
	struct nvdla_task *mem_handles;
	struct win_engine *engine;
	void *driver_context;
	void *tensor_set[NUM_OP_TYPE];
	//for e31
	void *prog_data_buf_bobj[NUM_OP_TYPE];
	dma_addr_t dma_addr[NUM_OP_TYPE];
	u32 prog_data_size[NUM_OP_TYPE];
	// this code is very ugly, need to remove, but now need it
	struct dla_buffer_object *tmp_for_simu[NUM_OP_TYPE];

	dma_addr_t recent_lut_iova;
	dma_addr_t lut_base_iova;

	int dsp_op_num;
	void *dsp_flat1_set_vaddr[DSP_MAX_CORE_NUM];
	dma_addr_t dsp_flat1_set_dma[DSP_MAX_CORE_NUM];
	u32 dsp_flat1_set_size[DSP_MAX_CORE_NUM];
	struct dsp_dma_buf *model_dsp_dmabuf[DSP_MAX_CORE_NUM];
	struct xarray dsp_ddr_xrray[DSP_MAX_CORE_NUM];
	struct mutex xrray_lock[DSP_MAX_CORE_NUM];

	struct io_mem_info dsp_io[DSP_MAX_CORE_NUM]
				 [DSP_KERNEL_MAX_INOUT_TENSOR_NUM];

	int dsp_iobuf_cnt[DSP_MAX_CORE_NUM];
	void *dsp_iobuf_virt[DSP_MAX_CORE_NUM];
	void *dsp_iobuf_offset[DSP_MAX_CORE_NUM];
	int dsp_all_inout[DSP_MAX_CORE_NUM][DSP_KERNEL_MAX_INOUT_TENSOR_NUM];

	u16 input_num;
	u16 output_num;
	u32 frame_size;
	u32 io_mem_handle_size;
	s16 head_op_idx[NUM_OP_TYPE];

#define INVALID_OP_IDX (-32768)
	s16 *cfg_seq[NUM_OP_TYPE];
	u16 total_op_num;
	u16 op_num[NUM_OP_TYPE];

	op_current_t op_prog_addrs;

	/*event map*/
	s16 *event_sink_map;
	s16 *event_source_map;
	u16 total_event_sink_num;
	u16 total_event_source_num;
	s32 dsp_fd[DSP_MAX_CORE_NUM];
	struct file *dsp_file[DSP_MAX_CORE_NUM];
	kmd_dump_info_t dump_info;
};

void *npu_alloc_dma_addr(struct win_executor *executor, size_t size,
			 dma_addr_t *dma_handle, int i, gfp_t gfp);
void npu_free_dma_addr(struct win_executor *executor, int i);
int npu_init_ipc(struct nvdla_device *ndev);
int npu_uninit_ipc(struct nvdla_device *ndev);

void npu_frame_schedule(struct win_engine *engine);
void npu_drop_all_frame(struct nvdla_device *ndev, bool dump_dtim);

int prepare_frame_io_tensor(struct win_engine *engine, struct win_executor *exe,
			    struct host_frame_desc *f);
int prepare_e31_frame_info(struct win_executor *executor,
			   struct user_model *model);

bool send_new_frame(struct win_engine *engine, struct win_executor *executor,
		    struct host_frame_desc *f);

int start_inference(void *arg_executor);

int set_current(struct win_engine *engine, struct win_executor *executor,
		u32 tiktok);
int unset_current(struct win_engine *engine, struct win_executor *executor,
		  u32 tiktok);
int create_executor(struct dla_task *task, struct dla_network_desc *network,
		    void **m_executor, struct nvdla_task *mem_handles,
		    void *engine, void *dev, struct user_model *model);
void destroy_executor(void *arg_executor);
int win_engine_init(struct nvdla_device *nvdla_dev, void **arg_engine);
void win_engine_destroy(struct nvdla_device *nvdla_dev);

int io_tensor_record(struct win_executor *executor, addrDesc_t *handle,
		     u32 *is_io_tensor);
int read_input_address(struct win_executor *executor,
		       struct dla_data_cube *data, uint64_t *address,
		       u32 *is_io_tensor);
int io_tensor_to_io_addr(struct win_executor *executor,
			 struct host_frame_desc *f);
int create_new_frame(struct win_executor *executor, struct host_frame_desc **f,
		     void *model);
void destroy_frame(struct host_frame_desc *f);
void executor_clearup(void *arg_executor);

extern const u8 processor_idx_convert[NUM_OP_TYPE];
extern const u8 processor_dla_convert[HW_OP_NUM];
char *pcer2str(u8 pcer);
void dump_frame_op_statistic(struct win_executor *executor);
/**************** engine_context.c ****************/

/**************** dep_graph_parser.c ****************/
int generate_small_program(struct win_executor *executor);
int generate_event_map(struct win_executor *executor);
int set_pause_op_done(struct win_executor *executor,
		      kmd_dump_info_t *dump_info);
int reset_pause_op_done(struct win_executor *executor);

void mbx_irq_frame_done(struct win_engine *engine, u32 tiktok, u32 stat);
void mbx_irq_op_done(struct win_engine *engine, u32 tiktok, u16 op_index);
void mbx_irq_event_sink_done(struct win_engine *engine, u32 tiktok,
			     u16 op_index);
int send_frame_to_npu(struct host_frame_desc *f, int tiktok);
/**************** frame_scheduler.c ****************/
int edma_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx);
int edma_prepare_prog_data(struct win_executor *executor, int rdma,
			   int tensor_idx, u16 op_idx,
			   union dla_operation_container *operation_desc,
			   union dla_surface_container *surface_desc);

int conv_set_program_data(struct win_executor *executor, int idx, int op_idx,
			  void *tensor,
			  union dla_operation_container *operation_desc);

int conv_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx);

int dla_conv_prepare_prog_data(struct win_executor *executor, int rdma,
			       int tensor_idx, u16 op_idx,
			       union dla_operation_container *operation_desc,
			       union dla_surface_container *surface_desc);

int sdp_tensor_unfold(struct win_executor *executor, int op_idx,
		      union dla_operation_container *operation_desc,
		      union dla_surface_container *surface_desc, void *tensor,
		      int idx);
int dla_sdp_prepare_prog_data(struct win_executor *executor, int rdma,
			      int tensor_idx, u16 op_idx,
			      union dla_operation_container *operation_desc,
			      union dla_surface_container *surface_desc);

int pdp_tensor_unfold(struct win_executor *executor, int op_idx,
		      union dla_operation_container *operation_desc,
		      union dla_surface_container *surface_desc, void *tensor,
		      int idx);
int dla_pdp_prepare_prog_data(struct win_executor *executor, int rdma,
			      int tensor_idx, u16 op_idx,
			      union dla_operation_container *operation_desc,
			      union dla_surface_container *surface_desc);

int rubik_tensor_unfold(struct win_executor *executor, int op_idx,
			union dla_operation_container *operation_desc,
			union dla_surface_container *surface_desc, void *tensor,
			int idx);
int dla_rubik_prepare_prog_data(struct win_executor *executor, int rdma,
				int tensor_idx, u16 op_idx,
				union dla_operation_container *operation_desc,
				union dla_surface_container *surface_desc);

int dla_event_sink_rdma_check(struct dla_processor_group *group,
			      union dla_operation_container *op,
			      union dla_surface_container *surface);

int event_sink_tensor_unfold(struct win_executor *executor, int op_idx,
			     union dla_operation_container *operation_desc,
			     union dla_surface_container *surface_desc,
			     void *tensor, int idx);

void dla_event_sink_dump_config(struct dla_processor_group *group);

int dla_event_sink_prepare_prog_data(
	struct win_executor *executor, int rdma, int tensor_idx, u16 op_idx,
	union dla_operation_container *operation_desc,
	union dla_surface_container *surface_desc);

int dla_event_source_rdma_check(struct dla_processor_group *group,
				union dla_operation_container *op,
				union dla_surface_container *surface);

int event_source_tensor_unfold(struct win_executor *executor, int op_idx,
			       union dla_operation_container *operation_desc,
			       union dla_surface_container *surface_desc,
			       void *tensor, int idx);

void dla_event_source_dump_config(struct dla_processor_group *group);

int dla_event_source_prepare_prog_data(
	struct win_executor *executor, int rdma, int tensor_idx, u16 op_idx,
	union dla_operation_container *operation_desc,
	union dla_surface_container *surface_desc);

/* dsp operation*/

int dsp0_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx);

int dsp1_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx);

int dsp2_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx);

int dsp3_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx);

int dsp0_prepare_prog_data(struct win_executor *executor, int rdma, int idx,
			   u16 op_idx, union dla_operation_container *op_desc,
			   union dla_surface_container *surf_desc);
int dsp1_prepare_prog_data(struct win_executor *executor, int rdma, int idx,
			   u16 op_idx, union dla_operation_container *op_desc,
			   union dla_surface_container *surf_desc);
int dsp2_prepare_prog_data(struct win_executor *executor, int rdma, int idx,
			   u16 op_idx, union dla_operation_container *op_desc,
			   union dla_surface_container *surf_desc);
int dsp3_prepare_prog_data(struct win_executor *executor, int rdma, int idx,
			   u16 op_idx, union dla_operation_container *op_desc,
			   union dla_surface_container *surf_desc);
#endif
