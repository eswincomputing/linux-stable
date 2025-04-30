#ifndef __VICAP_IOCTL_H__
#define __VICAP_IOCTL_H__

#ifdef __KERNEL__
#include <linux/cdev.h>
#else
#include <stdint.h>
#endif

#include <linux/ioctl.h>

#ifdef __KERNEL__
#define VICAP_DEV_MAXCNT 6

#define BIT_WIDTH_1                1
#define BIT_WIDTH_2                2
#define BIT_WIDTH_3                3

#define VICAP_COMMON_SET           0x00
#define LANE_NUM_LSB               8
#define PHY_NUM_LSB                10
#define COMBO_MODE_LSB             12
#define CROP_EN_LSB                13
#define AIL_ENDIAN_LSB             19
#define BIT_REORDER_LSB            20
#define L_BIT_LSB                  21
#define CAP_EN_LSB                 25

#define VICAP_PIC_RES              0x04
#define VICAP_HDR_ARG1             0x08
#define FRAME_HEIGHT_LSB           16
#define FRAME_HEIGHT_BITS          16

#define VICAP_CROP_X               0x10
#define VICAP_CROP_Y               0x14

#define VICAP_REG_SC1              0x18
#define SC_W1_LSB                  16
#define SC_W2_LSB                  0
#define VICAP_REG_SC2              0x1c
#define SC_W3_LSB                  16
#define SC_W4_LSB                  0
#define VICAP_REG_SC3              0x20
#define SC_W5_LSB                  16
#define SC_W6_LSB                  0
#define VICAP_REG_SC4              0x24
#define SC_W7_LSB                  16
#define SC_W8_LSB                  0
#define VICAP_REG_SC5              0x28
#define SC_W9_LSB                  16
#define SC_W10_LSB                 0
#define VICAP_REG_SC6              0x2c
#define SC_W11_LSB                 16
#define SC_W12_LSB                 0
#define VICAP_REG_SC7              0x30
#define SC_W13_LSB                 16
#define SC_W14_LSB                 0
#define VICAP_REG_SC8              0x34
#define SC_W15_LSB                 16
#define SC_W16_LSB                 0
#define SYNC_CODE_BITS             16

#define VICAP_SLVS_ARG             0x3c
#define OB_LINE_NUM_LSB            0
#define INFO_LINE_NUM_LSB          8
#define OB_LINE_EN_LSB             16
#define INFO_LINE_EN_LSB           20
#define SLVS_MODE_LSB              24
#define SLVS_5WORD_LSB             25
#define SLVS_MAP_LSB               26
#define LINE_NUM_BITS              8
#define LINE_EN_BITS               3

#define VICAP_HISPI_ARG            0x40
#define CRC_EN_LSB                 4
#define FLR_EN_LSB                 5 
#define HISPI_MODE_LSB             0
#define HISPI_MODE_BITS            4


#define VICAP_PHY_CTRL_CFG8        0xa0
#define VIN_SOFT_RST_LSB           25
#define VIN_UNLOCK_RST_FIFO_LSB    24
#define VIN_LANE_ENABLE_LSB        16
#define VIN_SC_BYTE_ORDOR_LSB      15
#define VIN_LANE_EN_BITS           8

#define VICAP_SW_RESET             0xc4
#endif

#define SLVS_SYNC_CODE_WORD 4

enum {
	VICAP_IOC_S_RESET_CTRL = _IO('v', 0), // Control reset, no register
	VICAP_IOC_S_DEV_ATTR,
	VICAP_IOC_S_DEV_ENABLE,
	VICAP_IOC_S_DEV_DISABLE,

	VICAP_IOC_MAX,
};

typedef enum _dev_mode {
	DEV_MODE_MASTER,
	DEV_MODE_SLAVE,
	DEV_MODE_BUTT,
} dev_mode_t;

typedef enum _phy_mode {
	PHY_MODE_LANE_8_4,
	PHY_MODE_LANE_8_2_2,
	PHY_MODE_LANE_4_4_4,
	PHY_MODE_LANE_4_4_2_2,
	PHY_MODE_LANE_4_2_2_2_2,
	PHY_MODE_LANE_2_2_2_2_2_2,
	PHY_MODE_BUTT,
} phy_mode_t;

typedef enum _combo_mode {
	MODE_HISPI = 0,
	MODE_SLVS_SUBLVDS,
	MODE_BUTT,
} combo_mode_t;

typedef struct _frame_info {
	uint32_t width;
	uint32_t height;
	uint32_t h_pad;
	uint32_t v_pad;
} frame_info_t;

typedef struct _crop_info {
	uint32_t crop_en;
	uint32_t crop_x1;
	uint32_t crop_x2;
	uint32_t crop_y1;
	uint32_t crop_y2;
} crop_info_t;

typedef enum _data_type {
	DATA_TYPE_RAW_8BIT = 0,
	DATA_TYPE_RAW_10BIT,
	DATA_TYPE_RAW_12BIT,
	DATA_TYPE_RAW_14BIT,
	DATA_TYPE_RAW_16BIT,
	DATA_TYPE_BUTT,
} data_type_t;

typedef enum _data_endian {
	DATA_ENDIAN_LSB = 0,
	DATA_ENDIAN_MSB,
	DATA_ENDIAN_BUTT,
} data_endian_t;

typedef enum _slvs_sync_mode {
	SLVS_SYNC_MODE_SAV = 0, // SAV, EAV
	SLVS_SYNC_MODE_SOF, // SOL, EOL, SOF, EOF
	SLVS_SYNC_MODE_BUTT,
} slvs_sync_mode_t;

typedef struct {
	slvs_sync_mode_t sync_mode; // sync mode: SOF, SAV
	uint16_t info_line_en;
	uint16_t info_line_num;
	uint16_t ob_line_en;
	uint16_t ob_line_num;
	// 0: sav 1: eav 2: sav(invalid) 3: eav(invalid)
	// 0: sol 1: eol 2: sof 3: eof
	uint16_t sync_code[SLVS_SYNC_CODE_WORD];
} slvs_dev_attr_t;

typedef enum {
	HISPI_MODE_PACKETIZED_SP = 0x1,
	HISPI_MODE_STREAMING_SP = 0x2,
	HISPI_MODE_STREAMING_S = 0x4,
	HISPI_MODE_ACTIVESTART_SP8 = 0x8,
	HISPI_MODE_BUTT,
} hispi_mode_t;

typedef struct {
	hispi_mode_t hispi_mode;
	uint16_t flr_en;
	uint16_t crc_en;
	data_endian_t ail_endian;
	// Packetized SP Mode: 0: SOF 1: SOL 2: EOF 3: EOL
	// HISPI_MODE_STREAMING_SP: 0: SOF 1: SOL 2: SOV
	// HISPI_MODE_ACTIVESTART_SP8: 0~7: SOF word1~8, 8~15: SOL word1~8
	union {
		uint16_t pack_sp_sc[4];
		uint16_t stream_sp_sc[3];
		uint16_t act_sp_sc[16];
	} sc;
} hispi_dev_attr_t;

typedef struct vicap_dev_attr {
	combo_mode_t combo_mode;
	data_type_t data_type;
	data_endian_t data_endian;
	frame_info_t frame_info;
	crop_info_t crop_info;
	phy_mode_t phy_mode;

	union {
		slvs_dev_attr_t slvs_attr; // slvs or sub-lvds
		hispi_dev_attr_t hispi_attr; // hispi mode
	} v;
} vicap_dev_attr_t;

#ifdef __KERNEL__

typedef struct _vicap_dev_info {
	uint32_t id;
	uint32_t enable;
	uint32_t phy_num;
	dev_mode_t dev_mode;
	vicap_dev_attr_t dev_attr;
} vicap_dev_info_t;

struct vicap_device {
	void __iomem *base;
	struct cdev cdev;
	dev_t devt;
	uint32_t id;
	struct mutex vicap_mutex;
	struct class *class;
	void *private;
};

long vicap_priv_ioctl(struct vicap_device *dev, unsigned int cmd, void __user *args);

#else
//User space connections

#endif

#endif // __VICAP_IOCTL_H__
