#ifndef __VIVSOC_DVP2AXI_H__
#define __VIVSOC_DVP2AXI_H__

#include <linux/module.h>
#include <linux/platform_device.h>

#include "soc_ioctl.h"

// VI DVP2AXI registers and masks
#define VI_DVP2AXI_CTRL0_CSR        0x4000
#define VI_DVP2AXI_RST_DONE_MASK    BIT(14)
#define VI_DVP2AXI_SOFT_RST_MASK    BIT(6)

#define VI_DVP2AXI_CTRL1_CSR        0x4004
#define IODVP_MUX_LSB               30
#define IODVP_MUX_BITS              2
#define PIX_WIDTH_BITS              5
#define DVP0_PIX_WIDTH_LSB          20
#define DVP1_PIX_WIDTH_LSB          25

#define VI_DVP2AXI_CTRL2_CSR        0x4008
#define DVP2_PIX_WIDTH_LSB          0
#define DVP3_PIX_WIDTH_LSB          5
#define DVP4_PIX_WIDTH_LSB          10
#define DVP5_PIX_WIDTH_LSB          15

#define VI_DVP2AXI_CTRL3_CSR        0x400c
#define VI_DVP2AXI_CTRL4_CSR        0x4010
#define VI_DVP2AXI_CTRL5_CSR        0x4014
#define VI_DVP2AXI_CTRL6_CSR        0x4018
#define VI_DVP2AXI_CTRL7_CSR        0x401c
#define VI_DVP2AXI_CTRL8_CSR        0x4020

#define VI_DVP2AXI_CTRL9_CSR        0x4024
#define VI_DVP2AXI_CTRL10_CSR       0x4028
#define VI_DVP2AXI_CTRL11_CSR       0x402c
#define VI_DVP2AXI_CTRL12_CSR       0x4030
#define VI_DVP2AXI_CTRL13_CSR       0x4034
#define VI_DVP2AXI_CTRL14_CSR       0x4038
#define VI_DVP2AXI_CTRL15_CSR       0x403c
#define VI_DVP2AXI_CTRL16_CSR       0x4040
#define VI_DVP2AXI_CTRL17_CSR       0x4044
#define VI_DVP2AXI_CTRL18_CSR       0x4048
#define VI_DVP2AXI_CTRL19_CSR       0x404c
#define VI_DVP2AXI_CTRL20_CSR       0x4050
#define VI_DVP2AXI_CTRL21_CSR       0x4054
#define VI_DVP2AXI_CTRL22_CSR       0x4058
#define VI_DVP2AXI_CTRL23_CSR       0x405c
#define VI_DVP2AXI_CTRL24_CSR       0x4060
#define VI_DVP2AXI_CTRL25_CSR       0x4064
#define VI_DVP2AXI_CTRL26_CSR       0x4068
#define VI_DVP2AXI_CTRL27_CSR       0x406c
#define VI_DVP2AXI_CTRL28_CSR       0x4070
#define VI_DVP2AXI_CTRL29_CSR       0x4074
#define VI_DVP2AXI_CTRL30_CSR       0x4078
#define VI_DVP2AXI_CTRL31_CSR       0x407c
#define VI_DVP2AXI_CTRL32_CSR       0x4080

#define STRIDE_BITS                 16
#define VI_DVP2AXI_CTRL33_CSR       0x4084
#define DVP0_HS_STRIDE_LSB          0
#define DVP1_HS_STRIDE_LSB          16
#define VI_DVP2AXI_CTRL34_CSR       0x4088
#define DVP2_HS_STRIDE_LSB          0
#define DVP3_HS_STRIDE_LSB          16
#define VI_DVP2AXI_CTRL35_CSR       0x408c
#define DVP4_HS_STRIDE_LSB          0
#define DVP5_HS_STRIDE_LSB          16

#define VI_DVP2AXI_CTRL36_CSR       0x4090
#define HDR_ID_BITS                 2
#define DVP0_FIRST_ID_LSB           0
#define DVP1_FIRST_ID_LSB           2
#define DVP2_FIRST_ID_LSB           4
#define DVP3_FIRST_ID_LSB           6
#define DVP4_FIRST_ID_LSB           8
#define DVP5_FIRST_ID_LSB           10
#define DVP0_LAST_ID_LSB            18
#define DVP1_LAST_ID_LSB            20
#define DVP2_LAST_ID_LSB            22
#define DVP3_LAST_ID_LSB            24
#define DVP4_LAST_ID_LSB            26
#define DVP5_LAST_ID_LSB            28

#define VI_DVP2AXI_INT0             0x40a0
#define DVP0_FLUSH_MASK             0x7
#define DVP1_FLUSH_MASK             0x38
#define DVP2_FLUSH_MASK             0x1c0
#define DVP0_DONE_MASK              0xe00
#define DVP1_DONE_MASK              0x7000
#define DVP2_DONE_MASK              0x38000

#define VI_DVP2AXI_INT1             0x40a4
#define DVP3_FLUSH_MASK             0x7
#define DVP4_FLUSH_MASK             0x38
#define DVP5_FLUSH_MASK             0x1c0
#define DVP3_DONE_MASK              0xe00
#define DVP4_DONE_MASK              0x7000
#define DVP5_DONE_MASK              0x38000

#define VI_DVP2AXI_INT2             0x40a8
#define VI_DVP2AXI_INT_MASK0        0x40ac
#define DVP0_DONE_MASK_LSB          9
#define DVP0_FLUSH_MASK_LSB         0
#define DVP1_DONE_MASK_LSB          12
#define DVP1_FLUSH_MASK_LSB         3
#define DVP2_DONE_MASK_LSB          15
#define DVP2_FLUSH_MASK_LSB         6

#define VI_DVP2AXI_INT_MASK1        0x40b0
#define DVP3_DONE_MASK_LSB          9
#define DVP3_FLUSH_MASK_LSB         0
#define DVP4_DONE_MASK_LSB          12
#define DVP4_FLUSH_MASK_LSB         3
#define DVP5_DONE_MASK_LSB          15
#define DVP5_FLUSH_MASK_LSB         6
#define VI_DVP2AXI_INT_MASK2        0x40b4

#define VI_DVP2AXI_CTRL37_CSR       0x4094
#define DVP0_EMD_STRIDE_LSB         0
#define DVP1_EMD_STRIDE_LSB         16
#define VI_DVP2AXI_CTRL38_CSR       0x4098
#define DVP2_EMD_STRIDE_LSB         0
#define DVP3_EMD_STRIDE_LSB         16
#define VI_DVP2AXI_CTRL39_CSR       0x409c
#define DVP4_EMD_STRIDE_LSB         0
#define DVP5_EMD_STRIDE_LSB         16

#define EMD_HSNUM_BITS              16
#define VI_DVP2AXI_CTRL40_CSR       0x40e0
#define DVP0_EMD_HSNUM_LSB          0
#define DVP1_EMD_HSNUM_LSB          16
#define VI_DVP2AXI_CTRL41_CSR       0x40e4
#define DVP2_EMD_HSNUM_LSB          0
#define DVP3_EMD_HSNUM_LSB          16
#define VI_DVP2AXI_CTRL42_CSR       0x40e8
#define DVP4_EMD_HSNUM_LSB          0
#define DVP5_EMD_HSNUM_LSB          16
#define VI_DVP2AXI_CTRL43_CSR       0x40ec

/*
 * Save frame number from sensor, buffer size should be +1 frame size
 */
#define SENSOR_FRAME_NUM 1

#define VI_DVP2AXI_ERRIRQ_NUM       9
#define VI_DVP2AXI_DVP_CHNS         6
#define VI_DVP2AXI_VIRTUAL_CHNS     4

#endif /* __VIVSOC_DVP2AXI_H__ */
