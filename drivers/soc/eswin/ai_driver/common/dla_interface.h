// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe root complex driver
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

#ifndef _DLA_INTERFACE_H_
#define _DLA_INTERFACE_H_

#include "hetero_env.h"

#if defined(__KERNEL__)
#include <nvdla_interface.h>
#else
#include <stdint.h>
#endif
#include "es_nn_common.h"
/*
 * alignment
 */
#define ALIGNMENT (64)

/**
 * @ingroup Processors
 * @name Maximum number of processors
 * @{
 */
#define HW_OP_NUM 0x20
/** @} */

/**
 * @ingroup Processors
 * @name NPU Processors
 * Processor modules in NPU engine. Each processor has it's
 * own operation a.k.a. HW layer. Network is formed using
 * graph of these operations
 * @{
 */

#define DLA_OP_EDMA 0
#define DLA_OP_CONV 1
#define DLA_OP_SDP 2
#define DLA_OP_PDP 3
#define DLA_OP_RUBIK 4
#define DLA_KMD_OP_DSP_0 5
#define DLA_KMD_OP_DSP_1 6
#define DLA_KMD_OP_DSP_2 7
#define DLA_KMD_OP_DSP_3 8
#define DLA_OP_EVENT_SINK 9
#define DLA_OP_EVENT_SOURCE 0xa
#define DLA_OP_DSP_0 0xb
#define DLA_OP_DSP_1 0xc
#define DLA_OP_DSP_2 0xd
#define DLA_OP_DSP_3 0xe
#define DLA_OP_HAE 0xf
#define DLA_OP_GPU 0x10
#define DLA_OP_SWITCH 0x11
#define DLA_OP_MERGE 0x12

/**
 * @name Memory types
 * @brief DLA engnine can read/write to/from 3 memory types
 * @{
 */
#define DLA_MEM_MC 0 /* External DRAM */
#define DLA_MEM_CV 1 /* CV-SRAM */
#define DLA_MEM_HW 2 /* DLA sub-module */
/** @} */

/**
 * @ingroup Events
 * @name Operation events
 * @brief Different events triggered by an operations
 * @{
 */
#define DLA_EVENT_OP_COMPLETED 1
#define DLA_EVENT_OP_PROGRAMMED 2
#define DLA_EVENT_OP_ENABLED 3
#define DLA_EVENT_CDMA_WT_DONE 4
#define DLA_EVENT_CDMA_DT_DONE 5
/** @} */

/**
 * @ingroup Processors
 * @name Precision types
 * @brief Precision formats supported by DLA engine
 * @{
 */
#define PRECISION_INT8 0
#define PRECISION_INT16 1
#define PRECISION_FP16 2
#define PRECISION_FP32 3
/** @} */

/**
 * @ingroup Processors
 * @name Bpe precision types
 * @brief bytes per element, used by rubik
 * @{
 */
#define BPE_PRECISION_INT8 1
#define BPE_PRECISION_INT16 2
#define BPE_PRECISION_FP16 2
/** @} */

/**
 * @ingroup Processors
 * @name Data formats
 * @brief Data formats supported by DLA engine
 * @{
 */
#define FORMAT_T_R8 0
#define FORMAT_T_R10 1
#define FORMAT_T_R12 2
#define FORMAT_T_R16 3
#define FORMAT_T_R16_I 4
#define FORMAT_T_R16_F 5
#define FORMAT_T_A16B16G16R16 6
#define FORMAT_T_X16B16G16R16 7
#define FORMAT_T_A16B16G16R16_F 8
#define FORMAT_T_A16Y16U16V16 9
#define FORMAT_T_V16U16Y16A16 10
#define FORMAT_T_A16Y16U16V16_F 11
#define FORMAT_T_A8B8G8R8 12
#define FORMAT_T_A8R8G8B8 13
#define FORMAT_T_B8G8R8A8 14
#define FORMAT_T_R8G8B8A8 15
#define FORMAT_T_X8B8G8R8 16
#define FORMAT_T_X8R8G8B8 17
#define FORMAT_T_B8G8R8X8 18
#define FORMAT_T_R8G8B8X8 19
#define FORMAT_T_A2B10G10R10 20
#define FORMAT_T_A2R10G10B10 21
#define FORMAT_T_B10G10R10A2 22
#define FORMAT_T_R10G10B10A2 23
#define FORMAT_T_A2Y10U10V10 24
#define FORMAT_T_V10U10Y10A2 25
#define FORMAT_T_A8Y8U8V8 26
#define FORMAT_T_V8U8Y8A8 27
#define FORMAT_T_Y8___U8V8_N444 28
#define FORMAT_T_Y8___V8U8_N444 29
#define FORMAT_T_Y10___U10V10_N444 30
#define FORMAT_T_Y10___V10U10_N444 31
#define FORMAT_T_Y12___U12V12_N444 32
#define FORMAT_T_Y12___V12U12_N444 33
#define FORMAT_T_Y16___U16V16_N444 34
#define FORMAT_T_Y16___V16U16_N444 35
#define FORMAT_FEATURE 36
/** @} */

/*
 * version field
 */
#define NPU_INTERFACE_MAJOR_VERSION 0x00
#define NPU_INTERFACE_MINOR_VERSION 0x00
#define NPU_INTERFACE_SUBMINOR_VERSION 0x03

struct npu_version {
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t subminor_version;
    uint8_t reserved;
} __attribute__((packed, aligned(ALIGNMENT)));

/**
 * Network descriptor
 *
 * Contains all information to execute a network
 *
 * @op_head: Index of first operation of each type in operations list
 * @num_rois: Number of ROIs
 * @num_operations: Number of operations in one list
 * @num_luts: Number of LUTs
 */
struct dla_network_desc {
    struct npu_version version;
    uint32_t reserved;
    int16_t operation_desc_index;
    int16_t surface_desc_index;

    int16_t dependency_graph_index;
    int16_t lut_data_index;
    int16_t op_config_index;

    uint16_t num_operations;
    uint16_t num_event_ops;

    uint16_t num_luts;
    uint16_t num_addresses;
    uint16_t reserved0;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_data_cube {
    uint16_t type;   /* dla_mem_type */
    int16_t address; /* offset to the actual IOVA in task.address_list */

    uint32_t offset; /* offset within address */
    uint32_t size;

    /* cube dimensions */
    uint16_t batch;
    uint16_t width;
    uint16_t height;

    uint16_t channel;
    uint16_t reserved0;

    /* stride information */
    uint32_t line_stride;
    uint32_t surf_stride;

    /* For Rubik only */
    uint32_t plane_stride;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_consumer {
    int16_t index;
    uint8_t event;
    uint8_t res;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_common_op_desc {
    int16_t index; /* set by ucode */
    int8_t roi_index;
    uint8_t op_type;

    uint8_t dependency_count;
    uint8_t reserved0[3]; /* esim_tool uses reserved0[2] to save offset of op_index */

    struct dla_consumer consumers[HW_OP_NUM];
    struct dla_consumer fused_parent;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_event_op_desc {
    int16_t index;         // a unique event op index in loadable
    int8_t submodel_type;  // 0-umd; 1-kmd
} __attribute__((packed, aligned(ALIGNMENT)));

#define EVENT_OP_TENSOR_NUM 8
struct event_surface_desc {
    struct dla_data_cube data[EVENT_OP_TENSOR_NUM];
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_cvt_param {
    int16_t scale;
    uint8_t truncate;
    uint8_t enable;

    int32_t offset;
} __attribute__((packed, aligned(ALIGNMENT)));

struct npu_edma_surface_desc {
    struct dla_data_cube src_data;
    struct dla_data_cube dst_data;
} __attribute__((packed, aligned(ALIGNMENT)));

struct npu_edma_op_desc {
    uint32_t input_c0_bytes;
    uint32_t src_num_line;
    uint32_t src_stride_line_bytes;
    uint32_t src_num_surface;
    uint32_t src_stride_surface_bytes;
    uint32_t src_num_cube;
    uint32_t src_stride_cube_bytes;
    uint32_t src_num_colony;

    uint32_t output_c0_bytes;
    uint32_t dst_num_line;
    uint32_t dst_stride_line_bytes;
    uint32_t dst_num_surface;
    uint32_t dst_stride_surface_bytes;
    uint32_t dst_num_cube;
    uint32_t dst_stride_cube_bytes;
    uint32_t dst_num_colony;
} __attribute__((packed, aligned(ALIGNMENT)));

/*
 * Post DRP op description
 *
 * ref: https://ekm.eswincomputing.com/preview.html?fileid=1170951
 *
 */
struct post_drp_op_desc {
    // control info
    uint32_t op_en_trig;

    // 0 - idle; 1 - running
    uint32_t op_en_status;

    // represent the write out data stride for SRAM level ofmap
    /*
     * The step value in the G direction at the SRAM level for OFMAP: if the data type is int8,
     * then g_stride_sram = N * E * M * F; otherwise, g_stride_sram = 2 * N * E * M * F,
     * used for calculating the address when writing data to LSRAM via DRP.
     */
    uint32_t g_stride_lsram;

    /*
     * The step value in the N direction at the SRAM level for OFMAP: if the data type is int8,
     * then n_stride_sram = E * M * F; otherwise, n_stride_sram = 2 * E * M * F,
     * used for calculating the address when writing data to LSRAM via DRP.
     */
    uint32_t n_stride_lsram;

    /*
     * The step value in the H direction at the SRAM level for OFMAP: if the data type is int8, 
     * then h_stride = F * C0 * C'' * C'''; otherwise, h_stride = 2 * F * C0 * C'' * C''',
     * used for calculating the address when writing data to LSRAM via DRP.
     */
    uint32_t h_stride;

    /*
     * The step value in the C' direction at the SRAM level for OFMAP: if the data type is int8, 
     * then c_stride = E * F * C0 * C'' * C'''; otherwise, c_stride = 2 * E * F * C0 * C'' * C''',
     * used for calculating the address when writing data to LSRAM via DRP.
     */
    uint32_t c_stride;

    /*
     * The address calculation for extension in the W direction based on the C direction: if the data type is int8,
     * then w_ext_stride = C'' * C0; otherwise, w_ext_stride = 2 * C'' * C0.
     */
    uint32_t w_stride;

    /* represent the whole layer parameter */
    uint32_t n;  /* N in one glb, N_glb = N0*N1*N2 */
    uint32_t e;  /* E in one glb, E_glb = 4*E2*E3 or E1_last_cnt */
    uint32_t m;  /* M and G in one glb, MG_glb = M0*M1*M2*G0*G1*G2 */
    uint32_t f;  /* the whole layer F */
    uint32_t c0; /* the next layer input c0 */

    // The high 32 bits of the base address for storing the reshap data output by DRP.
    uint32_t base_addr_ofmap_h;
    // The low 32 bits of the base address for storing the reshap data output by DRP
    uint32_t base_addr_ofmap_l;

    // 0 - int8; 1 - int16/fp16
    uint32_t type_16;

    // 1 : int8 -> int16; otherwise, 0
    uint32_t surface_double;

    // For PDP, The split num of PDP is the same.;
    // For SDP, split_num = F3, F is the ofm's W
    uint32_t split_num;
    uint16_t f_lst; /* The width of the last segment when F is split. */
    uint16_t f_mid; /* The width of the intermediate segment when F is split. */
    uint16_t f_fst; /* The width of the first segment when F is split. */
    uint16_t Reserved;

    uint16_t e4_all; /* original E */
    uint16_t m3_all; /* m3_all = g_glb* original_M, g_glb = G1*G2   */
    uint16_t n3_all; /* original N */
    uint16_t g3_all; /* g3_all = original_G * original_M  */
} __attribute__((packed, aligned(ALIGNMENT)));

/**
 * @ingroup Convolution
 * @name Convolution mode
 * @brief Convolution modes support by DLA
 * @{
 */
#define CONV_MODE_DIRECT 0
#define CONV_MODE_WINOGRAD 1
/** @} */

/**
 * @ingroup SDP
 * @name Activation functions
 * @brief Activation functions supported in SDP
 * @{
 */
#define ACTIVATION_NONE 0
#define ACTIVATION_RELU 1
#define ACTIVATION_LUT 2
#define ACTIVATION_PRELU 3
/** @} */

/**
 * @ingroup LUT
 * @name LUT size
 * @brief LUT sizes for linear and exponentila LUT
 * @{
 */
#define LUT_LINEAR_EXP_TABLE_ENTRY_LOG2 6
#define LUT_LINEAR_ONLY_TABLE_ENTRY_LOG2 8
/** @} */

/**
 * @ingroup LUT
 * @name LUT types
 * @brief DLA supports two types of LUT, linear and exonential
 * @{
 */
#define LUT_LINEAR_EXP_TABLE 0
#define LUT_LINEAR_ONLY_TABLE 1
/** @} */

/**
 * @ingroup LUT
 * @name LUT methods
 * @brief DLA supports two types of LUT, linear and exonential
 * @{
 */
#define LUT_METHOD_EXPONENTIAL 0
#define LUT_METHOD_LINEAR 1
/** @} */

/**
 * @ingroup LUT
 * @name LUT
 * @brief DLA supports two types of LUT, linear and exonential
 * @{
 */
#define LUT_PRI_LINEAR_EXP 0
#define LUT_PRI_LINEAR_ONLY 1
/** @} */

union dla_lut_offset {
    /**
     * Number should be subtracted on log domain before look up
     * exponential table it has the same definition as hardware
     * thus input scaling should also take into account when
     * set this field.
     */
    int8_t exp_offset;
    /**
     * Number of bits should be right shift before looking
     * up linear table
     */
    int8_t frac_bits;
    uint16_t reserved0;
} __attribute__((packed, aligned(ALIGNMENT)));

/**
 * This struct is used to represent floating point values by INT
 * suppose we have a float point number fp_x, it will be represented
 * as:
 *
 * fp_x = scale_int_x>>(shifter_x)
 *
 * This is very useful for INT pipeline;
 */
struct dla_float_data {
    int16_t scale;
    int8_t shifter;
    uint8_t reserved0;
} __attribute__((packed, aligned(ALIGNMENT)));

/**
 * For INT pipeline, we use the struct above to represent a floating number;
 * For FP16 pipeline, we should store the FP16 encoded value into a uint16_t
 * container
 */
union dla_slope {
    struct dla_float_data data_i;

    uint16_t data_f;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_sdp_surface_desc {
    /* Data cube */
    /* source input cube, available when SDP working on offline mode */
    struct dla_data_cube src_data;

    /* X1 input cube */
    struct dla_data_cube x1_data;

    /* X2 input cube */
    struct dla_data_cube x2_data;

    /* Y input cube */
    struct dla_data_cube y_data;

    /* Output cube */
    struct dla_data_cube dst_data;
} __attribute__((packed, aligned(ALIGNMENT)));

#define SDP_OP_NONE 0
#define SDP_OP_MUL 1
#define SDP_OP_ADD 2
#define SDP_OP_BOTH 3

#define SDP_ALU_OP_MAX 0
#define SDP_ALU_OP_MIN 1
#define SDP_ALU_OP_SUM 2
#define SDP_ALU_OP_EQL 3

#define SDP_OP_PER_LAYER 0
#define SDP_OP_PER_KERNEL 1
#define SDP_OP_PER_POINT 2

struct dla_sdp_cvt {
    struct dla_cvt_param alu_cvt;
    struct dla_cvt_param mul_cvt;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_sdp_op {
    uint8_t enable;
    uint8_t alu_type; /* dla_sdp_alu_op_type */
    uint8_t type;     /* dla_sdp_op_type */
    uint8_t mode;     /* dla_sdp_op_mode */

    uint8_t act;          /* dla_act_type */
    uint8_t shift_value;  // left shift
    uint8_t truncate;
    uint8_t precision;  // 0 1 2 3

    int32_t alu_operand;
    int32_t mul_operand;

    struct dla_sdp_cvt cvt;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_sdp_op_desc {
    /* Precision parameters */
    /* dla_precision */
    uint8_t src_precision;
    uint8_t dst_precision;
    int16_t lut_index;

    struct dla_cvt_param out_cvt;

    /* Performance parameters */
    /* dla_conv_mode */
    uint8_t conv_mode;
    uint8_t batch_num;
    uint16_t reserved0;

    uint32_t batch_stride;  // will be used when batch_num > 1

    /* Algorithm parameters */
    struct dla_sdp_op x1_op;
    struct dla_sdp_op x2_op;
    struct dla_sdp_op y_op;

    struct post_drp_op_desc post_drp_op;
} __attribute__((packed, aligned(ALIGNMENT)));

#define POOL_MODE_AVG 0
#define POOL_MODE_MAX 1
#define POOL_MODE_MIN 2

#define POOL_SIZE_1 0
#define POOL_SIZE_2 1
#define POOL_SIZE_3 2
#define POOL_SIZE_4 3
#define POOL_SIZE_5 4
#define POOL_SIZE_6 5
#define POOL_SIZE_7 6
#define POOL_SIZE_8 7

#define PDP_PAD_VAL_NUM 7

struct dla_pdp_surface_desc {
    /* Data cube */
    struct dla_data_cube src_data;

    struct dla_data_cube dst_data;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_pdp_op_desc {
    /* Performance parameters */
    uint16_t partial_in_width_first;
    uint16_t partial_in_width_mid;

    uint16_t partial_in_width_last;
    uint16_t partial_width_first;

    uint16_t partial_width_mid;
    uint16_t partial_width_last;

    uint8_t split_num;

    /* Algorithm parameters */
    uint8_t pool_mode;   /* dla_pool_mode */
    uint8_t pool_width;  /* dla_pool_width */
    uint8_t pool_height; /* dla_pool_height */

    uint8_t stride_x;
    uint8_t stride_y;

    /* The left/right padding size, pad_right might be less than pad_left */
    uint8_t pad_left;
    uint8_t pad_right;

    /* The top/bottom padding size */
    uint8_t pad_top;
    uint8_t pad_bottom;

    /* Precision parameters */
    uint8_t precision; /* dla_precision */
    uint8_t reserved0;
    /**
     * if input has non-zero "offset", this value should be set
     * There'll be 7 different paddding values, the relationship between
     * those versions are:
     * padding_value[0] = -offset*scaling;
     * padding_value[1] = 2*padding_value[0]
     * padding_value[2] = 3*padding_value[0]
     * ...
     * The purpose is to avoid ucode implement FP16 multiplier(for FP16 mode)
     */
    int32_t padding_value[PDP_PAD_VAL_NUM];

    struct post_drp_op_desc post_drp_op;
} __attribute__((packed, aligned(ALIGNMENT)));

struct dla_rubik_surface_desc {
    /* Data cube */
    struct dla_data_cube src_data;

    struct dla_data_cube dst_data;
} __attribute__((packed, aligned(ALIGNMENT)));

/* rubik mode */
#define RUBIK_MODE_CONTRACT 0
#define RUBIK_MODE_SPLIT 1
#define RUBIK_MODE_MERGE 2

struct dla_rubik_op_desc {
    /* Precision parameters */
    uint8_t mode;
    uint8_t precision;
    uint8_t stride_x;
    uint8_t stride_y;
} __attribute__((packed, aligned(ALIGNMENT)));

#define KERNEL_NAME_MAXLEN 128
#define KERNEL_LIB_NAME_MAXLEN 128
#define BUFFER_CNT_MAXSIZE 32

struct dsp_op_desc {
    /* *
     * Total byte size of this data structure.
     * */
    uint32_t total_size;
    /* *
     * The authoritative name of the operator.
     * */
    char operator_name[KERNEL_NAME_MAXLEN];
    /* *
     * Specify total number of parameter buffers.
     * */
    uint32_t buffer_cnt_cfg;
    /* *
     * Specify total number of input buffers.
     * */
    uint32_t buffer_cnt_input;
    /* *
     * Specify total number of output buffers.
     * */
    uint32_t buffer_cnt_output;
    /* *
     * Specify the byte size of each buffer. This is an array describing the
     * size of each buffer including parameter, input and output buffers. They
     * are sequentially placed in this array.
     * */
    uint32_t buffer_size[BUFFER_CNT_MAXSIZE];
    /* *
     * This is a variable length field which holds parameter information. All
     * parameter buffers are sequentially laid out in this field.
     * */
    // char param_data[0];

    uint32_t dsp_core_id;
    uint32_t mem_id;
    uint32_t offset;
} __attribute__((packed, aligned(ALIGNMENT)));

#define DSP_KERNEL_MAX_INOUT_TENSOR_NUM 8
struct dsp_surface_desc {
    struct dla_data_cube src_data[DSP_KERNEL_MAX_INOUT_TENSOR_NUM];
    struct dla_data_cube dst_data[DSP_KERNEL_MAX_INOUT_TENSOR_NUM];
} __attribute__((packed, aligned(ALIGNMENT)));

struct hae_op_desc {
    /* Source color format*/
    ES_COLOR_CODE_E srcFormat;

    /* Interpolation method to be used for resizing */
    ES_INTER_FLAG_E inter;

    /* Dest Data precision types */
    ES_DATA_PRECISION_E dstDataType;

    /* Normalization mode (e.g., Z-Score or Min-Max scaling) */
    ES_NORM_MODE_E mode;

    /* Normalization factors for each channel (r, g, b, x/alpha). Can represent 1/(max-min) or 1/std */
    ES_FLOAT normFactor[4];

    /* Bias values for each channel (r, g, b, x/alpha) for normalization. Can represent min or mean */
    ES_FLOAT bias[4];

    /* Scaling factor applied post-normalization(for quant) */
    ES_FLOAT scale;

    /* Bitwise flag set by the SET_OP_FLAG macro. Each bit represents an operation's enabled status. */
    ES_S32 flag;
} __attribute__((packed, aligned(ALIGNMENT)));

struct hae_surface_desc {
    struct dla_data_cube src_data;
    struct dla_data_cube dst_data;
} __attribute__((packed, aligned(ALIGNMENT)));

struct gpu_op_desc {
} __attribute__((packed, aligned(ALIGNMENT)));
struct gpu_surface_desc {
} __attribute__((packed, aligned(ALIGNMENT)));

struct cpu_op_desc {
} __attribute__((packed, aligned(ALIGNMENT)));
struct cpu_surface_desc {
} __attribute__((packed, aligned(ALIGNMENT)));

typedef struct LayerInfo {
    int g;
    int n;
    int h;
    int w;
    int c;
    int m;
    int r;
    int s;
    int e;
    int f;
    int stride_h;
    int stride_w;
    int pad_up;
    int pad_down;
    int pad_left;
    int pad_right;
} LayerInfo;

typedef struct MappingInfo_t {
    int G4, N4, M4, E5;
    int F3, G3, N3, M3, E4, C3;
    int N2, C2, E3, R3, M2;
    int G1, N1, M1, E2, R2, C1, E1, R1;
    int E0, N0, F0, S, C0, M0;
} MappingInfo;

#define NOC_MAX_LEVEL_SIZE 6
#define GLB_MAX_LEVEL_SIZE 5
#define SRAM_MAX_LEVEL_SIZE 6

#define DYN_LOOPS_MAX_ROWS 24
#define MAC_COL 8
#define MAC_ROW 10
#define MAX_MAPPING 80

typedef struct coordinate_t {
    uint8_t x;
    uint8_t y;
    uint8_t broader;  // 1 is broader
} coordinate_t;

typedef struct broader_mapping {
    coordinate_t listener[MAX_MAPPING];
    coordinate_t broader[MAX_MAPPING];
    uint8_t used_count;
} broader_mapping_t;

typedef enum noc_order_dim_t {
    NOC_DIM_E = 0,  // E2
    NOC_DIM_M,      // M1
    NOC_DIM_N,      // N1
    NOC_DIM_G,      // G1
    NOC_DIM_C,      // C1
    NOC_DIM_R,      // R2
} noc_order_dim_t;

typedef uint16_t half_fp16;

// forward declare npu_conv_op_desc
struct npu_conv_op_desc;

#define NUMBER_OF_E21 2
#define PEC_ROW 10
#define PEC_COL 8
#define N_LEVEL_LOOPS 12
#define MAX_MAPPINGS 12
#define MAX_TENSORS 4
#define MAX_DATA_TRANS 10

struct conv_mapping_info {
    uint32_t F3, G3, N3, M3, E4, C3;
    uint32_t G2, N2, C2, E3, R3, M2;
    uint32_t E1, R1, CV;
    uint32_t E0, F0, S, GMF, CMF, MMF;
    uint32_t GF, MF, CF;

    uint32_t G1_X, N1_X, M1_X, E2_X;
    uint32_t G1_Y, N1_Y, M1_Y, E2_Y, R2, C1;
} __attribute__((packed, aligned(ALIGNMENT)));

struct soft_conv_info {
    struct conv_mapping_info mapping_info;
    uint32_t first_level;
    uint32_t g, n, c, ofm_c0;
    uint32_t psum_trunc;
    uint8_t strides[2];
    uint8_t padding[4];
    uint8_t csc_format;
    uint8_t reserved;
    uint32_t ifmap_offset;
    uint32_t ifmap_cube_stride;
    uint32_t ifmap_surface_stride;
    uint32_t real_h;
    uint32_t real_w;
} __attribute__((packed, aligned(ALIGNMENT)));

#define CONV_CONFIG_MAX_SIZE (1536)
struct npu_conv_op_desc {
    char conv_config_data[CONV_CONFIG_MAX_SIZE];
    struct dla_cvt_param in_cvt;  /* input converter parameters */
    struct dla_cvt_param out_cvt; /* output converter parameters, support truncate only */
    struct soft_conv_info soft_conv_config;
    uint8_t src_precision;
    uint8_t dst_precision;
} __attribute__((packed, aligned(ALIGNMENT)));
// #endif

struct npu_conv_surface_desc {
    /* Data cube */
    struct dla_data_cube weight_data;
    struct dla_data_cube wmb_data;
    struct dla_data_cube wgs_data;
    struct dla_data_cube src_data;
    struct dla_data_cube dst_data;
    /*
     * u_addr = input_data.source_addr + offset_u
     * this field should be set when YUV is not interleave format
     * */
    int64_t offset_u;

    /* line stride for 2nd plane, must be 32bytes aligned */
    uint32_t in_line_uv_stride;
} __attribute__((packed, aligned(ALIGNMENT)));
union dla_operation_container {
    struct npu_edma_op_desc edma_op;
    struct npu_conv_op_desc npu_conv_op;  //! add for npu conv desc
    struct dla_sdp_op_desc sdp_op;
    struct dla_pdp_op_desc pdp_op;
    struct dsp_op_desc dsp_op;
    struct dla_rubik_op_desc rubik_op;
    struct dla_event_op_desc event_op;
    struct hae_op_desc hae_op;
    struct gpu_op_desc gpu_op;
    struct cpu_op_desc cpu_op;
};

union dla_surface_container {
    struct npu_edma_surface_desc edma_surface;
    struct npu_conv_surface_desc conv_surface;
    struct dla_sdp_surface_desc sdp_surface;
    struct dla_pdp_surface_desc pdp_surface;
    struct dla_rubik_surface_desc rubik_surface;
    struct dsp_surface_desc dsp_surface;

    struct event_surface_desc event_surface;
    struct hae_surface_desc hae_surface;
    struct gpu_surface_desc gpu_surface;
    struct cpu_surface_desc cpu_surface;
};

#endif
