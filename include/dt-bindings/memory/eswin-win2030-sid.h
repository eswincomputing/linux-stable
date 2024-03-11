// SPDX-License-Identifier: GPL-2.0
/*
 * For Eswin EIC7700 SoC, define Stream ID of the devices to identify devices by SMMU, and TBU ID of the devices.
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
 */

#ifndef DT_BINDINGS_MEMORY_WIN2030_SID_H
#define DT_BINDINGS_MEMORY_WIN2030_SID_H

#define WIN2030_SID_DMA0	1

#define WIN2030_SID_JDEC	2

#define WIN2030_SID_JENC	3

/* NPU DMA*/
#define WIN2030_SID_NPU_DMA	4

/* NPU-E31 */
#define WIN2030_SID_NPU_E31	5

/* Video In */
#define WIN2030_SID_ISP0	6

#define WIN2030_SID_ISP1	WIN2030_SID_ISP0

#define WIN2030_SID_DW		8

#define WIN2030_SID_DVP		9

/* High Speed */
#define WIN2030_SID_USB0	10

#define WIN2030_SID_USB1	11

#define WIN2030_SID_ETH0	12

#define WIN2030_SID_ETH1	13

#define WIN2030_SID_SATA	14

#define WIN2030_SID_EMMC0	15

#define WIN2030_SID_SD0		16

#define WIN2030_SID_SD1		17


/* DSP */
#define WIN2030_SID_DSP_0	18
#define WIN2030_SID_DSP_1	19
#define WIN2030_SID_DSP_2	20
#define WIN2030_SID_DSP_3	21

/* CODEC */
#define WIN2030_SID_VDEC	WIN2030_SID_JDEC

#define WIN2030_SID_VENC	WIN2030_SID_JENC

/*** AON subsystem ***/
/* Secure CPU */
#define WIN2030_SID_SCPU	24
#define SCPU_SID_REG_OFFSET     0x1004

/* Low power CPU */
#define WIN2030_SID_LCPU	25
#define LCPU_SID_REG_OFFSET	0x2004

/* Always on, DMA1 */
#define WIN2030_SID_DMA1	26
#define DMA1_SID_REG_OFFSET	0x3004

/* crypt */
#define WIN2030_SID_CRYPT	WIN2030_SID_SCPU
#define CRYPT_SID_REG_OFFSET	0x4004

/*** for iova mapping test ***/
#define WIN2030_SID_DEV_FOO_A		28
#define WIN2030_SID_DEV_FOO_B		29
#define WIN2030_SID_DEV_FOO_FOR_DIE1	30


/*** tbu id ***/
/* tbu_id: bit[3:0] is for major, bit[7:4] is for minor; 
	For example, tbu of dsp3 is tbu7_3, the bu 0x73. It measn tbu7_3
*/
#define	WIN2030_TBUID_0x0	0x0

#define	WIN2030_TBUID_0x10	0x10
#define	WIN2030_TBUID_0x11	0x11
#define	WIN2030_TBUID_0x12	0x12
#define	WIN2030_TBUID_0x13	0x13

#define	WIN2030_TBUID_0x2	0x2

#define	WIN2030_TBUID_0x3	0x3

#define	WIN2030_TBUID_0x4	0x4

#define	WIN2030_TBUID_0x5	0x5

#define	WIN2030_TBUID_0x70	0x70
#define	WIN2030_TBUID_0x71	0x71
#define	WIN2030_TBUID_0x72	0x72
#define	WIN2030_TBUID_0x73	0x73

#define	WIN2030_TBUID_0xF00	0xF00	// simulation for WIN2030_SID_DEV_FOO_A/B, No real tbu attached infact


/* For better use by devices in dts, create tbu alias for devices*/
#define WIN2030_TBUID_ISP	WIN2030_TBUID_0x0
#define WIN2030_TBUID_DW	WIN2030_TBUID_ISP

#define WIN2030_TBUID_VDEC	WIN2030_TBUID_0x10
#define WIN2030_TBUID_VENC	WIN2030_TBUID_0x11
#define WIN2030_TBUID_JENC	WIN2030_TBUID_0x12
#define WIN2030_TBUID_JDEC	WIN2030_TBUID_0x13

//high speed modules share the same tbu2
#define WIN2030_TBUID_DMA0	WIN2030_TBUID_0x2
#define WIN2030_TBUID_USB	WIN2030_TBUID_DMA0
#define WIN2030_TBUID_ETH	WIN2030_TBUID_DMA0
#define WIN2030_TBUID_SATA	WIN2030_TBUID_DMA0
#define WIN2030_TBUID_EMMC	WIN2030_TBUID_DMA0
#define WIN2030_TBUID_SD	WIN2030_TBUID_DMA0

#define WIN2030_TBUID_PCIE	WIN2030_TBUID_0x3

//scpu, crypto, lpcpu, dma1 share the same tbu4
#define WIN2030_TBUID_SCPU	WIN2030_TBUID_0x4
#define WIN2030_TBUID_CRYPT	WIN2030_TBUID_SCPU
#define WIN2030_TBUID_DMA1	WIN2030_TBUID_SCPU
#define WIN2030_TBUID_LPCPU	WIN2030_TBUID_SCPU

//npu
#define WIN2030_TBUID_NPU	WIN2030_TBUID_0x5

//dsp
#define WIN2030_TBUID_DSP0	WIN2030_TBUID_0x70
#define WIN2030_TBUID_DSP1	WIN2030_TBUID_0x71
#define WIN2030_TBUID_DSP2	WIN2030_TBUID_0x72
#define WIN2030_TBUID_DSP3	WIN2030_TBUID_0x73







#endif
