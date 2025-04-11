#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <linux/reset.h>
#include <media/eswin/common-def.h>
#include "phy-eswin-csi2-dphy-common.h"

/* eic770x */
#include "../../media/platform/eswin/vi_top/vitop.h"

/* GRF REG OFFSET */
#define GRF_VI_CON0 (0x0340)
#define GRF_VI_CON1 (0x0344)

/*GRF REG BIT DEFINE */
#define GRF_CSI2PHY_LANE_SEL_SPLIT (0x1)
#define GRF_CSI2PHY_SEL_SPLIT_0_1 (0x0)
#define GRF_CSI2PHY_SEL_SPLIT_2_3 BIT(0)

/* PHY REG OFFSET */
#define CSI2_DPHY_CTRL_INVALID_OFFSET (0xffff)
#define CSI2_DPHY_CTRL_PWRCTL CSI2_DPHY_CTRL_INVALID_OFFSET
#define CSI2_DPHY_CTRL_LANE_ENABLE (0x00)
#define CSI2_DPHY_CLK1_LANE_EN (0x2C)
#define CSI2_DPHY_DUAL_CAL_EN (0x80)
#define CSI2_DPHY_CLK_INV (0X84)

#define CSI2_DPHY_CLK_CONTINUE_MODE (0x128)
#define CSI2_DPHY_CLK_WR_THS_SETTLE (0x160)
#define CSI2_DPHY_CLK_CALIB_EN (0x168)
#define CSI2_DPHY_LANE0_WR_THS_SETTLE (0x1e0)
#define CSI2_DPHY_LANE0_CALIB_EN (0x1e8)
#define CSI2_DPHY_LANE1_WR_THS_SETTLE (0x260)
#define CSI2_DPHY_LANE1_CALIB_EN (0x268)
#define CSI2_DPHY_LANE2_WR_THS_SETTLE (0x2e0)
#define CSI2_DPHY_LANE2_CALIB_EN (0x2e8)
#define CSI2_DPHY_LANE3_WR_THS_SETTLE (0x360)
#define CSI2_DPHY_LANE3_CALIB_EN (0x368)
#define CSI2_DPHY_CLK1_CONTINUE_MODE (0x3a8)
#define CSI2_DPHY_CLK1_WR_THS_SETTLE (0x3e0)
#define CSI2_DPHY_CLK1_CALIB_EN (0x3e8)

#define CSI2_DPHY_PATH0_MODE_SEL (0x44C)
#define CSI2_DPHY_PATH0_LVDS_MODE_SEL (0x480)
#define CSI2_DPHY_PATH1_MODE_SEL (0x84C)
#define CSI2_DPHY_PATH1_LVDS_MODE_SEL (0x880)

/* PHY REG BIT DEFINE */
#define CSI2_DPHY_LANE_MODE_FULL (0x4)
#define CSI2_DPHY_LANE_MODE_SPLIT (0x2)
#define CSI2_DPHY_LANE_SPLIT_TOP (0x1)
#define CSI2_DPHY_LANE_SPLIT_BOT (0x2)
#define CSI2_DPHY_LANE_SPLIT_LANE0_1 (0x3 << 2)
#define CSI2_DPHY_LANE_SPLIT_LANE2_3 (0x3 << 4)
#define CSI2_DPHY_LANE_DUAL_MODE_EN BIT(6)
#define CSI2_DPHY_LANE_PARA_ARR_NUM (0x2)

#define CSI2_DPHY_CTRL_DATALANE_ENABLE_OFFSET_BIT 2
#define CSI2_DPHY_CTRL_DATALANE_SPLIT_LANE2_3_OFFSET_BIT 4
#define CSI2_DPHY_CTRL_CLKLANE_ENABLE_OFFSET_BIT 6

enum csi2_dphy_index {
	DPHY0 = 0x0,
	DPHY1,
	DPHY2,
};

enum csi2_dphy_lane {
	CSI2_DPHY_LANE_CLOCK = 0,
	CSI2_DPHY_LANE_CLOCK1,
	CSI2_DPHY_LANE_DATA0,
	CSI2_DPHY_LANE_DATA1,
	CSI2_DPHY_LANE_DATA2,
	CSI2_DPHY_LANE_DATA3
};

enum grf_reg_id {
	GRF_DPHY_RX0_TURNDISABLE = 0,
	GRF_DPHY_RX0_FORCERXMODE,
	GRF_DPHY_RX0_FORCETXSTOPMODE,
	GRF_DPHY_RX0_ENABLE,
	GRF_DPHY_RX0_TESTCLR,
	GRF_DPHY_RX0_TESTCLK,
	GRF_DPHY_RX0_TESTEN,
	GRF_DPHY_RX0_TESTDIN,
	GRF_DPHY_RX0_TURNREQUEST,
	GRF_DPHY_RX0_TESTDOUT,
	GRF_DPHY_TX0_TURNDISABLE,
	GRF_DPHY_TX0_FORCERXMODE,
	GRF_DPHY_TX0_FORCETXSTOPMODE,
	GRF_DPHY_TX0_TURNREQUEST,
	GRF_DPHY_TX1RX1_TURNDISABLE,
	GRF_DPHY_TX1RX1_FORCERXMODE,
	GRF_DPHY_TX1RX1_FORCETXSTOPMODE,
	GRF_DPHY_TX1RX1_ENABLE,
	GRF_DPHY_TX1RX1_MASTERSLAVEZ,
	GRF_DPHY_TX1RX1_BASEDIR,
	GRF_DPHY_TX1RX1_ENABLECLK,
	GRF_DPHY_TX1RX1_TURNREQUEST,
	GRF_DPHY_RX1_SRC_SEL,
	GRF_CON_DISABLE_ISP,
	GRF_CON_ISP_DPHY_SEL,
	GRF_DSI_CSI_TESTBUS_SEL,
	GRF_DVP_V18SEL,
	GRF_DPHY_CSI2PHY_FORCERXMODE,
	GRF_DPHY_CSI2PHY_CLKLANE_EN,
	GRF_DPHY_CSI2PHY_DATALANE_EN,
	GRF_DPHY_CLK_INV_SEL,
	GRF_DPHY_SEL,
	GRF_ISP_MIPI_CSI_HOST_SEL,
	GRF_DPHY_RX0_CLK_INV_SEL,
	GRF_DPHY_RX1_CLK_INV_SEL,
	GRF_DPHY_TX1RX1_SRC_SEL,
	GRF_DPHY_CSI2PHY_CLKLANE1_EN,
	GRF_DPHY_CLK1_INV_SEL,
	GRF_DPHY_ISP_CSI2PHY_SEL,
	GRF_DPHY_DVP2AXI_CSI2PHY_SEL,
	GRF_DPHY_CSI2PHY_LANE_SEL,
	GRF_DPHY_CSI2PHY1_LANE_SEL,
	GRF_DPHY_CSI2PHY_DATALANE_EN0,
	GRF_DPHY_CSI2PHY_DATALANE_EN1,
	GRF_CPHY_MODE,
	GRF_DPHY_CSIHOST2_SEL,
	GRF_DPHY_CSIHOST3_SEL,
	GRF_DPHY_CSIHOST4_SEL,
	GRF_DPHY_CSIHOST5_SEL,
	GRF_MIPI_HOST0_SEL,
	GRF_LVDS_HOST0_SEL,
	GRF_DPHY1_CLK_INV_SEL,
	GRF_DPHY1_CLK1_INV_SEL,
	GRF_DPHY1_CSI2PHY_CLKLANE1_EN,
	GRF_DPHY1_CSI2PHY_FORCERXMODE,
	GRF_DPHY1_CSI2PHY_CLKLANE_EN,
	GRF_DPHY1_CSI2PHY_DATALANE_EN,
	GRF_DPHY1_CSI2PHY_DATALANE_EN0,
	GRF_DPHY1_CSI2PHY_DATALANE_EN1,
};

enum csi2dphy_reg_id {
	CSI2PHY_REG_CTRL_LANE_ENABLE = 0,
	CSI2PHY_CTRL_PWRCTL,
	CSI2PHY_CTRL_DIG_RST,
	CSI2PHY_CLK_THS_SETTLE,
	CSI2PHY_LANE0_THS_SETTLE,
	CSI2PHY_LANE1_THS_SETTLE,
	CSI2PHY_LANE2_THS_SETTLE,
	CSI2PHY_LANE3_THS_SETTLE,
	CSI2PHY_CLK_CALIB_ENABLE,
	CSI2PHY_LANE0_CALIB_ENABLE,
	CSI2PHY_LANE1_CALIB_ENABLE,
	CSI2PHY_LANE2_CALIB_ENABLE,
	CSI2PHY_LANE3_CALIB_ENABLE,
	CSI2PHY_MIPI_LVDS_MODEL,
	CSI2PHY_LVDS_MODE,
	CSI2PHY_DUAL_CLK_EN,
	CSI2PHY_CLK1_THS_SETTLE,
	CSI2PHY_CLK1_CALIB_ENABLE,
	CSI2PHY_CLK_LANE_ENABLE,
	CSI2PHY_CLK1_LANE_ENABLE,
	CSI2PHY_DATA_LANE0_ENABLE,
	CSI2PHY_DATA_LANE1_ENABLE,
	CSI2PHY_DATA_LANE2_ENABLE,
	CSI2PHY_DATA_LANE3_ENABLE,
	CSI2PHY_LANE0_ERR_SOT_SYNC,
	CSI2PHY_LANE1_ERR_SOT_SYNC,
	CSI2PHY_LANE2_ERR_SOT_SYNC,
	CSI2PHY_LANE3_ERR_SOT_SYNC,
	CSI2PHY_S0C_GNR_CON1,
	CSI2PHY_COMBO_S0D0_GNR_CON1,
	CSI2PHY_COMBO_S0D1_GNR_CON1,
	CSI2PHY_COMBO_S0D2_GNR_CON1,
	CSI2PHY_S0D3_GNR_CON1,
	CSI2PHY_PATH0_MODEL,
	CSI2PHY_PATH0_LVDS_MODEL,
	CSI2PHY_PATH1_MODEL,
	CSI2PHY_PATH1_LVDS_MODEL,
	CSI2PHY_CLK_INV,
	CSI2PHY_CLK_CONTINUE_MODE,
	CSI2PHY_CLK1_CONTINUE_MODE,
};

#define HIWORD_UPDATE(val, mask, shift) \
	((val) << (shift) | (mask) << ((shift) + 16))

#define GRF_REG(_offset, _width, _shift)                                     \
	{                                                                    \
		.offset = _offset, .mask = BIT(_width) - 1, .shift = _shift, \
	}

#define CSI2PHY_REG(_offset)       \
	{                          \
		.offset = _offset, \
	}

struct hsfreq_range {
	u32 range_h;
	u16 cfg_bit;
};

static inline void write_sys_grf_reg(struct csi2_dphy_hw *hw, int index,
				     u8 value)
{
	const struct grf_reg *reg = NULL;
	unsigned int val = 0;

	if (index >= hw->drv_data->num_grf_regs)
		return;

	reg = &hw->grf_regs[index];
	val = HIWORD_UPDATE(value, reg->mask, reg->shift);
	// if (reg->mask)
	// 	regmap_write(hw->regmap_sys_grf, reg->offset, val);
}

static inline void write_grf_reg(struct csi2_dphy_hw *hw, int index, u8 value)
{
	const struct grf_reg *reg = NULL;
	unsigned int val = 0;

	if (index >= hw->drv_data->num_grf_regs)
		return;

	reg = &hw->grf_regs[index];
	val = HIWORD_UPDATE(value, reg->mask, reg->shift);
	// if (reg->mask)
	// 	regmap_write(hw->regmap_grf, reg->offset, val);
}

static inline u32 read_grf_reg(struct csi2_dphy_hw *hw, int index)
{
	const struct grf_reg *reg = NULL;
	unsigned int val = 0;

	if (index >= hw->drv_data->num_grf_regs)
		return -EINVAL;

	reg = &hw->grf_regs[index];
	// if (reg->mask) {
	// 	regmap_read(hw->regmap_grf, reg->offset, &val);
	// 	val = (val >> reg->shift) & reg->mask;
	// }

	return val;
}

static inline void write_csi2_dphy_reg(struct csi2_dphy_hw *hw, int index,
				       u32 value)
{
	// const struct csi2dphy_reg *reg = NULL;

	if (index >= hw->drv_data->num_csi2dphy_regs)
		return;

	// reg = &hw->csi2dphy_regs[index];
	// if ((index == CSI2PHY_REG_CTRL_LANE_ENABLE) ||
	//     (index == CSI2PHY_CLK_LANE_ENABLE) ||
	//     (index != CSI2PHY_REG_CTRL_LANE_ENABLE && reg->offset != 0x0))
		// writel(value, hw->hw_base_addr + reg->offset);
		// DPRINTK("t1 dbg write reg:0x%lx, val:0x%x\n", (uintptr_t)hw->hw_base_addr + reg->offset, value);
}

static inline void eic770x_write_csi2_dphy_reg(struct csi2_dphy_hw *hw, u32 offset,
				       u32 value)
{
		writel(value, hw->hw_base_addr + 4 * offset);
		// pr_info("%s :dbg write reg:0x%x, val:0x%x\n", __func__, hw->hw_base_addr + 4 * offset, value);
}

static inline void write_csi2_dphy_reg_mask(struct csi2_dphy_hw *hw, int index,
					    u32 value, u32 mask)
{
	const struct csi2dphy_reg *reg = NULL;
	u32 read_val = 0;

	if (index >= hw->drv_data->num_csi2dphy_regs)
		return;

	reg = &hw->csi2dphy_regs[index];
	// read_val = readl(hw->hw_base_addr + reg->offset);
	read_val &= ~mask;
	read_val |= value;
	// writel(read_val, hw->hw_base_addr + reg->offset);
	// DPRINTK("t1 dbg write reg:0x%lx, val:0x%x\n", (uintptr_t)hw->hw_base_addr + reg->offset, read_val);
}

static inline void read_csi2_dphy_reg(struct csi2_dphy_hw *hw, int index,
				      u32 *value)
{
	const struct csi2dphy_reg *reg = NULL;

	if (index >= hw->drv_data->num_csi2dphy_regs)
		return;

	reg = &hw->csi2dphy_regs[index];
	// if ((index == CSI2PHY_REG_CTRL_LANE_ENABLE) ||
	//     (index == CSI2PHY_CLK_LANE_ENABLE) ||
	//     (index != CSI2PHY_REG_CTRL_LANE_ENABLE && reg->offset != 0x0))
		// *value = readl(hw->hw_base_addr + reg->offset);
		// DPRINTK("t1 dbg read reg:0x%lx, val:0x%x\n", (uintptr_t)hw->hw_base_addr + reg->offset, *value);
}

static const struct grf_reg eic7700_grf_dphy_regs[] = {
	[GRF_DPHY_CSI2PHY_FORCERXMODE] = GRF_REG(GRF_VI_CON0, 4, 0),
	[GRF_DPHY_CSI2PHY_DATALANE_EN] = GRF_REG(GRF_VI_CON0, 4, 4),
	[GRF_DPHY_CSI2PHY_DATALANE_EN0] = GRF_REG(GRF_VI_CON0, 2, 4),
	[GRF_DPHY_CSI2PHY_DATALANE_EN1] = GRF_REG(GRF_VI_CON0, 2, 6),
	[GRF_DPHY_CSI2PHY_CLKLANE_EN] = GRF_REG(GRF_VI_CON0, 1, 8),
	[GRF_DPHY_CLK_INV_SEL] = GRF_REG(GRF_VI_CON0, 1, 9),
	[GRF_DPHY_CSI2PHY_CLKLANE1_EN] = GRF_REG(GRF_VI_CON0, 1, 10),
	[GRF_DPHY_CLK1_INV_SEL] = GRF_REG(GRF_VI_CON0, 1, 11),
	[GRF_DPHY_ISP_CSI2PHY_SEL] = GRF_REG(GRF_VI_CON1, 1, 12),
	[GRF_DPHY_DVP2AXI_CSI2PHY_SEL] = GRF_REG(GRF_VI_CON1, 1, 11),
	[GRF_DPHY_CSI2PHY_LANE_SEL] = GRF_REG(GRF_VI_CON1, 1, 7),
};

static const struct csi2dphy_reg eic7700_csi2dphy_regs[] = {
	[CSI2PHY_REG_CTRL_LANE_ENABLE] =
		CSI2PHY_REG(CSI2_DPHY_CTRL_LANE_ENABLE),
	[CSI2PHY_DUAL_CLK_EN] = CSI2PHY_REG(CSI2_DPHY_DUAL_CAL_EN),
	[CSI2PHY_CLK_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_CLK_WR_THS_SETTLE),
	[CSI2PHY_CLK_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK_CALIB_EN),
	[CSI2PHY_LANE0_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE0_WR_THS_SETTLE),
	[CSI2PHY_LANE0_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE0_CALIB_EN),
	[CSI2PHY_LANE1_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE1_WR_THS_SETTLE),
	[CSI2PHY_LANE1_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE1_CALIB_EN),
	[CSI2PHY_LANE2_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE2_WR_THS_SETTLE),
	[CSI2PHY_LANE2_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE2_CALIB_EN),
	[CSI2PHY_LANE3_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE3_WR_THS_SETTLE),
	[CSI2PHY_LANE3_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE3_CALIB_EN),
	[CSI2PHY_CLK1_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_WR_THS_SETTLE),
	[CSI2PHY_CLK1_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_CALIB_EN),
};

/* These tables must be sorted by .range_h ascending. */
static const struct hsfreq_range eic7700_csi2_dphy_hw_hsfreq_ranges[] = {
	{ 109, 0x02 },	{ 149, 0x03 },	{ 199, 0x06 },	{ 249, 0x06 },
	{ 299, 0x06 },	{ 399, 0x08 },	{ 499, 0x0b },	{ 599, 0x0e },
	{ 699, 0x10 },	{ 799, 0x12 },	{ 999, 0x16 },	{ 1199, 0x1e },
	{ 1399, 0x23 }, { 1599, 0x2d }, { 1799, 0x32 }, { 1999, 0x37 },
	{ 2199, 0x3c }, { 2399, 0x41 }, { 2499, 0x46 }
};

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[CSI2_DPHY_RX_PAD_SINK];
	remote = media_pad_remote_pad_first(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor\n");
		return NULL;
	}

	sensor_me = media_pad_remote_pad_first(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static struct csi2_sensor *sd_to_sensor(struct csi2_dphy *dphy,
					struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < dphy->num_sensors; ++i)
		if (dphy->sensors[i].sd == sd)
			return &dphy->sensors[i];

	return NULL;
}

#if 0
static void csi2_dphy_config_dual_mode(struct csi2_dphy *dphy,
				       struct csi2_sensor *sensor)
{
	struct csi2_dphy_hw *hw = dphy->dphy_hw;
	struct v4l2_subdev *sd = &dphy->sd;
	bool is_lvds = false;
	char *model;
	u32 val;

	model = sd->v4l2_dev->mdev->model;
	if (!strncmp(model, "es_mipi_lvds", sizeof("es_mipi_lvds") - 1))
		is_lvds = true;
	else
		is_lvds = false;

	if (hw->lane_mode == LANE_MODE_FULL) {
		val = !GRF_CSI2PHY_LANE_SEL_SPLIT;
		if (dphy->phy_index < 3) {
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN,
				      GENMASK(sensor->lanes - 1, 0));
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE_EN, 0x1);
			if (hw->drv_data->chip_id != CHIP_ID_EIC7700)
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL,
					      val);
			else
				write_sys_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL,
						  val);
		} else {
			if (hw->drv_data->chip_id <= CHIP_ID_EIC7700) {
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN,
					      GENMASK(sensor->lanes - 1, 0));
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE_EN,
					      0x1);
			} else {
				write_grf_reg(hw, GRF_DPHY1_CSI2PHY_DATALANE_EN,
					      GENMASK(sensor->lanes - 1, 0));
				write_grf_reg(hw, GRF_DPHY1_CSI2PHY_CLKLANE_EN,
					      0x1);
			}
			if (hw->drv_data->chip_id != CHIP_ID_EIC7700)
				write_grf_reg(hw, GRF_DPHY_CSI2PHY1_LANE_SEL,
					      val);
			else
				write_sys_grf_reg(
					hw, GRF_DPHY_CSI2PHY1_LANE_SEL, val);
		}
	} else {
		val = GRF_CSI2PHY_LANE_SEL_SPLIT;

		switch (dphy->phy_index) {
		case 1:
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN0,
				      GENMASK(sensor->lanes - 1, 0));
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE_EN, 0x1);
			if (hw->drv_data->chip_id < CHIP_ID_EIC7700) {
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL,
					      val);
				if (is_lvds)
					write_grf_reg(
						hw, GRF_DPHY_DVP2AXI_CSI2PHY_SEL,
						GRF_CSI2PHY_SEL_SPLIT_0_1);
				else
					write_grf_reg(
						hw, GRF_DPHY_ISP_CSI2PHY_SEL,
						GRF_CSI2PHY_SEL_SPLIT_0_1);
			} else if (hw->drv_data->chip_id == CHIP_ID_EIC7700) {
				write_sys_grf_reg(hw, GRF_DPHY_CSIHOST2_SEL,
						  0x0);
				write_sys_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL,
						  val);
			} else if (hw->drv_data->chip_id == CHIP_ID_EIC7700) {
				if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY)
					write_grf_reg(hw, GRF_MIPI_HOST0_SEL,
						      0x1);
				else
					write_grf_reg(hw, GRF_LVDS_HOST0_SEL,
						      0x1);
			} else if (hw->drv_data->chip_id == CHIP_ID_EIC7700) {
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL,
					      val);
			}
			break;
		case 2:
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN1,
				      GENMASK(sensor->lanes - 1, 0));
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE1_EN, 0x1);
			if (hw->drv_data->chip_id < CHIP_ID_EIC7700) {
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL,
					      val);
				if (is_lvds)
					write_grf_reg(
						hw, GRF_DPHY_DVP2AXI_CSI2PHY_SEL,
						GRF_CSI2PHY_SEL_SPLIT_2_3);
				else
					write_grf_reg(
						hw, GRF_DPHY_ISP_CSI2PHY_SEL,
						GRF_CSI2PHY_SEL_SPLIT_2_3);
			} else if (hw->drv_data->chip_id == CHIP_ID_EIC7700) {
				write_sys_grf_reg(hw, GRF_DPHY_CSIHOST3_SEL,
						  0x1);
				write_sys_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL,
						  val);
			}
			break;
		case 4:
			if (hw->drv_data->chip_id == CHIP_ID_EIC7700) {
				write_sys_grf_reg(
					hw, GRF_DPHY_CSI2PHY1_LANE_SEL, val);
				write_sys_grf_reg(hw, GRF_DPHY_CSIHOST4_SEL,
						  0x0);
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN0,
					      GENMASK(sensor->lanes - 1, 0));
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE_EN,
					      0x1);
			}
			break;
		case 5:
			if (hw->drv_data->chip_id == CHIP_ID_EIC7700) {
				write_sys_grf_reg(
					hw, GRF_DPHY_CSI2PHY1_LANE_SEL, val);
				write_sys_grf_reg(hw, GRF_DPHY_CSIHOST5_SEL,
						  0x1);
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN1,
					      GENMASK(sensor->lanes - 1, 0));
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE1_EN,
					      0x1);
			} 
			break;
		default:
			break;
		};
	}
}

#endif


static int eic770x_csi2_dphy_init(struct csi2_dphy_hw *dphy_hw)
{

	struct csi2_dphy_hw *hw = dphy_hw;
	dev_info(dphy_hw->dev, "start phy init \n");

	// 400Mbps
#ifndef CONFIG_EIC7700_EVB_VI
	dev_info(dphy_hw->dev, "config evb dphy \n");

	eic770x_write_csi2_dphy_reg(hw, 0xc10, 0x30);
	eic770x_write_csi2_dphy_reg(hw, 0x1cf2, 0x444);
	eic770x_write_csi2_dphy_reg(hw, 0x1cf2, 0x1444);
	eic770x_write_csi2_dphy_reg(hw, 0x1cf0, 0x1bfd);
	eic770x_write_csi2_dphy_reg(hw, 0xc11, 0x233);
	eic770x_write_csi2_dphy_reg(hw, 0xc06, 0x27);
	eic770x_write_csi2_dphy_reg(hw, 0xc26, 0x1f4);
	eic770x_write_csi2_dphy_reg(hw, 0xe02, 0x320);
	eic770x_write_csi2_dphy_reg(hw, 0xe03, 0x1b);
	eic770x_write_csi2_dphy_reg(hw, 0xe05, 0xfec8);

	eic770x_write_csi2_dphy_reg(hw, 0xe06, 0x646e);
	eic770x_write_csi2_dphy_reg(hw, 0xe06, 0x646e);
	eic770x_write_csi2_dphy_reg(hw, 0xe06, 0x646e);
	eic770x_write_csi2_dphy_reg(hw, 0xe08, 0x105);
	eic770x_write_csi2_dphy_reg(hw, 0xe36, 0x3);
	eic770x_write_csi2_dphy_reg(hw, 0xc02, 0x5);
	eic770x_write_csi2_dphy_reg(hw, 0xe40, 0x17);
	eic770x_write_csi2_dphy_reg(hw, 0xe50, 0x4);
	eic770x_write_csi2_dphy_reg(hw, 0xe01, 0x5f);
	eic770x_write_csi2_dphy_reg(hw, 0xe05, 0xfe1d);

	eic770x_write_csi2_dphy_reg(hw, 0xe06, 0xeee);
	eic770x_write_csi2_dphy_reg(hw, 0x1c20, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x1c21, 0x400);
	eic770x_write_csi2_dphy_reg(hw, 0x1c21, 0x400);
	eic770x_write_csi2_dphy_reg(hw, 0x1c23, 0x41f6);
	eic770x_write_csi2_dphy_reg(hw, 0x1c20, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x1c23, 0x43f6);
	eic770x_write_csi2_dphy_reg(hw, 0x1c26, 0x2000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c27, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x1c26, 0x3000);

	eic770x_write_csi2_dphy_reg(hw, 0x1c27, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x1c26, 0x7000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c27, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x1c25, 0x4000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c40, 0xf4);
	eic770x_write_csi2_dphy_reg(hw, 0x1c40, 0xf4);
	eic770x_write_csi2_dphy_reg(hw, 0x1c47, 0x14);
	eic770x_write_csi2_dphy_reg(hw, 0x1c47, 0x10);
	eic770x_write_csi2_dphy_reg(hw, 0x1c47, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0xc08, 0x50);

	eic770x_write_csi2_dphy_reg(hw, 0xc07, 0x68);
	eic770x_write_csi2_dphy_reg(hw, 0x3040, 0x473c);///oops
	eic770x_write_csi2_dphy_reg(hw, 0x3240, 0x473c);
	eic770x_write_csi2_dphy_reg(hw, 0x1022, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x1222, 0x1);
	eic770x_write_csi2_dphy_reg(hw, 0x1422, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x1c46, 0x9);
	eic770x_write_csi2_dphy_reg(hw, 0x1c46, 0x9);
	eic770x_write_csi2_dphy_reg(hw, 0x102c, 0x802);
	eic770x_write_csi2_dphy_reg(hw, 0x122c, 0x802);

	eic770x_write_csi2_dphy_reg(hw, 0x142c, 0x802);
	eic770x_write_csi2_dphy_reg(hw, 0x102d, 0x2);
	eic770x_write_csi2_dphy_reg(hw, 0x122d, 0x2);
	eic770x_write_csi2_dphy_reg(hw, 0x142d, 0x2);
	eic770x_write_csi2_dphy_reg(hw, 0x102c, 0x802);
	eic770x_write_csi2_dphy_reg(hw, 0x122c, 0x802);
	eic770x_write_csi2_dphy_reg(hw, 0x142c, 0x802);
	eic770x_write_csi2_dphy_reg(hw, 0x102d, 0xa);
	eic770x_write_csi2_dphy_reg(hw, 0x122d, 0xa);
	eic770x_write_csi2_dphy_reg(hw, 0x142d, 0xa);

	eic770x_write_csi2_dphy_reg(hw, 0x1229, 0xa70);
	eic770x_write_csi2_dphy_reg(hw, 0x102a, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x122a, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x142a, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x102f, 0x4);
	eic770x_write_csi2_dphy_reg(hw, 0x122f, 0x4);
	eic770x_write_csi2_dphy_reg(hw, 0x142f, 0x4);
	eic770x_write_csi2_dphy_reg(hw, 0x3880, 0x91c);
	eic770x_write_csi2_dphy_reg(hw, 0x3887, 0x3b06);
	eic770x_write_csi2_dphy_reg(hw, 0x3080, 0xe1d);

	eic770x_write_csi2_dphy_reg(hw, 0x3280, 0xe1d);
	eic770x_write_csi2_dphy_reg(hw, 0x3001, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x3201, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x3001, 0x8);
	eic770x_write_csi2_dphy_reg(hw, 0x3201, 0x8);
	eic770x_write_csi2_dphy_reg(hw, 0x3082, 0xe69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3282, 0xe69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3040, 0x173c);
	eic770x_write_csi2_dphy_reg(hw, 0x3240, 0x173c);
	eic770x_write_csi2_dphy_reg(hw, 0x3042, 0x0);

	eic770x_write_csi2_dphy_reg(hw, 0x3242, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x3840, 0x163c);
	eic770x_write_csi2_dphy_reg(hw, 0x3842, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x3082, 0xe69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3282, 0xe69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3081, 0x4010);
	eic770x_write_csi2_dphy_reg(hw, 0x3281, 0x4010);
	eic770x_write_csi2_dphy_reg(hw, 0x3082, 0xe69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3282, 0xe69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3083, 0x9209);

	eic770x_write_csi2_dphy_reg(hw, 0x3283, 0x9209);
	eic770x_write_csi2_dphy_reg(hw, 0x3084, 0x96);
	eic770x_write_csi2_dphy_reg(hw, 0x3284, 0x96);
	eic770x_write_csi2_dphy_reg(hw, 0x3085, 0x100);
	eic770x_write_csi2_dphy_reg(hw, 0x3285, 0x100);
	eic770x_write_csi2_dphy_reg(hw, 0x3085, 0x100);
	eic770x_write_csi2_dphy_reg(hw, 0x3285, 0x100);
	eic770x_write_csi2_dphy_reg(hw, 0x3086, 0x2d02);
	eic770x_write_csi2_dphy_reg(hw, 0x3086, 0x2d02);
	eic770x_write_csi2_dphy_reg(hw, 0x3087, 0x1b06);

	eic770x_write_csi2_dphy_reg(hw, 0x3087, 0x1b06);
	eic770x_write_csi2_dphy_reg(hw, 0x3087, 0x1b06);
	eic770x_write_csi2_dphy_reg(hw, 0x3087, 0x1b06);
	eic770x_write_csi2_dphy_reg(hw, 0x3083, 0x9201);
	eic770x_write_csi2_dphy_reg(hw, 0x3283, 0x9201);
	eic770x_write_csi2_dphy_reg(hw, 0x3089, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x3289, 0x0);
	eic770x_write_csi2_dphy_reg(hw, 0x3086, 0x2);
	eic770x_write_csi2_dphy_reg(hw, 0x3286, 0x2);

#else
	dev_info(dphy_hw->dev, "config dvb dphy \n");
	// eic770x_write_csi2_dphy_reg(hw, 0xc10       , 0x30  ) ;//PPI_STARTUP_RW_COMMON_DPHY_10
    eic770x_write_csi2_dphy_reg(hw, 0x1cf2      , 0x444 ) ;//CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_2
    eic770x_write_csi2_dphy_reg(hw, 0x1cf2      , 0x1444) ;//CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_2
    eic770x_write_csi2_dphy_reg(hw, 0x1cf0      , 0x1bfd) ;//CORE_DIG_ANACTRL_RW_COMMON_ANACTRL_0
    eic770x_write_csi2_dphy_reg(hw, 0xc11       , 0x233 ) ;//PPI_STARTUP_RW_COMMON_STARTUP_1_1
    eic770x_write_csi2_dphy_reg(hw, 0xc06       , 0x27  ) ;//PPI_STARTUP_RW_COMMON_DPHY_6
    eic770x_write_csi2_dphy_reg(hw, 0xc26       , 0x1f4 ) ;//PPI_CALIBCTRL_RW_COMMON_BG_0
    eic770x_write_csi2_dphy_reg(hw, 0xe02       , 0x320 ) ;//PPI_RW_LPDCOCAL_NREF
    eic770x_write_csi2_dphy_reg(hw, 0xe03       , 0x1b  ) ;//PPI_RW_LPDCOCAL_NREF_RANGE
    eic770x_write_csi2_dphy_reg(hw, 0xe05       , 0xfec8) ;//PPI_RW_LPDCOCAL_TWAIT_CONFIG
	
    eic770x_write_csi2_dphy_reg(hw, 0xe06       , 0x646e) ;//PPI_RW_LPDCOCAL_VT_CONFIG
    eic770x_write_csi2_dphy_reg(hw, 0xe06       , 0x646e) ;//PPI_RW_LPDCOCAL_VT_CONFIG
    eic770x_write_csi2_dphy_reg(hw, 0xe06       , 0x646e) ;//PPI_RW_LPDCOCAL_VT_CONFIG
    eic770x_write_csi2_dphy_reg(hw, 0xe08       , 0x105 ) ;//PPI_RW_LPDCOCAL_COARSE_CFG
    eic770x_write_csi2_dphy_reg(hw, 0xe36       , 0x3   ) ;//PPI_RW_COMMON_CFG
    eic770x_write_csi2_dphy_reg(hw, 0xc02       , 0x5   ) ;//PPI_STARTUP_RW_COMMON_DPHY_2
    eic770x_write_csi2_dphy_reg(hw, 0xe40       , 0x17  ) ;//PPI_RW_TERMCAL_CFG_0
    eic770x_write_csi2_dphy_reg(hw, 0xe50       , 0x4   ) ;//PPI_RW_OFFSETCAL_CFG_0
    eic770x_write_csi2_dphy_reg(hw, 0xe01       , 0x5f  ) ;//PPI_RW_LPDCOCAL_TIMEBASE
    eic770x_write_csi2_dphy_reg(hw, 0xe05       , 0xfe1d) ;//PPI_RW_LPDCOCAL_TWAIT_CONFIG
	
    eic770x_write_csi2_dphy_reg(hw, 0xe06       , 0xeee ) ;//PPI_RW_LPDCOCAL_VT_CONFIG
    eic770x_write_csi2_dphy_reg(hw, 0x1c20      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_0
    eic770x_write_csi2_dphy_reg(hw, 0x1c21      , 0x400 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_1
    eic770x_write_csi2_dphy_reg(hw, 0x1c21      , 0x400 ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_1
    eic770x_write_csi2_dphy_reg(hw, 0x1c23      , 0x41f6) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_3
    eic770x_write_csi2_dphy_reg(hw, 0x1c20      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_0
    eic770x_write_csi2_dphy_reg(hw, 0x1c23      , 0x43f6) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_3
    eic770x_write_csi2_dphy_reg(hw, 0x1c26      , 0x2000) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6
    eic770x_write_csi2_dphy_reg(hw, 0x1c27      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7
    eic770x_write_csi2_dphy_reg(hw, 0x1c26      , 0x3000) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6
	
    eic770x_write_csi2_dphy_reg(hw, 0x1c27      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7
    eic770x_write_csi2_dphy_reg(hw, 0x1c26      , 0x7000) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_6
    eic770x_write_csi2_dphy_reg(hw, 0x1c27      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_7
    eic770x_write_csi2_dphy_reg(hw, 0x1c25      , 0x4000) ;//CORE_DIG_IOCTRL_RW_AFE_CB_CTRL_2_5
    eic770x_write_csi2_dphy_reg(hw, 0x1c40      , 0xf4  ) ;//CORE_DIG_RW_COMMON_0
    eic770x_write_csi2_dphy_reg(hw, 0x1c40      , 0xf4  ) ;//CORE_DIG_RW_COMMON_0
    eic770x_write_csi2_dphy_reg(hw, 0x1c47      , 0x14  ) ;//CORE_DIG_RW_COMMON_7
    eic770x_write_csi2_dphy_reg(hw, 0x1c47      , 0x10  ) ;//CORE_DIG_RW_COMMON_7
    eic770x_write_csi2_dphy_reg(hw, 0x1c47      , 0x0   ) ;//CORE_DIG_RW_COMMON_7
    eic770x_write_csi2_dphy_reg(hw, 0xc08       , 0x50  ) ;//PPI_STARTUP_RW_COMMON_DPHY_8
	
    eic770x_write_csi2_dphy_reg(hw, 0xc07       , 0x68  ) ;//PPI_STARTUP_RW_COMMON_DPHY_7
    eic770x_write_csi2_dphy_reg(hw, 0x3040      , 0x473c) ;//CORE_DIG_DLANE_0_RW_LP_0
    eic770x_write_csi2_dphy_reg(hw, 0x3240      , 0x473c) ;//CORE_DIG_DLANE_1_RW_LP_0
    eic770x_write_csi2_dphy_reg(hw, 0x1022      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_2
    eic770x_write_csi2_dphy_reg(hw, 0x1222      , 0x1   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_2
    eic770x_write_csi2_dphy_reg(hw, 0x1422      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_2
    eic770x_write_csi2_dphy_reg(hw, 0x1c46      , 0x9   ) ;//CORE_DIG_RW_COMMON_6
    eic770x_write_csi2_dphy_reg(hw, 0x1c46      , 0x9   ) ;//CORE_DIG_RW_COMMON_6
    eic770x_write_csi2_dphy_reg(hw, 0x102c      , 0x802 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_12
    eic770x_write_csi2_dphy_reg(hw, 0x122c      , 0x802 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_12
	
    eic770x_write_csi2_dphy_reg(hw, 0x142c      , 0x802 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_12
    eic770x_write_csi2_dphy_reg(hw, 0x102d      , 0x2   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_13
    eic770x_write_csi2_dphy_reg(hw, 0x122d      , 0x2   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_13
    eic770x_write_csi2_dphy_reg(hw, 0x142d      , 0x2   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_13
    eic770x_write_csi2_dphy_reg(hw, 0x102c      , 0x802 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_12
    eic770x_write_csi2_dphy_reg(hw, 0x122c      , 0x802 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_12
    eic770x_write_csi2_dphy_reg(hw, 0x142c      , 0x802 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_12
    eic770x_write_csi2_dphy_reg(hw, 0x102d      , 0xa   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_13
    eic770x_write_csi2_dphy_reg(hw, 0x122d      , 0xa   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_13
    eic770x_write_csi2_dphy_reg(hw, 0x142d      , 0xa   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_13
	
    eic770x_write_csi2_dphy_reg(hw, 0x1229      , 0xa70 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
    eic770x_write_csi2_dphy_reg(hw, 0x102a      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_10
    eic770x_write_csi2_dphy_reg(hw, 0x122a      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_10
    eic770x_write_csi2_dphy_reg(hw, 0x142a      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_10
    eic770x_write_csi2_dphy_reg(hw, 0x102f      , 0x4   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_15
    eic770x_write_csi2_dphy_reg(hw, 0x122f      , 0x4   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_15
    eic770x_write_csi2_dphy_reg(hw, 0x142f      , 0x4   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_15
    eic770x_write_csi2_dphy_reg(hw, 0x3880      , 0x91c ) ;//CORE_DIG_DLANE_CLK_RW_HS_RX_0
    eic770x_write_csi2_dphy_reg(hw, 0x3887      , 0x3b06) ;//CORE_DIG_DLANE_CLK_RW_HS_RX_7
    eic770x_write_csi2_dphy_reg(hw, 0x3080      , 0xe1d ) ;//CORE_DIG_DLANE_0_RW_HS_RX_0
	
    eic770x_write_csi2_dphy_reg(hw, 0x3280      , 0xe1d ) ;//CORE_DIG_DLANE_1_RW_HS_RX_0
    eic770x_write_csi2_dphy_reg(hw, 0x3001      , 0x0   ) ;//CORE_DIG_DLANE_0_RW_CFG_1
    eic770x_write_csi2_dphy_reg(hw, 0x3201      , 0x0   ) ;//CORE_DIG_DLANE_1_RW_CFG_1
    eic770x_write_csi2_dphy_reg(hw, 0x3001      , 0x8   ) ;//CORE_DIG_DLANE_0_RW_CFG_1
    eic770x_write_csi2_dphy_reg(hw, 0x3201      , 0x8   ) ;//CORE_DIG_DLANE_1_RW_CFG_1
    eic770x_write_csi2_dphy_reg(hw, 0x3082      , 0xe69b) ;//CORE_DIG_DLANE_0_RW_HS_RX_2
    eic770x_write_csi2_dphy_reg(hw, 0x3282      , 0xe69b) ;//CORE_DIG_DLANE_1_RW_HS_RX_2
    eic770x_write_csi2_dphy_reg(hw, 0x3040      , 0x173c) ;//CORE_DIG_DLANE_0_RW_LP_0
    eic770x_write_csi2_dphy_reg(hw, 0x3240      , 0x173c) ;//CORE_DIG_DLANE_1_RW_LP_0
    eic770x_write_csi2_dphy_reg(hw, 0x3042      , 0x0   ) ;//CORE_DIG_DLANE_0_RW_LP_2
	
    eic770x_write_csi2_dphy_reg(hw, 0x3242      , 0x0   ) ;//CORE_DIG_DLANE_1_RW_LP_2
    eic770x_write_csi2_dphy_reg(hw, 0x3840      , 0x163c) ;//CORE_DIG_DLANE_CLK_RW_LP_0
    eic770x_write_csi2_dphy_reg(hw, 0x3842      , 0x0   ) ;//CORE_DIG_DLANE_CLK_RW_LP_2
    eic770x_write_csi2_dphy_reg(hw, 0x3082      , 0xe69b) ;//CORE_DIG_DLANE_0_RW_HS_RX_2
    eic770x_write_csi2_dphy_reg(hw, 0x3282      , 0xe69b) ;//CORE_DIG_DLANE_1_RW_HS_RX_2
    eic770x_write_csi2_dphy_reg(hw, 0x3081      , 0x4010) ;//CORE_DIG_DLANE_0_RW_HS_RX_1
    eic770x_write_csi2_dphy_reg(hw, 0x3281      , 0x4010) ;//CORE_DIG_DLANE_1_RW_HS_RX_1
    eic770x_write_csi2_dphy_reg(hw, 0x3082      , 0xe69b) ;//CORE_DIG_DLANE_0_RW_HS_RX_2
    eic770x_write_csi2_dphy_reg(hw, 0x3282      , 0xe69b) ;//CORE_DIG_DLANE_1_RW_HS_RX_2
    eic770x_write_csi2_dphy_reg(hw, 0x3083      , 0x9209) ;//CORE_DIG_DLANE_0_RW_HS_RX_3
	
    eic770x_write_csi2_dphy_reg(hw, 0x3283      , 0x9209) ;//CORE_DIG_DLANE_1_RW_HS_RX_3
    eic770x_write_csi2_dphy_reg(hw, 0x3084      , 0x96  ) ;//CORE_DIG_DLANE_0_RW_HS_RX_4
    eic770x_write_csi2_dphy_reg(hw, 0x3284      , 0x96  ) ;//CORE_DIG_DLANE_1_RW_HS_RX_4
    eic770x_write_csi2_dphy_reg(hw, 0x3085      , 0x100 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_5
    eic770x_write_csi2_dphy_reg(hw, 0x3285      , 0x100 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_5
    eic770x_write_csi2_dphy_reg(hw, 0x3085      , 0x100 ) ;//CORE_DIG_DLANE_0_RW_HS_RX_5
    eic770x_write_csi2_dphy_reg(hw, 0x3285      , 0x100 ) ;//CORE_DIG_DLANE_1_RW_HS_RX_5
    eic770x_write_csi2_dphy_reg(hw, 0x3086      , 0x2d02) ;//CORE_DIG_DLANE_0_RW_HS_RX_6
    eic770x_write_csi2_dphy_reg(hw, 0x3286      , 0x2d02) ;//CORE_DIG_DLANE_1_RW_HS_RX_6
    eic770x_write_csi2_dphy_reg(hw, 0x3087      , 0x1b06) ;//CORE_DIG_DLANE_0_RW_HS_RX_7
	
    eic770x_write_csi2_dphy_reg(hw, 0x3287      , 0x1b06) ;//CORE_DIG_DLANE_1_RW_HS_RX_7
    eic770x_write_csi2_dphy_reg(hw, 0x3087      , 0x1b06) ;//CORE_DIG_DLANE_0_RW_HS_RX_7
    eic770x_write_csi2_dphy_reg(hw, 0x3287      , 0x1b06) ;//CORE_DIG_DLANE_1_RW_HS_RX_7
    eic770x_write_csi2_dphy_reg(hw, 0x3083      , 0x9201) ;//CORE_DIG_DLANE_0_RW_HS_RX_3
    eic770x_write_csi2_dphy_reg(hw, 0x3283      , 0x9201) ;//CORE_DIG_DLANE_1_RW_HS_RX_3
    eic770x_write_csi2_dphy_reg(hw, 0x3089      , 0x0   ) ;//CORE_DIG_DLANE_0_RW_HS_RX_9
    eic770x_write_csi2_dphy_reg(hw, 0x3289      , 0x0   ) ;//CORE_DIG_DLANE_1_RW_HS_RX_9
    eic770x_write_csi2_dphy_reg(hw, 0x3086      , 0x2   ) ;//CORE_DIG_DLANE_0_RW_HS_RX_6
    eic770x_write_csi2_dphy_reg(hw, 0x3286      , 0x2   ) ;//CORE_DIG_DLANE_1_RW_HS_RX_6
	
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x404 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x40c ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x414 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x41c ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x423 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x429 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x430 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x43a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x445 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x44a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x450 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x45a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x465 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x469 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x472 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x47a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x485 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x489 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x490 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x49a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4a4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4ac ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4b4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4bc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4c4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4cc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4d4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4dc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4e4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4ec ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4f4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x4fc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x504 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x50c ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x514 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x51c ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x523 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x529 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x530 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x53a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x545 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x54a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x550 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x55a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x565 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x569 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x572 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x57a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x585 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x589 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x590 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x59a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5a4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5ac ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5b4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5bc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5c4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5cc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5d4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5dc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5e4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5ec ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5f4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x5fc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x604 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x60c ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x614 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x61c ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x623 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x629 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x632 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x63a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x645 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x64a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x650 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x65a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x665 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x669 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x672 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x67a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x685 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x689 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x690 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x69a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6a4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6ac ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6b4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6bc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6c4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6cc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6d4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6dc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6e4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6ec ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6f4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x6fc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x704 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x70c ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x714 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x71c ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x723 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x72a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x730 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x73a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x745 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x74a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x750 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x75a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x765 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x769 ) ;//CORE_DIG_CO:MMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x772 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x77a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x785 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x789 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x790 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x79a ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7a4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7ac ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7b4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7bc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7c4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7cc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7d4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7dc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7e4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7ec ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7f4 ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    eic770x_write_csi2_dphy_reg(hw, 0x1ff0      , 0x7fc ) ;//CORE_DIG_COMMON_RW_DESKEW_FINE_MEM
    
    eic770x_write_csi2_dphy_reg(hw, 0x3800      , 0x3   ) ;//CORE_DIG_DLANE_CLK_RW_CFG_0
    eic770x_write_csi2_dphy_reg(hw, 0x3800      , 0x3   ) ;//CORE_DIG_DLANE_CLK_RW_CFG_0
    eic770x_write_csi2_dphy_reg(hw, 0x3000      , 0x3   ) ;//CORE_DIG_DLANE_0_RW_CFG_0
    eic770x_write_csi2_dphy_reg(hw, 0x3200      , 0x3   ) ;//CORE_DIG_DLANE_1_RW_CFG_0
    eic770x_write_csi2_dphy_reg(hw, 0x3000      , 0x3   ) ;//CORE_DIG_DLANE_0_RW_CFG_0
    eic770x_write_csi2_dphy_reg(hw, 0x3200      , 0x3   ) ;//CORE_DIG_DLANE_1_RW_CFG_0

    eic770x_write_csi2_dphy_reg(hw, 0x1029      , 0xbf0 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
    eic770x_write_csi2_dphy_reg(hw, 0x1229      , 0xb70 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
    eic770x_write_csi2_dphy_reg(hw, 0x1429      , 0xbf0 ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9

#endif
	return 0;
}

int eic770x_csi0_cfg(struct csi2_dphy_hw *dphy_hw )
{
#ifdef CONFIG_EIC7700_EVB_VI
	writel(1, dphy_hw->csi_base_addr + 0x18);
#else
	writel(0, dphy_hw->csi_base_addr + 0x18);
#endif
	writel(1, dphy_hw->csi_base_addr + 0x40);  //phy shutdownz
	writel(1, dphy_hw->csi_base_addr + 0x44); //phy reset
	writel(0x1010100, dphy_hw->csi_base_addr + 0x80); 
	writel(0x2b, dphy_hw->csi_base_addr + 0x88); 
	writel(1, dphy_hw->csi_base_addr + 0x8); 

	return 0;
}

//TODO need to update the function
static int csi2_dphy_hw_stream_on(struct csi2_dphy *dphy,
				  struct v4l2_subdev *sd)
{
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor;
	struct csi2_dphy_hw *hw = dphy->dphy_hw;
	u32 phy_ready = 0;

	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(dphy, sensor_sd);
	if (!sensor)
		return -ENODEV;

	mutex_lock(&hw->mutex);

	dev_info(hw->dev, "stream on\n");
	dev_info(hw->dev, "%s:%d: dphy phy addr 0x%llx \n", __func__, __LINE__, hw->dphy_hw_phy_addr);
	#ifdef CONFIG_EIC7700_EVB_VI
	writel(0x2c3f5, hw->hw_base_phy0_addr + 0x0);
	#else
    writel(0x2c3f0, hw->hw_base_phy0_addr + 0x0); 
	#endif

	eic770x_csi2_dphy_init(hw);
	udelay(2000);
	eic770x_csi0_cfg(hw);
	int count = 20;
	while (count-- && phy_ready != 0x3) {
		//TODO
		phy_ready = readl(hw->hw_base_phy0_addr + 0x4); 
        dev_info(hw->dev, "0x%x: csi phy status 0x%x\n", hw->phy_cfg_addr, phy_ready);
        udelay(200000);
    }
	#ifdef CONFIG_EIC7700_EVB_VI
	writel(0x2c075, hw->hw_base_phy0_addr + 0x0);
	#else
    writel(0x2c070, hw->hw_base_phy0_addr + 0x0);
	#endif
	
	atomic_inc(&hw->stream_cnt);

	mutex_unlock(&hw->mutex);

	return 0;
}

//TODO need check stream off config for dwc dphy
static int csi2_dphy_hw_stream_off(struct csi2_dphy *dphy,
				   struct v4l2_subdev *sd)
{
	struct csi2_dphy_hw *hw = dphy->dphy_hw;

	if (atomic_dec_return(&hw->stream_cnt))
		return 0;

	mutex_lock(&hw->mutex);
	//dphy off and reset by csi2
	writel(0, hw->csi_base_addr + 0x44); //phy reset
	writel(0, hw->csi_base_addr + 0x40); //phy shutdownz

	mutex_unlock(&hw->mutex);

	return 0;
}

//TODO update the function to quick poweron dphy
static int csi2_dphy_hw_quick_stream_on(struct csi2_dphy *dphy,
					struct v4l2_subdev *sd)
{
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor;

	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(dphy, sensor_sd);
	if (!sensor)
		return -ENODEV;

	//TODO
	
	return 0;
}

static int csi2_dphy_hw_quick_stream_off(struct csi2_dphy *dphy,
					 struct v4l2_subdev *sd)
{
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor;

	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(dphy, sensor_sd);
	if (!sensor)
		return -ENODEV;

	//TODO
	return 0;
}

static void eic7700_csi2_dphy_hw_individual_init(struct csi2_dphy_hw *hw)
{
	hw->grf_regs = eic7700_grf_dphy_regs;
}

static const struct dphy_hw_drv_data eswin_csi2_dphy_hw_drv_data = {
	.hsfreq_ranges = eic7700_csi2_dphy_hw_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(eic7700_csi2_dphy_hw_hsfreq_ranges),
	.csi2dphy_regs = eic7700_csi2dphy_regs,
	.num_csi2dphy_regs = ARRAY_SIZE(eic7700_csi2dphy_regs),
	.grf_regs = eic7700_grf_dphy_regs,
	.num_grf_regs = ARRAY_SIZE(eic7700_grf_dphy_regs),
	.individual_init = eic7700_csi2_dphy_hw_individual_init,
	.chip_id = CHIP_ID_EIC7700,
	.stream_on = csi2_dphy_hw_stream_on,
	.stream_off = csi2_dphy_hw_stream_off,
};

static const struct of_device_id eswin_csi2_dphy_hw_match_id[] = {
	{
		.compatible = "eswin,eic770x-csi2-dphy-hw",
		.data = &eswin_csi2_dphy_hw_drv_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, eswin_csi2_dphy_hw_match_id);


static int eswin_csi2_dphy_hw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct csi2_dphy_hw *dphy_hw;
	struct resource *res;
	const struct of_device_id *of_id;
	const struct dphy_hw_drv_data *drv_data;
	u32 ret;
	
	dev_info(dev, "csi2 dphy hw probe in!\n");

	dphy_hw = devm_kzalloc(dev, sizeof(*dphy_hw), GFP_KERNEL);
	if (!dphy_hw)
		return -ENOMEM;
	dphy_hw->dev = dev;

	of_id = of_match_device(eswin_csi2_dphy_hw_match_id, dev);
	if (!of_id)
		return -EINVAL;

	drv_data = of_id->data;

	dphy_hw->dphy_dev_num = 0;
	dphy_hw->drv_data = drv_data;
	dphy_hw->lane_mode = LANE_MODE_UNDEF;
	dphy_hw->grf_regs = drv_data->grf_regs;
	dphy_hw->csi2dphy_regs = drv_data->csi2dphy_regs;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dphy_hw->hw_base_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(dphy_hw->hw_base_addr)) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);
		dphy_hw->hw_base_addr = devm_ioremap(dev, offset, size);
		if (IS_ERR(dphy_hw->hw_base_addr)) {
			dev_err(dev, "Can't find csi2 dphy hw addr!\n");
			return -ENODEV;
		}
	}
	dphy_hw->dphy_hw_phy_addr = res->start;

	dphy_hw->stream_on = drv_data->stream_on;
	dphy_hw->stream_off = drv_data->stream_off;
	dphy_hw->quick_stream_on = csi2_dphy_hw_quick_stream_on;
	dphy_hw->quick_stream_off = csi2_dphy_hw_quick_stream_off;
	
	ret = of_property_read_u32(dev->of_node, "phy_cfg", &dphy_hw->phy_cfg_addr);
    if (ret) {
        dev_err(dev, "Failed to read phy_cfg address\n");
        return ret;
    }

    dphy_hw->hw_base_phy0_addr = devm_ioremap(dev, dphy_hw->phy_cfg_addr, sizeof(u32));
    if (IS_ERR(dphy_hw->hw_base_phy0_addr)) {
        dev_err(dev, "Failed to map PHY address\n");
        return PTR_ERR(dphy_hw->hw_base_phy0_addr);
    }

	ret = of_property_read_u32(dev->of_node, "csi_base", &dphy_hw->csi_addr);
    if (ret) {
        dev_err(dev, "Failed to read csi_base address\n");
        return ret;
    }

    dphy_hw->csi_base_addr = devm_ioremap(dev, dphy_hw->csi_addr, sizeof(u32));
    if (IS_ERR(dphy_hw->csi_base_addr)) {
        dev_err(dev, "Failed to map csi address\n");
        return PTR_ERR(dphy_hw->csi_base_addr);
    }

	atomic_set(&dphy_hw->stream_cnt, 0);

	mutex_init(&dphy_hw->mutex);

	platform_set_drvdata(pdev, dphy_hw);

	dev_info(dev, "csi2 dphy hw probe successfully!\n");
	// eswin_csi2_dphy_init();
	return 0;
}

static int eswin_csi2_dphy_hw_remove(struct platform_device *pdev)
{
	struct csi2_dphy_hw *hw = platform_get_drvdata(pdev);

	mutex_destroy(&hw->mutex);

	return 0;
}

static struct platform_driver eswin_csi2_dphy_hw_driver = {
	.probe = eswin_csi2_dphy_hw_probe,
	.remove = eswin_csi2_dphy_hw_remove,
	.driver = {
		.name = "eswin-csi2-dphy-hw",
		.owner = THIS_MODULE,
		.of_match_table = eswin_csi2_dphy_hw_match_id,
	},
};

int eswin_csi2_dphy_hw_init(void)
{
	return platform_driver_register(&eswin_csi2_dphy_hw_driver);
}

static void eswin_csi2_dphy_hw_exit(void)
{
	platform_driver_unregister(&eswin_csi2_dphy_hw_driver);
}

late_initcall(eswin_csi2_dphy_hw_init);
module_exit(eswin_csi2_dphy_hw_exit);
// module_platform_driver(eswin_csi2_dphy_hw_driver);

MODULE_AUTHOR("luyulin@eswincomputing.com");
MODULE_DESCRIPTION("Eswin dphy platform driver");
MODULE_LICENSE("GPL v2");
