// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
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
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)executor->driver_context;
	event_sink_dev_t *event_sink;
	event_sink_dev_t *event_sink_data;
	event_source_dev_t *event_source;
	event_source_dev_t *event_source_data;
	int8_t p2p_src;
    int8_t p2p_dst;

	f->input_num = executor->input_num;
	f->output_num = executor->output_num;

	if (((address[0].devBuf.reserve >> 32) & 0xffff) == 0xff11) {
		event_sink = (event_sink_dev_t *)executor->prog_data_buf_bobj[IDX_EVENT_SINK];
		event_sink_data = (event_sink_dev_t *)&event_sink[0];
		p2p_src = (address[0].devBuf.reserve & 0xffff) >> 8;
		p2p_dst = address[0].devBuf.reserve & 0xff;

		mutex_lock(&nvdla_dev->mapping_mutex);
		ret = dma_set_mask_and_coherent(&nvdla_dev->pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_warn(&nvdla_dev->pdev->dev, "Unable to set coherent mask 32bit\n");
			mutex_unlock(&nvdla_dev->mapping_mutex);
			return ret;
		}

		dla_debug("sink event fd:%lld, p2p_src:%d, p2p_dst:%d\n", address[0].devBuf.memFd, p2p_src, p2p_dst);
		fd = address[0].devBuf.memFd;
		f->input_bobj[0] = dla_import_fd_to_device(fd, &nvdla_dev->pdev->dev);
		if (IS_ERR(f->input_bobj[0])) {
			dla_error("err:import input dmabuf error!\n");
			mutex_unlock(&nvdla_dev->mapping_mutex);
			return -ENOMEM;
		}
		f->input_bobj[0]->fd = -1;

		dla_debug("sink event dma addr:0x%llx\n", f->input_bobj[0]->dma_addr);

		event_sink_data->npu_info.peer_address = f->input_bobj[0]->dma_addr;
		event_sink_data->npu_info.peer_link = p2p_src << 8 | p2p_dst;

		ret = dma_set_mask_and_coherent(&nvdla_dev->pdev->dev, DMA_BIT_MASK(41));
		mutex_unlock(&nvdla_dev->mapping_mutex);
		if (ret) {
			dev_warn(&nvdla_dev->pdev->dev, "Unable to set coherent mask 41bit\n");
			return ret;
		}

		addr_list[address[0].bindId] = f->input_bobj[0]->dma_addr +	address[0].devBuf.offset;
		dla_detail("addr_list[address[0].bind_id=%lld\n", addr_list[address[0].bindId]);

		return 0;
	}

	if (((address[0].devBuf.reserve >> 32) & 0xffff) == 0xff22) {
		event_source = (event_source_dev_t *)executor->prog_data_buf_bobj[IDX_EVENT_SOURCE];
		event_source_data = (event_source_dev_t *)&event_source[0];
		p2p_src = (address[0].devBuf.reserve & 0xffff) >> 8;
		p2p_dst = address[0].devBuf.reserve & 0xff;

		dla_info("source event p2p_src:%d, p2p_dst:%d, \n", p2p_src, p2p_dst);

		event_source_data->npu_info.peer_link = p2p_src << 8 | p2p_dst;

		return 0;
	}

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
				mutex_lock(&nvdla_dev->mapping_mutex);
				f->input_bobj[i] = dla_import_fd_to_device(fd, &nvdla_dev->pdev->dev);
				mutex_unlock(&nvdla_dev->mapping_mutex);
				if (IS_ERR(f->input_bobj[i])) {
					dla_error("err:import input dmabuf error!i=%d\n", i);
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
				mutex_lock(&nvdla_dev->mapping_mutex);
				f->output_bobj[i] = dla_import_fd_to_device(fd, &nvdla_dev->pdev->dev);
				mutex_unlock(&nvdla_dev->mapping_mutex);
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
	if(f->sync_flag && f->sync_event_id != -1){
		complete(&f->synctask_comp);
	} else {
		spin_lock_irqsave(&engine->complete_lock, flags);
		list_add_tail(&f->complete_entry, &engine->frame_complete_list);
		spin_unlock_irqrestore(&engine->complete_lock, flags);

		if (!work_pending(&engine->complete_work)) {
			queue_work(system_highpri_wq, &engine->complete_work);
		}
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
	unsigned long flags;
	struct win_executor *executor;

	spin_lock_irqsave(&engine->executor_lock, flags);
	engine->engine_is_alive = false;
	f = engine->tiktok_frame[tiktok];
	executor = f->executor;
	engine->tiktok_frame[tiktok] = NULL;
	unset_current(engine, executor, f->tiktok);
	spin_unlock_irqrestore(&engine->executor_lock, flags);

	npu_dump_dtim(engine, f);
	npu_frame_done_process(f);
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
		     void *model, bool sync_flag)
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
	(*f)->sync_flag = sync_flag;
	if(sync_flag) {
		(*f)->sync_event_id = -1;
		init_completion(&(*f)->synctask_comp);
	}

	INIT_LIST_HEAD(&(*f)->complete_entry);
	return executor->io_mem_handle_size;
err:
	kfree(*f);
	*f = NULL;
	return ret;
}
