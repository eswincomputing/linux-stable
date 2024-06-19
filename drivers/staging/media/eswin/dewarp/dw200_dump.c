#include <linux/io.h>
#include "dw200_fe.h"
#include "dw200_ioctl.h"
#include "vivdw200_irq_queue.h"
#include "dw200_dump.h"

static u32 dw200_reg_offset[] = {
	0x00000000, 0x00000004, 0x00000008, 0x0000000c, 0x00000400, 0x00000404,
	0x00000408, 0x0000040C, 0x00000410, 0x00000414, 0x00000418, 0x0000041C,
	0x00000420, 0x00000424, 0x00000428, 0x0000042C, 0x00000430, 0x00000434,
	0x00000438, 0x0000043C, 0x00000440, 0x00000444, 0x00000448, 0x0000044C,
	0x00000450, 0x00000454, 0x00000458, 0x0000045C, 0x00000460, 0x00000464,
	0x00000468, 0x0000046C, 0x00000500, 0x00000504, 0x00000508, 0x0000050C,
	0x00000510, 0x00000514, 0x00000518, 0x0000051C, 0x00000520, 0x00000524,
	0x00000528, 0x0000052C, 0x00000530, 0x00000534, 0x00000538, 0x0000053C,
	0x00000540, 0x00000544, 0x00000548, 0x0000054C, 0x00000550, 0x00000554,
	0x00000558, 0x0000055C, 0x00000560, 0x00000564, 0x00000568, 0x0000056C,
	0x00000600, 0x00000604, 0x00000608, 0x0000060C, 0x00000610, 0x00000614,
	0x00000618, 0x0000061C, 0x00000620, 0x00000624, 0x00000628, 0x0000062C,
	0x00000630, 0x00000634, 0x00000638, 0x0000063C, 0x00000640, 0x00000644,
	0x00000648, 0x0000064C, 0x00000650, 0x00000654, 0x00000658, 0x0000065C,
	0x00000660, 0x00000664, 0x00000668, 0x0000066C, 0x00000700, 0x00000704,
	0x00000708, 0x0000070C, 0x00000800, 0x00000810, 0x00000814, 0x00000824,
	0x00000828, 0x0000082C, 0x00000830, 0x00000834, 0x00000838, 0x0000083C,
	0x00000840, 0x00000844, 0x00000848, 0x0000084C, 0x00000850, 0x00000854,
	0x00000858, 0x0000085C, 0x00000860, 0x00000864, 0x00000868, 0x0000086C,
	0x00000870, 0x00000874, 0x00000878, 0x0000087C, 0x00000880, 0x00000884,
	0x00000888, 0x0000088C, 0x00000890, 0x00000898, 0x0000089C, 0x000008a0,
	0x000008b0, 0x000008b4, 0x000008c4, 0x000008c8, 0x000008cC, 0x000008d0,
	0x000008d4, 0x000008d8, 0x000008dC, 0x000008e0, 0x000008e4, 0x000008e8,
	0x000008eC, 0x000008f0, 0x000008f4, 0x000008f8, 0x000008fC, 0x00000900,
	0x00000904, 0x00000908, 0x0000090C, 0x00000910, 0x00000914, 0x00000918,
	0x0000091C, 0x00000920, 0x00000924, 0x00000928, 0x0000092C, 0x00000930,
	0x00000934, 0x00000938, 0x0000093C, 0x00000940, 0x00000950, 0x00000954,
	0x00000964, 0x00000968, 0x0000096C, 0x00000970, 0x00000974, 0x00000978,
	0x0000097C, 0x00000980, 0x00000984, 0x00000988, 0x0000098C, 0x00000990,
	0x00000994, 0x00000998, 0x0000099C, 0x000009a0, 0x000009a4, 0x000009a8,
	0x000009aC, 0x000009b0, 0x000009b4, 0x000009b8, 0x000009bC, 0x000009c0,
	0x000009c4, 0x000009c8, 0x000009cC, 0x000009d0, 0x000009d4, 0x000009d8,
	0x000009dC, 0x000009e0, 0x000009e8, 0x000009eC, 0x000009f0, 0x000009f4,
	0x000009f8, 0x000009fC, 0x00000a00, 0x00000a04, 0x00000a08, 0x00000a0C,
	0x00000a10, 0x00000a14, 0x00000a1C, 0x00000a20, 0x00000a24, 0x00000a28,
	0x00000a2C, 0x00000a30, 0x00000a34, 0x00000a38, 0x00000a3C, 0x00000a40,
	0x00000a44, 0x00000a50, 0x00000a54, 0x00000a60, 0x00000a64, 0x00000a68,
	0x00000c00, 0x00000c04, 0x00000c08, 0x00000c0c, 0x00000c10, 0x00000c14,
	0x00000c18, 0x00000c1c, 0x00000c20, 0x00000c24, 0x00000c28, 0x00000c2c,
	0x00000c30, 0x00000c34, 0x00000c38, 0x00000c3c, 0x00000c40, 0x00000c44,
	0x00000c48, 0x00000c4c, 0x00000c50, 0x00000c54, 0x00000c58, 0x00000c5c,
	0x00000c60, 0x00000c64, 0x00000c68, 0x00000c6c, 0x00000c70, 0x00000c74,
	0x00000c78, 0x00000c7c, 0x00000c80, 0x00000c84, 0x00000c88, 0x00000c8c,
	0x00000c90, 0x00000c94, 0x00000c98, 0x00000c9c, 0x00000d00, 0x00000d04,
	0x00000d08, 0x00000d0c, 0x00000d10, 0x00000d14, 0x00000d18, 0x00000d1c,
	0x00000d20
};

void printDweInfo(struct dwe_hw_info *pDweHwInfo)
{
	printk(KERN_ERR "Print DWE_PARAMS :  \n");
	printk(KERN_ERR "DWE_PARAMS## split_line:%d\n", pDweHwInfo->split_line);
	printk(KERN_ERR "DWE_PARAMS## scale_factor:%d\n",
	       pDweHwInfo->scale_factor);
	printk(KERN_ERR "DWE_PARAMS## in_format:%d\n", pDweHwInfo->in_format);
	printk(KERN_ERR "DWE_PARAMS## out_format:%d\n", pDweHwInfo->out_format);
	printk(KERN_ERR "DWE_PARAMS## hand_shake:%d\n", pDweHwInfo->hand_shake);
	printk(KERN_ERR "DWE_PARAMS## roi_x:%d, roi_y:%d\n", pDweHwInfo->roi_x,
	       pDweHwInfo->roi_y);
	printk(KERN_ERR
	       "DWE_PARAMS## boundary_y:%d, boundary_u:%d, boundary_v:%d \n",
	       pDweHwInfo->boundary_y, pDweHwInfo->boundary_u,
	       pDweHwInfo->boundary_v);
	printk(KERN_ERR "DWE_PARAMS## map_w:%d, map_h:%d\n", pDweHwInfo->map_w,
	       pDweHwInfo->map_h);
	printk(KERN_ERR "DWE_PARAMS## src_auto_shadow:%d, dst_auto_shadow:%d\n",
	       pDweHwInfo->src_auto_shadow, pDweHwInfo->dst_auto_shadow);
	printk(KERN_ERR "DWE_PARAMS## src_w:%d, src_stride:%d, src_h:%d\n",
	       pDweHwInfo->src_w, pDweHwInfo->src_stride, pDweHwInfo->src_h);
	printk(KERN_ERR
	       "DWE_PARAMS## dst_w:%d, dst_stride:%d, dst_h:%d, dst_size_uv:%d\n",
	       pDweHwInfo->dst_w, pDweHwInfo->dst_stride, pDweHwInfo->dst_h,
	       pDweHwInfo->dst_size_uv);
	printk(KERN_ERR "DWE_PARAMS## split_h:%d, split_v1:%d, split_v2:%d\n",
	       pDweHwInfo->split_h, pDweHwInfo->split_v1, pDweHwInfo->split_v2);
	printk(KERN_ERR "DWE_PARAMS## dwe_start:%d\n", pDweHwInfo->dwe_start);
}

void printVseInfo(struct vse_params *pVseHwInfo)
{
	int i = 0;
	printk(KERN_ERR "Print VSE_PARAMS :  \n");
	printk(KERN_ERR "VSE_PARAMS## src_w:%d, src_h:%d\n", pVseHwInfo->src_w,
	       pVseHwInfo->src_h);
	printk(KERN_ERR "VSE_PARAMS## in_format:%d, in_yuvbit:%d\n",
	       pVseHwInfo->in_format, pVseHwInfo->in_yuvbit);
	printk(KERN_ERR "VSE_PARAMS## input_select:%d\n",
	       pVseHwInfo->input_select);
	printk(KERN_ERR "VSE_PARAMS## vse_start:%d, \n", pVseHwInfo->vse_start);
	for (i = 0; i < 3; i++) {
		printk(KERN_ERR
		       "VSE_PARAMS## crop_size[%d]:[left:%d,right:%d,top:%d,bottom:%d]\n",
		       i, pVseHwInfo->crop_size[i].left,
		       pVseHwInfo->crop_size[i].right,
		       pVseHwInfo->crop_size[i].top,
		       pVseHwInfo->crop_size[i].bottom);
	}
	for (i = 0; i < 3; i++) {
		printk(KERN_ERR
		       "VSE_PARAMS## out_size[%d]:[width:%d,height:%d]\n",
		       i, pVseHwInfo->out_size[i].width,
		       pVseHwInfo->out_size[i].height);
	}
	for (i = 0; i < 3; i++) {
		printk(KERN_ERR
		       "VSE_PARAMS## format_conv[%d]:[in_format:%d,out_format:%d]\n",
		       i, pVseHwInfo->format_conv[i].in_format,
		       pVseHwInfo->format_conv[i].out_format);
	}
	for (i = 0; i < 3; i++) {
		printk(KERN_ERR "VSE_PARAMS## resize_enable[%d]:[enable:%d]\n",
		       i, pVseHwInfo->resize_enable[i]);
	}
	for (i = 0; i < 3; i++) {
		printk(KERN_ERR
		       "VSE_PARAMS## mi_settings[%d]:[enable:%d,out_format:%d,width:%d,height:%d,yuvbit:%d]\n",
		       i, pVseHwInfo->mi_settings[i].enable,
		       pVseHwInfo->mi_settings[i].out_format,
		       pVseHwInfo->mi_settings[i].width,
		       pVseHwInfo->mi_settings[i].height,
		       pVseHwInfo->mi_settings[i].yuvbit);
	}
}

static void readVseRegAndPrint(struct dw200_subdev *pdwe_dev, u32 offset)
{
	u32 reg;
	reg = vse_read_reg(pdwe_dev, offset);
	printk(KERN_ERR "DUMP dw200 reg : 0x%04x=0x%08x\n", offset, reg);
}

void readVseReg(struct dw200_subdev *pdwe_dev)
{
	u32 i = 0;
	u32 reg_array_size = sizeof(dw200_reg_offset) / sizeof(u32);
	for (i = 0; i < reg_array_size; i++) {
		readVseRegAndPrint(pdwe_dev, dw200_reg_offset[i]);
	}
}
