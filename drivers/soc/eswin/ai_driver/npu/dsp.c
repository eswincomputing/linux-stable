// Copyright © 2024 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <opendla.h>
#include <dla_err.h>
#include <dla_interface.h>
#include "common.h"
#include "dla_engine_internal.h"
#include "dla_log.h"
#include "internal_interface.h"
#include "dsp_hw.h"
#include "hetero_ioctl.h"
#include "dsp.h"
#include "dla_buffer.h"

static struct dsp_dma_buf *npu_get_dsp_dmabuf(struct file *file, int memfd)
{
	struct dsp_file *dsp_file = NULL;
	struct dsp_dma_buf *dma_buf = NULL;

	if (file == NULL) {
		dla_error("%s, %d, file is null.\n", __func__, __LINE__);
		return NULL;
	}
	dsp_file = file->private_data;
	if (dsp_file == NULL) {
		dla_error("%s, %d, dsp file is null.\n", __func__, __LINE__);
		goto err;
	}
	dma_buf = dsp_get_dma_buf_ex(dsp_file, memfd);
	if (dma_buf == NULL) {
		dma_buf = NULL;
		goto err;
	}
err:
	return dma_buf;
}

static void npu_put_dsp_dmabuf(struct file *file, struct dsp_dma_buf **dma_buf)
{
	struct dsp_file *dsp_file = NULL;

	if (dma_buf == NULL) {
		return;
	}

	if (file == NULL) {
		return;
	}

	dsp_file = file->private_data;

	if (dsp_file == NULL) {
		dla_error("%s, %d, dsp file is null.\n", __func__, __LINE__);
		goto err;
	}
	dla_debug("%s, %d, file count=%ld.\n", __func__, __LINE__,
		  file_count(file));
	dsp_unmap_dmabuf(dsp_file, dma_buf, 1);
err:
	return;
}

static u32 npu_get_dsp_ddr(struct win_executor *executor, int devid, int index)
{
	struct nvdla_task *task;
	struct dsp_dma_buf *dmabuf;
	int memfd;

	task = executor->mem_handles;
	if (task == NULL) {
		dla_error("%s, %d, task is null. error.\n", __func__, __LINE__);
		return 0;
	}

	memfd = task->bobjs[index].fd;
	if (memfd < 0) {
		dla_error("%s, %d, index=%d, memfd is not invalid.\n", __func__,
			  __LINE__, index);
		return 0;
	}
	mutex_lock(&executor->xrray_lock[devid]);
	dmabuf = xa_load(&executor->dsp_ddr_xrray[devid], memfd);
	if (dmabuf) {
		mutex_unlock(&executor->xrray_lock[devid]);
		return dmabuf->dma_addr;
	}

	dmabuf = npu_get_dsp_dmabuf(executor->dsp_file[devid], memfd);
	if (dmabuf == NULL) {
		mutex_unlock(&executor->xrray_lock[devid]);
		dla_error(
			"%s, %d, cannot get index=%d, memfd=%d dmabuf, error.\n",
			__func__, __LINE__, index, memfd);
		return 0;
	}

	xa_store(&executor->dsp_ddr_xrray[devid], memfd, dmabuf, GFP_KERNEL);
	mutex_unlock(&executor->xrray_lock[devid]);
	return dmabuf->dma_addr;
}

static void npu_put_dsp_ddr(struct win_executor *executor, int devid)
{
	unsigned long idx = 0;
	struct dsp_dma_buf *entry = NULL;

	if (executor->dsp_file[devid] == NULL) {
		return;
	}
	mutex_lock(&executor->xrray_lock[devid]);
	xa_for_each(&executor->dsp_ddr_xrray[devid], idx, entry) {
		xa_erase(&executor->dsp_ddr_xrray[devid], idx);
		npu_put_dsp_dmabuf(executor->dsp_file[devid], &entry);
	}
	mutex_unlock(&executor->xrray_lock[devid]);
}

static int dsp_tensor_unfold(struct win_executor *executor, int op_idx,
			     union dla_operation_container *operation_desc,
			     union dla_surface_container *surface_desc,
			     void *tensor, int idx, int op_type)
{
	struct dsp_op_desc *dsp_op = NULL;
	dsp_tensor_t *tensor_set = (dsp_tensor_t *)tensor;
	dsp_tensor_t *dsp_tensor;
	struct dsp_surface_desc *surface = &surface_desc->dsp_surface;
	int dsp_dev_idx = op_type - DLA_KMD_OP_DSP_0;
	u32 mem_id, offset;
	int flat1_size;
	int ret, i;
	dsp_dev_t *vaddr = (dsp_dev_t *)executor->prog_data_buf_bobj[op_type];
	dsp_dev_t *dsp = (dsp_dev_t *)&vaddr[idx];

	dsp_op = &operation_desc->dsp_op;

	dsp_tensor = &tensor_set[idx];
	dla_debug("%s, %d, op_idx=%d, idx=%d, dsp_tensor=0x%px.\n", __func__,
		  __LINE__, op_idx, idx, dsp_tensor);

	if (tensor_set != NULL && dsp_tensor != NULL &&
	    dsp_tensor->have_unfold) {
		dsp->npu_info.current_op_idx = op_idx;
		ret = load_operator(executor->engine->dsp_dev[dsp_dev_idx],
				    NULL, dsp_op->operator_name,
				    &dsp_tensor->handle);
		if (ret != 0) {
			dla_error(
				"err:dsp load_operator failed,ret:%d dsp_dev_idx:%d idx:%d\n",
				ret, dsp_dev_idx, idx);
			return -1;
		}
		dla_debug(
			"%s, %d, op_idx=%d, dsp_tensor=0x%px, handle=0x%llx.\n",
			__func__, __LINE__, op_idx, dsp_tensor,
			dsp_tensor->handle);
		return 0;
	}

	flat1_size = (dsp_op->buffer_cnt_cfg + dsp_op->buffer_cnt_input +
		      dsp_op->buffer_cnt_output) *
		     sizeof(es_dsp_buffer);
	flat1_size += sizeof(struct es_dsp_flat1_desc);
	flat1_size = round_up(flat1_size, 64);

	dla_debug("mem_id=%d,flat1_size=%d\n", dsp_op->mem_id, flat1_size);

	mem_id = dsp_op->mem_id;
	offset = dsp_op->offset;

	dsp_tensor = &tensor_set[idx];
	dla_debug("%s, %d, op_type=%d.\n", __func__, __LINE__, op_type);
	dla_debug("%s, %d, idx=%d, tensor_set=0x%px.\n", __func__, __LINE__,
		  idx, tensor_set);
	dla_debug("%s, %d, idx=%d, dsp_tensor=0x%px.\n", __func__, __LINE__,
		  idx, dsp_tensor);
	for (i = 0; i < dsp_op->buffer_cnt_input; i++) {
		ret = read_input_address(executor, &surface->src_data[i],
					 (void *)&dsp_tensor->src_base_addr[i],
					 &dsp_tensor->src_is_io_tensor[i]);
		if (ret != 0) {
			dla_error("%s %d bad memory type %d\n", __func__,
				  __LINE__, surface->src_data[i].type);
			return -1;
		}
		if (dsp_tensor->src_is_io_tensor[i] != invalid_tensor_idx) {
			executor->dsp_all_inout
				[dsp_dev_idx][dsp_tensor->src_is_io_tensor[i]]++;
			executor->dsp_iobuf_cnt[dsp_dev_idx]++;
		}
		dla_debug("i=%d src_base_addr=0x%llx src_is_io_tensor=0x%x\n",
			  i, dsp_tensor->src_base_addr[i],
			  dsp_tensor->src_is_io_tensor[i]);
	}

	for (i = 0; i < dsp_op->buffer_cnt_output; i++) {
		ret = read_input_address(executor, &surface->dst_data[i],
					 (void *)&dsp_tensor->dst_base_addr[i],
					 &dsp_tensor->dst_is_io_tensor[i]);
		if (ret != 0) {
			dla_error("%s %d bad memory type %d\n", __func__,
				  __LINE__, surface->dst_data[i].type);
			return -1;
		}
		if (dsp_tensor->dst_is_io_tensor[i] != invalid_tensor_idx) {
			executor->dsp_all_inout
				[dsp_dev_idx][dsp_tensor->dst_is_io_tensor[i]]++;
			executor->dsp_iobuf_cnt[dsp_dev_idx]++;
		}

		dla_debug(
			"i=%d op_idx=%d, type=%d, dst_base_addr=0x%llx dst_is_io_tensor=0x%x\n",
			i, op_idx, surface->dst_data[i].type,
			dsp_tensor->dst_base_addr[i],
			dsp_tensor->dst_is_io_tensor[i]);
	}

	dsp_tensor->have_unfold = 1;
	return flat1_size;
}

int dsp0_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx)
{
	return dsp_tensor_unfold(executor, op_idx, operation_desc, surface_desc,
				 tensor, idx, DLA_KMD_OP_DSP_0);
}

int dsp1_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx)
{
	return dsp_tensor_unfold(executor, op_idx, operation_desc, surface_desc,
				 tensor, idx, DLA_KMD_OP_DSP_1);
}

int dsp2_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx)
{
	return dsp_tensor_unfold(executor, op_idx, operation_desc, surface_desc,
				 tensor, idx, DLA_KMD_OP_DSP_2);
}

int dsp3_tensor_unfold(struct win_executor *executor, int op_idx,
		       union dla_operation_container *operation_desc,
		       union dla_surface_container *surface_desc, void *tensor,
		       int idx)
{
	return dsp_tensor_unfold(executor, op_idx, operation_desc, surface_desc,
				 tensor, idx, DLA_KMD_OP_DSP_3);
}

static int processor_dsp_program(struct win_executor *executor, int idx,
				 u16 op_idx,
				 union dla_operation_container *op_desc,
				 union dla_surface_container *surf_desc,
				 int op_type)
{
	struct dsp_op_desc *dsp_op;
	u32 param_data_loac_offset = offsetof(struct dsp_op_desc, dsp_core_id);
	dsp_tensor_t *tensor_set = executor->tensor_set[op_type];
	dsp_tensor_t *dsp_tensor = &tensor_set[idx];
	dsp_dev_t *vaddr = (dsp_dev_t *)executor->prog_data_buf_bobj[op_type];
	dsp_dev_t *dsp = (dsp_dev_t *)&vaddr[idx];
	struct es_dsp_flat1_desc *flat1_desc;
	es_dsp_buffer *dsp_buffer;
	struct dsp_surface_desc *surface;
	u32 param_addr_offset = 0;
	u32 dsp_buf_idx = 0;
	u32 offset;
	es_dsp_h2d_msg h2d_msg;
	int i, io_cnt;
	int dev_id = op_type - DLA_KMD_OP_DSP_0;

	dsp_op = &op_desc->dsp_op;
	surface = &surf_desc->dsp_surface;

	offset = dsp_op->offset;

	flat1_desc = (struct es_dsp_flat1_desc *)dsp_tensor->flat1_vaddr;

	dla_debug("%s, %d, flat1_desc=0x%px.\n", __func__, __LINE__,
		  flat1_desc);
	dla_debug("%s, %d, cnt_cfg=%d.\n", __func__, __LINE__,
		  dsp_op->buffer_cnt_cfg);
	dla_debug("%s, %d, cnt_input=%d.\n", __func__, __LINE__,
		  dsp_op->buffer_cnt_input);
	dla_debug("%s, %d, cnt_output=%d.\n", __func__, __LINE__,
		  dsp_op->buffer_cnt_output);
	flat1_desc->num_buffer = dsp_op->buffer_cnt_cfg +
				 dsp_op->buffer_cnt_input +
				 dsp_op->buffer_cnt_output;

	dla_debug("%s, %d op_handle=0x%llx.\n", __func__, __LINE__,
		  dsp_tensor->handle);
	dsp_set_flat_func(flat1_desc, dsp_tensor->handle);

	flat1_desc->input_index = dsp_op->buffer_cnt_cfg;
	flat1_desc->output_index =
		dsp_op->buffer_cnt_cfg + dsp_op->buffer_cnt_input;
	dsp_buffer = flat1_desc->buffers;

	for (i = 0; i < dsp_op->buffer_cnt_cfg; i++) {
		dsp_buffer[i].addr = dsp_tensor->data_dma_addr +
				     param_data_loac_offset + offset +
				     param_addr_offset;
		dsp_buffer[i].size = dsp_op->buffer_size[i];
		param_addr_offset += dsp_op->buffer_size[i];
		dla_debug("%s, %d cfg=%d , addr=0x%x, size=0x%x.\n", __func__,
			  __LINE__, i, dsp_buffer[i].addr, dsp_buffer[i].size);
	}

	dsp_buf_idx = dsp_op->buffer_cnt_cfg;

	for (i = 0; i < dsp_op->buffer_cnt_input; i++) {
		if (dsp_tensor->src_is_io_tensor[i] == invalid_tensor_idx) {
			if (surface->src_data[i].type == DLA_MEM_MC) {
				dsp_buffer[dsp_buf_idx + i].addr =
					npu_get_dsp_ddr(
						executor, dev_id,
						surface->src_data[i].address) +
					surface->src_data[i].offset;
			} else if (surface->src_data[i].type == DLA_MEM_CV) {
				dsp_buffer[dsp_buf_idx + i]
					.addr = dsp_get_sram_iova_by_addr(
					executor->engine->dsp_dev[dev_id],
					dsp_tensor->src_base_addr[i]);
				dla_debug("get dsp sram iova = 0x%x.\n",
					  dsp_buffer[dsp_buf_idx + i].addr);
			}
		} else {
			dsp_buffer[dsp_buf_idx + i].addr =
				surface->src_data[i].offset;
			io_cnt =
				executor->dsp_io[dev_id]
						[dsp_tensor->src_is_io_tensor[i]]
							.io_cnt;
			executor->dsp_io[dev_id][dsp_tensor->src_is_io_tensor[i]]
				.virt[io_cnt] =
				(u64)&dsp_buffer[dsp_buf_idx + i].addr;
			executor->dsp_io[dev_id][dsp_tensor->src_is_io_tensor[i]]
				.offset[io_cnt] = surface->src_data[i].offset;
			executor->dsp_io[dev_id][dsp_tensor->src_is_io_tensor[i]]
				.io_cnt++;
		}
		dsp_buffer[dsp_buf_idx + i].size = surface->src_data[i].size;
	}

	dsp_buf_idx += dsp_op->buffer_cnt_input;
	for (i = 0; i < dsp_op->buffer_cnt_output; i++) {
		if (dsp_tensor->dst_is_io_tensor[i] == invalid_tensor_idx) {
			if (surface->dst_data[i].type == DLA_MEM_MC) {
				dla_debug(
					"%s, %d, output dst mc, address index=%d.\n",
					__func__, __LINE__,
					surface->dst_data[i].address);
				dsp_buffer[dsp_buf_idx + i].addr =
					npu_get_dsp_ddr(
						executor, dev_id,
						surface->dst_data[i].address) +
					surface->dst_data[i].offset;
				dla_debug("%s, %d, i=%d, addr=0x%x.\n",
					  __func__, __LINE__, i,
					  dsp_buffer[dsp_buf_idx + i].addr);
			} else if (surface->dst_data[i].type == DLA_MEM_CV) {
				dsp_buffer[dsp_buf_idx + i]
					.addr = dsp_get_sram_iova_by_addr(
					executor->engine->dsp_dev[dev_id],
					dsp_tensor->dst_base_addr[i]);
				dla_debug(
					"%s, %d, cv. output offset=0x%x, addr=0x%x.\n",
					__func__, __LINE__,
					surface->dst_data[i].offset,
					dsp_buffer[dsp_buf_idx + i].addr);
			}
			dla_debug(
				"%s, %d, i=%d, outidx=%d, buffer addr=0x%x, offset=0x%x. size=0x%x.\n",
				__func__, __LINE__, i, dsp_buf_idx + i,
				dsp_buffer[dsp_buf_idx + i].addr,
				surface->dst_data[i].offset,
				dsp_buffer[dsp_buf_idx + i].size);
		} else {
			dsp_buffer[dsp_buf_idx + i].addr =
				surface->dst_data[i].offset;
			io_cnt =
				executor->dsp_io[dev_id]
						[dsp_tensor->dst_is_io_tensor[i]]
							.io_cnt;
			executor->dsp_io[dev_id][dsp_tensor->dst_is_io_tensor[i]]
				.virt[io_cnt] =
				(u64)&dsp_buffer[dsp_buf_idx + i].addr;
			executor->dsp_io[dev_id][dsp_tensor->dst_is_io_tensor[i]]
				.offset[io_cnt] = surface->dst_data[i].offset;
			dla_debug(
				"%s, %d, io_cnt=%d, dsp_io=%d, dst=0x%llx.\n",
				__func__, __LINE__, io_cnt,
				dsp_tensor->dst_is_io_tensor[i],
				executor->dsp_io[dev_id]
						[dsp_tensor->dst_is_io_tensor[i]]
							.virt[io_cnt]);
			dla_debug("%s, %d, output %d, offset=0x%x.\n", __func__,
				  __LINE__, dsp_buf_idx + i,
				  dsp_buffer[dsp_buf_idx + i].addr);
			executor->dsp_io[dev_id][dsp_tensor->dst_is_io_tensor[i]]
				.io_cnt++;
		}
		dsp_buffer[dsp_buf_idx + i].size = surface->dst_data[i].size;
	}

	h2d_msg.command = DSP_CMD_FLAT1;
	h2d_msg.reserved = op_idx;
	h2d_msg.allow_eval = 1;
	h2d_msg.poll_mode = 1;
	h2d_msg.sync_cache = 0;

	h2d_msg.iova_ptr = dsp_tensor->flat1_dma_addr;
	memcpy((void *)&dsp->npu_info.dsp_eval_param, (void *)&h2d_msg,
	       sizeof(h2d_msg));
	return 0;
}

int dsp0_prepare_prog_data(struct win_executor *executor, int rdma, int idx,
			   u16 op_idx, union dla_operation_container *op_desc,
			   union dla_surface_container *surf_desc)
{
	int ret;
	dla_debug("%s, %d, op_idx=%d.\n", __func__, __LINE__, op_idx);
	ret = processor_dsp_program(executor, idx, op_idx, op_desc, surf_desc,
				    DLA_KMD_OP_DSP_0);
	return ret;
}

int dsp1_prepare_prog_data(struct win_executor *executor, int rdma, int idx,
			   u16 op_idx, union dla_operation_container *op_desc,
			   union dla_surface_container *surf_desc)
{
	int ret;

	ret = processor_dsp_program(executor, idx, op_idx, op_desc, surf_desc,
				    DLA_KMD_OP_DSP_1);
	return ret;
}

int dsp2_prepare_prog_data(struct win_executor *executor, int rdma, int idx,
			   u16 op_idx, union dla_operation_container *op_desc,
			   union dla_surface_container *surf_desc)
{
	int ret;

	ret = processor_dsp_program(executor, idx, op_idx, op_desc, surf_desc,
				    DLA_KMD_OP_DSP_2);
	return ret;
}

int dsp3_prepare_prog_data(struct win_executor *executor, int rdma, int idx,
			   u16 op_idx, union dla_operation_container *op_desc,
			   union dla_surface_container *surf_desc)
{
	int ret;

	ret = processor_dsp_program(executor, idx, op_idx, op_desc, surf_desc,
				    DLA_KMD_OP_DSP_3);
	return ret;
}

static int npu_acquire_dsp_file(struct win_executor *executor)
{
	struct file *file = NULL;
	struct dsp_file *dsp_file = NULL;
	int i;
	int filefd;

	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		if (executor->dsp_fd[i] < 3) {
			executor->dsp_file[i] = NULL;
			continue;
		}
		filefd = executor->dsp_fd[i];
		file = fget(filefd);
		if (file == NULL) {
			dla_error(
				"%s, %d, cannot find dsp%d dsp_file fd=%d file, error\n",
				__func__, __LINE__, i, filefd);
			executor->dsp_file[i] = NULL;
			continue;
		}

		dsp_file = file->private_data;
		if (dsp_file == NULL) {
			dla_error("%s, %d, dsp file(%d) is null.\n", __func__,
				  __LINE__, filefd);
			executor->dsp_file[i] = NULL;
			fput(file);
			continue;
		}
		executor->dsp_file[i] = file;
	}
	return 0;
}

static void npu_release_dsp_file(struct win_executor *executor)
{
	int i;

	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		if (executor->dsp_file[i] == NULL) {
			continue;
		}
		if (file_count(executor->dsp_file[i])) {
			fput(executor->dsp_file[i]);
		}
		executor->dsp_file[i] = NULL;
	}
}

int resolve_dsp_data(struct win_executor *executor)
{
	int i, j, op_num;
	dsp_tensor_t *tensor_set;
	dsp_tensor_t *dsp_tensor;
	struct processors_interface *pcer_interface;
	struct dla_task *task = executor->task;
	u32 flat1_total_len[DSP_MAX_CORE_NUM] = { 0 };
	int dsp_tensor_idx[DSP_MAX_CORE_NUM] = { 0 };
	int op_type, op_idx, size;
	int dsp_op_cnt = 0;
	struct device *dsp_dev = NULL;
	int ret;
	dma_addr_t dma_set, data_dma_set;
	void *flat_vaddr_set = NULL;
	int fd;

	ret = npu_acquire_dsp_file(executor);

	op_num = executor->network->num_operations;
	for (i = 0; i < op_num; i++) {
		op_type = executor->task->common_desc[i].op_type;
		op_idx = executor->task->common_desc[i].index;
		if ((op_type == DLA_KMD_OP_DSP_0) ||
		    (op_type == DLA_KMD_OP_DSP_1) ||
		    (op_type == DLA_KMD_OP_DSP_2) ||
		    (op_type == DLA_KMD_OP_DSP_3)) {
			tensor_set = executor->tensor_set[op_type];
			dsp_tensor =
				&tensor_set[dsp_tensor_idx[op_type -
							   DLA_KMD_OP_DSP_0]];
			pcer_interface = executor->engine->processors[op_type];

			j = dsp_tensor_idx[op_type - DLA_KMD_OP_DSP_0];
			size = pcer_interface->tensor_unfold(
				executor, op_idx, &task->op_desc[op_idx],
				&task->surface_desc[op_idx], tensor_set,
				j);  //get dsp flat1 len and mem_id
			if (size < 0) {
				dla_error("err:get dsp flat1 len failed!\n");
				return -1;
			}

			dsp_tensor->flat1_size = size;
			dsp_tensor->flat1_addr_offset =
				flat1_total_len[op_type - DLA_KMD_OP_DSP_0];
			flat1_total_len[op_type - DLA_KMD_OP_DSP_0] +=
				dsp_tensor->flat1_size;

			dsp_tensor_idx[op_type - DLA_KMD_OP_DSP_0]++;
			dsp_op_cnt++;
		}
	}
	executor->dsp_op_num = dsp_op_cnt;
	if (!dsp_op_cnt) {
		dla_detail("no dsp op\n");
		return 0;
	}

	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		xa_init_flags(&executor->dsp_ddr_xrray[i], GFP_KERNEL);
		mutex_init(&executor->xrray_lock[i]);

		if (executor->dsp_iobuf_cnt[i] == 0) {
			continue;
		}
		executor->dsp_iobuf_virt[i] =
			kzalloc(executor->dsp_iobuf_cnt[i] *
					(sizeof(u64) + sizeof(u32)),
				GFP_KERNEL);
		if (executor->dsp_iobuf_virt[i] == NULL) {
			dla_error(
				"%s, %d, alloc dsp iobuf vir for device %d error.\n",
				__func__, __LINE__, i);
			goto alloc_err;
		}
		size = 0;
		executor->dsp_iobuf_offset[i] =
			executor->dsp_iobuf_virt[i] +
			executor->dsp_iobuf_cnt[i] * sizeof(u64);
		for (j = 0; j < DSP_KERNEL_MAX_INOUT_TENSOR_NUM; j++) {
			if (executor->dsp_all_inout[i][j] != 0) {
				executor->dsp_io[i][j].virt =
					(u64 *)(executor->dsp_iobuf_virt[i] +
						size * sizeof(u64));
				executor->dsp_io[i][j].offset =
					(u32 *)(executor->dsp_iobuf_offset[i] +
						size * sizeof(u32));
				dla_debug("%s, %d, virt[0]=0x%px.\n", __func__,
					  __LINE__,
					  executor->dsp_io[i][j].virt);
				size += executor->dsp_all_inout[i][j];
			}
		}
	}
	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		if (dsp_tensor_idx[i] == 0) {
			continue;
		}
		dsp_dev = executor->engine->dsp_dev[i];
		if (dsp_dev == NULL) {
			dla_error("%s, %d, dsp device%d is NULL, err.\n",
				  __func__, __LINE__, i);
			ret = -ENODEV;
			goto err_dev;
		}

		executor->dsp_flat1_set_vaddr[i] = dma_alloc_coherent(
			dsp_dev, flat1_total_len[i],
			&executor->dsp_flat1_set_dma[i], GFP_KERNEL);
		if (executor->dsp_flat1_set_vaddr[i] == NULL) {
			dla_error(
				"%s, %d, dma alloc cohernet for dsp dev%d error.\n",
				__func__, __LINE__, i);
			goto alloc_err;
		}
		dla_debug("%s, %d, i=%d, flat vaddr=0x%px.\n", __func__,
			  __LINE__, i, executor->dsp_flat1_set_vaddr[i]);
		executor->dsp_flat1_set_size[i] = flat1_total_len[i];
		/*get the fd of dsp param buffer*/
		fd = dla_data_get_fd(executor->driver_context,
				     executor->mem_handles, NULL,
				     executor->network->op_config_index);
		if (fd < 0) {
			dla_error(
				"err:dla_data_get_fd failed,i=%d op_config_index=%d!\n",
				i, executor->network->op_config_index);
			goto alloc_err;
		}
		executor->model_dsp_dmabuf[i] =
			npu_get_dsp_dmabuf(executor->dsp_file[i], fd);
		if (executor->model_dsp_dmabuf[i] == NULL) {
			dla_error("err:import dsp data dmabuf error!\n");
			goto alloc_err;
		}
	}

	for (op_type = DLA_KMD_OP_DSP_0; op_type <= DLA_KMD_OP_DSP_3;
	     op_type++) {
		if (dsp_tensor_idx[op_type - DLA_KMD_OP_DSP_0] == 0) {
			continue;
		}
		tensor_set = executor->tensor_set[op_type];
		dma_set =
			executor->dsp_flat1_set_dma[op_type - DLA_KMD_OP_DSP_0];
		flat_vaddr_set =
			executor->dsp_flat1_set_vaddr[op_type -
						      DLA_KMD_OP_DSP_0];
		data_dma_set =
			executor->model_dsp_dmabuf[op_type - DLA_KMD_OP_DSP_0]
				->dma_addr;

		for (i = 0; i < dsp_tensor_idx[op_type - DLA_KMD_OP_DSP_0];
		     i++) {
			dsp_tensor = &tensor_set[i];
			dsp_tensor->flat1_dma_addr =
				dma_set + dsp_tensor->flat1_addr_offset;
			dsp_tensor->flat1_vaddr =
				flat_vaddr_set + dsp_tensor->flat1_addr_offset;
			dsp_tensor->data_dma_addr = data_dma_set;
		}
	}
	dla_debug("%s, %d, done.\n", __func__, __LINE__);
	return 0;

alloc_err:
	npu_release_dsp_file(executor);
err_dev:
	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		dsp_dev = executor->engine->dsp_dev[i];
		if (dsp_dev == NULL) {
			continue;
		}
		if (executor->dsp_flat1_set_vaddr[i] != NULL) {
			dma_free_coherent(dsp_dev,
					  executor->dsp_flat1_set_size[i],
					  executor->dsp_flat1_set_vaddr[i],
					  executor->dsp_flat1_set_dma[i]);
			executor->dsp_flat1_set_vaddr[i] = NULL;
			executor->dsp_flat1_set_dma[i] = 0;
		}
		if (executor->model_dsp_dmabuf[i] != NULL) {
			npu_put_dsp_dmabuf(executor->dsp_file[i],
					   &executor->model_dsp_dmabuf[i]);
			executor->model_dsp_dmabuf[i] = NULL;
		}
		if (executor->dsp_iobuf_virt[i] != NULL) {
			kfree(executor->dsp_iobuf_virt[i]);
			executor->dsp_iobuf_virt[i] = NULL;
		}
		xa_destroy(&executor->dsp_ddr_xrray[i]);
	}
	return -EINVAL;
}

int npu_set_dsp_iobuf(struct win_executor *executor, struct host_frame_desc *f)
{
	int i, j, k;
	int fd;
	addrDesc_t *address = f->io_tensor_list;
	u32 *tmp;
	u32 offset = 0;

	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		if (executor->dsp_iobuf_cnt[i] == 0) {
			continue;
		}
		for (j = 0; j < DSP_KERNEL_MAX_INOUT_TENSOR_NUM; j++) {
			if (executor->dsp_all_inout[i][j] == 0) {
				continue;
			}
			fd = address[j].devBuf.memFd;

			f->dsp_io_dmabuf[i][j] =
				npu_get_dsp_dmabuf(executor->dsp_file[i], fd);
			if (f->dsp_io_dmabuf[i][j] == NULL) {
				dla_error(
					"%s, %d, npu get dsp%d dmabuf-%d error.\n ",
					__func__, __LINE__, i, j);
				return -EINVAL;
			}
			dla_debug("%s, %d dspio=%d.\n", __func__, __LINE__,
				  executor->dsp_io[i][j].io_cnt);
			for (k = 0; k < executor->dsp_io[i][j].io_cnt; k++) {
				dla_debug("%s, %d, virt=0x%llx.\n", __func__,
					  __LINE__,
					  executor->dsp_io[i][j].virt[k]);
				tmp = (u32 *)executor->dsp_io[i][j].virt[k];
				offset = executor->dsp_io[i][j].offset[k];
				dla_debug(
					"%s, %d, offset=0x%x, dma addr=0x%x.\n",
					__func__, __LINE__, offset,
					f->dsp_io_dmabuf[i][j]->dma_addr);
				*tmp = offset +
				       f->dsp_io_dmabuf[i][j]->dma_addr;
				dla_debug("tmp content=0x%x.\n", *tmp);
			}
		}
	}
	return 0;
}

static int npu_unload_dsp_op(struct win_executor *executor, int idx,
			     int op_type)
{
	dsp_tensor_t *tensor_set = executor->tensor_set[op_type];
	dsp_tensor_t *dsp_tensor = NULL;
	int dev_id = op_type - DLA_KMD_OP_DSP_0;
	int ret;

	if (executor->engine->dsp_dev[dev_id] == NULL) {
		dla_error("%s, %d, unload op for dsp%d, idx=%d.\n", __func__,
			  __LINE__, dev_id, idx);
		return -EINVAL;
	}
	if (tensor_set == NULL) {
		dla_error("%s, %d, tensor set is null, idx=%d, op_type=%d.\n",
			  __func__, __LINE__, idx, op_type);
		return -EINVAL;
	}
	dsp_tensor = &tensor_set[idx];
	if (dsp_tensor == NULL || (dsp_tensor->handle == 0)) {
		dla_error(
			"%s, %d, idx=%d, op_type=%d.\n dsp tensor null or handle is 0, err.\n",
			__func__, __LINE__, idx, op_type);
		return -EINVAL;
	}
	ret = unload_operator(executor->engine->dsp_dev[dev_id],
			      dsp_tensor->handle);
	return ret;
}

void destroy_frame_dsp_info(struct win_executor *executor,
			    struct host_frame_desc *f)
{
	int i, j;

	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		if (executor->dsp_iobuf_cnt[i] == 0) {
			continue;
		}
		for (j = 0; j < DSP_KERNEL_MAX_INOUT_TENSOR_NUM; j++) {
			if (f->dsp_io_dmabuf[i][j] == NULL) {
				continue;
			}
			npu_put_dsp_dmabuf(executor->dsp_file[i],
					   &f->dsp_io_dmabuf[i][j]);
			f->dsp_io_dmabuf[i][j] = NULL;
		}
	}
}

void dsp_resource_destroy(struct win_executor *executor)
{
	int i;
	struct device *dsp_dev = NULL;
	int pcer, op_num;

	if (executor->dsp_op_num == 0) {
		goto put_dsp_file;
	}

	for (i = 0; i < DSP_MAX_CORE_NUM; i++) {
		dsp_dev = executor->engine->dsp_dev[i];

		if (dsp_dev == NULL) {
			continue;
		}

		npu_put_dsp_ddr(executor, i);
		xa_destroy(&executor->dsp_ddr_xrray[i]);

		if (executor->dsp_flat1_set_vaddr[i] != NULL) {
			dma_free_coherent(dsp_dev,
					  executor->dsp_flat1_set_size[i],
					  executor->dsp_flat1_set_vaddr[i],
					  executor->dsp_flat1_set_dma[i]);
			executor->dsp_flat1_set_vaddr[i] = NULL;
			executor->dsp_flat1_set_dma[i] = 0;
		}
		if (executor->model_dsp_dmabuf[i] != NULL) {
			npu_put_dsp_dmabuf(executor->dsp_file[i],
					   &executor->model_dsp_dmabuf[i]);
			executor->model_dsp_dmabuf[i] = NULL;
		}

		if (executor->dsp_iobuf_virt[i] != NULL) {
			kfree(executor->dsp_iobuf_virt[i]);
			executor->dsp_iobuf_virt[i] = NULL;
		}
	}

	for (pcer = IDX_KMD_DSP0; pcer < IDX_KMD_DSP3; pcer++) {
		op_num = executor->op_num[pcer];
		for (i = 0; i < op_num; i++) {
			npu_unload_dsp_op(executor, i, pcer);
		}
	}
put_dsp_file:
	npu_release_dsp_file(executor);
}
