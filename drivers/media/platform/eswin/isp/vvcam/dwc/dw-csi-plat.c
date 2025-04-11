// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 *
 * Synopsys DesignWare MIPI CSI-2 Host controller driver.
 * Platform driver
 *
 * Author: Luis Oliveira <luis.oliveira@synopsys.com>
 */

#include "include/dw-csi-data.h"
#include "include/dw-dphy-data.h"

#include "dw-csi-plat.h"


void __iomem *csc;
void __iomem *demo;

#ifdef OYX_MOD
static int init_dphy(void __iomem *base_addr){
	writel(0x30  ,base_addr + 4*0xc10  ) ;//PPI_STARTUP_RW_COMMON_DPHY_10
    writel(0x444 ,base_addr + 4*0x1cf2 ) ;//CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_2
    writel(0x1444,base_addr + 4*0x1cf2 ) ;//CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_2
    writel(0x1bfd,base_addr + 4*0x1cf0 ) ;//CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_0
    writel(0x233 ,base_addr + 4*0xc11  ) ;//PPI_STARTUP_RW_COMMON_STARTUP_1_1
    writel(0x27  ,base_addr + 4*0xc06  ) ;//PPI_STARTUP_RW_COMMON_DPHY_6
    writel(0x1f4 ,base_addr + 4*0xc26  ) ;//PPI_CALIBCTRL_RW_COMMON_BG_0
    writel(0x320 ,base_addr + 4*0xe02  ) ;//PPI_RW_LPDCOCAL_NREF
    writel(0x1b  ,base_addr + 4*0xe03  ) ;//PPI_RW_LPDCOCAL_NREF_RANGE
    writel(0xfec8,base_addr + 4*0xe05  ) ;//PPI_RW_LPDCOCAL_TWAIT_CONFIG
    writel(0x646e,base_addr + 4*0xe06  ) ;//PPI_RW_LPDCOCAL_VT_CONFIG
    writel(0x646e,base_addr + 4*0xe06  ) ;//PPI_RW_LPDCOCAL_VT_CONFIG
    writel(0x646e,base_addr + 4*0xe06  ) ;//PPI_RW_LPDCOCAL_VT_CONFIG
    writel(0x105 ,base_addr + 4*0xe08  ) ;//PPI_RW_LPDCOCAL_COARSE_CFG
    writel(0x3   ,base_addr + 4*0xe36  ) ;//PPI_RW_COMMON_CFG
    writel(0x5   ,base_addr + 4*0xc02  ) ;//PPI_STARTUP_RW_COMMON_DPHY_2
    writel(0x17  ,base_addr + 4*0xe40  ) ;//PPI_RW_TERMCAL_CFG_0
    writel(0x4   ,base_addr + 4*0xe50  ) ;//PPI_RW_OFFSETCAL_CFG_0
    writel(0x5f  ,base_addr + 4*0xe01  ) ;//PPI_RW_LPDCOCAL_TIMEBASE
    writel(0xfe1d,base_addr + 4*0xe05  ) ;//PPI_RW_LPDCOCAL_TWAIT_CONFIG
    writel(0xeee ,base_addr + 4*0xe06  ) ;//PPI_RW_LPDCOCAL_VT_CONFIG
    writel(0x0   ,base_addr + 4*0x1c20 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_0
    writel(0x400 ,base_addr + 4*0x1c21 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_1
    writel(0x400 ,base_addr + 4*0x1c21 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_1
    writel(0x41f6,base_addr + 4*0x1c23 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_3
    writel(0x0   ,base_addr + 4*0x1c20 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_0
    writel(0x43f6,base_addr + 4*0x1c23 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_3
    writel(0x2000,base_addr + 4*0x1c26 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6
    writel(0x0   ,base_addr + 4*0x1c27 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7
    writel(0x3000,base_addr + 4*0x1c26 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6
    writel(0x0   ,base_addr + 4*0x1c27 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7
    writel(0x7000,base_addr + 4*0x1c26 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6
    writel(0x0   ,base_addr + 4*0x1c27 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7
    writel(0x4000,base_addr + 4*0x1c25 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_5
    writel(0xf4  ,base_addr + 4*0x1c40 ) ;//CORE_DIG_RW_COMMON_0
    writel(0xf4  ,base_addr + 4*0x1c40 ) ;//CORE_DIG_RW_COMMON_0
    writel(0x14  ,base_addr + 4*0x1c47 ) ;//CORE_DIG_RW_COMMON_7
    writel(0x10  ,base_addr + 4*0x1c47 ) ;//CORE_DIG_RW_COMMON_7
    writel(0x0   ,base_addr + 4*0x1c47 ) ;//CORE_DIG_RW_COMMON_7
    writel(0x50  ,base_addr + 4*0xc08  ) ;//PPI_STARTUP_RW_COMMON_DPHY_8
    writel(0x68  ,base_addr + 4*0xc07  ) ;//PPI_STARTUP_RW_COMMON_DPHY_7
    writel(0x473c,base_addr + 4*0x3040 ) ;//CORE_DIG_DLANE_0_RW_LP_0
    writel(0x473c,base_addr + 4*0x3240 ) ;//CORE_DIG_DLANE_1_RW_LP_0
    writel(0x0   ,base_addr + 4*0x1022 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_2
    writel(0x1   ,base_addr + 4*0x1222 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_2
    writel(0x0   ,base_addr + 4*0x1422 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_2
    writel(0x9   ,base_addr + 4*0x1c46 ) ;//CORE_DIG_RW_COMMON_6
    writel(0x9   ,base_addr + 4*0x1c46 ) ;//CORE_DIG_RW_COMMON_6
    writel(0x802 ,base_addr + 4*0x102c ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_12
    writel(0x802 ,base_addr + 4*0x122c ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_12
    writel(0x802 ,base_addr + 4*0x142c ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_12
    writel(0x2   ,base_addr + 4*0x102d ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_13
    writel(0x2   ,base_addr + 4*0x122d ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_13
    writel(0x2   ,base_addr + 4*0x142d ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_13
    writel(0x802 ,base_addr + 4*0x102c ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_12
    writel(0x802 ,base_addr + 4*0x122c ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_12
    writel(0x802 ,base_addr + 4*0x142c ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_12
    writel(0xa   ,base_addr + 4*0x102d ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_13
    writel(0xa   ,base_addr + 4*0x122d ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_13
    writel(0xa   ,base_addr + 4*0x142d ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_13
    writel(0xa70 ,base_addr + 4*0x1229 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
    writel(0x0   ,base_addr + 4*0x102a ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_10
    writel(0x0   ,base_addr + 4*0x122a ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_10
    writel(0x0   ,base_addr + 4*0x142a ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_10
    writel(0x4   ,base_addr + 4*0x102f ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_15
    writel(0x4   ,base_addr + 4*0x122f ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_15
    writel(0x4   ,base_addr + 4*0x142f ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_15
    writel(0x91c ,base_addr + 4*0x3880 ) ;//CORE_DIG_DLANE_CLK_RW_HS_RX_0
    writel(0x3b06,base_addr + 4*0x3887 ) ;//CORE_DIG_DLANE_CLK_RW_HS_RX_7
    writel(0xe1d ,base_addr + 4*0x3080 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_0
    writel(0xe1d ,base_addr + 4*0x3280 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_0
    writel(0x0   ,base_addr + 4*0x3001 ) ;//CORE_DIG_DLANE_0_RW_CFG_1
    writel(0x0   ,base_addr + 4*0x3201 ) ;//CORE_DIG_DLANE_1_RW_CFG_1
    writel(0x8   ,base_addr + 4*0x3001 ) ;//CORE_DIG_DLANE_0_RW_CFG_1
    writel(0x8   ,base_addr + 4*0x3201 ) ;//CORE_DIG_DLANE_1_RW_CFG_1
    writel(0xe69b,base_addr + 4*0x3082 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_2
    writel(0xe69b,base_addr + 4*0x3282 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_2
    writel(0x173c,base_addr + 4*0x3040 ) ;//CORE_DIG_DLANE_0_RW_LP_0
    writel(0x173c,base_addr + 4*0x3240 ) ;//CORE_DIG_DLANE_1_RW_LP_0
    writel(0x0   ,base_addr + 4*0x3042 ) ;//CORE_DIG_DLANE_0_RW_LP_2
    writel(0x0   ,base_addr + 4*0x3242 ) ;//CORE_DIG_DLANE_1_RW_LP_2
    writel(0x163c,base_addr + 4*0x3840 ) ;//CORE_DIG_DLANE_CLK_RW_LP_0
    writel(0x0   ,base_addr + 4*0x3842 ) ;//CORE_DIG_DLANE_CLK_RW_LP_2
    writel(0xe69b,base_addr + 4*0x3082 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_2
    writel(0xe69b,base_addr + 4*0x3282 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_2
    writel(0x4010,base_addr + 4*0x3081 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_1
    writel(0x4010,base_addr + 4*0x3281 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_1
    writel(0xe69b,base_addr + 4*0x3082 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_2
    writel(0xe69b,base_addr + 4*0x3282 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_2
    writel(0x9209,base_addr + 4*0x3083 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_3
    writel(0x9209,base_addr + 4*0x3283 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_3
    writel(0x96  ,base_addr + 4*0x3084 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_4
    writel(0x96  ,base_addr + 4*0x3284 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_4
    writel(0x100 ,base_addr + 4*0x3085 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_5
    writel(0x100 ,base_addr + 4*0x3285 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_5
    writel(0x100 ,base_addr + 4*0x3085 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_5
    writel(0x100 ,base_addr + 4*0x3285 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_5
    writel(0x2d02,base_addr + 4*0x3086 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_6
    writel(0x2d02,base_addr + 4*0x3286 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_6
    writel(0x1b06,base_addr + 4*0x3087 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_7
    writel(0x1b06,base_addr + 4*0x3287 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_7
    writel(0x1b06,base_addr + 4*0x3087 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_7
    writel(0x1b06,base_addr + 4*0x3287 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_7
    writel(0x9201,base_addr + 4*0x3083 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_3
    writel(0x9201,base_addr + 4*0x3283 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_3
    writel(0x0   ,base_addr + 4*0x3089 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_9
    writel(0x0   ,base_addr + 4*0x3289 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_9
    writel(0x2   ,base_addr + 4*0x3086 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_6
    writel(0x2   ,base_addr + 4*0x3286 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_6
    writel(0x404 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x40c ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x414 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x41c ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x423 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x429 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x430 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x43a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x445 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x44a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x450 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x45a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x465 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x469 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x472 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x47a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x485 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x489 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x490 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x49a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4a4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4ac ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4b4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4bc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4c4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4cc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4d4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4dc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4e4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4ec ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4f4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x4fc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x504 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x50c ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x514 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x51c ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x523 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x529 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x530 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x53a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x545 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x54a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x550 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x55a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x565 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x569 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x572 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x57a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x585 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x589 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x590 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x59a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5a4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5ac ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5b4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5bc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5c4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5cc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5d4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5dc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5e4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5ec ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5f4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x5fc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x604 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x60c ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x614 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x61c ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x623 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x629 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x632 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x63a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x645 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x64a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x650 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x65a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x665 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x669 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x672 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x67a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x685 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x689 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x690 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x69a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6a4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6ac ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6b4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6bc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6c4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6cc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6d4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6dc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6e4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6ec ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6f4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x6fc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x704 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x70c ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x714 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x71c ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x723 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x72a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x730 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x73a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x745 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x74a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x750 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x75a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x765 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x769 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x772 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x77a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x785 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x789 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x790 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x79a ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7a4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7ac ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7b4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7bc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7c4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7cc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7d4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7dc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7e4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7ec ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7f4 ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x7fc ,base_addr + 4*0x1ff0 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    writel(0x3   ,base_addr + 4*0x3800 ) ;//CORE_DIG_DLANE_CLK_RW_CFG_0
    writel(0x3   ,base_addr + 4*0x3800 ) ;//CORE_DIG_DLANE_CLK_RW_CFG_0
    writel(0x3   ,base_addr + 4*0x3000 ) ;//CORE_DIG_DLANE_0_RW_CFG_0
    writel(0x3   ,base_addr + 4*0x3200 ) ;//CORE_DIG_DLANE_1_RW_CFG_0
    writel(0x3   ,base_addr + 4*0x3000 ) ;//CORE_DIG_DLANE_0_RW_CFG_0
    writel(0x3   ,base_addr + 4*0x3200 ) ;//CORE_DIG_DLANE_1_RW_CFG_0
    writel(0xbf0 ,base_addr + 4*0x1029 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
    writel(0xb70 ,base_addr + 4*0x1229 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
    writel(0xbf0 ,base_addr + 4*0x1429 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9
	udelay(20000);
    return 0;
}
#endif

static struct mipi_fmt *
find_dw_mipi_csi_format(struct v4l2_mbus_framefmt *mf)
{
	unsigned int i;

	pr_info("%s entered mbus: 0x%x\n", __func__, mf->code);

	for (i = 0; i < ARRAY_SIZE(dw_mipi_csi_formats); i++)
		if (mf->code == dw_mipi_csi_formats[i].mbus_code) {
			pr_info("Found mbus 0x%x\n", dw_mipi_csi_formats[i].mbus_code);
			return &dw_mipi_csi_formats[i];
		}
	return NULL;
}

static int dw_mipi_csi_enum_mbus_code(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *cfg,
				      struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = dw_mipi_csi_formats[code->index].mbus_code;
	return 0;
}

static struct mipi_fmt *
dw_mipi_csi_try_format(struct v4l2_mbus_framefmt *mf)
{
	struct mipi_fmt *fmt;

	fmt = find_dw_mipi_csi_format(mf);
	if (!fmt)
		fmt = &dw_mipi_csi_formats[0];

	mf->code = fmt->mbus_code;

	return fmt;
}

static struct v4l2_mbus_framefmt *
dw_mipi_csi_get_format(struct dw_csi *dev, struct v4l2_subdev_state *cfg,
		       enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return cfg ? v4l2_subdev_get_try_format(&dev->sd, cfg, 0) : NULL;
	dev_info(dev->dev,
		"%s got v4l2_mbus_pixelcode. 0x%x\n", __func__,
		dev->format.code);
	dev_info(dev->dev,
		"%s got width. 0x%x\n", __func__,
		dev->format.width);
	dev_info(dev->dev,
		"%s got height. 0x%x\n", __func__,
		dev->format.height);
	/*dev_dbg(dev->dev,
		"%s got v4l2_mbus_pixelcode. 0x%x\n", __func__,
		dev->format.code);
	dev_dbg(dev->dev,
		"%s got width. 0x%x\n", __func__,
		dev->format.width);
	dev_dbg(dev->dev,
		"%s got height. 0x%x\n", __func__,
		dev->format.height);*/
	return &dev->format;
}

static int
dw_mipi_csi_set_fmt(struct v4l2_subdev *sd,
		    struct v4l2_subdev_state *cfg,
		    struct v4l2_subdev_format *fmt)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);
	struct mipi_fmt *dev_fmt = NULL;
	struct v4l2_mbus_framefmt *mf = &fmt->format;

	dev_info(dev->dev,
		"%s got mbus_pixelcode. 0x%x\n", __func__,
		fmt->format.code);

	dev_fmt = dw_mipi_csi_try_format(&fmt->format);
	dev_info(dev->dev,
		"%s got v4l2_mbus_pixelcode. 0x%x\n", __func__,
		dev_fmt->mbus_code);
	if (!fmt)
		return -EINVAL;

	if (dev_fmt) {
		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			dev->fmt = dev_fmt;
		dev->fmt->mbus_code = mf->code;
		dw_mipi_csi_set_ipi_fmt(dev);
	}

	return 0;
}

static int
dw_mipi_csi_get_fmt(struct v4l2_subdev *sd,
		    struct v4l2_subdev_state *cfg,
		    struct v4l2_subdev_format *fmt)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = dw_mipi_csi_get_format(dev, cfg, fmt->which);
	if (!mf)
		return -EINVAL;

	mutex_lock(&dev->lock);
	fmt->format = *mf;
	mutex_unlock(&dev->lock);

	return 0;
}

static int
dw_mipi_csi_s_power(struct v4l2_subdev *sd, int on)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);

	dev_info(dev->dev, "%s: on=%d\n", __func__, on);
	if (on) {
		dw_mipi_csi_hw_stdby(dev);
		dw_mipi_csi_start(dev);
#ifdef OYX_MOD
		void __iomem *base = ioremap(0x510d8000, 0x10);
		writel(0x2c3f5, base + 0x0); // phy_cfg0
		void __iomem *base1 = ioremap(0x510c0000, 0x20000);
		init_dphy(base1);
		dw_mipi_csi_recfg(dev);
		unsigned int i ;
		for (i = 10; i > 0; i--){
			if (readl(base + 0x4) == 3)
				break;
			udelay(20000);
		}

		if (readl(base + 0x4) != 3) {
			printk("===========err========phy lock===\n");
			printk("===========err========phy lock===\n");
			printk("===========err========phy lock===\n");
			printk("===========err========phy lock===\n");
		} else {
			printk("===========ok========phy lock===\n");
		}
		writel(0x2c075, base + 0x0); // phy_cfg0
#endif
	} else {
#ifdef DWC_PHY_USING
		phy_power_off(dev->phy);
#endif
		dw_mipi_csi_mask_irq_power_off(dev);
	}
	return 0;
}

static int
dw_mipi_csi_log_status(struct v4l2_subdev *sd)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);

	dw_mipi_csi_dump(dev);

	return 0;
}

#if IS_ENABLED(CONFIG_VIDEO_ADV_DEBUG)
static int
dw_mipi_csi_g_register(struct v4l2_subdev *sd, struct v4l2_dbg_register *reg)
{
	struct dw_csi *dev = sd_to_mipi_csi_dev(sd);

	dev_vdbg(dev->dev, "%s: reg=%llu\n", __func__, reg->reg);
	reg->val = dw_mipi_csi_read(dev, reg->reg);

	return 0;
}
#endif
static int dw_mipi_csi_init_cfg(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *cfg)
{
	struct v4l2_mbus_framefmt *format =
	    v4l2_subdev_get_try_format(sd, cfg, 0);

	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->code = MEDIA_BUS_FMT_RGB888_1X24;
	format->field = V4L2_FIELD_NONE;

	return 0;
}

static int dw_mipi_set_stream(struct v4l2_subdev *sd, int enable)
{
	return dw_mipi_csi_s_power(sd, enable);
}

#define DW_MIPI_STREAM_CMD _IOWR('u', 0x20, int)

long dw_mipi_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	switch (cmd) {
	case DW_MIPI_STREAM_CMD: {
		int *enable = arg;
		dw_mipi_csi_s_power(sd, *enable);
		break;
	}
	default:
		break;
	}

	return 0;
}

static struct v4l2_subdev_core_ops dw_mipi_csi_core_ops = {
	.s_power = dw_mipi_csi_s_power,
	.log_status = dw_mipi_csi_log_status,
	.ioctl = dw_mipi_ioctl,
#if IS_ENABLED(CONFIG_VIDEO_ADV_DEBUG)
	.g_register = dw_mipi_csi_g_register,
#endif
};

static struct v4l2_subdev_pad_ops dw_mipi_csi_pad_ops = {
	.init_cfg = dw_mipi_csi_init_cfg,
	.enum_mbus_code = dw_mipi_csi_enum_mbus_code,
	.get_fmt = dw_mipi_csi_get_fmt,
	.set_fmt = dw_mipi_csi_set_fmt,
};

static const struct v4l2_subdev_video_ops dw_mipi_video_ops = {
	.s_stream = dw_mipi_set_stream,
};

static struct v4l2_subdev_ops dw_mipi_csi_subdev_ops = {
	.core = &dw_mipi_csi_core_ops,
	.pad = &dw_mipi_csi_pad_ops,
	.video = &dw_mipi_video_ops,
};

static irqreturn_t dw_mipi_csi_irq1(int irq, void *dev_id)
{
	struct dw_csi *csi_dev = dev_id;

	dw_mipi_csi_irq_handler(csi_dev);

	return IRQ_HANDLED;
}

static int
dw_mipi_csi_parse_dt(struct platform_device *pdev, struct dw_csi *dev)
{
	struct device_node *node = pdev->dev.of_node;
	struct v4l2_fwnode_endpoint ep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	int ret = 0;

	if (of_property_read_u32(node, "snps,output-type", &dev->hw.output))
		dev->hw.output = 2;

	if (of_property_read_u32(node, "snps,en-ppi-width", &dev->hw.ppi_width))
		dev->hw.ppi_width = 0;

	if (dev->hw.ppi_width > 1) {
		dev_err(&pdev->dev, "Wrong ppi width(%d), valid value is '0'(ppi8) or '1'(ppi16)\n",
				dev->hw.ppi_width);
		return -EINVAL;
	}

	if (of_property_read_u32(node, "snps,en-phy-mode", &dev->hw.phy_mode))
		dev->hw.phy_mode = 0;

	if (of_property_read_u32(node, "ipi2_en", &dev->hw.ipi2_en))
		dev->hw.ipi2_en = 0;

	if (of_property_read_u32(node, "ipi2_vcid", &dev->hw.ipi2_vcid))
		dev->hw.ipi2_vcid = 0;

	if (of_property_read_u32(node, "ipi3_en", &dev->hw.ipi3_en))
		dev->hw.ipi3_en = 0;

	if (of_property_read_u32(node, "ipi3_vcid", &dev->hw.ipi3_vcid))
		dev->hw.ipi3_vcid = 0;

	node = of_graph_get_next_endpoint(node, NULL);
	if (!node) {
		dev_err(&pdev->dev, "No port node at %pOF\n",
			pdev->dev.of_node);
		return -EINVAL;
	}
	/* Get port node and validate MIPI-CSI channel id. */
	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(node), &ep);
	if (ret)
		goto err;

	dev->index = ep.base.port - 1;
	if (dev->index >= CSI_MAX_ENTITIES) {
		ret = -ENXIO;
		goto err;
	}
	dev->hw.num_lanes = ep.bus.mipi_csi2.num_data_lanes;
err:
	of_node_put(node);
	return ret;
}

static const struct of_device_id dw_mipi_csi_of_match[];

static int dw_csi_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id = NULL;
	struct dw_csih_pdata *pdata = NULL;
	struct device *dev = &pdev->dev;
	struct dw_csi *csi;
	struct v4l2_subdev *sd;
	int ret;

	dev_info(dev, "DW MIPI CSI-2 Host Probe in. \n");

	if (!IS_ENABLED(CONFIG_OF))
		pdata = pdev->dev.platform_data;

	dev_vdbg(dev, "Probing started\n");

	/* Resource allocation */
	csi = devm_kzalloc(dev, sizeof(*csi), GFP_KERNEL);
	if (!csi)
		return -ENOMEM;

	mutex_init(&csi->lock);
	spin_lock_init(&csi->slock);
	csi->dev = dev;

	if (dev->of_node) {
		of_id = of_match_node(dw_mipi_csi_of_match, dev->of_node);
		if (!of_id)
			return -EINVAL;

		ret = dw_mipi_csi_parse_dt(pdev, csi);
		if (ret < 0)
			return ret;

#ifdef DWC_PHY_USING
		csi->phy = devm_of_phy_get(dev, dev->of_node, NULL);
		if (IS_ERR(csi->phy)) {
			dev_err(dev, "No DPHY available\n");
			return PTR_ERR(csi->phy);
		}
#endif
	} else {
#ifdef DWC_PHY_USING
		csi->phy = devm_phy_get(dev, phys[pdata->id].name);
		if (IS_ERR(csi->phy)) {
			dev_err(dev, "No '%s' DPHY available\n",
				phys[pdata->id].name);
			return PTR_ERR(csi->phy);
		}
		dev_info(dev, "got D-PHY %s with id %d\n", phys[pdata->id].name,
			 csi->phy->id);
#endif
	}

	/* Registers mapping */
	csi->base_address = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csi->base_address)) {
		dev_err(dev, "base address not set.\n");
		return PTR_ERR(csi->base_address);
	}

	csi->ctrl_irq_number = platform_get_irq(pdev, 0);
	if (csi->ctrl_irq_number < 0) {
		dev_err(dev, "irq number %d not set.\n", csi->ctrl_irq_number);
		ret = csi->ctrl_irq_number;
		goto end;
	}

	csi->rst = devm_reset_control_get_optional_shared(dev, NULL);
	if (IS_ERR(csi->rst)) {
		dev_err(dev, "error getting reset control %d\n", ret);
		return PTR_ERR(csi->rst);
	}

	ret = devm_request_irq(dev, csi->ctrl_irq_number, dw_mipi_csi_irq1,
			       IRQF_SHARED, dev_name(dev), csi);
	if (ret) {
		if (dev->of_node)
			dev_err(dev, "irq csi %s failed\n", of_id->name);
		else
			dev_err(dev, "irq csi %d failed\n", pdata->id);

		goto end;
	}

	sd = &csi->sd;
	v4l2_subdev_init(sd, &dw_mipi_csi_subdev_ops);
	sd->dev = dev;
	csi->sd.owner = THIS_MODULE;

	if (dev->of_node) {
		snprintf(sd->name, sizeof(sd->name), "%s.%d",
			 "dw-csi", csi->index);

		csi->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	} else {
		strlcpy(sd->name, dev_name(dev), sizeof(sd->name));
	}
	csi->fmt = &dw_mipi_csi_formats[0];
	csi->format.code = dw_mipi_csi_formats[0].mbus_code;

	sd->entity.function = MEDIA_ENT_F_IO_V4L;

	if (dev->of_node) {
		csi->pads[CSI_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
		csi->pads[CSI_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

		ret = media_entity_pads_init(&csi->sd.entity, CSI_PADS_NUM, csi->pads);
		if (ret < 0) {
			dev_err(dev, "media entity init failed\n");
			goto end;
		}
	} else {
		csi->hw.num_lanes = pdata->lanes;
		csi->hw.pclk = pdata->pclk;
		csi->hw.fps = pdata->fps;
		csi->hw.dphy_freq = pdata->hs_freq;
	}

	v4l2_set_subdevdata(&csi->sd, pdev);
	platform_set_drvdata(pdev, &csi->sd);
	dev_set_drvdata(dev, sd);

	if (csi->rst)
		reset_control_deassert(csi->rst);

#if IS_ENABLED(CONFIG_DWC_MIPI_TC_DPHY_GEN3)
	dw_csi_create_capabilities_sysfs(pdev);
#endif
	dw_mipi_csi_get_version(csi);
	dw_mipi_csi_specific_mappings(csi);
#ifdef OYX_MOD
	void __iomem *base = ioremap(0x51828000, 0x500);
	writel(0xffffffff, base + 0x200);
	writel(0x3ff, base + 0x424);
    udelay(20000);

	writel(0x7, base + 0x470);
	writel(0x1, base + 0x474);
	writel(0x1, base + 0x478);
	writel(0x1, base + 0x47c);
	writel(0x1, base + 0x480);

    udelay(20000);

	writel(0x80000020, base + 0x184);
	writel(0xc0000020, base + 0x188);
	writel(0x80000020, base + 0x18c);
	writel(0x80000020, base + 0x190);
	writel(0x80000100, base + 0x194);
	writel(0x80000100, base + 0x198);
	writel(0x80000100, base + 0x19c);
	writel(0x80000100, base + 0x1a0);
	writel(0x80000100, base + 0x1a4);
	writel(0x80000100, base + 0x1a8);
	writel(0x3, base + 0x1ac);

	void __iomem *base1 = ioremap(0x51030000, 0x100);
	writel(0xffffffff, base1 + 0x40);
    udelay(20000);
#endif
	dw_mipi_csi_mask_irq_power_off(csi);
	dw_mipi_csi_fill_timings(csi);

	printk("DW MIPI CSI-2 Host registered successfully HW v%u.%u\n",
		 csi->hw_version_major, csi->hw_version_minor);

#ifdef DWC_PHY_USING
	ret = phy_init(csi->phy);
	if (ret) {
		dev_err(&csi->phy->dev, "phy init failed --> %d\n", ret);
		goto end;
	}
#endif

	ret = v4l2_async_register_subdev(sd);
	if (ret < 0) {
		dev_err(dev, "failed to register subdev\n");
		goto end;
	}

	return 0;

end:
#if IS_ENABLED(CONFIG_OF)
	media_entity_cleanup(&csi->sd.entity);
	return ret;
#endif
	//v4l2_device_unregister(csi->vdev.v4l2_dev);
	return ret;
}

static int dw_csi_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct dw_csi *mipi_csi = sd_to_mipi_csi_dev(sd);

	if (mipi_csi->rst)
		reset_control_assert(mipi_csi->rst);

    v4l2_async_unregister_subdev(sd);

#if IS_ENABLED(CONFIG_OF)
	media_entity_cleanup(&mipi_csi->sd.entity);
#else
	v4l2_device_unregister(mipi_csi->vdev.v4l2_dev);
#endif
	dev_info(&pdev->dev, "DW MIPI CSI-2 Host module removed\n");

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id dw_mipi_csi_of_match[] = {
	{ .compatible = "snps,dw-csi" },
	{},
};

MODULE_DEVICE_TABLE(of, dw_mipi_csi_of_match);
#endif

static struct platform_driver dw_mipi_csi_driver = {
	.remove = dw_csi_remove,
	.probe = dw_csi_probe,
	.driver = {
		.name = "dw-csi",
		.owner = THIS_MODULE,
#if IS_ENABLED(CONFIG_OF)
		.of_match_table = of_match_ptr(dw_mipi_csi_of_match),
#endif
	},
};

module_platform_driver(dw_mipi_csi_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Luis Oliveira <luis.oliveira@synopsys.com>");
MODULE_DESCRIPTION("Synopsys DesignWare MIPI CSI-2 Host Platform driver");
