// SPDX-License-Identifier: GPL-2.0
/*
 * LT6911UXE Sensor Upgrade Driver Header File for EIC7700 SoC
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 * Authors: Junfa Sun <sunjunfa@eswincomputing.com>
 *          Yulin Lu <luyulin@eswincomputing.com>
 */

#ifndef __LT6911_I2C_H__
#define __LT6911_I2C_H__

#define LT6911_I2C_NAME "LT6911_i2c"
#define LT6911_I2C_DRIVER_VERSION "Ver00250402"
#define LT6911_I2C_RETRY_TIMES 5
#define LT6911_I2C_RMSG_COUNT 2
#define LT6911_I2C_WMSG_COUNT 1
#define LT6911_I2C_W_BUF 100 /* continuous writing 100 byte */
#define LT6911_I2C_SEND_BUF 10
#define LT6911_I2C_CMD_BUF 3

#define LT6911_I2C_UPDATE_BIN

#define LT6911_I2C_CHIP_ID_REG (0x00) /* chip id */
#define LT6911_I2C_VERSION_REG (0x81) /* bin version */
#define LT6911_I2C_EXT_I2C_ACCESS_REG (0xEE)
#define LT6911_I2C_VERSION_BANK_REG (0xE0)
#define LT6911_I2C_CHIP_ID_BANK_REG (0xE1)
#define LT6911_I2C_SWITCH_BANK_REG (0xFF)
#define LT6911_I2C_VERSION_BYTES (3)

#define LT6911_I2C_CRC_VAL 0xA1 /* 0xEB */
#define LT6911_I2C_FIFO_SIZE (64)

#define LT6911_I2C 0xA11
#define LT6911_I2C_IOCTL_CHIP_NAME _IOR(LT6911_I2C, 0x01, int)
#define LT6911_I2C_IOCTL_BIN_VERSION _IOR(LT6911_I2C, 0x02, int)
#define LT6911_I2C_IOCTL_UPDATE_BIN _IOR(LT6911_I2C, 0x03, int)
#define LT6911_I2C_IOCTL_UPDATE_UPDATE_STATUS _IOR(LT6911_I2C, 0x04, int)

#ifdef LT6911_I2C_UPDATE_BIN
/* Need to Know the Flash Size which is used to save the firmware */
#define LONTIUM_FW_MAINAREA_SIZE (32 * 1024)

//#define FW_FILE "/bin/LT6911UXE_VER00250402.bin"
#define FW_FILE "LT6911UXE_VER00250704.bin"
//#define LT6911UXE_FIFO_PAGE_SIZE 32
#define LT6911UXE_SRAM_PAGE_SIZE 256
#define FW_BUFF_SIZE 32768      //32KB Firmware area size

#define UPGRADE  	1
#define NOT_UPGRADE 0
#define UPG_FAIL	0
#define UPG_OK		1

struct crc_info_types_t{
	uint8_t width;
	uint32_t poly;
	uint32_t crc_init;
	uint32_t xor_out;
	bool ref_in;
	bool ref_out;
};

enum {
	LT6911_I2C_UPDATE_STATUS_IDLE = 0,
	LT6911_I2C_UPDATE_STATUS_RUNNING = 1,
	LT6911_I2C_UPDATE_STATUS_COMPLETE = 2,
	LT6911_I2C_UPDATE_STATUS_FAIL = 3,
	LT6911_I2C_UPDATE_STATUS_MAX = 4,
};
#endif

struct lt6911uxe_upgrade {
	struct i2c_client *client;
	struct mutex lock;
	int reset_gpio;
#ifdef LT6911_I2C_UPDATE_BIN
	struct delayed_work upg_work;
	int8_t update_status;
	uint8_t bin_buf[FW_BUFF_SIZE];
	uint32_t bin_size;
	uint32_t download_check_count;
	uint8_t download_code_flag;
#endif

};

bool is_lt6911_inUpgrading(void);
int lt6911_upgrade_init(struct i2c_client *client, struct gpio_desc *rst_gpio);

#endif /* __LT6911_I2C_H__ */
