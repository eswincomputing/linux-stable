// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN OTP Driver
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

#ifndef __OTP_MAP_EIC770X_H__
#define __OTP_MAP_EIC770X_H__

#include <asm/types.h>

extern void __iomem *crypto_otpc_io_base;

#define CRYPTO_OTPC_BASE    0x51B40000ul
#define OTPC_CHIP_CTRL      (crypto_otpc_io_base + 0x3C)
#define OTP_BASE  (0)
#define OTPC_BASE ((uint64_t)crypto_otpc_io_base)

/* OTP Memory Size 16K Byte(128K bit) */
#define OTP_SIZE         2048
#define OTP_MAPPING_SIZE 16384

#define OTP_RSA_PUBLIC_KEY_ADDR                (OTP_BASE + 0x0)
#define OTP_RSA_PUBLIC_KEY_SIZE                (256)
#define OTP_EXPONENT_ADDR                      (OTP_BASE + 0x200)
#define OTP_EXPONENT_SIZE                      (4)
#define OTP_MEMORY_TO_MEMORY_SECRET_KEY_ADDR   (OTP_BASE + 0x210)
#define OTP_MEMORY_TO_MEMORY_SECRET_KEY_SIZE   (16)
#define OTP_BOOT_IMAGE_DECRYPTION_KEY_ADDR     (OTP_BASE + 0x230)
#define OTP_BOOT_IMAGE_DECRYPTION_KEY_SIZE     (16)
#define OTP_WHOLE_MEMORY_ENCRYPTION_KEY_ADDR   (OTP_BASE + 0x250)
#define OTP_WHOLE_MEMORY_ENCRYPTION_KEY_SIZE   (16)
#define OTP_SECRET_KEY_ARRAY_ADDR              (OTP_BASE + 0x26a)
#define OTP_SECRET_KEY_ARRAY_SIZE              (256)
#define OTP_ECDSA_256BIT_KEY_X_ADDR            (OTP_BASE + 0x36a)
#define OTP_ECDSA_256BIT_KEY_X_SIZE            (32)
#define OTP_ECDSA_256BIT_KEY_Y_ADDR            (OTP_BASE + 0x38a)
#define OTP_ECDSA_256BIT_KEY_Y_SIZE            (32)
#define OTP_ECDSA_256BIT_KEY_Z_ADDR            (OTP_BASE + 0x3aa)
#define OTP_ECDSA_256BIT_KEY_Z_SIZE            (32)
#define OTP_PUBLIC_CERTIFICATION_ADDR          (OTP_BASE + 0x3ca)
#define OTP_PUBLIC_CERTIFICATION_SIZE          (522)
#define OTP_DEVICE_PRIVATE_KEY_ADDR            (OTP_BASE + 0x5d4)
#define OTP_DEVICE_PRIVATE_KEY_SIZE            (340)
#define OTP_PASSWORD_FOR_DEBUGGING_ADDR        (OTP_BASE + 0x728)
#define OTP_PASSWORD_FOR_DEBUGGING_SIZE        (16)
#define OTP_RESERVED_FOR_OTHER_KEYS_ADDR       (OTP_BASE + 0x738)
#define OTP_SECURITY_ENABLE_CONTROL_ADDR       (OTP_BASE + 0x800)
#define OTP_SECURITY_ENABLE_CONTROL_SIZE       (1)
#define OTP_WHOLE_OTP_PROGRAMMING_DISABLE_ADDR (OTP_BASE + 0x801)
#define OTP_WHOLE_OTP_PROGRAMMING_DISABLE_SIZE (1)
#define OTP_OTP_PAD_ACCESS_DISABLE_ADDR        (OTP_BASE + 0x802)
#define OTP_OTP_PAD_ACCESS_DISABLE_SIZE        (1)
#define OTP_DEV_MODE_ADDR                      (OTP_BASE + 0x803)
#define OTP_DEV_MODE_SIZE                      (1)
#define OTP_MARKET_ID_ADDR                     (OTP_BASE + 0x80c)
#define OTP_MARKET_ID_SIZE                     (32)
#define OTP_DEVICE_ID_ADDR                     (OTP_BASE + 0x82c)
#define OTP_DEVICE_ID_SIZE                     (32)
#define OTP_FUNCTIONAL_FEATURE_LIST_ADDR       (OTP_BASE + 0x84c)
#define OTP_FUNCTIONAL_FEATURE_LIST_SIZE       (16)
#define OTP_SECURITY_FEATURE_LIST_ADDR         (OTP_BASE + 0x85c)
#define OTP_SECURITY_FEATURE_LIST_SIZE         (16)
#define OTP_UNIQUE_CHIP_ID_ADDR                (OTP_BASE + 0xc00)
#define OTP_UNIQUE_CHIP_ID_SIZE                (16)
#define OTP_NUMBER_OF_DIE_ADDR                 (OTP_BASE + 0xc10)
#define OTP_NUMBER_OF_DIE_SIZE                 (1)
#define OTP_DIE_ORDINAL_ADDR                   (OTP_BASE + 0xc11)
#define OTP_DIE_ORDINAL_SIZE                   (1)
#define OTP_DFT_INFORMATION_ADDR               (OTP_BASE + 0xc80)
#define OTP_DFT_INFORMATION_SIZE               (256)

enum {
	OTP_RSA_PUBLIC_KEY                = 0,
	OTP_EXPONENT                      = 1,
	OTP_MEMORY_TO_MEMORY_SECRET_KEY   = 2,
	OTP_BOOT_IMAGE_DECRYPTION_KEY     = 3,
	OTP_WHOLE_MEMORY_ENCRYPTION_KEY   = 4,
	OTP_SECRET_KEY_ARRAY              = 5,
	OTP_ECDSA_256BIT_KEY_X            = 6,
	OTP_ECDSA_256BIT_KEY_Y            = 7,
	OTP_ECDSA_256BIT_KEY_Z            = 8,
	OTP_PUBLIC_CERTIFICATION          = 9,
	OTP_DEVICE_PRIVATE_KEY            = 10,
	OTP_PASSWORD_FOR_DEBUGGING        = 11,
	OTP_SECURITY_ENABLE_CONTROL       = 12,
	OTP_WHOLE_OTP_PROGRAMMING_DISABLE = 13,
	OTP_OTP_PAD_ACCESS_DISABLE        = 14,
	OTP_DEV_MOD                       = 15,
	OTP_MARKET_ID                     = 16,
	OTP_DEVICE_ID                     = 17,
	OTP_FUNCTIONAL_FEATURE_LIST       = 18,
	OTP_SECURITY_FEATURE_LIST         = 19,
	OTP_UNIQUE_CHIP_ID                = 20,
	OTP_NUMBER_OF_DIE                 = 21,
	OTP_DIE_ORDINAL                   = 22,
	OTP_DFT_INFORMATION               = 23,
	MAX_OTP_ITEM_NUM,
};

struct OTP_MAP_ITEM {
	s32 otp_id;
	s32 otp_addr;
	s32 otp_size;
	char *name;
	u8 enxor;
};

/* OTPC Register */
#define REG_OTPC_RD_ADDR_SIZE     (OTPC_BASE + 0x14)
#define REG_OTPC_RD_PACKET        (OTPC_BASE + 0x18)
#define REG_OTPC_OP_STATUS        (OTPC_BASE + 0x1c)
#define REG_OTPC_CLEAR_STATUS_INT (OTPC_BASE + 0x20)
#define REG_OTPC_GLB_CTRL         (OTPC_BASE + 0x30)
#define REG_OTPC_CMD_WORD         (OTPC_BASE + 0x34)
#define REG_OTPC_STATUS           (OTPC_BASE + 0x38)
#define REG_OTPC_CHIP_CTRL        (OTPC_BASE + 0X3C)
#define REG_OTPC_NP_PULSE_NUM     (OTPC_BASE + 0x40)
#define REG_OTPC_WR_DATA_MASK     (OTPC_BASE + 0x44)

#define RD_START_WORD      0x6ACCACC6
#define WR_START_WORD      0x9ACCACC9
#define PGMLOCK_START_WORD 0x10C610C6
#define Default_np2_num    0x02
#define MAX_OTP_READ_LEN   128

typedef struct {
	u16 rd_base_addr      : 16; /* 0-15 */
	u8  rd_length         : 8;  /* 16-23 */
	u8  rd_4bitxor1bit_en : 1;  /* 24 */
	u8  reserved0         : 7;  /* 25-31 */
} RD_ADDR_SIZE_T;

typedef struct {
	u8 rd_data   : 8; /* 0-7 */
	u8 rd_last   : 1; /* 8 */
	u8 reserved0 : 3; /* 9-11 */
	u8 rd_valid  : 1; /* 12 */
	u8 reserved1 : 3; /* 13-15 */
	u8 reserved2;
	u8 reserved3;
} RD_PACKET_T;

typedef struct {
	u8 wrfail_st  : 8; /* 0-7 */
	u8 wrdone_st  : 1; /* 8 */
	u8 pgmlock_st : 1; /* 9 */
	u8 rdone_st   : 1; /* 10 */
	u8 reserved0  : 5; /* 11-15 */
	u8 reserved1;
	u8 reserved2;
} OTP_OP_STATUS_T;

typedef struct {
	u8 clr_rdone   : 1; /* 0 */
	u8 clr_wrdone  : 1; /* 1 */
	u8 clr_wrfail  : 1; /* 2 */
	u8 clr_pgmlock : 1; /* 3 */
	u8 reserved0   : 4; /* 4-7 */
	u8 reserved1;
	u8 reserved2;
	u8 reserved3;
} CLEAR_STATUS_INT_T;

typedef struct {
	u8 init_otp           : 1; /* 0 */
	u8 reserved0          : 1; /* 1 */
	u8 pgmlock_en         : 1; /* 2 */
	u8 clean_readbuf      : 1; /* 3 */
	u8 wrdone_int_en      : 1; /* 4 */
	u8 rdone_int_en       : 1; /* 5 */
	u8 pgmlock_done_inten : 1; /* 6 */
	u8 clear_dut          : 1; /* 7 */
	u8 jtag2apb_acc       : 1; /* 8 */
	u8 progfail_int_en    : 1; /* 9 */
	u8 reserved1          : 6; /* 10-15 */
	u8 reserved2;
	u8 reserved3;
} OTP_GLB_CTRL_T;


typedef struct {
	u8 otp_busy      : 1; /* 0 */
	u8 otp_pgm_lock  : 1; /* 1 */
	u8 otp_bit_lock  : 1; /* 2 */
	u8 otp_init_done : 1; /* 3 */
	u8 reserved0     : 4; /* 4-7 */
	u8 reserved1;
	u8 reserved2;
	u8 reserved3;
} OTP_STATUS_T;

typedef struct {
	u8  die_num              : 8; /* 0-7 */
	u8  die_ordinal          : 8; /* 8-15 */
	u8  por_otprd_done       : 1; /* 16 */
	u8  secure_mode          : 1; /* 17 */
	u8  scpu_jtag_disable    : 1; /* 18 */
	u8  otp_padacc_disable   : 1; /* 19 */
	u8  otp_jtag2apb_disable : 2; /* 20-21 */
	u16 reserved;
} CHIP_CTRL_T;

typedef struct {
	u8 np1_num     : 2; /* 0-1 */
	u8 reserved0   : 2; /* 2-3 */
	u8 np2_num     : 6; /* 4-9 */
	u8 reserved1   : 2; /* 10-11 */
	u8 cap_d0_mode : 1; /* 12 */
	u8 reserved2   : 3; /* 13-15 */
	u8 reserved3;
	u8 reserved4;
} NP_PULSE_NUM_T;

typedef struct {
	u8  wr_data  : 8;  /* 0-7 */
	u8  wr_biten : 8;  /* 8-15 */
	u16 wr_addr  : 16; /* 16-31 */
} WR_DATA_MASK_T;

typedef enum {
	OFFSET_DIE_NUM,
	OFFSET_DIE_ORDINAL       = 8,
	OFFSET_POR_OPTRD_DONE    = 16,
	OFFSET_SECURE_MODE       = 17,
	OFFSET_SCPU_JTAG_DISABLE = 18,
	OFFSET_PADACC_DISABLE    = 19,
	OFFSET_JTAG2APB_DISABLE  = 20,
} CHIP_CTRL_OFFSET_T;
#endif /* __OTP_MAP_EIC770X_H__ */
