/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2024 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2024 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/delay.h>
#include "vvcam_mipi_driver.h"

/**
 * MRV_CSI2_NUM_LANES
 * Config num lanes register [2:0] rw
 * 0000b - 1 Lanes
 * 0001b - 2 Lane
 * 0010b - 3 Lanes
 * 0011b - 4 Lanes (D-PHY only)
 * 0100b - 5 Lanes (D-PHY only)
 * 0101b - 6 Lanes (D-PHY only)
 * 0110b - 7 Lanes (D-PHY only)
 * 0111b - 8 Lanes (D-PHY only)
 */
#define MRV_CSI2_NUM_LANES 0x04

/**
 * CSI2 logical reset
 * 0b : reset
 * 1b : normal
**/
#define MRV_CSI2_RSTN 0x08


/**
 * CSI2 data ID Monitors
**/
#define MRV_CSI2_DATA_IDS1 0x10

/**
 * CSI2 data ID Monitors2
**/
#define MRV_CSI2_DATA_IDS2 0x14

/**
 * CSI2 virtual channel data ID Monitors1
**/
#define MRV_CSI2_DATA_IDS_VC1 0x30

/**
 * CSI2 virtual channel data ID Monitors2
**/
#define MRV_CSI2_DATA_IDS_VC2 0x34

/**
 * PHY SHUTDOWN, shutdown input
 * 0b : OFF
 * 1b : ON
**/
#define MRV_CSI2_PHY_SHUTDOWNZ 0x40

/**
 * D-PHY reset output
 * 0b : reset
 * 1b : normal
**/
#define MRV_CSI2_PHY_RSTZ 0x44

/**
 * Rx D-PHY status
 * receiving high speed clock signal or ultra low power mode
**/
#define MRV_CSI2_PHY_RX 0x48


/**
 * D-PHY stop state
**/
#define MRV_CSI2_PHY_STOPSTATE 0x4C

/**
 IPI interface register
**/

/**
 * IPI mode
 * bit[0] : CAMERA mode(0), CTRL mode(1)
 * bit[8] : 48bit(0), 16bit(1)
 * bit[16]: CT inactive(0), CT active(1)
 * bit[24]: IPI disable(0), IPI enable(1)
**/
#define MRV_CSI2_IPI_MODE 0x80

/**
 * IPI Virtual Channel
 * select virtual channel processed by ipi
 * 00b : vc 0
 * 01b : vc 1
 * 10b : vc 2
 * 11b : vc 3
 * [3:2] : virtual channel extension
 * [4] : virtual channel extension extra bit
**/
#define MRV_CSI2_IPI_VCID 0x84

/**
 * MRV_CSI2_IPI_DATA_TYPE
 * [5:0] : data type
 * [8] : embedded data
 * set ipi data format
 * 0x18 - YUV420_8
 * 0x19 - YUV420_10
 * 0x1A - YUV420_8_LEG
 * 0x1C - YUV420_8_SHIFT(NV12)
 * 0x1D - YUV420_10_SHIFT
 * 0x1E - YUV422_8(NV16)
 * 0x1F - YUV422_10
 * 0x20 - RGB444
 * 0x21 - RGB555
 * 0x22 - RGB565
 * 0x23 - RGB666
 * 0x24 - RGB888
 * 0x28 - RAW6
 * 0x29 - RAW7
 * 0x2a - RAW8
 * 0x2b - RAW10
 * 0x2c - RAW12
 * 0x2d - RAW14
 * 0x2e - RAW16
 * 0x2f - RAW20
 * 0x27 - RAW24
 */
#define MRV_CSI2_IPI_DATA_TYPE 0x88

/**
 * IPI memory flush
 * [0] : flush ipi memory
 * [8] : memory is automatically flushed at each frame start
**/
#define MRV_CSI2_IPI_MEM_FLUSH 0x8C

/**
 * IPI horizontal synchronism active time
**/
#define MRV_CSI2_IPI_HSA_TIME 0x90

/**
 * IPI horizontal back porch time
**/
#define MRV_CSI2_IPI_HBP_TIME 0x94

/**
 * IPI horizontal sync delay time
**/
#define MRV_CSI2_IPI_HSD_TIME 0x98

/**
 * overall time for each video line
**/
#define MRV_CSI2_IPI_HLINE_TIME 0x9C

/**
 * IPI soft reset
 * [0/4/8/12/16/20/24/28] : 1/2/3/4/5/6/7/8 ipi soft reset
**/
#define MRV_CSI2_IPI_SOFT_RSTN 0xA0

/**
 * IPI advance features
**/
#define MRV_CSI2_IPI_ADV_FEATURES 0xAC


/**
 * vertical synchronism active period in number of horizontal lines
**/
#define MRV_CSI2_IPI_VSA_LINES 0xB0

/**
 * vertical back porch period in number of horizontal lines
**/
#define MRV_CSI2_IPI_VBP_LINES 0xB4

/**
 * vertical front porch period in number of horizontal lines
**/
#define MRV_CSI2_IPI_VFP_LINES 0xB8

/**
 * vertical resolution of video
**/
#define MRV_CSI2_IPI_VACTIVATE_LINES 0xBC

/**
 * IPI2 interface register
**/
#define MRV_CSI2_IPI2_MODE 0x200
#define MRV_CSI2_IPI2_VCID 0x204
#define MRV_CSI2_IPI2_DATA_TYPE 0x208
#define MRV_CSI2_IPI2_MEM_FLUSH 0x20C
#define MRV_CSI2_IPI2_HSA_TIME 0x210
#define MRV_CSI2_IPI2_HBP_TIME 0x214
#define MRV_CSI2_IPI2_HSD_TIME 0x218
#define MRV_CSI2_IPI2_ADV_FEATURES 0x21C

/**
 * IPI3 interface register
**/
#define MRV_CSI2_IPI3_MODE 0x220
#define MRV_CSI2_IPI3_VCID 0x224
#define MRV_CSI2_IPI3_DATA_TYPE 0x228
#define MRV_CSI2_IPI3_MEM_FLUSH 0x22C
#define MRV_CSI2_IPI3_HSA_TIME 0x230
#define MRV_CSI2_IPI3_HBP_TIME 0x234
#define MRV_CSI2_IPI3_HSD_TIME 0x238
#define MRV_CSI2_IPI3_ADV_FEATURES 0x23C

/**
 * IPI4 interface register
**/
#define MRV_CSI2_IPI4_MODE 0x240
#define MRV_CSI2_IPI4_VCID 0x244
#define MRV_CSI2_IPI4_DATA_TYPE 0x248
#define MRV_CSI2_IPI4_MEM_FLUSH 0x24C
#define MRV_CSI2_IPI4_HSA_TIME 0x250
#define MRV_CSI2_IPI4_HBP_TIME 0x254
#define MRV_CSI2_IPI4_HSD_TIME 0x258
#define MRV_CSI2_IPI4_ADV_FEATURES 0x25C

/**
 * IPI5 interface register
**/
#define MRV_CSI2_IPI5_MODE 0x5C0
#define MRV_CSI2_IPI5_VCID 0x5C4
#define MRV_CSI2_IPI5_DATA_TYPE 0x5C8
#define MRV_CSI2_IPI5_MEM_FLUSH 0x5CC
#define MRV_CSI2_IPI5_HSA_TIME 0x5D0
#define MRV_CSI2_IPI5_HBP_TIME 0x5D4
#define MRV_CSI2_IPI5_HSD_TIME 0x5D8
#define MRV_CSI2_IPI5_ADV_FEATURES 0x5DC

/**
 * IPI6 interface register
**/
#define MRV_CSI2_IPI6_MODE 0x5E0
#define MRV_CSI2_IPI6_VCID 0x5E4
#define MRV_CSI2_IPI6_DATA_TYPE 0x5E8
#define MRV_CSI2_IPI6_MEM_FLUSH 0x5EC
#define MRV_CSI2_IPI6_HSA_TIME 0x5F0
#define MRV_CSI2_IPI6_HBP_TIME 0x5F4
#define MRV_CSI2_IPI6_HSD_TIME 0x5F8
#define MRV_CSI2_IPI6_ADV_FEATURES 0x5FC

/**
 * IPI7 interface register
**/
#define MRV_CSI2_IPI7_MODE 0x600
#define MRV_CSI2_IPI7_VCID 0x604
#define MRV_CSI2_IPI7_DATA_TYPE 0x608
#define MRV_CSI2_IPI7_MEM_FLUSH 0x60C
#define MRV_CSI2_IPI7_HSA_TIME 0x610
#define MRV_CSI2_IPI7_HBP_TIME 0x614
#define MRV_CSI2_IPI7_HSD_TIME 0x618
#define MRV_CSI2_IPI7_ADV_FEATURES 0x61C

/**
 * IPI8 interface register
**/
#define MRV_CSI2_IPI8_MODE 0x620
#define MRV_CSI2_IPI8_VCID 0x624
#define MRV_CSI2_IPI8_DATA_TYPE 0x628
#define MRV_CSI2_IPI8_MEM_FLUSH 0x62C
#define MRV_CSI2_IPI8_HSA_TIME 0x630
#define MRV_CSI2_IPI8_HBP_TIME 0x634
#define MRV_CSI2_IPI8_HSD_TIME 0x638
#define MRV_CSI2_IPI8_ADV_FEATURES 0x63C

/**
 * macro values for register
**/

#define IPI_MODE_CAMERA             (0 << 0)
#define IPI_MODE_CTRL               (1 << 0)
#define IPI_MODE_48BIT_COLOR        (0 << 8)
#define IPI_MODE_16BIT_COLOR        (1 << 8)
#define IPI_MODE_CT_ACTIVATE        (1 << 16)
#define IPI_MODE_CT_INACTIVATE      (0 << 16)
#define IPI_MODE_ENABLE             (1 << 24)

#define IPI_EMBEDDED_DATA_ENABLE    (1 << 8)

#define IPI_DATA_TYPE_RAW_6BIT      0x28
#define IPI_DATA_TYPE_RAW_7BIT      0x29
#define IPI_DATA_TYPE_RAW_8BIT      0x2A
#define IPI_DATA_TYPE_RAW_10BIT     0x2B
#define IPI_DATA_TYPE_RAW_12BIT     0x2C
#define IPI_DATA_TYPE_RAW_14BIT     0x2D
#define IPI_DATA_TYPE_RAW_16BIT     0x2E
#define IPI_DATA_TYPE_RAW_20BIT     0x2F
#define IPI_DATA_TYPE_RAW_24BIT     0x27

#define IPI_DATA_TYPE_RGB_444        0x20
#define IPI_DATA_TYPE_RGB_555        0x21
#define IPI_DATA_TYPE_RGB_565        0x22
#define IPI_DATA_TYPE_RGB_666        0x23
#define IPI_DATA_TYPE_RGB_888        0x24

#define IPI_DATA_TYPE_YUV420_8BIT           0x18
#define IPI_DATA_TYPE_YUV420_10BIT          0x19
#define IPI_DATA_TYPE_YUV420_8BIT_LEG       0x1A
#define IPI_DATA_TYPE_YUV420_8BIT_CSPS      0x1C
#define IPI_DATA_TYPE_YUV420_10BIT_CSPS     0x1D
#define IPI_DATA_TYPE_YUV422_8BIT           0x1E
#define IPI_DATA_TYPE_YUV422_10BIT          0x1F

#define IS_VALID_ID(id) (id != -1)

static vvcam_input_dev_attr_t default_config = {
    .mode = VVCAM_INPUT_MODE_MIPI,
    .image_rect = {
        .left = 0,
        .top =0,
        .width = 1920,
        .height = 1080,
    },
    .mipi_attr = {
        .itf_cfg[0] = {
            .enable = true,
            .embedded_data_en = false,
            .vcid = VVCAM_ITF_VCID_0,
            .bit_mode = VVCAM_ITF_BIT_MODE_16BIT,
            .data_type = VVCAM_RAW_12BIT,
            .h_time = {
                .hsa = 80,
                .hbp = 350,
                .hfp = 300,
                .hline = 2500,
            },
            .v_time = {
                .vsa = 5,
                .vbp = 25,
                .vfp = 10,
                .vactivate_lines = 1080,
            }
        },
        .lane_id  = {0, 1, 2, 3, -1, -1, -1, -1},
    },
};

static void vvcam_mipi_dwc_write_reg(void __iomem *base,
			uint32_t addr, uint32_t value)
{
	writel(value, base + addr);
}

static uint32_t vvcam_mipi_dwc_read_reg(void __iomem *base, uint32_t addr)
{
	return readl(base + addr);
}

static int vvcam_mipi_dwc_set_stream(struct vvcam_mipi_dev *mipi_dev,
                                    uint32_t enable)
{
    uint32_t lanes_stop_state;
    uint32_t phy_rx;

    if (enable) {
        lanes_stop_state =
            vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_PHY_STOPSTATE);
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_PHY_SHUTDOWNZ, 1);
    } else {
        /* shutdown d-phy input */
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_PHY_SHUTDOWNZ, 0);
    }

    phy_rx = vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_PHY_RX);

    return 0;
}

static int vvcam_mipi_dwc_set_ipi_mode(struct vvcam_mipi_dev *mipi_dev,
                                        uint8_t ipi,
                                        vvcam_itf_bit_mode_t bit_mode,
                                        vvcam_itf_vcid_t vcid)
{
    switch (ipi) {
    case 0:
        if ((vcid >= 0) && (vcid < VVCAM_ITF_VCID_MAX)) {
            vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_IPI_VCID, vcid);
        }
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_IPI_MODE,
            (bit_mode << 8) | IPI_MODE_CT_ACTIVATE | IPI_MODE_ENABLE);
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_IPI_ADV_FEATURES, 0x01000000);
        break;

    case 1:
    case 2:
    case 3:
        if ((vcid >= 0) && (vcid < VVCAM_ITF_VCID_MAX)) {
            vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI2_VCID + (ipi - 1) * 0x20, vcid);
        }
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
            MRV_CSI2_IPI2_MODE + (ipi - 1) * 0x20,
            (bit_mode << 8) | IPI_MODE_CT_ACTIVATE | IPI_MODE_ENABLE);
        if (ipi == 1) {
            vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI2_ADV_FEATURES + (ipi - 1) * 0x20, 0x01200000);
        } else {
            vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI2_ADV_FEATURES + (ipi - 1) * 0x20, 0x01000000);
        }
        break;

    case 4:
    case 5:
    case 6:
    case 7:
        if ((vcid >= 0) && (vcid < VVCAM_ITF_VCID_MAX)) {
            vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI5_VCID + (ipi - 4) * 0x20, vcid);
        }
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
            MRV_CSI2_IPI5_MODE + (ipi - 4) * 0x20,
            (bit_mode << 8) | IPI_MODE_CT_ACTIVATE | IPI_MODE_ENABLE);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
            MRV_CSI2_IPI5_ADV_FEATURES + (ipi - 4) * 0x20, 0x01000000);
        break;

    default:
        return -EINVAL;
    }
    return 0;
}

static int vvcam_mipi_dwc_set_ipi_format(struct vvcam_mipi_dev *mipi_dev,
                    uint8_t ipi, uint32_t format, bool embedded_data_en)
{
    uint32_t fmt_code = 0;
    uint32_t ids = 0;
    uint32_t embedded_data_en_mask = 0;

    if (embedded_data_en) {
        embedded_data_en_mask = IPI_EMBEDDED_DATA_ENABLE;
    }

    switch (format) {
    case VVCAM_RAW_8BIT:
        fmt_code = IPI_DATA_TYPE_RAW_8BIT;
        break;

    case VVCAM_RAW_10BIT:
        fmt_code = IPI_DATA_TYPE_RAW_10BIT;
        break;

    case VVCAM_RAW_12BIT:
        fmt_code = IPI_DATA_TYPE_RAW_12BIT;
        break;

    case VVCAM_RAW_14BIT:
        fmt_code = IPI_DATA_TYPE_RAW_14BIT;
        break;

    case VVCAM_RAW_16BIT:
        fmt_code = IPI_DATA_TYPE_RAW_16BIT;
        break;

    case VVCAM_RAW_20BIT:
        fmt_code = IPI_DATA_TYPE_RAW_20BIT;
        break;

    case VVCAM_RAW_24BIT:
        fmt_code = IPI_DATA_TYPE_RAW_24BIT;
        break;

    case VVCAM_RGB_444:
        fmt_code = IPI_DATA_TYPE_RGB_444;
        break;

    case VVCAM_RGB_555:
        fmt_code = IPI_DATA_TYPE_RGB_555;
        break;

    case VVCAM_RGB_565:
        fmt_code = IPI_DATA_TYPE_RGB_565;
        break;

    case VVCAM_RGB_666:
        fmt_code = IPI_DATA_TYPE_RGB_666;
        break;

    case VVCAM_RGB_888:
        fmt_code = IPI_DATA_TYPE_RGB_888;
        break;

    case VVCAM_YUV420_8BIT:
        fmt_code = IPI_DATA_TYPE_YUV420_8BIT;
        break;

    case VVCAM_YUV420_10BIT:
        fmt_code = IPI_DATA_TYPE_YUV420_10BIT;
        break;

    case VVCAM_YUV420_8BIT_CSPS:
        fmt_code = IPI_DATA_TYPE_YUV420_8BIT_LEG;
        break;

    case VVCAM_YUV420_10BIT_CSPS:
        fmt_code = IPI_DATA_TYPE_YUV420_10BIT_CSPS;
        break;

    case VVCAM_YUV422_8BIT:
        fmt_code = IPI_DATA_TYPE_YUV422_8BIT;
        break;

    case VVCAM_YUV422_10BIT:
        fmt_code = IPI_DATA_TYPE_YUV422_10BIT;
        break;

    default:
        pr_err("format %d not support", format);
        return -EINVAL;
    }

    switch (ipi) {
    case 0:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                    MRV_CSI2_IPI_DATA_TYPE, fmt_code | embedded_data_en_mask);
        ids = vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_DATA_IDS1);
        ids = fmt_code | (ids & 0xFFFFFFC0);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                    MRV_CSI2_DATA_IDS1, ids);
        break;
    case 1:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                    MRV_CSI2_IPI2_DATA_TYPE, fmt_code | embedded_data_en_mask);
        ids = vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_DATA_IDS1);
        ids = ((fmt_code << 8) | (ids & 0xFFFFC0FF));
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_DATA_IDS1, ids);
        break;
    case 2:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                    MRV_CSI2_IPI3_DATA_TYPE, fmt_code | embedded_data_en_mask);
        ids = vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_DATA_IDS1);
        ids = ((fmt_code << 16) | (ids & 0xFFC0FFFF));
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_DATA_IDS1, ids);
        break;
    case 3:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                    MRV_CSI2_IPI4_DATA_TYPE, fmt_code | embedded_data_en_mask);
        ids = vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_DATA_IDS1);
        ids = ((fmt_code << 24) | (ids & 0xC0FFFFFF));
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_DATA_IDS1, ids);
        break;
    case 4:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                    MRV_CSI2_IPI5_DATA_TYPE, fmt_code | embedded_data_en_mask);
        ids = vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_DATA_IDS2);
        ids = fmt_code | (ids & 0xFFFFFFC0);
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_DATA_IDS2, ids);
        break;
    case 5:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                    MRV_CSI2_IPI6_DATA_TYPE, fmt_code | embedded_data_en_mask);
        ids = vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_DATA_IDS2);
        ids = ((fmt_code << 8) | (ids & 0xFFFFC0FF));
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_DATA_IDS2, ids);
        break;
    case 6:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                    MRV_CSI2_IPI7_DATA_TYPE, fmt_code | embedded_data_en_mask);
        ids = vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_DATA_IDS2);
        ids = ((fmt_code << 16) | (ids & 0xFFC0FFFF));
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_DATA_IDS2, ids);
        break;
    case 7:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                    MRV_CSI2_IPI8_DATA_TYPE, fmt_code | embedded_data_en_mask);
        ids = vvcam_mipi_dwc_read_reg(mipi_dev->base, MRV_CSI2_DATA_IDS2);
        ids = ((fmt_code << 24) | (ids & 0xC0FFFFFF));
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_DATA_IDS2, ids);
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static int vvcam_mipi_dwc_set_ipi_timing(struct vvcam_mipi_dev *mipi_dev,
                                        uint8_t ipi,
                                        vvcam_itf_horizontal_time_t *h_time,
                                        vvcam_itf_vertical_time_t *v_time)
{
    switch (ipi) {
    case 0:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI_HSA_TIME, h_time->hsa);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI_HBP_TIME, h_time->hbp);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI_HSD_TIME, h_time->hfp);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI_HLINE_TIME, h_time->hline);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI_VSA_LINES, v_time->vsa);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI_VBP_LINES, v_time->vbp);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI_VFP_LINES, v_time->vfp);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI_VACTIVATE_LINES, v_time->vactivate_lines);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI_MEM_FLUSH, 0x100);
        break;

    case 1:
    case 2:
    case 3:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI2_HSA_TIME + (ipi - 1) * 0x20, h_time->hsa);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI2_HBP_TIME + (ipi - 1) * 0x20, h_time->hbp);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI2_HSD_TIME + (ipi - 1) * 0x20, h_time->hfp);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI2_MEM_FLUSH + (ipi - 1) * 0x20, 0x100);
        break;

    case 4:
    case 5:
    case 6:
    case 7:
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI5_HSA_TIME + (ipi - 4) * 0x20, h_time->hsa);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI5_HBP_TIME + (ipi - 4) * 0x20, h_time->hbp);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI5_HSD_TIME + (ipi - 4) * 0x20, h_time->hfp);
        vvcam_mipi_dwc_write_reg(mipi_dev->base,
                MRV_CSI2_IPI5_MEM_FLUSH + (ipi - 4) * 0x20, 0x100);
        break;

    default:
        pr_err("ipi %d is not support\n", ipi);
        return -EINVAL;
    }

    return 0;
}

static int vvcam_mipi_dwc_cfg(struct vvcam_mipi_dev *mipi_dev,
                            vvcam_input_dev_attr_t mipi_cfg)
{
    int i = 0;
    int32_t ret;
    uint8_t mipi_lanes = 0;
    uint32_t ids_vc_1 = 0;
    uint32_t ids_vc_2 = 0;

    for (i = 0; i < MIPI_LANE_NUM_MAX; i++) {
        if (IS_VALID_ID(mipi_cfg.mipi_attr.lane_id[i])){
            mipi_lanes++;
        }
    }

    vvcam_mipi_reset(mipi_dev, 1);
    switch (mipi_lanes) {
    case 1:
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_NUM_LANES, 0x00);
        break;
    case 2:
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_NUM_LANES, 0x01);
        break;
    case 3:
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_NUM_LANES, 0x02);
        break;
    case 4:
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_NUM_LANES, 0x03);
        break;
    case 5:
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_NUM_LANES, 0x04);
        break;
    case 6:
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_NUM_LANES, 0x05);
        break;
    case 7:
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_NUM_LANES, 0x06);
        break;
    case 8:
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_NUM_LANES, 0x07);
        break;
    default:
        return -EINVAL;
    }

    for (i = 0; i < MIPI_OUTPUT_INTERFACES_MAX; i++) {
        if (!mipi_cfg.mipi_attr.itf_cfg[i].enable)
            continue;

        ret = vvcam_mipi_dwc_set_ipi_format(mipi_dev, i,
                mipi_cfg.mipi_attr.itf_cfg[i].data_type,
                mipi_cfg.mipi_attr.itf_cfg[i].embedded_data_en);
        if (ret)
            return ret;

        ret = vvcam_mipi_dwc_set_ipi_timing(mipi_dev, i,
                &mipi_cfg.mipi_attr.itf_cfg[i].h_time,
                &mipi_cfg.mipi_attr.itf_cfg[i].v_time);
        if (ret)
            return ret;

        ret = vvcam_mipi_dwc_set_ipi_mode(mipi_dev, i,
                mipi_cfg.mipi_attr.itf_cfg[i].bit_mode,
                mipi_cfg.mipi_attr.itf_cfg[i].vcid);
        if (ret)
            return ret;

        if (i < 4) {
            ids_vc_1 |= (mipi_cfg.mipi_attr.itf_cfg[i].vcid << (i * 8));
        } else {
            ids_vc_2 |= (mipi_cfg.mipi_attr.itf_cfg[i].vcid << ((i - 4) * 8));
        }
    }

    vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_DATA_IDS_VC1, ids_vc_1);
    vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_DATA_IDS_VC2, ids_vc_2);
    vvcam_mipi_reset(mipi_dev, 0);

    return 0;
}

int vvcam_mipi_reset(struct vvcam_mipi_dev *mipi_dev, uint32_t val)
{
    if (val) {
        /* reset */
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_PHY_RSTZ, 0);
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_RSTN, 0);
    } else {
        /* unreset */
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_RSTN, 1);
        vvcam_mipi_dwc_write_reg(mipi_dev->base, MRV_CSI2_PHY_RSTZ, 1);
    }
    return 0;
}

int vvcam_mipi_default_cfg(struct vvcam_mipi_dev *mipi_dev)
{
    memcpy(&mipi_dev->mipi_cfg,
        &default_config, sizeof(vvcam_input_dev_attr_t));

    vvcam_mipi_dwc_cfg(mipi_dev, mipi_dev->mipi_cfg);
#if defined(ISP_VI200)
    //hardware pin power on sensor
    vvcam_mipi_dwc_write_reg(mipi_dev->csi_ctrl_reg-4, 0, 0x0);
    vvcam_mipi_dwc_write_reg(mipi_dev->csi_ctrl_reg-4, 0, 0x3);
 #elif defined(ISP_VI200_V2)
    //sepearte reset bit
    vvcam_mipi_dwc_write_reg(mipi_dev->csi_ctrl_reg-4, 0, 0x00000000);
    vvcam_mipi_dwc_write_reg(mipi_dev->csi_ctrl_reg-4, 0, 0x00f0f000);
#endif

    return 0;
}


int vvcam_mipi_set_cfg(struct vvcam_mipi_dev *mipi_dev,
                    vvcam_input_dev_attr_t mipi_cfg)
{
    int ret = 0;

    switch (mipi_cfg.mode) {
    case VVCAM_INPUT_MODE_MIPI:
        ret = vvcam_mipi_dwc_cfg(mipi_dev, mipi_cfg);
        break;
    case VVCAM_INPUT_MODE_LVDS:
        ret = -EINVAL;
        break;
    case VVCAM_INPUT_MODE_BT1120:
        ret = -EINVAL;
        break;
    default:
        ret = -EINVAL;
        break;
    }

    if (ret == 0) {
        memcpy(&mipi_dev->mipi_cfg, &mipi_cfg, sizeof(mipi_cfg));
    }

    return ret;
}

int vvcam_mipi_set_stream(struct vvcam_mipi_dev *mipi_dev, uint32_t enable)
{
    int ret = 0;

    switch (mipi_dev->mipi_cfg.mode) {
    case VVCAM_INPUT_MODE_MIPI:
        ret = vvcam_mipi_dwc_set_stream(mipi_dev, enable);
        break;
    case VVCAM_INPUT_MODE_LVDS:
        ret = -EINVAL;
        break;
    case VVCAM_INPUT_MODE_BT1120:
        ret = -EINVAL;
        break;
    default:
        ret = -EINVAL;
        break;
    }

    return ret;
}
