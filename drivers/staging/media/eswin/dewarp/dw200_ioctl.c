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
#ifdef __KERNEL__
// #include "dw200_driver.h"
#include <linux/dmabuf-heap-import-helper.h>
//#include <soc/sifive/sifive_l2_cache.h>
#else
#include <linux/videodev2.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "dw200_ioctl.h"
#include "dwe_regs.h"
#include "vse_regs.h"
#include "dw200_fe.h"
#include <linux/types.h>
#include "dw200_dump.h"
#undef ALIGN_UP
#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))

#ifndef __KERNEL__
#ifdef HAL_CMODEL
#define DEWARP_REGISTER_OFFSET 0xc00
#else
#define DEWARP_REGISTER_OFFSET 0x380000
#endif

pReadBar g_dw200_read_func;
pWriteBar g_dw200_write_func;

void dwe_set_func(pReadBar read_func, pWriteBar write_func)
{
	g_dw200_read_func = read_func;
	g_dw200_write_func = write_func;
}

void dwe_write_reg(struct dw200_subdev *dev, u32 offset, u32 val)
{
	g_dw200_write_func(DEWARP_REGISTER_OFFSET + offset, val);
}

u32 dwe_read_reg(struct dw200_subdev *dev, u32 offset)
{
	u32 data;
	g_dw200_read_func(DEWARP_REGISTER_OFFSET + offset, &data);
	return data;
}

long dw200_copy_data(void *dst, void *src, int size)
{
	if (dst != src)
		memcpy(dst, src, size);
	return 0;
}

void vse_set_func(pVseReadBar read_func, pVseWriteBar write_func)
{
	g_dw200_read_func = read_func;
	g_dw200_write_func = write_func;
}

void vse_write_reg(struct dw200_subdev *dev, u32 offset, u32 val)
{
	g_dw200_write_func(offset, val);
}

u32 vse_read_reg(struct dw200_subdev *dev, u32 offset)
{
	u32 data;
	g_dw200_read_func(offset, &data);
	return data;
}
#else

void dwe_write_reg(struct dw200_subdev *dev, u32 offset, u32 val)
{
	if (dev->fe.enable) {
		dw200_fe_write_reg(dev, DEWARP_REGISTER_BASE_ADDR + offset,
				   val);
	} else {
		__raw_writel(val, dev->dwe_base + offset);
	}
	// pr_info("dwe write addr 0x%08x val 0x%08x\n",DEWARP_REGISTER_BASE_ADDR+offset,val);
}

u32 dwe_read_reg(struct dw200_subdev *dev, u32 offset)
{
	u32 val;
	if (dev->fe.enable) {
		dw200_fe_read_reg(dev, DEWARP_REGISTER_BASE_ADDR + offset,
				  &val);
	} else {
		val = __raw_readl(dev->dwe_base + offset);
	}
	// pr_info("dwe read addr 0x%08x val 0x%08x\n",DEWARP_REGISTER_BASE_ADDR+offset,val);
	return val;
}

void vse_write_reg(struct dw200_subdev *dev, u32 offset, u32 val)
{
	if (dev->fe.enable) {
		dw200_fe_write_reg(dev, offset, val);
	} else {
		__raw_writel(val, dev->vse_base + offset);
	}
	// pr_info("vse write addr 0x%08x val 0x%08x\n",offset,val);
}

u32 vse_read_reg(struct dw200_subdev *dev, u32 offset)
{
	u32 val;
	if (dev->fe.enable) {
		dw200_fe_read_reg(dev, offset, &val);
	} else {
		val = __raw_readl(dev->vse_base + offset);
	}
	// pr_info("vse read addr 0x%08x val 0x%08x\n",offset,val);
	return val;
}

#endif

#if defined(__KERNEL__)
static u64 get_dma_addr(struct dw200_subdev *dev, int dmabuf_fd,
			int buffer_type)
{
	struct heap_mem *hmem;
	u64 dma_addr;

	hmem = common_dmabuf_lookup_heapobj_by_fd(dev->pheap_root, dmabuf_fd);
	if (!hmem) {
		pr_err("Could not find dmabuf-heap for dmabuf fd %d, Please import it first\n",
		       dmabuf_fd);
		return -1;
	}

	dma_addr = (u32)sg_dma_address(hmem->sgt->sgl);

	// Unmask below to flush cache when using cma or system
	// common_dmabuf_heap_end_cpu_access(hmem);

	return dma_addr;
}

int dma_heap_import_from_user(struct dw200_subdev *dev, int dmabuf_fd)
{
	struct heap_mem *hmem;

	if (dmabuf_fd <= 0) {
		pr_err("Import invalid dmabuf fd, please check it in userspace.\n");
		return -1;
	}

	hmem = common_dmabuf_heap_import_from_user(dev->pheap_root, dmabuf_fd);
	if (IS_ERR(hmem)) {
		pr_err("dmabuf-heap alloc from userspace fail for dmabuf fd %d\n",
		       dmabuf_fd);
		return PTR_ERR(hmem);
	}

	pr_debug("dmabuf fd 0x%x iova:0x%lx, size:%ld\n", dmabuf_fd,
		 (unsigned long)sg_dma_address(hmem->sgt->sgl),
		 common_dmabuf_heap_get_size(hmem));

	return 0;
}

int dma_heap_iova_release(struct dw200_subdev *dev, int dmabuf_fd)
{
	struct heap_mem *hmem;

	if (dmabuf_fd <= 0) {
		pr_err("Release invalid dmabuf fd, please check it in userspace.\n");
		return -1;
	}

	hmem = common_dmabuf_lookup_heapobj_by_fd(dev->pheap_root, dmabuf_fd);
	if (!hmem) {
		pr_err("Cannot find dmabuf-heap for dmabuf_fd %d\n", dmabuf_fd);
		return -1;
	}
	common_dmabuf_heap_release(hmem);

	return 0;
}

#else
#define get_dma_addr(dev, addr, buffer_type) (addr)
#endif

int dwe_reset(struct dw200_subdev *dev)
{
	// pr_info("enter %s\n", __func__);
#ifdef DWE_REG_RESET
	__raw_writel(0, dev->dwe_reset);
	__raw_writel(1, dev->dwe_reset);
	vivdw200_destroy_circle_queue(&dev->dwe_circle_list);
	vivdw200_create_circle_queue(&dev->dwe_circle_list, QUEUE_NODE_COUNT);
	if (dev->fe.enable) {
		dw200_fe_reset(dev);
	}
#endif
	dwe_write_reg(dev, DEWARP_CTRL, 0x0c);
	return 0;
}

int dwe_s_params(struct dw200_subdev *dev)
{
	struct dwe_hw_info *info = &dev->dwe_info;
	u32 reg = 0;
	u32 vUp = (info->split_v1 & ~0x0F) | 0x0C;
	u32 vDown = (info->split_v2 & ~0x0F) | 0x0C;
	u32 hLine = (info->split_h & ~0x0F) | 0x0C;
	u32 dst_img_width_bn;
	struct buffer_info *input_buf = &dev->buf_info[DWE_INPUT_BUFFER_0];
	struct buffer_info *output_buf = &dev->buf_info[DWE_OUTPUT_BUFFER_0];
	u32 reg_y_rbuff_size = output_buf->stride[0] * info->dst_h;
	u32 dst_size_uv = output_buf->stride[1] * info->dst_h;

#ifdef _ES_DW200_DEBUG_PRINT
	printDweInfo(info);
#endif
	if (info->out_yuvbit == 1) {
		if (info->out_format == MEDIA_PIX_FMT_YUV422I)
			dst_img_width_bn = info->dst_w * 4;
		else
			dst_img_width_bn = info->dst_w * 2;
	} else {
		if (info->out_format == MEDIA_PIX_FMT_YUV422I)
			dst_img_width_bn = info->dst_w * 2;
		else
			dst_img_width_bn = info->dst_w;
	}

	// pr_info("enter %s\n", __func__);

	dwe_write_reg(dev, MAP_LUT_SIZE,
		      ((info->map_w & 0x7ff) | ((info->map_h & 0x7ff) << 16)));
	dwe_write_reg(dev, SRC_IMG_SIZE,
		      ((info->src_w & 0x1fff) |
		       ((info->src_h & 0x1fff) << 16)));
	dwe_write_reg(dev, SRC_IMG_STRIDE, input_buf->stride[0]);

	dwe_write_reg(dev, DST_IMG_SIZE,
		      ((info->dst_w & 0x1FFF) |
		       ((info->dst_h & 0x1FFF) << 16)));

	dwe_write_reg(dev, DST_IMG_WIDTH_BN,
		      ((dst_img_width_bn & 0x7FFF) |
		       ((dst_img_width_bn & 0x7FFF) << 16)));
	dwe_write_reg(dev, DST_IMG_STRIDE, output_buf->stride[0]);
	dwe_write_reg(dev, DST_IMG_Y_SIZE1, reg_y_rbuff_size >> 4);
	dwe_write_reg(dev, DST_IMG_UV_SIZE1, dst_size_uv >> 4);
	dwe_write_reg(dev, VERTICAL_SPLIT_LINE,
		      (vUp & 0x1fff) | ((vDown & 0x1fff) << 16));
	dwe_write_reg(dev, HORIZON_SPLIT_LINE, (hLine & 0x1fff));

	reg = 0x4c808009;
	reg |= ((info->split_line & 0x1) << 11);
	reg |= ((info->in_format & 0x3) << 4);
	reg |= ((info->out_format & 0x3) << 6);
	reg |= ((info->in_yuvbit & 0x1) << 12);
	reg |= ((info->out_yuvbit & 0x1) << 13);
	reg |= ((info->src_auto_shadow & 0x1) << 8) |
	       ((info->dst_auto_shadow & 0x1) << 10);
	reg |= ((info->hand_shake & 0x1) << 9);
	dwe_write_reg(dev, DEWARP_CTRL, reg);

	dwe_write_reg(dev, BOUNDRY_PIXEL,
		      (((info->boundary_y & 0xff) << 16) |
		       ((info->boundary_u & 0xff) << 8) |
		       (info->boundary_v & 0xff)));
	dwe_write_reg(dev, SCALE_FACTOR, info->scale_factor);
	dwe_write_reg(dev, ROI_START,
		      ((info->roi_x & 0x1fff) |
		       ((info->roi_y & 0x1fff) << 16)));
	return 0;
}

int dwe_enable_bus(struct dw200_subdev *dev, bool enable)
{
	u32 reg = dwe_read_reg(dev, BUS_CTRL);

	// pr_info("enter %s\n", __func__);

	if (enable) {
		dwe_write_reg(dev, BUS_CTRL, reg | DEWRAP_BUS_CTRL_ENABLE_MASK);
	} else {
		dwe_write_reg(dev, BUS_CTRL,
			      reg & ~DEWRAP_BUS_CTRL_ENABLE_MASK);
	}

	return 0;
}

int dwe_disable_irq(struct dw200_subdev *dev)
{
	// pr_info("enter %s\n", __func__);
	dwe_write_reg(dev, INTERRUPT_STATUS, INT_RESET_MASK | INT_CLR_MASK);
	return 0;
}

int dwe_clear_irq(struct dw200_subdev *dev, u32 mis)
{
	u32 interrupt_status;

	// pr_info("enter %s\n", __func__);
	interrupt_status = dwe_read_reg(dev, INTERRUPT_STATUS);

	dwe_write_reg(dev, INTERRUPT_STATUS,
		      mis | interrupt_status); // INT_CLR_MASK);
	// dwe_write_reg(dev, DEWARP_CTRL, reg_dewarp_ctrl | 2);
	// dwe_write_reg(dev, DEWARP_CTRL, reg_dewarp_ctrl);
	return 0;
}
int dwe_clear_irq_1(struct dw200_subdev *dev)
{
	u32 reg_dewarp_ctrl;

	// pr_info("enter %s\n", __func__);
	reg_dewarp_ctrl = dwe_read_reg(dev, DEWARP_CTRL);
	dwe_write_reg(dev, INTERRUPT_STATUS, INT_CLR_MASK);
	dwe_write_reg(dev, DEWARP_CTRL, reg_dewarp_ctrl | 2);
	dwe_write_reg(dev, DEWARP_CTRL, reg_dewarp_ctrl);
	return 0;
}
int dwe_read_irq(struct dw200_subdev *dev, u32 *ret)
{
	u32 irq = 0;

	irq = dwe_read_reg(dev, INTERRUPT_STATUS);
	*ret = irq;

	return 0;
}

int dwe_dequeue_irq(struct dw200_subdev *dev, u32 *ret)
{
	vivdw200_mis_t data;

	if (dev == NULL || ret == NULL) {
		// pr_info("wrong input parameter.\n");
		return -1;
	}
	if (0 == vivdw200_read_circle_queue(&data, &dev->dwe_circle_list)) {
		*ret = data.val;
	} else {
		pr_err("failed to dequeue dwe mis.\n");
		*ret = 0;
	}
	return 0;
}

int dwe_start_dma_read(struct dw200_subdev *dev, u64 addr)
{
	struct dwe_hw_info *info = &dev->dwe_info;
#ifdef DWE_REG_RESET
	u32 regStart = 1 << 4;
	u32 reg;
#endif
	struct buffer_info *input_buf = &dev->buf_info[DWE_INPUT_BUFFER_0];
	u32 reg_dst_y_base = (u32)get_dma_addr(dev, addr, DWE_INPUT_BUFFER_0);
	u32 reg_y_rbuff_size = input_buf->stride[0] * info->src_h;
	u32 reg_dst_uv_base = reg_dst_y_base + reg_y_rbuff_size;
	u32 offset[3] = { 0, 0, 0 };
	u32 i = 0;

	for (i = 0; i < 3; i++) {
		offset[i] = input_buf->offset[i];
	}
	reg_dst_y_base = reg_dst_y_base + offset[0];
	if (offset[1] > 0) {
		reg_dst_uv_base = reg_dst_y_base + offset[1];
	}

	// pr_info("enter %s\n", __func__);

	dwe_write_reg(dev, SRC_IMG_Y_BASE, (reg_dst_y_base) >> 4);
	dwe_write_reg(dev, SRC_IMG_UV_BASE, (reg_dst_uv_base) >> 4);

#ifdef DWE_REG_RESET
	reg = __raw_readl(dev->dwe_reset);
	__raw_writel(reg | regStart, dev->dwe_reset);
	__raw_writel(reg & ~regStart, dev->dwe_reset);
#endif
	return 0;
}

int dwe_start(struct dw200_subdev *dev)
{
	u32 reg_dewarp_ctrl;
	// pr_info("enter %s\n", __func__);
	reg_dewarp_ctrl = dwe_read_reg(dev, DEWARP_CTRL);
	dwe_write_reg(dev, DEWARP_CTRL, reg_dewarp_ctrl | DWE_START_MASK);
	dwe_write_reg(dev, INTERRUPT_STATUS, 0x00000100);

	return 0;
}

int dwe_set_buffer(struct dw200_subdev *dev, u64 addr)
{
	struct dwe_hw_info *info = &dev->dwe_info;
	u32 reg_dst_y_base = (u32)get_dma_addr(dev, addr, DWE_OUTPUT_BUFFER_0);
	struct buffer_info *output_buf = &dev->buf_info[DWE_OUTPUT_BUFFER_0];
	u32 reg_y_rbuff_size = output_buf->stride[0] * info->dst_h;
	u32 reg_dst_uv_base = reg_dst_y_base + reg_y_rbuff_size;
	u32 offset[3] = { 0, 0, 0 };
	u32 i = 0;

	for (i = 0; i < 3; i++) {
		offset[i] = output_buf->offset[i];
	}
	reg_dst_y_base = reg_dst_y_base + offset[0];
	if (offset[1] > 0) {
		reg_dst_uv_base = reg_dst_y_base + offset[1];
	}

	// pr_info("enter %s\n", __func__);
	dwe_write_reg(dev, DST_IMG_Y_BASE, (reg_dst_y_base) >> 4);
	dwe_write_reg(dev, DST_IMG_UV_BASE, (reg_dst_uv_base) >> 4);

	return 0;
}

int dwe_set_lut(struct dw200_subdev *dev, u64 addr)
{
	dwe_write_reg(dev, MAP_LUT_ADDR,
		      ((u32)get_dma_addr(dev, addr, DWE_INPUT_LUT_BUFFER)) >>
			      4);
	return 0;
}

int dwe_ioc_qcap(struct dw200_subdev *dev, void *args)
{
	struct v4l2_capability *cap = (struct v4l2_capability *)args;

	strcpy((char *)cap->driver, "viv_dw200");
	return 0;
}

int vse_reset(struct dw200_subdev *dev)
{
	// pr_info("enter %s\n", __func__);

#ifdef VSE_REG_RESET
	// pr_info("hard resert enter %s\n", __func__);
	__raw_writel(0, dev->vse_reset);
	__raw_writel(1, dev->vse_reset);
	vivdw200_destroy_circle_queue(&dev->vse_circle_list);
	vivdw200_create_circle_queue(&dev->vse_circle_list, QUEUE_NODE_COUNT);
	// TODO: dwe-fe engine reset
	if (dev->fe.enable) {
		dw200_fe_reset(dev);
	}
#endif
	vse_write_reg(dev, VSE_REG_CTRL, 0x100);
	return 0;
}

void vse_triger_dma_read(struct dw200_subdev *dev)
{
	u32 reg = vse_read_reg(dev, VSE_REG_CTRL);
	REG_SET_BIT(reg, VSE_CONTROL_DMA_FRAME_START_BIT, 1);
	if (dev->fe.enable) {
		vse_write_reg(dev, VSE_REG_DMA_CTRL, 5);
		vse_write_reg(dev, VSE_REG_CTRL, reg);
	} else {
		vse_write_reg(dev, VSE_REG_CTRL, reg);
		vse_write_reg(dev, VSE_REG_DMA_CTRL, 5);
	}
}

void setMIBaseAddress(struct dw200_subdev *dev, u32 width, u32 height,
		      u32 format, u32 yuvbit, u64 addr, int channel)
{
	struct buffer_info *output_buf =
		&dev->buf_info[VSE_OUTPUT_BUFFER_0 + channel];
	u32 stride = output_buf->stride[0];
	u32 crSize = 0;
	u32 yBaseAddr = get_dma_addr(dev, addr, VSE_OUTPUT_BUFFER_0 + channel);
	u32 regAddress = VSE_MI_BASE(channel);
	u32 crBaseAddr = 0;
	u32 ysize = stride * height;
	u32 cbSize = ysize;
	u32 cbBaseAddr = yBaseAddr + ysize;
	u32 offset[3] = { 0, 0, 0 };
	u32 i = 0;
	u32 tmpYBaseAddr = yBaseAddr;

	for (i = 0; i < 3; i++) {
		offset[i] = output_buf->offset[i];
	}
	yBaseAddr = yBaseAddr + offset[0];
	if (offset[1] > 0) {
		cbBaseAddr = yBaseAddr + offset[1];
	}

	if (ysize == 0)
		return;

	switch (format) {
	case MEDIA_PIX_FMT_YUV420SP:
		cbSize /= 2;
		crSize = 0;
		break;
	case MEDIA_PIX_FMT_YUV422SP:
		break;
	case MEDIA_PIX_FMT_YUV422I:
		// ysize = ysize * 2;
		break;
	case MEDIA_PIX_FMT_YUV444:
	case MEDIA_PIX_FMT_RGB888P:
		crSize = ysize;
		crBaseAddr = cbBaseAddr + cbSize;
		if (offset[2] > 0) {
			crBaseAddr = yBaseAddr + offset[2];
		}
		break;
	case MEDIA_PIX_FMT_BGR888:
	case MEDIA_PIX_FMT_RGB888: /* only supoort RGB interleave format.  RGB RGB RGB .. */
		// ysize *= 3;
		cbSize = 0;
		cbBaseAddr = 0;
		break;
	case MEDIA_PIX_FMT_BGR888P:
		crSize = ysize;
		yBaseAddr = cbBaseAddr + cbSize;
		crBaseAddr = tmpYBaseAddr;
		if (offset[2] > 0) {
			yBaseAddr = tmpYBaseAddr + offset[2];
		}
		break;
	}

	vse_write_reg(dev, regAddress + VSE_REG_MI_Y_BASE_ADDR_INIT, yBaseAddr);
	vse_write_reg(dev, regAddress + VSE_REG_MI_Y_SIZE_INIT, ysize);
	vse_write_reg(dev, regAddress + VSE_REG_MI_CB_BASE_ADDR_INIT,
		      cbBaseAddr);
	vse_write_reg(dev, regAddress + VSE_REG_MI_CB_SIZE_INIT, cbSize);
	vse_write_reg(dev, regAddress + VSE_REG_MI_CR_BASE_ADDR_INIT,
		      crBaseAddr);
	vse_write_reg(dev, regAddress + VSE_REG_MI_CR_SIZE_INIT, crSize);
}

int vse_start_dma_read(struct dw200_subdev *dev, u64 addr)
{
	u32 writeFormat = 0;
	u32 writeString = 0;
	u32 reg = 0;
	u32 address = VSE_REG_DMA_FORMAT;
	u32 width = dev->vse_info.src_w;
	u32 height = dev->vse_info.src_h;
	u32 format = dev->vse_info.in_format;
	u32 yuvbit = dev->vse_info.in_yuvbit;
	u32 ysize, cbSize;
	u32 yBaseAddr, cbBaseAddr, crBaseAddr;
	u32 stride;
	u32 img_width_bn;
	u32 offset[3] = { 0, 0, 0 };
	u32 i = 0;
	struct buffer_info *input_buf = &dev->buf_info[VSE_INPUT_BUFFER_0];

	stride = input_buf->stride[0];
	if (yuvbit == 1) {
		if (format == MEDIA_PIX_FMT_YUV422I) {
			img_width_bn = width * 4;
		} else {
			img_width_bn = width * 2;
		}
	} else {
		if (format == MEDIA_PIX_FMT_YUV422I) {
			img_width_bn = width * 2;
		} else
			img_width_bn = width;
	}

	switch (format) {
	case MEDIA_PIX_FMT_YUV422SP:
		writeFormat = 1;
		writeString = 0;
		break;
	case MEDIA_PIX_FMT_YUV422I:
		writeFormat = 1;
		writeString = 1;
		break;
	case MEDIA_PIX_FMT_YUV420SP:
		writeFormat = 0;
		writeString = 0;
		break;
	}

	reg = vse_read_reg(dev, address);

	// pr_info("img_width_bn 0x%08x\n", img_width_bn );
	REG_SET_MASK(reg, VSE_RD_IMG_HSIZE_BN, (img_width_bn & 0x3FFF));
	REG_SET_MASK(reg, VSE_MI_FORMAT_WORD_ALIGNED, yuvbit);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_FMT_ALIGNED, yuvbit);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_YUV_STR, writeString);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_YUV_FMT, writeFormat);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_YUV_10BIT, yuvbit);
	// pr_info("VSE_REG_DMA_FORMAT reg 0x%08x\n", reg);
	vse_write_reg(dev, address, reg);
	vse_write_reg(dev, VSE_REG_DMA_Y_PIC_WIDTH, width);
	vse_write_reg(dev, VSE_REG_DMA_Y_PIC_HEIGHT, height);
	vse_write_reg(dev, VSE_REG_DMA_Y_PIC_STRIDE, stride);
	ysize = stride * height;
	cbSize = ysize;
	for (i = 0; i < 3; i++) {
		offset[i] = input_buf->offset[i];
	}
	yBaseAddr =
		(u32)get_dma_addr(dev, addr, VSE_INPUT_BUFFER_0) + offset[0];
	if (offset[1] > 0) {
		cbBaseAddr = yBaseAddr + offset[1];
	} else {
		cbBaseAddr = yBaseAddr + ysize;
	}
	crBaseAddr = 0;

	// if (format == MEDIA_PIX_FMT_YUV420SP) {
	//     cbSize /= 2;
	// } else if (format == MEDIA_PIX_FMT_YUV422I) {
	//     cbSize /= 2;
	//     crBaseAddr = cbBaseAddr + cbSize;
	//     if (offset[2] > 0) {
	//         crBaseAddr = cbBaseAddr + offset[2];
	//     }
	// }
	vse_write_reg(dev, VSE_REG_Y_PIC_START_ADDR, yBaseAddr);
	vse_write_reg(dev, VSE_REG_CB_PIC_START_ADDR, cbBaseAddr);
	vse_write_reg(dev, VSE_REG_CR_PIC_START_ADDR, crBaseAddr);
	vse_triger_dma_read(dev);
	return 0;
}

void setFormatConvPack(struct dw200_subdev *dev, u32 enable, int channel)
{
	u32 reg;
	u32 address = VSE_RSZBASE(channel) + VSE_REG_FORMAT_CONV_CTRL;

	reg = vse_read_reg(dev, address);
	REG_SET_MASK(reg, VSE_FORMAT_CONV_ENABLE_PACK, enable);
	vse_write_reg(dev, address, reg);
}

void setFormatConvFull(struct dw200_subdev *dev, u32 y_full, u32 cbcr_full,
		       u32 noco422, int channel)
{
	u32 reg;
	u32 address = VSE_RSZBASE(channel) + VSE_REG_FORMAT_CONV_CTRL;

	reg = vse_read_reg(dev, address);
	REG_SET_MASK(reg, VSE_FORMAT_CONV_Y_FULL, y_full);
	REG_SET_MASK(reg, VSE_FORMAT_CONV_CBCR_FULL, cbcr_full);
	REG_SET_MASK(reg, VSE_FORMAT_CONV_CONFIG_422NOCO, 0);
	vse_write_reg(dev, address, reg);
}

static u32 format_conv_map[] = { 2, 2, 1, 3, 6, 6 };

void setFormatConvFormat(struct dw200_subdev *dev, u32 inputFormat,
			 u32 outputFormat, u32 yuvbit, int channel)
{
	u32 reg;
	u32 address = VSE_RSZBASE(channel) + VSE_REG_FORMAT_CONV_CTRL;
	u32 out_fmt = 0;

	if (inputFormat == MEDIA_PIX_FMT_YUV420SP &&
	    outputFormat == MEDIA_PIX_FMT_YUV420SP) {
	} else if (inputFormat == MEDIA_PIX_FMT_YUV422SP &&
		   outputFormat == MEDIA_PIX_FMT_YUV420SP) {
		inputFormat = MEDIA_PIX_FMT_YUV420SP;
	} else if (inputFormat == MEDIA_PIX_FMT_YUV420SP &&
		   outputFormat != MEDIA_PIX_FMT_YUV420SP) {
		inputFormat = MEDIA_PIX_FMT_YUV422SP;
	}
	if (outputFormat == MEDIA_PIX_FMT_BGR888 ||
	    outputFormat == MEDIA_PIX_FMT_BGR888P) {
		out_fmt = 6;
	} else {
		out_fmt = format_conv_map[outputFormat];
	}

	reg = vse_read_reg(dev, address);
	REG_SET_MASK(reg, VSE_FORMAT_CONV_OUTPUT_FORMAT, out_fmt);
	REG_SET_MASK(reg, VSE_FORMAT_CONV_INPUT_FORMAT,
		     format_conv_map[inputFormat]);
	REG_SET_MASK(reg, VSE_FORMAT_CONV_EXPAND_8TO10_ENABLE, yuvbit);
	vse_write_reg(dev, address, reg);
}

void setInputSize(struct dw200_subdev *dev, u32 width, u32 height)
{
	u32 reg = ((height & 0x1FFF) << 16) | (width & 0x1FFF);

	vse_write_reg(dev, VSE_REG_IN_SIZE, reg);
}

void setCropSize(struct dw200_subdev *dev, u32 left, u32 right, u32 top,
		 u32 bottom, int channel)
{
	u32 hreg = ((right & 0x1FFF) << 16) | (left & 0x1FFF);
	u32 vreg = ((bottom & 0x1FFF) << 16) | (top & 0x1FFF);

	vse_write_reg(dev, VSE_RSZBASE(channel) + VSE_RSZ_CROP_XDIR, hreg);
	vse_write_reg(dev, VSE_RSZBASE(channel) + VSE_RSZ_CROP_YDIR, vreg);
}

void updateResizeControl(struct dw200_subdev *dev, u32 reg, int channel)
{
	u32 address = VSE_RSZBASE(channel);

	vse_write_reg(dev, address, reg);
}

void resizeControlAutoUpdate(struct dw200_subdev *dev, u32 autoUpdate,
			     int channel)
{
	u32 reg;
	u32 address = VSE_RSZBASE(channel);

	reg = vse_read_reg(dev, address);
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_AUTO_UPDATE_BIT, autoUpdate);
	vse_write_reg(dev, address, reg);
}

void resizeControlConfigUpdate(struct dw200_subdev *dev, u32 configUpdate,
			       int channel)
{
	u32 reg;
	u32 address = VSE_RSZBASE(channel);

	reg = vse_read_reg(dev, address);
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_CONFIG_UPDATE_BIT, configUpdate);
	vse_write_reg(dev, address, reg);
}

void resizeControlEnableCrop(struct dw200_subdev *dev, u32 enable, int channel)
{
	u32 reg;
	u32 address = VSE_RSZBASE(channel);

	reg = vse_read_reg(dev, address);
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_CROP_ENABLE_BIT, enable);
	vse_write_reg(dev, address, reg);
}

void updateVseControl(struct dw200_subdev *dev, u32 inputChannel,
		      u32 inputFormat, u32 enbalePath0, u32 enbalePath1,
		      u32 enbalePath2, u32 autoUpdate, u32 configUpdate)
{
	u32 reg = 0;
	int i;

	REG_SET_BIT(reg, VSE_CONTROL_AUTO_UPDATE_BIT, autoUpdate);
	REG_SET_BIT(reg, VSE_CONTROL_CONFIG_UPDATE_BIT, configUpdate);
	REG_SET_BIT(reg, VSE_CONTROL_PATH0_ENABLE_BIT, enbalePath0);
	REG_SET_BIT(reg, VSE_CONTROL_PATH1_ENABLE_BIT, enbalePath1);
	REG_SET_BIT(reg, VSE_CONTROL_PATH2_ENABLE_BIT, enbalePath2);

	REG_SET_MASK(reg, VSE_CONTROL_INPUT_SELECT, inputChannel);
	REG_SET_MASK(reg, VSE_CONTROL_INPUT_FORMAT, inputFormat);
	vse_write_reg(dev, VSE_REG_CTRL, reg);

	for (i = 0; i < 3; i++) {
		reg = vse_read_reg(dev, VSE_RSZBASE(i));
		REG_SET_BIT(reg, VSE_RESIZE_CONTROL_CONFIG_UPDATE_BIT, 1);
		vse_write_reg(dev, VSE_RSZBASE(i), reg);
	}

	/* BUS ID and BUS CONFIG, hardcode here, user need adjust it on their hardware. */
	vse_write_reg(dev, 0xa24, 0x01000108);
	vse_write_reg(dev, 0xa20, 0x001000fa);
	vse_write_reg(dev, 0xa28, 0x924c5214);
	vse_write_reg(dev, 0xa30, 0x100000fa);
	vse_write_reg(dev, 0xa34, 0x01000108);
	vse_write_reg(dev, 0xa38, 0x924c5214);
}

void enableMIControl(struct dw200_subdev *dev, u32 enable, int channel)
{
	u32 reg = vse_read_reg(dev, VSE_REG_MI_CTRL);

	REG_SET_BIT(reg, channel, enable);
	REG_SET_BIT(reg, VSE_MI_CONTROL_RDMA_ENABLE_BIT, 1);
	vse_write_reg(dev, VSE_REG_MI_CTRL, reg);
}

void setMIBufferInfo(struct dw200_subdev *dev, u32 width, u32 height,
		     u32 format, u32 is10Bit, u32 aligned, int channel)
{
	u32 writeFormat = 0;
	u32 writeString = 0;
	u32 reg = 0;
	u32 address;
	u32 nvy = 0;
	u32 n21 = 0;
	struct buffer_info *output_buf =
		&dev->buf_info[VSE_OUTPUT_BUFFER_0 + channel];

	u32 stride = output_buf->stride[0];
	u32 size = stride * height;
	// switch (format) {
	//     case MEDIA_PIX_FMT_YUV422I:
	//         size = size * 2;
	//         break;
	// }
	if (format == MEDIA_PIX_FMT_RGB888 || format == MEDIA_PIX_FMT_BGR888) {
		stride /= 3;
	} else if (format == MEDIA_PIX_FMT_YUV422I) {
		stride /= 2;
	}

	vse_write_reg(dev, VSE_MI_BASE(channel) + VSE_REG_MI_Y_LENGTH, stride);
	vse_write_reg(dev, VSE_MI_BASE(channel) + VSE_REG_MI_Y_PIC_WIDTH,
		      width);
	vse_write_reg(dev, VSE_MI_BASE(channel) + VSE_REG_MI_Y_PIC_HEIGHT,
		      height);
	vse_write_reg(dev, VSE_MI_BASE(channel) + VSE_REG_MI_Y_PIC_SIZE, size);

	switch (format) {
	case MEDIA_PIX_FMT_YUV422SP:
		writeFormat = 1;
		writeString = 0;
		break;
	case MEDIA_PIX_FMT_YUV422I:
		writeFormat = 1;
		writeString = 1;
		break;
	case MEDIA_PIX_FMT_YUV420SP:
		writeFormat = 0;
		writeString = 0;
		break;
	case MEDIA_PIX_FMT_YUV444:
		writeFormat = 2;
		writeString = 2;
		break;
	case MEDIA_PIX_FMT_RGB888:
		writeFormat = 2;
		writeString = 1;
		break;
	case MEDIA_PIX_FMT_BGR888:
		writeFormat = 2;
		writeString = 1;
		nvy = 2;
		n21 = 1;
		break;
	case MEDIA_PIX_FMT_RGB888P:
	case MEDIA_PIX_FMT_BGR888P:
		writeFormat = 2;
		writeString = 2;
		break;
	}

	address = VSE_MI_BASE(channel) + VSE_REG_MI_FMT;
	reg = vse_read_reg(dev, address);
	// DEBUG_PRINT("%s, before mask reg = 0x%x\n", __func__, reg);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WORD_ALIGNED, is10Bit);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_YUV_NVY, nvy);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_YUV_N21, n21);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_FMT_ALIGNED, is10Bit);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_YUV_STR, writeString);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_YUV_FMT, writeFormat);
	REG_SET_MASK(reg, VSE_MI_FORMAT_WR_YUV_10BIT, is10Bit);
	// DEBUG_PRINT("%s, after mask reg = 0x%x\n",  __func__,reg);
	vse_write_reg(dev, address, reg);
}

void setMIControlConfig(struct dw200_subdev *dev, u32 enableOffset,
			u32 enableUpdateBaseAddress, u32 configUpdate, u32 skip,
			u32 autoUpdate, u32 enablePingpong, int channel)
{
	u32 reg = 0;

	REG_SET_BIT(reg, VSE_MI_PATH_INIT_OFFSET_EN_BIT, enableOffset);
	REG_SET_BIT(reg, VSE_MI_PATH_INIT_BASE_EN_BIT, enableUpdateBaseAddress);
	REG_SET_BIT(reg, VSE_MI_PATH_CONFIG_UPDATE_BIT, configUpdate);
	REG_SET_BIT(reg, VSE_MI_PATH_ENABLE_SKIP_BIT, skip);
	REG_SET_BIT(reg, VSE_MI_PATH_AUTO_UPDATE_BIT, autoUpdate);
	REG_SET_BIT(reg, VSE_MI_PATH_ENABLE_PINGPONG_BIT, enablePingpong);
	vse_write_reg(dev, VSE_MI_BASE(channel), reg);
}

u32 vse_get_scale_factor(int src, int dst)
{
	if (dst >= src) {
		return ((65536 * (src - 1)) / (dst - 1));
	} else if (dst < src) {
		return ((65536 * (dst - 1)) / (src - 1)) + 1;
	}
	return 0;
}

void setScaleFactor(struct dw200_subdev *dev, u32 src_w, u32 src_h, u32 dst_w,
		    u32 dst_h, u32 inputFormat, u32 outputFormat, int channel)
{
	bool hyup, vyup, hcup, vcup;
	u32 scale_factor = vse_get_scale_factor(src_w, dst_w);
	u32 address = VSE_RSZBASE(channel);
	u32 reg = vse_read_reg(dev, address);

	vse_write_reg(dev, VSE_RSZBASE(channel) + VSE_RSZ_SCALE_HY,
		      scale_factor);
	scale_factor = vse_get_scale_factor(src_h, dst_h);
	vse_write_reg(dev, VSE_RSZBASE(channel) + VSE_RSZ_SCALE_VY,
		      scale_factor);
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_SCALE_VY_ENABLE_BIT,
		    ((src_h != dst_h) & 0x01));
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_SCALE_HY_ENABLE_BIT,
		    ((src_w != dst_w) & 0x01));
	hyup = src_w < dst_w;
	vyup = src_h < dst_h;

	/* Format conv module doesn't support convert other formats to YUV420SP.
       doesn't support convert 420SP to other formats too.
       so scale down/up cbcr here.
     */
	src_w /= 2;
	dst_w /= 2;
	if (inputFormat == MEDIA_PIX_FMT_YUV420SP &&
	    outputFormat == MEDIA_PIX_FMT_YUV420SP) {
		src_h /= 2;
		dst_h /= 2;
	} else if (((inputFormat == MEDIA_PIX_FMT_YUV422SP) ||
		    (inputFormat == MEDIA_PIX_FMT_YUV422I)) &&
		   outputFormat == MEDIA_PIX_FMT_YUV420SP) {
		/* scale 422 to 420 */
		dst_h /= 2;
	} else if (inputFormat == MEDIA_PIX_FMT_YUV420SP &&
		   outputFormat != MEDIA_PIX_FMT_YUV420SP) {
		/* scale 420 to 422
           cbcr width*2, use input buffer as 422SP */
		src_h /= 2;
	}

	hcup = src_w < dst_w;
	vcup = src_h < dst_h;
	scale_factor = vse_get_scale_factor(src_w, dst_w);
	vse_write_reg(dev, VSE_RSZBASE(channel) + VSE_RSZ_SCALE_HCB,
		      scale_factor);
	vse_write_reg(dev, VSE_RSZBASE(channel) + VSE_RSZ_SCALE_HCR,
		      scale_factor);
	scale_factor = vse_get_scale_factor(src_h, dst_h);
	vse_write_reg(dev, VSE_RSZBASE(channel) + VSE_RSZ_SCALE_VC,
		      scale_factor);

	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_SCALE_VCUP_BIT, vcup);
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_SCALE_VYUP_BIT, vyup);
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_SCALE_HCUP_BIT, hcup);
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_SCALE_HYUP_BIT, hyup);
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_SCALE_VC_ENABLE_BIT,
		    (src_h != dst_h));
	REG_SET_BIT(reg, VSE_RESIZE_CONTROL_SCALE_HC_ENABLE_BIT,
		    (src_w != dst_w));
	vse_write_reg(dev, address, reg);
}

int vse_read_irq(struct dw200_subdev *dev, u32 *ret)
{
	// pr_info("enter %s\n", __func__);
	*ret = vse_read_reg(dev, VSE_REG_MI_MSI);
	pr_debug("enter %s *ret:0x%08x\n", __func__, *ret);
	return 0;
}

int vse_dequeue_irq(struct dw200_subdev *dev, u32 *ret)
{
	vivdw200_mis_t data;

	if (dev == NULL || ret == NULL) {
		// pr_info("wrong input parameter.\n");
		return -1;
	}
	if (0 == vivdw200_read_circle_queue(&data, &dev->vse_circle_list)) {
		*ret = data.val;
	} else {
		pr_err("failed to dequeue vse mis.\n");
		*ret = 0;
	}
	return 0;
}

int vse_clear_irq(struct dw200_subdev *dev, u32 mis)
{
	// pr_info("enter %s\n", __func__);
	vse_write_reg(dev, VSE_REG_MI_ICR, mis);
	return 0;
}

int vse_mask_irq(struct dw200_subdev *dev, u32 mask)
{
	// pr_info("enter %s 0x%08x\n", __func__, mask);
	vse_write_reg(dev, VSE_REG_MI_IMSC, mask);
	return 0;
}

int vse_ioc_qcap(struct dw200_subdev *dev, void *args)
{
	struct v4l2_capability *cap = (struct v4l2_capability *)args;

	strcpy((char *)cap->driver, "viv_vse");
	return 0;
}

int vse_update_buffers(struct dw200_subdev *dev, u64 *addr)
{
	struct vse_params *param = &dev->vse_info;
	struct vse_mi_settings *mi = param->mi_settings;
	int i = 0;

	// pr_info("enter %s\n", __func__);
	for (; i < 3; i++) {
		if (addr[i] != 0) {
			setMIBaseAddress(dev, mi[i].width, mi[i].height,
					 mi[i].out_format, mi[i].yuvbit,
					 addr[i], i);
		}
	}
	return 0;
}

int vse_update_mi_info(struct dw200_subdev *dev)
{
	struct vse_params *param = &dev->vse_info;
	struct vse_mi_settings *mi = param->mi_settings;
	int i = 0;

	// pr_info("enter %s\n", __func__);

	for (; i < 3; i++) {
		if (!mi[i].enable)
			continue;
		setMIBufferInfo(dev, mi[i].width, mi[i].height,
				mi[i].out_format, mi[i].yuvbit, true, i);
		enableMIControl(dev, mi[i].enable, i);
		setMIControlConfig(dev, true, true, true, false, true, false,
				   i);
	}
	return 0;
}

int vse_s_params(struct dw200_subdev *dev)
{
	struct vse_params *param = &dev->vse_info;
	int in_format = 0;
	int i = 0;
	int input_select = 0;
	int crop_w, crop_h, scale_w, scale_h;
	struct vse_crop_size *cropSize;
	struct vse_size *outputSize;

#ifdef _ES_DW200_DEBUG_PRINT
	printVseInfo(param);
#endif
	// pr_info("enter %s\n", __func__);
	setInputSize(dev, param->src_w, param->src_h);

	for (; i < 3; i++) {
		if (!param->resize_enable[i])
			continue;
		cropSize = &param->crop_size[i];
		outputSize = &param->out_size[i];
		crop_w = cropSize->right - cropSize->left + 1;
		crop_h = cropSize->bottom - cropSize->top + 1;
		scale_w = param->src_w;
		scale_h = param->src_h;
		if (crop_w > 1 && crop_h > 1) {
			resizeControlEnableCrop(dev, 1, i);
			setCropSize(dev, cropSize->left, cropSize->right,
				    cropSize->top, cropSize->bottom, i);
			scale_w = crop_w;
			scale_h = crop_h;
		} else {
			resizeControlEnableCrop(dev, 0, i);
		}

		if (param->mi_settings[i].out_format ==
			    MEDIA_PIX_FMT_YUV422SP ||
		    param->mi_settings[i].out_format ==
			    MEDIA_PIX_FMT_YUV420SP) {
			setFormatConvPack(dev, 1, i);
		} else {
			setFormatConvPack(dev, 0, i);
		}
		setFormatConvFull(dev, true, true, false, i);
		setFormatConvFormat(dev, param->in_format,
				    param->format_conv[i].out_format,
				    param->mi_settings[i].yuvbit, i);

		setScaleFactor(dev, scale_w, scale_h, outputSize->width,
			       outputSize->height, param->in_format,
			       param->format_conv[i].out_format, i);
		resizeControlAutoUpdate(dev, 1, i);
		resizeControlConfigUpdate(dev, 1, i);
	}

#ifndef HAL_CMODEL
	input_select = param->input_select;
#endif
	if (param->in_format == 1)
		in_format = 0;
	else
		in_format = param->in_format;
	updateVseControl(dev, input_select, in_format,
			 param->mi_settings[0].enable,
			 param->mi_settings[1].enable,
			 param->mi_settings[2].enable, true, true);
	return 0;
}

long dw200_priv_ioctl(struct dw200_subdev *dev, unsigned int cmd, void *args)
{
	int ret = -1;
	u64 addr;
	switch (cmd) {
	case DWEIOC_RESET:
		ret = dwe_reset(dev);
		break;
	case DWEIOC_S_PARAMS:
		viv_check_retval(copy_from_user(&dev->dwe_info, args,
						sizeof(dev->dwe_info)));
		ret = dwe_s_params(dev);
		break;
	case DWEIOC_ENABLE_BUS:
		ret = dwe_enable_bus(dev, 1);
		break;
	case DWEIOC_DISABLE_BUS:
		ret = dwe_enable_bus(dev, 0);
		break;
	case DWEIOC_DISABLE_IRQ:
		ret = dwe_disable_irq(dev);
		break;
	case DWEIOC_CLEAR_IRQ:
		ret = dwe_clear_irq_1(dev);
		break;
	case DWEIOC_READ_IRQ: {
		u32 irq = 0;
#ifdef HAL_CMODEL
		dwe_read_irq((struct dw200_subdev *)dev, &irq);
#else
		ret = dwe_dequeue_irq(dev, &irq);
#endif

		viv_check_retval(copy_to_user(args, &irq, sizeof(irq)));
		break;
	}
	case DWEIOC_START_DMA_READ:
		viv_check_retval(copy_from_user(&addr, args, sizeof(addr)));
		ret = dwe_start_dma_read(dev, addr);
		break;
	case DWEIOC_IMPORT_DMA_HEAP_BUF:
		viv_check_retval(copy_from_user(&addr, args, sizeof(addr)));
		ret = dma_heap_import_from_user(dev, addr);
		break;
	case DWEIOC_RELEASE_DMA_HEAP_BUF:
		viv_check_retval(copy_from_user(&addr, args, sizeof(addr)));
		ret = dma_heap_iova_release(dev, addr);
		break;
	case DWEIOC_SET_BUFFER:
		viv_check_retval(copy_from_user(&addr, args, sizeof(addr)));
		ret = dwe_set_buffer(dev, addr);
		break;
	case DWEIOC_SET_LUT:
		viv_check_retval(copy_from_user(&addr, args, sizeof(addr)));
		ret = dwe_set_lut(dev, addr);
		break;
	case DWEIOC_START:
		ret = dwe_start(dev);
		break;
#ifdef __KERNEL__
	case VIDIOC_QUERYCAP:
		ret = dwe_ioc_qcap(dev, args);
		break;
#endif
	case VSEIOC_RESET:
		ret = vse_reset(dev);
		break;
	case VSEIOC_S_PARAMS:
		viv_check_retval(copy_from_user(&dev->vse_info, args,
						sizeof(dev->vse_info)));
		ret = vse_s_params(dev);
		break;
	case VSEIOC_CLEAR_IRQ:
		// ret = vse_clear_irq(dev);
		vse_write_reg(dev, VSE_REG_MI_MSI, 0);
		break;
	case VSEIOC_READ_IRQ: {
		u32 irq = 0;
#ifdef HAL_CMODEL
		vse_read_irq((struct dw200_subdev *)dev, &irq);
#else
		ret = vse_dequeue_irq(dev, &irq);
#endif
		viv_check_retval(copy_to_user(args, &irq, sizeof(irq)));
		break;
	}
	case VSEIOC_START_DMA_READ: {
		u64 addr;
		viv_check_retval(copy_from_user(&addr, args, sizeof(addr)));
		ret = vse_start_dma_read(dev, addr);
		break;
	}
	case VSEIOC_U_MI_INFO:
		ret = vse_update_mi_info(dev);
		break;
	case VSEIOC_U_BUFFER: {
		u64 addrs[3];
		viv_check_retval(copy_from_user(addrs, args, sizeof(addrs)));
		vse_update_buffers(dev, addrs);
		break;
	}
	case VSEIOC_MASK_IRQ: {
		u32 mask;
		viv_check_retval(copy_from_user(&mask, args, sizeof(mask)));
		vse_mask_irq(dev, mask);
		break;
	}
	default:
		pr_err("unsupported dwe command %d", cmd);
		break;
	}

	return ret;
}
