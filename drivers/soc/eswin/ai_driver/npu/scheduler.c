// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include "dla_log.h"
#include "dla_driver.h"
#include "dla_engine.h"
#include "hetero_ioctl.h"
#include "internal_interface.h"
#include "npu_spram.h"
#include "conv.h"
#include "hetero_host.h"
#include "dla_buffer.h"
#include "debug.h"
#include "nvdla_proc.h"

static int frame_timeout = 1000000;
module_param(frame_timeout, int, 0644);
MODULE_PARM_DESC(frame_timeout, "frame timeout in ms, default 1000s");

extern void handle_event_sink_from_e31(struct win_engine *engine, u32 tiktok,
				       u16 op_index);

static inline void pick_next_frame(struct win_engine *engine,
				   struct host_frame_desc **f)
{
	*f = list_first_entry_or_null(&engine->sched_frame_list,
				      struct host_frame_desc, sched_node);
	if (unlikely(*f == NULL)) {
		/* No more frame */
		dla_debug("%s %d no more frame\n", __func__, __LINE__);
		return;
	}
	list_del(&((*f)->sched_node));
	return;
}

static int send_frame_to_hw(struct win_engine *engine, u8 tiktok,
			    hetero_ipc_frame_t *frame_desc,
			    npu_io_tensor_t *io_tensor, host_node_t *host_node)
{
	int ret;
	emission_node_t *pemission_node =
		(emission_node_t *)host_node->emission_base_addr;
	program_node_t *program_node =
		(program_node_t *)host_node->program_base_addr;
	u8 param = tiktok | ((engine->perf_switch) ? 0x02 : 0x0);
	msg_payload_t payload = { FRAME_READY, param };
	memcpy(&pemission_node->frame_desc[tiktok], frame_desc,
	       sizeof(hetero_ipc_frame_t));
	memcpy(program_node->io_addr_list[tiktok].tensor_addr,
	       io_tensor->tensor_addr, sizeof(io_tensor->tensor_addr));

	ret = send_mbx_msg_to_e31(engine, payload);
	if (ret) {
		dla_error("%s, %d, send mailbox to npu error=%d.\n", __func__,
			  __LINE__, ret);
	}
	return ret;
}

int send_frame_to_npu(struct host_frame_desc *f, int tiktok)
{
#if (NPU_DEV_SIM != NPU_MCU_HOST)
	struct win_engine *engine = f->executor->engine;
#endif
#if (NPU_DEV_SIM == NPU_REAL_ENV)
	struct user_model *model = (struct user_model *)f->model;
#endif

	ASSERT(tiktok < NUM_TIKTOK);
#if (NPU_DEV_SIM == NPU_REAL_ENV)
	send_frame_to_hw(engine, tiktok,
			 (hetero_ipc_frame_t *)&model->e31_frame_info,
			 &f->io_tensor, engine->host_node);
#if 0
	send_frame_ready(tiktok, engine->perf_switch,
			 (hetero_ipc_frame_t *)&model->e31_frame_info,
			 &f->io_tensor,
			 (void *)((struct nvdla_device *)engine->nvdla_dev)
				 ->e31_mmio_base,
			 engine->host_node);
	ret = send_mbx_msg_to_e31(engine);
	if (ret) {
		dla_error("%s, %d, send mailbox to npu error=%d.\n", __func__,
			  __LINE__, ret);
		return ret;
	}
#endif
#elif (NPU_DEV_SIM == NPU_MCU_ALONE)
	if (!work_pending(&engine->dump_file_work[tiktok].work)) {
		engine->dump_file_work[tiktok].f = f;
		queue_work(engine->work_queue,
			   &engine->dump_file_work[tiktok].work);
	} else {
		dla_error("%s, %d, dump_file_work error\n", __func__, __LINE__);
		return -1;
	}
#elif (NPU_DEV_SIM == NPU_MCU_HOST)
	dla_debug("hetero_send_frame_to_npu\n");
	hetero_send_frame_to_npu(tiktok, f);
#else
	dla_error("%s, %d, Wrong NPU_DEV_SIM = %d.\n", __func__, __LINE__,
		  NPU_DEV_SIM);
	return -1;
#endif

	return 0;
}

static bool is_frame_model_alive(struct host_frame_desc *f)
{
	struct user_model *model = f->model;
	struct user_context *uctx = model->uctx;
	struct win_engine *engine = (struct win_engine *)model->engine;

	return uctx->uctx_is_alive && engine->engine_is_alive;
}

void npu_frame_schedule(struct win_engine *engine)
{
	struct host_frame_desc *f = NULL;
	unsigned long flags;
	bool ret;

	while (true) {
		spin_lock_irqsave(&engine->executor_lock, flags);
		if (atomic_read(&engine->is_sending) ||
		    engine->tiktok_frame[engine->tiktok] != NULL) {
			spin_unlock_irqrestore(&engine->executor_lock, flags);
			break;
		}

		pick_next_frame(engine, &f);
		if (f == NULL) {
			spin_unlock_irqrestore(&engine->executor_lock, flags);
			break;
		}
		atomic_set(&engine->is_sending, 1);
		spin_unlock_irqrestore(&engine->executor_lock, flags);

		ret = is_frame_model_alive(f);
		if (!ret) {
			spin_lock_irqsave(&engine->complete_lock, flags);
			list_add_tail(&f->complete_entry,
				      &engine->frame_complete_list);
			spin_unlock_irqrestore(&engine->complete_lock, flags);

			if (!work_pending(&engine->complete_work)) {
				queue_work(system_highpri_wq,
					   &engine->complete_work);
			}

		} else {
			engine->tiktok_frame[engine->tiktok] = f;
			f->tiktok = engine->tiktok;
			f->is_event_source_done =
				engine->is_event_source_done[engine->tiktok];
			memset(f->is_event_source_done, 0,
			       COMPLETE_EVENT_ID / 8);

			set_current(engine, f->executor, f->tiktok);
			engine->timer[f->tiktok].expires =
				jiffies + msecs_to_jiffies(frame_timeout);
			engine->tiktok = (engine->tiktok + 1) % NUM_TIKTOK;
			smp_mb();

			preempt_disable();
			add_timer(&engine->timer[f->tiktok]);
			send_frame_to_npu(f, f->tiktok);
			preempt_enable();

			dla_debug("%s, %d, done.\n", __func__, __LINE__);
		}
		atomic_set(&engine->is_sending, 0);
	}
}

bool send_new_frame(struct win_engine *engine, struct win_executor *executor,
		    struct host_frame_desc *f)
{
	unsigned long flags;

	INIT_LIST_HEAD(&f->sched_node);
	spin_lock_irqsave(&engine->executor_lock, flags);
	if (is_frame_model_alive(f) == false) {
		dla_error("queue frame on executor state dead\n");
		spin_unlock_irqrestore(&engine->executor_lock, flags);
		return false;
	}
	dla_debug("add frame to sched_frame_list.\n");
	list_add_tail(&f->sched_node, &engine->sched_frame_list);
	spin_unlock_irqrestore(&engine->executor_lock, flags);
	npu_frame_schedule(engine);

	return true;
}

void mbx_irq_event_sink_done(struct win_engine *priv, u32 tiktok, u16 op_index)
{
	struct win_engine *engine = priv;
	dla_debug("receive event sink op done: tiktok %u op_index %u.\n",
		  tiktok, op_index);
	handle_event_sink_from_e31(engine, tiktok, op_index);
}

void mbx_irq_op_done(struct win_engine *priv, u32 tiktok, u16 op_index)
{
	struct win_engine *engine = priv;
	struct host_frame_desc *f;
	u16 op_type = NUM_OP_TYPE;
	unsigned long flags;

	spin_lock_irqsave(&engine->executor_lock, flags);
	f = engine->tiktok_frame[tiktok];
	if (f == NULL) {
		dla_debug("irq get %d, frame is null, err.\n", tiktok);
		spin_unlock_irqrestore(&engine->executor_lock, flags);
		return;
	}

	if (f->executor->network->num_operations > op_index) {
		op_type = f->executor->task->common_desc[op_index].op_type;
	}

	if (op_type == NUM_OP_TYPE) {
		dla_error("cannot find op_type by idx:%d\n", op_index);
		spin_unlock_irqrestore(&engine->executor_lock, flags);
		return;
	}

	spin_unlock_irqrestore(&engine->executor_lock, flags);

	if (!work_pending(&engine->dump_op_work[op_type].work)) {
		engine->dump_op_work[op_type].tiktok = tiktok;
		engine->dump_op_work[op_type].f = f;
		engine->dump_op_work[op_type].op_index = op_index;
		queue_work(engine->dump_op_work_queue,
			   &engine->dump_op_work[op_type].work);
		return;
	}
}

void mbx_irq_frame_done(struct win_engine *priv, u32 tiktok, u32 stat)
{
	struct win_engine *engine = priv;
	struct win_executor *executor;
	struct host_frame_desc *f;
	struct user_model *model;
	int ret = 1;
	unsigned long last_state;
	unsigned long flags;

	ret = del_timer(&engine->timer[tiktok]);
	if (!ret) {
		dla_debug("%s, %d, task is now processing in timer.\n",
			  __func__, __LINE__);
		return;
	}

	f = engine->tiktok_frame[tiktok];
	if (f == NULL) {
		dla_debug("irq get %d, frame is null, err.\n", tiktok);
		return;
	}

	executor = f->executor;
	model = f->model;
	engine = executor->engine;
	if (unlikely(stat && engine->perf_switch)) {
		refresh_op_statistic(executor, engine, tiktok);
	}

	if (!is_frame_model_alive(f)) {
		model->uctx->event_desc.len = 0;
		memset(&model->uctx->event_desc.event_sinks[0], -1,
		       MAX_EVENT_SINK_SAVE_NUM * sizeof(s16));
	}
	spin_lock_irqsave(&engine->executor_lock, flags);
	engine->tiktok_frame[f->tiktok] = NULL;
	unset_current(engine, executor, f->tiktok);
	spin_unlock_irqrestore(&engine->executor_lock, flags);

	npu_frame_done_process(f);
	last_state = atomic_fetch_and(~NPU_RT_MUTX_FRAME_DONE,
				      &model->uctx->lock_status);
	if (last_state == NPU_RT_MUTX_FRAME_DONE) {
		up(&engine->runtime_sem);
		dla_debug("%s, %d unlocked, last_state:0x%lx\n", __func__,
			  __LINE__, last_state);
	}

	dla_debug("%s, %d.\n", __func__, __LINE__);
}
