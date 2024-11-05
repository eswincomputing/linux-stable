// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
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
 *
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

#ifndef __CDMA_REGS_H__
#define __CDMA_REGS_H__

/**
 * @brief NPU configuration DMA related register offsets. Refer to
 * https://ekm.eswincomputing.com/preview.html?fileid=1267313
 *
 */

/**
 * @brief NPU configuration DMA major size register.
 * This is a 32 bits register. Total bits are divided equally into 4 groups and each 8 bits belong to a group. Each 8
 * bits map to a transfer size (In 4 bytes unit) of a given Master node. Total 4 groups correspond to Master node 0 ~ 3.
 *
 */
#define NPU_CDMA_MJLX_SZ_OFFSET 0x0

/**
 * @brief NPU configuration DMA major size register.
 * This is a 32 bits register. Total bits are divided equally into 4 groups and each 8 bits belong to a group. Each 8
 * bits map to a transfer size (In 4 bytes unit) of a given Master node. Total 4 groups correspond to Master node 4 ~ 7.
 *
 */
#define NPU_CDMA_MJHX_SZ_OFFSET 0x4

/**
 * @brief NPU configuration DMA miscellaneous configuration register.
 * See npu_cdma_misc_cfg0_t for a detailed explanation.
 *
 */
#define NPU_CDMA_MISC_CFG0_OFFSET 0x8

/**
 * @brief NPU configuration DMA source address register.
 * This is a 32 bits wide register corresponding to the 0~31 bits of source address. Source address should be aligned to
 * 4 bytes boundary.
 *
 */
#define NPU_CDMA_SRC_ADD_OFFSET 0xC

/**
 * @brief NPU configuration DMA miscellaneous configuration register.
 * See npu_cdma_misc_cfg1_t for a detailed explanation.
 *
 */
#define NPU_CDMA_MISC_CFG1_OFFSET 0x10

/**
 * @brief NPU configuration DMA miscellaneous control register.
 * See npu_cdma_misc_ctl_t for a detailed explanation.
 *
 */
#define NPU_CDMA_MISC_CTL_OFFSET 0x14

/**
 * @brief NPU configuration DMA miscellaneous control register.
 * .See npu_cdma_err_intr_clr_t for a detailed explanation.
 *
 */
#define NPU_CDMA_ERR_INT_CLEAR_OFFSET 0x1c

/**
 * @brief This is a bitmap. Each bit in 0~9 specifies if common information data should be sent to the given node if
 * this bit is 1.
 *
 */
#define NPU_CDMA_COM_BITMAP 0x28

/**
 * @brief NPU configuration DMA miscellaneous configuration register 0.
 *
 */
union npu_cdma_misc_cfg0_t {
    struct {
        /**
         * @brief Master node DMA transfer size (In 4 bytes unit).
         *
         */
        u32 master_size : 8;

        /**
         * @brief Auxiliary node DMA transfer size (In 4 bytes unit).
         *
         */
        u32 aux_size : 8;

        /**
         * @brief Major nodes destination address offset (In 4 bytes unit) relative to DTim start.
         *
         */
        u32 major_des_addr : 8;

        /**
         * @brief Auxiliary node destination address offset (In 4 bytes unit) relative to DTim start.
         *
         */
        u32 aux_des_addr : 8;
    };

    /**
     * @brief DWord register value.
     *
     */
    u32 dw;
};

/**
 * @brief NPU configuration DMA miscellaneous configuration register 1.
 *
 */
union npu_cdma_misc_cfg1_t {
    struct {
        /**
         * @brief  Corresponds to the 32~47 bits of source address.
         *
         */
        u32 src_addr_hi : 16;

        /**
         * @brief Common DMA transfer size (In 4 bytes unit).
         *
         */
        u32 com_size : 8;

        /**
         * @brief Master nodes destination address offset (In 4 bytes unit) relative to DTim start.
         *
         */
        u32 master_des_addr : 8;
    };

    /**
     * @brief DWord register value.
     *
     */
    u32 dw;
};

/**
 * @brief NPU configuration DMA miscellaneous control register.
 *
 */
union npu_cdma_misc_ctl_t {
    struct {
        /**
         * @brief Write this register with setting 1 to this bit triggers DMA transfer. Write this register with setting
         * 0 to this bit if DMA is outstanding is undefined behavior. Read this register and this bit reflects if there
         * is outstanding DMA.
         *
         */
        u32 dma_en : 1;

        /**
         * @brief Enable DMA completion interrupt.
         *
         */
        u32 int_en : 1;

        /**
         * @brief Indicates the status of last DMA transfer.
         *
         */
        u32 dma_status : 6;

        /**
         * @brief Specifies the AXI maximum burst length. This value should
         * satisfy 2 ** n - 1 where n is a natural number.
         *
         */
        u32 burst_len : 6;

        u32 : 2;

        /**
         * @brief Specifies the total length (In 4-bytes unit) of the data to be read from source address.
         *
         */
        u32 total_len : 16;
    };

    /**
     * @brief DWord register value.
     *
     */
    u32 dw;
};

union npu_cdma_err_intr_clr_t {
    struct {
        /**
         * @brief Normal interrupt clear(include success interrupt and total size zero interruput)
         *
         */
        u32 normal_clear : 1;

        /**
         * @brief Enable cfg error clear
         *
         */
        u32 dma_cfg_err_clear : 1;

        /**
         * @brief Enable axi slave error clear
         *
         */
        u32 dma_axi_slv_err_clear : 1;

        /**
         * @brief Enable axi decode error clear
         *
         */
        u32 dma_axi_dec_err_clear : 1;

        /**
         * @brief Enable the hardware automatically clears the interrupt and error status
         *
         */
        u32 hw_int_ctrl : 1;

        u32 : 11;

        /**
         * @brief Retry error clear
         * [0] for for master, [1] for aux, [9:2] for major 0~7
         */
        u32 retry_error_clear : 10;

        u32 : 6;
    };

    /**
     * @brief DWord register value.
     *
     */
    u32 dw;
};

#endif