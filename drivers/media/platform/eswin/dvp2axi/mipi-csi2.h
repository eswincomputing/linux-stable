#ifndef _ES_DVP2AXI_MIPI_CSI2_H_
#define _ES_DVP2AXI_MIPI_CSI2_H_

#include <linux/notifier.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-event.h>
#include "../../../../phy/eswin/es-dvp2axi-config.h"

#define CSI2_ERR_FSFE_MASK	(0xff << 8)
#define CSI2_ERR_COUNT_ALL_MASK	(0xff)

#define ES_DVP2AXI_V4L2_EVENT_ELEMS 4

/*
 * there must be 5 pads: 1 input pad from sensor, and
 * the 4 virtual channel output pads
 */
#define CSI2_SINK_PAD			0
#define CSI2_NUM_SINK_PADS		4
#define CSI2_NUM_SRC_PADS		11
#define CSI2_NUM_PADS			3
#define CSI2_NUM_PADS_MAX		12
#define CSI2_NUM_PADS_SINGLE_LINK	2
#define MAX_CSI2_SENSORS		2

// #define ES_DVP2AXI_DEFAULT_WIDTH 1920	//3280
// #define ES_DVP2AXI_DEFAULT_HEIGHT 1080 //	2464

#define CSI_ERRSTR_LEN		(256)
#define CSI_VCINFO_LEN		(12)

/*
 * The default maximum bit-rate per lane in Mbps, if the
 * source subdev does not provide V4L2_CID_LINK_FREQ.
 */
#define CSI2_DEFAULT_MAX_MBPS 849

#define IMX_MEDIA_GRP_ID_CSI2      BIT(8)
#define CSIHOST_MAX_ERRINT_COUNT	10

#define DEVICE_NAME "eswin-mipi-csi2"
#define DEVICE_NAME_HW "eswin-mipi-csi2-hw"

/* CSI Host Registers Define */
#define CSIHOST_N_LANES		0x04
#define CSIHOST_DPHY_SHUTDOWNZ	0x08
#define CSIHOST_PHY_RSTZ	0x0c
#define CSIHOST_RESETN		0x10
#define CSIHOST_PHY_STATE	0x14
#define CSIHOST_ERR1		0x20
#define CSIHOST_ERR2		0x24
#define CSIHOST_MSK1		0x28
#define CSIHOST_MSK2		0x2c
#define CSIHOST_CONTROL		0x40

#define CSIHOST_ERR1_PHYERR_SPTSYNCHS	0x0000000f
#define CSIHOST_ERR1_ERR_BNDRY_MATCH	0x000000f0
#define CSIHOST_ERR1_ERR_SEQ		0x00000f00
#define CSIHOST_ERR1_ERR_FRM_DATA	0x0000f000
#define CSIHOST_ERR1_ERR_CRC		0x0f000000
#define CSIHOST_ERR1_ERR_ECC2		0x10000000
#define CSIHOST_ERR1_ERR_CTRL		0x000f0000

#define CSIHOST_ERR2_PHYERR_ESC		0x0000000f
#define CSIHOST_ERR2_PHYERR_SOTHS	0x000000f0
#define CSIHOST_ERR2_ECC_CORRECTED	0x00000f00
#define CSIHOST_ERR2_ERR_ID		0x0000f000
#define CSIHOST_ERR2_PHYERR_CODEHS	0x01000000


#define DWC_MIPI_CSI2_VERSION			0x0

#define DWC_MIPI_CSI2_N_LANES			0x4
#define DWC_MIPI_CSI2_N_LANES_N_LANES(x)	((x) & 0x7)

#define DWC_MIPI_CSI2_HOST_RESETN		0x8
#define DWC_MIPI_CSI2_HOST_RESETN_ENABLE	(0x1)

#define DWC_MIPI_CSI2_INT_ST_MAIN		0xC

#define DWC_MIPI_CSI2_DATA_IDS_1		0x10
#define DWC_MIPI_CSI2_DATA_IDS_1_DI0_DT(x)	(((x) & 0x3f))
#define DWC_MIPI_CSI2_DATA_IDS_1_DI1_DT(x)	(((x) & 0x3f) << 8)
#define DWC_MIPI_CSI2_DATA_IDS_1_DI2_DT(x)	(((x) & 0x3f) << 16)
#define DWC_MIPI_CSI2_DATA_IDS_1_DI3_DT(x)	(((x) & 0x3f) << 24)

#define DWC_MIPI_CSI2_DATA_IDS_2		0x14
#define DWC_MIPI_CSI2_DATA_IDS_2_DI0_DT(x)	(((x) & 0x3f))
#define DWC_MIPI_CSI2_DATA_IDS_2_DI1_DT(x)	(((x) & 0x3f) << 8)
#define DWC_MIPI_CSI2_DATA_IDS_2_DI2_DT(x)	(((x) & 0x3f) << 16)
#define DWC_MIPI_CSI2_DATA_IDS_2_DI3_DT(x)	(((x) & 0x3f) << 24)

#define DWC_MIPI_CSI2_DPHY_CFG			0x18
#define DWC_MIPI_CSI2_DPHY_CFG_PPI8		(0x0)
#define DWC_MIPI_CSI2_DPHY_CFG_PPI16		(0x1)

#define DWC_MIPI_CSI2_DPHY_MODE			0x1C
#define DWC_MIPI_CSI2_DPHY_MODE_DPHY		(0x0)
#define DWC_MIPI_CSI2_DPHY_MODE_CPHY		(0x1)

#define DWC_MIPI_CSI2_INT_ST_AP_MAIN		0x2C

#define DWC_MIPI_CSI2_DATA_IDS_VC_1			0x30
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI0_VC(x)		((x & 0x3))
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI0_VC_0_1(x)	((x & 0x3) << 2)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI0_VC_2		(0x1 << 4)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI1_VC(x)		((x & 0x3) << 8)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI1_VC_0_1(x)	((x & 0x3) << 10)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI1_VC_2		(0x1 << 12)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI2_VC(x)		((x & 0x3) << 16)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI2_VC_0_1(x)	((x & 0x3) << 18)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI2_VC_2		(0x1 << 20)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI3_VC(x)		((x & 0x3) << 24)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI3_VC_0_1(x)	((x & 0x3) << 26)
#define DWC_MIPI_CSI2_DATA_IDS_VC_1_DI3_VC_2		(0x1 << 28)

#define DWC_MIPI_CSI2_DATA_IDS_VC_2			0x34
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI4_VC(x)		((x & 0x3))
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI4_VC_0_1(x)	((x & 0x3) << 2)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI4_VC_2		(0x1 << 4)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI5_VC(x)		((x & 0x3) << 8)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI5_VC_0_1(x)	((x & 0x3) << 10)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI5_VC_2		(0x1 << 12)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI6_VC(x)		((x & 0x3) << 16)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI6_VC_0_1(x)	((x & 0x3) << 18)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI6_VC_2		(0x1 << 20)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI7_VC(x)		((x & 0x3) << 24)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI7_VC_0_1(x)	((x & 0x3) << 26)
#define DWC_MIPI_CSI2_DATA_IDS_VC_2_DI7_VC_2		(0x1 << 28)

#define DWC_MIPI_CSI2_DPHY_SHUTDOWNZ		0x40
#define DWC_MIPI_CSI2_DPHY_SHUTDOWNZ_ENABLE	(0x1)

#define DWC_MIPI_CSI2_DPHY_RSTZ			0x44
#define DWC_MIPI_CSI2_DPHY_RSTZ_ENABLE		(0x1)

#define DWC_MIPI_CSI2_DPHY_RX_STATUS			0x48
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_CLK_LANE_HS	(0x1 << 17)
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_CLK_LANE_ULP	(0x1 << 16)
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_DATA_LANE0_ULP	(0x1)
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_DATA_LANE1_ULP	(0x1 << 1)
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_DATA_LANE2_ULP	(0x1 << 2)
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_DATA_LANE3_ULP	(0x1 << 3)
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_DATA_LANE4_ULP	(0x1 << 4)
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_DATA_LANE5_ULP	(0x1 << 5)
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_DATA_LANE6_ULP	(0x1 << 6)
#define DWC_MIPI_CSI2_DPHY_RX_STATUS_DATA_LANE7_ULP	(0x1 << 7)

#define DWC_MIPI_CSI2_DPHY_STOPSTATE			0x4C
#define DWC_MIPI_CSI2_DPHY_STOPSTATE_CLK_LANE		(0x1 << 16)
#define DWC_MIPI_CSI2_DPHY_STOPSTATE_DATA_LANE0		(0x1)
#define DWC_MIPI_CSI2_DPHY_STOPSTATE_DATA_LANE1		(0x1 << 1)
#define DWC_MIPI_CSI2_DPHY_STOPSTATE_DATA_LANE2		(0x1 << 2)
#define DWC_MIPI_CSI2_DPHY_STOPSTATE_DATA_LANE3		(0x1 << 3)
#define DWC_MIPI_CSI2_DPHY_STOPSTATE_DATA_LANE4		(0x1 << 4)
#define DWC_MIPI_CSI2_DPHY_STOPSTATE_DATA_LANE5		(0x1 << 5)
#define DWC_MIPI_CSI2_DPHY_STOPSTATE_DATA_LANE6		(0x1 << 6)
#define DWC_MIPI_CSI2_DPHY_STOPSTATE_DATA_LANE7		(0x1 << 7)

#define DWC_MIPI_CSI2_DPHY_TEST_CTRL0			0x50
#define DWC_MIPI_CSI2_DPHY_TEST_CTRL0_TEST_CLR		(0x1)
#define DWC_MIPI_CSI2_DPHY_TEST_CTRL0_TEST_CLKEN	(0x1 << 1)

#define DWC_MIPI_CSI2_DPHY_TEST_CTRL1			0x54
#define DWC_MIPI_CSI2_DPHY_TEST_CTRL1_TEST_DIN(x)	((x) & 0xFF)
#define DWC_MIPI_CSI2_DPHY_TEST_CTRL1_TEST_DOUT(x)	(((x) & 0xFF) << 8)
#define DWC_MIPI_CSI2_DPHY_TEST_CTRL1_TEST_EN		(0x1 << 16)

#define DWC_MIPI_CSI2_PPI_PG_PATTERN_VRES		0x60
#define DWC_MIPI_CSI2_PPI_PG_PATTERN_VRES_VRES(x)	((x) & 0xFFFF)

#define DWC_MIPI_CSI2_PPI_PG_PATTERN_HRES		0x64
#define DWC_MIPI_CSI2_PPI_PG_PATTERN_HRES_HRES(x)	((x) & 0xFFFF)

#define DWC_MIPI_CSI2_PPI_PG_CONFIG			0x68
#define DWC_MIPI_CSI2_PPI_PG_CONFIG_DATA_TYPE(x)	(((x) & 0x3F) << 8)
#define DWC_MIPI_CSI2_PPI_PG_CONFIG_VIR_CHAN(x)		(((x) & 0x3) << 14)
#define DWC_MIPI_CSI2_PPI_PG_CONFIG_VIR_CHAN_EX(x)	(((x) & 0x3) << 16)
#define DWC_MIPI_CSI2_PPI_PG_CONFIG_VIR_CHAN_EX_2_EN	(0x1 << 18)
#define DWC_MIPI_CSI2_PPI_PG_CONFIG_PG_MODE(x)		(x)

#define DWC_MIPI_CSI2_PPI_PG_ENABLE			0x6C
#define DWC_MIPI_CSI2_PPI_PG_ENABLE_EN			0x1

#define DWC_MIPI_CSI2_PPI_PG_STATUS			0x70

#define DWC_MIPI_CSI2_IPI_MODE				0x80
#define DWC_MIPI_CSI2_IPI_MODE_CAMERA			0x0
#define DWC_MIPI_CSI2_IPI_MODE_CONTROLLER		0x1
#define DWC_MIPI_CSI2_IPI_MODE_COLOR_MODE16		(0x1 << 8)
#define DWC_MIPI_CSI2_IPI_MODE_COLOR_MODE48		(0x0 << 8)
#define DWC_MIPI_CSI2_IPI_MODE_CUT_THROUGH		(0x1 << 16)
#define DWC_MIPI_CSI2_IPI_MODE_ENABLE			(0x1 << 24)

#define DWC_MIPI_CSI2_IPI_VCID				0x84
#define DWC_MIPI_CSI2_IPI_VCID_VC(x)			((x)  & 0x3)
#define DWC_MIPI_CSI2_IPI_VCID_VC_0_1(x)		(((x) & 0x3) << 2)
#define DWC_MIPI_CSI2_IPI_VCID_VC_2			(0x1 << 4)

#define DWC_MIPI_CSI2_IPI_DATA_TYPE			0x88
#define DWC_MIPI_CSI2_IPI_DATA_TYPE_DT(x)		((x) & 0x3F)
#define DWC_MIPI_CSI2_IPI_DATA_TYPE_EMB_DATA_EN		(0x1 << 8)

#define DWC_MIPI_CSI2_IPI_MEM_FLUSH			0x8C
#define DWC_MIPI_CSI2_IPI_MEM_FLUSH_AUTO		(0x1 << 8)

#define DWC_MIPI_CSI2_IPI_HSA_TIME			0x90
#define DWC_MIPI_CSI2_IPI_HSA_TIME_VAL(x)		((x) & 0xFFF)

#define DWC_MIPI_CSI2_IPI_HBP_TIME			0x94
#define DWC_MIPI_CSI2_IPI_HBP_TIME_VAL(x)		((x) & 0xFFF)

#define DWC_MIPI_CSI2_IPI_HSD_TIME			0x98
#define DWC_MIPI_CSI2_IPI_HSD_TIME_VAL(x)		((x) & 0xFFF)

#define DWC_MIPI_CSI2_IPI_HLINE_TIME			0x9C
#define DWC_MIPI_CSI2_IPI_HLINE_TIME_VAL(x)		((x) & 0x3FFF)

#define DWC_MIPI_CSI2_IPI_SOFTRSTN			0xA0
#define DWC_MIPI_CSI2_IPI_ADV_FEATURES			0xAC

#define DWC_MIPI_CSI2_IPI_VSA_LINES			0xB0
#define DWC_MIPI_CSI2_IPI_VSA_LINES_VAL(x)		((x) & 0x3FF)

#define DWC_MIPI_CSI2_IPI_VBP_LINES			0xB4
#define DWC_MIPI_CSI2_IPI_VBP_LINES_VAL(x)		((x) & 0x3FF)

#define DWC_MIPI_CSI2_IPI_VFP_LINES			0xB8
#define DWC_MIPI_CSI2_IPI_VFP_LINES_VAL(x)		((x) & 0x3FF)

#define DWC_MIPI_CSI2_IPI_VACTIVE_LINES			0xBC
#define DWC_MIPI_CSI2_IPI_VACTIVE_LINES_VAL(x)		((x) & 0x3FFF)

#define DWC_MIPI_CSI2_VC_EXTENSION			0xC8

#define DWC_MIPI_CSI2_DPHY_CAL				0xCC
#define DWC_MIPI_CSI2_INT_ST_DPHY_FATAL			0xE0
#define DWC_MIPI_CSI2_INT_MSK_DPHY_FATAL		0xE4
#define DWC_MIPI_CSI2_INT_FORCE_DPHY_FATAL		0xE8
#define DWC_MIPI_CSI2_INT_ST_PKT_FATAL			0xF0
#define DWC_MIPI_CSI2_INT_MSK_PKT_FATAL			0xF4
#define DWC_MIPI_CSI2_INT_FORCE_PKT_FATAL		0xF8

#define DWC_MIPI_CSI2_INT_ST_DPHY			0x110
#define DWC_MIPI_CSI2_INT_MSK_DPHY			0x114
#define DWC_MIPI_CSI2_INT_FORCE_DPHY			0x118
#define DWC_MIPI_CSI2_INT_ST_LINE			0x130
#define DWC_MIPI_CSI2_INT_MSK_LINE			0x134
#define DWC_MIPI_CSI2_INT_FORCE_LINE			0x138
#define DWC_MIPI_CSI2_INT_ST_IPI_FATAL			0x140
#define DWC_MIPI_CSI2_INT_MSK_IPI_FATAL			0x144
#define DWC_MIPI_CSI2_INT_FORCE_IPI_FATAL		0x148

#define DWC_MIPI_CSI2_INT_ST_AP_GENERIC			0x180
#define DWC_MIPI_CSI2_INT_MSK_AP_GENERIC		0x184
#define DWC_MIPI_CSI2_INT_FORCE_AP_GENERIC		0x188
#define DWC_MIPI_CSI2_INT_ST_AP_IPI_FATAL		0x190
#define DWC_MIPI_CSI2_INT_MSK_AP_IPI_FATAL		0x194
#define DWC_MIPI_CSI2_INT_FORCE_AP_IPI_FATAL		0x198

#define DWC_MIPI_CSI2_INT_ST_BNDRY_FRAME_FATAL		0x280
#define DWC_MIPI_CSI2_INT_MSK_BNDRY_FRAME_FATAL		0x284
#define DWC_MIPI_CSI2_INT_FORCE_BNDRY_FRAME_FATAL	0x288
#define DWC_MIPI_CSI2_INT_ST_SEQ_FRAME_FATAL		0x290
#define DWC_MIPI_CSI2_INT_MSK_SEQ_FRAME_FATAL		0x294
#define DWC_MIPI_CSI2_INT_FORCE_SEQ_FRAME_FATAL		0x298
#define DWC_MIPI_CSI2_INT_ST_CRC_FRAME_FATAL		0x2A0
#define DWC_MIPI_CSI2_INT_MSK_CRC_FRAME_FATAL		0x2A4
#define DWC_MIPI_CSI2_INT_FORCE_CRC_FRAME_FATAL		0x2A8
#define DWC_MIPI_CSI2_INT_ST_PLD_CRC_FATAL		0x2B0
#define DWC_MIPI_CSI2_INT_MSK_PLD_CRC_FATAL		0x2B4
#define DWC_MIPI_CSI2_INT_FORCE_PLD_CRC_FATAL		0x2B8
#define DWC_MIPI_CSI2_INT_ST_DATA_ID			0x2C0
#define DWC_MIPI_CSI2_INT_MSK_DATA_ID			0x2C4
#define DWC_MIPI_CSI2_INT_FORCE_DATA_ID			0x2C8
#define DWC_MIPI_CSI2_INT_ST_ECC_CORRECTED		0x2D0
#define DWC_MIPI_CSI2_INT_MSK_ECC_CORRECTED		0x2D4
#define DWC_MIPI_CSI2_INT_FORCE_ECC_CORRECTED		0x2D8
#define DWC_MIPI_CSI2_IPI_RAM_ERR_LOG_AP		0x2E0
#define DWC_MIPI_CSI2_IPI_RAM_ERR_ADDR_AP		0x2E4

#define DWC_MIPI_CSI2_SCRAMBLING			0x300
#define DWC_MIPI_CSI2_SCRAMBLING_SEED1			0x304
#define DWC_MIPI_CSI2_SCRAMBLING_SEED2			0x308

/* mediamix_GPR register */
#define DISP_MIX_CAMERA_MUX				0x30
#define DISP_MIX_CAMERA_MUX_DATA_TYPE(x)		(((x) & 0x3f) << 3)
// #define DISP_MIX_CAMERA_MUX_GASKET_ENABLE		(1 << 16)

#define DISP_MIX_CSI_REG				0x48
#define DISP_MIX_CSI_REG_CFGFREQRANGE(x)		((x)  & 0x3f)
#define DISP_MIX_CSI_REG_HSFREQRANGE(x)			(((x) & 0x7f) << 8)

#define dwc_mipi_csi2h_write(__csi2h, __r, __v) writel(__v, __csi2h->base_regs + __r)
#define dwc_mipi_csi2h_read(__csi2h, __r) readl(__csi2h->base_regs + __r)


#define SW_CPHY_EN(x)		((x) << 0)
#define SW_DSI_EN(x)		((x) << 4)
#define SW_DATATYPE_FS(x)	((x) << 8)
#define SW_DATATYPE_FE(x)	((x) << 14)
#define SW_DATATYPE_LS(x)	((x) << 20)
#define SW_DATATYPE_LE(x)	((x) << 26)

#define ES_MAX_CSI_HW		(6)

/*
 * add new chip id in tail in time order
 * by increasing to distinguish csi2 host version
 */
enum escsi2_chip_id {
	CHIP_PX30_CSI2,
	CHIP_ES1808_CSI2,
	CHIP_ES3128_CSI2,
	CHIP_ES3288_CSI2,
	CHIP_RV1126_CSI2,
	CHIP_ES3568_CSI2,
	CHIP_ES3588_CSI2,
	CHIP_RV1106_CSI2,
	CHIP_ES3562_CSI2,
};

enum csi2_pads {
	ES_CSI2_PAD_SINK = 0,
	ES_CSI2X_PAD_SOURCE0,
	ES_CSI2X_PAD_SOURCE1,
	ES_CSI2X_PAD_SOURCE2,
	ES_CSI2X_PAD_SOURCE3
};

enum csi2_err {
	ES_CSI2_ERR_SOTSYN = 0x0,
	ES_CSI2_ERR_FS_FE_MIS,
	ES_CSI2_ERR_FRM_SEQ_ERR,
	ES_CSI2_ERR_CRC_ONCE,
	ES_CSI2_ERR_CRC,
	ES_CSI2_ERR_ALL,
	ES_CSI2_ERR_MAX
};

enum host_type_t {
	ES_CSI_RXHOST,
	ES_DSI_RXHOST
};

struct csi2_match_data {
	int chip_id;
	int num_pads;
	int num_hw;
};

struct csi2_hw_match_data {
	int chip_id;
};

struct csi2_sensor_info {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

struct csi2_err_stats {
	unsigned int cnt;
};

struct csi2_dev {
	struct device		*dev;
	struct v4l2_subdev	sd;
	struct media_pad	pad[CSI2_NUM_PADS_MAX];
	struct clk_bulk_data	*clks_bulk;
	int			clks_num;
	struct reset_control	*rsts_bulk;

	void __iomem		*base;
	struct v4l2_async_notifier	notifier;
	struct v4l2_mbus_config_mipi_csi2	bus;

	/* lock to protect all members below */
	struct mutex lock;

	struct v4l2_mbus_framefmt	format_mbus;
	struct v4l2_rect	crop;
	int			stream_count;
	struct v4l2_subdev	*src_sd;
	bool			sink_linked[CSI2_NUM_SRC_PADS];
	bool			is_check_sot_sync;
	struct csi2_sensor_info	sensors[MAX_CSI2_SENSORS];
	const struct csi2_match_data	*match_data;
	int			num_sensors;
	atomic_t		frm_sync_seq;
	struct csi2_err_stats	err_list[ES_CSI2_ERR_MAX];
	struct csi2_hw		*csi2_hw[ES_MAX_CSI_HW];
	int			irq1;
	int			irq2;
	int			dsi_input_en;
	struct es_dvp2axi_csi_info	csi_info;
	const char		*dev_name;
};

struct csi2_hw {
	struct device		*dev;
	struct clk_bulk_data	*clks_bulk;
	int			clks_num;
	struct reset_control	*rsts_bulk;
	struct csi2_dev		*csi2;
	const struct csi2_hw_match_data	*match_data;

	void __iomem		*base;

	/* lock to protect all members below */
	struct mutex lock;

	int			irq1;
	int			irq2;
	const char		*dev_name;

	/* eic770x_mipi_csi2_init */
	u32 num_lanes:4;
    u32 ppi_width:2;
	u32 phy_mode:1;

    u32 vc:5;
    u32 dt:6;	
	u32 emb:1;
	u32 frame_det:1;

    u32 ipi_mode:1;
    u32 ipi_color_com:1;
    u32 ipi_auto_flush:1;
    u32 ipi_cut_through:1;
    u32 ipi_line_event;

    u32 hsa;
	u32 hbp;
	u32 hsd;
	u32 htotal;

    u32 vsa;
	u32 vbp;
	u32 vfp;
	u32 vactive;
};

u32 es_dvp2axi_csi2_get_sof(struct csi2_dev *csi2_dev);
void es_dvp2axi_csi2_set_sof(struct csi2_dev *csi2_dev, u32 seq);
void es_dvp2axi_csi2_event_inc_sof(struct csi2_dev *csi2_dev);
int es_dvp2axi_csi2_plat_drv_init(void);
void es_dvp2axi_csi2_plat_drv_exit(void);
int es_dvp2axi_csi2_hw_plat_drv_init(void);
void es_dvp2axi_csi2_hw_plat_drv_exit(void);
int es_dvp2axi_csi2_register_notifier(struct notifier_block *nb);
int es_dvp2axi_csi2_unregister_notifier(struct notifier_block *nb);
void es_dvp2axi_csi2_event_reset_pipe(struct csi2_dev *csi2_dev, int reset_src);

#endif
