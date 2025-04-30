#ifndef __COMMON_DEF_HEADER__
#define __COMMON_DEF_HEADER__

#define ENABLE_DEBUG_MSG

#ifdef ENABLE_DEBUG_MSG
#define DPRINTK(args...)		printk(KERN_INFO args);
#else
#define DPRINTK(args...)
#endif

// CSI Interrupt
#define INTERRUPT_THRESHOLD_LEVEL 0  // allow all interrupts to be handled
#define INTERRUPT_ISP_PRIORITY 1

#define SENSOR_OUT_2LANES
#define SENSOR_OUT_10BIT
// #define SENSOR_HDR_DOL2    // L:0, S:1
// #define SENSOR_HDR_DOL3    // L:0, S:1, VS:2
// #define SENSOR_HDR_STAGGER2 //HCG:0 LCG:1 or L:0 S:1
// #define SENSOR_HDR_STAGGER3 //L:0 S:1 VS:2
// #define SENSOR_HDR_HCG_VS  // HCG:0, VS:2
// #define SENSOR_HDR_CDCG_VS // L:0 16bit, VS:2 12bit

#ifdef CONFIG_EIC7700_EVB_VI
#define SENSOR_OUT_H 1280
#define SENSOR_OUT_V 720
#define SENSOR_OUT_H_PAD 0
#else
#define SENSOR_OUT_H 3280
#define SENSOR_OUT_V 2464
#define SENSOR_OUT_H_PAD 0
#endif


#define DVP2AXI_MAX_CHANNEL 6

// i2c master id and controller id used
#define CSI_CONTROLLER_ID0 0
#define CSI_CONTROLLER_ID1 1
#define CSI_CONTROLLER_ID2 2
#define CSI_CONTROLLER_ID3 3
#define CSI_CONTROLLER_ID4 4
#define CSI_CONTROLLER_ID5 5

#define I2C_MASTER_ID0 0
#define I2C_MASTER_ID1 1


#define DVP2AXI_DVP0_ENABLE
#if 0
#define DVP2AXI_DVP1_ENABLE
#define DVP2AXI_DVP2_ENABLE
#define DVP2AXI_DVP3_ENABLE
#define DVP2AXI_DVP4_ENABLE
#define DVP2AXI_DVP5_ENABLE
#endif
#define CSI_CONTROLLER_ID CSI_CONTROLLER_ID0
// #define I2C_MASTER_ID I2C_MASTER_ID0


#endif
