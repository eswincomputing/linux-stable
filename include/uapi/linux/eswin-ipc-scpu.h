/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ESWIN cipher serivce driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Min Lin <linmin@eswincomputing.com>
 */

#ifndef _ESWIN_IPC_SCPU_H
#define _ESWIN_IPC_SCPU_H

#include <linux/types.h>

#define MAX_IPC_DATA_BUFF_LEN           256
#define SERVICE_ID_OFFSET               4
#define FRAME_HEAD_U84                  0xBB
#define FRAME_HEAD_E21                  0xCC
#define PREAM_REQ                       0xBB4565A6
#define PREAM_RSP                       0xCC4578B5
#define PREAM_RSP_LEN                   4
#define RES_DATA_OFFSET                 20
#define IPC_RES_XOR_ERR                 0xdd
#define IPC_RES_HEADER_ERR              0xff
#define IPC_RES_DATA_LEN_OFFSET         5
#define IPC_RES_HEADER_SIZE             4
#define IPC_RES_NUM_SIZE                4
#define IPC_RES_SRVC_ID_SIZE            4
#define IPC_RES_STATUS_SIZE             4
#define IPC_RES_SERVICE_STATUS_SIZE     4
#define IPC_RES_LEN_SIZE                4
#define IPC_RES_XOR_SIZE                1
#define IPC_RES_FIXED_SIZE              \
    (IPC_RES_HEADER_SIZE + IPC_RES_NUM_SIZE + IPC_RES_SRVC_ID_SIZE + \
     IPC_RES_STATUS_SIZE + IPC_RES_SERVICE_STATUS_SIZE+IPC_RES_LEN_SIZE+ \
     IPC_RES_XOR_SIZE)

typedef enum
{
    OTP_KEY = 0,
    ROM_KEY_1,
    ROM_KEY_2,
    ROM_KEY_3,
    ROM_KEY_4,
    ROM_KEY_5,
    ROM_KEY_6,
    ROM_KEY_7,
    RAM_KEY_1,
    RAM_KEY_2,
    RAM_KEY_3,
    RAM_KEY_4,
    RAM_KEY_5,
    RAM_KEY_6,
    RAM_KEY_7,
    RAM_KEY_8,
    EXT_PRIV_KEY = 254,
    EXT_PUB_KEY,
} ASYMM_KEY_TYPE;

typedef enum {
    SRVC_TYPE_SIGN_CHECK,
    SRVC_TYPE_IMG_DECRPT,
    SRVC_TYPE_FIRMWARE_DOWNLOAD,
    SRVC_TYPE_PUBKEY_DOWNLOAD,
    SRVC_TYPE_RSA_CRYPT_DECRYPT,
    SRVC_TYPE_ECDH_KEY,
    SRVC_TYPE_SYM_CRYPT_DECRYPT,
    SRVC_TYPE_DIGEST,
    SRVC_TYPE_HMAC,
    SRVC_TYPE_OTP_READ_PROGRAM,
    SRVC_TYPE_TRNG,
    SRVC_TYPE_ADDR_REGION_PROTECTION,
    SRVC_TYPE_DOWNLOADABLE,
    SRVC_TYPE_BASIC_IO,
    SRVC_TYPE_AXPROT,
    SRVC_TYPE_MAX
} SRVC_TYPE;

typedef enum
{
    OP_ENCRYPT = 0,
    OP_DECRYPT
}CRYPTO_TYPE;

typedef struct
{
    u_int8_t algorithm;
    u_int8_t keyid;
    u_int8_t reserved1;
    u_int8_t reserved2;
} flag1_t;

typedef struct
{
    u_int8_t algorithm :2;
    u_int8_t reserved0 :6;
    u_int8_t keyid;
    u_int8_t crypto :1;
    u_int8_t reserved1 :7;
    u_int8_t reserved2;
} flag5_t;

typedef struct
{
    u_int8_t crypto_mode;
    u_int8_t keyid;
    u_int8_t crypto :1;
    u_int8_t reserved1 :7;
    u_int8_t reserved2;
} flag7_t;

typedef struct
{
    u_int8_t crypto_mode;
    u_int8_t reserved1;
    u_int8_t reserved2;
    u_int8_t reserved3;
} flag8_t;

typedef struct
{
    u_int8_t algorithm;
    u_int8_t reserved1;
    u_int8_t reserved2;
    u_int8_t reserved3;
} flag9_t;

typedef struct
{
    u_int8_t read_program:1;
    u_int8_t reserved0:3;
    u_int8_t otp_enxor:1;
    u_int8_t reserved1:3;
    u_int8_t reserved2;
    u_int8_t reserved3;
    u_int8_t reserved4;
} flag10_t;

typedef struct
{
    u_int8_t hdcp_version:1;
    u_int8_t reserved0  :7;
    u_int8_t reserved1;
    u_int8_t reserved2;
    u_int8_t reserved3;
} flag13_t;

typedef struct
{
    u_int8_t algorithm :2;
    u_int8_t reserved0 :6;
    u_int8_t keyid;
    u_int8_t reserved1;
    u_int8_t service_type;
} flag14_t;


typedef struct
{
    u_int8_t rw :2;
    u_int8_t reserved0 :6;
    u_int8_t data_width :2;
    u_int8_t reserved1 :6;
    u_int8_t reserved2;
    u_int8_t reserved3;
} flag15_t;

typedef struct
{
    u_int8_t rw :2;
    u_int8_t reserved0 :6;
    u_int8_t reserved1;
    u_int8_t reserved2;
    u_int8_t reserved3;
} flag16_t;


#define RSA1024_PUBLICKEY_LEN 128
#define RSA1024_PRIKEY_LEN 128
#define RSA2048_PUBLICKEY_LEN 256
#define RSA2048_PRIKEY_LEN 256
#define RSA4096_PUBLICKEY_LEN 512
#define RSA4096_PRIKEY_LEN 512
#define RSA_PRIKEY_LEN 512
#define ECC_PUBLICKEY_LEN 32

/* RSA1024 public key structure */
typedef struct __rsa1024_pubkey_st
{
  u_int32_t keylen;
  u_int32_t exponent;
  u_int8_t mod[RSA1024_PUBLICKEY_LEN];
} rsa1024_pubkey_t;

/* RSA2048 public key structure */
typedef struct __rsa2048_pubkey_st
{
  u_int32_t keylen;
  u_int32_t exponent;
  u_int8_t mod[RSA2048_PUBLICKEY_LEN];
} rsa2048_pubkey_t;

/* RSA4096 public key structure */
typedef struct __rsa4096_pubkey_st
{
  u_int32_t keylen;
  u_int32_t exponent;
  u_int8_t mod[RSA4096_PUBLICKEY_LEN];
} rsa4096_pubkey_t;

/* RSA1024 private key structure */
typedef struct __rsa1024_privkey_st
{
    u_int32_t keylen;
    u_int8_t d[RSA1024_PRIKEY_LEN];
    u_int8_t mod[RSA1024_PRIKEY_LEN];
} rsa1024_privkey_t;

/* RSA2048 private key structure */
typedef struct __rsa2048_privkey_st
{
    u_int32_t keylen;
    u_int8_t d[RSA2048_PRIKEY_LEN];
    u_int8_t mod[RSA2048_PRIKEY_LEN];
} rsa2048_privkey_t;

/* RSA4096 private key structure */
typedef struct __rsa4096_privkey_st
{
    u_int32_t keylen;
    u_int8_t d[RSA4096_PRIKEY_LEN];
    u_int8_t mod[RSA4096_PRIKEY_LEN];
} rsa4096_privkey_t;

/* RSA public key structure */
typedef struct __rsa_pubkey_st
{
  u_int32_t keylen;
  u_int32_t exponent;
  u_int8_t mod[RSA4096_PUBLICKEY_LEN];
} rsa_pubkey_t;

/* RSA private key structure */
typedef struct __rsa_privkey_st
{
  u_int32_t keylen;
  u_int8_t d[RSA4096_PUBLICKEY_LEN];
  u_int8_t mod[RSA4096_PUBLICKEY_LEN];
} rsa_privkey_t;

/* ECC public key structure */
typedef struct __ecc_pubkey_st
{
  u_int32_t keylen;
  u_int8_t x[ECC_PUBLICKEY_LEN];
  u_int8_t y[ECC_PUBLICKEY_LEN];
} ecc_pubkey_t;

typedef struct __ecc_privkey_st
{
  u_int32_t keylen;
  u_int8_t z[ECC_PUBLICKEY_LEN];
} ecc_privkey_t;

typedef union
{
  rsa2048_pubkey_t rsa2048_pubkey;
  rsa4096_pubkey_t rsa4096_pubkey;
  ecc_pubkey_t ecc_pubkey;
} pubkey_t;

typedef struct {
    flag1_t flag;
    u_int32_t sign_size;
    u_int32_t sign_addr;
    u_int32_t payload_size;
    u_int32_t payload_addr;
    u_int32_t dest_addr;
} signature_validation_check_req_t;

typedef struct {
    u_int32_t sign_size;
    u_int32_t sign_addr;
} signature_validation_check_res_t;

typedef struct {
    flag1_t flag;
    u_int32_t sign_size;
    u_int32_t sign_addr;
    u_int32_t data_size;
    u_int32_t data_addr;
    u_int32_t dest_addr;
} image_decryption_req_t;

typedef struct {
    u_int32_t sign_size;
    u_int32_t sign_addr;
    u_int32_t data_size;
    u_int32_t data_addr;
} image_decryption_res_t;

typedef struct {
    flag1_t flag;
    u_int32_t sign_size;
    u_int32_t sign_addr;
    u_int32_t img_size;
    u_int32_t img_addr;
} firmware_download_req_t;

typedef struct {
    u_int32_t ret_val;
} firmware_download_res_t;

typedef struct {
    flag1_t flag;
    u_int32_t sign_size;
    u_int32_t sign_addr;
    u_int32_t key_size;
    u_int32_t key_addr;
} pubkey_download_req_t;

typedef union {
    rsa1024_pubkey_t rsa1024_pubkey;
    rsa1024_privkey_t rsa1024_prvkey;
    rsa2048_pubkey_t rsa2048_pubkey;
    rsa2048_privkey_t rsa2048_prvkey;
    rsa4096_pubkey_t rsa4096_pubkey;
    rsa4096_privkey_t rsa4096_prvkey;
    rsa_privkey_t rsa_prvkey;
} rsa_keystruct_t;

typedef struct
{
    flag5_t flag;
    u_int32_t data_size;
    u_int32_t data_addr;
    u_int32_t dest_addr;
    u_int32_t keystruct_size;
    rsa_keystruct_t t_key;
} rsa_encdec_req_t;

typedef struct {
    u_int32_t data_size;
    u_int32_t dest_addr;
} rsa_encdec_res_t;

/* ECDH KEY */
typedef enum
{
  PKA_SW_CURVE_NIST_P256 = 0,
  PKA_SW_CURVE_NIST_P384,
  PKA_SW_CURVE_NIST_P512,
  PKA_SW_CURVE_BRAINPOOL_P256R1,
  PKA_SW_CURVE_BRAINPOOL_P384R1,
  PKA_SW_CURVE_BRAINPOOL_P512R1,
} ecc_curve_t;

enum eccCurveByteSize {
    PKA_CURVE_NIST_P256_BYTE = 32,
    PKA_CURVE_NIST_P384_BYTE = 48,
    PKA_CURVE_NIST_P521_BYTE = 66,
    PKA_CURVE_BRAINPOOL_P256R1_BYTE = 32,
    PKA_CURVE_BRAINPOOL_P384R1_BYTE = 48,
    PKA_CURVE_BRAINPOOL_P512R1_BYTE = 64,
    PKA_CURVE_SM2_SCA256_BYTE = 32,
    PKA_CURVE_ED25519_BYTE = 32,
    PKA_CURVE_CURVE25519_BYTE = 32
};

typedef struct __ecdh_key_st
{
    u_int32_t keylen;
    u_int8_t x[PKA_CURVE_NIST_P521_BYTE];
    u_int8_t y[PKA_CURVE_NIST_P521_BYTE];
    u_int8_t z[PKA_CURVE_NIST_P521_BYTE];
} ecdh_key_t;

/* ECDH public key structure */
typedef struct __ecdh_pubkey_st
{
    u_int32_t keylen;
    u_int8_t x[PKA_CURVE_NIST_P521_BYTE];
    u_int8_t y[PKA_CURVE_NIST_P521_BYTE];
} ecdh_pubk_t;

typedef struct __ecdh_privkey_st
{
    u_int32_t keylen;
    u_int8_t z[PKA_CURVE_NIST_P521_BYTE];
} ecdh_privk_t;

typedef struct
{
    ecdh_pubk_t ecdh_pubk;
    ecdh_privk_t ecdh_privk;
} ecdhkey_t;

typedef struct
{
    ecc_curve_t ecdh_curve;
    ecdhkey_t ecdh_key;
} ecdh_curve_key_t;

typedef enum
{
    OPT_GEN,
    OPT_AGR,
    OPT_DRIV,
}ecdh_key_opt_t;

typedef struct
{
    u_int32_t ecc_curve;
} ecdh_key_gen_t;

typedef struct
{
    u_int32_t ecc_curve;
    ecdh_pubk_t pubk_remote;
    ecdh_privk_t privk_local;
} ecdh_key_agr_t;

typedef struct
{
    ecdh_pubk_t seck;
    u_int32_t rand;
} ecdh_key_driv_t;

typedef union
{
    ecdh_key_gen_t gen;
    ecdh_key_agr_t agr;
    ecdh_key_driv_t driv;
}ecdh_key_data_t;

typedef struct
{
    u_int8_t reserved0;
    u_int8_t reserved1;
    u_int8_t reserved2;
    u_int8_t opt;
} flag6_t;

typedef struct
{
    flag6_t flag;
    ecdh_key_data_t data;
} ecdh_key_req_t;

typedef union __attribute__((packed, aligned(1))) {
    ecdh_key_t ecdh_key;
    ecdh_pubk_t secure_key;
    u_int8_t sym_key[64];
} ecdh_key_res_t;

#define AES_SM4_KEY_LEN 32
#define IV_LEN 32
typedef struct
{
    flag7_t flag;
    u_int32_t data_size;
    u_int32_t data_addr;
    u_int32_t dest_addr;
    u_int32_t key_size;
    u_int8_t key[AES_SM4_KEY_LEN];
    u_int32_t iv_size;
    u_int8_t iv[IV_LEN];
} aes_sm4_encdec_req_t;

typedef struct {
    u_int32_t data_size;
    u_int32_t dest_addr;
} aes_sm4_encdec_res_t;

typedef struct
{
    flag8_t flag;
    u_int32_t data_size;
    u_int32_t data_addr;
} sha1_sha256_sm3_digest_req_t;

#define DIGEST_LEN 64
typedef struct {
    u_int32_t digest_size;
    u_int8_t digest[DIGEST_LEN];
} sha1_sha256_sm3_digest_res_t;

#define HMAC_KEY_LEN 256
typedef struct {
    flag9_t flag;
    u_int32_t data_size;
    u_int32_t data_addr;
    u_int32_t key_size;
    u_int8_t key[HMAC_KEY_LEN];
} hmac_req_t;

#define MAX_HMAC_SIZE 64
typedef struct {
    u_int32_t hmac_size;
    u_int8_t hmac[MAX_HMAC_SIZE];
} hmac_res_t;

#define MAX_OTP_DATA_LEN 920

typedef struct {
    flag10_t flag;
    u_int32_t otp_addr;
    u_int32_t data_size;
    u_int32_t data_addr;
    u_int32_t mask_addr;
} otp_req_t;

typedef struct {
    u_int8_t data[MAX_OTP_DATA_LEN];
} otp_res_t;

#define MAX_TRNG_DATA_LEN 256
typedef struct {
    u_int32_t flag;
} trng_req_t;

typedef struct {
    u_int8_t data[MAX_TRNG_DATA_LEN];
} trng_res_t;

typedef struct
{
    u_int8_t region;
    u_int32_t addr_h;
    u_int32_t addr_l;
    u_int32_t config;
}addr_region_protection_req_t;

typedef struct {
    u_int32_t srvc_id;
} downloadable_srvc_res;

typedef struct {
    u_int8_t region_id;
} addr_region_protection_res_t;


typedef struct {
    flag14_t flag;
    u_int32_t sign_size;
    u_int32_t sign_addr;
    u_int32_t code_size;
    u_int32_t code_addr;
} downloadable_init_req_t;

typedef struct {
    flag14_t flag;
    u_int32_t service_id;
} downloadable_destory_req_t;

typedef struct {
    flag14_t flag;
    u_int32_t service_id;
    u_int32_t user_cmd;
    u_int32_t param_size;
    u_int8_t  param[0];
} downloadable_ioctl_req_t;


typedef struct {
    flag15_t flag;
    u_int32_t addr;
    u_int32_t data;
    u_int32_t mask;
} basicio_req_t;

typedef struct {
    u_int32_t data;
} basicio_res_t;

typedef struct {
    flag16_t flag;
    u_int32_t master_id;
    u_int32_t config;
} axprot_req_t;

typedef struct {
    u_int32_t config;
} axprot_res_t;

typedef union {
    signature_validation_check_req_t sign_check_req;
    image_decryption_req_t img_dec_req;
    firmware_download_req_t firm_dl_req;
    pubkey_download_req_t pubkey_dl_req;
    rsa_encdec_req_t rsa_crypto_req;
    ecdh_key_req_t ecdh_key_req;
    aes_sm4_encdec_req_t symm_crypto_req;
    sha1_sha256_sm3_digest_req_t digest_req;
    hmac_req_t hmac_req;
    otp_req_t otp_req;
    trng_req_t trng_req;
    addr_region_protection_req_t region_protect_req;
    downloadable_init_req_t dl_init_req;
    downloadable_destory_req_t dl_destory;
    downloadable_ioctl_req_t dl_ioctl;
    basicio_req_t basicio_req;
    axprot_req_t axprot_req;
} req_data_domain_t;

typedef union {
    signature_validation_check_res_t sign_check_res;
    image_decryption_res_t img_dec_res;
    firmware_download_res_t firm_dl_res;
    rsa_encdec_res_t rsa_crypto_res;
    ecdh_key_res_t ecdh_key_res;
    aes_sm4_encdec_res_t symm_crypto_res;
    sha1_sha256_sm3_digest_res_t digest_res;
    hmac_res_t hmac_res;
    otp_res_t otp_res;
    trng_res_t trng_res;
    addr_region_protection_res_t region_protect_res;
    downloadable_srvc_res dl_srvc_res;
    basicio_res_t basicio_res;
    axprot_res_t axprot_res;
} res_data_domain_t;

#define IPC_ADDR_LEN   4
typedef struct {
    u_int8_t fream_head;
    u_int8_t fream_len;
    u_int8_t ipc_addr[IPC_ADDR_LEN];
    u_int8_t xor;
} mbox_register_data;

typedef struct {
    SRVC_TYPE serivce_type;
    req_data_domain_t data;
} req_service_t;

typedef struct {
    u_int32_t num;
    u_int32_t service_id;
    u_int32_t ipc_status;
    u_int32_t service_status;
    u_int32_t size;
    res_data_domain_t data_t;
}res_service_t;

typedef struct
{
    u_int32_t fream_head;
    u_int32_t num;
    SRVC_TYPE service_id;
    u_int32_t size;
    u_int8_t *data;
}msg_send_t;


struct dma_allocation_data {
	__u64 size;
	__u64 phy_addr;
	__u64 dma_addr;
	void *cpu_vaddr;
	int dmabuf_fd;
};

struct dmabuf_bank_info {
	struct heap_mem *hmem;
	struct dma_allocation_data dma_alloc_info;
};


typedef struct {
	int cpuid;
	int nid;
}cipher_get_nid_req_t;

typedef struct {
	__u32 id; // globally unique id for this mem info in kernel, ipc driver use it to find this mem info
	struct dma_allocation_data dma_alloc_info;
}khandle_dma_allocation_data_t;

#define MAX_NUM_K_DMA_ALLOC_INFO	4
typedef struct {
	__u32 handle_id;
	req_service_t service_req;
	res_service_t service_resp;
	__u32 kinfo_cnt;
	khandle_dma_allocation_data_t k_dma_infos[MAX_NUM_K_DMA_ALLOC_INFO];
}cipher_create_handle_req_t;

#define SCPU_IOC_MAGIC 'S'
#define SCPU_IOC_ALLOCATE_MEM_BY_DRIVER_WITH_DMA_HEAP	_IOWR(SCPU_IOC_MAGIC, 0x1, struct dmabuf_bank_info)
#define SCPU_IOC_FREE_MEM_BY_DRIVER_WITH_DMA_HEAP	_IOWR(SCPU_IOC_MAGIC, 0x2, struct dmabuf_bank_info)
#define SCPU_IOC_ALLOC_IOVA				_IOWR(SCPU_IOC_MAGIC, 0x3, khandle_dma_allocation_data_t)
#define SCPU_IOC_FREE_IOVA				_IOW(SCPU_IOC_MAGIC, 0x4, khandle_dma_allocation_data_t)
#define SCPU_IOC_CPU_NID				_IOR(SCPU_IOC_MAGIC, 0x5, cipher_get_nid_req_t)
#define SCPU_IOC_CREATE_HANDLE			_IOWR(SCPU_IOC_MAGIC, 0x6, cipher_create_handle_req_t)
#define SCPU_IOC_DESTROY_HANDLE			_IOWR(SCPU_IOC_MAGIC, 0x7, cipher_create_handle_req_t)
#define SCPU_IOC_GET_HANDLE_CONFIG		_IOR(SCPU_IOC_MAGIC, 0x8, cipher_create_handle_req_t)
#define SCPU_IOC_UPDATE_HANDLE_CONFIG		_IOWR(SCPU_IOC_MAGIC, 0x9, cipher_create_handle_req_t)
#define SCPU_IOC_MESSAGE_COMMUNICATION 		_IOWR(SCPU_IOC_MAGIC, 0xa, cipher_create_handle_req_t)

#define CIPHER_MAX_MESSAGE_LEN		0x40000000 /* 1GB */

#endif
