// Copyright © 2024 ESWIN. All rights reserved.
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
#include "dsp.h"

static inline void set_dep_cnt(u8 *dep, u16 op, u8 value)
{
	u16 slot = op / NUM_CNT_PER_BYTE;
	u8 shift = (op % NUM_CNT_PER_BYTE) * BIT_PER_DEPCNT;

	dep[slot] |= (value & DEP_CNT_MASK) << shift;
}

int prepare_e31_frame_info(struct win_executor *executor,
			   struct user_model *model)
{
	hetero_ipc_frame_t *frame_info =
		(hetero_ipc_frame_t *)&model->e31_frame_info;
	int i;
	u16 op;

	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		if (executor->op_num[i] <= 0) {
			frame_info->op_current.program_addr[i] = 0;
			frame_info->op_current.num_remain_ops[i] = 0;
			continue;
		}

		if (i == IDX_CONV) {
			dla_debug("conv_op#=%u, %s, %d.\n", executor->op_num[i],
				  __func__, __LINE__);
			memcpy(&frame_info->op_current.next_conv_hdr,
			       &executor->op_prog_addrs.next_conv_hdr,
			       sizeof(conv_dev_hdr_t));
		}
		dla_debug("%s, %d, i = %d, bobj addr=0x%px.\n", __func__,
			  __LINE__, i, executor->prog_data_buf_bobj[i]);
		frame_info->op_current.program_addr[i] = executor->dma_addr[i];

		frame_info->op_current.num_remain_ops[i] = executor->op_num[i];
	}
	for (op = 0; op < executor->total_op_num && op < MAX_DTIM_DEPCNT;
	     op++) {
		set_dep_cnt(frame_info->op_dependency.ref_count, op,
			    executor->dependency_count[op]);
	}

	frame_info->op_dependency.num_op = executor->total_op_num;

	dla_debug("%s, %d. total_op_num:%d\n", __func__, __LINE__,
		  executor->total_op_num);
	return 0;
}

int io_tensor_to_io_addr(struct win_executor *executor,
			 struct host_frame_desc *f)
{
	u16 i = 0, input_num = 0, output_num = 0;
	addrDesc_t *address = f->io_tensor_list;
	u64 *addr_list = f->io_tensors_addr_list;
	int fd = 0;
	struct khandle *handle;
	struct npu_dma_buf_ex *entry;
	int ret;
	struct user_model *model = (struct user_model *)executor->model;
	struct user_context *uctx = model->uctx;
	struct nvdla_device *nvdla_dev =
		(struct nvdla_device *)executor->driver_context;

	f->input_num = executor->input_num;
	f->output_num = executor->output_num;

	ret = npu_set_dsp_iobuf(executor, f);
	if (ret < 0) {
		dla_error("%s, %d, set dsp iobuf error.\n", __func__, __LINE__);
		return -EINVAL;
	}

	for (i = 0; input_num < executor->input_num ||
		    output_num < executor->output_num;
	     i++) {
		dla_detail("i=%d fd=%lld address[i].flag=0x%x\n", i,
			   address[i].devBuf.memFd, address[i].flag);
		if (address[i].flag == mem_flag_input) {
			if (address[i].bindId > executor->input_num) {
				dla_error("%s %d invalid bind_id %d\n",
					  __func__, __LINE__,
					  address[i].bindId);
				goto map_err;
			}

			fd = address[i].devBuf.memFd;
			mutex_lock(&uctx->dma_lock);
			entry = xa_load(&uctx->buf_xrray, (int)fd);
			if (entry) {
				dla_debug(
					"%s, %d, input entry->buf_info.memFd = %d.\n",
					__func__, __LINE__,
					(int)entry->buf_info.memFd);
				handle = find_kernel_handle(
					&uctx->handle, entry->handle.fd,
					NPU_DMABUF_HANDLE_MAGIC);
			}
			mutex_unlock(&uctx->dma_lock);
			if (!entry || !handle) {
				f->input_bobj[i] = dla_import_fd_to_device(
					fd, &nvdla_dev->pdev->dev);
				if (IS_ERR(f->input_bobj[i])) {
					dla_error(
						"err:import input dmabuf error!i=%d\n",
						i);
					goto map_err;
				}
				f->input_bobj[i]->fd = -1;
			} else {
				f->input_bobj[i] = &entry->obj;
			}

			addr_list[address[i].bindId] =
				f->input_bobj[i]->dma_addr +
				address[i].devBuf.offset;
			dla_detail("i=%d addr_list[address[i].bind_id=%lld\n",
				   i, addr_list[address[i].bindId]);

			input_num++;
		}
		if (address[i].flag == mem_flag_output) {
			if (address[i].bindId > executor->output_num) {
				dla_error("%s %d invalid bind_id %d\n",
					  __func__, __LINE__,
					  address[i].bindId);
				goto map_err;
			}

			fd = address[i].devBuf.memFd;
			mutex_lock(&uctx->dma_lock);
			entry = xa_load(&uctx->buf_xrray, (int)fd);
			if (entry) {
				dla_debug(
					"%s, %d, output entry->buf_info.memFd = %d.\n",
					__func__, __LINE__,
					(int)entry->buf_info.memFd);
				handle = find_kernel_handle(
					&uctx->handle, entry->handle.fd,
					NPU_DMABUF_HANDLE_MAGIC);
			}
			mutex_unlock(&uctx->dma_lock);
			if (!entry || !handle) {
				f->output_bobj[i] = dla_import_fd_to_device(
					fd, &nvdla_dev->pdev->dev);
				if (!f->output_bobj[i]) {
					dla_error(
						"%s, %d, import output fd = %d err.r\n",
						__func__, __LINE__, fd);
					goto map_err;
				}
				f->output_bobj[i]->fd = -1;
			} else {
				f->output_bobj[i] = &entry->obj;
			}

			addr_list[address[i].bindId + executor->input_num] =
				f->output_bobj[i]->dma_addr +
				address[i].devBuf.offset;
			dla_detail("i=%d addr_list[address[i].bind_id=%lld\n",
				   i, addr_list[address[i].bindId]);

			output_num++;
		}
	}

	return 0;

map_err:
	return -1;
}

void destroy_frame(struct host_frame_desc *f)
{
	int i = 0;
	struct npu_dma_buf_ex *entry;

	if (f == NULL) {
		return;
	}
	for (i = 0; i < ES_TASK_MAX_FD_CNT; i++) {
		if (f->input_bobj[i] != NULL) {
			dla_detail("release input bobj\n");
			if (f->input_bobj[i]->fd == -1) {
				dla_release_bobj(f->input_bobj[i]);
				f->input_bobj[i] = NULL;
			} else {
				entry = container_of(f->input_bobj[i],
						     struct npu_dma_buf_ex,
						     obj);
				kernel_handle_decref(&entry->handle);
			}
		}
		if (f->output_bobj[i] != NULL) {
			dla_detail("release output bobj\n");
			if (f->output_bobj[i]->fd == -1) {
				dla_release_bobj(f->output_bobj[i]);
				f->output_bobj[i] = NULL;
			} else {
				entry = container_of(f->output_bobj[i],
						     struct npu_dma_buf_ex,
						     obj);
				kernel_handle_decref(&entry->handle);
			}
		}
	}
	destroy_frame_dsp_info(f->executor, f);
	kfree(f);
	dla_debug("%s, %d. ok.\n", __func__, __LINE__);
}

static void npu_release_frame(struct khandle *h)
{
	struct host_frame_desc *f =
		container_of(h, struct host_frame_desc, handle);
	struct user_model *model;

	model = f->model;
	destroy_frame(f);
	kernel_handle_decref(&model->handle);
	dla_debug("npu_free_frame ok.\n");
}

void npu_frame_done_process(struct host_frame_desc *f)
{
	struct win_executor *executor = f->executor;
	struct win_engine *engine = executor->engine;
	unsigned long flags;

	spin_lock_irqsave(&engine->complete_lock, flags);
	list_add_tail(&f->complete_entry, &engine->frame_complete_list);
	spin_unlock_irqrestore(&engine->complete_lock, flags);

	if (!work_pending(&engine->complete_work)) {
		queue_work(system_highpri_wq, &engine->complete_work);
	}
	npu_frame_schedule(engine);
}

static void npu_dump_dtim(struct win_engine *engine, struct host_frame_desc *f)
{
#if (NPU_DEV_SIM == NPU_REAL_ENV)
	int i;

	f->dump_dtim = true;
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
#endif
}

void npu_drop_all_frame(struct nvdla_device *ndev, bool dump)
{
	struct host_frame_desc *f;
	unsigned long flags;
	struct win_executor *executor;
	struct win_engine *engine;
	int ret = 1;
	int i;

	engine = npu_get_win_engine(ndev);
	if (engine == NULL) {
		return;
	}

	spin_lock_irqsave(&engine->executor_lock, flags);
	engine->engine_is_alive = false;

	for (i = 0; i < NUM_TIKTOK; i++) {
		f = engine->tiktok_frame[i];
		if (f == NULL) {
			continue;
		}
		ret = del_timer(&engine->timer[i]);
		if (!ret) {
			dla_debug("%s, %d, task is now processing in timer.\n",
				  __func__, __LINE__);
			continue;
		}
		executor = f->executor;
		engine->tiktok_frame[i] = NULL;
		unset_current(engine, executor, f->tiktok);
		ret = 1;
		spin_unlock_irqrestore(&engine->executor_lock, flags);
		if (dump) {
			npu_dump_dtim(engine, f);
		}
		npu_frame_done_process(f);
		spin_lock_irqsave(&engine->executor_lock, flags);
	}
	spin_unlock_irqrestore(&engine->executor_lock, flags);
}

static void npu_process_timeout(struct win_engine *engine, u32 tiktok)
{
	struct host_frame_desc *f;
	struct user_model *model;
	unsigned long flags;
	struct win_executor *executor;
	unsigned long last_state;

	spin_lock_irqsave(&engine->executor_lock, flags);
	engine->engine_is_alive = false;
	f = engine->tiktok_frame[tiktok];
	executor = f->executor;
	engine->tiktok_frame[tiktok] = NULL;
	unset_current(engine, executor, f->tiktok);
	spin_unlock_irqrestore(&engine->executor_lock, flags);

	npu_dump_dtim(engine, f);

	npu_frame_done_process(f);
	model = f->model;
	last_state = atomic_fetch_and(~NPU_RT_MUTX_FRAME_DONE,
				      &model->uctx->lock_status);
	if (last_state == NPU_RT_MUTX_FRAME_DONE) {
		dla_debug("%s, %d unlocked, last_state:0x%lx\n", __func__,
			  __LINE__, last_state);
		up(&engine->runtime_sem);
	}

	dla_debug("%s, %d, timeout frame free done.\n", __func__, __LINE__);
}

void npu_frame_timeout_tok(struct timer_list *t)
{
	struct win_engine *engine =
		container_of(t, struct win_engine, timer[1]);
	dla_error("%s, npu frame timeout.\n", __func__);
	npu_process_timeout(engine, 1);
}

void npu_frame_timeout_tik(struct timer_list *t)
{
	struct win_engine *engine =
		container_of(t, struct win_engine, timer[0]);
	dla_error("%s, npu frame timeout.\n", __func__);
	npu_process_timeout(engine, 0);
}

int create_new_frame(struct win_executor *executor, struct host_frame_desc **f,
		     void *model)
{
	int ret = 0;
	struct user_model *m = (struct user_model *)model;
	struct user_context *uctx = m->uctx;

	*f = kzalloc(executor->frame_size, GFP_KERNEL);
	if (unlikely(*f == NULL)) {
		dla_error("%s %d no mem\n", __func__, __LINE__);
		return -ENOMEM;
	}
	(*f)->io_tensor_list =
		(addrDesc_t *)((u8 *)*f + sizeof(struct host_frame_desc));
	(*f)->io_tensors_addr_list =
		(u64 *)((u8 *)*f + sizeof(struct host_frame_desc) +
			executor->io_mem_handle_size);
	(*f)->model = model;

	ret = init_kernel_handle(&(*f)->handle, npu_release_frame,
				 NPU_FRAME_KHANDLE_MAGIC, &uctx->handle);
	if (ret) {
		dla_error("create khandle for frame error.\n");
		ret = -ENOMEM;
		goto err;
	}

	INIT_LIST_HEAD(&(*f)->complete_entry);
	return executor->io_mem_handle_size;
err:
	kfree(*f);
	*f = NULL;
	return ret;
}
