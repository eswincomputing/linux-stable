// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Sony IMX415 CMOS Image Sensor.
 *
 * Copyright (C) 2023 WolfVision GmbH.
 */

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include <linux/es-camera-module.h>

#include <linux/minmax.h>

static int es_camera_debug = 0;
module_param_named(debug, es_camera_debug, int, 0644);
MODULE_PARM_DESC(debug, "manual config camera parameters, 0: disable, 1: enable");

#define IMX415_PIXEL_ARRAY_TOP	  0
#define IMX415_PIXEL_ARRAY_LEFT	  0
#define IMX415_PIXEL_ARRAY_WIDTH  3864
#define IMX415_PIXEL_ARRAY_HEIGHT 2192
#define IMX415_PIXEL_ARRAY_VBLANK 58
#define IMX415_EXPOSURE_OFFSET	  8

#define IMX415_PIXEL_RATE_74_25MHZ	891000000
#define IMX415_PIXEL_RATE_72MHZ		864000000

#define IMX415_NUM_CLK_PARAM_REGS 11

#define IMX415_REG_8BIT(n)	  ((1 << 16) | (n))
#define IMX415_REG_16BIT(n)	  ((2 << 16) | (n))
#define IMX415_REG_24BIT(n)	  ((3 << 16) | (n))
#define IMX415_REG_SIZE_SHIFT	  16
#define IMX415_REG_ADDR_MASK	  0xffff

#define IMX415_MODE		  IMX415_REG_8BIT(0x3000)
#define IMX415_MODE_OPERATING	  (0)
#define IMX415_MODE_STANDBY	  BIT(0)
#define IMX415_REGHOLD		  IMX415_REG_8BIT(0x3001)
#define IMX415_REGHOLD_INVALID	  (0)
#define IMX415_REGHOLD_VALID	  BIT(0)
#define IMX415_XMSTA		  IMX415_REG_8BIT(0x3002)
#define IMX415_XMSTA_START	  (0)
#define IMX415_XMSTA_STOP	  BIT(0)
#define IMX415_BCWAIT_TIME	  IMX415_REG_16BIT(0x3008)
#define IMX415_CPWAIT_TIME	  IMX415_REG_16BIT(0x300A)
#define IMX415_WINMODE		  IMX415_REG_8BIT(0x301C)
#define IMX415_ADDMODE		  IMX415_REG_8BIT(0x3022)
#define IMX415_REVERSE		  IMX415_REG_8BIT(0x3030)
#define IMX415_HREVERSE_SHIFT	  (0)
#define IMX415_VREVERSE_SHIFT	  BIT(0)
#define IMX415_ADBIT		  IMX415_REG_8BIT(0x3031)
#define IMX415_MDBIT		  IMX415_REG_8BIT(0x3032)
#define IMX415_SYS_MODE		  IMX415_REG_8BIT(0x3033)
#define IMX415_OUTSEL		  IMX415_REG_8BIT(0x30C0)
#define IMX415_DRV		  IMX415_REG_8BIT(0x30C1)
#define IMX415_VMAX		  IMX415_REG_24BIT(0x3024)
#define IMX415_VMAX_MAX		  0xfffff
#define IMX415_HMAX		  IMX415_REG_16BIT(0x3028)
#define IMX415_HMAX_MAX		  0xffff
#define IMX415_HMAX_MULTIPLIER	  12
#define IMX415_SHR0		  IMX415_REG_24BIT(0x3050)
#define IMX415_SHR1       IMX415_REG_24BIT(0x3054)
#define IMX415_SHR2       IMX415_REG_24BIT(0x3058)
#define IMX415_RHS1       IMX415_REG_24BIT(0x3060)
#define IMX415_RHS2       IMX415_REG_24BIT(0x3064)
#define IMX415_GAIN_PCG_0	  IMX415_REG_16BIT(0x3090)
#define IMX415_GAIN_PGC_1       IMX415_REG_16BIT(0x3092)
#define IMX415_GAIN_PGC_2       IMX415_REG_16BIT(0x3094)
#define IMX415_GAIN_PGC_FIDMD   IMX415_REG_8BIT(0x3260)

#define IMX415_AGAIN_MIN	  0
#define IMX415_AGAIN_MAX	  0xF0
#define IMX415_AGAIN_STEP	  1
#define IMX415_BLKLEVEL		  IMX415_REG_16BIT(0x30E2)
#define IMX415_BLKLEVEL_DEFAULT	  50
#define IMX415_TPG_EN_DUOUT	  IMX415_REG_8BIT(0x30E4)
#define IMX415_TPG_PATSEL_DUOUT	  IMX415_REG_8BIT(0x30E6)
#define IMX415_TPG_COLORWIDTH	  IMX415_REG_8BIT(0x30E8)
#define IMX415_TESTCLKEN_MIPI	  IMX415_REG_8BIT(0x3110)
#define IMX415_INCKSEL1		  IMX415_REG_8BIT(0x3115)
#define IMX415_INCKSEL2		  IMX415_REG_8BIT(0x3116)
#define IMX415_INCKSEL3		  IMX415_REG_16BIT(0x3118)
#define IMX415_INCKSEL4		  IMX415_REG_16BIT(0x311A)
#define IMX415_INCKSEL5		  IMX415_REG_8BIT(0x311E)
#define IMX415_DIG_CLP_MODE	  IMX415_REG_8BIT(0x32C8)
#define IMX415_WRJ_OPEN		  IMX415_REG_8BIT(0x3390)
#define IMX415_SENSOR_INFO	  IMX415_REG_16BIT(0x3F12)
#define IMX415_SENSOR_INFO_MASK	  0xFFF
#define IMX415_CHIP_ID		  0x514
#define IMX415_LANEMODE		  IMX415_REG_16BIT(0x4001)
#define IMX415_LANEMODE_2	  1
#define IMX415_LANEMODE_4	  3
#define IMX415_TXCLKESC_FREQ	  IMX415_REG_16BIT(0x4004)
#define IMX415_INCKSEL6		  IMX415_REG_8BIT(0x400C)
#define IMX415_TCLKPOST		  IMX415_REG_16BIT(0x4018)
#define IMX415_TCLKPREPARE	  IMX415_REG_16BIT(0x401A)
#define IMX415_TCLKTRAIL	  IMX415_REG_16BIT(0x401C)
#define IMX415_TCLKZERO		  IMX415_REG_16BIT(0x401E)
#define IMX415_THSPREPARE	  IMX415_REG_16BIT(0x4020)
#define IMX415_THSZERO		  IMX415_REG_16BIT(0x4022)
#define IMX415_THSTRAIL		  IMX415_REG_16BIT(0x4024)
#define IMX415_THSEXIT		  IMX415_REG_16BIT(0x4026)
#define IMX415_TLPX		  IMX415_REG_16BIT(0x4028)
#define IMX415_INCKSEL7		  IMX415_REG_8BIT(0x4074)

#define OF_CAMERA_HDR_MODE		"eswin,camera-hdr-mode"

struct imx415_reg {
	u32 address;
	u32 val;
};

static const char *const imx415_supply_names[] = {
	"dvdd",
	"ovdd",
	"avdd",
};

/*
 * The IMX415 data sheet uses lane rates but v4l2 uses link frequency to
 * describe MIPI CSI-2 speed. This driver uses lane rates wherever possible
 * and converts them to link frequencies by a factor of two when needed.
 */
static const s64 link_freq_menu_items[] = {
	594000000 / 2,	720000000 / 2,	891000000 / 2,
	1440000000 / 2, 1485000000 / 2, 1782000000 / 2,
	2079000000 / 2, 2376000000 / 2,
};

struct imx415_clk_params {
	u64 lane_rate;
	u64 inck;
	struct imx415_reg regs[IMX415_NUM_CLK_PARAM_REGS];
};

static const struct imx415_reg imx415_hdr2_10bit_3864x2192_1485M_regs[] = {
    { IMX415_REG_8BIT(0x3020), 0x00 },
    { IMX415_REG_8BIT(0x3021), 0x00 },
    { IMX415_REG_8BIT(0x3022), 0x00 },
    { IMX415_REG_8BIT(0x3024), 0xFC },
    { IMX415_REG_8BIT(0x3025), 0x08 },
    { IMX415_REG_8BIT(0x3028), 0x1A },
    { IMX415_REG_8BIT(0x3029), 0x02 },
    { IMX415_REG_8BIT(0x302C), 0x01 },
    { IMX415_REG_8BIT(0x302D), 0x01 },
    { IMX415_REG_8BIT(0x3033), 0x08 },
    { IMX415_REG_8BIT(0x3050), 0xA8 },
    { IMX415_REG_8BIT(0x3051), 0x0D },
    { IMX415_REG_8BIT(0x3054), 0x09 },
    { IMX415_REG_8BIT(0x3058), 0x3E },
    { IMX415_REG_8BIT(0x3060), 0x4D },
    { IMX415_REG_8BIT(0x3064), 0x4A },
    { IMX415_REG_8BIT(0x30CF), 0x01 },
    { IMX415_REG_8BIT(0x3118), 0xA0 },
    { IMX415_REG_8BIT(0x3260), 0x00 },
    { IMX415_REG_8BIT(0x400C), 0x01 },
    { IMX415_REG_8BIT(0x4018), 0xA7 },
    { IMX415_REG_8BIT(0x401A), 0x57 },
    { IMX415_REG_8BIT(0x401C), 0x5F },
    { IMX415_REG_8BIT(0x401E), 0x97 },
    { IMX415_REG_8BIT(0x401F), 0x01 },
    { IMX415_REG_8BIT(0x4020), 0x5F },
    { IMX415_REG_8BIT(0x4022), 0xAF },
    { IMX415_REG_8BIT(0x4024), 0x5F },
    { IMX415_REG_8BIT(0x4026), 0x9F },
    { IMX415_REG_8BIT(0x4028), 0x4F },
    { IMX415_REG_8BIT(0x4074), 0x00 },
};

static const struct imx415_reg imx415_hdr3_10bit_3864x2192_1485M_regs[] = {
	{ IMX415_REG_8BIT(0x3020), 0x00 },
    { IMX415_REG_8BIT(0x3021), 0x00 },
    { IMX415_REG_8BIT(0x3022), 0x00 },
    { IMX415_REG_8BIT(0x3024), 0xBD },
    { IMX415_REG_8BIT(0x3025), 0x06 },
    { IMX415_REG_8BIT(0x3028), 0x1A },
    { IMX415_REG_8BIT(0x3029), 0x02 },
    { IMX415_REG_8BIT(0x302C), 0x01 },
    { IMX415_REG_8BIT(0x302D), 0x02 },
    { IMX415_REG_8BIT(0x3033), 0x08 },
    { IMX415_REG_8BIT(0x3050), 0x90 },
    { IMX415_REG_8BIT(0x3051), 0x15 },
    { IMX415_REG_8BIT(0x3054), 0x0D },
    { IMX415_REG_8BIT(0x3058), 0xA4 },
    { IMX415_REG_8BIT(0x3060), 0x97 },
    { IMX415_REG_8BIT(0x3064), 0xB6 },
    { IMX415_REG_8BIT(0x30CF), 0x03 },
    { IMX415_REG_8BIT(0x3118), 0xA0 },
    { IMX415_REG_8BIT(0x3260), 0x00 },
    { IMX415_REG_8BIT(0x400C), 0x01 },
    { IMX415_REG_8BIT(0x4018), 0xA7 },
    { IMX415_REG_8BIT(0x401A), 0x57 },
    { IMX415_REG_8BIT(0x401C), 0x5F },
    { IMX415_REG_8BIT(0x401E), 0x97 },
    { IMX415_REG_8BIT(0x401F), 0x01 },
    { IMX415_REG_8BIT(0x4020), 0x5F },
    { IMX415_REG_8BIT(0x4022), 0xAF },
    { IMX415_REG_8BIT(0x4024), 0x5F },
    { IMX415_REG_8BIT(0x4026), 0x9F },
    { IMX415_REG_8BIT(0x4028), 0x4F },
    { IMX415_REG_8BIT(0x4074), 0x00 },
};



/* INCK Settings - includes all lane rate and INCK dependent registers */
static const struct imx415_clk_params imx415_clk_params[] = {
	{
		.lane_rate = 594000000UL,
		.inck = 27000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x05D },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x042 },
		.regs[2] = { IMX415_SYS_MODE, 0x7 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x084 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E7 },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x06C0 },
	},
	{
		.lane_rate = 594000000UL,
		.inck = 37125000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x07F },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x05B },
		.regs[2] = { IMX415_SYS_MODE, 0x7 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x24 },
		.regs[5] = { IMX415_INCKSEL3, 0x080 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x24 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0984 },
	},
	{
		.lane_rate = 594000000UL,
		.inck = 74250000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x0FF },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x0B6 },
		.regs[2] = { IMX415_SYS_MODE, 0x7 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x28 },
		.regs[5] = { IMX415_INCKSEL3, 0x080 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x28 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x1290 },
	},
	{
		.lane_rate = 720000000UL,
		.inck = 24000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x054 },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x03B },
		.regs[2] = { IMX415_SYS_MODE, 0x9 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0B4 },
		.regs[6] = { IMX415_INCKSEL4, 0x0FC },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0600 },
	},
	{
		.lane_rate = 720000000UL,
		.inck = 72000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x0F8 },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x0B0 },
		.regs[2] = { IMX415_SYS_MODE, 0x9 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x28 },
		.regs[5] = { IMX415_INCKSEL3, 0x0A0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x28 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x1200 },
	},
	{
		.lane_rate = 891000000UL,
		.inck = 27000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x05D },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x042 },
		.regs[2] = { IMX415_SYS_MODE, 0x5 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0C6 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E7 },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x06C0 },
	},
	{
		.lane_rate = 891000000UL,
		.inck = 37125000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x07F },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x05B },
		.regs[2] = { IMX415_SYS_MODE, 0x5 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x24 },
		.regs[5] = { IMX415_INCKSEL3, 0x0C0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x24 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0948 },
	},
	{
		.lane_rate = 891000000UL,
		.inck = 74250000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x0FF },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x0B6 },
		.regs[2] = { IMX415_SYS_MODE, 0x5 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x28 },
		.regs[5] = { IMX415_INCKSEL3, 0x0C0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x28 },
		.regs[8] = { IMX415_INCKSEL6, 0x0 },
		.regs[9] = { IMX415_INCKSEL7, 0x1 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x1290 },
	},
	{
		.lane_rate = 1440000000UL,
		.inck = 24000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x054 },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x03B },
		.regs[2] = { IMX415_SYS_MODE, 0x8 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0B4 },
		.regs[6] = { IMX415_INCKSEL4, 0x0FC },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0600 },
	},
	{
		.lane_rate = 1440000000UL,
		.inck = 72000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x0F8 },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x0B0 },
		.regs[2] = { IMX415_SYS_MODE, 0x8 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x28 },
		.regs[5] = { IMX415_INCKSEL3, 0x0A0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x28 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x1200 },
	},
	{
		.lane_rate = 1485000000UL,
		.inck = 27000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x05D },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x042 },
		.regs[2] = { IMX415_SYS_MODE, 0x8 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0A5 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E7 },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x06C0 },
	},
	{
		.lane_rate = 1485000000UL,
		.inck = 37125000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x07F },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x05B },
		.regs[2] = { IMX415_SYS_MODE, 0x8 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x24 },
		.regs[5] = { IMX415_INCKSEL3, 0x0A0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x24 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0948 },
	},
	{
		.lane_rate = 1485000000UL,
		.inck = 74250000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x0FF },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x0B6 },
		.regs[2] = { IMX415_SYS_MODE, 0x8 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x28 },
		.regs[5] = { IMX415_INCKSEL3, 0x0A0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x28 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x1290 },
	},
	{
		.lane_rate = 1782000000UL,
		.inck = 27000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x05D },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x042 },
		.regs[2] = { IMX415_SYS_MODE, 0x4 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0C6 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E7 },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x06C0 },
	},
	{
		.lane_rate = 1782000000UL,
		.inck = 37125000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x07F },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x05B },
		.regs[2] = { IMX415_SYS_MODE, 0x4 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x24 },
		.regs[5] = { IMX415_INCKSEL3, 0x0C0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x24 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0948 },
	},
	{
		.lane_rate = 1782000000UL,
		.inck = 74250000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x0FF },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x0B6 },
		.regs[2] = { IMX415_SYS_MODE, 0x4 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x28 },
		.regs[5] = { IMX415_INCKSEL3, 0x0C0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x28 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x1290 },
	},
	{
		.lane_rate = 2079000000UL,
		.inck = 27000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x05D },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x042 },
		.regs[2] = { IMX415_SYS_MODE, 0x2 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x0E7 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E7 },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x06C0 },
	},
	{
		.lane_rate = 2079000000UL,
		.inck = 37125000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x07F },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x05B },
		.regs[2] = { IMX415_SYS_MODE, 0x2 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x24 },
		.regs[5] = { IMX415_INCKSEL3, 0x0E0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x24 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0948 },
	},
	{
		.lane_rate = 2079000000UL,
		.inck = 74250000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x0FF },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x0B6 },
		.regs[2] = { IMX415_SYS_MODE, 0x2 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x28 },
		.regs[5] = { IMX415_INCKSEL3, 0x0E0 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x28 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x1290 },
	},
	{
		.lane_rate = 2376000000UL,
		.inck = 27000000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x05D },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x042 },
		.regs[2] = { IMX415_SYS_MODE, 0x0 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x23 },
		.regs[5] = { IMX415_INCKSEL3, 0x108 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E7 },
		.regs[7] = { IMX415_INCKSEL5, 0x23 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x06C0 },
	},
	{
		.lane_rate = 2376000000UL,
		.inck = 37125000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x07F },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x05B },
		.regs[2] = { IMX415_SYS_MODE, 0x0 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x24 },
		.regs[5] = { IMX415_INCKSEL3, 0x100 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x24 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x0948 },
	},
	{
		.lane_rate = 2376000000UL,
		.inck = 74250000,
		.regs[0] = { IMX415_BCWAIT_TIME, 0x0FF },
		.regs[1] = { IMX415_CPWAIT_TIME, 0x0B6 },
		.regs[2] = { IMX415_SYS_MODE, 0x0 },
		.regs[3] = { IMX415_INCKSEL1, 0x00 },
		.regs[4] = { IMX415_INCKSEL2, 0x28 },
		.regs[5] = { IMX415_INCKSEL3, 0x100 },
		.regs[6] = { IMX415_INCKSEL4, 0x0E0 },
		.regs[7] = { IMX415_INCKSEL5, 0x28 },
		.regs[8] = { IMX415_INCKSEL6, 0x1 },
		.regs[9] = { IMX415_INCKSEL7, 0x0 },
		.regs[10] = { IMX415_TXCLKESC_FREQ, 0x1290 },
	},
};

/* 720 Mbps CSI configuration */
static const struct imx415_reg imx415_linkrate_720mbps[] = {
	{ IMX415_TCLKPOST, 0x006F },
	{ IMX415_TCLKPREPARE, 0x002F },
	{ IMX415_TCLKTRAIL, 0x002F },
	{ IMX415_TCLKZERO, 0x00BF },
	{ IMX415_THSPREPARE, 0x002F },
	{ IMX415_THSZERO, 0x0057 },
	{ IMX415_THSTRAIL, 0x002F },
	{ IMX415_THSEXIT, 0x004F },
	{ IMX415_TLPX, 0x0027 },
};

/* 1440 Mbps CSI configuration */
static const struct imx415_reg imx415_linkrate_1440mbps[] = {
	{ IMX415_TCLKPOST, 0x009F },
	{ IMX415_TCLKPREPARE, 0x0057 },
	{ IMX415_TCLKTRAIL, 0x0057 },
	{ IMX415_TCLKZERO, 0x0187 },
	{ IMX415_THSPREPARE, 0x005F },
	{ IMX415_THSZERO, 0x00A7 },
	{ IMX415_THSTRAIL, 0x005F },
	{ IMX415_THSEXIT, 0x0097 },
	{ IMX415_TLPX, 0x004F },
};

/* 891 Mbps */
static const struct imx415_reg imx415_linkrate_891mbps[] = {
	{ IMX415_TCLKPOST, 0x007F },
	{ IMX415_TCLKPREPARE, 0x0037 },
	{ IMX415_TCLKTRAIL, 0x0037 },
	{ IMX415_TCLKZERO, 0x00F7 },
	{ IMX415_THSPREPARE, 0x003F },
	{ IMX415_THSZERO, 0x006F },
	{ IMX415_THSTRAIL, 0x003F },
	{ IMX415_THSEXIT, 0x005F },
	{ IMX415_TLPX, 0x002F },
};

/* 2376 Mbps CSI configuration */
static const struct imx415_reg imx415_linkrate_2376mbps[] = {
	{ IMX415_TCLKPOST, 0x00E7 },
	{ IMX415_TCLKPREPARE, 0x008F },
	{ IMX415_TCLKTRAIL, 0x008F },
	{ IMX415_TCLKZERO, 0x027F },
	{ IMX415_THSPREPARE, 0x0097 },
	{ IMX415_THSZERO, 0x010F },
	{ IMX415_THSTRAIL, 0x0097 },
	{ IMX415_THSEXIT, 0x00F7 },
	{ IMX415_TLPX, 0x007F },
};

/* 1782 Mbps CSI configuration */
static const struct imx415_reg imx415_linkrate_1782mbps[] = {
	{ IMX415_TCLKPOST, 0x00B7 },
	{ IMX415_TCLKPREPARE, 0x006F },
	{ IMX415_TCLKTRAIL, 0x006F },
	{ IMX415_TCLKZERO, 0x01DF },
	{ IMX415_THSPREPARE, 0x006F },
	{ IMX415_THSZERO, 0x00CF },
	{ IMX415_THSTRAIL, 0x006F },
	{ IMX415_THSEXIT, 0x00B7 },
	{ IMX415_TLPX, 0x005F },
};

/* 2079 Mbps CSI configuration */
static const struct imx415_reg imx415_linkrate_2079mbps[] = {
	{ IMX415_TCLKPOST, 0x00D7 },
	{ IMX415_TCLKPREPARE, 0x007F },
	{ IMX415_TCLKTRAIL, 0x007F },
	{ IMX415_TCLKZERO, 0x0237 },
	{ IMX415_THSPREPARE, 0x0087 },
	{ IMX415_THSZERO, 0x00EF },
	{ IMX415_THSTRAIL, 0x0087 },
	{ IMX415_THSEXIT, 0x00DF },
	{ IMX415_TLPX, 0x006F },
};

/* 1485 Mbps CSI configuration */
static const struct imx415_reg imx415_linkrate_1485mbps[] = {
	{ IMX415_TCLKPOST, 0x00A7 },
	{ IMX415_TCLKPREPARE, 0x0057 },
	{ IMX415_TCLKTRAIL, 0x005F },
	{ IMX415_TCLKZERO, 0x0197 },
	{ IMX415_THSPREPARE, 0x005F },
	{ IMX415_THSZERO, 0x00AF },
	{ IMX415_THSTRAIL, 0x005F },
	{ IMX415_THSEXIT, 0x009F },
	{ IMX415_TLPX, 0x004F },
};

struct imx415_mode_reg_list {
	u32 num_of_regs;
	const struct imx415_reg *regs;
	u32 hdr_mode;
};

struct imx415_mode {
	u64 lane_rate;
	u32 hmax_min[2];
	struct imx415_mode_reg_list reg_list;
};

/* mode configs */
static const struct imx415_mode supported_modes[] = {
	{
		.lane_rate = 720000000,
		.hmax_min = { 2032, 1066 },
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_linkrate_720mbps),
			.regs = imx415_linkrate_720mbps,
			.hdr_mode = NO_HDR,
		},
	},
	{
		.lane_rate = 1485000000,
		.hmax_min = { 1100, 550 },
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_hdr2_10bit_3864x2192_1485M_regs),
			.regs = imx415_hdr2_10bit_3864x2192_1485M_regs,
			.hdr_mode = HDR_X2,
		},
	},
	{
		.lane_rate = 1485000000,
		.hmax_min = { 1100, 550 },
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_hdr3_10bit_3864x2192_1485M_regs),
			.regs = imx415_hdr3_10bit_3864x2192_1485M_regs,
			.hdr_mode = HDR_X3,
		},
	},
	{
		.lane_rate = 1440000000,
		.hmax_min = { 1066, 533 },
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_linkrate_1440mbps),
			.regs = imx415_linkrate_1440mbps,
			.hdr_mode = NO_HDR,
		},
	},
	{
		.lane_rate = 891000000,
		.hmax_min = { 1100, 550 },
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_linkrate_891mbps),
			.regs = imx415_linkrate_891mbps,
			.hdr_mode = NO_HDR,
		},
	},
	{
		.lane_rate = 1485000000,
		.hmax_min = { 1100, 550 },
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_linkrate_1485mbps),
			.regs = imx415_linkrate_1485mbps,
			.hdr_mode = NO_HDR,
		},
	},
	{
		.lane_rate = 1782000000,
		.hmax_min = { 1100, 550 },
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_linkrate_1782mbps),
			.regs = imx415_linkrate_1782mbps,
			.hdr_mode = NO_HDR,
		},
	},
	{
		.lane_rate = 2079000000,
		.hmax_min = { 1100, 550 },
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_linkrate_2079mbps),
			.regs = imx415_linkrate_2079mbps,
			.hdr_mode = NO_HDR,
		},
	},
	{
		.lane_rate = 2376000000,
		.hmax_min = { 366, 366 },
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(imx415_linkrate_2376mbps),
			.regs = imx415_linkrate_2376mbps,
			.hdr_mode = NO_HDR,
		},
	},
};

static const struct regmap_config imx415_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
};

static const char *const imx415_test_pattern_menu[] = {
	"disabled",
	"solid black",
	"solid white",
	"solid dark gray",
	"solid light gray",
	"stripes light/dark grey",
	"stripes dark/light grey",
	"stripes black/dark grey",
	"stripes dark grey/black",
	"stripes black/white",
	"stripes white/black",
	"horizontal color bar",
	"vertical color bar",
};

struct imx415 {
	struct device *dev;
	struct clk *clk;
	unsigned long pixel_rate;
	struct regulator_bulk_data supplies[ARRAY_SIZE(imx415_supply_names)];
	struct gpio_desc *reset;
	struct regmap *regmap;

	const struct imx415_clk_params *clk_params;

	bool streaming;

	struct v4l2_subdev subdev;
	struct media_pad pad;

	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *exposure;

	unsigned int cur_mode;
	unsigned int num_data_lanes;

	u32			cfg_num;
	struct v4l2_ctrl *hdr_mode_ctrl;
	u32 hdr_mode;

	u32 rhs1;
	u32 rhs2;
	u32 cur_vmax;
	u32 fsc_multiplier;

	/* Use our own mutex to protect ctrl handler / subdev state on kernels
	 * that don't embed a mutex inside v4l2_subdev. This prevents NULL
	 * derefs for sensor->subdev.lock on older kernels.
	 */
	struct mutex lock;
};

/*
 * This table includes fixed register settings and a bunch of undocumented
 * registers that have to be set to another value than default.
 */
static const struct imx415_reg imx415_init_table[] = {
	/* use all-pixel readout mode, no flip */
	{ IMX415_WINMODE, 0x00 },
	{ IMX415_ADDMODE, 0x00 },
	{ IMX415_REVERSE, 0x00 },
	/* use RAW 10-bit mode */
	{ IMX415_ADBIT, 0x00 },
	{ IMX415_MDBIT, 0x00 },
	/* output VSYNC on XVS and low on XHS */
	{ IMX415_OUTSEL, 0x22 },
	{ IMX415_DRV, 0x00 },

	/* SONY magic registers */
	{ IMX415_REG_8BIT(0x32D4), 0x21 },
	{ IMX415_REG_8BIT(0x32EC), 0xA1 },
	{ IMX415_REG_8BIT(0x3452), 0x7F },
	{ IMX415_REG_8BIT(0x3453), 0x03 },
	{ IMX415_REG_8BIT(0x358A), 0x04 },
	{ IMX415_REG_8BIT(0x35A1), 0x02 },
	{ IMX415_REG_8BIT(0x36BC), 0x0C },
	{ IMX415_REG_8BIT(0x36CC), 0x53 },
	{ IMX415_REG_8BIT(0x36CD), 0x00 },
	{ IMX415_REG_8BIT(0x36CE), 0x3C },
	{ IMX415_REG_8BIT(0x36D0), 0x8C },
	{ IMX415_REG_8BIT(0x36D1), 0x00 },
	{ IMX415_REG_8BIT(0x36D2), 0x71 },
	{ IMX415_REG_8BIT(0x36D4), 0x3C },
	{ IMX415_REG_8BIT(0x36D6), 0x53 },
	{ IMX415_REG_8BIT(0x36D7), 0x00 },
	{ IMX415_REG_8BIT(0x36D8), 0x71 },
	{ IMX415_REG_8BIT(0x36DA), 0x8C },
	{ IMX415_REG_8BIT(0x36DB), 0x00 },
	{ IMX415_REG_8BIT(0x3724), 0x02 },
	{ IMX415_REG_8BIT(0x3726), 0x02 },
	{ IMX415_REG_8BIT(0x3732), 0x02 },
	{ IMX415_REG_8BIT(0x3734), 0x03 },
	{ IMX415_REG_8BIT(0x3736), 0x03 },
	{ IMX415_REG_8BIT(0x3742), 0x03 },
	{ IMX415_REG_8BIT(0x3862), 0xE0 },
	{ IMX415_REG_8BIT(0x38CC), 0x30 },
	{ IMX415_REG_8BIT(0x38CD), 0x2F },
	{ IMX415_REG_8BIT(0x395C), 0x0C },
	{ IMX415_REG_8BIT(0x3A42), 0xD1 },
	{ IMX415_REG_8BIT(0x3A4C), 0x77 },
	{ IMX415_REG_8BIT(0x3AE0), 0x02 },
	{ IMX415_REG_8BIT(0x3AEC), 0x0C },
	{ IMX415_REG_8BIT(0x3B00), 0x2E },
	{ IMX415_REG_8BIT(0x3B06), 0x29 },
	{ IMX415_REG_8BIT(0x3B98), 0x25 },
	{ IMX415_REG_8BIT(0x3B99), 0x21 },
	{ IMX415_REG_8BIT(0x3B9B), 0x13 },
	{ IMX415_REG_8BIT(0x3B9C), 0x13 },
	{ IMX415_REG_8BIT(0x3B9D), 0x13 },
	{ IMX415_REG_8BIT(0x3B9E), 0x13 },
	{ IMX415_REG_8BIT(0x3BA1), 0x00 },
	{ IMX415_REG_8BIT(0x3BA2), 0x06 },
	{ IMX415_REG_8BIT(0x3BA3), 0x0B },
	{ IMX415_REG_8BIT(0x3BA4), 0x10 },
	{ IMX415_REG_8BIT(0x3BA5), 0x14 },
	{ IMX415_REG_8BIT(0x3BA6), 0x18 },
	{ IMX415_REG_8BIT(0x3BA7), 0x1A },
	{ IMX415_REG_8BIT(0x3BA8), 0x1A },
	{ IMX415_REG_8BIT(0x3BA9), 0x1A },
	{ IMX415_REG_8BIT(0x3BAC), 0xED },
	{ IMX415_REG_8BIT(0x3BAD), 0x01 },
	{ IMX415_REG_8BIT(0x3BAE), 0xF6 },
	{ IMX415_REG_8BIT(0x3BAF), 0x02 },
	{ IMX415_REG_8BIT(0x3BB0), 0xA2 },
	{ IMX415_REG_8BIT(0x3BB1), 0x03 },
	{ IMX415_REG_8BIT(0x3BB2), 0xE0 },
	{ IMX415_REG_8BIT(0x3BB3), 0x03 },
	{ IMX415_REG_8BIT(0x3BB4), 0xE0 },
	{ IMX415_REG_8BIT(0x3BB5), 0x03 },
	{ IMX415_REG_8BIT(0x3BB6), 0xE0 },
	{ IMX415_REG_8BIT(0x3BB7), 0x03 },
	{ IMX415_REG_8BIT(0x3BB8), 0xE0 },
	{ IMX415_REG_8BIT(0x3BBA), 0xE0 },
	{ IMX415_REG_8BIT(0x3BBC), 0xDA },
	{ IMX415_REG_8BIT(0x3BBE), 0x88 },
	{ IMX415_REG_8BIT(0x3BC0), 0x44 },
	{ IMX415_REG_8BIT(0x3BC2), 0x7B },
	{ IMX415_REG_8BIT(0x3BC4), 0xA2 },
	{ IMX415_REG_8BIT(0x3BC8), 0xBD },
	{ IMX415_REG_8BIT(0x3BCA), 0xBD },
};

static inline struct imx415 *to_imx415(struct v4l2_subdev *sd)
{
	return container_of(sd, struct imx415, subdev);
}

static int imx415_read(struct imx415 *sensor, u32 addr)
{
	u8 data[3] = { 0 };
	int ret;

	ret = regmap_raw_read(sensor->regmap, addr & IMX415_REG_ADDR_MASK, data,
			      (addr >> IMX415_REG_SIZE_SHIFT) & 3);
	if (ret < 0)
		return ret;

	return (data[2] << 16) | (data[1] << 8) | data[0];
}

static int imx415_write(struct imx415 *sensor, u32 addr, u32 value)
{
	u8 data[3] = { value & 0xff, (value >> 8) & 0xff, value >> 16 };
	int ret;

	ret = regmap_raw_write(sensor->regmap, addr & IMX415_REG_ADDR_MASK,
			       data, (addr >> IMX415_REG_SIZE_SHIFT) & 3);
	if (ret < 0)
		dev_err_ratelimited(sensor->dev,
				    "%u-bit write to 0x%04x failed: %d\n",
				    ((addr >> IMX415_REG_SIZE_SHIFT) & 3) * 8,
				    addr & IMX415_REG_ADDR_MASK, ret);

	return 0;
}

static int imx415_set_testpattern(struct imx415 *sensor, int val)
{
	int ret;

	if (val) {
		ret = imx415_write(sensor, IMX415_BLKLEVEL, 0x00);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TPG_EN_DUOUT, 0x01);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TPG_PATSEL_DUOUT, val - 1);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TPG_COLORWIDTH, 0x01);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TESTCLKEN_MIPI, 0x20);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_DIG_CLP_MODE, 0x00);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_WRJ_OPEN, 0x00);
	} else {
		ret = imx415_write(sensor, IMX415_BLKLEVEL,
				   IMX415_BLKLEVEL_DEFAULT);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TPG_EN_DUOUT, 0x00);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_TESTCLKEN_MIPI, 0x00);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_DIG_CLP_MODE, 0x01);
		if (ret)
			return ret;
		ret = imx415_write(sensor, IMX415_WRJ_OPEN, 0x01);
	}
	return 0;
}

static int imx415_set_mode(struct imx415 *sensor, int mode);

static int imx415_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct imx415 *sensor = container_of(ctrl->handler, struct imx415,
					     ctrls);
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	u32 exposure_max;
	unsigned int vmax;
	unsigned int flip;
	int ret;

	state = v4l2_subdev_get_locked_active_state(&sensor->subdev);
	format = v4l2_subdev_get_pad_format(&sensor->subdev, state, 0);

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = format->height + ctrl->val -
			       IMX415_EXPOSURE_OFFSET;
		__v4l2_ctrl_modify_range(sensor->exposure,
					 sensor->exposure->minimum,
					 exposure_max, sensor->exposure->step,
					 sensor->exposure->default_value);
	}

	if(es_camera_debug == 0) {
		if (!sensor->streaming)
			return 0;
	}

	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		ret = imx415_write(sensor, IMX415_VMAX,
				   format->height + ctrl->val);
		if (ret)
			return ret;
		/*
		 * Deliberately fall through as exposure is set based on VMAX
		 * which has just changed.
		 */
		fallthrough;
	case V4L2_CID_EXPOSURE:
		/* clamp the exposure value to VMAX */
		vmax = format->height + sensor->vblank->cur.val;
		ctrl->val = min_t(int, ctrl->val, vmax);
		return imx415_write(sensor, IMX415_SHR0, vmax - ctrl->val);

	case V4L2_CID_ANALOGUE_GAIN:
		return imx415_write(sensor, IMX415_GAIN_PCG_0, ctrl->val);
	case V4L2_CID_NOTIFY_GAINS:return 0;
	case V4L2_CID_DIGITAL_GAIN:return 0;

	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		flip = (sensor->hflip->val << IMX415_HREVERSE_SHIFT) |
		       (sensor->vflip->val << IMX415_VREVERSE_SHIFT);
		return imx415_write(sensor, IMX415_REVERSE, flip);

	case V4L2_CID_TEST_PATTERN:
		return imx415_set_testpattern(sensor, ctrl->val);

	case V4L2_CID_HBLANK:
		return imx415_write(sensor, IMX415_HMAX,
				    (format->width + ctrl->val) /
						IMX415_HMAX_MULTIPLIER);

	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops imx415_ctrl_ops = {
	.s_ctrl = imx415_s_ctrl,
};

static int imx415_ctrls_init(struct imx415 *sensor)
{
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *ctrl;
	u64 lane_rate = supported_modes[sensor->cur_mode].lane_rate;
	u32 exposure_max = IMX415_PIXEL_ARRAY_HEIGHT +
			   IMX415_PIXEL_ARRAY_VBLANK -
			   IMX415_EXPOSURE_OFFSET;
	u32 hblank_min, hblank_max;
	unsigned int i;
	int ret;

	ret = v4l2_fwnode_device_parse(sensor->dev, &props);
	if (ret < 0)
		return ret;

	v4l2_ctrl_handler_init(&sensor->ctrls, 10);

	for (i = 0; i < ARRAY_SIZE(link_freq_menu_items); ++i) {
		if (lane_rate == link_freq_menu_items[i] * 2)
			break;
	}
	if (i == ARRAY_SIZE(link_freq_menu_items)) {
		return dev_err_probe(sensor->dev, -EINVAL,
				     "lane rate %llu not supported\n",
				     lane_rate);
	}


	ctrl = v4l2_ctrl_new_int_menu(&sensor->ctrls, &imx415_ctrl_ops,
				      V4L2_CID_LINK_FREQ,
				      ARRAY_SIZE(link_freq_menu_items) - 1, i,
				      link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	sensor->exposure = v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
					     V4L2_CID_EXPOSURE, 4,
					     exposure_max, 1, exposure_max);

	v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
			  V4L2_CID_ANALOGUE_GAIN, IMX415_AGAIN_MIN,
			  IMX415_AGAIN_MAX, IMX415_AGAIN_STEP,
			  IMX415_AGAIN_MIN);

	v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
			  V4L2_CID_NOTIFY_GAINS, IMX415_AGAIN_MIN,
			  IMX415_AGAIN_MAX, IMX415_AGAIN_STEP,
			  IMX415_AGAIN_MIN);

	v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
		      V4L2_CID_DIGITAL_GAIN, IMX415_AGAIN_MIN,
		      IMX415_AGAIN_MAX, IMX415_AGAIN_STEP,
		      IMX415_AGAIN_MIN);

	hblank_min = (supported_modes[sensor->cur_mode].hmax_min[sensor->num_data_lanes == 2 ? 0 : 1] *
		      IMX415_HMAX_MULTIPLIER) - IMX415_PIXEL_ARRAY_WIDTH;
	hblank_max = (IMX415_HMAX_MAX * IMX415_HMAX_MULTIPLIER) -
		     IMX415_PIXEL_ARRAY_WIDTH;
	ctrl = v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
				 V4L2_CID_HBLANK, hblank_min,
				 hblank_max, IMX415_HMAX_MULTIPLIER,
				 hblank_min);

	sensor->vblank = v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
					   V4L2_CID_VBLANK,
					   IMX415_PIXEL_ARRAY_VBLANK,
					   IMX415_VMAX_MAX - IMX415_PIXEL_ARRAY_HEIGHT,
					   1, IMX415_PIXEL_ARRAY_VBLANK);

	v4l2_ctrl_new_std(&sensor->ctrls, NULL, V4L2_CID_PIXEL_RATE,
			  sensor->pixel_rate, sensor->pixel_rate, 1,
			  sensor->pixel_rate);

	sensor->hflip = v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	sensor->vflip = v4l2_ctrl_new_std(&sensor->ctrls, &imx415_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std_menu_items(&sensor->ctrls, &imx415_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(imx415_test_pattern_menu) - 1,
				     0, 0, imx415_test_pattern_menu);

	v4l2_ctrl_new_fwnode_properties(&sensor->ctrls, &imx415_ctrl_ops,
					&props);

	/* check for errors from v4l2 ctrl init */
	if (sensor->ctrls.error) {
		dev_err_probe(sensor->dev, sensor->ctrls.error,
			      "failed to add controls\n");
		v4l2_ctrl_handler_free(&sensor->ctrls);
		return sensor->ctrls.error;
	}

	/* let subdev point to this ctrl handler */
	sensor->subdev.ctrl_handler = &sensor->ctrls;

	return 0;
}

static int imx415_set_mode(struct imx415 *sensor, int mode)
{
	const struct imx415_reg *reg;
	unsigned int i;
	int ret = 0;
	u32 vmax_val;

	if (mode >= ARRAY_SIZE(supported_modes)) {
		dev_err(sensor->dev, "Mode %d not supported\n", mode);
		return -EINVAL;
	}

	for (i = 0; i < supported_modes[mode].reg_list.num_of_regs; ++i) {
		reg = &supported_modes[mode].reg_list.regs[i];
		ret = imx415_write(sensor, reg->address, reg->val);
		if (ret)
			return ret;
	}

	switch (sensor->hdr_mode) {
	case HDR_X2:
		dev_dbg(sensor->dev, "Applying HDR X2 specific configuration\n");
		for (i = 0; i < ARRAY_SIZE(imx415_hdr2_10bit_3864x2192_1485M_regs); ++i) {
			reg = &imx415_hdr2_10bit_3864x2192_1485M_regs[i];
			ret = imx415_write(sensor, reg->address, reg->val);
			if (ret) return ret;
		}
		break;
	case HDR_X3:
		dev_dbg(sensor->dev, "Applying HDR X3 specific configuration\n");
		for (i = 0; i < ARRAY_SIZE(imx415_hdr3_10bit_3864x2192_1485M_regs); ++i) {
			reg = &imx415_hdr3_10bit_3864x2192_1485M_regs[i];
			ret = imx415_write(sensor, reg->address, reg->val);
			if (ret) return ret;
		}
		break;
	default:
		dev_dbg(sensor->dev, "Applying NO_HDR configuration\n");
		break;
	}

	for (i = 0; i < IMX415_NUM_CLK_PARAM_REGS; ++i) {
		reg = &sensor->clk_params->regs[i];
		ret = imx415_write(sensor, reg->address, reg->val);
		if (ret) return ret;
	}

	ret = imx415_write(sensor, IMX415_LANEMODE,
				sensor->num_data_lanes == 2 ? IMX415_LANEMODE_2 :
								IMX415_LANEMODE_4);

	if (sensor->hdr_mode != NO_HDR) {
		ret = imx415_read(sensor, IMX415_RHS1);
		if (ret < 0) {
			dev_warn(sensor->dev, "Failed to read RHS1, ret=%d\n", ret);
		} else {
			sensor->rhs1 = ret;
			dev_dbg(sensor->dev, "RHS1 = 0x%x\n", sensor->rhs1);
		}

		ret = imx415_read(sensor, IMX415_RHS2);
		if (ret < 0) {
			dev_warn(sensor->dev, "Failed to read RHS2, ret=%d\n", ret);
		} else {
			sensor->rhs2 = ret;
			dev_dbg(sensor->dev, "RHS2 = 0x%x\n", sensor->rhs2);
		}

	}

	vmax_val = imx415_read(sensor, IMX415_VMAX);
	if (vmax_val >= 0)
		sensor->cur_vmax = vmax_val;

	return 0;
}

static int imx415_setup(struct imx415 *sensor, struct v4l2_subdev_state *state)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(imx415_init_table); ++i) {
		ret = imx415_write(sensor, imx415_init_table[i].address,
				   imx415_init_table[i].val);
		if (ret)
			return ret;
	}

	return imx415_set_mode(sensor, sensor->cur_mode);
}

static int imx415_wakeup(struct imx415 *sensor)
{
	int ret;

	ret = imx415_write(sensor, IMX415_MODE, IMX415_MODE_OPERATING);
	if (ret)
		return ret;

	/*
	 * According to the datasheet we have to wait at least 63 us after
	 * leaving standby mode. But this doesn't work even after 30 ms.
	 * So probably this should be 63 ms and therefore we wait for 80 ms.
	 */
	msleep(80);

	return 0;
}

static int imx415_stream_on(struct imx415 *sensor)
{
	int ret;

	ret = imx415_wakeup(sensor);
	if (ret)
		return ret;

	ret = imx415_read(sensor, IMX415_RHS1);
	if (ret >= 0) {
		sensor->rhs1 = ret;
		dev_dbg(sensor->dev, "RHS1 read = 0x%06x\n", sensor->rhs1);
	} else {
		dev_warn(sensor->dev, "Failed to read RHS1\n");
	}

	ret = imx415_read(sensor, IMX415_RHS2);
	if (ret >= 0) {
		sensor->rhs2 = ret;
		dev_dbg(sensor->dev, "RHS2 read = 0x%06x\n", sensor->rhs2);
	} else {
		dev_warn(sensor->dev, "Failed to read RHS2\n");
	}

	ret = imx415_read(sensor, IMX415_GAIN_PGC_FIDMD);
	if (ret >= 0) {
		dev_dbg(sensor->dev, "GAIN_PGC_FIDMD = 0x%x\n", ret);
	} else {
		dev_warn(sensor->dev, "Failed to read GAIN_PGC_FIDMD\n");
	}

	return imx415_write(sensor, IMX415_XMSTA, IMX415_XMSTA_START);
}

static int imx415_stream_off(struct imx415 *sensor)
{
	int ret;

	ret = imx415_write(sensor, IMX415_XMSTA, IMX415_XMSTA_STOP);
	if (ret)
		return ret;

	return imx415_write(sensor, IMX415_MODE, IMX415_MODE_STANDBY);
}

static int imx415_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct imx415 *sensor = to_imx415(sd);
	struct v4l2_subdev_state *state;
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (!enable) {
		ret = imx415_stream_off(sensor);

		pm_runtime_mark_last_busy(sensor->dev);
		pm_runtime_put_autosuspend(sensor->dev);

		sensor->streaming = false;

		goto unlock;
	}

	ret = pm_runtime_resume_and_get(sensor->dev);
	if (ret < 0)
		goto unlock;

	ret = imx415_setup(sensor, state);
	if (ret)
		goto err_pm;

	/*
	 * Set streaming to true to ensure __v4l2_ctrl_handler_setup() will set
	 * the controls. The flag is reset to false further down if an error
	 * occurs.
	 */
	sensor->streaming = true;

	ret = __v4l2_ctrl_handler_setup(&sensor->ctrls);
	if (ret < 0)
		goto err_pm;

	ret = imx415_stream_on(sensor);
	if (ret)
		goto err_pm;

	ret = 0;

unlock:
	v4l2_subdev_unlock_state(state);

	return ret;

err_pm:
	/*
	 * In case of error, turn the power off synchronously as the device
	 * likely has no other chance to recover.
	 */
	pm_runtime_put_sync(sensor->dev);
	sensor->streaming = false;

	goto unlock;
}

static int imx415_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGBRG10_1X10;

	return 0;
}

static int imx415_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	const struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_pad_format(sd, state, fse->pad);

	if (fse->index > 0 || fse->code != format->code)
		return -EINVAL;

	fse->min_width = IMX415_PIXEL_ARRAY_WIDTH;
	fse->max_width = fse->min_width;
	fse->min_height = IMX415_PIXEL_ARRAY_HEIGHT;
	fse->max_height = fse->min_height;
	return 0;
}

static int imx415_get_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_format *fmt)
{
	fmt->format = *v4l2_subdev_get_pad_format(sd, state, fmt->pad);

	return 0;
}

static int imx415_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *state,
			     struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_pad_format(sd, state, fmt->pad);

	format->width = fmt->format.width;
	format->height = fmt->format.height;
	format->code = MEDIA_BUS_FMT_SGBRG10_1X10;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_RAW;
	format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->quantization = V4L2_QUANTIZATION_DEFAULT;
	format->xfer_func = V4L2_XFER_FUNC_NONE;

	fmt->format = *format;
	return 0;
}

static int imx415_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *sd_state,
				struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = IMX415_PIXEL_ARRAY_TOP;
		sel->r.left = IMX415_PIXEL_ARRAY_LEFT;
		sel->r.width = IMX415_PIXEL_ARRAY_WIDTH;
		sel->r.height = IMX415_PIXEL_ARRAY_HEIGHT;

		return 0;
	}

	return -EINVAL;
}

static int imx415_init_cfg(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format format = {
		.format = {
			.width = IMX415_PIXEL_ARRAY_WIDTH,
			.height = IMX415_PIXEL_ARRAY_HEIGHT,
		},
	};

	imx415_set_format(sd, state, &format);

	return 0;
}

static int imx415_get_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2_DPHY;
	cfg->bus.mipi_csi2.num_data_lanes = 4;
	return 0;
}


static const struct v4l2_subdev_video_ops imx415_subdev_video_ops = {
	.s_stream = imx415_s_stream,
};

static const struct v4l2_subdev_pad_ops imx415_subdev_pad_ops = {
	.enum_mbus_code = imx415_enum_mbus_code,
	.enum_frame_size = imx415_enum_frame_size,
	.get_fmt = imx415_get_format,
	.set_fmt = imx415_set_format,
	.get_selection = imx415_get_selection,
	.init_cfg = imx415_init_cfg,
	.get_mbus_config = imx415_get_mbus_config,
};

static int imx415_find_mode(u64 lane_rate, u32 hdr_mode)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (supported_modes[i].lane_rate == lane_rate &&
			supported_modes[i].reg_list.hdr_mode == hdr_mode) {
			return i;
		}
	}

	return -EINVAL;
}

static long imx415_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct imx415 *sensor = to_imx415(sd);
	struct esmodule_hdr_cfg *hdr_cfg;
	u64 current_lane_rate;
	int new_mode;
	u32 exposure_lines;
	u32 fsc;
	u32 shr0, shr1, shr2;
	u32 rhs1, rhs2;
	u32 gain_val;
	int ret = 0;

	switch (cmd) {
	case ESMODULE_SET_SHR0:
		shr0 = *(u32 *)arg;
		dev_dbg(sensor->dev, "write SHR0 = 0x%x\n", shr0);
		return imx415_write(sensor, IMX415_SHR0, shr0);

	case ESMODULE_SET_SHR1:
		shr1 = *(u32 *)arg;
		dev_dbg(sensor->dev, "write SHR1 = 0x%x\n", shr1);
		return imx415_write(sensor, IMX415_SHR1, shr1);

	case ESMODULE_SET_SHR2:
		shr2 = *(u32 *)arg;
		dev_dbg(sensor->dev, "write SHR2 = 0x%x\n", shr2);
		return imx415_write(sensor, IMX415_SHR2, shr2);

	case ESMODULE_SET_LONG_EXPOSURE:
		exposure_lines = *(u32 *)arg;

		if (sensor->hdr_mode == NO_HDR)
			return -EINVAL;

		fsc = sensor->cur_vmax * sensor->fsc_multiplier;

		if (sensor->hdr_mode == HDR_X2) {
			u32 rhs_limit = sensor->rhs1 + 9;
			u32 max_exp_lines = fsc - rhs_limit;

			if (exposure_lines > max_exp_lines)
				exposure_lines = max_exp_lines;
			if (exposure_lines < 8)
				exposure_lines = 8;

			shr0 = fsc - exposure_lines;
			shr0 &= ~1;
			if (shr0 < (sensor->rhs1 + 9))
				shr0 = (sensor->rhs1 + 9) & ~1;
			if (shr0 > (fsc - 8))
				shr0 = (fsc - 8) & ~1;
		} else {
			/* SHR0 = 3n, (RHS2 + 13) ≤ SHR0 ≤ (FSC - 12) */
			u32 min_shr0 = ((sensor->rhs2 + 13) + 2) / 3 * 3;
			u32 max_shr0 = (fsc - 12) / 3 * 3;

			if (exposure_lines > (fsc - min_shr0))
				exposure_lines = fsc - min_shr0;
			if (exposure_lines < (fsc - max_shr0))
				exposure_lines = fsc - max_shr0;

			shr0 = fsc - exposure_lines;
			shr0 = clamp(shr0, min_shr0, max_shr0);
			shr0 = (shr0 / 3) * 3;
		}

		dev_dbg(sensor->dev, "LONG_EXP: req=%u fsc=%u shr0=0x%x\n",
			 *(u32 *)arg, fsc, shr0);
		ret = imx415_write(sensor, IMX415_SHR0, shr0);
		break;

	case ESMODULE_SET_SHORT1_EXPOSURE:
		exposure_lines = *(u32 *)arg;
		if (sensor->hdr_mode == NO_HDR)
			return -EINVAL;

		if (sensor->hdr_mode == HDR_X2) {
			rhs1 = sensor->rhs1;
			u32 max_short_lines = rhs1 - 9;

			if (exposure_lines > max_short_lines)
				exposure_lines = max_short_lines;
			if (exposure_lines < 1)
				exposure_lines = 1;

			shr1 = rhs1 - exposure_lines;
			shr1 = ((shr1 - 1) | 1) + 1;
			if (shr1 < 9)
				shr1 = 9;
			if (shr1 > (rhs1 - 8))
				shr1 = (rhs1 - 8) | 1;
		} else {
			/* SHR1 = 3n+1, 13 ≤ SHR1 ≤ (RHS1 - 12) */
			rhs1 = sensor->rhs1;
			u32 min_shr1 = 13;
			u32 max_shr1 = (rhs1 - 12) / 3 * 3 + 1;

			u32 max_exp_lines = rhs1 - min_shr1;
			u32 min_exp_lines = rhs1 - max_shr1;

			if (exposure_lines > max_exp_lines)
				exposure_lines = max_exp_lines;
			if (exposure_lines < min_exp_lines)
				exposure_lines = min_exp_lines;

			shr1 = rhs1 - exposure_lines;
			shr1 = clamp(shr1, min_shr1, max_shr1);
			shr1 = ((shr1 - 1) / 3) * 3 + 1;  /* 3n+1 */
		}

		dev_dbg(sensor->dev, "SHORT1_EXP: req=%u rhs1=0x%x shr1=0x%x\n",
			 *(u32 *)arg, sensor->rhs1, shr1);
		ret = imx415_write(sensor, IMX415_SHR1, shr1);
		break;

	case ESMODULE_SET_SHORT2_EXPOSURE:
		exposure_lines = *(u32 *)arg;

		if (sensor->hdr_mode != HDR_X3) {
			dev_err(sensor->dev, "SHORT2_EXPOSURE only for DOL3\n");
			return -EINVAL;
		}

		/* SHR2 = 3n+2, (RHS1 + 13) ≤ SHR2 ≤ (RHS2 - 12) */
		rhs1 = sensor->rhs1;
		rhs2 = sensor->rhs2;
		u32 min_shr2 = rhs1 + 13;
		u32 max_shr2 = (rhs2 - 12) / 3 * 3 + 2;

		u32 max_exp_lines = rhs2 - min_shr2;
		u32 min_exp_lines = rhs2 - max_shr2;

		if (exposure_lines > max_exp_lines)
			exposure_lines = max_exp_lines;
		if (exposure_lines < min_exp_lines)
			exposure_lines = min_exp_lines;

		shr2 = rhs2 - exposure_lines;
		shr2 = clamp(shr2, min_shr2, max_shr2);
		shr2 = ((shr2 - 2) / 3) * 3 + 2;

		dev_dbg(sensor->dev, "SHORT2_EXP: req=%u rhs2=0x%x shr2=0x%x\n",
			 *(u32 *)arg, rhs2, shr2);
		ret = imx415_write(sensor, IMX415_SHR2, shr2);
		break;

	case ESMODULE_GET_RHS1:
		*(u32 *)arg = sensor->rhs1;
		dev_dbg(sensor->dev, "GET_RHS1 = 0x%x\n", sensor->rhs1);
		break;

	case ESMODULE_GET_RHS2:
		*(u32 *)arg = sensor->rhs2;
		dev_dbg(sensor->dev, "GET_RHS2 = 0x%x\n", sensor->rhs2);
		break;

	case ESMODULE_SET_LONG_GAIN:
		gain_val = *(u32 *)arg;
		dev_dbg(sensor->dev, "LONG GAIN: received long gain reg = 0x%x (%u)\n", gain_val, gain_val);
		ret = imx415_write(sensor, IMX415_GAIN_PCG_0, gain_val);
		break;

	case ESMODULE_SET_SHORT1_GAIN:
        gain_val = *(u32 *)arg;
		dev_dbg(sensor->dev, "SHORT GAIN: received short1 gain reg = 0x%x (%u)\n", gain_val, gain_val);
        if (sensor->hdr_mode == NO_HDR) {
            dev_err(sensor->dev, "Not in HDR mode, cannot set short1 gain\n");
            return -EINVAL;
        }
        ret = imx415_write(sensor, IMX415_GAIN_PGC_1, gain_val);
        break;

	case ESMODULE_SET_SHORT2_GAIN:
        gain_val = *(u32 *)arg;
		dev_dbg(sensor->dev, "set short2 gain reg=0x%x\n", gain_val);
        if (sensor->hdr_mode != HDR_X3) {
            dev_err(sensor->dev, "Not in DOL3 mode, cannot set short2 gain\n");
            return -EINVAL;
        }
        ret = imx415_write(sensor, IMX415_GAIN_PGC_2, gain_val);
        break;

	case ESMODULE_GET_HDR_CFG:
		hdr_cfg = (struct esmodule_hdr_cfg *)arg;
		hdr_cfg->hdr_mode = sensor->hdr_mode;
		dev_dbg(sensor->dev, "Get HDR mode: %d\n", sensor->hdr_mode);
		break;

	case ESMODULE_SET_HDR_CFG:
		hdr_cfg = (struct esmodule_hdr_cfg *)arg;
		if (hdr_cfg->hdr_mode != NO_HDR && 
			hdr_cfg->hdr_mode != HDR_X2 && 
			hdr_cfg->hdr_mode != HDR_X3) {
			dev_err(sensor->dev, "Invalid HDR mode: %d\n", hdr_cfg->hdr_mode);
			return -EINVAL;
		}

		dev_dbg(sensor->dev, "HDR mode: %d\n", hdr_cfg->hdr_mode);

		/* TODO: support sensor streaming can change HDR MODE */
		if (sensor->streaming) {
			dev_err(sensor->dev, "Cannot set HDR mode while streaming. Stop streaming first.\n");
			return -EBUSY;
		}

		current_lane_rate = supported_modes[sensor->cur_mode].lane_rate;

		new_mode = imx415_find_mode(current_lane_rate, hdr_cfg->hdr_mode);
		if (new_mode < 0) {
			dev_err(sensor->dev, "No mode found for HDR mode %d\n",
					hdr_cfg->hdr_mode);
			return -EINVAL;
		}

		sensor->cur_mode = new_mode;
		sensor->hdr_mode = hdr_cfg->hdr_mode;

		switch (sensor->hdr_mode) {
			case NO_HDR: sensor->fsc_multiplier = 1; break;
			case HDR_X2: sensor->fsc_multiplier = 2; break;
			case HDR_X3: sensor->fsc_multiplier = 4; break;
			default: sensor->fsc_multiplier = 1;
		}

		dev_dbg(sensor->dev, "HDR mode set to %d. Configuration will be applied on next stream start.\n",
					sensor->hdr_mode);
		break;


	default:
		return -ENOIOCTLCMD;
	}

    return ret;
}

static const struct v4l2_subdev_core_ops imx415_subdev_core_ops = {
    .ioctl = imx415_ioctl,
};

static const struct v4l2_subdev_ops imx415_subdev_ops = {
	.core = &imx415_subdev_core_ops,
	.video = &imx415_subdev_video_ops,
	.pad = &imx415_subdev_pad_ops,
};

static int imx415_subdev_init(struct imx415 *sensor)
{
	struct i2c_client *client = to_i2c_client(sensor->dev);
	int ret;

	v4l2_i2c_subdev_init(&sensor->subdev, client, &imx415_subdev_ops);

	/* controls init (subdev.ctrl_handler will be set inside) */
	ret = imx415_ctrls_init(sensor);
	if (ret)
		return ret;

	sensor->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
				V4L2_SUBDEV_FL_HAS_EVENTS;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sensor->subdev.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sensor->subdev.entity, 1, &sensor->pad);
	if (ret < 0) {
		v4l2_ctrl_handler_free(&sensor->ctrls);
		return ret;
	}

	sensor->subdev.state_lock = sensor->subdev.ctrl_handler->lock;
	v4l2_subdev_init_finalize(&sensor->subdev);

	return 0;
}

static void imx415_subdev_cleanup(struct imx415 *sensor)
{
	media_entity_cleanup(&sensor->subdev.entity);
	v4l2_ctrl_handler_free(&sensor->ctrls);
}

static int imx415_power_on(struct imx415 *sensor)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies),
				    sensor->supplies);
	if (ret < 0)
		return ret;

	gpiod_set_value_cansleep(sensor->reset, 1);

	udelay(1);

	ret = clk_prepare_enable(sensor->clk);
	if (ret < 0)
		goto err_reset;

	/*
	 * Data sheet states that 20 us are required before communication start,
	 * but this doesn't work in all cases. Use 100 us to be on the safe
	 * side.
	 */
	usleep_range(100, 200);

	return 0;

err_reset:
	gpiod_set_value_cansleep(sensor->reset, 0);
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
	return ret;
}

static void imx415_power_off(struct imx415 *sensor)
{
	clk_disable_unprepare(sensor->clk);
	gpiod_set_value_cansleep(sensor->reset, 0);
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
}

static int imx415_identify_model(struct imx415 *sensor)
{
	int model, ret;

	/*
	 * While most registers can be read when the sensor is in standby, this
	 * is not the case of the sensor info register :-(
	 */
	ret = imx415_wakeup(sensor);
	if (ret)
		return dev_err_probe(sensor->dev, ret,
				     "failed to get sensor out of standby\n");

	ret = imx415_read(sensor, IMX415_SENSOR_INFO);
	if (ret < 0) {
		dev_err_probe(sensor->dev, ret,
			      "failed to read sensor information\n");
		goto done;
	}

	model = ret & IMX415_SENSOR_INFO_MASK;

	switch (model) {
	case IMX415_CHIP_ID:
		dev_info(sensor->dev, "Detected IMX415 image sensor\n");
		break;
	default:
		ret = dev_err_probe(sensor->dev, -ENODEV,
				    "invalid device model 0x%04x\n", model);
		goto done;
	}

	ret = 0;

done:
	imx415_write(sensor, IMX415_MODE, IMX415_MODE_STANDBY);
	return ret;
}

static int imx415_check_inck(unsigned long inck, u64 link_frequency)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(imx415_clk_params); ++i) {
		if ((imx415_clk_params[i].lane_rate == link_frequency * 2) &&
		    imx415_clk_params[i].inck == inck)
			break;
	}

	if (i == ARRAY_SIZE(imx415_clk_params))
		return -EINVAL;
	else
		return 0;
}

static int imx415_parse_hw_config(struct imx415 *sensor)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep;
	u64 lane_rate;
	unsigned long inck;
	unsigned int i;
	int ret;

	ret = of_property_read_u32(sensor->dev->of_node, OF_CAMERA_HDR_MODE, &sensor->hdr_mode);
	if (ret)
		sensor->hdr_mode = NO_HDR;

	for (i = 0; i < ARRAY_SIZE(sensor->supplies); ++i)
		sensor->supplies[i].supply = imx415_supply_names[i];

	ret = devm_regulator_bulk_get(sensor->dev, ARRAY_SIZE(sensor->supplies),
				      sensor->supplies);
	if (ret)
		return dev_err_probe(sensor->dev, ret,
				     "failed to get supplies\n");

	sensor->reset = devm_gpiod_get_optional(sensor->dev, "reset",
						GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset))
		return dev_err_probe(sensor->dev, PTR_ERR(sensor->reset),
				     "failed to get reset GPIO\n");

	sensor->clk = devm_clk_get(sensor->dev, "inck");
	if (IS_ERR(sensor->clk))
		return dev_err_probe(sensor->dev, PTR_ERR(sensor->clk),
				     "failed to get clock\n");

	ep = fwnode_graph_get_next_endpoint(dev_fwnode(sensor->dev), NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	switch (bus_cfg.bus.mipi_csi2.num_data_lanes) {
	case 2:
	case 4:
		sensor->num_data_lanes = bus_cfg.bus.mipi_csi2.num_data_lanes;
		break;
	default:
		ret = dev_err_probe(sensor->dev, -EINVAL,
				    "invalid number of CSI2 data lanes %d\n",
				    bus_cfg.bus.mipi_csi2.num_data_lanes);
		goto done_endpoint_free;
	}

	if (!bus_cfg.nr_of_link_frequencies) {
		ret = dev_err_probe(sensor->dev, -EINVAL,
				    "no link frequencies defined");
		goto done_endpoint_free;
	}

	/*
	 * Check if there exists a sensor mode defined for current INCK,
	 * number of lanes and given lane rates.
	 */
	inck = clk_get_rate(sensor->clk);
	if (inck > 23520000 && inck < 24480000) {
		/* If clock is in range 24MHz, use 24MHz link frequency */
		inck = 24000000;
	}

	for (i = 0; i < bus_cfg.nr_of_link_frequencies; ++i) {
		if (imx415_check_inck(inck, bus_cfg.link_frequencies[i])) {
			dev_info(sensor->dev,
				"INCK %lu Hz not supported for this link freq",
				inck);
			continue;
		}

		lane_rate = bus_cfg.link_frequencies[i] * 2;
		sensor->cur_mode = imx415_find_mode(lane_rate, sensor->hdr_mode);
		if (sensor->cur_mode >= 0) {
			break;
		} else {
			dev_info(sensor->dev, "No mode found for HDR %d, trying NO_HDR\n",
						sensor->hdr_mode);
			sensor->cur_mode = imx415_find_mode(lane_rate, NO_HDR);
			if (sensor->cur_mode >= 0) {
				sensor->hdr_mode = NO_HDR;
				break;
			}
		}
	}

	if (i == bus_cfg.nr_of_link_frequencies) {
		ret = dev_err_probe(sensor->dev, -EINVAL,
				    "no valid sensor mode defined\n");
		goto done_endpoint_free;
	}

	switch (inck) {
	case 27000000:
	case 37125000:
	case 74250000:
		sensor->pixel_rate = IMX415_PIXEL_RATE_74_25MHZ;
		break;
	case 24000000:
	case 72000000:
		sensor->pixel_rate = IMX415_PIXEL_RATE_72MHZ;
		break;
	}

	lane_rate = supported_modes[sensor->cur_mode].lane_rate;
	for (i = 0; i < ARRAY_SIZE(imx415_clk_params); ++i) {
		if (lane_rate == imx415_clk_params[i].lane_rate &&
		    inck == imx415_clk_params[i].inck) {
			sensor->clk_params = &imx415_clk_params[i];
			break;
		}
	}

	if (i == ARRAY_SIZE(imx415_clk_params)) {
		ret = dev_err_probe(sensor->dev, -EINVAL,
				    "Mode %d not supported\n",
				    sensor->cur_mode);
		goto done_endpoint_free;
	}

	ret = 0;
	dev_dbg(sensor->dev, "clock: %lu Hz, lane_rate: %llu bps, lanes: %d\n",
		inck, lane_rate, sensor->num_data_lanes);

done_endpoint_free:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int imx415_probe(struct i2c_client *client)
{
	struct imx415 *sensor;
	int ret;

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->dev = &client->dev;

	ret = imx415_parse_hw_config(sensor);
	if (ret)
		return ret;

	sensor->regmap = devm_regmap_init_i2c(client, &imx415_regmap_config);
	if (IS_ERR(sensor->regmap))
		return PTR_ERR(sensor->regmap);

	/*
	 * Enable power management. The driver supports runtime PM, but needs to
	 * work when runtime PM is disabled in the kernel. To that end, power
	 * the sensor on manually here, identify it, and fully initialize it.
	 */
	ret = imx415_power_on(sensor);
	if (ret)
		return ret;

	ret = imx415_identify_model(sensor);
	if (ret)
		goto err_power;

	ret = imx415_subdev_init(sensor);
	if (ret)
		goto err_power;

	/*
	 * Enable runtime PM. As the device has been powered manually, mark it
	 * as active, and increase the usage count without resuming the device.
	 */
	pm_runtime_set_active(sensor->dev);
	pm_runtime_get_noresume(sensor->dev);
	pm_runtime_enable(sensor->dev);

	ret = v4l2_async_register_subdev_sensor(&sensor->subdev);
	if (ret < 0)
		goto err_pm;

	/*
	 * Finally, enable autosuspend and decrease the usage count. The device
	 * will get suspended after the autosuspend delay, turning the power
	 * off.
	 */
	pm_runtime_set_autosuspend_delay(sensor->dev, 1000);
	pm_runtime_use_autosuspend(sensor->dev);
	pm_runtime_put_autosuspend(sensor->dev);

	dev_info(&client->dev, "IMX415 sensor probed successfully\n");

	return 0;

err_pm:
	pm_runtime_disable(sensor->dev);
	pm_runtime_put_noidle(sensor->dev);
	imx415_subdev_cleanup(sensor);
err_power:
	imx415_power_off(sensor);
	return ret;
}

static void imx415_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct imx415 *sensor = to_imx415(subdev);

	v4l2_async_unregister_subdev(subdev);

	imx415_subdev_cleanup(sensor);

	/*
	 * Disable runtime PM. In case runtime PM is disabled in the kernel,
	 * make sure to turn power off manually.
	 */
	pm_runtime_disable(sensor->dev);
	if (!pm_runtime_status_suspended(sensor->dev))
		imx415_power_off(sensor);
	pm_runtime_set_suspended(sensor->dev);
}

static int imx415_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct imx415 *sensor = to_imx415(subdev);

	return imx415_power_on(sensor);
}

static int imx415_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct imx415 *sensor = to_imx415(subdev);

	imx415_power_off(sensor);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(imx415_pm_ops, imx415_runtime_suspend,
				 imx415_runtime_resume, NULL);

static const struct of_device_id imx415_of_match[] = {
	{ .compatible = "sony,imx415" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, imx415_of_match);

static struct i2c_driver imx415_driver = {
	.probe = imx415_probe,
	.remove = imx415_remove,
	.driver = {
		.name = "imx415",
		.of_match_table = imx415_of_match,
		.pm = pm_ptr(&imx415_pm_ops),
	},
};

module_i2c_driver(imx415_driver);

MODULE_DESCRIPTION("Sony IMX415 image sensor driver");
MODULE_AUTHOR("Gerald Loacker <gerald.loacker@wolfvision.net>");
MODULE_AUTHOR("Michael Riesch <michael.riesch@wolfvision.net>");
MODULE_LICENSE("GPL");