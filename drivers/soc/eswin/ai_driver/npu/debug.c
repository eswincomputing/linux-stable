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

#include <linux/shmem_fs.h>
#include <linux/pipe_fs_i.h>
#include <linux/mount.h>
#include <linux/fs_struct.h>
#include <linux/task_work.h>
#include <asm/errno.h>
#include <linux/dma-mapping.h>
#include <asm-generic/barrier.h>
#include <opendla.h>
#include <dla_err.h>
#include <dla_interface.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/list.h>
#include <nvdla_linux.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include "hetero_ioctl.h"
#include "common.h"
#include "dla_engine_internal.h"
#include "dla_engine.h"
#include "nvdla_linux.h"
#include "dla_log.h"
#include "nvdla_interface.h"
#include "opendla_initial.h"
#include "debug.h"
#include "npu_spram.h"
#include "post_drp.h"
#include "internal_interface.h"
#include "dla_buffer.h"
#include "hetero_host.h"
#include <crypto/hash.h>
#include "md5.h"

char g_op_names[][16] = { "OP_EDMA", "OP_CONV",	 "OP_SDP", "OP_PDP",
			  "OP_CDP",  "OP_RUBIK", "OP_BDMA" };

static int32_t max_index = 0;

static md5_container_t md5_dump[MAX_OP_NUM] = { 0 };

static int npu_dump_data_enable = 1;
module_param(npu_dump_data_enable, int, 0644);

static int npu_dump_op_num_start = 0;
module_param(npu_dump_op_num_start, int, 0644);

int npu_dump_data_len = 16;
module_param(npu_dump_data_len, int, 0644);

static int npu_dump_data_start = 0;
module_param(npu_dump_data_start, int, 0644);

static void md5_to_hex(char *out, char *ck_data)
{
	int i;
	for (i = 0; i < ((MD5_LENGTH - 1) / 2); i++) {
		unsigned char c = ck_data[i];
		*out++ =
			'0' + ((c & 0xf0) >> 4) + (c >= 0xa0) * ('a' - '9' - 1);
		*out++ = '0' + (c & 0x0f) +
			 ((c & 0x0f) >= 0x0a) * ('a' - '9' - 1);
	}
}

static void calc_md5(char *md5, u8 *data, u32 len)
{
	u32 ck_len;
	u8 *ck_data;
	struct crypto_shash *tfm;
	md5[MD5_LENGTH - 1] = '\0';

	tfm = crypto_alloc_shash("md5", 0, 0);
	if (IS_ERR(tfm)) {
		dla_error("failed to call crypto alloc shash.\n");
		return;
	}

	ck_len = crypto_shash_digestsize(tfm);
	ck_data = kzalloc(ck_len, GFP_KERNEL);
	if (ck_data == NULL) {
		dla_error("kzalloc mem failed.\n");
		goto out;
	}

	if (crypto_shash_tfm_digest(tfm, data, len, ck_data)) {
		dla_error("failed to call crypto_shash_tfm_disgest.\n");
	} else {
		md5_to_hex(md5, ck_data);
	}

	kfree(ck_data);
out:
	crypto_free_shash(tfm);
	return;
}

void dump_data(const void *buf, const u32 len)
{
#ifdef NPU_DBG
	int i = 0;
	const unsigned char *cbuf = buf;

	dla_debug("=======================\n");
	dla_debug("ver01: buf=0x%px, len=%u.\n", buf, len);
	for (i = 0; i < len; i++) {
		if (i % 16 == 0) {
			dla_debug("\n0x%04x: ", i);
		}
		dla_debug(KERN_CONT "%02X ", cbuf[i]);
	}
	dla_debug("========================\n");
#endif
}

int dump2file(const char *fname, const void *data, size_t len)
{
	loff_t pos = 0;
	struct file *filep = NULL;
	dla_debug("dump buf %px to file %s, len=%lu.\n", data, fname, len);

	if (filep == NULL) {
		filep = filp_open(fname, O_RDWR | O_CREAT, 0644);
	}

	if (IS_ERR(filep)) {
		dla_error("Open file %s error\n", fname);
		return -1;
	}

	kernel_write(filep, data, len, &pos);

	if (filep != NULL) {
		filp_close(filep, NULL);
	}
	dla_debug("dump2file done.\n");
	return 0;
}

int dumpMD5(const char *fname, struct win_executor *executor, uint32_t index)
{
	int i;
	int len = 0;
	char content[512] = { 0 };
	loff_t pos = 0;
	struct file *filep = NULL;

	if (filep == NULL) {
		filep = filp_open(fname, O_RDWR | O_CREAT | O_APPEND, 0644);
	}

	if (IS_ERR(filep)) {
		dla_error("Open file %s error\n", fname);
		return -1;
	}

	if (max_index < index) {
		max_index = index;
	}

	for (i = npu_dump_op_num_start; i < (max_index + 1); i++) {
		if (md5_dump[i].calced_flag == 0) {
			goto end_dumpMD5;
		}

		if (md5_dump[i].writed_flag == 0) {
			md5_dump[i].writed_flag = 1;
			len = snprintf(
				content, sizeof(content), "%d_%s_INPUT: %s\n",
				i,
				pcer2str(
					executor->task->common_desc[i].op_type),
				md5_dump[i].src_md5);
			len += snprintf(
				content + len, sizeof(content) - len,
				"%d_%s_OUTPUT: %s\n", i,
				pcer2str(
					executor->task->common_desc[i].op_type),
				md5_dump[i].dst_md5);

			if (executor->task->common_desc[i].op_type ==
			    DLA_OP_CONV) {
				len += snprintf(
					content + len, sizeof(content) - len,
					"%d_%s_WEIGHT: %s\n", i,
					pcer2str(executor->task->common_desc[i]
							 .op_type),
					md5_dump[i].md5_spec.conv_md5.wgt_md5);
			}

			if (executor->task->common_desc[i].op_type ==
			    DLA_OP_SDP) {
				len += snprintf(
					content + len, sizeof(content) - len,
					"%d_%s_X1: %s\n", i,
					pcer2str(executor->task->common_desc[i]
							 .op_type),
					md5_dump[i].md5_spec.sdp_md5.x1_md5);
				len += snprintf(
					content + len, sizeof(content) - len,
					"%d_%s_X2: %s\n", i,
					pcer2str(executor->task->common_desc[i]
							 .op_type),
					md5_dump[i].md5_spec.sdp_md5.x2_md5);
				len += snprintf(
					content + len, sizeof(content) - len,
					"%d_%s_Y: %s\n", i,
					pcer2str(executor->task->common_desc[i]
							 .op_type),
					md5_dump[i].md5_spec.sdp_md5.y_md5);
			}
			kernel_write(filep, content, strlen(content), &pos);
		}
	}

end_dumpMD5:
	if (filep != NULL) {
		filp_close(filep, NULL);
	}

	dla_debug("dumpMD5 done.\n");
	return 0;
}

void dump_dtim_to_file(struct win_engine *engine, u32 tiktok)
{
	char name[60];
	int i;
	ktime_t k_time;
	struct rtc_time tm;
	k_time = ktime_get_real();
	tm = rtc_ktime_to_tm(k_time);

	if (engine->master_mem) {
		sprintf(name,
			"/opt/mcu_emission_tiktok%d_%d-%02d-%02d-%02d%02d%02d.bin",
			tiktok, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour + 8, tm.tm_min, tm.tm_sec);

		dump2file(name, engine->master_mem, E31_EMISSION_DTIM_SIZE);
	}
	if (engine->aux_mem) {
		sprintf(name,
			"/opt/mcu_program_tiktok%d_%d-%02d-%02d-%02d%02d%02d.bin",
			tiktok, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			tm.tm_hour + 8, tm.tm_min, tm.tm_sec);

		dump2file(name, engine->aux_mem, E31_PROGRAM_DTIM_SIZE);
	}
	for (i = 0; i < NUM_MAJOR_CORES; i++) {
		if (engine->major_mem[i]) {
			sprintf(name,
				"/opt/mcu_major%d_tiktok%d_%d-%02d-%02d-%02d%02d%02d.bin",
				i, tiktok, tm.tm_year + 1900, tm.tm_mon + 1,
				tm.tm_mday, tm.tm_hour + 8, tm.tm_min,
				tm.tm_sec);
			dump2file(name, engine->major_mem[i],
				  E31_MAJOR_DTIM_SIZE);
		}
	}
}

static char *pcer_name[] = {
	[IDX_START] = "EDMA",
	[IDX_CONV] = "CONV",
	[IDX_SDP] = "SDP",
	[IDX_PDP] = "PDP",
	[IDX_RUBIK] = "RUBIK",
	[IDX_KMD_DSP0] = "KMD_DSP0",
	[IDX_KMD_DSP1] = "KMD_DSP1",
	[IDX_KMD_DSP2] = "KMD_DSP2",
	[IDX_KMD_DSP3] = "KMD_DSP3",
	[IDX_EVENT_SINK] = "EVENT_SINK",
	[IDX_EVENT_SOURCE] = "EVENT_SOURCE",
};

char *pcer2str(u8 pcer)
{
	if (pcer == IDX_NONE) {
		return "NONE";
	}
	if (pcer >= ARRAY_SIZE(pcer_name)) {
		return "FAIL";
	}
	return pcer_name[pcer];
}

#define DATA_CNT_ONE_LINE 16
#define P_BUF_LEN 128
#define F_NAME_LEN 512

void dump_data_cube(struct win_executor *executor, struct host_frame_desc *f,
		    struct dla_data_cube *data,
		    struct dla_common_op_desc *op_desc, const char *name)
{
	int16_t src_addr_index = -1;
	int16_t src_type = -1;
	uint64_t src_data_size = 0;
	uint64_t offset = 0;
	u64 input_address;
	uint32_t dump_buf_len = 0;
	u32 input_is_io_tensor;
	int32_t ret;
	int32_t index = op_desc->index;
	int32_t op_type = op_desc->op_type;
	struct user_model *model = executor->model;
	char *src_dump_buf = NULL;
	char f_name[F_NAME_LEN] = { 0 };
	kmd_dump_info_t *dump_info = &executor->dump_info;
	char md5[MD5_LENGTH] = { 0 };

	src_addr_index = data->address;
	src_data_size = data->size;
	offset = data->offset;
	src_type = data->type;
	if ((-1 == src_addr_index) || (src_type == DLA_MEM_HW)) {
		if (strstr(name, "weight") != NULL) {
			strcpy(md5_dump[index].md5_spec.conv_md5.wgt_md5,
			       "on-fly");
		} else if (strstr(name, "x1") != NULL) {
			strcpy(md5_dump[index].md5_spec.sdp_md5.x1_md5,
			       "on-fly");
		} else if (strstr(name, "x2") != NULL) {
			strcpy(md5_dump[index].md5_spec.sdp_md5.x2_md5,
			       "on-fly");
		} else if (strstr(name, "y") != NULL) {
			strcpy(md5_dump[index].md5_spec.sdp_md5.y_md5,
			       "on-fly");
		}

		dla_error("%s dump failed, src_addr_index:%d\n", name,
			  src_addr_index);
		return;
	}
	dump_buf_len = src_data_size;
	read_input_address(executor, data, &input_address, &input_is_io_tensor);
	dla_debug("input_address:0x%llx, input_is_io_tensor:%d\n",
		  input_address, input_is_io_tensor);

	if (src_type == DLA_MEM_MC) {
		if (input_is_io_tensor == invalid_tensor_idx) {
			ret = dla_data_get_vaddr(&model->mem_handles,
						 src_addr_index,
						 (void **)&src_dump_buf);
			if (ret < 0) {
				dla_error("err:get index(%d) vaddr failed!\n",
					  src_addr_index);
				return;
			}

			src_dump_buf += offset;
		} else {
			input_address =
				f->io_tensors_addr_list[input_is_io_tensor] +
				offset;
			if (f->input_bobj[input_is_io_tensor] != NULL)
				src_dump_buf = dla_dmabuf_vmap(
					f->input_bobj[input_is_io_tensor]);
		}

		if (src_dump_buf == NULL) {
			dla_error("%d, error:src_dump_buf == NULL!index=%d\n",
				  __LINE__, index);
			return;
		}

	} else if (src_type == DLA_MEM_CV) {
		int addr_offset =
			input_address -
			((struct nvdla_device *)(executor->driver_context))
				->spram_base_addr;

		struct dla_buffer_object *spram_bobj =
			((struct nvdla_device *)(executor->driver_context))
				->spram_bobj;

		if (spram_bobj != NULL) {
			src_dump_buf = dla_dmabuf_vmap(spram_bobj);
			src_dump_buf += addr_offset;
		}

		if (src_dump_buf == NULL) {
			dla_error("%d, error:src_dump_buf == NULL!index=%d\n",
				  __LINE__, index);
			return;
		}
	} else {
		dla_error("invalid error\n");
		return;
	}

	sprintf(f_name, "%s/%d_%d_%d_%s_%d_0_%s.bin", dump_info->path,
		dump_info->process_id, dump_info->model_id, f->frame_idx,
		pcer2str(op_type), index, name);

	dump2file(f_name, src_dump_buf, src_data_size);

	calc_md5(md5, src_dump_buf, src_data_size);
	if (strstr(name, "weight") != NULL) {
		memcpy(md5_dump[index].md5_spec.conv_md5.wgt_md5, md5,
		       MD5_LENGTH);
	} else if (strstr(name, "x1") != NULL) {
		memcpy(md5_dump[index].md5_spec.sdp_md5.x1_md5, md5,
		       MD5_LENGTH);
	} else if (strstr(name, "x2") != NULL) {
		memcpy(md5_dump[index].md5_spec.sdp_md5.x2_md5, md5,
		       MD5_LENGTH);
	} else if (strstr(name, "y") != NULL) {
		memcpy(md5_dump[index].md5_spec.sdp_md5.y_md5, md5, MD5_LENGTH);
	}
}

void dla_dump_src_data(struct win_executor *executor, struct host_frame_desc *f,
		       int op_index)
{
	int16_t src_addr_index = -1;
	uint64_t src_data_size = 0;
	uint64_t offset = 0;
	uint32_t op_num;
	uint32_t index;
	int32_t ret;
	int op_type = 0;
	int src_type = 0;
	int i = 0, j = 0, k = 0;
	char *src_dump_buf = NULL;
	uint32_t dump_buf_len = 0;
	struct dla_data_cube *src_data;
	u64 input_address;
	u32 input_is_io_tensor;
	struct user_model *model = executor->model;
	char f_name[F_NAME_LEN] = { 0 };
	char p_buf[P_BUF_LEN] = { 0 };
	uint32_t seg_data_len = npu_dump_data_len;
	uint32_t seg_adrr[3] = { 0 };
	kmd_dump_info_t *dump_info = &executor->dump_info;
	char md5[MD5_LENGTH] = { 0 };

	if ((npu_dump_data_enable == 0) || (npu_dump_data_len <= 0)) {
		return;
	}

	op_num = executor->network->num_operations;

	if (npu_dump_op_num_start >= op_num) {
		dla_error("error:npu_dump_op_num_start(%d) >= op_num(%d)!\n",
			  npu_dump_op_num_start, op_num);
		return;
	}

	for (i = npu_dump_op_num_start; i < op_num; i++) {
		src_addr_index = -1;
		index = executor->task->common_desc[i].index;
		op_type = executor->task->common_desc[i].op_type;
		if (index != op_index)
			continue;
		switch (op_type) {
		case DLA_OP_EDMA:
			src_data = &executor->task->surface_desc[index]
					    .edma_surface.src_data;
			src_addr_index = executor->task->surface_desc[index]
						 .edma_surface.src_data.address;
			src_data_size = executor->task->surface_desc[index]
						.edma_surface.src_data.size;
			offset = executor->task->surface_desc[index]
					 .edma_surface.src_data.offset;
			src_type = executor->task->surface_desc[index]
					   .edma_surface.src_data.type;
			break;
		case DLA_OP_CONV:
			src_data = &executor->task->surface_desc[index]
					    .conv_surface.src_data;
			src_addr_index = executor->task->surface_desc[index]
						 .conv_surface.src_data.address;
			src_data_size = executor->task->surface_desc[index]
						.conv_surface.src_data.size;
			offset = executor->task->surface_desc[index]
					 .conv_surface.src_data.offset;
			src_type = executor->task->surface_desc[index]
					   .conv_surface.src_data.type;
			break;
		case DLA_OP_SDP:
			src_data = &executor->task->surface_desc[index]
					    .sdp_surface.src_data;
			src_addr_index = executor->task->surface_desc[index]
						 .sdp_surface.src_data.address;
			src_data_size = executor->task->surface_desc[index]
						.sdp_surface.src_data.size;
			offset = executor->task->surface_desc[index]
					 .sdp_surface.src_data.offset;
			src_type = executor->task->surface_desc[index]
					   .sdp_surface.src_data.type;

			break;
		case DLA_OP_PDP:
			src_data = &executor->task->surface_desc[index]
					    .pdp_surface.src_data;
			src_addr_index = executor->task->surface_desc[index]
						 .pdp_surface.src_data.address;
			src_data_size = executor->task->surface_desc[index]
						.pdp_surface.src_data.size;
			offset = executor->task->surface_desc[index]
					 .pdp_surface.src_data.offset;
			src_type = executor->task->surface_desc[index]
					   .pdp_surface.src_data.type;

			break;
		case DLA_OP_RUBIK:
			src_data = &executor->task->surface_desc[index]
					    .rubik_surface.src_data;
			src_addr_index =
				executor->task->surface_desc[index]
					.rubik_surface.src_data.address;
			src_data_size = executor->task->surface_desc[index]
						.rubik_surface.src_data.size;
			offset = executor->task->surface_desc[index]
					 .rubik_surface.src_data.offset;
			src_type = executor->task->surface_desc[index]
					   .rubik_surface.src_data.type;
			break;
		case DLA_KMD_OP_DSP_0:
		case DLA_KMD_OP_DSP_1:
		case DLA_KMD_OP_DSP_2:
		case DLA_KMD_OP_DSP_3:
			src_data = &executor->task->surface_desc[index]
					    .dsp_surface.src_data[0];
			src_addr_index = executor->task->surface_desc[index]
						 .dsp_surface.src_data[0]
						 .address;
			src_data_size = executor->task->surface_desc[index]
						.dsp_surface.src_data[0]
						.size;
			offset = executor->task->surface_desc[index]
					 .dsp_surface.src_data[0]
					 .offset;
			src_type = executor->task->surface_desc[index]
					   .dsp_surface.src_data[0]
					   .type;
			break;
		default:
			continue;
		}

		if (op_type == IDX_CONV) {
			dump_data_cube(executor, f,
				       &executor->task->surface_desc[index]
						.conv_surface.weight_data,
				       &executor->task->common_desc[i],
				       "weight");
		} else if (op_type == IDX_SDP) {
			dump_data_cube(executor, f,
				       &executor->task->surface_desc[index]
						.sdp_surface.x1_data,
				       &executor->task->common_desc[i], "x1");
			dump_data_cube(executor, f,
				       &executor->task->surface_desc[index]
						.sdp_surface.x2_data,
				       &executor->task->common_desc[i], "x2");
			dump_data_cube(executor, f,
				       &executor->task->surface_desc[index]
						.sdp_surface.y_data,
				       &executor->task->common_desc[i], "y");
		}

		dla_debug(
			"src data dump %d:index:%d op_type:%d src_type:%d src_addr_index: %d size:0x%llx offset:0x%llx\n",
			__LINE__, index, op_type, src_type, src_addr_index,
			src_data_size, offset);

		if ((-1 == src_addr_index) || (src_type == DLA_MEM_HW)) {
			strcpy(md5_dump[index].src_md5, "on-fly");
			continue;
		}
		if ((index == 0) && (op_type == 0) && (src_type == 0) &&
		    (src_addr_index == 0)) {
			continue;
		}

		dump_buf_len = src_data_size;

		seg_adrr[1] = src_data_size / 2;
		seg_adrr[2] = src_data_size - seg_data_len;

		read_input_address(executor, src_data, &input_address,
				   &input_is_io_tensor);
		dla_debug("input_address:0x%llx, input_is_io_tensor:%d\n",
			  input_address, input_is_io_tensor);

		if (src_type == DLA_MEM_MC) {
			if (input_is_io_tensor == invalid_tensor_idx) {
				ret = dla_data_get_vaddr(
					&model->mem_handles, src_addr_index,
					(void **)&src_dump_buf);
				if (ret < 0) {
					dla_error(
						"err:get index(%d) vaddr failed!\n",
						src_addr_index);
					return;
				}

				src_dump_buf += offset;
			} else {
				input_address = f->io_tensors_addr_list
							[input_is_io_tensor] +
						offset;
				if (f->input_bobj[input_is_io_tensor] != NULL)
					src_dump_buf = dla_dmabuf_vmap(
						f->input_bobj
							[input_is_io_tensor]);
			}

			if (src_dump_buf == NULL) {
				dla_error(
					"%d, error:src_dump_buf == NULL!index=%d\n",
					__LINE__, index);
				continue;
			}

		} else if (src_type == DLA_MEM_CV) {
			int addr_offset =
				input_address -
				((struct nvdla_device
					  *)(executor->driver_context))
					->spram_base_addr;

			struct dla_buffer_object *spram_bobj =
				((struct nvdla_device
					  *)(executor->driver_context))
					->spram_bobj;

			if (spram_bobj != NULL) {
				src_dump_buf = dla_dmabuf_vmap(spram_bobj);
				src_dump_buf += addr_offset;
			}

			if (src_dump_buf == NULL) {
				dla_error(
					"%d, error:src_dump_buf == NULL!index=%d\n",
					__LINE__, index);
				continue;
			}

		} else {
			continue;
		}

		memset(p_buf, 0, P_BUF_LEN);
		for (k = 0; k < 3; k++) {
			for (j = 0; j < seg_data_len; j++) {
				if (j % DATA_CNT_ONE_LINE == 0) {
					if (j != 0) {
						dla_debug("%s\n", p_buf);
						memset(p_buf, 0, P_BUF_LEN);
					}
					snprintf(p_buf + strlen(p_buf),
						 P_BUF_LEN - strlen(p_buf),
						 "%05x: ", seg_adrr[k] + j);
				}
				snprintf(p_buf + strlen(p_buf),
					 P_BUF_LEN - strlen(p_buf), "%02x",
					 src_dump_buf[seg_data_len * k + j]);
			}
			dla_debug("%s\n", p_buf);
			memset(p_buf, 0, P_BUF_LEN);
		}

		sprintf(f_name, "%s/%d_%d_%d_%s_%d_0_in.bin", dump_info->path,
			dump_info->process_id, dump_info->model_id,
			f->frame_idx, pcer2str(op_type), index);

		dump2file(f_name, src_dump_buf, src_data_size);

		calc_md5(md5, src_dump_buf, src_data_size);
		memcpy(md5_dump[index].src_md5, md5, MD5_LENGTH);
	}

	return;
}

void dla_dump_dst_data(struct win_executor *executor, struct host_frame_desc *f,
		       int op_index)
{
	int16_t dst_addr_index = -1;
	uint64_t dst_data_size = 0;
	uint64_t offset = 0;
	uint32_t op_num;
	uint32_t index;
	int32_t ret;
	int op_type = 0;
	int dst_type = 0;
	int i = 0, j = 0, k = 0;
	char *dst_dump_buf = NULL;
	uint32_t dump_buf_len = 0;
	struct dla_data_cube *dst_data;
	u64 output_address;
	u32 output_is_io_tensor;
	struct user_model *model = executor->model;
	char f_name[F_NAME_LEN] = { 0 };
	char md5_f_name[F_NAME_LEN] = { 0 };
	char p_buf[P_BUF_LEN] = { 0 };
	uint32_t seg_data_len = npu_dump_data_len;
	uint32_t seg_adrr[3] = { 0 };
	kmd_dump_info_t *dump_info = &executor->dump_info;
	char md5[MD5_LENGTH] = { 0 };

	if ((npu_dump_data_enable == 0) || (npu_dump_data_len <= 0)) {
		return;
	}

	op_num = executor->network->num_operations;

	if (npu_dump_op_num_start >= op_num) {
		dla_error("error:npu_dump_op_num_start(%d) >= op_num(%d)!\n",
			  npu_dump_op_num_start, op_num);
		return;
	}

	for (i = npu_dump_op_num_start; i < op_num; i++) {
		dst_addr_index = -1;
		index = executor->task->common_desc[i].index;
		op_type = executor->task->common_desc[i].op_type;
		if (index != op_index)
			continue;
		switch (op_type) {
		case DLA_OP_EDMA:
			dst_data = &executor->task->surface_desc[index]
					    .edma_surface.dst_data;
			dst_addr_index = executor->task->surface_desc[index]
						 .edma_surface.dst_data.address;
			dst_data_size = executor->task->surface_desc[index]
						.edma_surface.dst_data.size;
			offset = executor->task->surface_desc[index]
					 .edma_surface.dst_data.offset;
			dst_type = executor->task->surface_desc[index]
					   .edma_surface.dst_data.type;
			break;
		case DLA_OP_CONV:
			dst_data = &executor->task->surface_desc[index]
					    .conv_surface.dst_data;
			dst_addr_index = executor->task->surface_desc[index]
						 .conv_surface.dst_data.address;
			dst_data_size = executor->task->surface_desc[index]
						.conv_surface.dst_data.size;
			offset = executor->task->surface_desc[index]
					 .conv_surface.dst_data.offset;
			dst_type = executor->task->surface_desc[index]
					   .conv_surface.dst_data.type;
			break;
		case DLA_OP_SDP:
			dst_data = &executor->task->surface_desc[index]
					    .sdp_surface.dst_data;
			dst_addr_index = executor->task->surface_desc[index]
						 .sdp_surface.dst_data.address;
			dst_data_size = executor->task->surface_desc[index]
						.sdp_surface.dst_data.size;
			offset = executor->task->surface_desc[index]
					 .sdp_surface.dst_data.offset;
			dst_type = executor->task->surface_desc[index]
					   .sdp_surface.dst_data.type;

			break;
		case DLA_OP_PDP:
			dst_data = &executor->task->surface_desc[index]
					    .pdp_surface.dst_data;
			dst_addr_index = executor->task->surface_desc[index]
						 .pdp_surface.dst_data.address;
			dst_data_size = executor->task->surface_desc[index]
						.pdp_surface.dst_data.size;
			offset = executor->task->surface_desc[index]
					 .pdp_surface.dst_data.offset;
			dst_type = executor->task->surface_desc[index]
					   .pdp_surface.dst_data.type;

			break;
		case DLA_OP_RUBIK:
			dst_data = &executor->task->surface_desc[index]
					    .rubik_surface.dst_data;
			dst_addr_index =
				executor->task->surface_desc[index]
					.rubik_surface.dst_data.address;
			dst_data_size = executor->task->surface_desc[index]
						.rubik_surface.dst_data.size;
			offset = executor->task->surface_desc[index]
					 .rubik_surface.dst_data.offset;
			dst_type = executor->task->surface_desc[index]
					   .rubik_surface.dst_data.type;
			break;
		case DLA_KMD_OP_DSP_0:
		case DLA_KMD_OP_DSP_1:
		case DLA_KMD_OP_DSP_2:
		case DLA_KMD_OP_DSP_3:
			dst_data = &executor->task->surface_desc[index]
					    .dsp_surface.dst_data[0];
			dst_addr_index = executor->task->surface_desc[index]
						 .dsp_surface.dst_data[0]
						 .address;
			dst_data_size = executor->task->surface_desc[index]
						.dsp_surface.dst_data[0]
						.size;
			offset = executor->task->surface_desc[index]
					 .dsp_surface.dst_data[0]
					 .offset;
			dst_type = executor->task->surface_desc[index]
					   .dsp_surface.dst_data[0]
					   .type;
			break;
		default:
			continue;
		}

		dla_debug(
			"dst data dump %d:index:%d op_type:%d dst_type:%d dst_addr_index: %d size:0x%llx offset:0x%llx\n",
			__LINE__, index, op_type, dst_type, dst_addr_index,
			dst_data_size, offset);
		sprintf(md5_f_name, "%s/%d_%d_%d_md5.txt", dump_info->path,
			dump_info->process_id, dump_info->model_id,
			f->frame_idx);
		if (-1 == dst_addr_index) {
			if (dst_type == DLA_MEM_HW) {
				strcpy(md5_dump[index].dst_md5, "on-fly");
				md5_dump[index].calced_flag = 1;
				dumpMD5(md5_f_name, executor, index);
			}
			continue;
		}

		if ((index == 0) && (op_type == 0) && (dst_type == 0) &&
		    (dst_addr_index == 0)) {
			continue;
		}

		dump_buf_len = dst_data_size;

		seg_adrr[1] = dst_data_size / 2;
		seg_adrr[2] = dst_data_size - seg_data_len;

		read_input_address(executor, dst_data, &output_address,
				   &output_is_io_tensor);
		dla_debug("output_address:0x%llx, output_is_io_tensor:%d\n",
			  output_address, output_is_io_tensor);

		if (dst_type == DLA_MEM_MC) {
			if (output_is_io_tensor == invalid_tensor_idx) {
				ret = dla_data_get_vaddr(
					&model->mem_handles, dst_addr_index,
					(void **)&dst_dump_buf);
				if (ret < 0) {
					dla_error(
						"err:get index(%d) vaddr failed!\n",
						dst_addr_index);
					return;
				}

				dst_dump_buf += offset;
			} else {
				output_address = f->io_tensors_addr_list
							 [output_is_io_tensor] +
						 offset;
				if (f->output_bobj[output_is_io_tensor] != NULL)
					dst_dump_buf = dla_dmabuf_vmap(
						f->output_bobj
							[output_is_io_tensor]);
				dst_dump_buf += offset;
			}

			if (dst_dump_buf == NULL) {
				dla_error(
					"%d, error:dst_dump_buf == NULL!index=%d\n",
					__LINE__, index);
				continue;
			}

		} else if (dst_type == DLA_MEM_CV) {
			int addr_offset =
				output_address -
				((struct nvdla_device
					  *)(executor->driver_context))
					->spram_base_addr;

			struct dla_buffer_object *spram_bobj =
				((struct nvdla_device
					  *)(executor->driver_context))
					->spram_bobj;

			if (spram_bobj != NULL) {
				dst_dump_buf = dla_dmabuf_vmap(spram_bobj);
				dst_dump_buf += addr_offset;
			}

			if (dst_dump_buf == NULL) {
				dla_error(
					"%d, error:dst_dump_buf == NULL!index=%d\n",
					__LINE__, index);
				continue;
			}

		} else {
			continue;
		}

		memset(p_buf, 0, P_BUF_LEN);
		for (k = 0; k < 3; k++) {
			for (j = 0; j < seg_data_len; j++) {
				if (j % DATA_CNT_ONE_LINE == 0) {
					if (j != 0) {
						dla_debug("%s\n", p_buf);
						memset(p_buf, 0, P_BUF_LEN);
					}
					snprintf(p_buf + strlen(p_buf),
						 P_BUF_LEN - strlen(p_buf),
						 "%05x: ", seg_adrr[k] + j);
				}
				snprintf(p_buf + strlen(p_buf),
					 P_BUF_LEN - strlen(p_buf), "%02x",
					 dst_dump_buf[seg_data_len * k + j]);
			}
			dla_debug("%s\n", p_buf);
			memset(p_buf, 0, P_BUF_LEN);
		}

		sprintf(f_name, "%s/%d_%d_%d_%s_%d_0_out.bin", dump_info->path,
			dump_info->process_id, dump_info->model_id,
			f->frame_idx, pcer2str(op_type), index);

		dump2file(f_name, dst_dump_buf, dst_data_size);

		calc_md5(md5, dst_dump_buf, dst_data_size);
		memcpy(md5_dump[index].dst_md5, md5, MD5_LENGTH);
		md5_dump[index].calced_flag = 1;
		dumpMD5(md5_f_name, executor, index);
	}

	return;
}

#if (NPU_DEV_SIM == NPU_MCU_ALONE)
static char *E31_FRAME_DESC_BIN = "/sim_frame_desc";
#define FILE_NAME_MAX_LEN 128
#endif

static void send_op_resume_to_hw(struct win_engine *engine, u8 tiktok,
				 u16 op_index)
{
	msg_payload_t payload;
	payload.type = NOTIFY_OP_RESUME;
	payload.param = tiktok;
	payload.lparam = op_index;
	send_mbx_msg_to_e31(engine, payload);
}

void dump_data_to_file(struct work_struct *work)
{
#if (NPU_DEV_SIM == NPU_REAL_ENV)
	struct dump_op_work_t *dump_op_work =
		container_of(work, struct dump_op_work_t, work);

	int tiktok = dump_op_work->tiktok;
	u16 op_index = dump_op_work->op_index;
	struct host_frame_desc *f = dump_op_work->f;
	struct win_executor *executor = f->executor;
	struct win_engine *engine = executor->engine;

	dla_dump_src_data(executor, f, op_index);
	dla_dump_dst_data(executor, f, op_index);
	send_op_resume_to_hw(engine, tiktok, op_index);

#elif (NPU_DEV_SIM == NPU_MCU_ALONE)
	struct dump_file_work_t *dump_file_work =
		container_of(work, struct dump_file_work_t, work);
	int tiktok = dump_file_work->tiktok;
	struct host_frame_desc *f = dump_file_work->f;
	struct win_executor *executor = f->executor;
	struct win_engine *engine = executor->engine;
	struct file *flip = NULL;
	loff_t pos = 0;
	struct user_model *model = f->model;
	hetero_ipc_frame_t *frame_info =
		(hetero_ipc_frame_t *)&model->e31_frame_info;

	int wsize;
	int i;
	//	struct processors_interface *pcer = engine->processors;
	struct dump_file_header *header;
	int buf_size = 0;
	u32 cur_pos = 0;
	void *tmp = NULL;
	int *magic_end = NULL;
	u8 *dependent = frame_info->op_dependency
				.ref_count;  // executor->dependency_count;
	char *buffer = NULL;
	npu_io_tensor_t *io_addr = NULL;
	char *name;

	name = kzalloc(FILE_NAME_MAX_LEN, GFP_KERNEL);
	if (name == NULL) {
		printk("%s, %d, alloc name mem failed.\n", __func__, __LINE__);
		return;
	}
	snprintf(name, FILE_NAME_MAX_LEN, "%s-%d.bin", E31_FRAME_DESC_BIN,
		 engine->frame_seq);
	engine->frame_seq++;
	flip = filp_open(name, O_RDWR | O_APPEND | O_CREAT, 0666);
	if (IS_ERR(flip)) {
		printk("open file e31_data_file.c error.\n");
		goto err_file;
	}

	buf_size += sizeof(struct dump_file_header);
	buf_size += executor->network->num_operations;
	buf_size += sizeof(npu_io_tensor_t);
	buf_size = ROUND_UP(buf_size, CDMA_TRANSFER_BYTE_ALIGN);
	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		if (executor->op_num[i] <= 0) {
			continue;
		}
		buf_size += executor->prog_data_size[i];
		printk("%s, %d, buf_size=%d.\n", __func__, __LINE__, buf_size);
	}
	if (executor->network->num_luts) {
		buf_size += executor->network->num_luts * sizeof(lut_dev_t);
	}
	buf_size += sizeof(int);
	buffer = kzalloc(buf_size, GFP_KERNEL);
	if (buffer == NULL) {
		printk("%s, %d, alloc buffer failed.\n", __func__, __LINE__);
		goto err_buffer;
	}

	header = (struct dump_file_header *)buffer;
	if (header == NULL) {
		printk("%s, %d, why..\n", __func__, __LINE__);
	}
	header->magic = 0x20230809;

	header->op_num = executor->network->num_operations;
	memcpy(&header->first_conv_hdr, &executor->op_prog_addrs.next_conv_hdr,
	       sizeof(conv_dev_hdr_t));
	dla_debug("total_len=%u\n", header->first_conv_hdr.total_len);
	dla_debug("master_len=%u\n", header->first_conv_hdr.master_len);
	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		printk("%s, %d, i=%d, op_num=%d.\n", __func__, __LINE__, i,
		       executor->op_num[i]);
		header->pcer_op_num[i] = executor->op_num[i];
	}

	cur_pos += sizeof(struct dump_file_header);
	header->depend_offset = sizeof(struct dump_file_header);
	header->depend_len = header->op_num > MAX_DTIM_DEPCNT ?
				     MAX_DTIM_DEPCNT :
				     header->op_num;
	tmp = (void *)(buffer + cur_pos);
	memcpy(tmp, dependent, header->depend_len);
	cur_pos += header->op_num;
	header->io_tensor_offset = cur_pos;

	tmp = (void *)(buffer + cur_pos);
	io_addr = &f->io_tensor;
	memcpy(tmp, (void *)io_addr, sizeof(npu_io_tensor_t));

	cur_pos += sizeof(npu_io_tensor_t);
	cur_pos = ROUND_UP(cur_pos, CDMA_TRANSFER_BYTE_ALIGN);
	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		if (executor->op_num[i] <= 0 ||
		    executor->prog_data_size[i] == 0) {
			header->op_data_offset[i] = 0;
			header->op_data_len[i] = 0;
			continue;
		}

		tmp = (void *)(buffer + cur_pos);
		header->op_data_offset[i] = cur_pos;
		header->op_data_len[i] = executor->prog_data_size[i];
		printk("%s, op_type=%d, op_data_offset=%d, op_data_len=%d.\n",
		       __func__, i, header->op_data_offset[i],
		       header->op_data_len[i]);

		if (header->op_data_len[i]) {
			memcpy(tmp, executor->prog_data_buf_bobj[i],
			       executor->prog_data_size[i]);
		}

		cur_pos += executor->prog_data_size[i];
		printk("%s, i=%d, offset=%d, len=%d.\n", __func__, i,
		       header->op_data_offset[i], header->op_data_len[i]);
	}

	if (executor->network->num_luts) {
		header->lut_len =
			executor->network->num_luts * sizeof(lut_dev_t);
		printk("%s, %d. NOTE: fill lut with zero. header->lut_len=%u, lut_bse_iova=0x%llx\n",
		       __func__, __LINE__, header->lut_len,
		       executor->lut_base_iova);
		header->lut_offset = cur_pos;
		tmp = (void *)(buffer + cur_pos);
		memset(tmp, 0, header->lut_len);
		header->lut_base_iova = executor->lut_base_iova;
		cur_pos += header->lut_len;
	} else {
		header->lut_offset = 0;
		header->lut_len = 0;
		printk("%s, %d. no lut been used. header->lut_len=%u, lut_bse_iova=0x%llx\n",
		       __func__, __LINE__, header->lut_len,
		       executor->lut_base_iova);
	}

	magic_end = (int *)(buffer + cur_pos);
	header->magic_end_offset = cur_pos;
	*magic_end = 0x131452;
	printk("%s, %d, magic_end_ofs=%d.\n", __func__, __LINE__,
	       header->magic_end_offset);

	wsize = kernel_write(flip, buffer, buf_size, &pos);
	printk("%s, %d, wsize=%d, header size=%d, pos=%lld.\n", __func__,
	       __LINE__, wsize, buf_size, pos);

	mbx_irq_frame_done(executor->engine, tiktok, 0);

	kfree(buffer);

err_buffer:
	filp_close(flip, NULL);
err_file:
	kfree(name);
#endif
}
