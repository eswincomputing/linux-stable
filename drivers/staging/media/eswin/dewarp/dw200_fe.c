/****************************************************************************
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 VeriSilicon Holdings Co., Ltd.
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
 * Copyright (c) 2021 VeriSilicon Holdings Co., Ltd.
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
#ifdef __KERNEL__

#ifdef __KERNEL__
#include <linux/io.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/of_reserved_mem.h>
#endif

#include "dw200_ioctl.h"
#include "dw200_fe.h"

#define VIV_DW200_FE_DMA_TIMOUT_MS 1000
#define VIV_DW200_FE_DEBUG_DUMP 0

#ifndef __KERNEL__
extern HalHandle_t hal_handle; //c-model
#endif

void dw200_fe_raw_write_reg(struct dw200_subdev *dev, u32 offset, u32 val)
{
	if (offset >= DW200_FE_REG_OFFSET_BYTE_MAX)
		return;
	//pr_info("fe raw write addr 0x%08x val 0x%08x\n",offset,val);
	__raw_writel(val, dev->vse_base + offset);
}

u32 dw200_fe_raw_read_reg(struct dw200_subdev *dev, u32 offset)
{
	u32 val = 0;

	if (offset >= DW200_FE_REG_OFFSET_BYTE_MAX)
		return 0;
	val = __raw_readl(dev->vse_base + offset);
	//pr_info("fe raw read addr 0x%08x val 0x%08x\n",offset,val);
	return val;
}

static uint32_t dw200_fe_hash_map(uint32_t offset)
{
	uint32_t mapped_index = 0;
	if (offset < DW200_FE_REG_OFFSET_BYTE_MAX) {
		//map method 2
		//mapped_index =
		//((uint32_t)ISP_FE_REG_OFFSET_BYTE_MAX - 4 - offset) / ISP_FE_REG_SIZE;
		//map method 3
		if (offset == DEWARP_CTRL + DEWARP_REGISTER_BASE_ADDR) {
			mapped_index = (DW200_FE_REG_OFFSET_BYTE_MAX - 0x0C) /
				       DW200_FE_REG_SIZE;
		} else if (offset == VSE_REG_CTRL) {
			mapped_index = (DW200_FE_REG_OFFSET_BYTE_MAX - 0x10) /
				       DW200_FE_REG_SIZE;
		} else {
			mapped_index = offset / DW200_FE_REG_SIZE;
		}
	} else {
		mapped_index = offset / DW200_FE_REG_SIZE;
	}
	return mapped_index;
}

#if VIV_DW200_FE_DEBUG_DUMP == 1
static void dw200_fe_cmdbuffer_dump(struct dw200_fe_cmd_buffer_t *refresh_regs)
{
	int j;
	for (j = 0; j < refresh_regs->curr_cmd_num; j++) {
		if (refresh_regs->cmd_buffer[j].cmd_wreg.fe_cmd.cmd.op_code ==
			    DW200_FE_CMD_REG_WRITE &&
		    refresh_regs->cmd_buffer[j].cmd_wreg.w_data != 0) {
			pr_info("0x%04x=0x%08x\n",
				refresh_regs->cmd_buffer[j]
					.cmd_wreg.fe_cmd.cmd.start_reg_addr,
				refresh_regs->cmd_buffer[j].cmd_wreg.w_data);
		}
	}
	pr_info("\n");
}

static void dw200_fe_cmdbuffer_parse(struct dw200_fe_cmd_buffer_t *refresh_regs)
{
	union dw200_fe_cmd_u *cmd_buffer;
	uint32_t i;
	cmd_buffer = refresh_regs->cmd_buffer;

	for (i = 0; i < refresh_regs->curr_cmd_num; i++) {
		switch (cmd_buffer[i].cmd_wreg.fe_cmd.cmd.op_code) {
		case DW200_FE_CMD_REG_WRITE:
			pr_info("off %04x: WR 0x%04x len=%d all=0x%08x data=0x%08x\n",
				i,
				cmd_buffer[i].cmd_wreg.fe_cmd.cmd.start_reg_addr,
				cmd_buffer[i].cmd_wreg.fe_cmd.cmd.length,
				cmd_buffer[i].cmd_wreg.fe_cmd.all,
				cmd_buffer[i].cmd_wreg.w_data);
			break;
		case DW200_FE_CMD_END:
			pr_info("off %04x: END all=0x%08x\n", i,
				cmd_buffer[i].cmd_end.fe_cmd.all);
			break;
		case DW200_FE_CMD_NOP:
			pr_info("off %04x: NOP all=0x%08x\n", i,
				cmd_buffer[i].cmd_nop.fe_cmd.all);
			break;
		default:
			pr_info("off %04x: ERR all=0x%08x\n", i,
				cmd_buffer[i].cmd_wreg.fe_cmd.all);
			break;
		}
	}
}
#endif

static int dw200_fe_buffer_memset(union dw200_fe_cmd_u *cmd_buffer,
				  u32 buffer_num)
{
	int32_t i;
	struct dw200_fe_cmd_nop_t cmd_nop;
	if (cmd_buffer) {
		cmd_nop.fe_cmd.cmd.rsv0 = 0;
		cmd_nop.fe_cmd.cmd.op_code = DW200_FE_CMD_NOP;
		cmd_nop.rsv1 = 0;
		for (i = 0; i < buffer_num; i++) {
			cmd_buffer[i].cmd_nop = cmd_nop;
		}
	}
	return 0;
}

static int dw200_fe_get_reg_def_val(struct dw200_subdev *dev)
{
	//TODO: Will add this default value later

	uint32_t i;
	union dw200_fe_cmd_u *full_cmd_buffer;
	uint32_t mapped_index;
	struct dw200_fe_context *fe = &dev->fe;
	pr_info("enter %s\n", __func__);

	full_cmd_buffer = fe->refresh_full_regs.cmd_buffer;
	for (i = 0; i < DW200_FE_REG_NUM; i++) {
		//read the default register value from isp hardware
		//before isp startup
#ifndef __KERNEL__
		if (fe->state == DW200_FE_STATE_INIT) {
			fe->reg_buffer[i] =
				vse_read_reg(dev, i * DW200_FE_REG_SIZE);
		}
#else
		if (fe->state == DW200_FE_STATE_INIT) {
			fe->reg_buffer[i] = dw200_fe_raw_read_reg(
				dev, i * DW200_FE_REG_SIZE);
		}
#endif
		mapped_index = dw200_fe_hash_map(i * DW200_FE_REG_SIZE);
		full_cmd_buffer[mapped_index].cmd_wreg.w_data =
			fe->reg_buffer[i];
	}
	//set lut register to nop in full cmd buffer
	for (i = 0; i < fe->tbl_reg_addr_num; i++) {
		mapped_index =
			dw200_fe_hash_map(fe->tbl_buffer[i].tbl_reg_offset);
		dw200_fe_buffer_memset(&full_cmd_buffer[mapped_index], 1);
	}
#if VIV_DW200_FE_DEBUG_DUMP == 1
	dw200_fe_cmdbuffer_dump(&fe->refresh_full_regs);
#endif

	fe->state = DW200_FE_STATE_READY;
	return 0;
}

int dw200_fe_reset_all(struct dw200_subdev *dev)
{
	return 0;
}

static bool is_start_fe_dma(struct dw200_subdev *dev, u32 offset, u32 val)
{ //init start or MCM start
	bool ret = false;
	struct dw200_fe_context *fe = &dev->fe;
	// uint32_t mapped_index;

	//Case 1: Dewarp start
	if ((DEWARP_CTRL + DEWARP_REGISTER_BASE_ADDR == offset) &&
	    (val & DWE_START_MASK) == DWE_START_MASK) {
		fe->dw200_dwe_enable = 1;
	}
	//Case 1: VSE start
	if ((VSE_REG_CTRL == offset) &&
	    (val & (1 << VSE_CONTROL_DMA_FRAME_START_BIT)) ==
		    (1 << VSE_CONTROL_DMA_FRAME_START_BIT)) {
		fe->dw200_vse_enable = 1;
	}

	if (fe->dw200_vse_enable == 1 || fe->dw200_dwe_enable == 1) {
		fe->dw200_dwe_enable = 0;
		fe->dw200_vse_enable = 0;
		pr_info("dw200 dma start \n");
		return true;
	}

	return ret;
}

#if 0
//TODO: Fe Stop need update
static bool is_stop_fe_dma(struct dw200_subdev *dev, u32 offset, u32 val)
{//stop
    bool ret = false;
    struct dw200_fe_context *fe = &dev->fe;

//Case 1: Dewarp start
    if((DEWARP_CTRL+DEWARP_REGISTER_BASE_ADDR == offset) &&
       (val & DWE_START_MASK) == DWE_START_MASK) {
        fe->dw200_dwe_enable = 0;
    }
//Case 1: VSE start
    if((VSE_REG_CTRL == offset) &&
       (val & VSE_CONTROL_DMA_FRAME_START_BIT) == VSE_CONTROL_DMA_FRAME_START_BIT) {
        fe->dw200_vse_enable = 0;
    }

    if(fe->dw200_vse_enable == 0 || fe->dw200_dwe_enable == 0) {
        return true;
    }

    return ret;
}
#endif

int dw200_fe_read_reg(struct dw200_subdev *dev, u32 offset, u32 *val)
{
	union dw200_fe_cmd_u *full_cmd_buffer;
	uint32_t mapped_index;
	struct dw200_fe_context *fe = &dev->fe;
	full_cmd_buffer = fe->refresh_full_regs.cmd_buffer;
	mapped_index = dw200_fe_hash_map(offset);

	//read register directly
	if (((INTERRUPT_STATUS + DEWARP_REGISTER_BASE_ADDR) == offset) ||
	    (DW200_REG_FE_MIS == offset) || (VSE_REG_MI_MSI == offset) ||
	    (VSE_REG_MI_MSI1 == offset)) {
		*val = dw200_fe_raw_read_reg(dev, offset);
		full_cmd_buffer[mapped_index].cmd_wreg.w_data = *val;
	} else {
		*val = full_cmd_buffer[mapped_index].cmd_wreg.w_data;
	}
	return 0;
}
int dw200_fe_write_tbl(struct dw200_subdev *dev, u32 offset, u32 val)
{
	int ret = 0;
	// struct dw200_fe_cmd_wreg_t *tbl_cmd_buffer;
	// union dw200_fe_cmd_u  *part_cmd_buffer;
	// struct dw200_fe_context *fe = &dev->fe;
	// uint8_t tbl_index;
	// uint16_t  params_index;
//TODO: Will add Dw200 table later
#if 0
    tbl_index = fe->prev_index;
    do {//optimize the search algorithm
        if (fe->tbl_buffer[tbl_index].tbl_reg_offset == offset) {
//pr_info("bbc--offset=%x tbl_index=%d\n", offset, tbl_index);
            tbl_cmd_buffer = fe->tbl_buffer[tbl_index].tbl_cmd_buffer;
            params_index = fe->tbl_buffer[tbl_index].curr_index++;
//pr_info("bbc--tbl_cmd_buffer=%p params_index=%d\n", tbl_cmd_buffer, params_index);
            if (fe->tbl_buffer[tbl_index].curr_index >= fe->tbl_buffer[tbl_index].params_num) {
                fe->tbl_buffer[tbl_index].curr_index = 0;
            }
            tbl_cmd_buffer[params_index].fe_cmd.cmd.start_reg_addr = offset;
            tbl_cmd_buffer[params_index].fe_cmd.cmd.length = 1;
            tbl_cmd_buffer[params_index].fe_cmd.cmd.rsv = 0;
            tbl_cmd_buffer[params_index].fe_cmd.cmd.op_code = DW200_FE_CMD_REG_WRITE;
            tbl_cmd_buffer[params_index].w_data = val;
            fe->prev_index = tbl_index; //for next time search

            if (fe->refresh_part_regs.curr_cmd_num < DW200_FE_REG_PART_REFRESH_NUM) {
                part_cmd_buffer = &fe->refresh_part_regs.cmd_buffer[fe->refresh_part_regs.curr_cmd_num];
                part_cmd_buffer->cmd_wreg.fe_cmd.cmd.start_reg_addr = offset;
                part_cmd_buffer->cmd_wreg.fe_cmd.cmd.length = 1;
                part_cmd_buffer->cmd_wreg.fe_cmd.cmd.rsv = 0;
                part_cmd_buffer->cmd_wreg.fe_cmd.cmd.op_code = DW200_FE_CMD_REG_WRITE;
                part_cmd_buffer->cmd_wreg.w_data = val;
                fe->refresh_part_regs.curr_cmd_num++;
            }
            break;
        }
        tbl_index++;//(tbl_index + 1) % fe->tbl_reg_addr_num;
        if (tbl_index == fe->tbl_reg_addr_num) {
            tbl_index = 0;
        }
    } while(tbl_index != fe->prev_index);
#endif
	return ret;
}

static void dw200_fe_buffer_append_end(union dw200_fe_cmd_u *cmd_buffer,
				       u32 *curr_num)
{
	uint8_t append_num = 0;
	if ((*curr_num & 0x01) == 0) {
		cmd_buffer[0].cmd_nop.fe_cmd.cmd.rsv0 = 0;
		cmd_buffer[0].cmd_nop.fe_cmd.cmd.op_code = DW200_FE_CMD_NOP;
		cmd_buffer[0].cmd_nop.rsv1 = 0;
		*curr_num = *curr_num + 1;
		append_num = 1;
	}
	cmd_buffer[append_num].cmd_end.fe_cmd.cmd.event_id = 0x1A;
	cmd_buffer[append_num].cmd_end.fe_cmd.cmd.rsv0 = 0;
	cmd_buffer[append_num].cmd_end.fe_cmd.cmd.event_enable = 1;
	cmd_buffer[append_num].cmd_end.fe_cmd.cmd.rsv1 = 0;
	cmd_buffer[append_num].cmd_end.fe_cmd.cmd.op_code = DW200_FE_CMD_END;
	cmd_buffer[append_num].cmd_end.rsv2 = 0;
	*curr_num = *curr_num + 1;
}

static void dw200_fe_shd_to_immediately(u32 offset, u32 *val)
{
	//TODO: Dw200 seems not need update immediately, Will update later

	/* if (offset == REG_ADDR(isp_ctrl)) {
        REG_SET_SLICE(*val, MRV_ISP_ISP_GEN_CFG_UPD, 0);
        REG_SET_SLICE(*val, MRV_ISP_ISP_CFG_UPD_PERMANENT, 0);
        REG_SET_SLICE(*val, MRV_ISP_ISP_CFG_UPD, 1);
    } else if (offset == REG_ADDR(isp_stitching_ctrl) ||
               offset == 0x3800 || offset == 0x4600 || offset == 0x4B00) {
        REG_SET_SLICE(*val, STITCHING_GEN_CFG_UPD, 0);
        REG_SET_SLICE(*val, STITCHING_GEN_CFG_UPD_FIX, 0);
        REG_SET_SLICE(*val, STITCHING_CFG_UPD, 1);
    }//isp_stitching0_ctrl ~ isp_stitching3_ctrl */
}

static void dw200_fe_busid_filter(u32 offset, u32 *val)
{
	//TODO: Change to DW200 PATH
	uint8_t dwe_rd_id, dwe_wr_id;
	if (offset == BUS_CTRL) {
		REG_SET_SLICE(*val, DWE_BUS_SW_EN, 1U);
		REG_SET_SLICE(*val, DWE_RD_ID_EN, 1U);
		dwe_wr_id = (uint8_t)REG_GET_SLICE(*val, DWE_WR_ID_CFG);
		dwe_rd_id = dwe_wr_id ^ 0x80;
		REG_SET_SLICE(*val, DWE_RD_ID_CFG, (u32)dwe_rd_id);
	}
}

int dw200_fe_set_dma(struct dw200_subdev *dev,
		     struct dw200_fe_cmd_buffer_t *refresh_regs)
{
	//TODO: dewarp register without offset 0xc00. but VSE register had this offset. Need modify later.
	union dw200_fe_cmd_u *full_cmd_buffer;
	uint32_t mapped_index;
	u32 dwe_bus_id;

	struct dw200_fe_context *fe = &dev->fe;
	dw200_fe_raw_write_reg(dev, DW200_REG_FE_CTL, 0x00000000);

	full_cmd_buffer = fe->refresh_full_regs.cmd_buffer;
	mapped_index = dw200_fe_hash_map(DEWARP_REGISTER_BASE_ADDR + BUS_CTRL);
	dwe_bus_id = full_cmd_buffer[mapped_index].cmd_wreg.w_data;
	dw200_fe_busid_filter(DEWARP_REGISTER_BASE_ADDR + BUS_CTRL,
			      &dwe_bus_id);
	full_cmd_buffer[mapped_index].cmd_wreg.w_data = dwe_bus_id;
	dw200_fe_raw_write_reg(dev, DEWARP_REGISTER_BASE_ADDR + BUS_CTRL,
			       DEWRAP_BUS_CTRL_ENABLE_MASK |
				       dwe_bus_id); //0x0DCABD1E

	dw200_fe_raw_write_reg(dev, DW200_REG_FE_CTL, 0x00000001);
	dw200_fe_raw_write_reg(dev, DW200_REG_FE_IMSC, 0x00000001);
	dw200_fe_raw_write_reg(dev, DW200_REG_FE_DMA_AD,
			       refresh_regs->cmd_dma_addr);
	fe->state = DW200_FE_STATE_RUNNING;
	pr_info("bbc----refresh_regs->cmd_dma_addr=%08llx\n",
		refresh_regs->cmd_dma_addr);
	pr_info("bbc----refresh_regs->curr_cmd_num=%08x\n",
		refresh_regs->curr_cmd_num);
	dw200_fe_raw_write_reg(dev, DW200_REG_FE_DMA_START,
			       0x00010000 | refresh_regs->curr_cmd_num);
	refresh_regs->curr_cmd_num = 0;
	return 0;
}

/*call when irq*/
int dw200_fe_writeback_status(struct dw200_subdev *dev)
{
//TODO: dewarp without satatus need wirte back, will add this function in the future
#if 0
    //3A statics HIST64
    union dw200_fe_cmd_u *full_cmd_buffer;
    uint32_t mapped_index, i, reg_index, offset;
    struct dw200_fe_context *fe = &dev->fe;

    full_cmd_buffer = fe->refresh_full_regs.cmd_buffer;
    for (i = 0; i < (uint32_t)fe->status_regs_num; i++) {
        offset = fe->status_regs[i].reg_base;
        for (reg_index = 0; reg_index < fe->status_regs[i].reg_num; reg_index++) {
            mapped_index = dw200_fe_hash_map(offset);
            full_cmd_buffer[mapped_index].cmd_wreg.w_data =
                                                __raw_readl(dev->vse_base + offset);
            offset = offset + fe->status_regs[i].reg_step;
        }
    }
#endif
	return 0;
}

void dw200_fe_dw200_irq_work(struct dw200_subdev *dev)
{
	struct dw200_fe_context *fe = &dev->fe;
	dw200_fe_writeback_status(dev);

	if (fe->refresh_part_regs.curr_cmd_num ==
	    0) { //no register to be updated
		return;
	}

	if (fe->refresh_part_regs.curr_cmd_num <
	    DW200_FE_REG_PART_REFRESH_NUM) {
		//refresh part registers
		dw200_fe_buffer_append_end(
			&fe->refresh_part_regs
				 .cmd_buffer[fe->refresh_part_regs.curr_cmd_num],
			&fe->refresh_part_regs.curr_cmd_num);
		pr_info("bbc--%s: refresh_part_regs.curr_cmd_num=0x%08x\n",
			__func__, fe->refresh_part_regs.curr_cmd_num);
		//config dma
		dw200_fe_raw_write_reg(dev, DW200_REG_FE_CTL, 0x00000001);
		dw200_fe_raw_write_reg(dev, DW200_REG_FE_DMA_AD,
				       (u32)fe->refresh_part_regs.cmd_dma_addr);
		dw200_fe_raw_write_reg(
			dev, DW200_REG_FE_DMA_START,
			0x00010000 | fe->refresh_part_regs.curr_cmd_num);
		fe->state = DW200_FE_STATE_RUNNING;
	} else { //refresh the whole registers
		//config dma
		pr_info("bbc--%s: refresh_full_regs.curr_cmd_num=0x%08x\n",
			__func__, fe->refresh_full_regs.curr_cmd_num);
		dw200_fe_raw_write_reg(dev, DW200_REG_FE_CTL, 0x00000001);
		dw200_fe_raw_write_reg(dev, DW200_REG_FE_DMA_AD,
				       (u32)fe->refresh_full_regs.cmd_dma_addr);
		dw200_fe_raw_write_reg(
			dev, DW200_REG_FE_DMA_START,
			0x00010000 | fe->refresh_full_regs.curr_cmd_num);
		fe->state = DW200_FE_STATE_RUNNING;
	}
}

int dw200_fe_write_reg(struct dw200_subdev *dev, u32 offset, u32 val)
{
	unsigned long flags;
	union dw200_fe_cmd_u *part_cmd_buffer;
	union dw200_fe_cmd_u *full_cmd_buffer;
	u32 mapped_index;
	//    u32 reg_updated_mask;
	struct dw200_fe_context *fe = &dev->fe;

	spin_lock_irqsave(&fe->cmd_buffer_lock, flags);
	spin_unlock_irqrestore(&fe->cmd_buffer_lock, flags);
	//pr_info("fe write addr 0x%08x val 0x%08x\n",offset,val);
	//TODO: Will add this default value later.The first time using the part register buffer first.
	if (fe->fst_wr_flag == true) {
		pr_info("bbc--frst offset=0x%08x val=0x%x\n", offset, val);
		dw200_fe_get_reg_def_val(
			dev); /*update the reset value when writing register at first time*/
		fe->fst_wr_flag = false;
	}

	//write register directly
	if (VSE_REG_MI_ICR == offset || VSE_REG_MI_ICR1 == offset ||
	    DW200_REG_FE_ICR == offset || DW200_REG_FE_CTL == offset ||
	    ((INTERRUPT_STATUS + DEWARP_REGISTER_BASE_ADDR == offset) &&
	     (val & DWE_CLR_STATUS))) {
		//TODO: dewarp clear and mask used the same rigister "interrupt_status"
		dw200_fe_raw_write_reg(dev, offset, val);
	} else {
		if (fe->state == DW200_FE_STATE_RUNNING) {
			//block the write when FE is running, add completion for FE
			if (wait_for_completion_timeout(
				    &fe->fe_completion,
				    msecs_to_jiffies(
					    VIV_DW200_FE_DMA_TIMOUT_MS)) == 0) {
				pr_info("%s error: wait FE DMA time out!",
					__func__);
				return -ETIMEDOUT;
			}
		}
		dw200_fe_busid_filter(offset, &val);
		//mask some ctrl enable and shadow enable
		dw200_fe_shd_to_immediately(offset, &val);

		//reset isp
		//TODO reset dewarp fe engine

		//fe->reg_buffer[offset / dw200_fe_REG_SIZE] = val;
		mapped_index = dw200_fe_hash_map(offset);
		full_cmd_buffer = fe->refresh_full_regs.cmd_buffer;
		full_cmd_buffer[mapped_index]
			.cmd_wreg.fe_cmd.cmd.start_reg_addr = offset;
		full_cmd_buffer[mapped_index].cmd_wreg.fe_cmd.cmd.length = 1;
		full_cmd_buffer[mapped_index].cmd_wreg.fe_cmd.cmd.rsv = 0;
		full_cmd_buffer[mapped_index].cmd_wreg.fe_cmd.cmd.op_code =
			DW200_FE_CMD_REG_WRITE;
		full_cmd_buffer[mapped_index].cmd_wreg.w_data = val;

		if (fe->refresh_part_regs.curr_cmd_num <
		    DW200_FE_REG_PART_REFRESH_NUM) {
			part_cmd_buffer =
				&fe->refresh_part_regs.cmd_buffer
					 [fe->refresh_part_regs.curr_cmd_num];
			part_cmd_buffer->cmd_wreg.fe_cmd.cmd.start_reg_addr =
				offset;
			part_cmd_buffer->cmd_wreg.fe_cmd.cmd.length = 1;
			part_cmd_buffer->cmd_wreg.fe_cmd.cmd.rsv = 0;
			part_cmd_buffer->cmd_wreg.fe_cmd.cmd.op_code =
				DW200_FE_CMD_REG_WRITE;
			part_cmd_buffer->cmd_wreg.w_data = val;
			fe->refresh_part_regs.curr_cmd_num++;
		}

		pr_info("%s fe->refresh_part_regs.curr_cmd_num = %d \n",
			__func__, fe->refresh_part_regs.curr_cmd_num);
		pr_info("%s fe state %d \n", __func__, fe->state);
		if (fe->state == DW200_FE_STATE_READY &&
		    is_start_fe_dma(dev, offset, val)) {
			if (fe->refresh_part_regs.curr_cmd_num <
			    DW200_FE_REG_PART_REFRESH_NUM) {
				//refresh part registers
				dw200_fe_buffer_append_end(
					&fe->refresh_part_regs.cmd_buffer
						 [fe->refresh_part_regs
							  .curr_cmd_num],
					&fe->refresh_part_regs.curr_cmd_num);
				//config dma
				dw200_fe_set_dma(dev, &fe->refresh_part_regs);

			} else { //refresh the whole registers
				//config dma
#if VIV_DW200_FE_DEBUG_DUMP == 1
				dw200_fe_cmdbuffer_dump(&fe->refresh_full_regs);
#endif
				dw200_fe_set_dma(dev, &fe->refresh_full_regs);
			}
		} else {
		}
	}

	return 0;
}

static int dw200_fe_tbl_reg_addr_init(struct dw200_subdev *dev)
{
	uint8_t i = 0, j;
	struct dw200_fe_context *fe = &dev->fe;

	//TODO: No Fe TBL need update now.

	fe->prev_index = 0;
	/* tbl buffer = ... */

	if (i > DW200_FE_TBL_REG_MAX) {
		return -ENOMEM;
	}
	fe->tbl_reg_addr_num = i;
	fe->tbl_total_params_num = 0;
	for (j = 0; j < fe->tbl_reg_addr_num; j++) {
		fe->tbl_buffer[j].curr_index = 0;
		fe->tbl_total_params_num += fe->tbl_buffer[j].params_num;
	}

	return 0;
}

static void dw200_fe_status_regs_init(struct dw200_subdev *dev)
{
	struct dw200_fe_context *fe = &dev->fe;
	uint8_t reg_block_num;
	//TODO: No Fe status need update now.

	/*initial dw200 status
   */

	reg_block_num = 0;

	fe->status_regs_num = reg_block_num;
}

int dw200_fe_reset(struct dw200_subdev *dev)
{
	int ret = 0;
	struct dw200_fe_context *fe = &dev->fe;
	pr_info("%s: enter!\n", __func__);

	if (!fe->refresh_full_regs.cmd_buffer) {
		pr_info("%s: alloc refresh_all_regs error!\n", __func__);
		goto alloc_all_err;
	}

	dw200_fe_buffer_memset(fe->refresh_full_regs.cmd_buffer,
			       fe->refresh_full_regs.cmd_num_max);
	fe->refresh_full_regs.curr_cmd_num =
		DW200_FE_REG_NUM + fe->tbl_total_params_num;
	dw200_fe_buffer_append_end(
		&fe->refresh_full_regs
			 .cmd_buffer[fe->refresh_full_regs.curr_cmd_num],
		&fe->refresh_full_regs.curr_cmd_num);

	fe->refresh_part_regs.cmd_num_max = DW200_FE_REG_PART_REFRESH_NUM;
	fe->refresh_part_regs.curr_cmd_num = 0;

	if (!fe->refresh_part_regs.cmd_buffer) {
		pr_info("%s: alloc refresh_part_regs error!\n", __func__);
		goto alloc_part_err;
	}

	dw200_fe_buffer_memset(fe->refresh_part_regs.cmd_buffer,
			       fe->refresh_part_regs.curr_cmd_num);

	dw200_fe_status_regs_init(dev);

	fe->state = DW200_FE_STATE_INIT;
	fe->fst_wr_flag = true;

	return ret;

alloc_part_err:
	dma_free_coherent(dev->dev,
			  sizeof(union dw200_fe_cmd_u) *
				  fe->refresh_full_regs.cmd_num_max,
			  fe->refresh_full_regs.cmd_buffer,
			  fe->refresh_full_regs.cmd_dma_addr);

alloc_all_err:
	kfree(fe->fixed_reg_buffer);

	return -ENOMEM;
}

int dw200_fe_init(struct dw200_subdev *dev)
{
	int ret, i;
	struct dw200_fe_context *fe = &dev->fe;
	if (fe->reg_buffer) {
		kfree(fe->reg_buffer);
	}

	fe->reg_buffer =
		(u32 *)kmalloc(DW200_FE_REG_OFFSET_BYTE_MAX, GFP_KERNEL);
	if (!fe->reg_buffer) {
		pr_info("%s: alloc reg_buffer error!\n", __func__);
		return -ENOMEM;
	}

	init_completion(&fe->fe_completion);

	fe->rd_index = 0;
	fe->fixed_reg_rd_num = DW200_FE_FIXED_OFFSET_READ_NUM;
	fe->fixed_reg_buffer = (uint32_t *)kmalloc(
		fe->fixed_reg_rd_num * sizeof(uint32_t), GFP_KERNEL);
	if (!fe->fixed_reg_buffer) {
		pr_info("%s: alloc fixed_reg_buffer error!\n", __func__);
		goto alloc_fixed_buf_err;
	}

	dw200_fe_tbl_reg_addr_init(dev);
	fe->refresh_full_regs.cmd_num_max = DW200_FE_REG_NUM +
					    fe->tbl_total_params_num +
					    2; //2--end or nop+end flag
	//fe->refresh_full_regs.cmd_num_max = (fe->refresh_full_regs.cmd_num_max + 8 - 1) & (~(8 - 1));

	if (!dev->dev->dma_mask) {
		dev->dev->dma_mask = &dev->dev->coherent_dma_mask;
	}

	ret = dma_set_coherent_mask(dev->dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		pr_info("%s: Cannot configure coherent dma mask!\n", __func__);
		goto alloc_all_err;
	}

	fe->refresh_full_regs.cmd_buffer =
		(union dw200_fe_cmd_u *)dma_alloc_coherent(
			dev->dev,
			sizeof(union dw200_fe_cmd_u) *
				fe->refresh_full_regs.cmd_num_max,
			&fe->refresh_full_regs.cmd_dma_addr,
			GFP_KERNEL); //GFP_KERNEL
	pr_info("%s:%d fe->tbl_total_params_num=0x%08x\n", __func__, __LINE__,
		fe->tbl_total_params_num);
	pr_info("%s:%d fe->refresh_full_regs.cmd_num_max=0x%08x\n", __func__,
		__LINE__, fe->refresh_full_regs.cmd_num_max);
	pr_info("%s:%d fe->refresh_full_regs.cmd_buffer=%p\n", __func__,
		__LINE__, fe->refresh_full_regs.cmd_buffer);
	pr_info("%s:%d fe->refresh_full_regs.cmd_dma_addr=0x%llx\n", __func__,
		__LINE__, fe->refresh_full_regs.cmd_dma_addr);
	if (!fe->refresh_full_regs.cmd_buffer) {
		pr_info("%s: alloc refresh_all_regs error!\n", __func__);
		goto alloc_all_err;
	}

	fe->tbl_buffer[0].tbl_cmd_buffer =
		(struct dw200_fe_cmd_wreg_t *)(fe->refresh_full_regs.cmd_buffer +
					       DW200_FE_REG_NUM);
	pr_info("bbc--tbl_buffer[0].tbl_cmd_buffer=%p\n",
		fe->tbl_buffer[0].tbl_cmd_buffer);
	for (i = 1; i < fe->tbl_reg_addr_num; i++) {
		fe->tbl_buffer[i].tbl_cmd_buffer =
			fe->tbl_buffer[i - 1].tbl_cmd_buffer +
			fe->tbl_buffer[i - 1].params_num;
		pr_info("bbc--tbl_buffer[%d].tbl_cmd_buffer=%p\n", i,
			fe->tbl_buffer[i].tbl_cmd_buffer);
	}

	dw200_fe_buffer_memset(fe->refresh_full_regs.cmd_buffer,
			       fe->refresh_full_regs.cmd_num_max);
	fe->refresh_full_regs.curr_cmd_num =
		DW200_FE_REG_NUM + fe->tbl_total_params_num;
	dw200_fe_buffer_append_end(
		&fe->refresh_full_regs
			 .cmd_buffer[fe->refresh_full_regs.curr_cmd_num],
		&fe->refresh_full_regs.curr_cmd_num);

	fe->refresh_part_regs.cmd_num_max = DW200_FE_REG_PART_REFRESH_NUM;
	fe->refresh_part_regs.curr_cmd_num = 0;
	fe->refresh_part_regs.cmd_buffer =
		(union dw200_fe_cmd_u *)dma_alloc_coherent(
			dev->dev,
			sizeof(union dw200_fe_cmd_u) *
				fe->refresh_part_regs.cmd_num_max,
			&fe->refresh_part_regs.cmd_dma_addr, GFP_KERNEL);
	pr_info("%s:%d fe->refresh_part_regs.cmd_num_max=0x%08x\n", __func__,
		__LINE__, fe->refresh_part_regs.cmd_num_max);
	pr_info("%s:%d fe->refresh_part_regs.cmd_buffer=%p\n", __func__,
		__LINE__, fe->refresh_part_regs.cmd_buffer);
	pr_info("%s:%d fe->refresh_part_regs.cmd_dma_addr=0x%llx\n", __func__,
		__LINE__, fe->refresh_part_regs.cmd_dma_addr);
	if (!fe->refresh_part_regs.cmd_buffer) {
		pr_info("%s: alloc refresh_part_regs error!\n", __func__);
		goto alloc_part_err;
	}

	dw200_fe_buffer_memset(fe->refresh_part_regs.cmd_buffer,
			       fe->refresh_part_regs.curr_cmd_num);
	fe->state = DW200_FE_STATE_INIT;

	dw200_fe_status_regs_init(dev);

	//TODO: no full register firstly, will added later
	fe->fst_wr_flag = true;

	return 0;

alloc_part_err:
	dma_free_coherent(dev->dev,
			  sizeof(union dw200_fe_cmd_u) *
				  fe->refresh_full_regs.cmd_num_max,
			  fe->refresh_full_regs.cmd_buffer,
			  fe->refresh_full_regs.cmd_dma_addr);

alloc_all_err:
	kfree(fe->fixed_reg_buffer);

alloc_fixed_buf_err:
	kfree(fe->reg_buffer);

	return -ENOMEM;
}

int dw200_fe_destory(struct dw200_subdev *dev)
{
	struct dw200_fe_context *fe = &dev->fe;

	if (fe->state ==
	    DW200_FE_STATE_RUNNING) { //block the destory when FE is running
		if (wait_for_completion_timeout(
			    &fe->fe_completion,
			    msecs_to_jiffies(VIV_DW200_FE_DMA_TIMOUT_MS)) ==
		    0) {
			pr_info("%s error: wait FE DMA time out!", __func__);
			return -ETIMEDOUT;
		}
	}

	fe->state = DW200_FE_STATE_EXIT;
	fe->fst_wr_flag = true;

	dma_free_coherent(dev->dev,
			  sizeof(union dw200_fe_cmd_u) *
				  fe->refresh_part_regs.cmd_num_max,
			  fe->refresh_part_regs.cmd_buffer,
			  fe->refresh_part_regs.cmd_dma_addr);

	fe->refresh_part_regs.cmd_buffer = NULL;

	dma_free_coherent(dev->dev,
			  sizeof(union dw200_fe_cmd_u) *
				  fe->refresh_full_regs.cmd_num_max,
			  fe->refresh_full_regs.cmd_buffer,
			  fe->refresh_full_regs.cmd_dma_addr);
	fe->refresh_full_regs.cmd_buffer = NULL;

	kfree(fe->fixed_reg_buffer);
	fe->fixed_reg_buffer = NULL;

	kfree(fe->reg_buffer);
	fe->reg_buffer = NULL;

	return 0;
}
#endif