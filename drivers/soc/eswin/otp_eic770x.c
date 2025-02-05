// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN OTP driver
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
 * Authors: Huangyifeng <huangifeng@eswincomputing.com>
 */

/**
 * Include files
 */
// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN EIC770x SOC OTP  Driver
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
 * Authors: HuangYiFeng<huangyifeng@eswincomputing.com>
 */
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/types.h>
#include "otp_eic770x.h"

void __iomem *crypto_otpc_io_base = NULL;

const struct OTP_MAP_ITEM otp_map_eic770x[] = {
	{ OTP_RSA_PUBLIC_KEY, OTP_RSA_PUBLIC_KEY_ADDR, OTP_RSA_PUBLIC_KEY_SIZE,
		"rsa_public_key", 0 },

	{ OTP_EXPONENT, OTP_EXPONENT_ADDR, OTP_EXPONENT_SIZE,
		"exponent", 0 },

	{ OTP_MEMORY_TO_MEMORY_SECRET_KEY, OTP_MEMORY_TO_MEMORY_SECRET_KEY_ADDR,
		OTP_MEMORY_TO_MEMORY_SECRET_KEY_SIZE,
		"memory_to_memory_secret_key", 0 },

	{ OTP_BOOT_IMAGE_DECRYPTION_KEY, OTP_BOOT_IMAGE_DECRYPTION_KEY_ADDR,
		OTP_BOOT_IMAGE_DECRYPTION_KEY_SIZE,
		"boot_image_decryption_key", 0 },

	{ OTP_WHOLE_MEMORY_ENCRYPTION_KEY, OTP_WHOLE_MEMORY_ENCRYPTION_KEY_ADDR,
		OTP_WHOLE_MEMORY_ENCRYPTION_KEY_SIZE,
		"whole_memory_encryption_key", 0 },

	{ OTP_SECRET_KEY_ARRAY, OTP_SECRET_KEY_ARRAY_ADDR,
		OTP_SECRET_KEY_ARRAY_SIZE,
		"secret_key_array", 0 },

	{ OTP_ECDSA_256BIT_KEY_X, OTP_ECDSA_256BIT_KEY_X_ADDR,
		OTP_ECDSA_256BIT_KEY_X_SIZE,
		"ecdsa_256bit_key_x", 0 },

	{ OTP_ECDSA_256BIT_KEY_Y, OTP_ECDSA_256BIT_KEY_Y_ADDR,
		OTP_ECDSA_256BIT_KEY_Y_SIZE,
		"ecdsa_256bit_key_y", 0 },

	{ OTP_ECDSA_256BIT_KEY_Z, OTP_ECDSA_256BIT_KEY_Z_ADDR,
		OTP_ECDSA_256BIT_KEY_Z_SIZE,
		"ecdsa_256bit_key_z", 0 },

	{ OTP_PUBLIC_CERTIFICATION, OTP_PUBLIC_CERTIFICATION_ADDR,
		OTP_PUBLIC_CERTIFICATION_SIZE,
		"public_certification", 0},

	{ OTP_DEVICE_PRIVATE_KEY, OTP_DEVICE_PRIVATE_KEY_ADDR,
		OTP_DEVICE_PRIVATE_KEY_SIZE,
		"device_private_key", 0 },

	{ OTP_PASSWORD_FOR_DEBUGGING, OTP_PASSWORD_FOR_DEBUGGING_ADDR,
		OTP_PASSWORD_FOR_DEBUGGING_SIZE,
		"password_for_debugging", 0 },

	{ OTP_SECURITY_ENABLE_CONTROL, OTP_SECURITY_ENABLE_CONTROL_ADDR,
		OTP_SECURITY_ENABLE_CONTROL_SIZE,
		"security_enable_control", 1 },

	{ OTP_WHOLE_OTP_PROGRAMMING_DISABLE, OTP_WHOLE_OTP_PROGRAMMING_DISABLE_ADDR,
		OTP_WHOLE_OTP_PROGRAMMING_DISABLE_SIZE,
		"whole_programming_disable", 1 },

	{ OTP_OTP_PAD_ACCESS_DISABLE, OTP_OTP_PAD_ACCESS_DISABLE_ADDR,
		OTP_OTP_PAD_ACCESS_DISABLE_SIZE,
		"pad_access_disable", 1 },

	{ OTP_DEV_MOD, OTP_DEV_MODE_ADDR, OTP_DEV_MODE_SIZE,
		"dev_mod", 1 },

	{ OTP_MARKET_ID, OTP_MARKET_ID_ADDR, OTP_MARKET_ID_SIZE,
		"market_id", 1 },

	{ OTP_DEVICE_ID, OTP_DEVICE_ID_ADDR, OTP_DEVICE_ID_SIZE,
		"device_id", 1 },

	{ OTP_FUNCTIONAL_FEATURE_LIST, OTP_FUNCTIONAL_FEATURE_LIST_ADDR,
		OTP_FUNCTIONAL_FEATURE_LIST_SIZE,
		"functional_feature_list", 1 },

	{ OTP_SECURITY_FEATURE_LIST, OTP_SECURITY_FEATURE_LIST_ADDR,
		OTP_SECURITY_FEATURE_LIST_SIZE,
		"security_feature_list", 1 },

	{ OTP_UNIQUE_CHIP_ID, OTP_UNIQUE_CHIP_ID_ADDR, OTP_UNIQUE_CHIP_ID_SIZE,
		"unique_chip_id", 0 },

	{ OTP_NUMBER_OF_DIE, OTP_NUMBER_OF_DIE_ADDR, OTP_NUMBER_OF_DIE_SIZE,
		"number_of_die", 0},

	{ OTP_DIE_ORDINAL, OTP_DIE_ORDINAL_ADDR, OTP_DIE_ORDINAL_SIZE,
		"die_ordinal", 0 },

	{ OTP_DFT_INFORMATION, OTP_DFT_INFORMATION_ADDR, OTP_DFT_INFORMATION_SIZE,
		"dft_information", 0 }
};

static u32 otp_read32(u64 index)
{
	u64 index_x;
	u32 index_y;
	u32 value1 = 0, value2 = 0;

	/* Align index_x to a 4-byte boundary */
	index_x = (index & ~0x3);
	/* Get the byte offset within the 32-bit word */
	index_y = index & 0x3;

	/* If the index is already 4-byte aligned, read directly */
	if (index_y == 0) {
		return readl((void __iomem *)index_x);
	} else {
		/* Read two consecutive 32-bit values */
		value1 = readl((void __iomem *)index_x);
		value2 = readl((void __iomem *)(index_x + 4));

		/* Shift and merge to extract the required 32-bit value */
		value1 = value1 >> (index_y * 8);
		value2 = value2 << ((4 - index_y) * 8);

		return value1 | value2;
	}
}

static void otp_write32(u64 index, u32 value)
{
	u64 index_x;
	u32 index_y;
	u32 value1 = 0, value2 = 0;

	/* Align index_x to a 4-byte boundary */
	index_x = (index & ~0x3);
	/* Get the byte offset within the 32-bit word */
	index_y = index & 0x3;

	/* If the index is already 4-byte aligned, write directly */
	if (index_y == 0) {
		writel(value, (void __iomem *)index_x);
	} else {
		/* Read the first 32-bit word and modify the relevant bytes */
		value1 = readl((void __iomem *)index_x);
		value1 &= (0xFFFFFFFF >> (8 * (4 - index_y)));  /* Preserve existing higher bytes */
		value2 = value << (8 * index_y);
		value1 |= value2;
		writel(value1, (void __iomem *)index_x);

		/* Read the second 32-bit word and modify the relevant bytes */
		value1 = readl((void __iomem *)(index_x + 4));
		value1 &= (0xFFFFFFFF << (8 * index_y));  /* Preserve existing lower bytes */
		value2 = value >> (8 * (4 - index_y));
		value1 |= value2;
		writel(value1, (void __iomem *)(index_x + 4));
	}
}

/**
 * otp init
 */
static s32 otp_init(void)
{
	u32           val0, val1, val2;
	u32           i;
	u32           count = 15000;
	CHIP_CTRL_T    *p_chip_ctrl;
	OTP_GLB_CTRL_T *p_otp_glb_ctrl;
	OTP_STATUS_T   *p_otp_status;

	p_chip_ctrl = (CHIP_CTRL_T *)(&val0);
	p_otp_glb_ctrl = (OTP_GLB_CTRL_T *)(&val1);
	p_otp_status = (OTP_STATUS_T *)(&val2);

	if (!crypto_otpc_io_base) {
		crypto_otpc_io_base = ioremap(CRYPTO_OTPC_BASE, 0x1000);
	}
	val0 = readl((void __iomem *)REG_OTPC_CHIP_CTRL);
	if (p_chip_ctrl->por_otprd_done) {
		writel(1, (void __iomem *)REG_OTPC_GLB_CTRL);
	} else {
		return ERR_OTP;
	}
	/* if init_otp is 1, init is not required, then return */
	val1 = readl((void __iomem *)REG_OTPC_GLB_CTRL);
	if (p_otp_glb_ctrl->init_otp)
		return ERR_OK;

	/* if init_otp is 0, set init_otp to 1, circular query init_done */
	p_otp_glb_ctrl->init_otp = 1;
	writel(val1, (void __iomem *)REG_OTPC_GLB_CTRL);

	val2 = readl((void __iomem *)REG_OTPC_STATUS);
	if (p_otp_status->otp_init_done)
		return ERR_OK;

	for (i = 0; i < count; i++) {
		val1 = readl((void __iomem *)REG_OTPC_GLB_CTRL);
		if (p_otp_glb_ctrl->init_otp)
			break;
	}

	/* circular query init_done */
	val2 = readl((void __iomem *)REG_OTPC_STATUS);
	if (p_otp_status->otp_init_done)
		return ERR_OK;

	for (i = 0; i < count; i++) {
		val2 = readl((void __iomem *)REG_OTPC_STATUS);
		if (p_otp_status->otp_init_done)
			break;
	}
	if (p_otp_status->otp_init_done)
		return ERR_OK;

	return ERR_OTP;
}

/**
 * read idle status
 */
static s32 otp_read_status(void)
{
	u32         val;
	u32         i;
	u32         count = 15000;
	OTP_STATUS_T *p_otp_status;
	p_otp_status = (OTP_STATUS_T *)(&val);

	val = readl((void __iomem *)REG_OTPC_STATUS);
	if (!p_otp_status->otp_busy)
		return ERR_OK;

	for (i = 0; i < count; i++) {
		val = otp_read32(REG_OTPC_STATUS);
		if (!p_otp_status->otp_busy)
			break;
	}
	if (!p_otp_status->otp_busy)
		return ERR_OK;
	return ERR_OTP;
}

/**
 * Set otp read data parameter
 * addr : otp memory address of read data
 * len  : data size, the size must be no more than 128 byte
 * enxor: enable or disable 4bitxor1bit
 */
static s32 otp_set_rd_para(u16 addr, u8 len, u8 enxor)
{
	u32           val;
	RD_ADDR_SIZE_T *p_rd_addr_size;

	if (len > MAX_OTP_READ_LEN)
		return ERR_OTP;

	p_rd_addr_size = (RD_ADDR_SIZE_T *)(&val);
	val = otp_read32(REG_OTPC_RD_ADDR_SIZE);

	p_rd_addr_size->rd_base_addr = addr;
	p_rd_addr_size->rd_length    = len;
	if (enxor) {
		p_rd_addr_size->rd_4bitxor1bit_en = 1;
	} else
		p_rd_addr_size->rd_4bitxor1bit_en = 0;

	otp_write32(REG_OTPC_RD_ADDR_SIZE, val);
	val = otp_read32(REG_OTPC_RD_ADDR_SIZE);

	return ERR_OK;
}

/**
 * Configure the start word when reading data
 */
s32 otp_set_rd_start_word(void)
{
	otp_write32(REG_OTPC_CMD_WORD, RD_START_WORD);
	return ERR_OK;
}

/**
 * Query read done
 */
static s32 otp_query_rd_done(void)
{
	u32            val;
	u32            i;
	u32            count = 15000;
	OTP_OP_STATUS_T *p_otp_op_status;

	p_otp_op_status = (OTP_OP_STATUS_T *)(&val);
	val             = otp_read32(REG_OTPC_OP_STATUS);

	for (i = 0; i < count; i++) {
		val = otp_read32(REG_OTPC_OP_STATUS);
		if (p_otp_op_status->rdone_st)
			break;
	}
	if (p_otp_op_status->rdone_st)
		return ERR_OK;
	return ERR_OTP;
}

/**
 * Clear read done
 */
static s32 clear_rd_done(void)
{
	u32 val;
	CLEAR_STATUS_INT_T *p_clr_status_int;

	p_clr_status_int = (CLEAR_STATUS_INT_T *)(&val);
	val = otp_read32(REG_OTPC_CLEAR_STATUS_INT);

	p_clr_status_int->clr_rdone = 1;
	otp_write32(REG_OTPC_CLEAR_STATUS_INT, val);
	val = otp_read32(REG_OTPC_CLEAR_STATUS_INT);
	if (p_clr_status_int->clr_rdone)
		return ERR_OK;
	return ERR_OTP;
}

/**
 * Clear write done
 */
static s32 clear_wr_done(void)
{
	u32               val;
	CLEAR_STATUS_INT_T *p_clr_status_int;

	p_clr_status_int = (CLEAR_STATUS_INT_T *)(&val);
	val              = otp_read32(REG_OTPC_CLEAR_STATUS_INT);

	p_clr_status_int->clr_wrdone = 1;
	otp_write32(REG_OTPC_CLEAR_STATUS_INT, val);
	val = otp_read32(REG_OTPC_CLEAR_STATUS_INT);
	if (p_clr_status_int->clr_wrdone)
		return ERR_OK;
	return ERR_OTP;
}

/**
 * Clear pgmlock done

s32 clear_pgmlock_done()
{
    u32               val;
    CLEAR_STATUS_INT_T *p_clr_status_int;

    p_clr_status_int = (CLEAR_STATUS_INT_T *)(&val);
    val              = otp_read32(REG_OTPC_CLEAR_STATUS_INT);

    p_clr_status_int->clr_pgmlock = 1;
    otp_write32(REG_OTPC_CLEAR_STATUS_INT, val);
    val = otp_read32(REG_OTPC_CLEAR_STATUS_INT);
    if (p_clr_status_int->clr_pgmlock)
        return ERR_OK;
    return ERR_OTP;
}
*/
/**
 * Clear write fail
 */
static s32 clear_wr_fail(void)
{
	u32               val;
	CLEAR_STATUS_INT_T *p_clr_status_int;

	p_clr_status_int = (CLEAR_STATUS_INT_T *)(&val);
	val              = otp_read32(REG_OTPC_CLEAR_STATUS_INT);

	p_clr_status_int->clr_wrfail = 1;
	otp_write32(REG_OTPC_CLEAR_STATUS_INT, val);
	val = otp_read32(REG_OTPC_CLEAR_STATUS_INT);
	if (p_clr_status_int->clr_wrfail)
		return ERR_OK;
	return ERR_OTP;
}

/**
 * Clear readbuf
 */
static s32 clean_readbuf(void)
{
	u32 val;
	OTP_GLB_CTRL_T *p_otp_glb_ctrl;

	p_otp_glb_ctrl = (OTP_GLB_CTRL_T *)(&val);
	val = otp_read32(REG_OTPC_GLB_CTRL);

	if (p_otp_glb_ctrl->clean_readbuf) {
		p_otp_glb_ctrl->clean_readbuf = 0;
		otp_write32(REG_OTPC_GLB_CTRL, val);
		return ERR_OK;
	}
	p_otp_glb_ctrl->clean_readbuf = 1;
	otp_write32(REG_OTPC_GLB_CTRL, val);

	p_otp_glb_ctrl->clean_readbuf = 0;
	otp_write32(REG_OTPC_GLB_CTRL, val);
	return ERR_OK;
}

/**
 * read lock status
 */
static s32 otp_read_lock(void)
{
	u32 val;
	OTP_STATUS_T *p_otp_status;

	p_otp_status = (OTP_STATUS_T *)(&val);
	val = otp_read32(REG_OTPC_STATUS);

	if (p_otp_status->otp_bit_lock)
		return ERR_OTP;

	if (p_otp_status->otp_pgm_lock)
		return ERR_OTP;

	return ERR_OK;
}

/**
 * Set otp write data parameter
 * addr    : otp memory address of write data
 * bit_mask: the bit_mask corresponds to the bits of the written data,
 *           1-writable, 0-not writable
 * data    : write data one byte at a time
 * np2_num : the pulse num default is 0x2
 */
static s32 otp_set_wr_para(u16 addr, u8 bit_mask, u8 data, u8 np2_num)
{
	u64 val0, val1;
	NP_PULSE_NUM_T *p_np_pulse_num;
	WR_DATA_MASK_T *p_wr_data_mask;

	p_np_pulse_num = (NP_PULSE_NUM_T *)(&val0);
	val0 = otp_read32(REG_OTPC_NP_PULSE_NUM);
	p_wr_data_mask = (WR_DATA_MASK_T *)(&val1);
	val1 = otp_read32(REG_OTPC_WR_DATA_MASK);
	p_np_pulse_num->np2_num = np2_num;

	if (Default_np2_num != (int)p_np_pulse_num->np2_num)
		otp_write32(REG_OTPC_NP_PULSE_NUM, (u32)val0);

	p_wr_data_mask->wr_addr  = addr;
	p_wr_data_mask->wr_data  = data;
	p_wr_data_mask->wr_biten = bit_mask;
	otp_write32(REG_OTPC_WR_DATA_MASK, (u32)val1);
	return ERR_OK;
}

/**
 * Configure the start word when writing data
 */
static s32 otp_set_wr_start_word(void)
{
	otp_write32(REG_OTPC_CMD_WORD, WR_START_WORD);
	return ERR_OK;
}

/**
 * Query write done
 */
static s32 otp_query_wr_done(u8 *wrfail_st)
{
	u32 val;
	u32 i;
	u32 count = 15000;
	OTP_OP_STATUS_T *p_otp_op_status;

	p_otp_op_status = (OTP_OP_STATUS_T *)(&val);
	val = otp_read32(REG_OTPC_OP_STATUS);

	if (NULL != wrfail_st)
		*wrfail_st = p_otp_op_status->wrfail_st;

	for (i = 0; i < count; i++) {
		val = otp_read32(REG_OTPC_OP_STATUS);
		if (p_otp_op_status->wrdone_st)
			break;
	}

	if (p_otp_op_status->wrdone_st)
		return ERR_OK;

	return ERR_OTP;
}

/**
* otp read data interface
* data : the addr to put into readbuf
* index : the addr to read data
* size : otp read data size at one time
* enxor : enable or disable 4bitxor1bit, 1-enbale, 0-disable
* return: read data success or fail, 0-success, other-fail
*/
s32 otp_read_data(u8 *data, u32 index, u32 size, u8 enxor)
{
	s32 ret = ERR_OK;
	u32 i, k;
	u32 dw_len;
	u32 count;
	u32 val;
	u32 remainder;
	u32 dw_total_len = 0;
	u32 repeat = 1000;
	u16 rd_addr;
	u8 *p_buf;

	RD_PACKET_T *p_rd_packet;

	if (NULL == data) {
		pr_err("otp_read_data pointer of data is NULL, failed.\r\n");
		return ERR_OTP;
	}

	if (0 == size) {
		pr_err("otp_read_data size of data is 0, failed.\r\n");
		return ERR_OTP;
	}

	count = (u32)(size / MAX_OTP_READ_LEN);
	remainder = (u32)(size % MAX_OTP_READ_LEN);

	rd_addr = (u16)index;
	p_buf = data;

	p_rd_packet = (RD_PACKET_T *)(&val);

	for (k = 0; k < count; k++) {
		ret = otp_init();
		if (ERR_OK != ret) {
			pr_err("otp_read_data, otp_init(k=%d) failed.\r\n", k);
			return ret;
		}

		ret = otp_read_status();
		if (ERR_OK != ret) {
			pr_err("otp_read_data, otp_read_status(k=%d), failed.\r\n", k);
			return ret;
		}

		ret = otp_set_rd_para(rd_addr, MAX_OTP_READ_LEN, enxor);
		if (ERR_OK != ret) {
			pr_err("otp_read_data(k=%d), otp_set_rd_para rd_addr=0x%x,MAX_OTP_READ_LEN,xor=%d, failed.\r\n",
				k, rd_addr, enxor);
			clean_readbuf();
			return ret;
		}

		otp_set_rd_start_word();
		ret = otp_query_rd_done();

		if (ERR_OK != ret) {
			pr_err("otp_read_data(k=%d), otp_query_rd_done, failed.\r\n", k);
			clean_readbuf();
			return ERR_OTP;
		}

		dw_len = 0;
		for (i = 0; i < repeat; i++) {
			val = otp_read32(REG_OTPC_RD_PACKET);

			if (!p_rd_packet->rd_valid) {
				continue;
			}
			*(p_buf + i) = p_rd_packet->rd_data;
			dw_len++;
			if (MAX_OTP_READ_LEN == dw_len)
				break;
			if (p_rd_packet->rd_last)
				break;
		}

		if (MAX_OTP_READ_LEN != dw_len) {
			pr_err("otp_read_data(k=%d), dw_len=%d, failed.\r\n", k, dw_len);
			clean_readbuf();
			clear_rd_done();
			return ERR_OTP;
		}

		if (!p_rd_packet->rd_last) {
			pr_err( "otp_read_data(k=%d), p_rd_packet->rd_last=0, failed.\r\n", k);
			clean_readbuf();
			clear_rd_done();
			return ERR_OTP;
		}
		p_buf += MAX_OTP_READ_LEN;
		rd_addr += MAX_OTP_READ_LEN;
		dw_total_len += MAX_OTP_READ_LEN;
		clean_readbuf();
		clear_rd_done();
	}

	if (0 == remainder) {
		if (!p_rd_packet->rd_last || size != dw_total_len) {
			pr_err( "otp_read_data, size=%d,p_rd_packet->rd_last is %d and totalLen=%d, failed.\r\n",
				size, p_rd_packet->rd_last, dw_total_len);
			clean_readbuf();
			clear_rd_done();
			return ERR_OTP;
		}
		clean_readbuf();
		clear_rd_done();
		return ERR_OK;
	}
	ret = otp_init();
	if (ERR_OK != ret) {
		pr_err("otp_read_data, otp_init(remainder=%d) failed.\r\n", remainder);
		return ret;
	}

	ret = otp_read_status();
	if (ERR_OK != ret) {
		pr_err("otp_read_data, otp_read_status(remainder=%d), failed.\r\n", remainder);
		return ret;
	}

	ret = otp_set_rd_para(rd_addr, remainder, enxor);
	if (ERR_OK != ret) {
		pr_err( "otp_read_data,otp_set_rd_para addr=0x%x,remainder=%d,xor=%d,failed.\r\n", rd_addr, remainder, enxor);
		clean_readbuf();
		clear_rd_done();
		return ret;
	}

	otp_set_rd_start_word();

	ret = otp_query_rd_done();
	if (ERR_OK != ret) {
		pr_err("otp_read_data,otp_query_rd_done(remainder=%d),failed.\r\n", remainder);
		clean_readbuf();
		return ERR_OTP;
	}

	dw_len = 0;
	for (i = 0; i < repeat; i++) {
		val = otp_read32(REG_OTPC_RD_PACKET);
		if (!p_rd_packet->rd_valid) {
			continue;
		}
		*(p_buf + i) = p_rd_packet->rd_data;
		dw_len++;
		if (remainder == dw_len)
			break;
		if (p_rd_packet->rd_last)
			break;
	}

	if (!p_rd_packet->rd_last) {
		pr_err("otp_read_data(remainder=%d):p_rd_packet->rd_last is 0,failed.\r\n", remainder);
		clean_readbuf();
		clear_rd_done();
		return ERR_OTP;
	}

	if (remainder != dw_len) {
		pr_err("otp_read_data(remainder=%d):dw_len=%d,failed.\r\n", remainder, dw_len);
		clean_readbuf();
		clear_rd_done();
		return ERR_OTP;
	}

	dw_total_len += remainder;

	if (size != dw_total_len) {
		pr_err("otp_read_data, size=%d and totalLen=%d, failed.\r\n", size, dw_total_len);
		clean_readbuf();
		clear_rd_done();
		return ERR_OTP;
	}
	clean_readbuf();
	clear_rd_done();
	return ERR_OK;
}

/**
 * otp write data interface
 * addr   : the addr to write data
 * data   : data to be written
 * bit_mask: the bit_mask corresponds to the bits of the written data, 1-writable, 0-not writable
 * return : write data success or fail, 0-success, other-fail
 */
s32 otp_write_data(u32 addr, u8 data, u8 bit_mask)
{
	s32 ret = ERR_OK;
	u32 i;
	u32 count = 1000;
	u16 wr_addr;
	u8 wrfail_st;

	wr_addr = (u16)addr;
	ret = otp_init();
	if (ERR_OK != ret) {
		pr_err("otp_write_data addr=0x%x, otp_init return not 0, failed.\r\n", addr);
		return ret;
	}

	ret = otp_read_status();
	if (ERR_OK != ret) {
		pr_err("otp_write_data addr=0x%x, otp_read_status return not 0, failed.\r\n", addr);
		return ret;
	}

	ret = otp_read_lock();
	if (ERR_OK != ret) {
		pr_err("otp_write_data addr=0x%x, otp_read_lock return not 0, failed.\r\n", addr);
		return ERR_OTP;
	}

	for (i = 0; i < count; i++) {
		otp_set_wr_para(wr_addr, bit_mask, data, Default_np2_num);
		otp_set_wr_start_word();
		ret = otp_query_wr_done(&wrfail_st);
		if (ERR_OK != ret) {
			pr_err("otp_write_data addr=0x%x, otp_query_wr_done failed.\r\n", addr);
			return ERR_OTP;
		}

		if (wrfail_st != 0) {
			bit_mask = wrfail_st;
		} else {
			break;
		}
	}
	clear_wr_done();
	clear_wr_fail();
	return ERR_OK;
}

/**
 * @brief  Get data from OTP
 * @param [out] *data: addr of the data reading from otp
 *              when enxor=1,the data is 4bitxor1bit result, the valid data size
 *              is quarter of the input size.
 *              when enxor=0,the valid data size is the input size.
 * @param [in] otp_id: id of the data filed in otp
 * @param [in] size: byte size of the data reading from otp.
 * @param [in] enxor: enable 4bitxor1bit read.
 *                   0 --- disable
 *                   1 --- enable
 * @return
 *  - ERR_OK if no error.
 *  - errorcode if error occur
 */
s32 get_otp_value(u8 *data, int otp_id, u32 size, u8 enxor)
{
	s32 ret = ERR_OK;
	u32 address;
	u32 item_size;
	u32 rd_size;
	u32 i, j;
	int map_element_num;
	u8 *rd_buff;

	rd_size = size;
	map_element_num = MAX_OTP_ITEM_NUM;

	if (enxor) {
		if (size != 1 && (size & 0x3) != 0)
			return ERR_OTP_LEN;
	}

	if (otp_id >= map_element_num || NULL == data)
		return ERR_OTP;

	address = otp_map_eic770x[otp_id].otp_addr;
	item_size = otp_map_eic770x[otp_id].otp_size;

	if (rd_size > item_size)
		return -EINVAL;

	rd_buff = (u8 *)kmalloc(size, GFP_KERNEL);
	if (!rd_buff)
		return -ENOMEM;

	memset(rd_buff, 0, size);
	ret = otp_read_data(rd_buff, address, rd_size, enxor);
	if (enxor && size >= 4) {
		u8 tmp;
		for (i = 0, j = 0; i < size; i = i + 4, j++) {
			/*
				otp.py generate the data in little endian
				X86 is big endian, so change the btye order here.
			*/
			SWAP(rd_buff[i], rd_buff[i + 3]);
			SWAP(rd_buff[i + 1], rd_buff[i + 2]);
			tmp = 0x80 & rd_buff[i] << 3;
			tmp += 0x40 & rd_buff[i] << 6;
			tmp += 0x20 & rd_buff[i + 1] << 1;
			tmp += 0x10 & rd_buff[i + 1] << 4;
			tmp += 0x08 & rd_buff[i + 2] >> 1;
			tmp += 0x04 & rd_buff[i + 2] << 2;
			tmp += 0x02 & rd_buff[i + 3] >> 3;
			tmp += 0x01 & rd_buff[i + 3];
			data[j] = tmp;
			/*
			pr_err("i = %d\r\n", i);
			pr_err("data before 4bitTo1bit = %02x %02x %02x %02x\r\n",
				rd_buff[i], rd_buff[i + 1], rd_buff[i + 2],
				rd_buff[i + 3]);
			pr_err("data after 4bitTo1bit = %x\r\n", tmp);
			*/
		}
	} else {
		memcpy(data, rd_buff, size);
	}
	kfree(rd_buff);
	return ret;
}

