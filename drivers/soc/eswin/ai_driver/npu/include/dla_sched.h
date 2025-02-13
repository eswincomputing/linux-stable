/*
 * Copyright (c) 2017-2018, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DLA_SCHED_H_
#define __DLA_SCHED_H_

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
struct dla_task {
	/* platform specific data to communicate with portability layer */
	void *task_data;
	/* task state */
	uint32_t state;
	/* Task base address */
	uint64_t base;
	struct dla_common_op_desc *common_desc;
	union dla_surface_container *surface_desc;
	union dla_operation_container *op_desc;
} __attribute__((packed, aligned(256)));

/**
 * @brief			Configuration parameters supported by the engine
 *
 * atom_size			Memory smallest access size
 * bdma_enable			Defines whether bdma is supported
 * rubik_enable			Defines whether rubik is supported
 * weight_compress_support	Defines whether weight data compression is supported
 */
struct dla_config {
	uint32_t atom_size;
	bool bdma_enable;
	bool rubik_enable;
	bool weight_compress_support;
};

#endif
