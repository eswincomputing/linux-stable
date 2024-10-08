// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __HETERO_COMMON_H__
#define __HETERO_COMMON_H__

#include "hetero_env.h"
#include "hetero_types.h"
#include "edma_regs.h"
#include "sdp_regs.h"
#include "pdp_regs.h"
#include "rubik_regs.h"
#include "cdma_regs.h"
#include "hetero_processor.h"
#include "hetero_perf.h"

#if NPU_DEV_SIM != NPU_REAL_ENV
#include "dla_interface.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define STATIC_ASSERT static_assert

#define UNUSED(x) (void)(x)

#define INVALID_U64_ADDRESS 0xFFFFFFFFFFFFFFFFULL

#if NPU_DEV_SIM != NPU_REAL_ENV
#ifdef __KERNEL__
#define ASSERT(exp) BUG_ON(!(exp))
#else
#define ASSERT assert
#endif
#else
#define ASSERT(exp) UNUSED(exp)
#endif
/**
 * @brief Get the element count of an array.
 *
 */
#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))

/**
 * @brief Get the byte offset of an member relative to the start of a given data type.
 *
 */
#define ADDR_OFFSET(type, member) ((char *)(&((type *)0)->member) - (char *)0)

/**
 * @brief Create a bitmask for a given bit shift value.
 *
 */
#define BITMASK(bit) ((1U << (bit)) - 1U)

#if SMALL_PEC_MAT
/* Rows of PE clusters. */
#define NUM_PEC_ROW (2)
/* Columns of PE clusters. */
#define NUM_PEC_COLUMN (2)
/* Number of E31 cores. */
#define NUM_NPU_CORES (4)
#else
/* Rows of PE clusters. */
#define NUM_PEC_ROW (8)
/* Columns of PE clusters. */
#define NUM_PEC_COLUMN (4)
/* Number of E31 cores. */
#define NUM_NPU_CORES (10)
#endif

#define BIT_PER_DEPCNT 4UL
#define NUM_CNT_PER_BYTE 2UL
#define DEP_CNT_MASK 0xf

#define NUM_BITS_IN_BYTE 8UL
#define NUM_BITS_IN_DWORD 32UL

#define NUM_PINGPONG 2UL
#define NUM_DATA_SLOT 4UL
#define NUM_TIKTOK 2UL
#define MAX_OP_NUM (4096UL * 8)
#define MAX_CDMA_PROGRAM_SIZE 768UL
#define NUM_CDMA_SLOT 2UL
#define INVALID_TIKTOK -1

#define CDMA_TRANSFER_BYTE_ALIGN 16
#define CDMA_SRC_BYTE_ALIGN 16
#define CDMA_DST_BYTE_ALIGN 16

#define NUM_MAJOR_CORES (NUM_NPU_CORES - 2UL)

#define ROUND_UP(x, y) (((x) + ((y) - 1)) / y * y)
#define ROUND_DOWN(x, y) (x - ((x) % (y)))

static const u32 NUM_BIT_IN_DWORD = 32;

/**
 * @brief Defines tensor element data types.
 *
 */
enum type_fmt_e {
    _INT8,
    _UINT8,
    _FP16,
    _INT16,
};

// Complete event id (Complete operator use)
#define COMPLETE_EVENT_ID (4096)

/**
 * @brief PEC matrix row and column information.
 *
 */
/* Columns of PE clusters. */
static const u32 num_max_pec_row = 8;
static const u32 num_max_pec_column = 4;

/* Number of all E31 cores in the system. */
static const u32 max_num_npu_cores = 10;

STATIC_ASSERT(NUM_PEC_COLUMN <= 4);

/* Number of rows inside of a PEC. */
static const u32 num_pe_row = 3;
/* Number of columns inside of a PEC. */
static const u32 num_pe_column = 4;

STATIC_ASSERT(NUM_NPU_CORES >= 3);

typedef struct _conv_dev_hdr {
    /**
     * @brief Specifies the total length of conv_config_data in DWs(4 bytes).
     *
     */
    u16 total_len;

    /**
     * @brief Specifies the total length of rdma_dev_master_inf_t and pec_dev_master_inf_t in DWs(4 bytes).
     *
     */
    u8 master_len;  // DO NOT remove this since model uses it!

    /**
     * @brief Specifies the total length of used num pec in row.
     *
     */
    u8 used_num_pec_row;

    /**
     * @brief Specifies the total length of used num pec in column.
     *
     */
    u8 used_num_pec_column;
    u8 emission_len;  // used by npu_perf
    u8 program_len;   // used by npu_perf

    /**
     * @brief Specifies how many DWs(4 bytes) of data for each Major nodes. This is a bitmap and each 4 bits
     * correspond to a Major node.
     *
     */
    union {
        u8 major_lens[NUM_MAJOR_CORES];
        u32 dws[0];
    };
} __attribute__((aligned(CDMA_TRANSFER_BYTE_ALIGN))) conv_dev_hdr_t;

/**
 * @brief Wrapper of device model.
 *
 */
typedef struct _conv_dev_t {
    struct {
        conv_dev_hdr_t header;
    } __attribute__((aligned(CDMA_TRANSFER_BYTE_ALIGN)));

    /**
     * @brief This is a placeholder of future extension fields.
     *
     */
    u32 dws[0];
} conv_dev_t __attribute__((aligned(CDMA_TRANSFER_BYTE_ALIGN)));

/* dump tensor data to file need this head */
struct dump_file_header {
    int magic;
    int magic_end_offset;
    u32 op_num;

    u32 depend_offset;
    u32 depend_len;
    u32 io_tensor_offset;
    u32 io_tensor_len;
    u32 lut_offset;
    u32 lut_len;
    u64 lut_base_iova;
    conv_dev_hdr_t first_conv_hdr;

    u32 pcer_op_num[NUM_OP_TYPE];
    u32 op_data_offset[NUM_OP_TYPE];
    u32 op_data_len[NUM_OP_TYPE];
} __attribute__((packed));

typedef struct _op_current {
    /**
     * @brief The IOVA of a pointer array. Each pointer points to an array of
     * program data of given operation type.
     */
    u64 program_addr[NUM_OP_TYPE];

    /**
     * @brief The number of remaining operators of a given type.
     */
    u16 num_remain_ops[NUM_OP_TYPE];

    /**
     * @brief The program specification for convolution. This determines how to
     * generate CDMA requests to transfer data from DDR to program and each
     * major core.
     */
    conv_dev_hdr_t next_conv_hdr;
} __attribute__((aligned(sizeof(u64)))) op_current_t;

static const u8 invalid_tensor_idx = 0xFF;
#define MAX_INPUTS 8
#define MAX_OUTPUTS 8
#define MAX_NUM_INPUT_OUTPUT (MAX_INPUTS + MAX_OUTPUTS)

/**
 * @brief The input and output tensor IOVA addresses from NPU side.
 */
typedef struct _npu_io_tensor {
    u64 tensor_addr[MAX_NUM_INPUT_OUTPUT];
} npu_io_tensor_t;

#define MAX_DTIM_DEPCNT 4096
#define NUM_DEPENDENCY_BITS 4

/**
 * @brief The dependency count of each operator. Maximum 4096 operators are
 * supported. Each dependency count has maximum value of 15 and thus occupies 4
 * bits of storage.
 *
 * dependency count of each frame is saved in DTIM. u84 initiates
 * the whole entries and e31 updates entry per op execution.
 *
 * TODO: dtim address for dep_cnt is fixed as 0x0;
 *
 * 4KB entry x 4bit_per_entry
 * from lowest bytes to highest bytes:
 * |-- byte0 ------------|-- byte1--------------|
 * op0_depcnt, op1_depcnt; op2_depcnt, op3_depcnt; etc.
 */
typedef struct _dependency {
    /**
     * @brief The actual operators in this inference frame.
     */
    u16 num_op;
    u8 ref_count[MAX_DTIM_DEPCNT * NUM_DEPENDENCY_BITS / NUM_BITS_IN_BYTE];
} __attribute__((aligned(sizeof(u32)))) dependency_t;

/**
 * @brief The frame descriptor that is shared by Host and E31 on how to trigger
 * NPU evaluation.
 */
// TODO(JIRA13047 zhangyizhong): rename to hetero_ipc_frame_t
typedef struct _frame_desc {
    op_current_t op_current;
    dependency_t op_dependency;
} __attribute__((aligned(sizeof(u32)))) hetero_ipc_frame_t;

typedef struct _frame_info {
    npu_io_tensor_t io_tensor;
    hetero_ipc_frame_t frame;
    u32 is_ready;
    u32 tiktok;
} __attribute__((aligned(sizeof(u32)))) frame_info_t;

typedef struct _resume_info {
    u8 resume_flag;
    u8 tiktok;
    u16 op_index;
} __attribute__((aligned(sizeof(u32)))) resume_info_t;

typedef struct _event_op_info {
    u8 tiktok;
    u16 op_index;
} __attribute__((aligned(sizeof(u32)))) event_op_info_t;

static const u16 invalid_op_index = 0xFFFF;

/**
 * @brief The dependency information for each operator.
 */
typedef struct _npu_dep_info {
    u16 depcnt;

    u16 current_op_idx;

    /**
     * @brief This operator has enable dependency on current operator. If
     * enable_op_idx equals invalid_op_index, then no operator has enable
     * dependency.
     */
    u16 enable_op_idx;

    /**
     * @brief This is a bitmap. Each bit specifies if an operator of given type
     * has a completion dependency on current operator.
     */
    u16 completion_event_bitmap : 12;

    /**
     * @brief If this bit is 1, notify host by sending NOTIFY_OP_DONE when this
     * operation evaluation is done.
     */
    u16 notify_op_done : 1;

    /**
     * @brief If this bit is 1, pause the dependency resolution when an
     * operation evaluation completes.
     */
    u16 pause_op_done : 1;

    /**
     * @brief The consumer's op_idx that completed dependency.
     * max consumer is all npu op + dsp op
     */
    u16 completion_op_idx[MAX_KMD_DEPCNT];

    /**
     * @brief Provides the LUT IOVA for SDP. If this value is 0, then LUT is not
     * needed.
     */
    union {
        u64 lut_address;
        u64 dsp_eval_param;
    };
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) npu_dep_info_t;

/* This assertion ensures CDMA transfer address is in hardware capability range. */
STATIC_ASSERT(__builtin_offsetof(npu_dep_info_t, lut_address) % sizeof(u64) == 0,
              "please ensure lut_address aligned with 8 bytes.");

/*** edma prog data ******/
typedef struct _edma_program {
    union {
        u64 u84_bitmap;
        u32 e31_bitmap[0];
    };
    u32 reg[EDMA_REG_MAX];
    u8 input_tensor_idx;
    u8 output_tensor_idx;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN), packed)) edma_program_t;

typedef struct _edma_dev {
    npu_dep_info_t npu_info;
    edma_program_t prog_data;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) edma_dev_t;

/*** conv prog data ******/
typedef struct _conv_emission {
    npu_dep_info_t npu_info;
    conv_dev_hdr_t next_convolution_header;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) conv_emission_t;

typedef struct _conv_program {
    /**
     * @brief Specifies the whole 48 bits RDMA Ifmap system base address from
     * NPU's perspective. Note NPU may access the data via a SMMU.
     */
    u64 ifmap_base_addr;

    /**
     * @brief Specifies the whole 48 bits RDMA weight system base address from
     * NPU's perspective. Note NPU may access the data via a SMMU.
     */
    u64 weight_base_addr;

    /**
     * @brief Specifies the whole 48 bits RDMA Ofmap system base address from
     * NPU's perspective. Note NPU may access the data via a SMMU.
     */
    u64 ofmap_base_addr;

    /**
     * @brief Specifies the total length of used num pec in row.
     *
     */
    u8 used_num_pec_row;

    /**
     * @brief Specifies the total length of used num pec in column.
     *
     */
    u8 used_num_pec_column;

    /**
     * @brief Specifies the IO tensor index if this is an IO tensor. Otherwise
     * set it to invalid_tensor_idx.
     */
    u8 input0_io_index;

    /**
     * @brief Specifies the IO tensor index if this is an IO tensor. Otherwise
     * set it to invalid_tensor_idx. Output tensor is not necessary.
     */
    u8 output0_io_index;

    /**
     * @brief Specifies the IO tensor offset of input0.
     */
    u32 input0_io_offset;

    /**
     * @brief Specifies the IO tensor offset of output0.
     */
    u32 output0_io_offset;
} __attribute__((aligned(CDMA_TRANSFER_BYTE_ALIGN))) conv_program_t;

typedef struct _rdma_dev_com_inf {
    /**
     * @brief Specifies Ifmap related information. Note Ch0 and Che actually corresponds to multiple parts in our
     * authoritative mapping: Ch0 = Cf x Gf x CMf x GMf and Che = div_up(conv_prob_.G, G0) * div_up(conv_prob_.C, C0)
     * respectively. Meanwhile, the layout format is always E_FORMAT.
     *
     */
    u16 Che;
    u16 W, H;
    u8 N;
    u8 Ch0;
    u8 ifmap_type_fmt;
    u8 stride_h, stride_w;

    /**
     * @brief Specifies mapping related information. Note space parallelism is not specified here because for each
     * PEC, it only needs its unique space parameters. Ch0 = Cf x Gf x CMf x GMf and thus we do not need to specify
     * Cf, Gf, CMf, GMf separately.
     *
     */
    u8 E1, R1, Cv, E0, F0, S, R;
    u8 N3, N2, G3, G2, E3, R3, M2, F3;

    /**
     * @brief Specifies PEC space parallelism information. For Ifmap, G1 and C1 can be treated together.
     *
     */
    u8 G1_C1, N1, M1, E2;

    /**
     * @brief Padding for top and left. Note compiler should verify padding constraints and ensure they are met.
     * Firmware will not check the legitimacy again! Padding also take strides in height and width into account. If they
     * does not begin at the origin, then pad_h_t and pad_w_l should subtract the offsets in height and width
     * correspondingly.
     *
     */
    s16 pad_h_t, pad_w_l;
    u16 M3, E4, C3, C2;
} __attribute__((aligned(CDMA_TRANSFER_BYTE_ALIGN))) rdma_dev_com_inf_t;

union narrator_dev_desc_t {
    struct {
        /**
         * @brief Specifies PEC space parallelism information. For Ifmap, g1 and c1 can be treated together. That
         * means g1_c1 = g1 * C1 + c1;
         *
         */
        u8 g1_c1, n1, m1, e2;
    };
    struct {
        /**
         * @brief Specifies the low 32 bits part of RDMA weight physical base address.
         *
         */
        u32 weight_base_addr;
    };
};

/**
 * @brief Specifies device convolution related information sent to Master node.
 *
 */
typedef struct _rdma_dev_master_inf {
    /**
     * @brief Specifies RDMA (Including WIG) related configurations.
     * rdma and wig broadcast register bitmask
     * 10'h008	IFM_WT_CR           bit0  -> 0
     * 10'h00c	IFM_SADDR_H         bit1  -> 0
     * 10'h010	IFM_SADDR_L         bit2  -> 0
     * 10'h014	WT_INFO_SADDR_H     bit3  -> 0
     * 10'h018	WT_INFO_SADDR_L     bit4  -> 0
     * 10'h01c	EMPTY_HOLE          bit5  -> 0
     * 10'h020	CHANEL_SIZE_0       bit6  -> 0  // use default
     * 10'h024	CHANEL_SIZE_1       bit7  -> 0  // use default
     * 10'h028	CHANEL_SIZE_2       bit8  -> 0  // use default
     * 10'h02c	CHANEL_SIZE_3       bit9  -> 0  // use default
     * 10'h030	WIG_BASE_ADDR_0     bit10 -> 0
     * 10'h034	WIG_BASE_ADDR_1     bit11 -> 0
     * 10'h038	WIG_BASE_ADDR_2     bit12 -> 0
     * 10'h03c	WIG_BASE_ADDR_3	    bit13 -> 0
     * 10'h040	LOOP_NUM_2_CH0      bit14 -> 1
     * 10'h044	LOOP_NUM_1_CH0      bit15 -> 1
     * 10'h048	LOOP_NUM_0_CH0      bit16 -> 1
     * 10'h04c	EMPTY_HOLE          bit17 -> 0
     * 10'h050	OFFSET_9_CH0        bit18 -> 1
     * 10'h054	OFFSET_8_CH0        bit19 -> 1
     * 10'h058	OFFSET_7_CH0        bit20 -> 1
     * 10'h05c	OFFSET_6_CH0        bit21 -> 1
     * 10'h060	OFFSET_5_CH0        bit22 -> 1
     * 10'h064	OFFSET_4_CH0        bit23 -> 1
     * 10'h068	OFFSET_3_CH0        bit24 -> 1
     * 10'h06c	OFFSET_2_CH0        bit25 -> 1
     * 10'h070	OFFSET_1_CH0        bit26 -> 1
     * 10'h074	OFFSET_0_CH0        bit27 -> 1
     * 10'h078  OFFSET_10_CH0       bit28 -> 1
     * old rdma_and_wig_bitmap = 0x7FFE1E0
     * new rdma_and_wig_bitmap = 0x1FFDC000
     **/

    u32 rdma_and_wig_bitmap;
    /**
     * @brief Specifies rdma and wig register configurations.
     *
     */
    u32 regs[0];
} rdma_dev_master_inf_t;

/**
 * @brief Specifies PreDRP register configurations.
 *
 */
union pre_drp_regs_t {
    struct {
        /* registers */
        u32 predrp_ctrl;
        u32 n2_stride;
        u32 g2_stride;
        u32 e3_stride;
        u32 m2_stride;
        u32 m_stride;
        u32 g3_threshold;
        u32 n3_threshold;
        u32 m3_threshold;
        u32 e4_threshold;
        u32 f3_threshold;
        u32 pe_num;
        u32 predrp_size_glb;

        u32 reshape_ctrl;
        u32 g_stride_glb;
        u32 n_stride_glb;
        u32 e_stride_glb;
        u32 m_stride_glb;
        u32 n_stride_sram;
        u32 h_stride_sram;
        u32 c_stride_sram;
        /*rtl remove 2 registers, the address is not continues */
        u32 imap_para_l;
        u32 omap_para_rsp_w;
        u32 layer_para_l;
        u32 layer_para_m;
        u32 layer_para_h;
        u32 glb_para_l;
        u32 glb_para_h;
        u32 omap_para_l;
        u32 reshape_size_glb;
        u32 precision_ctrl_l;
        u32 precision_ctrl_h;
    };
    u32 regs[0];
};

/**
 * @brief Specifies the PEC related information that is sent to Master node.
 *
 */
typedef struct _pec_dev_master_inf {
    /**
     * @brief Specifies the length of this data structure in DWs(4 bytes).
     *
     */
    u8 struct_len;

    /**
     * @brief Specifies if each DW register exists in this data structure and should be broadcasted in this node.
     *
     */
    u16 reg_bcast_bitmap;

    /**
     * @brief Specifies PEC active bitmap.
     *
     */
    u32 pec_active_bitmap;
    /**
     * @brief Specifies Pre-data-reshaper registers.
     *
     */
    union pre_drp_regs_t pre_drp_regs;

    /**
     * @brief Specifies PEC related registers.
     *
     */
    u32 regs[0];
} pec_dev_master_inf_t;

/**
 * @brief Specifies the PEC related information that is sent to each Major node.
 *
 */
typedef struct _pec_dev_major_inf {
    /**
     * @brief Specifies the length of this data structure in DWs(4 bytes).
     *
     */
    u8 struct_len;

    /**
     * @brief Specifies if each DW register exists in this data structure and should be multicasted in this node.
     *
     */
    u8 reg_mcast_bitmap;

    /**
     * @brief Specifies if each DW register exists in this data structure and should be uniquecasted in this node.
     *
     */
    u8 reg_ucast_bitmap;
    /**
     * @brief Specifies if each PEC should be enabled for trigger. If a given bit is 1, then the corresponding PEC
     * should set pe_en_trig register to 2 so that it can be triggered by broadcasting pe_en_trigger.
     *
     */
    u8 pec_en_bitmap;

    /**
     * @brief Specifies PEC register configurations.
     *
     */
    u32 regs[0];
} pec_dev_major_inf_t;

/**
 * @brief Specifies device convolution related information separately passed to each Major node. This data structure
 * has variable length depending on the number of active Ifmap narrators.
 *
 */
typedef struct _rdma_dev_major_inf {
    /**
     * @brief Specifies the length of this data structure in DWs(4 bytes).
     *
     */
    u8 struct_len;

    /**
     * @brief Each bit specifies a given PEC is a narrator if corresponding bit is 1.
     *
     */
    u8 weight_narrator_bitmap;
    u8 ifmap_narrator_bitmap;

    /**
     * @brief Specifies if each RDMA should be enabled for trigger. If rdma_en_flag is true, then the corresponding
     * RDMA should set op_en_trig register to 2 so that it can be triggered by broadcasting pe_en_trigger.
     *
     */
    u8 rdma_en_flag : 1;

    /**
     * @brief Specifies if ifmap_weight_ctrl should be set in each Major core.
     *
     */
    u8 is_ctrl_valid : 1;

    /**
     * @brief Specifies the value to be configured to RDMA Ifmap weight control register.
     *
     */
    u32 ifmap_weight_ctrl;

    /**
     * @brief Each element corresponds to a given weight or Ifmap if corresponding bit in weight_narrator_bitmap or
     * ifmap_narrator_bitmap is 1.
     * Note if weight_narrator_bitmap has 3 outstanding narrators and ifmap_narrator_bitmap has 2 outstanding
     * narrators, then narrator_desc[0~2] correspond to weight narrators and narrator_desc[3~4] correspond to Ifmap
     * narrators.
     *
     */
    union narrator_dev_desc_t narrator_desc[0];
} rdma_dev_major_inf_t;

/**
 * @brief Specifies the maximum size of conv_dev_t in Program node.
 * @todo paragraph describing what is to be done
 */
#define MAX_PROGRAM_CONV_CONFIG_SIZE                                                                     \
    ROUND_UP(sizeof(rdma_dev_com_inf_t) + sizeof(rdma_dev_master_inf_t) + sizeof(pec_dev_master_inf_t) + \
                 sizeof(conv_program_t) + 136,                                                           \
             CDMA_TRANSFER_BYTE_ALIGN)

typedef struct _conv_program_data {
    u8 program_data[MAX_PROGRAM_CONV_CONFIG_SIZE];
} conv_program_data_t;

typedef struct _major_core_info {
    rdma_dev_major_inf_t rdma_info;
    pec_dev_major_inf_t pec_info;
} major_core_info_t;

#define NUM_LINEAR_EXP_TABLE_ENTRIES 65
#define NUM_LINEAR_ONLY_TABLE_ENTRIES 257
typedef struct _lut_dev {
    u8 precision;
    u32 lut_cfg;
    u32 lut_info;
    u32 le_start;
    u32 le_end;
    u32 lo_start;
    u32 lo_end;
    u32 le_slope_scale;
    u32 lo_slope_scale;
    u32 le_slope_shift;
    u32 lo_slope_shift;

    u16 linear_exp_table[NUM_LINEAR_EXP_TABLE_ENTRIES];
    u16 linear_only_table[NUM_LINEAR_ONLY_TABLE_ENTRIES];
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) lut_dev_t;

/*** sdp ******/
// 35 regs for sdp, 28 regs for sdp_rdma, 15 regs for post_drp. 78 total
typedef struct _sdp_program {
    union {
        u64 u84_bitmap;
        u32 e31_bitmap[0];
    };
    union {
        u64 u84_drp_bitmap;
        u32 e31_drp_bitmap[0];
    };
    union {
        u64 u84_rdma_bitmap;
        u32 e31_rdma_bitmap[0];
    };
    u32 reg[SDP_REG_MAX];
    u32 sdp_drp_reg[POST_DRP_REG_MAX];
    u32 sdp_rdma_reg[SDP_RDMA_REG_MAX];

    u8 input_tensor_idx;
    u8 output_tensor_idx;
    u8 x1_tensor_idx;
    u8 x2_tensor_idx;
    u8 y_tensor_idx;
    u8 is_rdma;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN), packed)) sdp_program_t;

typedef struct _sdp_dev_t {
    npu_dep_info_t npu_info;
    sdp_program_t prog_data;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) sdp_dev_t;

typedef struct _dsp_program {
    u64 rsv;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN), packed)) dsp_program_t;

typedef struct _dsp_dev_t {
    npu_dep_info_t npu_info;
    dsp_program_t prog_data;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) dsp_dev_t;

/*** pdp 41 regs ******/
typedef struct _pdp_program {
    union {
        u64 u84_bitmap;
        u32 e31_bitmap[0];
    };
    union {
        u64 u84_rdma_bitmap;
        u32 e31_rdma_bitmap[0];
    };
    union {
        u64 u84_drp_bitmap;
        u32 e31_drp_bitmap[0];
    };
    u32 reg[PDP_REG_MAX];
    u32 pdp_rdma_reg[PDP_RDMA_REG_MAX];
    u32 pdp_drp_reg[PDP_DRP_REG_MAX];

    u8 input_tensor_idx;
    u8 output_tensor_idx;
    u8 is_rdma;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN), packed)) pdp_program_t;

typedef struct _pdp_dev {
    npu_dep_info_t npu_info;
    pdp_program_t prog_data;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) pdp_dev_t;

/*** rubik ******/
typedef struct _rubik_program {
    u64 bitmap;
    union {
        u64 u84_bitmap;
        u32 e31_bitmap[0];
    };
    u32 reg[RUBIK_REG_MAX];
    u8 input_tensor_idx;
    u8 output_tensor_idx;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN), packed)) rubik_program_t;

typedef struct _rubik_dev {
    npu_dep_info_t npu_info;
    rubik_program_t prog_data;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) rubik_dev_t;

/*** event ******/
typedef struct _event_sink_program {
    u32 reserved;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN), packed)) event_sink_program_t;

typedef struct _event_sink_dev {
    npu_dep_info_t npu_info;
    event_sink_program_t prog_data;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) event_sink_dev_t;

typedef struct _event_source_program {
    u32 reserved;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN), packed)) event_source_program_t;

typedef struct _event_source_dev {
    npu_dep_info_t npu_info;
    event_sink_program_t prog_data;
} __attribute__((aligned(CDMA_SRC_BYTE_ALIGN))) event_source_dev_t;

STATIC_ASSERT(sizeof(rdma_dev_com_inf_t) % CDMA_TRANSFER_BYTE_ALIGN == 0);

/**
 * @brief Specifies maximum number of narrator descriptors in each dev_rdma_major_inf_t instance.
 *
 */
static const u32 max_num_narrator_desc = NUM_PEC_COLUMN * 2;

/**
 * @brief Specifies the optimal buffer alignment for Ifmap, Ofmap and weight in convolution unit.
 *
 */
static const u32 device_buffer_align = 64;

/**
 * @brief Specifies the maximum size of conv_dev_t in Emission node.
 * @todo paragraph describing what is to be done
 */
static const u32 MAX_EMISSION_CONV_CONFIG_SIZE =
    ROUND_UP(sizeof(rdma_dev_com_inf_t) + sizeof(npu_dep_info_t) + sizeof(conv_dev_t), CDMA_TRANSFER_BYTE_ALIGN);

/**
 * @brief Specifies the maximum size of conv_dev_t in Major node.
 * @todo
 */
#define MAX_MAJOR_CONFIG_SIZE                                                                               \
    ROUND_UP(sizeof(rdma_dev_com_inf_t) + sizeof(rdma_dev_major_inf_t) + sizeof(pec_dev_major_inf_t) + 112, \
             CDMA_TRANSFER_BYTE_ALIGN)

static const u32 MAX_CONV_PROG_SIZE =
    (MAX_EMISSION_CONV_CONFIG_SIZE + MAX_PROGRAM_CONV_CONFIG_SIZE + MAX_MAJOR_CONFIG_SIZE * NUM_MAJOR_CORES);

#if NPU_DEV_SIM != NPU_REAL_ENV
typedef struct _conv_extra_data {
    struct npu_conv_op_desc conv_op_desc;
    struct npu_conv_surface_desc conv_surface_desc;
} conv_extra_data_t;

typedef struct _conv_cmodel_data {
    conv_program_t conv_prog_desc;
    u64 conv_extra_desc_addr;
} conv_cmodel_data_t;

#define CONV_EXTRA_DATA_SIZE (sizeof(struct npu_conv_op_desc) + sizeof(struct npu_conv_surface_desc))
#else
#define CONV_EXTRA_DATA_SIZE 0U
#endif

#define MAX_CONV_PROG_DATA_SIZE (MAX_CONV_PROG_SIZE + CONV_EXTRA_DATA_SIZE)

typedef struct _host_frame_done {
    u32 param;
    u32 stat_addr;
} __attribute__((packed)) host_frame_done_t;

typedef struct _model_stat {
    npu_e31_perf_t op_stats[MAX_OP_NUM];
} model_stat_t;

typedef struct _model_op_stat {
    npu_e31_perf_t op_stats[NUM_OP_TYPE];
} model_op_stat_t;

typedef struct _cdma_config_param {
    union npu_cdma_misc_ctl_t misc_ctl;
    union npu_cdma_misc_cfg0_t misc_cfg0;
    union npu_cdma_misc_cfg1_t misc_cfg1;
    union npu_cdma_err_intr_clr_t intr_clr;
} cdma_config_param_t;

#ifdef __cplusplus
}
#endif

#endif
