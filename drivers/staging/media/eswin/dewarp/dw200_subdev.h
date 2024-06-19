/****************************************************************************
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 * The GPL License (GPL)
 *
 * Copyright (c) 2020 VeriSilicon Holdings Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;
 *
 *****************************************************************************
 *
 * Note: This software is released under dual MIT and GPL licenses. A
 * recipient may use this file under the terms of either the MIT license or
 * GPL License. If you wish to use only one license not the other, you can
 * indicate your decision by deleting one of the above license notices in your
 * version of this file.
 *
 *****************************************************************************/
#ifndef _DW200_DEV_H
#define _DW200_DEV_H

#ifdef __KERNEL__
#include <linux/dmabuf-heap-import-helper.h>
#include <linux/workqueue.h>
#include <linux/ioctl.h>
#include <linux/clk.h>
#include <linux/reset.h>
#endif

#include "vvdefs.h"
#include "vivdw200_irq_queue.h"

#ifndef __KERNEL__
#include <stdlib.h>
#include <stdio.h>
#define copy_from_user(a, b, c) dw200_copy_data(a, b, c)
#define copy_to_user(a, b, c) dw200_copy_data(a, b, c)

typedef void (*pReadBar)(uint32_t bar, uint32_t *data);
typedef void (*pWriteBar)(uint32_t bar, uint32_t data);

extern void dwe_set_func(pReadBar read_func, pWriteBar write_func);
//extern void dw200_set_func(cmDW200* pDW200,pDw200ReadBar read_func, pDw200WriteBar write_func);

typedef bool (*pVseReadBar)(uint32_t bar, uint32_t *data);
typedef bool (*pVseWriteBar)(uint32_t bar, uint32_t data);

extern void vse_set_func(pVseReadBar read_func, pVseWriteBar write_func);
extern long dw200_copy_data(void *dst, void *src, int size);
#endif

struct dwe_hw_info {
	u32 split_line;
	u32 scale_factor;
	u32 in_format;
	u32 out_format;
	u32 in_yuvbit;
	u32 out_yuvbit;
	u32 hand_shake;
	u32 roi_x, roi_y;
	u32 boundary_y, boundary_u, boundary_v;
	u32 map_w, map_h;
	u32 src_auto_shadow, dst_auto_shadow;
	u32 src_w, src_stride, src_h;
	u32 dst_w, dst_stride, dst_h, dst_size_uv;
	u32 split_h, split_v1, split_v2;
	u32 dwe_start;
};

struct vse_crop_size {
	u32 left;
	u32 right;
	u32 top;
	u32 bottom;
};

struct vse_size {
	u32 width;
	u32 height;
};

struct vse_format_conv_settings {
	u32 in_format;
	u32 out_format;
};

struct vse_mi_settings {
	bool enable;
	u32 out_format;
	u32 width;
	u32 height;
	u32 yuvbit;
};

struct vse_params {
	u32 src_w;
	u32 src_h;
	u32 in_format;
	u32 in_yuvbit;
	u32 input_select;
	bool vse_start;
	struct vse_crop_size crop_size[3];
	struct vse_size out_size[3];
	struct vse_format_conv_settings format_conv[3];
	bool resize_enable[3];
	struct vse_mi_settings mi_settings[3];
};

//Dw200 fe module
enum dw200_fe_state {
	DW200_FE_STATE_INIT,
	DW200_FE_STATE_READY,
	DW200_FE_STATE_RUNNING,
	DW200_FE_STATE_WAITING,
	DW200_FE_STATE_EXIT
};

enum fe_work_mode_e {
	DW200_FE_WORK_MODE_BYPASS = 0,
	DW200_FE_WORK_MODE_MCM = 1,
	DW200_FE_WORK_MODE_TEST = 2,
	DW200_FE_WORK_MODE_MAX
};

enum dw200_fe_tbl_type_e { DW200_TBL_NULL };

enum dw200_fe_cmd_type_e {
	DW200_FE_CMD_REG_READ = 0, /**< unused */
	DW200_FE_CMD_REG_WRITE =
		1, /**< Write immediate values into SW registers of IP */
	DW200_FE_CMD_END = 2, /**< The last command in this command buffer */
	DW200_FE_CMD_NOP = 3 /**< Add an empty command to */
};

union cmd_wreg_u {
	struct cmd_wreg_t {
		uint32_t start_reg_addr : 16; /**< [15:0] Specify the address of
										   the first SW register to be writen*/
		uint32_t length : 10; /**< [25:16] Specify how many SW
										   registers (32 bits for each) should
										   be writen; 0:1024, 1:1, ?? , 1023:1023;
										   length is fixed to 1 in current driver*/
		uint32_t rsv : 1; /**< [26] Reserved for future */
		uint32_t op_code : 5; /**< [31:27] opcode = 5??h01 */
	} cmd;
	uint32_t all;
};

struct dw200_fe_cmd_wreg_t {
	union cmd_wreg_u fe_cmd;
	uint32_t w_data;
};

union cmd_end_u {
	struct cmd_end_t {
		uint32_t event_id : 5; /**< [4:0] ID indicator when this cmd is
												executed, set as 5'h1A*/
		uint32_t rsv0 : 3; /**< [7:5] Reserved for future*/
		uint32_t event_enable : 1; /**< [8] Event enable when this cmd is
											 executed, set as 1'b1 */
		uint32_t rsv1 : 18; /**< [26:9] Reserved for future*/
		uint32_t op_code : 5; /**< [31:27]opcode = 5??h02 */
	} cmd;
	uint32_t all;
};

struct dw200_fe_cmd_end_t {
	union cmd_end_u fe_cmd;
	uint32_t rsv2; /**< Reserved for future */
};

union cmd_nop_u {
	struct cmd_nop_t {
		uint32_t rsv0 : 27; /**< [26:0] Reserved for future*/
		uint32_t op_code : 5; /**< [31:27]opcode = 5??h03 */
	} cmd;
	uint32_t all;
};

struct dw200_fe_cmd_nop_t {
	union cmd_nop_u fe_cmd;
	uint32_t rsv1; /**< Reserved for future */
};

union dw200_fe_cmd_u {
	struct dw200_fe_cmd_wreg_t cmd_wreg;
	struct dw200_fe_cmd_end_t cmd_end;
	struct dw200_fe_cmd_nop_t cmd_nop;
};

struct dw200_fe_cmd_buffer_t {
	//struct list_head	cmd_buffer_entry;
	uint32_t curr_cmd_num;
	uint32_t cmd_num_max;
	union dw200_fe_cmd_u *cmd_buffer;
#ifdef __KERNEL__
	dma_addr_t cmd_dma_addr;
#endif
};

struct dw200_fe_status_regs_t {
	uint32_t reg_base;
	uint32_t reg_num;
	uint32_t reg_step; //4/8/12/...
};

struct dw200_fe_tbl_buffer_t {
	uint16_t params_num;
	bool updated_flag;
	uint32_t tbl_reg_offset;
	uint16_t curr_index;
	struct dw200_fe_cmd_wreg_t *tbl_cmd_buffer;
};

struct dw200_fe_context {
	bool enable;
	enum fe_work_mode_e work_mode;
#ifdef __KERNEL__
	struct completion fe_completion;
	spinlock_t cmd_buffer_lock;
#endif
	enum dw200_fe_state state;
	bool fst_wr_flag; //first write
#define DW200_FE_REG_OFFSET_BYTE_MAX (0x0F00)
#define DW200_FE_REG_SIZE (4) /* Byte */
#define DW200_FE_REG_NUM (DW200_FE_REG_OFFSET_BYTE_MAX / DW200_FE_REG_SIZE)
#define DW200_FE_REG_BITS (5) /* 2^5=32 bits */
#define DW200_FE_REG_BITS_MASK (DW200_FE_REG_SIZE * 8 - 1)
#define DW200_FE_REG_MAPPED_BYTES (32 * DW200_FE_REG_SIZE)
#define DW200_FE_REG_MAPPED_BITS (7) /* 2^7 = REG_MAPPED_BYTES*/
#define DW200_FE_REG_UPDATED_NUM \
	(DW200_FE_REG_OFFSET_BYTE_MAX / DW200_FE_REG_MAPPED_BYTES)
#define DW200_FE_REG_PART_REFRESH_NUM (0x200)
#define DW200_FE_FIXED_OFFSET_READ_NUM (256)
#define DW200_FE_STATUS_REGS_MAX (12)
#define DW200_FE_TBL_REG_MAX (1)

	/*
#define ISP_FE_MEM_MODE_RESERVED  (0)
#define ISP_FE_MEM_MODE_DMA_ALLOC (1)
*/
	uint32_t *reg_buffer; /* Store the default value of isp registers.
	                               It can use the cached buffer */
	uint8_t prev_index; /* Speed up the search for LUT buffer index*/
	uint8_t tbl_reg_addr_num; /*number of register address for LUT*/
	uint16_t tbl_total_params_num; /*the total number of params for LUT*/
	struct dw200_fe_tbl_buffer_t tbl_buffer[DW200_FE_TBL_REG_MAX];

	struct dw200_fe_cmd_buffer_t refresh_full_regs;
	struct dw200_fe_cmd_buffer_t refresh_part_regs;

	uint32_t rd_index;
	uint32_t fixed_reg_rd_num; /*statics register which has the fixed offset*/
	uint32_t *fixed_reg_buffer; /* only store the value of statics register
								which has the fixed offset*/
	uint8_t status_regs_num;
	struct dw200_fe_status_regs_t status_regs[DW200_FE_STATUS_REGS_MAX];

	uint8_t dw200_ctrl_enable;
	uint8_t dw200_vse_enable;
	uint8_t dw200_dwe_enable;
};

enum {
	DWE_INPUT_BUFFER_0 = 0,
	DWE_OUTPUT_BUFFER_0,
	DWE_INPUT_LUT_BUFFER,
	VSE_INPUT_BUFFER_0,
	VSE_OUTPUT_BUFFER_0,
	VSE_OUTPUT_BUFFER_1,
	VSE_OUTPUT_BUFFER_2,
	DW200_BUFFER_INDEX
};

#define DWE_DMABUF_DONE (1 << 0)
#define VSE_DMABUF_DONE (1 << 1)

typedef struct _dw_clk_rst {
	struct clk *aclk;
	struct clk *cfg_clk;
	struct clk *dw_aclk;
	struct clk *aclk_mux;
	struct clk *dw_mux;
	struct clk *spll0_fout1;
	struct clk *vpll_fout1;
	struct reset_control *rstc_axi;
	struct reset_control *rstc_cfg;
	struct reset_control *rstc_dwe;
} dw_clk_rst_t;

struct buffer_info {
	u32 buffer_type;
	u32 offset[3];
	u32 stride[3]; //Y/U/V stride
	u64 addr;
};

struct dw200_subdev {
	struct dwe_hw_info dwe_info;
	struct vse_params vse_info;
	void __iomem *dwe_base;
	void __iomem *dwe_reset;
	void __iomem *vse_base;
	void __iomem *vse_reset;
	vivdw200_mis_list_t dwe_circle_list;
	vivdw200_mis_list_t vse_circle_list;
#ifdef __KERNEL__
	struct heap_root *pheap_root;
	struct heap_mem *import_dmabuf[DW200_BUFFER_INDEX];
	struct work_struct dmabuf_work;
	wait_queue_head_t dmabuf_head;
	int dmabuf_flag;
#endif
	struct device *dev; //used for dma alloc
	struct dw200_fe_context fe;
	struct dentry *dw200_reset;
	dw_clk_rst_t dw_crg;
	struct buffer_info buf_info[DW200_BUFFER_INDEX];
};

void dwe_write_reg(struct dw200_subdev *dev, u32 offset, u32 val);
u32 dwe_read_reg(struct dw200_subdev *dev, u32 offset);
void vse_write_reg(struct dw200_subdev *dev, u32 offset, u32 val);
u32 vse_read_reg(struct dw200_subdev *dev, u32 offset);

#endif // _DW200_DEV_H
