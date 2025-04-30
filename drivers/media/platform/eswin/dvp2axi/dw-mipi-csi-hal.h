// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI dw-mipi-csi-hal driver
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Eswin VI team
 */

#ifndef _DW_MIPI_CSI_HAL_
#define _DW_MIPI_CSI_HAL_
// #include "sys_port.h"
#include "../vi_top/vitop.h"
#include <media/eswin/common-def.h>

#define MIPI_CSI_REG_ADDR_0 VI_CSI2_CTRL0_REGISTER_BASE_ADDRESS
#define MIPI_CSI_REG_SIZE_0 0x00010000UL
#define MIPI_CSI_IRQ_NUM_0 VI_CSI2_CTRL0_IRQ_NUM
#define MIPI_CSI_IRQ_NUM_1 VI_CSI2_CTRL1_IRQ_NUM
#define MIPI_CSI_IRQ_NUM_2 VI_CSI2_CTRL2_IRQ_NUM
#define MIPI_CSI_IRQ_NUM_3 VI_CSI2_CTRL3_IRQ_NUM
#define MIPI_CSI_IRQ_NUM_4 VI_CSI2_CTRL4_IRQ_NUM
#define MIPI_CSI_IRQ_NUM_5 VI_CSI2_CTRL5_IRQ_NUM

/* MIPI CSI2 REGISTERS OFFSET */
#define VERSION 0x00
#define N_LANES 0x04
#define CSI2_RESETN 0x08
#define INT_ST_MAIN 0x0c
// #define DATA_IDS_1 0x10
// #define DATA_IDS_2 0x14

#define PHY_CFG 0x18
#define PHY_MODE 0x1c

#define INT_ST_AP_MAIN 0x2c
// #define DATA_IDS_VC_1 0x30
// #define DATA_IDS_VC_2 0x34

#define PHY_SHUTDOWNZ 0x40
#define DPHY_RSTZ 0x44
// #define PHY_RX 0x48
// #define PHY_STOPSTATE 0x4c
#define PHY_TEST_CTRL0 0x50
#define PHY_TEST_CTRL1 0x54
#define PHY2_TEST_CTRL0 0x58
#define PHY2_TEST_CTRL1 0x5c

#define PPI_PG_PATTERN_VRES 0x60
#define PPI_PG_PATTERN_HRES 0x64
#define PPI_PG_CONFIG 0x68
#define PPI_PG_ENABLE 0x6c
#define PPI_PG_STATUS 0x70

#define IPI_MODE 0x80
#define IPI_VCID 0x84
#define IPI_DATA_TYPE 0x88
#define IPI_MEM_FLUSH 0x8c
#define IPI_HSA_TIME 0x90
#define IPI_HBP_TIME 0x94
#define IPI_HSD_TIME 0x98
#define IPI_HLINE_TIME 0x9c
#define IPI_SOFTRSTN 0xa0
#define IPI_ADV_FEATURES 0xac
#define IPI_VSA_LINES 0xb0
#define IPI_VBP_LINES 0xb4
#define IPI_VFP_LINES 0xb8
#define IPI_VACTIVE_LINES 0xbc
// #define VC_EXTENSION 0xc8
// #define PHY_CAL 0xcc

#define IPI2_MODE 0x200
#define IPI2_VCID 0x204
#define IPI2_DATA_TYPE 0x208
#define IPI2_MEM_FLUSH 0x20c
#define IPI2_HSA_TIME 0x210
#define IPI2_HBP_TIME 0x214
#define IPI2_HSD_TIME 0x218
#define IPI2_ADV_FEATURES 0x21c

#define IPI3_MODE 0x220
#define IPI3_VCID 0x224
#define IPI3_DATA_TYPE 0x228
#define IPI3_MEM_FLUSH 0x22c
#define IPI3_HSA_TIME 0x230
#define IPI3_HBP_TIME 0x234
#define IPI3_HSD_TIME 0x238
#define IPI3_ADV_FEATURES 0x23c

#define LINE_EVENT_SELECTION_BIT (1 << 16)
#define EN_VIDEO_BIT (1 << 17)
#define EN_LINE_START_BIT (1 << 18)
#define EN_NULL_BIT (1 << 19)
#define EN_BLANKING_BIT (1 << 20)
#define EN_EMBEDDED_BIT (1 << 21)

#define LINE_ALL_EVENT_SELECT (EN_EMBEDDED_BIT | EN_BLANKING_BIT | EN_NULL_BIT |    \
                            EN_LINE_START_BIT | EN_VIDEO_BIT | LINE_EVENT_SELECTION_BIT)

// #define MIPI_CSI_PPI_PATTERN
// #define MIPI_CSI_PPI_PATTERN_CAMERA_TIMING

enum {
    D_PHY_PPI_8 = 0,
    D_PHY_PPI_16 = 1
};

enum {
	CAMERA_TIMING = 0,
	CONTROLLER_TIMING = 1
};

enum {
    IPI_DATA_48_BIT = 0,
    IPI_DATA_16_BIT = 1
};

/* IPI cut through */
enum cut_through {
	IPI_CTINACTIVE = 0,
	IPI_CTACTIVE = 1
};

enum {
	D_PHY_MODE = 0,
	C_PHY_MODE = 1
};

/* IPI Data Types */
enum ipi_data_type {
	CSI_2_YUV420_8 = 0x18,
	CSI_2_YUV420_10 = 0x19,
	CSI_2_YUV420_8_LEG = 0x1A,
	CSI_2_YUV420_8_SHIFT = 0x1C,
	CSI_2_YUV420_10_SHIFT = 0x1D,
	CSI_2_YUV422_8 = 0x1E,
	CSI_2_YUV422_10 = 0x1F,
	CSI_2_RGB444 = 0x20,
	CSI_2_RGB555 = 0x21,
	CSI_2_RGB565 = 0x22,
	CSI_2_RGB666 = 0x23,
	CSI_2_RGB888 = 0x24,
	CSI_2_RAW6 = 0x28,
	CSI_2_RAW7 = 0x29,
	CSI_2_RAW8 = 0x2A,
	CSI_2_RAW10 = 0x2B,
	CSI_2_RAW12 = 0x2C,
	CSI_2_RAW14 = 0x2D,
	CSI_2_RAW16 = 0x2E,
	CSI_2_RAW20 = 0x2F,
	USER_DEFINED_1 = 0x30,
	USER_DEFINED_2 = 0x31,
	USER_DEFINED_3 = 0x32,
	USER_DEFINED_4 = 0x33,
	USER_DEFINED_5 = 0x34,
	USER_DEFINED_6 = 0x35,
	USER_DEFINED_7 = 0x36,
	USER_DEFINED_8 = 0x37,
};

// #ifdef MIPI_CSI_PPI_PATTERN
struct ppi_pattern_info {
    u32 ppi_patn_vres:16;
    u32 ppi_patn_hres:16;

    u32 phy_mode:1;
    u32 ppi_patn_dphy_vc:4;
    u32 ppi_patn_cphy_vc:5;

    u32 ppi_patn_datatype:6;

    u32 ppi_pg_pattern:1;
};
// #endif
struct control_timing {
    u32 hsa;
	u32 hbp;
	u32 hsd;
	u32 htotal;

    u32 vsa;
	u32 vbp;
	u32 vfp;
	u32 vactive;
};

struct csi_data {
    u32 num_lanes:4;
    u32 ppi_width:2;
	u32 phy_mode:1;

    u32 vc:5;
    u32 dt:6;	
	u32 emb:1;
	u32 frame_det:1;

    u32 ipi_mode:1;
    u32 ipi_color_com:1;
    u32 ipi_auto_flush:1;
    u32 ipi_cut_through:1;
    u32 ipi_line_event;

    u32 hsa;
	u32 hbp;
	u32 hsd;
	u32 htotal;

    u32 vsa;
	u32 vbp;
	u32 vfp;
	u32 vactive;
};

struct dw_csi {
	void volatile *base_address;

    struct csi_data hw;
};

// void mipi_csi_init(uint32_t controller_id);

#endif
