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

#include <opendla.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/iommu.h>
#include "es_iommu_rsv.h"
#include <dla_err.h>
#include <dla_interface.h>
#include "dla_engine.h"
#include "dla_engine_internal.h"
#include "hetero_ioctl.h"
#include "hetero_host.h"
#include "dla_driver.h"
#include "common.h"
#include "dla_log.h"
#include "internal_interface.h"
#include "dla_buffer.h"
#include "debug.h"
#include "dsp_hw.h"

#include "dsp.h"
const u8 processor_idx_convert[NUM_OP_TYPE] = {
	[IDX_EDMA] = DLA_OP_EDMA,
	[IDX_CONV] = DLA_OP_CONV,
	[IDX_SDP] = DLA_OP_SDP,
	[IDX_PDP] = DLA_OP_PDP,
	[IDX_RUBIK] = DLA_OP_RUBIK,
	[IDX_KMD_DSP0] = DLA_KMD_OP_DSP_0,
	[IDX_KMD_DSP1] = DLA_KMD_OP_DSP_1,
	[IDX_KMD_DSP2] = DLA_KMD_OP_DSP_2,
	[IDX_KMD_DSP3] = DLA_KMD_OP_DSP_3,
	[IDX_EVENT_SINK] = DLA_OP_EVENT_SINK,
	[IDX_EVENT_SOURCE] = DLA_OP_EVENT_SOURCE,
};
const u8 processor_dla_convert[HW_OP_NUM] = {
	[DLA_OP_EDMA] = IDX_EDMA,
	[DLA_OP_CONV] = IDX_CONV,
	[DLA_OP_SDP] = IDX_SDP,
	[DLA_OP_PDP] = IDX_PDP,
	[DLA_OP_RUBIK] = IDX_RUBIK,
	[DLA_KMD_OP_DSP_0] = IDX_KMD_DSP0,
	[DLA_KMD_OP_DSP_1] = IDX_KMD_DSP1,
	[DLA_KMD_OP_DSP_2] = IDX_KMD_DSP2,
	[DLA_KMD_OP_DSP_3] = IDX_KMD_DSP3,
	[DLA_OP_EVENT_SINK] = IDX_EVENT_SINK,
	[DLA_OP_EVENT_SOURCE] = IDX_EVENT_SOURCE,
};

int set_current(struct win_engine *engine, struct win_executor *executor,
		u32 tiktok)
{
	struct win_engine *win_engine = engine;

	if (unlikely(win_engine->cur[tiktok] != NULL || executor == NULL)) {
		dla_error(
			"BUG: trying to use busy engine or set engine NULL\n");
		return -1;
	}
	win_engine->cur[tiktok] = executor;
	return 0;
}
int unset_current(struct win_engine *engine, struct win_executor *executor,
		  u32 tiktok)
{
	if (unlikely(engine->cur[tiktok] != executor)) {
		dla_info("trying to unset invalid executor, nothing todo\n");
		return -1;
	}
	engine->cur[tiktok] = NULL;
	return 0;
}

int32_t dla_enable_intr(struct nvdla_device *dev, uint32_t mask)
{
	void *base = (struct nvdla_device *)dev->base;
	uint32_t reg = glb_reg_read(base, S_INTR_MASK);

	reg = reg & (~mask);
	glb_reg_write(base, S_INTR_MASK, reg);

	return 0;
}

int32_t dla_disable_intr(struct nvdla_device *dev, uint32_t mask)
{
	void *base = (struct nvdla_device *)dev->base;
	uint32_t reg = glb_reg_read(base, S_INTR_MASK);

	reg = reg | mask;
	glb_reg_write(base, S_INTR_MASK, reg);

	return 0;
}

/**
 * Get DMA data cube address
 */
int dla_get_dma_cube_address(void *driver_context, void *task_data,
			     int16_t index, uint32_t offset, void *dst_ptr,
			     u32 *is_io_tensor)
{
	int32_t ret = 0;
	uint64_t *pdst = (uint64_t *)dst_ptr;

	ret = dla_get_dma_address(driver_context, task_data, index, dst_ptr,
				  is_io_tensor);
	if (ret < 0)
		goto exit;

	if (ret == 1) {
		ret = 0;
		goto exit;
	}
	pdst[0] += offset;

exit:
	return ret;
}

int dla_get_sram_cube_address(void *driver_context, void *task_data,
			      int16_t index, uint32_t offset, uint64_t *dst_ptr,
			      u32 *is_io_tensor)
{
	int32_t ret = 0;
	ret = dla_get_sram_address(driver_context, task_data, index, dst_ptr,
				   is_io_tensor);
	if (ret < 0)
		goto exit;

	if (ret == 1) {
		ret = 0;
		goto exit;
	}
	*dst_ptr += offset;

exit:
	return ret;
}

int read_input_address(struct win_executor *executor,
		       struct dla_data_cube *data, uint64_t *address,
		       u32 *is_io_tensor)
{
	int32_t ret = ERR(INVALID_INPUT);

	/**
	 * If memory type is HW then no address required
	 */
	if (data->type == DLA_MEM_HW) {
		*address = -1ull;
		*is_io_tensor = invalid_tensor_idx;
		ret = 0;
		goto exit;
	}

	if (data->address == -1) {
		dla_error("address is invalid\n");
		goto exit;
	}
	if (data->type == DLA_MEM_MC) {
		ret = dla_get_dma_cube_address(executor->driver_context,
					       executor->mem_handles,
					       data->address, data->offset,
					       (void *)address, is_io_tensor);
		if (ret < 0) {
			dla_error("dla_get_cube_address fail\n");
		}
	} else if (data->type == DLA_MEM_CV) {
		ret = dla_get_sram_cube_address(executor->driver_context,
						executor->mem_handles,
						data->address, data->offset,
						(void *)address, is_io_tensor);
		if (ret < 0) {
			dla_error("dla_get_sram_address fail\n");
			goto exit;
		}
	} else {
		dla_error("data type is invalid\n");
		ret = -1;
	}
exit:
	return ret;
}

int io_tensor_record(struct win_executor *executor, addrDesc_t *address,
		     u32 *is_io_tensor)
{
	if (is_io_tensor == NULL) {
		return 0;
	}

	switch (address->flag) {
	case mem_flag_input:
		if (address->bindId > executor->input_num) {
			dla_error("%s %d invalid bind_id %d\n", __func__,
				  __LINE__, address->bindId);
			return -1;
		}
		*is_io_tensor = address->bindId;
		return 1;
	case mem_flag_output:
		if (address->bindId > executor->output_num) {
			dla_error("%s %d invalid bind_id %d\n", __func__,
				  __LINE__, address->bindId);
			return -1;
		}
		*is_io_tensor = address->bindId + executor->input_num;
		return 1;
	default:
		*is_io_tensor = invalid_tensor_idx;
		return 0;
	}
}

static struct processors_interface edma_interface = {
	.name = "EDMA",
	.op_type = DLA_OP_EDMA,
	.tensor_unfold = edma_tensor_unfold,
	.prepare_prog_data = edma_prepare_prog_data,
	.rdma_check = edma_rdma_check,
};
static struct processors_interface conv_interface = {
	.name = "Convolution",
	.op_type = DLA_OP_CONV,
	.tensor_unfold = conv_tensor_unfold,
	.prepare_prog_data = dla_conv_prepare_prog_data,
	.rdma_check = dla_conv_rdma_check,
};
static struct processors_interface sdp_interface = {
	.name = "SDP",
	.op_type = DLA_OP_SDP,
	.tensor_unfold = sdp_tensor_unfold,
	.prepare_prog_data = dla_sdp_prepare_prog_data,
	.rdma_check = dla_sdp_rdma_check,
};
static struct processors_interface pdp_interface = {
	.name = "PDP",
	.op_type = DLA_OP_PDP,
	.tensor_unfold = pdp_tensor_unfold,
	.prepare_prog_data = dla_pdp_prepare_prog_data,
	.rdma_check = dla_pdp_rdma_check,
};
static struct processors_interface rubik_interface = {
	.name = "RUBIK",
	.op_type = DLA_OP_RUBIK,
	.tensor_unfold = rubik_tensor_unfold,
	.prepare_prog_data = dla_rubik_prepare_prog_data,
	.rdma_check = dla_rubik_rdma_check,
};
static struct processors_interface event_sink_interface = {
	.name = "EVENT_SINK",
	.op_type = DLA_OP_EVENT_SINK,
	.tensor_unfold = event_sink_tensor_unfold,
	.prepare_prog_data = dla_event_sink_prepare_prog_data,
	.rdma_check = dla_event_sink_rdma_check,
};
static struct processors_interface event_source_interface = {
	.name = "EVENT_SOURCE",
	.op_type = DLA_OP_EVENT_SOURCE,
	.tensor_unfold = event_source_tensor_unfold,
	.prepare_prog_data = dla_event_source_prepare_prog_data,
	.rdma_check = dla_event_source_rdma_check,
};

static struct processors_interface dsp0_interface = {
	.name = "DSP0",
	.op_type = DLA_KMD_OP_DSP_0,
	.tensor_unfold = dsp0_tensor_unfold,
	.prepare_prog_data = dsp0_prepare_prog_data,
	.rdma_check = NULL,
};

static struct processors_interface dsp1_interface = {
	.name = "DSP1",
	.op_type = DLA_KMD_OP_DSP_1,
	.tensor_unfold = dsp1_tensor_unfold,
	.prepare_prog_data = dsp1_prepare_prog_data,
	.rdma_check = NULL,
};

static struct processors_interface dsp2_interface = {
	.name = "DSP2",
	.op_type = DLA_KMD_OP_DSP_2,
	.tensor_unfold = dsp2_tensor_unfold,
	.prepare_prog_data = dsp2_prepare_prog_data,
	.rdma_check = NULL,
};

static struct processors_interface dsp3_interface = {
	.name = "DSP3",
	.op_type = DLA_KMD_OP_DSP_3,
	.tensor_unfold = dsp3_tensor_unfold,
	.prepare_prog_data = dsp3_prepare_prog_data,
	.rdma_check = NULL,
};

static void frame_done_handler(struct work_struct *w)
{
	int ret;
	struct host_frame_desc *f, *f_tmp;
	struct win_engine *engine =
		container_of(w, struct win_engine, frame_done_work);

	if (engine->frame_done == NULL) {
		dla_error("error:%s %d frame_done is null!\n", __func__,
			  __LINE__);
		return;
	}

	while (true) {
		f = engine->frame_done;
		ret = __atomic_compare_exchange_n(&engine->frame_done, &f, NULL,
						  true, __ATOMIC_RELAXED,
						  __ATOMIC_RELAXED);
		if (ret) {
			break;
		}
	}

	if (f == NULL) {
		dla_error("error:%s %d frame_done is null!\n", __func__,
			  __LINE__);
		return;
	}

	while (f) {
		f_tmp = f;
		f = f->next;
		destroy_frame(f_tmp);
	}

	return;
}

static void npu_task_complete_work(struct work_struct *work)
{
	struct win_engine *engine =
		container_of(work, struct win_engine, complete_work);
	unsigned long flags;
	struct list_head tmp_list;

	INIT_LIST_HEAD(&tmp_list);
	spin_lock_irqsave(&engine->complete_lock, flags);
	list_replace_init(&engine->frame_complete_list, &tmp_list);
	spin_unlock_irqrestore(&engine->complete_lock, flags);

	while (!list_empty(&tmp_list)) {
		struct host_frame_desc *f;
		f = list_first_entry(&tmp_list, struct host_frame_desc,
				     complete_entry);
		list_del(&f->complete_entry);
		if (f->dump_dtim) {
			dump_dtim_to_file(engine, f->tiktok);
		}
		kernel_handle_decref(&f->handle);
	}
}

static void npu_put_dsp_dev(struct win_engine *engine)
{
	int i;

	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		if (engine->dsp_dev[i] == NULL) {
			continue;
		}

#if (NPU_DEV_SIM == NPU_REAL_ENV)
		dsp_detach_sram_dmabuf(engine->dsp_dev[i]);
#endif
		engine->dsp_dev[i] = NULL;
	}
}

static int npu_get_dsp_dev(struct win_engine *engine)
{
	struct nvdla_device *nvdla_dev =
		(struct nvdla_device *)engine->nvdla_dev;
	struct device *npu_dev = &nvdla_dev->pdev->dev;
	int ret;
	int i;

	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		ret = subscribe_dsp_device(nvdla_dev->numa_id, i, npu_dev,
					   &engine->dsp_dev[i]);
		if (ret == -EPROBE_DEFER) {
			engine->dsp_dev[i] = NULL;
			goto err;
		}

#if (NPU_DEV_SIM == NPU_REAL_ENV)
		if (!ret) {
			ret = dsp_attach_sram_dmabuf(
				engine->dsp_dev[i],
				nvdla_dev->spram_bobj->dmabuf);
			if (ret) {
				dla_error(
					"%s, %d, attach dsp sram failed, ret=%d.\n",
					__func__, __LINE__, ret);
				goto err;
			}
		}
#endif
	}
	return 0;

err:
	npu_put_dsp_dev(engine);
	return ret;
}

extern void npu_frame_timeout_tok(struct timer_list *t);
extern void npu_frame_timeout_tik(struct timer_list *t);

#define HOST_NODE_IOVA 0xf0000000
int npu_init_ipc(struct nvdla_device *ndev)
{
	struct win_engine *engine = (struct win_engine *)ndev->win_engine;

#if (NPU_DEV_SIM == NPU_REAL_ENV)
	engine->host_node_iova = HOST_NODE_IOVA;
	engine->host_node = iommu_map_rsv_iova(&ndev->pdev->dev, HOST_NODE_IOVA,
					       sizeof(host_node_t), GFP_KERNEL,
					       IOMMU_MMIO);
	dla_debug("%s, %d, host_node_iova=0x%llx.\n", __func__, __LINE__,
		  engine->host_node_iova);

#else
	engine->host_node =
		dma_alloc_coherent(&ndev->pdev->dev, sizeof(host_node_t),
				   &engine->host_node_iova, GFP_KERNEL);
	dla_debug("%s, %d, host_node_iova=0x%llx.\n", __func__, __LINE__,
		  engine->host_node_iova);
#endif

	if (!engine->host_node) {
		return -ENOMEM;
	}
	host_ipc_initialize((u64)engine->host_node, (u32)engine->host_node_iova,
			    (u64)ndev->emission_base, (u64)ndev->program_base);
	return 0;
}

int npu_uninit_ipc(struct nvdla_device *ndev)
{
	struct win_engine *engine = (struct win_engine *)ndev->win_engine;

	if (!engine) {
		return -EINVAL;
	}
#if (NPU_DEV_SIM == NPU_REAL_ENV)
	iommu_unmap_rsv_iova(&ndev->pdev->dev, engine->host_node,
			     engine->host_node_iova, sizeof(host_node_t));
#else
	dma_free_coherent(&ndev->pdev->dev, sizeof(host_node_t),
			  engine->host_node, engine->host_node_iova);
#endif
	return 0;
}

int win_engine_init(struct nvdla_device *nvdla_dev, void **arg_engine)
{
	struct win_engine *engine;
	int ret;

#if (NPU_DEV_SIM != NPU_MCU_HOST)
	u32 i;
#endif

	if (nvdla_dev->win_engine != NULL) {
		dla_error("%s %d engine inited\n", __func__, __LINE__);
		return -1;
	}
	engine = kzalloc(sizeof(struct win_engine), GFP_KERNEL);
	if (engine == NULL) {
		dla_error("%s %d nomem\n", __func__, __LINE__);
		return -ENOMEM;
	}
	engine->nvdla_dev = (void *)nvdla_dev;
	engine->processors[IDX_EDMA] = &edma_interface;
	engine->processors[IDX_CONV] = &conv_interface;
	engine->processors[IDX_SDP] = &sdp_interface;
	engine->processors[IDX_PDP] = &pdp_interface;
	engine->processors[IDX_RUBIK] = &rubik_interface;
	engine->processors[IDX_KMD_DSP0] = &dsp0_interface;
	engine->processors[IDX_KMD_DSP1] = &dsp1_interface;
	engine->processors[IDX_KMD_DSP2] = &dsp2_interface;
	engine->processors[IDX_KMD_DSP3] = &dsp3_interface;
	engine->processors[IDX_EVENT_SINK] = &event_sink_interface;
	engine->processors[IDX_EVENT_SOURCE] = &event_source_interface;
#if (NPU_DEV_SIM == NPU_MCU_ALONE)
	engine->work_queue = alloc_ordered_workqueue("dump_file", 0);
	for (i = 0; i < NUM_TIKTOK; i++) {
		engine->dump_file_work[i].tiktok = i;
		INIT_WORK(&engine->dump_file_work[i].work, dump_data_to_file);
	}
#elif (NPU_DEV_SIM == NPU_REAL_ENV)
	engine->dump_op_work_queue =
		alloc_ordered_workqueue("dump_op_to_file", 0);
	for (i = 0; i < NUM_OP_TYPE; i++) {
		INIT_WORK(&engine->dump_op_work[i].work, dump_data_to_file);
	}
#endif

	INIT_WORK(&engine->frame_done_work, frame_done_handler);
	INIT_LIST_HEAD(&engine->sched_frame_list);
	sema_init(&engine->runtime_sem, 1);
	spin_lock_init(&engine->executor_lock);
	atomic_set(&engine->is_sending, 0);
	mutex_init(&engine->reset_mutex);

	engine->engine_is_alive = true;
	*arg_engine = (void *)engine;
	INIT_WORK(&engine->complete_work, npu_task_complete_work);
	spin_lock_init(&engine->complete_lock);
	INIT_LIST_HEAD(&engine->frame_complete_list);
	timer_setup(&engine->timer[0], npu_frame_timeout_tik, 0);
	timer_setup(&engine->timer[1], npu_frame_timeout_tok, 0);

	engine->is_event_source_done[0] =
		(u8 *)kzalloc((COMPLETE_EVENT_ID / 8) * NUM_TIKTOK, GFP_KERNEL);
	engine->is_event_source_done[1] =
		engine->is_event_source_done[0] + (COMPLETE_EVENT_ID / 8);
	if (engine->is_event_source_done[0] == NULL) {
		kfree(engine);
		return -ENOMEM;
	}

	ret = npu_get_dsp_dev(engine);
	if (ret) {
		dla_error("%s, %d, get dsp device err, ret=%d.\n", __func__,
			  __LINE__, ret);
		kfree(engine->is_event_source_done[0]);
		kfree(engine);
		return -ENOMEM;
	}
#if NPU_PERF_STATS > 1
	engine->perf_data_buf =
		kzalloc(sizeof(npu_e31_perf_t) * MAX_OP_NUM, GFP_KERNEL);
	if (engine->perf_data_buf == NULL) {
		kfree(engine->is_event_source_done[0]);
		kfree(engine);
		return -ENOMEM;
	}
#endif
	ret = npu_init_ipc(nvdla_dev);
#if (NPU_DEV_SIM == NPU_REAL_ENV)
	if (ret) {
		destroy_workqueue(engine->dump_op_work_queue);
		kfree(engine);
		return -ENOMEM;
	}
	for (i = 0; i < NUM_MAJOR_CORES; i++) {
		engine->major_shm[i] =
			ioremap(E31_MAJOR_DTIM_BASE(i), E31_MAJOR_DTIM_SIZE);
		if (engine->major_shm[i] == NULL) {
			ret = -EIO;
			goto err_ioremap;
		}
		engine->major_mem[i] = kzalloc(E31_MAJOR_DTIM_SIZE, GFP_KERNEL);
		if (engine->major_mem[i] == NULL) {
			ret = -ENOMEM;
			goto err_ioremap;
		}
	}
	engine->aux_mem = kzalloc(E31_PROGRAM_DTIM_SIZE * 2, GFP_KERNEL);
	if (!engine->aux_mem) {
		ret = -ENOMEM;
		goto err_aux_mem;
	}
	engine->master_mem = engine->aux_mem + E31_EMISSION_DTIM_SIZE;

	engine->master_shm = nvdla_dev->emission_base;
	engine->aux_shm = nvdla_dev->program_base;

#else
	if (ret) {
#if (NPU_DEV_SIM == NPU_MCU_ALONE)
		destroy_workqueue(engine->work_queue);
#endif
		kfree(engine);
		return -ENOMEM;
	}
#endif

	return 0;
#if (NPU_DEV_SIM == NPU_REAL_ENV)
err_aux_mem:
err_ioremap:
	for (i = 0; i < NUM_MAJOR_CORES; i++) {
		if (engine->major_shm[i] != NULL) {
			iounmap(engine->major_shm[i]);
			engine->major_shm[i] = NULL;
		}
		if (engine->major_mem[i] != NULL) {
			kfree(engine->major_mem[i]);
			engine->major_mem[i] = NULL;
		}
	}
	if (engine->aux_mem) {
		kfree(engine->aux_mem);
		engine->master_mem = NULL;
		engine->aux_mem = NULL;
	}
	if (!engine->host_node) {
		npu_uninit_ipc(nvdla_dev);
		engine->host_node = NULL;
	}
	destroy_workqueue(engine->dump_op_work_queue);
	kfree(engine);
	return ret;
#endif
}

void win_engine_destroy(struct nvdla_device *nvdla_dev)
{
	struct win_engine *engine = (struct win_engine *)nvdla_dev->win_engine;
#if (NPU_DEV_SIM == NPU_REAL_ENV)
	int i;
#endif

	if (engine) {
		npu_put_dsp_dev(engine);
#if (NPU_DEV_SIM == NPU_MCU_ALONE)
		flush_workqueue(engine->work_queue);
		destroy_workqueue(engine->work_queue);
#elif (NPU_DEV_SIM == NPU_REAL_ENV)
		flush_workqueue(engine->dump_op_work_queue);
		destroy_workqueue(engine->dump_op_work_queue);
		for (i = 0; i < NUM_MAJOR_CORES; i++) {
			if (engine->major_shm[i] != NULL) {
				iounmap(engine->major_shm[i]);
				engine->major_shm[i] = NULL;
			}
			if (engine->major_mem[i] != NULL) {
				kfree(engine->major_mem[i]);
				engine->major_mem[i] = NULL;
			}
		}
#endif
		flush_work(&engine->frame_done_work);
		npu_uninit_ipc(nvdla_dev);

		if (engine->is_event_source_done[0] != NULL) {
			kfree(engine->is_event_source_done[0]);
			engine->is_event_source_done[0] = NULL;
		}
		if (engine->perf_data_buf != NULL) {
			kfree(engine->perf_data_buf);
			engine->perf_data_buf = NULL;
		}
		kfree(engine);
		engine = NULL;
	}
	return;
}

static void free_executor_tensor_data(struct win_executor *executor)
{
	int i;

	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		if (executor->prog_data_buf_bobj[i] != NULL) {
			npu_free_dma_addr(executor, i);
			executor->prog_data_buf_bobj[i] = NULL;
			executor->prog_data_size[i] = 0;
		}
		if (executor->tensor_set[i] != NULL) {
			kfree(executor->tensor_set[i]);
			executor->tensor_set[i] = NULL;
		}
	}
}

#define alloc_op_tensor_set(op)                                              \
	({                                                                   \
		if (cnt[i] < 1) {                                            \
			continue;                                            \
		}                                                            \
		executor->tensor_set[i] =                                    \
			kzalloc(cnt[i] * sizeof(op##_tensor_t), GFP_KERNEL); \
		size[i] = cnt[i] * sizeof(op##_dev_t);                       \
		if (executor->tensor_set[i] == NULL) {                       \
			size[i] = 0;                                         \
			dla_error("%s %d no mem\n", __func__, __LINE__);     \
			ret = -ENOMEM;                                       \
			goto err_tensor;                                     \
		}                                                            \
	})

static int pre_setup_op_tensor(void *pexecutor, struct dla_task *task,
			       u16 input_num, u16 output_num, u16 op_num)
{
	int ret = 0, i, cnt[NUM_OP_TYPE];
	u8 pcer;
	struct win_executor *executor = pexecutor;
	u32 size[NUM_OP_TYPE];

	if (input_num > 0) {
		executor->input_num = input_num;
	} else {
		dla_error("%s %d no input\n", __func__, __LINE__);
		return -EINVAL;
	}
	if (output_num > 0) {
		executor->output_num = output_num;
	} else {
		dla_error("%s %d no output\n", __func__, __LINE__);
		ret = -EINVAL;
		return ret;
	}
	executor->io_mem_handle_size =
		(executor->input_num + executor->output_num) *
		sizeof(addrDesc_t);
	executor->frame_size =
		sizeof(struct host_frame_desc) + executor->io_mem_handle_size +
		(executor->input_num + executor->output_num) * sizeof(u64);
	memset(cnt, 0, sizeof(cnt));
	memset(size, 0, sizeof(size));

	for (i = 0; i < op_num; i++) {
		dla_detail("i=%d, op_type=%d.\n", i,
			   task->common_desc[i].op_type);
		pcer = processor_dla_convert[task->common_desc[i].op_type];
		//the same processor have how many op.
		cnt[pcer]++;
	}

	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		dla_info("%s, %d, i=%d, cnt=%d.\n", __func__, __LINE__, i,
			 cnt[i]);
	}

	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		switch (i) {
		case IDX_EDMA:
			alloc_op_tensor_set(edma);
			break;
		case IDX_CONV:
			alloc_op_tensor_set(conv);
			break;
		case IDX_SDP:
			alloc_op_tensor_set(sdp);
			break;
		case IDX_PDP:
			alloc_op_tensor_set(pdp);
			break;
		case IDX_RUBIK:
			alloc_op_tensor_set(rubik);
			break;
		case IDX_KMD_DSP0:
		case IDX_KMD_DSP1:
		case IDX_KMD_DSP2:
		case IDX_KMD_DSP3:
			alloc_op_tensor_set(dsp);
			break;
		case IDX_EVENT_SINK:
			alloc_op_tensor_set(event_sink);
			break;
		case IDX_EVENT_SOURCE:
			alloc_op_tensor_set(event_source);
			break;
		default:
			ret = -EINVAL;
			dla_info("invalid idx, i = %d.\n", i);
		}
	}

	if (ret) {
		goto err_tensor;
	}

	// alloc dma memory.
	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		if (size[i] <= 0) {
			continue;
		}
		if (i == IDX_CONV) {
			size[i] = cnt[i] * MAX_CONV_PROG_DATA_SIZE;
			dla_debug("single MAX_CONV_PROG_SIZE=%u.\n",
				  MAX_CONV_PROG_SIZE);
		}

		executor->prog_data_size[i] = size[i];
		executor->prog_data_buf_bobj[i] = npu_alloc_dma_addr(
			executor, size[i], &executor->dma_addr[i], i,
			GFP_KERNEL);

		if (executor->prog_data_buf_bobj[i] == NULL) {
			dla_error("%s, %d, i=%d.\n", __func__, __LINE__, i);
			ret = -ENOMEM;
			goto dma_err;
		}
	}
	return 0;

dma_err:
err_tensor:
	free_executor_tensor_data(executor);
	dla_debug("%s, %d.\n", __func__, __LINE__);
	return ret;
}

static int extract_input_output_address(struct win_executor *executor)
{
	int i, input_num = 0, output_num = 0;
	addrDesc_t *address = executor->mem_handles->addrlist->addrDesc;

	dla_debug("num_addresses=%d\n",
		  executor->mem_handles->addrlist->numAddress);
	for (i = 0; i < executor->mem_handles->addrlist->numAddress; i++) {
		if (address[i].flag == mem_flag_input) {
			dla_debug("i=%d,input\n", i);
			input_num++;
		}
		if (address[i].flag == mem_flag_output) {
			dla_debug("i=%d,output\n", i);
			output_num++;
		}
	}

	return pre_setup_op_tensor(executor, executor->task, input_num,
				   output_num,
				   executor->network->num_operations);
}

int create_executor(struct dla_task *task, struct dla_network_desc *network,
		    void **m_executor, struct nvdla_task *mem_handles,
		    void *engine, void *dev, struct user_model *model)
{
	struct win_executor *executor;
	int ret, i;

	executor = kzalloc(sizeof(struct win_executor), GFP_KERNEL);
	if (executor == NULL) {
		dla_error("%s %d no mem\n", __func__, __LINE__);
		return -ENOMEM;
	}

	dla_debug("executor=0x%px executor_size=%ld\n", executor,
		  sizeof(struct win_executor));

	executor->dependency_count =
		kzalloc(network->num_operations, GFP_KERNEL);
	if (executor->dependency_count == NULL) {
		dla_error("%s %d no mem\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err_free0;
	}

	executor->dump_info.op_idx_list =
		kzalloc(MAX_OP_NUM * sizeof(u16), GFP_KERNEL);
	if (executor->dump_info.op_idx_list == NULL) {
		dla_error("%s %d no mem\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err_free1;
	}

	executor->task = task;
	for (i = 0; i < network->num_operations; i++) {
		executor->dependency_count[i] =
			task->common_desc[i].dependency_count;
		dla_debug("%s,%d, i=%d, cnt=%d.\n", __func__, __LINE__, i,
			  executor->dependency_count[i]);
	}

	executor->network = network;
	executor->mem_handles = mem_handles;
	mem_handles->executor = executor;
	executor->engine = (struct win_engine *)engine;
	executor->model = model;
	executor->driver_context = dev;
	memcpy(executor->dsp_fd, model->model_shm->dspFd,
	       sizeof(s32) * DSP_MAX_CORE_NUM);

	ret = extract_input_output_address(executor);
	if (ret < 0) {
		dla_error("Failed to extract input output\n");
		goto err_free2;
	}
	ret = resolve_dsp_data(executor);
	if (ret < 0) {
		dla_error("Failed to resolve dsp, ret = %d.\n", ret);
		goto err_free3;
	}

	ret = generate_small_program(executor);
	if (ret < 0) {
		goto err_free4;
	}
	ret = generate_event_map(executor);
	if (ret < 0) {
		goto err_event;
	}

	executor->state = executor_state_live;

	*m_executor = executor;
	return 0;

err_event:
	kfree(executor->cfg_seq[IDX_START]);
	executor->cfg_seq[IDX_START] = NULL;
err_free4:
	dsp_resource_destroy(executor);
err_free3:
	free_executor_tensor_data(executor);
err_free2:
	kfree(executor->dump_info.op_idx_list);
err_free1:
	kfree(executor->dependency_count);
err_free0:
	kfree(executor);
	*m_executor = NULL;
	return ret;
}

void executor_clearup(void *arg_executor)
{
	struct win_executor *executor = arg_executor;
	int i = 0;

	if (executor->dependency_count != NULL) {
		kfree(executor->dependency_count);
		executor->dependency_count = NULL;
	}
	if (executor->dump_info.op_idx_list != NULL) {
		kfree(executor->dump_info.op_idx_list);
		executor->dump_info.op_idx_list = NULL;
	}
	if (executor->cfg_seq[IDX_START]) {
		kfree(executor->cfg_seq[IDX_START]);
		for (i = IDX_START; i < NUM_OP_TYPE; i++) {
			executor->cfg_seq[i] = NULL;
		}
	}
	if (executor->event_sink_map != NULL) {
		kfree(executor->event_sink_map);
		executor->event_sink_map = NULL;
	}
	if (executor->event_source_map != NULL) {
		kfree(executor->event_source_map);
		executor->event_source_map = NULL;
	}
	dsp_resource_destroy(executor);
	free_executor_tensor_data(executor);

	kfree(executor);
	dla_debug("%s, %d. done.\n", __func__, __LINE__);
}
