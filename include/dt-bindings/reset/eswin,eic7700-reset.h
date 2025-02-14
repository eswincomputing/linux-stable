/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 * Device Tree binding constants for EIC7700 reset controller.
 *
 * Authors: huangyifeng<huangyifeng@eswincomputing.com>
 */

#ifndef __DT_ESWIN_EIC7700_RESET_H__
#define __DT_ESWIN_EIC7700_RESET_H__

#define SNOC_RST_CTRL 0X00
#define GPU_RST_CTRL 0X01
#define DSP_RST_CTRL 0X02
#define D2D_RST_CTRL 0X03
#define DDR_RST_CTRL 0X04
#define TCU_RST_CTRL 0X05
#define NPU_RST_CTRL 0X06
#define HSPDMA_RST_CTRL 0X07
#define PCIE_RST_CTRL 0X08
#define I2C_RST_CTRL 0X09
#define FAN_RST_CTRL 0X0A
#define PVT_RST_CTRL 0X0B
#define MBOX_RST_CTRL 0X0C
#define UART_RST_CTRL 0X0D
#define GPIO_RST_CTRL 0X0E
#define TIMER_RST_CTRL 0X0F
#define SSI_RST_CTRL 0X10
#define WDT_RST_CTRL 0X11
#define LSP_CFGRST_CTRL 0X12
#define U84_RST_CTRL 0X13
#define SCPU_RST_CTRL 0X14
#define LPCPU_RST_CTRL 0X15
#define VC_RST_CTRL 0X16
#define JD_RST_CTRL 0X17
#define JE_RST_CTRL 0X18
#define VD_RST_CTRL 0X19
#define VE_RST_CTRL 0X1A
#define G2D_RST_CTRL 0X1B
#define VI_RST_CTRL 0X1C
#define DVP_RST_CTRL 0X1D
#define ISP0_RST_CTRL 0X1E
#define ISP1_RST_CTRL 0X1F
#define SHUTTER_RST_CTRL 0X20
#define VO_PHYRST_CTRL 0X21
#define VO_I2SRST_CTRL 0X22
#define VO_RST_CTRL 0X23
#define BOOTSPI_RST_CTRL 0X24
#define I2C1_RST_CTRL 0X25
#define I2C0_RST_CTRL 0X26
#define DMA1_RST_CTRL 0X27
#define FPRT_RST_CTRL 0X28
#define HBLOCK_RST_CTRL 0X29
#define SECSR_RST_CTRL 0X2A
#define OTP_RST_CTRL 0X2B
#define PKA_RST_CTRL 0X2C
#define SPACC_RST_CTRL 0X2D
#define TRNG_RST_CTRL 0X2E
#define RESERVED 0X2F
#define TIMER0_RST_CTRL 0X30
#define TIMER1_RST_CTRL 0X31
#define TIMER2_RST_CTRL 0X32
#define TIMER3_RST_CTRL 0X33
#define RTC_RST_CTRL 0X34
#define MNOC_RST_CTRL 0X35
#define RNOC_RST_CTRL 0X36
#define CNOC_RST_CTRL 0X37
#define LNOC_RST_CTRL 0X38

/*
 * CONSUMER RESET CONTROL BIT
 */
/*SNOC*/
#define SW_NOC_NSP_RSTN (1 << 0)
#define SW_NOC_CFG_RSTN (1 << 1)
#define SW_RNOC_NSP_RSTN (1 << 2)
#define SW_SNOC_TCU_ARSTN (1 << 3)
#define SW_SNOC_U84_ARSTN (1 << 4)
#define SW_SNOC_PCIET_XSRSTN (1 << 5)
#define SW_SNOC_PCIET_XMRSTN (1 << 6)
#define SW_SNOC_PCIET_PRSTN (1 << 7)
#define SW_SNOC_NPU_ARSTN (1 << 8)
#define SW_SNOC_JTAG_ARSTN (1 << 9)
#define SW_SNOC_DSPT_ARSTN (1 << 10)
#define SW_SNOC_DDRC1_P2_ARSTN (1 << 11)
#define SW_SNOC_DDRC1_P1_ARSTN (1 << 12)
#define SW_SNOC_DDRC0_P2_ARSTN (1 << 13)
#define SW_SNOC_DDRC0_P1_ARSTN (1 << 14)
#define SW_SNOC_D2D_ARSTN (1 << 15)
#define SW_SNOC_AON_ARSTN (1 << 16)

/*GPU*/
#define SW_GPU_AXI_RSTN (1 << 0)
#define SW_GPU_CFG_RSTN (1 << 1)
#define SW_GPU_GRAY_RSTN (1 << 2)
#define SW_GPU_JONES_RSTN (1 << 3)
#define SW_GPU_SPU_RSTN (1 << 4)

/*DSP*/
#define SW_DSP_AXI_RSTN (1 << 0)
#define SW_DSP_CFG_RSTN (1 << 1)
#define SW_DSP_DIV4_RSTN (1 << 2)
#define SW_DSP_DIV_RSTN_0 (1 << 4)
#define SW_DSP_DIV_RSTN_1 (1 << 5)
#define SW_DSP_DIV_RSTN_2 (1 << 6)
#define SW_DSP_DIV_RSTN_3 (1 << 7)

/*D2D*/
#define SW_D2D_AXI_RSTN (1 << 0)
#define SW_D2D_CFG_RSTN (1 << 1)
#define SW_D2D_PRST_N (1 << 2)
#define SW_D2D_RAW_PCS_RST_N (1 << 4)
#define SW_D2D_RX_RST_N (1 << 5)
#define SW_D2D_TX_RST_N (1 << 6)
#define SW_D2D_CORE_RST_N (1 << 7)

/*TCU*/
#define SW_TCU_AXI_RSTN (1 << 0)
#define SW_TCU_CFG_RSTN (1 << 1)
#define TBU_RSTN_0 (1 << 4)
#define TBU_RSTN_1 (1 << 5)
#define TBU_RSTN_2 (1 << 6)
#define TBU_RSTN_3 (1 << 7)
#define TBU_RSTN_4 (1 << 8)
#define TBU_RSTN_5 (1 << 9)
#define TBU_RSTN_6 (1 << 10)
#define TBU_RSTN_7 (1 << 11)
#define TBU_RSTN_8 (1 << 12)
#define TBU_RSTN_9 (1 << 13)
#define TBU_RSTN_10 (1 << 14)
#define TBU_RSTN_11 (1 << 15)
#define TBU_RSTN_12 (1 << 16)
#define TBU_RSTN_13 (1 << 17)
#define TBU_RSTN_14 (1 << 18)
#define TBU_RSTN_15 (1 << 19)
#define TBU_RSTN_16 (1 << 20)

/*NPU*/
#define SW_NPU_AXI_RSTN (1 << 0)
#define SW_NPU_CFG_RSTN (1 << 1)
#define SW_NPU_CORE_RSTN (1 << 2)
#define SW_NPU_E31CORE_RSTN (1 << 3)
#define SW_NPU_E31BUS_RSTN (1 << 4)
#define SW_NPU_E31DBG_RSTN (1 << 5)
#define SW_NPU_LLC_RSTN (1 << 6)

/*HSP DMA*/
#define SW_HSP_AXI_RSTN (1 << 0)
#define SW_HSP_CFG_RSTN (1 << 1)
#define SW_HSP_POR_RSTN (1 << 2)
#define SW_MSHC0_PHY_RSTN (1 << 3)
#define SW_MSHC1_PHY_RSTN (1 << 4)
#define SW_MSHC2_PHY_RSTN (1 << 5)
#define SW_MSHC0_TXRX_RSTN (1 << 6)
#define SW_MSHC1_TXRX_RSTN (1 << 7)
#define SW_MSHC2_TXRX_RSTN (1 << 8)
#define SW_SATA_ASIC0_RSTN (1 << 9)
#define SW_SATA_OOB_RSTN (1 << 10)
#define SW_SATA_PMALIVE_RSTN (1 << 11)
#define SW_SATA_RBC_RSTN (1 << 12)
#define SW_DMA0_RST_N (1 << 13)
#define SW_HSP_DMA0_RSTN (1 << 14)
#define SW_USB0_VAUX_RSTN (1 << 15)
#define SW_USB1_VAUX_RSTN (1 << 16)
#define SW_HSP_SD1_PRSTN (1 << 17)
#define SW_HSP_SD0_PRSTN (1 << 18)
#define SW_HSP_EMMC_PRSTN (1 << 19)
#define SW_HSP_DMA_PRSTN (1 << 20)
#define SW_HSP_SD1_ARSTN (1 << 21)
#define SW_HSP_SD0_ARSTN (1 << 22)
#define SW_HSP_EMMC_ARSTN (1 << 23)
#define SW_HSP_DMA_ARSTN (1 << 24)
#define SW_HSP_ETH1_ARSTN (1 << 25)
#define SW_HSP_ETH0_ARSTN (1 << 26)
#define SW_HSP_SATA_ARSTN (1 << 27)

/*PCIE*/
#define SW_PCIE_CFG_RSTN (1 << 0)
#define SW_PCIE_POWERUP_RSTN (1 << 1)
#define SW_PCIE_PERST_N (1 << 2)

/*I2C*/
#define SW_I2C_RST_N_0 (1 << 0)
#define SW_I2C_RST_N_1 (1 << 1)
#define SW_I2C_RST_N_2 (1 << 2)
#define SW_I2C_RST_N_3 (1 << 3)
#define SW_I2C_RST_N_4 (1 << 4)
#define SW_I2C_RST_N_5 (1 << 5)
#define SW_I2C_RST_N_6 (1 << 6)
#define SW_I2C_RST_N_7 (1 << 7)
#define SW_I2C_RST_N_8 (1 << 8)
#define SW_I2C_RST_N_9 (1 << 9)

/*FAN*/
#define SW_FAN_RST_N (1 << 0)

/*PVT*/
#define SW_PVT_RST_N_0 (1 << 0)
#define SW_PVT_RST_N_1 (1 << 1)

/*MBOX*/
#define SW_MBOX_RST_N_0 (1 << 0)
#define SW_MBOX_RST_N_1 (1 << 1)
#define SW_MBOX_RST_N_2 (1 << 2)
#define SW_MBOX_RST_N_3 (1 << 3)
#define SW_MBOX_RST_N_4 (1 << 4)
#define SW_MBOX_RST_N_5 (1 << 5)
#define SW_MBOX_RST_N_6 (1 << 6)
#define SW_MBOX_RST_N_7 (1 << 7)
#define SW_MBOX_RST_N_8 (1 << 8)
#define SW_MBOX_RST_N_9 (1 << 9)
#define SW_MBOX_RST_N_10 (1 << 10)
#define SW_MBOX_RST_N_11 (1 << 11)
#define SW_MBOX_RST_N_12 (1 << 12)
#define SW_MBOX_RST_N_13 (1 << 13)
#define SW_MBOX_RST_N_14 (1 << 14)
#define SW_MBOX_RST_N_15 (1 << 15)

/*UART*/
#define SW_UART_RST_N_0 (1 << 0)
#define SW_UART_RST_N_1 (1 << 1)
#define SW_UART_RST_N_2 (1 << 2)
#define SW_UART_RST_N_3 (1 << 3)
#define SW_UART_RST_N_4 (1 << 4)

/*GPIO*/
#define SW_GPIO_RST_N_0 (1 << 0)
#define SW_GPIO_RST_N_1 (1 << 1)

/*TIMER*/
#define SW_TIMER_RST_N (1 << 0)

/*SSI*/
#define SW_SSI_RST_N_0 (1 << 0)
#define SW_SSI_RST_N_1 (1 << 1)

/*WDT*/
#define SW_WDT_RST_N_0 (1 << 0)
#define SW_WDT_RST_N_1 (1 << 1)
#define SW_WDT_RST_N_2 (1 << 2)
#define SW_WDT_RST_N_3 (1 << 3)

/*LSP CFG*/
#define SW_LSP_CFG_RSTN (1 << 0)

/*U84 CFG*/
#define SW_U84_CORE_RSTN_0 (1 << 0)
#define SW_U84_CORE_RSTN_1 (1 << 1)
#define SW_U84_CORE_RSTN_2 (1 << 2)
#define SW_U84_CORE_RSTN_3 (1 << 3)
#define SW_U84_BUS_RSTN (1 << 4)
#define SW_U84_DBG_RSTN (1 << 5)
#define SW_U84_TRACECOM_RSTN (1 << 6)
#define SW_U84_TRACE_RSTN_0 (1 << 8)
#define SW_U84_TRACE_RSTN_1 (1 << 9)
#define SW_U84_TRACE_RSTN_2 (1 << 10)
#define SW_U84_TRACE_RSTN_3 (1 << 11)

/*SCPU*/
#define SW_SCPU_CORE_RSTN (1 << 0)
#define SW_SCPU_BUS_RSTN (1 << 1)
#define SW_SCPU_DBG_RSTN (1 << 2)

/*LPCPU*/
#define SW_LPCPU_CORE_RSTN (1 << 0)
#define SW_LPCPU_BUS_RSTN (1 << 1)
#define SW_LPCPU_DBG_RSTN (1 << 2)

/*VC*/
#define SW_VC_CFG_RSTN (1 << 0)
#define SW_VC_AXI_RSTN (1 << 1)
#define SW_VC_MONCFG_RSTN (1 << 2)

/*JD*/
#define SW_JD_CFG_RSTN (1 << 0)
#define SW_JD_AXI_RSTN (1 << 1)

/*JE*/
#define SW_JE_CFG_RSTN (1 << 0)
#define SW_JE_AXI_RSTN (1 << 1)

/*VD*/
#define SW_VD_CFG_RSTN (1 << 0)
#define SW_VD_AXI_RSTN (1 << 1)

/*VE*/
#define SW_VE_AXI_RSTN (1 << 0)
#define SW_VE_CFG_RSTN (1 << 1)

/*G2D*/
#define SW_G2D_CORE_RSTN (1 << 0)
#define SW_G2D_CFG_RSTN (1 << 1)
#define SW_G2D_AXI_RSTN (1 << 2)

/*VI*/
#define SW_VI_AXI_RSTN (1 << 0)
#define SW_VI_CFG_RSTN (1 << 1)
#define SW_VI_DWE_RSTN (1 << 2)

/*DVP*/
#define SW_VI_DVP_RSTN (1 << 0)

/*ISP0*/
#define SW_VI_ISP0_RSTN (1 << 0)

/*ISP1*/
#define SW_VI_ISP1_RSTN (1 << 0)

/*SHUTTR*/
#define SW_VI_SHUTTER_RSTN_0 (1 << 0)
#define SW_VI_SHUTTER_RSTN_1 (1 << 1)
#define SW_VI_SHUTTER_RSTN_2 (1 << 2)
#define SW_VI_SHUTTER_RSTN_3 (1 << 3)
#define SW_VI_SHUTTER_RSTN_4 (1 << 4)
#define SW_VI_SHUTTER_RSTN_5 (1 << 5)

/*VO PHY*/
#define SW_VO_MIPI_PRSTN (1 << 0)
#define SW_VO_PRSTN (1 << 1)
#define SW_VO_HDMI_PRSTN (1 << 3)
#define SW_HDMI_PHYCTRL_RSTN (1 << 4)
#define SW_VO_HDMI_RSTN (1 << 5)

/*VO I2S*/
#define SW_VO_I2S_RSTN (1 << 0)
#define SW_VO_I2S_PRSTN (1 << 1)

/*VO*/
#define SW_VO_AXI_RSTN (1 << 0)
#define SW_VO_CFG_RSTN (1 << 1)
#define SW_VO_DC_RSTN (1 << 2)
#define SW_VO_DC_PRSTN (1 << 3)

/*BOOTSPI*/
#define SW_BOOTSPI_HRSTN (1 << 0)
#define SW_BOOTSPI_RSTN (1 << 1)

/*I2C1*/
#define SW_I2C1_PRSTN (1 << 0)

/*I2C0*/
#define SW_I2C0_PRSTN (1 << 0)

/*DMA1*/
#define SW_DMA1_ARSTN (1 << 0)
#define SW_DMA1_HRSTN (1 << 1)

/*FPRT*/
#define SW_FP_PRT_HRSTN (1 << 0)

/*HBLOCK*/
#define SW_HBLOCK_HRSTN (1 << 0)

/*SECSR*/
#define SW_SECSR_HRSTN (1 << 0)

/*OTP*/
#define SW_OTP_PRSTN (1 << 0)

/*PKA*/
#define SW_PKA_HRSTN (1 << 0)

/*SPACC*/
#define SW_SPACC_RSTN (1 << 0)

/*TRNG*/
#define SW_TRNG_HRSTN (1 << 0)

/*TIMER0*/
#define SW_TIMER0_RSTN_0 (1 << 0)
#define SW_TIMER0_RSTN_1 (1 << 1)
#define SW_TIMER0_RSTN_2 (1 << 2)
#define SW_TIMER0_RSTN_3 (1 << 3)
#define SW_TIMER0_RSTN_4 (1 << 4)
#define SW_TIMER0_RSTN_5 (1 << 5)
#define SW_TIMER0_RSTN_6 (1 << 6)
#define SW_TIMER0_RSTN_7 (1 << 7)
#define SW_TIMER0_PRSTN (1 << 8)

/*TIMER1*/
#define SW_TIMER1_RSTN_0 (1 << 0)
#define SW_TIMER1_RSTN_1 (1 << 1)
#define SW_TIMER1_RSTN_2 (1 << 2)
#define SW_TIMER1_RSTN_3 (1 << 3)
#define SW_TIMER1_RSTN_4 (1 << 4)
#define SW_TIMER1_RSTN_5 (1 << 5)
#define SW_TIMER1_RSTN_6 (1 << 6)
#define SW_TIMER1_RSTN_7 (1 << 7)
#define SW_TIMER1_PRSTN (1 << 8)

/*TIMER2*/
#define SW_TIMER2_RSTN_0 (1 << 0)
#define SW_TIMER2_RSTN_1 (1 << 1)
#define SW_TIMER2_RSTN_2 (1 << 2)
#define SW_TIMER2_RSTN_3 (1 << 3)
#define SW_TIMER2_RSTN_4 (1 << 4)
#define SW_TIMER2_RSTN_5 (1 << 5)
#define SW_TIMER2_RSTN_6 (1 << 6)
#define SW_TIMER2_RSTN_7 (1 << 7)
#define SW_TIMER2_PRSTN (1 << 8)

/*TIMER3*/
#define SW_TIMER3_RSTN_0 (1 << 0)
#define SW_TIMER3_RSTN_1 (1 << 1)
#define SW_TIMER3_RSTN_2 (1 << 2)
#define SW_TIMER3_RSTN_3 (1 << 3)
#define SW_TIMER3_RSTN_4 (1 << 4)
#define SW_TIMER3_RSTN_5 (1 << 5)
#define SW_TIMER3_RSTN_6 (1 << 6)
#define SW_TIMER3_RSTN_7 (1 << 7)
#define SW_TIMER3_PRSTN (1 << 8)

/*RTC*/
#define SW_RTC_RSTN (1 << 0)

/*MNOC*/
#define SW_MNOC_SNOC_NSP_RSTN (1 << 0)
#define SW_MNOC_VC_ARSTN (1 << 1)
#define SW_MNOC_CFG_RSTN (1 << 2)
#define SW_MNOC_HSP_ARSTN (1 << 3)
#define SW_MNOC_GPU_ARSTN (1 << 4)
#define SW_MNOC_DDRC1_P3_ARSTN (1 << 5)
#define SW_MNOC_DDRC0_P3_ARSTN (1 << 6)

/*RNOC*/
#define SW_RNOC_VO_ARSTN (1 << 0)
#define SW_RNOC_VI_ARSTN (1 << 1)
#define SW_RNOC_SNOC_NSP_RSTN (1 << 2)
#define SW_RNOC_CFG_RSTN (1 << 3)
#define SW_MNOC_DDRC1_P4_ARSTN (1 << 4)
#define SW_MNOC_DDRC0_P4_ARSTN (1 << 5)

/*CNOC*/
#define SW_CNOC_VO_CFG_RSTN (1 << 0)
#define SW_CNOC_VI_CFG_RSTN (1 << 1)
#define SW_CNOC_VC_CFG_RSTN (1 << 2)
#define SW_CNOC_TCU_CFG_RSTN (1 << 3)
#define SW_CNOC_PCIET_CFG_RSTN (1 << 4)
#define SW_CNOC_NPU_CFG_RSTN (1 << 5)
#define SW_CNOC_LSP_CFG_RSTN (1 << 6)
#define SW_CNOC_HSP_CFG_RSTN (1 << 7)
#define SW_CNOC_GPU_CFG_RSTN (1 << 8)
#define SW_CNOC_DSPT_CFG_RSTN (1 << 9)
#define SW_CNOC_DDRT1_CFG_RSTN (1 << 10)
#define SW_CNOC_DDRT0_CFG_RSTN (1 << 11)
#define SW_CNOC_D2D_CFG_RSTN (1 << 12)
#define SW_CNOC_CFG_RSTN (1 << 13)
#define SW_CNOC_CLMM_CFG_RSTN (1 << 14)
#define SW_CNOC_AON_CFG_RSTN (1 << 15)

/*LNOC*/
#define SW_LNOC_CFG_RSTN (1 << 0)
#define SW_LNOC_NPU_LLC_ARSTN (1 << 1)
#define SW_LNOC_DDRC1_P0_ARSTN (1 << 2)
#define SW_LNOC_DDRC0_P0_ARSTN (1 << 3)

#endif /*endif __DT_ESWIN_EIC7700_RESET_H__*/
