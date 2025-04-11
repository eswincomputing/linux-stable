// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN phy camera-module driver
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

 #ifndef _UAPI_ESMODULE_CAMERA_H
 #define _UAPI_ESMODULE_CAMERA_H
 
 #include <linux/types.h>
 #include "es-video-format.h"
 
 #define ESMODULE_API_VERSION		KERNEL_VERSION(0, 1, 0x2)
 
 /* using for es3588 dual isp unite */
 #define ESMOUDLE_UNITE_EXTEND_PIXEL	128
 /* using for rv1109 and rv1126 */
 #define ESMODULE_EXTEND_LINE		24
 
 #define ESMODULE_NAME_LEN		32
 #define ESMODULE_LSCDATA_LEN		289
 
 #define ESMODULE_MAX_VC_CH		4
 
 #define ESMODULE_PADF_GAINMAP_LEN	1024
 #define ESMODULE_PDAF_DCCMAP_LEN	256
 #define ESMODULE_AF_OTP_MAX_LEN		3
 
 #define ESMODULE_MAX_SENSOR_NUM		8
 
 #define ESMODULE_CAMERA_MODULE_INDEX	"eswin,camera-module-index"
 #define ESMODULE_CAMERA_MODULE_FACING	"eswin,camera-module-facing"
 #define ESMODULE_CAMERA_MODULE_NAME	"eswin,camera-module-name"
 #define ESMODULE_CAMERA_LENS_NAME	"eswin,camera-module-lens-name"
 
 #define ESMODULE_CAMERA_SYNC_MODE	"eswin,camera-module-sync-mode"
 #define ESMODULE_INTERNAL_MASTER_MODE	"internal_master"
 #define ESMODULE_EXTERNAL_MASTER_MODE	"external_master"
 #define ESMODULE_SLAVE_MODE		"slave"
 
 /* BT.656 & BT.1120 multi channel
  * On which channels it can send video data
  * related with struct esmodule_bt656_mbus_info
  */
 #define ESMODULE_CAMERA_BT656_ID_EN_BITS_1		(0x1)
 #define ESMODULE_CAMERA_BT656_ID_EN_BITS_2		(0x3)
 #define ESMODULE_CAMERA_BT656_ID_EN_BITS_3		(0x7)
 #define ESMODULE_CAMERA_BT656_ID_EN_BITS_4		(0xf)
 #define ESMODULE_CAMERA_BT656_PARSE_ID_LSB		BIT(0)
 #define ESMODULE_CAMERA_BT656_PARSE_ID_MSB		BIT(1)
 #define ESMODULE_CAMERA_BT656_CHANNEL_0			BIT(2)
 #define ESMODULE_CAMERA_BT656_CHANNEL_1			BIT(3)
 #define ESMODULE_CAMERA_BT656_CHANNEL_2			BIT(4)
 #define ESMODULE_CAMERA_BT656_CHANNEL_3			BIT(5)
 #define ESMODULE_CAMERA_BT656_CHANNELS			(ESMODULE_CAMERA_BT656_CHANNEL_0 | \
                              ESMODULE_CAMERA_BT656_CHANNEL_1 | \
                              ESMODULE_CAMERA_BT656_CHANNEL_2 | \
                              ESMODULE_CAMERA_BT656_CHANNEL_3)
 
 #define DPHY_MAX_LANE					4
 #define ESMODULE_MULTI_DEV_NUM				4
 
 #define ESMODULE_GET_MODULE_INFO	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 0, struct esmodule_inf)
 
 #define ESMODULE_AWB_CFG	\
     _IOW('V', BASE_VIDIOC_PRIVATE + 1, struct esmodule_awb_cfg)
 
 #define ESMODULE_AF_CFG	\
     _IOW('V', BASE_VIDIOC_PRIVATE + 2, struct esmodule_af_cfg)
 
 #define ESMODULE_LSC_CFG	\
     _IOW('V', BASE_VIDIOC_PRIVATE + 3, struct esmodule_lsc_cfg)
 
 #define ESMODULE_GET_HDR_CFG	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 4, struct esmodule_hdr_cfg)
 
 #define ESMODULE_SET_HDR_CFG	\
     _IOW('V', BASE_VIDIOC_PRIVATE + 5, struct esmodule_hdr_cfg)
 
 #define ESMODULE_SET_CONVERSION_GAIN	\
     _IOW('V', BASE_VIDIOC_PRIVATE + 6, __u32)
 
 #define ESMODULE_GET_LVDS_CFG	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 7, struct esmodule_lvds_cfg)
 
 #define ESMODULE_SET_DPCC_CFG	\
     _IOW('V', BASE_VIDIOC_PRIVATE + 8, struct esmodule_dpcc_cfg)
 
 #define ESMODULE_GET_NR_SWITCH_THRESHOLD	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 9, struct esmodule_nr_switch_threshold)
 
 #define ESMODULE_SET_QUICK_STREAM	\
     _IOW('V', BASE_VIDIOC_PRIVATE + 10, __u32)
 
 #define ESMODULE_GET_BT656_INTF_TYPE	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 11, __u32)
 
 #define ESMODULE_GET_VC_FMT_INFO \
     _IOR('V', BASE_VIDIOC_PRIVATE + 12, struct esmodule_vc_fmt_info)
 
 #define ESMODULE_GET_VC_HOTPLUG_INFO \
     _IOR('V', BASE_VIDIOC_PRIVATE + 13, struct esmodule_vc_hotplug_info)
 
 #define ESMODULE_GET_START_STREAM_SEQ	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 14, __u32)
 
 #define ESMODULE_GET_VICAP_RST_INFO	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 15, struct esmodule_vicap_reset_info)
 
 #define ESMODULE_SET_VICAP_RST_INFO	\
     _IOW('V', BASE_VIDIOC_PRIVATE + 16, struct esmodule_vicap_reset_info)
 
 #define ESMODULE_GET_BT656_MBUS_INFO	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 17, struct esmodule_bt656_mbus_info)
 
 #define ESMODULE_GET_DCG_RATIO	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 18, struct esmodule_dcg_ratio)
 
 #define ESMODULE_GET_SONY_BRL	\
     _IOR('V', BASE_VIDIOC_PRIVATE + 19, __u32)
 
 #define ESMODULE_GET_CHANNEL_INFO	\
     _IOWR('V', BASE_VIDIOC_PRIVATE + 20, struct esmodule_channel_info)
 
 #define ESMODULE_GET_SYNC_MODE       \
     _IOR('V', BASE_VIDIOC_PRIVATE + 21, __u32)
 
 #define ESMODULE_SET_SYNC_MODE       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 22, __u32)
 
 #define ESMODULE_SET_MCLK       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 23, struct esmodule_mclk_data)
 
 #define ESMODULE_SET_LINK_FREQ       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 24, __s64)
 
 #define ESMODULE_SET_BUS_CONFIG       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 25, struct esmodule_bus_config)
 
 #define ESMODULE_GET_BUS_CONFIG       \
     _IOR('V', BASE_VIDIOC_PRIVATE + 26, struct esmodule_bus_config)
 
 #define ESMODULE_SET_REGISTER       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 27, struct esmodule_reg)
 
 #define ESMODULE_SYNC_I2CDEV       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 28, __u8)
 
 #define ESMODULE_SYNC_I2CDEV_COMPLETE       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 29, __u8)
 
 #define ESMODULE_SET_DEV_INFO       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 30, struct esmodule_dev_info)
 
 #define ESMODULE_SET_CSI_DPHY_PARAM       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 31, struct esmodule_csi_dphy_param)
 
 #define ESMODULE_GET_CSI_DPHY_PARAM       \
     _IOWR('V', BASE_VIDIOC_PRIVATE + 32, struct esmodule_csi_dphy_param)
 
 #define ESMODULE_GET_CSI_DSI_INFO       \
     _IOWR('V', BASE_VIDIOC_PRIVATE + 33, __u32)
 
 #define ESMODULE_GET_HDMI_MODE       \
     _IOR('V', BASE_VIDIOC_PRIVATE + 34, __u32)
 
 #define ESMODULE_SET_SENSOR_INFOS       \
     _IOWR('V', BASE_VIDIOC_PRIVATE + 35, struct esmodule_sensor_infos)
 
 #define ESMODULE_GET_READOUT_LINE_CNT_PER_LINE  \
     _IOR('V', BASE_VIDIOC_PRIVATE + 36, __u32)
 
 #define ESMODULE_GET_GROUP_ID       \
     _IOR('V', BASE_VIDIOC_PRIVATE + 37, __u32)
 
 #define ESMODULE_SET_GROUP_ID       \
     _IOW('V', BASE_VIDIOC_PRIVATE + 38, __u32)
 
 #define ESMODULE_GET_CAPTURE_MODE  \
     _IOR('V', BASE_VIDIOC_PRIVATE + 39, struct esmodule_capture_info)
 
 #define ESMODULE_SET_CAPTURE_MODE  \
     _IOW('V', BASE_VIDIOC_PRIVATE + 40, struct esmodule_capture_info)
 
 #define ESMODULE_GET_SKIP_FRAME  \
     _IOR('V', BASE_VIDIOC_PRIVATE + 41, __u32)
 
 struct esmodule_i2cdev_info {
     __u8 slave_addr;
 } __attribute__ ((packed));
 
 struct esmodule_dev_info {
     union {
         struct esmodule_i2cdev_info i2c_dev;
         __u32 reserved[8];
     };
 } __attribute__ ((packed));
 
 /* csi0/csi1 phy support full/split mode */
 enum esmodule_phy_mode {
     PHY_FULL_MODE,
     PHY_SPLIT_01,
     PHY_SPLIT_23,
 };
 
 struct esmodule_mipi_lvds_bus {
     __u32 bus_type;
     __u32 lanes;
     __u32 phy_mode; /* data type enum esmodule_phy_mode */
 };
 
 struct esmodule_bus_config {
     union {
         struct esmodule_mipi_lvds_bus bus;
         __u32 reserved[32];
     };
 } __attribute__ ((packed));
 
 struct esmodule_reg {
     __u64 num_regs;
     __u64 preg_addr;
     __u64 preg_value;
     __u64 preg_addr_bytes;
     __u64 preg_value_bytes;
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_base_inf - module base information
  *
  */
 struct esmodule_base_inf {
     char sensor[ESMODULE_NAME_LEN];
     char module[ESMODULE_NAME_LEN];
     char lens[ESMODULE_NAME_LEN];
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_fac_inf - module factory information
  *
  */
 struct esmodule_fac_inf {
     __u32 flag;
 
     char module[ESMODULE_NAME_LEN];
     char lens[ESMODULE_NAME_LEN];
     __u32 year;
     __u32 month;
     __u32 day;
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_awb_inf - module awb information
  *
  */
 struct esmodule_awb_inf {
     __u32 flag;
 
     __u32 r_value;
     __u32 b_value;
     __u32 gr_value;
     __u32 gb_value;
 
     __u32 golden_r_value;
     __u32 golden_b_value;
     __u32 golden_gr_value;
     __u32 golden_gb_value;
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_lsc_inf - module lsc information
  *
  */
 struct esmodule_lsc_inf {
     __u32 flag;
 
     __u16 lsc_w;
     __u16 lsc_h;
     __u16 decimal_bits;
 
     __u16 lsc_r[ESMODULE_LSCDATA_LEN];
     __u16 lsc_b[ESMODULE_LSCDATA_LEN];
     __u16 lsc_gr[ESMODULE_LSCDATA_LEN];
     __u16 lsc_gb[ESMODULE_LSCDATA_LEN];
 
     __u16 width;
     __u16 height;
     __u16 table_size;
 } __attribute__ ((packed));
 
 /**
  * enum esmodule_af_dir - enum of module af otp direction
  */
 enum esmodele_af_otp_dir {
     AF_OTP_DIR_HORIZONTAL = 0,
     AF_OTP_DIR_UP = 1,
     AF_OTP_DIR_DOWN = 2,
 };
 
 /**
  * struct esmodule_af_otp - module af otp in one direction
  */
 struct esmodule_af_otp {
     __u32 vcm_start;
     __u32 vcm_end;
     __u32 vcm_dir;
 };
 
 /**
  * struct esmodule_af_inf - module af information
  *
  */
 struct esmodule_af_inf {
     __u32 flag;
     __u32 dir_cnt;
     struct esmodule_af_otp af_otp[ESMODULE_AF_OTP_MAX_LEN];
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_pdaf_inf - module pdaf information
  *
  */
 struct esmodule_pdaf_inf {
     __u32 flag;
 
     __u32 gainmap_width;
     __u32 gainmap_height;
     __u32 dccmap_width;
     __u32 dccmap_height;
     __u32 dcc_mode;
     __u32 dcc_dir;
     __u32 pd_offset;
     __u16 gainmap[ESMODULE_PADF_GAINMAP_LEN];
     __u16 dccmap[ESMODULE_PDAF_DCCMAP_LEN];
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_otp_module_inf - otp module info
  *
  */
 struct esmodule_otp_module_inf {
     __u32 flag;
     __u8 vendor[8];
     __u32 module_id;
     __u16 version;
     __u16 full_width;
     __u16 full_height;
     __u8 supplier_id;
     __u8 year;
     __u8 mouth;
     __u8 day;
     __u8 sensor_id;
     __u8 lens_id;
     __u8 vcm_id;
     __u8 drv_id;
     __u8 flip;
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_inf - module information
  *
  */
 struct esmodule_inf {
     struct esmodule_base_inf base;
     struct esmodule_fac_inf fac;
     struct esmodule_awb_inf awb;
     struct esmodule_lsc_inf lsc;
     struct esmodule_af_inf af;
     struct esmodule_pdaf_inf pdaf;
     struct esmodule_otp_module_inf module_inf;
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_awb_inf - module awb information
  *
  */
 struct esmodule_awb_cfg {
     __u32 enable;
     __u32 golden_r_value;
     __u32 golden_b_value;
     __u32 golden_gr_value;
     __u32 golden_gb_value;
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_af_cfg
  *
  */
 struct esmodule_af_cfg {
     __u32 enable;
     __u32 vcm_start;
     __u32 vcm_end;
     __u32 vcm_dir;
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_lsc_cfg
  *
  */
 struct esmodule_lsc_cfg {
     __u32 enable;
 } __attribute__ ((packed));
 
 /**
  * NO_HDR: linear mode
  * HDR_X2: hdr two frame or line mode
  * HDR_X3: hdr three or line mode
  * HDR_COMPR: linearised and compressed data for hdr
  */
 enum esmodule_hdr_mode {
     NO_HDR = 0,
     HDR_X2 = 5,
     HDR_X3 = 6,
     HDR_COMPR,
 };
 
 enum esmodule_hdr_compr_segment {
     HDR_COMPR_SEGMENT_4 = 4,
     HDR_COMPR_SEGMENT_12 = 12,
     HDR_COMPR_SEGMENT_16 = 16,
 };
 
 /* esmodule_hdr_compr
  * linearised and compressed data for hdr: data_src = K * data_compr + XX
  *
  * bit: bit of src data, max 20 bit.
  * segment: linear segment, support 4, 6 or 16.
  * k_shift: left shift bit of slop amplification factor, 2^k_shift, [0 15].
  * slope_k: K * 2^k_shift.
  * data_src_shitf: left shift bit of source data, data_src = 2^data_src_shitf
  * data_compr: compressed data.
  */
 struct esmodule_hdr_compr {
     enum esmodule_hdr_compr_segment segment;
     __u8 bit;
     __u8 k_shift;
     __u8 data_src_shitf[HDR_COMPR_SEGMENT_16];
     __u16 data_compr[HDR_COMPR_SEGMENT_16];
     __u32 slope_k[HDR_COMPR_SEGMENT_16];
 };
 
 /**
  * HDR_NORMAL_VC: hdr frame with diff virtual channels
  * HDR_LINE_CNT: hdr frame with line counter
  * HDR_ID_CODE: hdr frame with identification code
  */
 enum hdr_esp_mode {
     HDR_NORMAL_VC = 0,
     HDR_LINE_CNT,
     HDR_ID_CODE,
 };
 
 /*
  * CSI/DSI input select IOCTL
  */
 enum esmodule_csi_dsi_seq {
     ESMODULE_CSI_INPUT = 0,
     ESMODULE_DSI_INPUT,
 };
 
 /**
  * lcnt: line counter
  *     padnum: the pixels of padding row
  *     padpix: the payload of padding
  * idcd: identification code
  *     efpix: identification code of Effective line
  *     obpix: identification code of OB line
  */
 struct esmodule_hdr_esp {
     enum hdr_esp_mode mode;
     union {
         struct {
             __u32 padnum;
             __u32 padpix;
         } lcnt;
         struct {
             __u32 efpix;
             __u32 obpix;
         } idcd;
     } val;
 };
 
 struct esmodule_hdr_cfg {
     __u32 hdr_mode;
     struct esmodule_hdr_esp esp;
     struct esmodule_hdr_compr compr;
 } __attribute__ ((packed));
 
 /* sensor lvds sync code
  * sav: start of active video codes
  * eav: end of active video codes
  */
 struct esmodule_sync_code {
     __u16 sav;
     __u16 eav;
 };
 
 /* sensor lvds difference sync code mode
  * LS_FIRST: valid line ls-le or sav-eav
  *	   invalid line fs-fe or sav-eav
  * FS_FIRST: valid line fs-le
  *	   invalid line ls-fe
  * ls: line start
  * le: line end
  * fs: frame start
  * fe: frame end
  * SONY_DOL_HDR_1: sony dol hdr pattern 1
  * SONY_DOL_HDR_2: sony dol hdr pattern 2
  */
 enum esmodule_lvds_mode {
     LS_FIRST = 0,
     FS_FIRST,
     SONY_DOL_HDR_1,
     SONY_DOL_HDR_2
 };
 
 /* sync code of different frame type (hdr or linear) for lvds
  * act: valid line sync code
  * blk: invalid line sync code
  */
 struct esmodule_lvds_frm_sync_code {
     struct esmodule_sync_code act;
     struct esmodule_sync_code blk;
 };
 
 /* sync code for lvds of sensor
  * odd_sync_code: sync code of odd frame id for lvds of sony sensor
  * even_sync_code: sync code of even frame id for lvds of sony sensor
  */
 struct esmodule_lvds_frame_sync_code {
     struct esmodule_lvds_frm_sync_code odd_sync_code;
     struct esmodule_lvds_frm_sync_code even_sync_code;
 };
 
 /* lvds sync code category of sensor for different operation */
 enum esmodule_lvds_sync_code_group {
     LVDS_CODE_GRP_LINEAR = 0x0,
     LVDS_CODE_GRP_LONG,
     LVDS_CODE_GRP_MEDIUM,
     LVDS_CODE_GRP_SHORT,
     LVDS_CODE_GRP_MAX
 };
 
 /* struct esmodule_lvds_cfg
  * frm_sync_code[index]:
  *  index == LVDS_CODE_GRP_LONG:
  *    sync code for frame of linear mode or for long frame of hdr mode
  *  index == LVDS_CODE_GRP_MEDIUM:
  *    sync code for medium long frame of hdr mode
  *  index == LVDS_CODE_GRP_SHOR:
  *    sync code for short long frame of hdr mode
  */
 struct esmodule_lvds_cfg {
     enum esmodule_lvds_mode mode;
     struct esmodule_lvds_frame_sync_code frm_sync_code[LVDS_CODE_GRP_MAX];
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_dpcc_cfg
  * enable: 0 -> disable dpcc, 1 -> enable multiple,
  *         2 -> enable single, 3 -> enable all;
  * cur_single_dpcc: the strength of single dpcc;
  * cur_multiple_dpcc: the strength of multiple dpcc;
  * total_dpcc: the max strength;
  */
 struct esmodule_dpcc_cfg {
     __u32 enable;
     __u32 cur_single_dpcc;
     __u32 cur_multiple_dpcc;
     __u32 total_dpcc;
 } __attribute__ ((packed));
 
 /**
  * nr switch by gain
  * direct: 0 -> up_thres LSNR to HSNR, 1 -> up_thres HSNR to LSNR
  * up_thres: threshold of nr change from low gain to high gain
  * down_thres: threshold of nr change from high gain to low gain;
  * div_coeff: Coefficients converted from float to int
  */
 struct esmodule_nr_switch_threshold {
     __u32 direct;
     __u32 up_thres;
     __u32 down_thres;
     __u32 div_coeff;
 } __attribute__ ((packed));
 
 /**
  * enum esmodule_bt656_intf_type
  * to support sony bt656 raw
  */
 enum esmodule_bt656_intf_type {
     BT656_STD_RAW = 0,
     BT656_SONY_RAW,
 };
 
 /**
  * struct esmodule_vc_fmt_info - virtual channels fmt info
  *
  */
 struct esmodule_vc_fmt_info {
     __u32 width[ESMODULE_MAX_VC_CH];
     __u32 height[ESMODULE_MAX_VC_CH];
     __u32 fps[ESMODULE_MAX_VC_CH];
 } __attribute__ ((packed));
 
 /**
  * struct esmodule_vc_hotplug_info - virtual channels hotplug status info
  * detect_status: hotplug status
  *     bit 0~3 means channels id, value : 0 -> plug out, 1 -> plug in.
  */
 struct esmodule_vc_hotplug_info {
     __u8 detect_status;
 } __attribute__ ((packed));
 
 
 /* sensor start stream sequence
  * ESMODULE_START_STREAM_DEFAULT: by default
  * ESMODULE_START_STREAM_BEHIND : sensor start stream should be behind the controller
  * ESMODULE_START_STREAM_FRONT  : sensor start stream should be in front of the controller
  */
 enum esmodule_start_stream_seq {
     ESMODULE_START_STREAM_DEFAULT = 0,
     ESMODULE_START_STREAM_BEHIND,
     ESMODULE_START_STREAM_FRONT,
 };
 
 /*
  * HDMI to MIPI-CSI MODE IOCTL
  */
 enum esmodule_hdmiin_mode_seq {
     ESMODULE_HDMIIN_DEFAULT = 0,
     ESMODULE_HDMIIN_MODE,
 };
 /*
  * the causation to do dvp2axi reset work
  */
 enum esmodule_reset_src {
     ES_RESET_SRC_NON = 0x0,
     ES_RESET_SRC_ERR_CSI2,
     ES_RESET_SRC_ERR_LVDS,
     ES_RESET_SRC_ERR_CUTOFF,
     ES_RESET_SRC_ERR_HOTPLUG,
     ES_RESET_SRC_ERR_APP,
     ES_RESET_SRC_ERR_ISP,
 };
 
 struct esmodule_vicap_reset_info {
     __u32 is_reset;
     enum esmodule_reset_src src;
 } __attribute__ ((packed));
 
 struct esmodule_bt656_mbus_info {
     __u32 flags;
     __u32 id_en_bits;
 } __attribute__ ((packed));
 
 /* DCG ratio (float) = integer + decimal / div_coeff */
 struct esmodule_dcg_ratio {
     __u32 integer;
     __u32 decimal;
     __u32 div_coeff;
 };
 
 struct esmodule_channel_info {
     __u32 index;
     __u32 vc;
     __u32 width;
     __u32 height;
     __u32 bus_fmt;
     __u32 data_type;
     __u32 data_bit;
 } __attribute__ ((packed));
 
 /*
  * link to vicap
  * linear mode: pad0~pad3 for id0~id3;
  *
  * HDR_X2: id0 fiexd to vc0 for long frame
  *         id1 fixed to vc1 for short frame;
  *         id2~id3 reserved, can config by PAD2~PAD3
  *
  * HDR_X3: id0 fiexd to vc0 for long frame
  *         id1 fixed to vc1 for middle frame
  *         id2 fixed to vc2 for short frame;
  *         id3 reserved, can config by PAD3
  *
  * link to isp, the connection relationship is as follows
  */
 enum esmodule_max_pad {
     PAD0, /* link to isp */
     PAD1, /* link to csi wr0 | hdr x2:L x3:M */
     PAD2, /* link to csi wr1 | hdr      x3:L */
     PAD3, /* link to csi wr2 | hdr x2:M x3:S */
     PAD_MAX,
 };
 
 /*
  * sensor exposure sync mode
  */
 enum esmodule_sync_mode {
     NO_SYNC_MODE = 0,
     EXTERNAL_MASTER_MODE,
     INTERNAL_MASTER_MODE,
     SLAVE_MODE,
 };
 
 struct esmodule_mclk_data {
     __u32 enable;
     __u32 mclk_index;
     __u32 mclk_rate;
     __u32 reserved[8];
 };
 
 /*
  * csi dphy param
  * lp_vol_ref -> Reference voltage-645mV for LP  Function control pin
  * for es3588 dcphy
  * 3'b000 : 605mV
  * 3'b001 : 625mV
  * 3'b010 : 635mV
  * 3'b011 : 645mV
  * 3'b100 : 655mV
  * 3'b101 : 665mV
  * 3'b110 : 685mV
  * 3'b111 : 725mV
  *
  * lp_hys_sw -> LP-RX Hysteresis Level Control
  * for es3588 dcphy
  * 2'b00=45mV
  * 2'b01=65mV
  * 2'b10=85mV
  * 2'b11=100mV
  *
  * lp_escclk_pol_sel -> LP ESCCLK Polarity sel
  * for es3588 dcphy
  * 1'b0: normal
  * 1'b1: swap ,Increase 1ns delay
  *
  * skew_data_cal_clk -> Skew Calibration Manual Data Fine Delay Control Register
  * for es3588 dcphy
  * BIT[4:0] 30ps a step
  *
  * clk_hs_term_sel/data_hs_term_sel -> HS-RX Termination Impedance Control
  * for es3588 dcphy
  * 3b'000 : 102Ω
  * 3b'001 : 99.1Ω
  * 3b'010 : 96.6Ω (default)
  * 3b'011 : 94.1Ω
  * 3b'100 : 113Ω
  * 3b'101 : 110Ω
  * 3b'110 : 107Ω
  * 3b'111 : 104Ω
  */
 
 enum csi2_dphy_vendor {
     PHY_VENDOR_INNO = 0x0,
 };
 
 struct esmodule_csi_dphy_param {
     __u32 vendor;
     __u32 lp_vol_ref;
     __u32 lp_hys_sw[DPHY_MAX_LANE];
     __u32 lp_escclk_pol_sel[DPHY_MAX_LANE];
     __u32 skew_data_cal_clk[DPHY_MAX_LANE];
     __u32 clk_hs_term_sel;
     __u32 data_hs_term_sel[DPHY_MAX_LANE];
     __u32 reserved[32];
 };
 
 struct esmodule_sensor_fmt {
     __u32 sensor_index;
     __u32 sensor_width;
     __u32 sensor_height;
 };
 
 struct esmodule_sensor_infos {
     struct esmodule_sensor_fmt sensor_fmt[ESMODULE_MAX_SENSOR_NUM];
 };
 
 enum esmodule_capture_mode {
     ESMODULE_CAPTURE_MODE_NONE = 0,
     ESMODULE_MULTI_DEV_COMBINE_ONE,
     ESMODULE_ONE_CH_TO_MULTI_ISP,
     ESMODULE_MULTI_CH_TO_MULTI_ISP,
     ESMODULE_MULTI_CH_COMBINE_SQUARE,
 };
 
 struct esmodule_multi_dev_info {
     __u32 dev_idx[ESMODULE_MULTI_DEV_NUM];
     __u32 combine_idx[ESMODULE_MULTI_DEV_NUM];
     __u32 pixel_offset;
     __u32 dev_num;
     __u32 reserved[8];
 };
 
 struct esmodule_one_to_multi_info {
     __u32 isp_num;
     __u32 frame_pattern[ESMODULE_MULTI_DEV_NUM];
 };
 
 struct esmodule_multi_combine_info {
     __u32 combine_num;
     __u32 combine_index[ESMODULE_MULTI_DEV_NUM];
 };
 
 struct esmodule_capture_info {
     __u32 mode;
     union {
         struct esmodule_multi_dev_info multi_dev;
         struct esmodule_one_to_multi_info one_to_multi;
         struct esmodule_multi_combine_info multi_combine_info;
     };
 };
 
 #endif /* _UAPI_ESMODULE_CAMERA_H */
 