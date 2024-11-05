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

#ifndef __DLA_ENGINE_INTERNAL_H_
#define __DLA_ENGINE_INTERNAL_H_

#include <opendla.h>
#include <dla_engine.h>
#include <dla_interface.h>
#include "nvdla_interface.h"
#include <linux/kernel.h>
#include "internal_interface.h"

#define BITS(num, range) ((((0xFFFFFFFF >> (31 - (1 ? range))) & \
			(0xFFFFFFFF << (0 ? range))) & num) >> \
			(0 ? range))
#define HIGH32BITS(val64bit) ((uint32_t)((val64bit) >> 32))
#define LOW32BITS(val64bit) ((uint32_t)(val64bit))

#ifdef MIN
#undef MIN
#endif /* MIN */

#ifdef MAX
#undef MAX
#endif /* MAX */

#define MIN(a, b) ((a) > (b) ? (b) : (a))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

/*********************************************************/
/******************** Utilities **************************/
/*********************************************************/
#ifdef DEBUG
#define CHECK_ALIGN(val, align) assert((val & (align - 1)) == 0)
#else
#define CHECK_ALIGN(val, align)
#endif /* DEBUG */

#define NPU_MASK(reg, field) (reg##_##field##_FIELD)
#define FIELD_ENUM(r, f, e) (r##_##f##_##e)
#define SHIFT(reg, field) (reg##_##field##_SHIFT)

#define GLB_REG(name) GLB_##name##_0
#define MCIF_REG(name) MCIF_##name##_0
#define CVIF_REG(name) CVIF_##name##_0
#define BDMA_REG(name) BDMA_##name##_0
#define CDMA_REG(name) CDMA_##name##_0
#define CSC_REG(name) CSC_##name##_0
#define CMAC_A_REG(name) CMAC_A_##name##_0
#define CMAC_B_REG(name) CMAC_B_##name##_0
#define CACC_REG(name) CACC_##name##_0
#define SDP_RDMA_REG(name) SDP_RDMA_##name##_0
#define SDP_REG(name) SDP_##name##_0
#define PDP_RDMA_REG(name) PDP_RDMA_##name##_0
#define PDP_REG(name) PDP_##name##_0
#define CDP_RDMA_REG(name) CDP_RDMA_##name##_0
#define CDP_REG(name) CDP_##name##_0
#define RBK_REG(name) RBK_##name##_0

/* alias for register read for each sub-module */
#define glb_reg_read(base, reg) readl(base + GLB_REG(reg))

/* alias for register write for each sub-module */
#define glb_reg_write(base, reg, val) writel(val, base + GLB_REG(reg))

int dla_enable_intr(struct nvdla_device *dev, uint32_t mask);
int dla_disable_intr(struct nvdla_device *dev, uint32_t mask);
int dla_get_dma_cube_address(void *driver_context, void *task_data,
			     int16_t index, uint32_t offset, void *dst_ptr,
			     u32 *is_io_tensor);
int dla_read_input_address(struct dla_data_cube *data, uint64_t *address);
int dla_get_sram_cube_address(void *driver_context, void *task_data,
			      int16_t index, uint32_t offset, uint64_t *dst_ptr,
			      u32 *is_io_tensor);

/**
 * Convolution operations
 */
int dla_conv_rdma_check(struct dla_processor_group *group,
			union dla_operation_container *op,
			union dla_surface_container *surface);

/**
 * SDP operations
 */
int dla_sdp_rdma_check(struct dla_processor_group *group,
		       union dla_operation_container *op,
		       union dla_surface_container *surface);
/**
 * PDP operations
 */
int dla_pdp_rdma_check(struct dla_processor_group *group,
		       union dla_operation_container *op,
		       union dla_surface_container *surface);

/**
 * RUBIK operations
 */
int dla_rubik_rdma_check(struct dla_processor_group *group,
			 union dla_operation_container *op,
			 union dla_surface_container *surface);

int edma_rdma_check(struct dla_processor_group *group,
		    union dla_operation_container *op,
		    union dla_surface_container *surface);

int dsp_register_interface(void *rt, int instance_num);
void dsp_set_producer(int32_t group_id, int32_t __unused);
int dsp_enable(struct dla_processor_group *group);
int dsp_rdma_check(struct dla_processor_group *group,
		   union dla_operation_container *op,
		   union dla_surface_container *surface);
int dsp_is_ready(struct processors_interface *processor,
		 struct dla_processor_group *group);
void dsp_dump_config(struct dla_processor_group *group);

void sim_pdp_set_producer(int32_t group_id, int32_t rdma_group_id);

int sim_pdp_enable(struct dla_processor_group *group);

void sim_pdp_rdma_check(struct dla_processor_group *group);

int32_t sim_pdp_program(struct dla_processor_group *group);

int sim_pdp_is_ready(struct dla_processor *processor,
		     struct dla_processor_group *group);

void sim_pdp_dump_config(struct dla_processor_group *group);

void sim_pdp_stat_data(struct dla_processor *processor,
		       struct dla_processor_group *group);

void sim_pdp_dump_stat(struct dla_processor *processor);

void sim_sdp_set_producer(int32_t group_id, int32_t rdma_group_id);

int sim_sdp_enable(struct dla_processor_group *group);

void sim_sdp_rdma_check(struct dla_processor_group *group);

int32_t sim_sdp_program(struct dla_processor_group *group);

int sim_sdp_is_ready(struct dla_processor *processor,
		     struct dla_processor_group *group);

void sim_sdp_dump_config(struct dla_processor_group *group);

void sim_sdp_stat_data(struct dla_processor *processor,
		       struct dla_processor_group *group);

void sim_sdp_dump_stat(struct dla_processor *processor);

void sim_edma_set_producer(int32_t group_id, int32_t rdma_group_id);

int sim_edma_enable(struct dla_processor_group *group);

void sim_edma_rdma_check(struct dla_processor_group *group);

int32_t sim_edma_program(struct dla_processor_group *group);

int sim_edma_is_ready(struct dla_processor *processor,
		      struct dla_processor_group *group);

void sim_edma_dump_config(struct dla_processor_group *group);

void sim_edma_stat_data(struct dla_processor *processor,
			struct dla_processor_group *group);

void sim_edma_dump_stat(struct dla_processor *processor);

#endif
