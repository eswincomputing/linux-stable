// SPDX-License-Identifier: GPL-2.0
/*
 * LT6911UXE Sensor Upgrade Driver for EIC7700 SoC 
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 * Authors: Junfa Sun <sunjunfa@eswincomputing.com>
 *          Yulin Lu <luyulin@eswincomputing.com>
 */

#include <linux/kernel.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c-dev.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <asm/setup.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/printk.h>
#include <linux/workqueue.h>

#include <linux/major.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/kmod.h>
#include <linux/gfp.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/types.h>

#include "lt6911uxe_upgrade.h"

#define DEBUG_ON_OFF
#if defined(DEBUG_ON_OFF)
#define lt6911_i2c_dbg(fmt, arg...) \
printk(fmt, ##arg)
#else
#define lt6911_i2c_dbg(...)
#endif

static int cur_chip = 0;
static int iap_length = 0;
static char cur_version[7] = "";
static char iap_version[7] = "";
static struct i2c_client *lt_i2c_client;

#define LT6911_GPIOD_RESET

#ifdef LT6911_GPIOD_RESET
struct gpio_desc *reset_gpio;
#else
int reset_gpio;
#endif

u8 *fw_buffer;	// 32KB buffer
const struct firmware *fw = NULL;
struct lt6911uxe_upgrade upg_state;

#ifdef LT6911_I2C_UPDATE_BIN
atomic_t is_lt6911_upgrading = ATOMIC_INIT(0);
struct delayed_work upg_work;
#endif

#define WAIT_FOR_UPGRADE_COMPLETE() \
		do { \
			while (atomic_read(&is_lt6911_upgrading)) { \
				msleep(50);  \
			} \
		} while(0)

static int lt6911_i2c_read_bytes(u8 reg_addr, u8 *buf, s32 len)
{
	s32 ret = -1;
	s32 retries = 0;
	struct i2c_msg msgs[LT6911_I2C_RMSG_COUNT];
	struct i2c_client *client = lt_i2c_client;

	msgs[0].addr = client->addr;
	msgs[0].flags = !I2C_M_RD; /* write */
	msgs[0].len = 1;
	msgs[0].buf = &reg_addr;

	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD; /* read */
	msgs[1].len = len;
	msgs[1].buf = buf;

	while (retries < LT6911_I2C_RETRY_TIMES) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret == ARRAY_SIZE(msgs)) {
			return 1;
		}
		retries++;
		msleep(10);
	}

	if (retries >= LT6911_I2C_RETRY_TIMES) {
		pr_err("I2C Read: %d bytes failed, errcode: %d!\n", len, ret);
	}

	return 0;
}

#if 0
static int lt6911_i2c_write_bytes(u8 reg_addr, u8 *buf, s32 len)
{
	s32 ret = -1;
	s32 retries = 0;
	struct i2c_msg msgs[LT6911_I2C_WMSG_COUNT];
	u8 w_buf[LT6911_I2C_W_BUF] = {0};
	struct i2c_client *client = lt_i2c_client;

	w_buf[0] = reg_addr & 0xFF;
	memcpy(&w_buf[1], buf, len);

	msgs[0].addr = client->addr;
	msgs[0].flags = !I2C_M_RD; /* write */
	msgs[0].len = len + 1;
	msgs[0].buf = w_buf;

	while (retries < LT6911_I2C_RETRY_TIMES) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret ==  ARRAY_SIZE(msgs)) {
			return 1;
		}
		retries++;
		msleep(10);
	}

	if (retries >= LT6911_I2C_RETRY_TIMES) {
		pr_err("I2C Write: 0x%02X, %d bytes failed, errcode: %d!\n", buf[0], len - 1, ret);
	}

	return 0;
}
#endif

static int lt6911_i2c_write_byte(u8 reg_addr, u8 buf)
{
	s32 ret = -1;
	s32 retries = 0;
	struct i2c_msg msgs[LT6911_I2C_WMSG_COUNT];
	u8 w_buf[2] = {0};
	struct i2c_client *client = lt_i2c_client;

	w_buf[0] = reg_addr & 0xFF;
	w_buf[1] = buf;

	msgs[0].addr = client->addr;
	msgs[0].flags = !I2C_M_RD; /* write */
	msgs[0].len = 2;
	msgs[0].buf = w_buf;

	while (retries < LT6911_I2C_RETRY_TIMES) {
		ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
		if (ret ==  ARRAY_SIZE(msgs)) {
			return 1;
		}
		retries++;
		msleep(10);
	}

	if (retries >= LT6911_I2C_RETRY_TIMES) {
		pr_err("I2C Write: 0x%02X failed, errcode: %d! \n", buf, ret);
	}

	return 0;
}

#ifdef LT6911_GPIOD_RESET
void lt6911_reset(void)
{
	if (reset_gpio) {
		gpiod_set_value(reset_gpio, 1);
		usleep_range(2000, 2100);
		gpiod_set_value(reset_gpio, 0);
		usleep_range(120*1000, 121*1000);
		gpiod_set_value(reset_gpio, 1);
		usleep_range(300*1000, 310*1000);

		lt6911_i2c_dbg("LT6911UXE: reset chip");
	}
}
#else
void lt6911_reset(void)
{
	if (gpio_is_valid(reset_gpio)) {
		gpio_set_value(reset_gpio,1);
		msleep(5);
		gpio_set_value(reset_gpio,0);
		msleep(100);
		gpio_set_value(reset_gpio,1);
		msleep(5);

		lt6911_i2c_dbg("LT6911UXE: reset chip");
	}
}
#endif

static int lt6911_i2c_get_chip_name(u8 *buf)
{
	lt6911_i2c_dbg("==>%s \n", __func__);

	if (buf == NULL)
		return -1;

	lt6911_i2c_write_byte(LT6911_I2C_SWITCH_BANK_REG, LT6911_I2C_VERSION_BANK_REG);
	lt6911_i2c_write_byte(LT6911_I2C_EXT_I2C_ACCESS_REG, 0x01);
	lt6911_i2c_write_byte(LT6911_I2C_SWITCH_BANK_REG, LT6911_I2C_CHIP_ID_BANK_REG);
	lt6911_i2c_read_bytes(LT6911_I2C_CHIP_ID_REG, buf, 2);
	lt6911_i2c_write_byte(LT6911_I2C_SWITCH_BANK_REG, LT6911_I2C_VERSION_BANK_REG);
	lt6911_i2c_write_byte(LT6911_I2C_EXT_I2C_ACCESS_REG, 0x00);

	upg_state.update_status = LT6911_I2C_UPDATE_STATUS_IDLE;
	cur_chip = ((cur_chip << 8) | buf[0]);
	cur_chip = ((cur_chip << 8) | buf[1]);
	return cur_chip;
}

static int lt6911_i2c_get_cur_version(u8 *buf)
{
	int ret = 0;
	lt6911_i2c_dbg("==>%s \n", __func__);

	if (buf == NULL)
		return -1;

	ret = lt6911_i2c_write_byte(LT6911_I2C_SWITCH_BANK_REG, LT6911_I2C_VERSION_BANK_REG);
	ret = lt6911_i2c_read_bytes(LT6911_I2C_VERSION_REG, buf, LT6911_I2C_VERSION_BYTES);
	sprintf(cur_version, "%02x%02x%02x", buf[0], buf[1], buf[2]);
	pr_notice("==>cur_version %s \n", cur_version);
	return ret;
}

void lt6911_i2c_enable(void)
{
	lt6911_i2c_write_byte(0xff, 0xe0);
	lt6911_i2c_write_byte(0xee, 0x01);
}

void lt6911_i2c_disable(void)
{
	lt6911_i2c_write_byte(0xff, 0xe0);
	lt6911_i2c_write_byte(0xee, 0x00);
}

void lt6911_Wren(void)
{
    lt6911_i2c_write_byte(0xFF, 0XE1);
	lt6911_i2c_write_byte(0x03, 0X2E);
	lt6911_i2c_write_byte(0x03, 0XEE);
	lt6911_i2c_write_byte(0xFF, 0XE0);
	lt6911_i2c_write_byte(0x5A, 0X04);
	lt6911_i2c_write_byte(0x5A, 0X00);
}

void lt6911_Wrdi(void)
{
	lt6911_i2c_write_byte(0x5A, 0X08);
	lt6911_i2c_write_byte(0x5A, 0X00);
}

#ifdef LT6911_I2C_UPDATE_BIN
#if 0
static int lt6911_i2c_read_bytes_compare(u8 reg, uint8_t *buf, size_t rx_len)
{
	int ret;
	int retry = 0;
	char rxbuf[32];
	size_t len = rx_len;
	struct lt6911uxe_upgrade *data = &upg_state;

	//lt6911_i2c_dbg("==>%s \n", __func__);

	if (unlikely(!rx_len)) {
		return 0;
	}

    /* retry 5 times */
	for (retry = 0; retry < 5; retry++) {
		memset(rxbuf, 0, 32);
		lt6911_i2c_read_bytes(reg, rxbuf, len);

		for (int i = 0; i < 2; i++) {
			printk("rxbuf[%d]:0x%x", i, rxbuf[i]);
		}

		ret = memcmp(buf, rxbuf, 32);
		if (ret) {
			data->download_check_count++;
			pr_err("lt6911_i2c_read_bytes_compare: failed %d \n", data->download_check_count);
			msleep(10);
			continue;
		}

		data->download_check_count = 0;
		break;
	}

	return ret;
}
#endif

static uint32_t lt6911_i2c_bits_reverse(uint32_t in_val, uint8_t bits)
{
	uint32_t out_val = 0;
	uint8_t i = 0;

	lt6911_i2c_dbg("==>%s \n", __func__);

	for (i = 0; i< bits; i++) {
		if (in_val & (1 << i))
			out_val |= 1 << (bits - 1 - i);
	}

	return out_val;
}

static uint32_t lt6911_i2c_get_crc(struct crc_info_types_t type, const u8 *buf, uint32_t buf_len)
{
	uint8_t width = type.width;
	uint32_t poly = type.poly;
	uint32_t crc = type.crc_init;
	uint32_t xor_out = type.xor_out;
	bool ref_in  = type.ref_in;
	bool ref_out = type.ref_out;
	uint8_t n = 0;
	uint32_t bits = 0;
	uint32_t data = 0;
	uint8_t i = 0;

	//lt6911_i2c_dbg("==>%s \n", __func__);

	n    =  (width < 8) ? 0 : (width - 8);
	crc  =  (width < 8) ? (crc << (8 - width)) : crc;
	bits =  (width < 8) ? 0x80 : (1 << (width - 1));
	poly =  (width < 8) ? (poly << (8 - width)) : poly;

	while (buf_len--) {
		data = *(buf++);
		if (ref_in == TRUE)
			data = lt6911_i2c_bits_reverse(data, 8);

		crc ^= (data << n);
		for (i = 0; i < 8; i++)
			if(crc & bits)
				crc = (crc << 1) ^ poly;
			else
				crc = crc << 1;
	}

	crc = (width < 8) ? (crc >> (8 - width)) : crc;
	if (ref_out == TRUE)
		crc = lt6911_i2c_bits_reverse(crc, width);
	crc ^= xor_out;

	return (crc & ((2 << (width - 1)) - 1));
}

static uint8_t lt6911_i2c_get_main_crc(const u8 *upgrade_data, uint32_t len)
{
	uint32_t crc_size = LONTIUM_FW_MAINAREA_SIZE - 1;
	uint8_t default_val = 0xFF;

	lt6911_i2c_dbg("==>%s, len = %d\n", __func__, len);

	struct crc_info_types_t type = {
		.width = 8,
		.poly  = 0x31,
		.crc_init = 0,
		.xor_out = 0,
		.ref_out = FALSE,
		.ref_in = FALSE,
	};

	type.crc_init = lt6911_i2c_get_crc(type, upgrade_data, len);

	crc_size -= len;
	lt6911_i2c_dbg("==>%s, crc_size = %d\n", __func__, crc_size);
	while (crc_size--)
		type.crc_init = lt6911_i2c_get_crc(type, &default_val, 1);

	return type.crc_init;
}

static void lt6911_i2c_to_flash_config(void)
{
	lt6911_i2c_dbg("==>%s \n", __func__);

	lt6911_i2c_write_byte(0xFF, 0xE0); /* configure parameter */
	lt6911_i2c_write_byte(0xEE, 0x01);
	lt6911_i2c_write_byte(0x5E, 0xC1);
	lt6911_i2c_write_byte(0x58, 0x00);
	lt6911_i2c_write_byte(0x59, 0x50);
	lt6911_i2c_write_byte(0x5A, 0x10);
	lt6911_i2c_write_byte(0x5A, 0x00);
	lt6911_i2c_write_byte(0x58, 0x21);
}

static void lt6911_i2c_block_erase_32k(u32 addr)
{
	u8 addrs[3] = { 0, 0, 0 };

	lt6911_i2c_dbg("==>%s \n", __func__);

	lt6911_i2c_write_byte(0xFF, 0xe0);
	lt6911_i2c_write_byte(0x5a, 0x04);
	lt6911_i2c_write_byte(0x5A, 0x00);
	addrs[0] = ( addr & 0xFF0000 ) >> 16;
	addrs[1] = ( addr & 0xFF00 ) >>	8;
	addrs[2] = ( addr & 0xFF);
	lt6911_i2c_write_byte(0x5B, addrs[0]);		//  16bit
	lt6911_i2c_write_byte(0x5C, addrs[1]);		//	8bit
	lt6911_i2c_write_byte(0x5D, addrs[2]);		//	0bit
	lt6911_i2c_write_byte(0x5A, 0x01);
	lt6911_i2c_write_byte(0x5A, 0x00);

	lt6911_i2c_write_byte(0xff,0xe1);
	lt6911_i2c_write_byte(0x03,0x3f);
	lt6911_i2c_write_byte(0x03,0xff);
	lt6911_i2c_write_byte(0xff,0xe0);
	lt6911_i2c_write_byte(0x5e,0x40);
	lt6911_i2c_write_byte(0x56,0x05);
	lt6911_i2c_write_byte(0x55,0x25);
	lt6911_i2c_write_byte(0x55,0x01);
	lt6911_i2c_write_byte(0x58,0x21);

	msleep(500);
	printk("Block Erase End");
}

#if 0
static int lt6911_i2c_read_flash(uint8_t *firmware, int filesize)
{
	u8 addr1, addr2, addr3;
	uint32_t count = 0;
	uint32_t flash_add = 0;
	struct lt6911uxe_upgrade *data = &upg_state;

	lt6911_i2c_dbg("==>%s \n", __func__);

	count = 0;
	flash_add = 0;
	data->download_check_count = 0;

	lt6911_i2c_enable();
	//Configure Parameters
	lt6911_i2c_to_flash_config();

	//lt6911_i2c_write_byte(0xFF, 0xE0);
	//lt6911_i2c_write_byte(0xEE, 0x01);
	while((count + 32) < filesize) {
		addr1 = flash_add;
		addr2 = flash_add >> 8;
		addr3 = flash_add >> 16;
		lt6911_i2c_write_byte(0x5E, 0x5F);
		lt6911_i2c_write_byte(0x5A, 0x20);
		lt6911_i2c_write_byte(0x5A, 0x00);
		lt6911_i2c_write_byte(0x5B, addr3);
		lt6911_i2c_write_byte(0x5C, addr2);
		lt6911_i2c_write_byte(0x5D, addr1);
		lt6911_i2c_write_byte(0x5A, 0x10);
		lt6911_i2c_write_byte(0x5A, 0x00);
		lt6911_i2c_write_byte(0x58, 0x21);

		lt6911_i2c_read_bytes_compare(0x5F, firmware + count, 32);
		flash_add = flash_add + 0x20;
		count = count + 32;
		if (data->download_check_count > 0) {
			data->download_code_flag = 0;
			data->update_status = LT6911_I2C_UPDATE_STATUS_FAIL;
			pr_notice("LT6911 Download Code Fail\n");
			return UPG_FAIL;
		}

		msleep(1);
	}

	if (filesize - count > 0) {
		addr1 = flash_add;
		addr2 = flash_add >> 8;
		addr3 = flash_add >> 16;
		lt6911_i2c_write_byte(0x5E, 0x5F);
		lt6911_i2c_write_byte(0x5A, 0x20);
		lt6911_i2c_write_byte(0x5A, 0x00);
		lt6911_i2c_write_byte(0x5B, addr3);
		lt6911_i2c_write_byte(0x5C, addr2);
		lt6911_i2c_write_byte(0x5D, addr1);
		lt6911_i2c_write_byte(0x5A, 0x10);
		lt6911_i2c_write_byte(0x5A, 0x00);
		lt6911_i2c_write_byte(0x58, 0x21);
		lt6911_i2c_write_byte(0x5A, 0x10);
		lt6911_i2c_write_byte(0x5A, 0x00);
		lt6911_i2c_write_byte(0x58, 0x21);
		lt6911_i2c_read_bytes_compare(0x5F, firmware + count, filesize - count);
		msleep(1);
	}

	//WRDI
	lt6911_i2c_write_byte(0xFF, 0xE0);
	lt6911_i2c_write_byte(0x5a, 0x08);
	lt6911_i2c_write_byte(0x5a, 0x00);

	if (data->download_check_count == 0) {
		data->download_code_flag = 1;
		data->update_status = LT6911_I2C_UPDATE_STATUS_COMPLETE;
		pr_notice("LT6911 Download Code Success !!!");
		return UPG_OK;
	} else {
		data->download_code_flag = 0;
		data->update_status = LT6911_I2C_UPDATE_STATUS_FAIL;
		pr_notice("LT6911 Download Code Fail !!!");
		return UPG_FAIL;
	}
}
#endif

int lt6911_upgrade_judgment(void)
{
    u8 read_flash_CRC_Value;
	u32 addr = FW_BUFF_SIZE-1;

    //	Configure Parameters
	lt6911_i2c_to_flash_config();

	//Flash to FIFO
	lt6911_i2c_write_byte(0x5E, 0X5F);
	lt6911_i2c_write_byte(0x5A, 0x20);
	lt6911_i2c_write_byte(0x5A, 0x00);
	lt6911_i2c_write_byte(0x5B, ((addr & 0xFF0000) >> 16) );
	lt6911_i2c_write_byte(0x5C, ((addr & 0xFF00) >> 8));
	lt6911_i2c_write_byte(0x5D, (addr & 0xFF));
	lt6911_i2c_write_byte(0x5A, 0x10);
	lt6911_i2c_write_byte(0x5A, 0x00);

	//FIFO to I2C
	lt6911_i2c_write_byte(0x58, 0x21);

	lt6911_i2c_read_bytes(0x5f, &read_flash_CRC_Value, 1);
    msleep(1);

	//WRDI
	lt6911_i2c_write_byte(0xFF, 0xE0);
	lt6911_i2c_write_byte(0x5a, 0x08);
	lt6911_i2c_write_byte(0x5a, 0x00);

	lt6911_i2c_dbg("read Flash CRC is: 0x%02x", read_flash_CRC_Value);

	//compare the CRC
    if(fw_buffer[FW_BUFF_SIZE - 1] == read_flash_CRC_Value){
		return NOT_UPGRADE;
	} else {
        return UPGRADE;
    }
}

void lt6911_isUpgradeSuccess(void)
{
	u8 read_flash_CRC_Value;
	u32 addr = FW_BUFF_SIZE-1;

	//Configure Parameters
	lt6911_i2c_to_flash_config();

	//Flash to FIFO
	lt6911_i2c_write_byte(0x5E, 0X5F);
	lt6911_i2c_write_byte(0x5A, 0x20);
	lt6911_i2c_write_byte(0x5A, 0x00);
	lt6911_i2c_write_byte(0x5B, ((addr & 0xFF0000) >> 16) );
	lt6911_i2c_write_byte(0x5C, ((addr & 0xFF00) >> 8));
	lt6911_i2c_write_byte(0x5D, (addr & 0xFF));
	lt6911_i2c_write_byte(0x5A, 0x10);
	lt6911_i2c_write_byte(0x5A, 0x00);

	//FIFO to I2C
	lt6911_i2c_write_byte(0x58, 0x21);
	lt6911_i2c_read_bytes(0x5f, &read_flash_CRC_Value, 1);
	msleep(1);

	//WRDI
	lt6911_i2c_write_byte(0xFF, 0xE0);
	lt6911_i2c_write_byte(0x5a, 0x08);
	lt6911_i2c_write_byte(0x5a, 0x00);

	if (fw_buffer[FW_BUFF_SIZE - 1] == read_flash_CRC_Value)
	{
		lt6911_i2c_dbg("Upgrade is success.\n");
	} else {
		lt6911_i2c_dbg("Upgrade is failure.\n");
	}
}

int lt6911_prepare_firmwaredata(void)
{
	int ret;

    // Request the firmware file from the /lib/firmware directory
	ret = request_firmware(&fw, FW_FILE, &lt_i2c_client->dev);
	if (ret) {
		lt6911_i2c_dbg("Failed to load firmware: %d\n", ret);
		return ret;
	}

	lt6911_i2c_dbg("Firmware loaded, size: %zu bytes\n", fw->size);

	if (fw->size > FW_BUFF_SIZE - 1) {
		lt6911_i2c_dbg("Firmware size exceeds 32KB limit\n");
		release_firmware(fw);
		return -1;
	}

	if (fw->size < 0x5000) {
		lt6911_i2c_dbg("Firmware size too small\n");
		release_firmware(fw);
		return -1;
	}

	fw_buffer = kzalloc(FW_BUFF_SIZE, GFP_KERNEL);
	if (!fw_buffer) {
		lt6911_i2c_dbg("Failed to allocate firmware buffer\n");
		release_firmware(fw);
		return -1;
	}

	memcpy(fw_buffer, fw->data, fw->size);

	memset(fw_buffer + fw->size, 0xFF, FW_BUFF_SIZE - fw->size - 1);

	fw_buffer[FW_BUFF_SIZE - 1] = lt6911_i2c_get_main_crc(fw->data, fw->size);
	lt6911_i2c_dbg("LT6911UXE.bin Firmware CRC: 0x%02X\n", fw_buffer[FW_BUFF_SIZE - 1]);
	sprintf(iap_version, "%02x%02x%02x", fw_buffer[4096+3], fw_buffer[4096+4], fw_buffer[4096+5]);
	lt6911_i2c_dbg("LT6911UXE.bin Firmware version: %s \n", iap_version);
	iap_length = fw->size;
	release_firmware(fw);
	return 0;
}

int lt6911_firmware_upgrade(const u8 *pfile, u16 filesize, u64 addr)
{
	int page = 0, i = 0, num = 0;

	lt6911_i2c_to_flash_config();
	lt6911_i2c_block_erase_32k(0);
	msleep(100);

	page = (filesize % LT6911UXE_SRAM_PAGE_SIZE) ? ((filesize / LT6911UXE_SRAM_PAGE_SIZE) + 1) : (filesize / LT6911UXE_SRAM_PAGE_SIZE);
	if (page*LT6911UXE_SRAM_PAGE_SIZE > 32*1024)   //more than 32K, return
	{
		lt6911_i2c_dbg("\nFile size is out of range!");
		return UPG_FAIL;
	}

	lt6911_i2c_dbg("Writing to SRAM: %u pages, total size = %u bytes\n", page, filesize);
	lt6911_i2c_write_byte(0xff, 0xe0);
	lt6911_i2c_write_byte(0xee, 0x01);
	for (num = 0; num < page; num++) {

		lt6911_i2c_write_byte(0x55, 0x80);
		lt6911_i2c_write_byte(0x5e, 0xc0);
		lt6911_i2c_write_byte(0x58, 0x21);

		 //write data to sram
		 for (i = 0; i < LT6911UXE_SRAM_PAGE_SIZE; i++)
		 {
			 if ((num*LT6911UXE_SRAM_PAGE_SIZE + i) < filesize) {
				 if (lt6911_i2c_write_byte(0x59, *(pfile + (num * LT6911UXE_SRAM_PAGE_SIZE + i))) == 0) {
					 lt6911_i2c_dbg("Error writing data at page %u, index %u\n", num, i);
					 return UPG_FAIL;
				 }
			 } else {
				 lt6911_i2c_write_byte(0x59,0XFF);
			 }
		 }

		 lt6911_i2c_write_byte(0x5A, 0x04);
		 lt6911_i2c_write_byte(0x5A, 0x00);
		 lt6911_i2c_write_byte(0x5A, 0x30);
		 lt6911_i2c_write_byte(0x5A, 0x00);
		 usleep_range(150, 200);
	}

	//wrdi
	lt6911_Wrdi();
	lt6911_i2c_dbg("Write Data end");
	return UPG_OK;
}

int lt6911_firmware_upgrade_andcheck(const u8 *pfile, u16 filesize, u64 addr)
{
	u8 ret, retry = 0;
	u8 buf[LT6911_I2C_VERSION_BYTES] = {0};
	struct lt6911uxe_upgrade *data = &upg_state;

	lt6911_i2c_dbg("==>%s \n", __func__);

	while (retry < 5) {
		data->update_status = LT6911_I2C_UPDATE_STATUS_RUNNING;
		lt6911_i2c_dbg("lt6911 start upgrade : %d \r\n", retry);
		retry++;
		ret = lt6911_firmware_upgrade(pfile, filesize, addr);
		if (ret == UPG_FAIL)
			continue;

		lt6911_reset();

		//new version
		lt6911_i2c_get_cur_version(buf);
		lt6911_i2c_dbg("new version is %s \n", cur_version);
		if (strcmp(cur_version, iap_version) == 0) {
			pr_notice("Upgrade is success. !!! \n");
			return UPG_OK;
		}

		//if (lt6911_i2c_read_flash(pfile, filesize) == UPG_OK)
		//	return UPG_OK;
		//lt6911_isUpgradeSuccess();
	}

	lt6911_i2c_dbg("Upgrade failure");
	return UPG_FAIL;
}

int lt6911_upgrade(void)
{
	printk("$$$$$ test_0918 lt6911_upgrade_1 $$$$$\n");
	int ret;
	bool isValid = 1;
	u8 buf[LT6911_I2C_VERSION_BYTES] = {0};

	ret = lt6911_i2c_get_cur_version(buf);
	if (ret == 0) {
		pr_err("Failed to get version \n");
		return UPG_FAIL;
	}

	ret = lt6911_prepare_firmwaredata();
	if (ret < 0) {
	    lt6911_i2c_dbg("Failed to prepare firmware data: %d\n", ret);
		return UPG_FAIL;
	}

	// ret = strncasecmp(iap_version, cur_version, strlen("000000"));
	// if (ret > 0) {
	// 	// check old version, is valid
	// 	if ((strcmp(cur_version, "000000") == 0) ||
	// 		(strcmp(cur_version, "250401") == 0) ||
	// 		(strcmp(cur_version, "250402") == 0)) {
	// 		pr_notice("lt6911 needUpgrade!!! \n");
	// 		isValid = 1;
	// 	}

	// 	// check iap version, is valid
	// 	if ((strcmp(iap_version, "250704") != 0)) {
	// 		pr_notice("iap ver invalid!!! \n");
	// 		isValid = 0;
	// 	}
	// }

	if (!isValid) {
		kfree(fw_buffer);
		pr_err("lt6911 no needUpgrade!!! \n");
		return UPG_FAIL;
	}

	ret = lt6911_upgrade_judgment();
	if (ret == UPGRADE) {
		lt6911_i2c_dbg("The old version is %s \n", cur_version);
		lt6911_i2c_dbg("The CRC is different, need to upgrade the firmware");
		ret = lt6911_firmware_upgrade_andcheck(fw_buffer, FW_BUFF_SIZE,0);
	} else if (ret == NOT_UPGRADE){
		printk("CRC is same, not need update");
	} else {
		printk("upgrade judgment failure");
	}

	kfree(fw_buffer);
	return ret;
}

static void lt6911_update_work(struct work_struct *work)
{
    //struct lt6911uxe_state *s = container_of(work, struct lt6911uxe_state, upg_work.work);

	lt6911_i2c_dbg("==>%s \n", __func__);

	if (atomic_read(&is_lt6911_upgrading) == 1)
		return;

	atomic_set(&is_lt6911_upgrading, 1);
    msleep(200);

	lt6911_upgrade();
	lt6911_i2c_disable();

	atomic_set(&is_lt6911_upgrading, 0);

	//data = container_of(work, struct lt6911_i2c_data, work);
	//lt6911_i2c_transfer_file(data->bin_buf, data->bin_size);
}
#endif

bool is_lt6911_inUpgrading(void)
{
	WAIT_FOR_UPGRADE_COMPLETE();

	return (atomic_read(&is_lt6911_upgrading) == 1);
}

int lt6911_upgrade_init(struct i2c_client *client, struct gpio_desc *rst_gpio)
{
	u8 buf[2] = {0};

	lt6911_i2c_dbg("==>%s \n", __func__);

	lt_i2c_client = client;
	reset_gpio = rst_gpio;
	atomic_set(&is_lt6911_upgrading, 0);

	//lt6911_reset();

	lt6911_i2c_get_chip_name(buf);
	lt6911_i2c_dbg("==>chip_name = 0x%02x%02x\n", buf[0], buf[1]);

	INIT_DELAYED_WORK(&upg_work, lt6911_update_work);
	msleep(20);
	schedule_delayed_work(&upg_work, msecs_to_jiffies(5 * 1000));
	return 0;
}
