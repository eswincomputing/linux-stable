// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef _ES_NN_COMMONN_H_
#define _ES_NN_COMMONN_H_

#include "es_type.h"
#include "es_dsp_types.h"
#include "es_dsp_op_types.h"

#define MAX_BOXES_NUM 32
#define MAX_DIM_CNT 6

// define normalization modes
typedef enum {
    ES_Z_SCORE = 1,  // Normalize using Z-Score: (x-mean) * (1/std)
    ES_MIN_MAX = 2,  // Normalize using Min-Max scaling: (x – min) * (1/(max-min))
} ES_NORM_MODE_E;

// define output data formats for normalization
// These attributes can be effectively represented by the shape and
// dataType fields within the ES_TENSOR_S structure.
typedef enum {
    ES_RGB8B8 = 1,
    ES_RGB8B8_PLANAR,
    ES_RGB8BI,
    ES_RGB8BI_PLANAR,
    ES_R16G16B16I,
    ES_R16G16B16I_PLANAR,
    ES_R16G16B16F,
    ES_R16G16B16F_PLANAR,
    ES_R32G32B32F,
    ES_R32G32B32F_PLANAR,
    ES_GRAY8,
    ES_GRAY8I,
    ES_GRAY16I,
    ES_GRAY16F,
    ES_GRAY32F
} ES_NORM_OUT_FORMAT_E;

// define data precision types
typedef enum {
    ES_PRECISION_UNKNOWN,
    ES_PRECISION_INT8,
    ES_PRECISION_UINT8,
    ES_PRECISION_INT16,
    ES_PRECISION_UINT16,
    ES_PRECISION_INT32,
    ES_PRECISION_UINT32,
    ES_PRECISION_INT64,
    ES_PRECISION_UINT64,
    ES_PRECISION_FP16,
    ES_PRECISION_FP32,
} ES_DATA_PRECISION_E;

typedef struct {
    ES_DATA_PRECISION_E precision;
    char name[8];
    int size;
} DataTypeInfo;

static const DataTypeInfo DataTypeInfoMap[] = {
    {ES_PRECISION_UNKNOWN, "unknown", 0}, {ES_PRECISION_INT8, "s8", 1},    {ES_PRECISION_UINT8, "u8", 1},
    {ES_PRECISION_INT16, "s16", 2},       {ES_PRECISION_UINT16, "u16", 2}, {ES_PRECISION_INT32, "i32", 4},
    {ES_PRECISION_UINT32, "u32", 4},      {ES_PRECISION_INT64, "i64", 8},  {ES_PRECISION_UINT64, "u64", 8},
    {ES_PRECISION_FP16, "f16", 2},        {ES_PRECISION_FP32, "f32", 4},
};

// define color codes
typedef enum {
    ES_RGB = 0,
    ES_RGBX,         // packed NHWC
    ES_RGBX_PLANAR,  // planar NCHW
    ES_BGRX,         // packed NHWC
    ES_BGRX_PLANAR,  // planar NCHW
    ES_YUV_NV12,
    ES_YUV_NV21,
    ES_YUV420P,
} ES_COLOR_CODE_E;

// define color conversion codes
typedef enum {
    ES_YUV2RGB = 0,
    ES_YUV2BGR,
    ES_YUV2RGB_NV12,
    ES_YUV2BGR_NV12,
    ES_YUV2RGB_NV21,
    ES_YUV2BGR_NV21,
    ES_YUV420P2RGB,
    ES_YUV420P2BGR,
} ES_CVT_CODE_E;

// define interpolation flags
typedef enum {
    ES_INTER_NEAREST = 0,
    ES_INTER_LINEAR = 1,
    ES_INTER_AREA = 2,
    ES_INTER_CUBIC = 3,
    ES_INTER_LANCZOS4 = 4,
    ES_INTER_NEAREST_EXACT = 6,
    ES_INTER_STRETCH = 20,  // for HAE
    ES_INTER_FILTER = 21,   // for HAE
} ES_INTER_FLAG_E;

// define a bounding box with top-left and bottom-right coordinates
typedef struct {
    ES_FLOAT tlx;  // Top-left x-coordinate
    ES_FLOAT tly;  // Top-left y-coordinate
    ES_FLOAT brx;  // Bottom-right x-coordinate
    ES_FLOAT bry;  // Bottom-right y-coordinate
} ES_BOX_S;

// define dimensions
typedef struct {
    ES_S32 height;  // Vertical dimension
    ES_S32 width;   // Horizontal dimension
} ES_SIZE_S;

// define a 2D ratio
typedef struct {
    ES_FLOAT fx;  // Horizontal scaling factor or ratio
    ES_FLOAT fy;  // Vertical scaling factor or ratio
} ES_RATIO_S;

typedef union {
    ES_DEV_BUF_S devBuffer;  // for device
    void* hostBuffer;        // for cpu
} ES_DATA_BUFFER;

/*
 *  the layout is c0 -> w -> h -> c -> n,
 *  each dimension is counted by elements
 *  the constraint for C0 is C0 * bytes_per_elem <= 32 bytes
 */
typedef struct {
    ES_S32 N;   // batch size
    ES_S32 C;   // number of C0
    ES_S32 H;   // height
    ES_S32 W;   // width
    ES_S32 C0;  // the atomic unit of channel
    ES_S32 Cs;  // the real channels Cs <= C*C0 and C = ceil(Cs/C0)
} ES_TENSOR_SHAPE_S;

typedef struct {
    union {
        ES_DEV_BUF_S pData;
        void *hostBuf;
    };
    ES_DATA_PRECISION_E dataType;
    ES_U32 shapeDim;
    ES_U32 shape[MAX_DIM_CNT];
} ES_TENSOR_S;


typedef enum {
    MEM_UNKNOWN,
    MEM_DDR,
    MEM_LLC,
    MEM_SRAM,
    MEM_LINE,
} ES_DSP_MEM_DESC_E;

typedef struct {
    ES_DATA_PRECISION_E precision;
    /* Specify the data is on Online, DDR, LLC or SRAM. */
    ES_DSP_MEM_DESC_E memInfo;
    ES_TENSOR_SHAPE_S shape;
    ES_S32 strideCs, strideC, strideH, strideW, strideN;
} ES_DSP_TENSOR_DESC_S;

// follow params using in mobile v2 net
#define OP_ARR_NUM_MAX 256       /* the max op num of mobilenet v2 */
#define OUT_CHANNEL_NUM_MAX 2048 /* equal to the num of conv kernel weight */

/* Instruct dsp to go through different implementations */
typedef enum {
    ES_NORM_CONV = 1,
    ES_DEPTHWISE_CONV = 2,
    ES_POINTWISE_CONV = 3,
    ES_ADD = 4,
} ES_DSP_OP_TYPE_E;

/* Info of a single op */
typedef struct DSP_OP_INFO_S {
    /* op public info */
    ES_CHAR opName[OPERATOR_NAME_MAXLEN];
    ES_DSP_OP_TYPE_E opType;
    ES_TENSOR_SHAPE_S input_shape;
    ES_TENSOR_SHAPE_S output_shape;

    /* add op params ----> scale info */
    ES_S16 addOpInput0Scale;
    ES_S16 addOpInput1Scale;
    ES_S16 addOpRightShift;

    /* conv op params ----> weight and bias info */
    ES_U32 kernelShape[2];
    ES_U32 pads[4];
    ES_U32 strides[2];
    ES_U32 weightOffset;
    ES_U32 biasOffset;
    /* if is no relu6, it is -128 to 127.
     * If is relu6, it needs to be calc using 6 / output_scale? */
    ES_S32 upper_bound;
    ES_S32 lower_bound;

    /* conv op params ----> scale info */
    ES_S16 convOpWeightScale[OUT_CHANNEL_NUM_MAX];
    ES_S16 convOpRightShift;
} ES_DSP_OP_INFO_S;

/* The header information for weight and bias in ddr */
typedef struct DSP_INFO_DESC_S {
    ES_U32 totalBufSize;
    ES_U32 totalOpNum;
    ES_DSP_OP_INFO_S opInfoArr[OP_ARR_NUM_MAX];
    ES_CHAR paramsData[0]; /* store weight and bias */
} ES_DSP_OP_INFO_DESC_S;

// detection out params
#define MAX_IN_TENSOR_NUM 9
#define MAX_TOTAL_ANCHORS_NUM 27  // max num of anchors in yolo net

typedef enum {
    yolov3 = 1,
    yolov4 = 2,
    yolov5 = 3,
    yolov7 = 4,
    yolov8 = 5,
    yolo5face = 6,
    ppyoloe = 7,
    ssd = 20,
    frcnn = 30,
} ES_DET_NETWORK_E;

typedef enum {
    HARD_NMS,
    SOFT_NMS_GAUSSIAN,
    SOFT_NMS_LINEAR,
} ES_NMS_METHOD_E;

typedef enum {
    IOU,
    GIOU,
    DIOU,
} ES_IOU_METHOD_E;

typedef enum {
    XminYminXmaxYmax,
    XminYminWH,
    YminXminYmaxXmax,
    XmidYmidWH,
} ES_BOX_TYPE_E;

#define MASK 0x00000001

// Macro to set the 'flag' variable based on the status of various operations.
// Each operation (crop, cvt, resize, normal) has a corresponding bit in 'flag'.
// If an operation is enabled (value is 1), its corresponding bit is set.
#define SET_OP_FLAG(flag, crop, cvt, resize, normal)                                                         \
    {                                                                                                        \
        flag = ((crop & MASK) << 0) | ((cvt & MASK) << 1) | ((resize & MASK) << 2) | ((normal & MASK) << 3); \
    }

#endif  // _ES_NN_COMMONN_H_