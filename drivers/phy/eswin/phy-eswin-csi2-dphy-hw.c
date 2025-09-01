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
#include <media/eswin/eswin_vi.h>
#include "phy-eswin-csi2-dphy-common.h"

/* eic770x */
#define D0_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS      0x51000000
#define D0_VI_COMBO_PHY0_REGISTER_BASE_ADDRESS     (D0_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0xc0000)
#define D0_VI_COMBO_PHY1_REGISTER_BASE_ADDRESS     (D0_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0xe0000)
#define D0_VI_COMBO_PHY2_REGISTER_BASE_ADDRESS     (D0_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0x100000)
#define D0_VI_COMBO_PHY3_REGISTER_BASE_ADDRESS     (D0_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0x120000)
#define D0_VI_COMBO_PHY4_REGISTER_BASE_ADDRESS     (D0_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0x140000)
#define D0_VI_COMBO_PHY5_REGISTER_BASE_ADDRESS     (D0_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0x160000)

#define D1_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS      0x71000000
#define D1_VI_COMBO_PHY0_REGISTER_BASE_ADDRESS     (D1_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0xc0000)
#define D1_VI_COMBO_PHY1_REGISTER_BASE_ADDRESS     (D1_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0xe0000)
#define D1_VI_COMBO_PHY2_REGISTER_BASE_ADDRESS     (D1_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0x100000)
#define D1_VI_COMBO_PHY3_REGISTER_BASE_ADDRESS     (D1_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0x120000)
#define D1_VI_COMBO_PHY4_REGISTER_BASE_ADDRESS     (D1_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0x140000)
#define D1_VI_COMBO_PHY5_REGISTER_BASE_ADDRESS     (D1_VI_SUBSYSTEM_REGISTER_BASE_ADDRESS + 0x160000)

static inline void eic770x_write_csi2_dphy_reg(void __iomem *hw_base_addr, u32 offset,
				       u32 value)
{
		writel(value, hw_base_addr + 4 * offset);
		pr_debug("%s : write reg:0x%x, val:0x%x\n", __func__, offset, value);
}

//	{445, {{0x1229, 0xa70}, {0x3080, 0xe1d}, {0x3280, 0xe1d},  {0x1029, 0xbf0}, {0x1229, 0xb70}, {0x1429, 0xbf0},}} 这应该是400Mpbs速率
static const struct csi2_dphy_rate_table eic7700_csi2_dphy_hw_hsfreq_ranges[] = {
	{223, {{0x1229, 0xa50}, {0x3080, 0x101d}, {0x3280, 0x101d}, {0x1029, 0xaf0}, {0x1229, 0xa50}, {0x1429, 0xaf0},}},
	{400, {{0x1229, 0xa70}, {0x3080, 0xe1d}, {0x3280, 0xe1d}, {0x1029, 0xaf0}, {0x1229, 0xa70}, {0x1429, 0xaf0},}},
	{445, {{0x1229, 0xa70}, {0x3080, 0xe1d}, {0x3280, 0xe1d}, {0x1029, 0xaf0}, {0x1229, 0xa70}, {0x1429, 0xaf0},}},
	{500, {{0x1229, 0xa70}, {0x3080, 0xe1d}, {0x3280, 0xe1d}, {0x1029, 0xaf0}, {0x1229, 0xa70}, {0x1429, 0xaf0},}},
	{550, {{0x1229, 0xa70}, {0x3080, 0xe1d}, {0x3280, 0xe1d}, {0x1029, 0xaf0}, {0x1229, 0xa70}, {0x1429, 0xaf0},}},
	{600, {{0x1229, 0xa70}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa70}, {0x1429, 0xaf0},}},
	{640, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{720, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{755, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{800, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{850, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{891, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	//support 327/219/258/415 {720, {{0x1229, 0xa70}, {0x3080, 0xe1d}, {0x3280, 0xe1d},  {0x1029, 0xbf0}, {0x1229, 0xb70}, {0x1429, 0xbf0},}},
	{912, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{950, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{1000, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{1050, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{1100, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{1150, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{1200, {{0x1229, 0xa90}, {0x3080, 0xd1d}, {0x3280, 0xd1d}, {0x1029, 0xaf0}, {0x1229, 0xa90}, {0x1429, 0xaf0},}},
	{1300, {{0x1229, 0xab0}, {0x3080, 0xd1d}, {0x3280, 0xd1d},  {0x1029, 0xaf0}, {0x1229, 0xab0}, {0x1429, 0xaf0},}},
	{1440, {{0x1229, 0xab0}, {0x3080, 0xc1d}, {0x3280, 0xc1d}, {0x1029, 0xaf0}, {0x1229, 0xab0}, {0x1429, 0xaf0},}},
	{1501, {{0xe23, 0x76}, {0xe21, 0x178f}, {0xe25, 0x110}, {0xe25, 0x113}, {0x3083, 0x9221}, {0x3283, 0x9221}, {0x3089, 0x8f}, {0x3289, 0x8f}, {0x3086, 0x1d02},{0x3286, 0x1d02},}},
	{1550, {{0xe23, 0x7a}, {0xe21, 0x178f}, {0xe25, 0x110}, {0xe25, 0x113}, {0x3083, 0x9221}, {0x3283, 0x9221}, {0x3089, 0x8f}, {0x3289, 0x8f}, {0x3086, 0x1d02},{0x3286, 0x1d02},}},
	{1782, {{0xe23, 0x8c}, {0xe21, 0x177f}, {0xe25, 0xf0}, {0xe25, 0xf2}, {0x3083, 0x9221}, {0x3283, 0x9221}, {0x3089, 0x7f}, {0x3289, 0x7f}, {0x3086, 0x1a02},{0x3286, 0x1a02},}},
	{1900, {{0xe23, 0x95}, {0xe21, 0x1777}, {0xe25, 0xe0}, {0xe25, 0xe2}, {0x3083, 0x9219}, {0x3283, 0x9219}, {0x3089, 0x77}, {0x3289, 0x77}, {0x3086, 0x1802},{0x3286, 0x1802},}},
	{2376, {{0xe23, 0xba}, {0xe21, 0x175f}, {0xe25, 0xb0}, {0xe25, 0xb1}, {0x3083, 0x9219}, {0x3283, 0x9219}, {0x3089, 0x5f}, {0x3289, 0x5f}, {0x3086, 0x1302},{0x3286, 0x1302},}},
	{2500, {{0xe23, 0xc4}, {0xe21, 0x1757}, {0xe25, 0xa0}, {0xe25, 0xa1}, {0x3083, 0x9219}, {0x3283, 0x9219}, {0x3089, 0x57}, {0x3289, 0x57}, {0x3086, 0x1202},{0x3286, 0x1202},}}
};

unsigned int deskew_fine_mem_values[] = {
    0x404, 0x40c, 0x414, 0x41c, 0x423, 0x429, 0x430, 0x43a,
    0x445, 0x44a, 0x450, 0x45a, 0x465, 0x469, 0x472, 0x47a,
    0x485, 0x489, 0x490, 0x49a, 0x4a4, 0x4ac, 0x4b4, 0x4bc,
    0x4c4, 0x4cc, 0x4d4, 0x4dc, 0x4e4, 0x4ec, 0x4f4, 0x4fc,
    0x504, 0x50c, 0x514, 0x51c, 0x523, 0x529, 0x530, 0x53a,
    0x545, 0x54a, 0x550, 0x55a, 0x565, 0x569, 0x572, 0x57a,
    0x585, 0x589, 0x590, 0x59a, 0x5a4, 0x5ac, 0x5b4, 0x5bc,
    0x5c4, 0x5cc, 0x5d4, 0x5dc, 0x5e4, 0x5ec, 0x5f4, 0x5fc,
    0x604, 0x60c, 0x614, 0x61c, 0x623, 0x629, 0x632, 0x63a,
    0x645, 0x64a, 0x650, 0x65a, 0x665, 0x669, 0x672, 0x67a,
    0x685, 0x689, 0x690, 0x69a, 0x6a4, 0x6ac, 0x6b4, 0x6bc,
    0x6c4, 0x6cc, 0x6d4, 0x6dc, 0x6e4, 0x6ec, 0x6f4, 0x6fc,
    0x704, 0x70c, 0x714, 0x71c, 0x723, 0x72a, 0x730, 0x73a,
    0x745, 0x74a, 0x750, 0x75a, 0x765, 0x769, 0x772, 0x77a,
    0x785, 0x789, 0x790, 0x79a, 0x7a4, 0x7ac, 0x7b4, 0x7bc,
    0x7c4, 0x7cc, 0x7d4, 0x7dc, 0x7e4, 0x7ec, 0x7f4, 0x7fc
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


static int eic770x_csi2_dphy_init(struct csi2_dphy_hw *dphy_hw, void __iomem * hw)
{
	unsigned int num_values = sizeof(deskew_fine_mem_values) / sizeof(deskew_fine_mem_values[0]);
	eic770x_write_csi2_dphy_reg(hw, 0xc10       , 0x30  ) ;//PPI_STARTUP_RW_COMMON_DPHY_10
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

	eic770x_write_csi2_dphy_reg(hw, 0x1229      , dphy_hw->dphy_rate_tbl.reg_vals[0].val ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
	dev_dbg(dphy_hw->dev, "dphy_rate_tbl.reg_vals[0].val = 0x%x\n", dphy_hw->dphy_rate_tbl.reg_vals[0].val);

	eic770x_write_csi2_dphy_reg(hw, 0x102a      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_10
	eic770x_write_csi2_dphy_reg(hw, 0x122a      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_10
	eic770x_write_csi2_dphy_reg(hw, 0x142a      , 0x0   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_10
	eic770x_write_csi2_dphy_reg(hw, 0x102f      , 0x4   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_15
	eic770x_write_csi2_dphy_reg(hw, 0x122f      , 0x4   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_15
	eic770x_write_csi2_dphy_reg(hw, 0x142f      , 0x4   ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_15
	eic770x_write_csi2_dphy_reg(hw, 0x3880      , 0x91c ) ;//CORE_DIG_DLANE_CLK_RW_HS_RX_0
	eic770x_write_csi2_dphy_reg(hw, 0x3887      , 0x3b06) ;//CORE_DIG_DLANE_CLK_RW_HS_RX_7

	eic770x_write_csi2_dphy_reg(hw, 0x3080      , dphy_hw->dphy_rate_tbl.reg_vals[1].val ) ;//CORE_DIG_DLANE_0_RW_HS_RX_0
	eic770x_write_csi2_dphy_reg(hw, 0x3280      , dphy_hw->dphy_rate_tbl.reg_vals[2].val ) ;//CORE_DIG_DLANE_1_RW_HS_RX_0
	dev_dbg(dphy_hw->dev, "dphy_rate_tbl.reg_vals[1].val = 0x%x\n", dphy_hw->dphy_rate_tbl.reg_vals[1].val);
	dev_dbg(dphy_hw->dev, "dphy_rate_tbl.reg_vals[2].val = 0x%x\n", dphy_hw->dphy_rate_tbl.reg_vals[2].val);

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

	if(dphy_hw->rate >= 1500) {
		for (unsigned int i = 0; i < num_values; i++) {
			eic770x_write_csi2_dphy_reg(hw, 0x1ff0, deskew_fine_mem_values[i]);
		}
	}

	if(dphy_hw->phy_cfg_base_addr == hw) {
		if(dphy_hw->lanes_dp_dn[0] == 0x3 && dphy_hw->lanes_dp_dn[1] == 0x3) {
			eic770x_write_csi2_dphy_reg(hw, 0x1029      , (dphy_hw->dphy_rate_tbl.reg_vals[3].val | 0x100)) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1229      , (dphy_hw->dphy_rate_tbl.reg_vals[4].val | 0x100)) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1429      , (dphy_hw->dphy_rate_tbl.reg_vals[5].val | 0x100)) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9
		} else {
			eic770x_write_csi2_dphy_reg(hw, 0x1029      , dphy_hw->dphy_rate_tbl.reg_vals[3].val ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1229      , dphy_hw->dphy_rate_tbl.reg_vals[4].val ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1429      , dphy_hw->dphy_rate_tbl.reg_vals[5].val ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9
		}
		eic770x_write_csi2_dphy_reg(hw, 0x3800      , dphy_hw->lanes_dp_dn[0]	) ;//CORE_DIG_DLANE_CLK_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3800      , dphy_hw->lanes_dp_dn[0]	) ;//CORE_DIG_DLANE_CLK_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3000      , dphy_hw->lanes_dp_dn[0]   ) ;//CORE_DIG_DLANE_0_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3200      , dphy_hw->lanes_dp_dn[1]   ) ;//CORE_DIG_DLANE_1_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3000      , dphy_hw->lanes_dp_dn[0]   ) ;//CORE_DIG_DLANE_0_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3200      , dphy_hw->lanes_dp_dn[1]   ) ;//CORE_DIG_DLANE_1_RW_CFG_0
	} else {
		if(dphy_hw->lanes_dp_dn[2] == 0x3 && dphy_hw->lanes_dp_dn[3] == 0x3) {
			eic770x_write_csi2_dphy_reg(hw, 0x1029      , (dphy_hw->dphy_rate_tbl.reg_vals[3].val | 0x100)) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1229      , (dphy_hw->dphy_rate_tbl.reg_vals[4].val | 0x100)) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1429      , (dphy_hw->dphy_rate_tbl.reg_vals[5].val | 0x100)) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9
		} else {
			eic770x_write_csi2_dphy_reg(hw, 0x1029      , dphy_hw->dphy_rate_tbl.reg_vals[3].val ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1229      , dphy_hw->dphy_rate_tbl.reg_vals[4].val ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1429      , dphy_hw->dphy_rate_tbl.reg_vals[5].val ) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9
		}
		eic770x_write_csi2_dphy_reg(hw, 0x3800      , dphy_hw->lanes_dp_dn[2]) ;//CORE_DIG_DLANE_CLK_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3800      , dphy_hw->lanes_dp_dn[2]   ) ;//CORE_DIG_DLANE_CLK_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3000      , dphy_hw->lanes_dp_dn[2]   ) ;//CORE_DIG_DLANE_0_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3200      , dphy_hw->lanes_dp_dn[3]   ) ;//CORE_DIG_DLANE_1_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3000      , dphy_hw->lanes_dp_dn[2]   ) ;//CORE_DIG_DLANE_0_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3200      , dphy_hw->lanes_dp_dn[3]   ) ;//CORE_DIG_DLANE_1_RW_CFG_0
	}

	return 0;
}

static int eic770x_csi2_dphy_init_1_5_G_high(struct csi2_dphy_hw *dphy_hw, void __iomem * hw)
{
	eic770x_write_csi2_dphy_reg(hw, 0xc10,       0x00000030);
	eic770x_write_csi2_dphy_reg(hw, 0x1cf2,      0x00000444);
	eic770x_write_csi2_dphy_reg(hw, 0x1cf2,      0x00001444);
	eic770x_write_csi2_dphy_reg(hw, 0x1cf0,      0x00001bfd);
	eic770x_write_csi2_dphy_reg(hw, 0xc11,       0x00000233);
	eic770x_write_csi2_dphy_reg(hw, 0xc06,       0x00000027);
	eic770x_write_csi2_dphy_reg(hw, 0xc26,       0x000001f4);
	eic770x_write_csi2_dphy_reg(hw, 0xe02,       0x00000320);
	eic770x_write_csi2_dphy_reg(hw, 0xe03,       0x0000001b);
	eic770x_write_csi2_dphy_reg(hw, 0xe05,       0x0000fec8);
	eic770x_write_csi2_dphy_reg(hw, 0xe06,       0x0000646e);
	eic770x_write_csi2_dphy_reg(hw, 0xe06,       0x0000646e);
	eic770x_write_csi2_dphy_reg(hw, 0xe06,       0x0000646e);
	eic770x_write_csi2_dphy_reg(hw, 0xe08,       0x00000105);
	eic770x_write_csi2_dphy_reg(hw, 0xe36,       0x00000003);
	eic770x_write_csi2_dphy_reg(hw, 0xc02,       0x00000005);
	eic770x_write_csi2_dphy_reg(hw, 0xe40,       0x00000017);
	eic770x_write_csi2_dphy_reg(hw, 0xe50,       0x00000004);
	eic770x_write_csi2_dphy_reg(hw, 0xe01,       0x0000005f);
	eic770x_write_csi2_dphy_reg(hw, 0xe05,       0x0000fe1d);
	eic770x_write_csi2_dphy_reg(hw, 0xe06,       0x00000eee);
	eic770x_write_csi2_dphy_reg(hw, 0x1c20,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c21,      0x00000400);
	eic770x_write_csi2_dphy_reg(hw, 0x1c21,      0x00000400);
	eic770x_write_csi2_dphy_reg(hw, 0x1c23,      0x000041f6);
	eic770x_write_csi2_dphy_reg(hw, 0x1c20,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c23,      0x000043f6);
	eic770x_write_csi2_dphy_reg(hw, 0x1c26,      0x00002000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c27,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c26,      0x00003000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c27,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c26,      0x00007000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c27,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c25,      0x00004000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c40,      0x000000f4);
	eic770x_write_csi2_dphy_reg(hw, 0x1c40,      0x000000f4);
	eic770x_write_csi2_dphy_reg(hw, 0x1c47,      0x00000014);
	eic770x_write_csi2_dphy_reg(hw, 0x1c47,      0x00000010);
	eic770x_write_csi2_dphy_reg(hw, 0x1c47,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0xc08,       0x00000050);
	eic770x_write_csi2_dphy_reg(hw, 0xc07,       0x00000028);
	eic770x_write_csi2_dphy_reg(hw, 0xe20,       0x00000077);
	eic770x_write_csi2_dphy_reg(hw, 0xe27,       0x00001132);
	eic770x_write_csi2_dphy_reg(hw, 0xe21,       0x00001740);
	eic770x_write_csi2_dphy_reg(hw, 0xe22,       0x00004b14);
	eic770x_write_csi2_dphy_reg(hw, 0xe22,       0x00004b14);
	eic770x_write_csi2_dphy_reg(hw, 0xe22,       0x00004b14);
	eic770x_write_csi2_dphy_reg(hw, 0xe22,       0x00004b17);
	eic770x_write_csi2_dphy_reg(hw, 0xe22,       0x00004b17);
	eic770x_write_csi2_dphy_reg(hw, 0xe24,       0x0000000a);
	eic770x_write_csi2_dphy_reg(hw, 0xe26,       0x0000800a);
	eic770x_write_csi2_dphy_reg(hw, 0xe27,       0x0000110b);

	eic770x_write_csi2_dphy_reg(hw, 0xe13,       dphy_hw->dphy_rate_tbl.reg_vals[0].val ) ;
	eic770x_write_csi2_dphy_reg(hw, 0xe11,       dphy_hw->dphy_rate_tbl.reg_vals[1].val ) ;
	eic770x_write_csi2_dphy_reg(hw, 0xe15,       dphy_hw->dphy_rate_tbl.reg_vals[2].val ) ;
	eic770x_write_csi2_dphy_reg(hw, 0xe15,       dphy_hw->dphy_rate_tbl.reg_vals[3].val ) ;

	eic770x_write_csi2_dphy_reg(hw, 0x1028,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1228,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1428,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x3040,      0x0000473c);
	eic770x_write_csi2_dphy_reg(hw, 0x3240,      0x0000473c);
	eic770x_write_csi2_dphy_reg(hw, 0x1022,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1222,      0x00000001);
	eic770x_write_csi2_dphy_reg(hw, 0x1422,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1c46,      0x00000009);
	eic770x_write_csi2_dphy_reg(hw, 0x1c46,      0x00000009);
	eic770x_write_csi2_dphy_reg(hw, 0x102c,      0x00000800);
	eic770x_write_csi2_dphy_reg(hw, 0x122c,      0x00000800);
	eic770x_write_csi2_dphy_reg(hw, 0x142c,      0x00000800);
	eic770x_write_csi2_dphy_reg(hw, 0x102d,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x122d,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x142d,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x102c,      0x00000800);
	eic770x_write_csi2_dphy_reg(hw, 0x122c,      0x00000800);
	eic770x_write_csi2_dphy_reg(hw, 0x142c,      0x00000800);
	eic770x_write_csi2_dphy_reg(hw, 0x102d,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x122d,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x142d,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1229,      0x00000ab0);
	eic770x_write_csi2_dphy_reg(hw, 0x102a,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x122a,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x142a,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x102f,      0x00000004);
	eic770x_write_csi2_dphy_reg(hw, 0x122f,      0x00000004);
	eic770x_write_csi2_dphy_reg(hw, 0x142f,      0x00000004);
	eic770x_write_csi2_dphy_reg(hw, 0x3880,      0x0000091c);
	eic770x_write_csi2_dphy_reg(hw, 0x3887,      0x00003b06);
	eic770x_write_csi2_dphy_reg(hw, 0x3080,      0x00000c1d);
	eic770x_write_csi2_dphy_reg(hw, 0x3280,      0x00000c1d);
	eic770x_write_csi2_dphy_reg(hw, 0x3001,      0x00000004);
	eic770x_write_csi2_dphy_reg(hw, 0x3201,      0x00000004);
	eic770x_write_csi2_dphy_reg(hw, 0x3001,      0x00000004);
	eic770x_write_csi2_dphy_reg(hw, 0x3201,      0x00000004);
	eic770x_write_csi2_dphy_reg(hw, 0x3082,      0x0000e69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3282,      0x0000e69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3040,      0x0000173c);
	eic770x_write_csi2_dphy_reg(hw, 0x3240,      0x0000173c);
	eic770x_write_csi2_dphy_reg(hw, 0x3042,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x3242,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x3840,      0x0000163c);
	eic770x_write_csi2_dphy_reg(hw, 0x3842,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x3082,      0x0000e69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3282,      0x0000e69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3081,      0x00004010);
	eic770x_write_csi2_dphy_reg(hw, 0x3281,      0x00004010);
	eic770x_write_csi2_dphy_reg(hw, 0x3082,      0x0000e69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3282,      0x0000e69b);
	eic770x_write_csi2_dphy_reg(hw, 0x3083,      0x00009209);
	eic770x_write_csi2_dphy_reg(hw, 0x3283,      0x00009209);
	eic770x_write_csi2_dphy_reg(hw, 0x3084,      0x00000096);
	eic770x_write_csi2_dphy_reg(hw, 0x3284,      0x00000096);
	eic770x_write_csi2_dphy_reg(hw, 0x3085,      0x00000100);
	eic770x_write_csi2_dphy_reg(hw, 0x3285,      0x00000100);
	eic770x_write_csi2_dphy_reg(hw, 0x3085,      0x00000100);
	eic770x_write_csi2_dphy_reg(hw, 0x3285,      0x00000100);
	eic770x_write_csi2_dphy_reg(hw, 0x3086,      0x00002d02);
	eic770x_write_csi2_dphy_reg(hw, 0x3286,      0x00002d02);
	eic770x_write_csi2_dphy_reg(hw, 0x3087,      0x00001b06);
	eic770x_write_csi2_dphy_reg(hw, 0x3287,      0x00001b06);
	eic770x_write_csi2_dphy_reg(hw, 0x3087,      0x00001b06);
	eic770x_write_csi2_dphy_reg(hw, 0x3287,      0x00001b06);

	eic770x_write_csi2_dphy_reg(hw, 0x3083, dphy_hw->dphy_rate_tbl.reg_vals[4].val ) ;//CORE_DIG_DLANE_0_RW_HS_RX_0
	eic770x_write_csi2_dphy_reg(hw, 0x3283, dphy_hw->dphy_rate_tbl.reg_vals[5].val ) ;//CORE_DIG_DLANE_0_RW_HS_RX_0
	eic770x_write_csi2_dphy_reg(hw, 0x3089, dphy_hw->dphy_rate_tbl.reg_vals[6].val ) ;//CORE_DIG_DLANE_0_RW_HS_RX_0
	eic770x_write_csi2_dphy_reg(hw, 0x3289, dphy_hw->dphy_rate_tbl.reg_vals[7].val ) ;//CORE_DIG_DLANE_0_RW_HS_RX_0
	eic770x_write_csi2_dphy_reg(hw, 0x3086, dphy_hw->dphy_rate_tbl.reg_vals[8].val ) ;//CORE_DIG_DLANE_0_RW_HS_RX_0
	eic770x_write_csi2_dphy_reg(hw, 0x3286, dphy_hw->dphy_rate_tbl.reg_vals[9].val ) ;//CORE_DIG_DLANE_0_RW_HS_RX_0

	for (unsigned int i = 0; i < sizeof(deskew_fine_mem_values)/sizeof(deskew_fine_mem_values[0]); i++) {
		eic770x_write_csi2_dphy_reg(hw, 0x740, deskew_fine_mem_values[i]);
	}

	eic770x_write_csi2_dphy_reg(hw, 0x3800,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x3800,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x3000,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x3200,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x3000,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x3200,      0x00000000);
	eic770x_write_csi2_dphy_reg(hw, 0x1029,      0x00000af0);
	eic770x_write_csi2_dphy_reg(hw, 0x1229,      0x00000ab0);
	eic770x_write_csi2_dphy_reg(hw, 0x1429,      0x00000af0);

	if(dphy_hw->phy_cfg_base_addr == hw) {
		if(dphy_hw->lanes_dp_dn[0] == 0x3 && dphy_hw->lanes_dp_dn[1] == 0x3) {
			eic770x_write_csi2_dphy_reg(hw, 0x1029, (0x00000af0 | 0x100)) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1229, (0x00000ab0 | 0x100)) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1429, (0x00000af0 | 0x100)) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9
		} else {
			eic770x_write_csi2_dphy_reg(hw, 0x1029, 0x00000af0) ;//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1229, 0x00000ab0) ;//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1429, 0x00000af0) ;//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9
		}
		eic770x_write_csi2_dphy_reg(hw, 0x3800, dphy_hw->lanes_dp_dn[0]);//CORE_DIG_DLANE_CLK_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3800, dphy_hw->lanes_dp_dn[0]);//CORE_DIG_DLANE_CLK_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3000, dphy_hw->lanes_dp_dn[0]);//CORE_DIG_DLANE_0_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3200, dphy_hw->lanes_dp_dn[1]);//CORE_DIG_DLANE_1_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3000, dphy_hw->lanes_dp_dn[0]);//CORE_DIG_DLANE_0_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3200, dphy_hw->lanes_dp_dn[1]);//CORE_DIG_DLANE_1_RW_CFG_0
	} else {
		if(dphy_hw->lanes_dp_dn[2] == 0x3 && dphy_hw->lanes_dp_dn[3] == 0x3) {
			eic770x_write_csi2_dphy_reg(hw, 0x1029, (0x00000af0 | 0x100));//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1229, (0x00000ab0 | 0x100));//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1429, (0x00000af0 | 0x100));//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9
		} else {
			eic770x_write_csi2_dphy_reg(hw, 0x1029, 0x00000af0);//CORE_DIG_IOCTRL_RW_AFE_LANE0_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1229, 0x00000ab0);//CORE_DIG_IOCTRL_RW_AFE_LANE1_CTRL_2_9
			eic770x_write_csi2_dphy_reg(hw, 0x1429, 0x00000af0);//CORE_DIG_IOCTRL_RW_AFE_LANE2_CTRL_2_9
		}
		eic770x_write_csi2_dphy_reg(hw, 0x3800, dphy_hw->lanes_dp_dn[2]);//CORE_DIG_DLANE_CLK_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3800, dphy_hw->lanes_dp_dn[2]);//CORE_DIG_DLANE_CLK_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3000, dphy_hw->lanes_dp_dn[2]);//CORE_DIG_DLANE_0_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3200, dphy_hw->lanes_dp_dn[3]);//CORE_DIG_DLANE_1_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3000, dphy_hw->lanes_dp_dn[2]);//CORE_DIG_DLANE_0_RW_CFG_0
		eic770x_write_csi2_dphy_reg(hw, 0x3200, dphy_hw->lanes_dp_dn[3]);//CORE_DIG_DLANE_1_RW_CFG_0
	}

	return 0;
}

static void eic770x_csi2_dphy_init_4lane(u32 phy_addr, void __iomem  *hw) 
{
	eic770x_write_csi2_dphy_reg(hw, 0x102a, 0x4);
	eic770x_write_csi2_dphy_reg(hw, 0x122a, 0x4);
	eic770x_write_csi2_dphy_reg(hw, 0x142a, 0x4);
	eic770x_write_csi2_dphy_reg(hw, 0x102f, 0x1c);
	if (phy_addr == D0_VI_COMBO_PHY0_REGISTER_BASE_ADDRESS ||
	    phy_addr == D0_VI_COMBO_PHY2_REGISTER_BASE_ADDRESS ||
	    phy_addr == D0_VI_COMBO_PHY4_REGISTER_BASE_ADDRESS ||
		phy_addr == D1_VI_COMBO_PHY0_REGISTER_BASE_ADDRESS ||
		phy_addr == D1_VI_COMBO_PHY2_REGISTER_BASE_ADDRESS ||
		phy_addr == D1_VI_COMBO_PHY4_REGISTER_BASE_ADDRESS) { //phy0/2/4 only
		eic770x_write_csi2_dphy_reg(hw, 0x122f, 0x1c);
	}
	eic770x_write_csi2_dphy_reg(hw, 0x142f, 0x1c);
	eic770x_write_csi2_dphy_reg(hw, 0x1c40, 0xf6);
}

int eic770x_csi_cfg(struct csi2_dphy_hw *dphy_hw )
{
	writel(1, dphy_hw->csi_base_addr + 0x40);  //phy shutdownz
	writel(1, dphy_hw->csi_base_addr + 0x44); //phy reset
	writel(0x1010100, dphy_hw->csi_base_addr + 0x80);
	writel(1, dphy_hw->csi_base_addr + 0x8);
	return 0;
}

static void eic770x_csi2_dphy_match_best_rate(struct csi2_dphy *dphy)
{
	struct csi2_dphy_hw *hw = dphy->dphy_hw;
	u32 min_diff = UINT_MAX;
	u64 rate = dphy->data_rate_mbps;
	int i;

	for (i = 0; i < ARRAY_SIZE(eic7700_csi2_dphy_hw_hsfreq_ranges); i++) {
		u32 curr_diff =
			abs((u64)eic7700_csi2_dphy_hw_hsfreq_ranges[i].rate -
			    (u64)rate);

		if (curr_diff < min_diff) {
			min_diff = curr_diff;
			hw->dphy_rate_tbl =
				eic7700_csi2_dphy_hw_hsfreq_ranges[i];
		}

		if (curr_diff == 0)
			break;
	}

	dev_dbg(dphy->dev, "Matched DPHY rate: %lld Mbps (closest to requested %lld Mbps)\n",
		hw->dphy_rate_tbl.rate, rate);
}

static int csi2_dphy_hw_stream_on(struct csi2_dphy *dphy,
				  struct v4l2_subdev *sd)
{
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor;
	struct csi2_dphy_hw *hw = dphy->dphy_hw;
	u32 phy_ready = 0;
	int ret;

	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(dphy, sensor_sd);
	if (!sensor)
		return -ENODEV;

	mutex_lock(&hw->mutex);

	eic770x_csi2_dphy_match_best_rate(dphy);
	writel(0x2c3f5, hw->phy_cfg_base_addr + 0x0);

	if(hw->dphy_rate_tbl.rate < 1500)
		eic770x_csi2_dphy_init(hw, hw->hw_base_addr);
	else
		eic770x_csi2_dphy_init_1_5_G_high(hw, hw->hw_base_addr);

	if(hw->num_lanes == CSI2_DPHY_4LANES) {
		eic770x_csi2_dphy_init_4lane(hw->dphy_hw_phy_addr, hw->hw_base_addr);
		// combine phy
		dev_dbg(hw->dev, "start combine phy, combine phy phy addr 0x%x, combine phy base addr %p \n",
			hw->combine_dphy_phy_addr, hw->combine_dphy_base_addr);
		if(hw->dphy_rate_tbl.rate < 1500)
			eic770x_csi2_dphy_init(hw, hw->combine_dphy_base_addr);
		else
			eic770x_csi2_dphy_init_1_5_G_high(hw, hw->combine_dphy_base_addr)	;
		eic770x_csi2_dphy_init_4lane(hw->combine_dphy_phy_addr, hw->combine_dphy_base_addr);
	}
	// udelay(20);
	ret = eic770x_csi_cfg(hw);
	if (ret) {
		dev_err(hw->dev, "Failed to configure csi phy\n");
		mutex_unlock(&hw->mutex);
		return ret;
	}
	int count = 20;
	while (count-- && phy_ready != 0x3) {
		phy_ready = readl(hw->phy_cfg_base_addr + 0x4);
		dev_dbg(hw->dev, "0x%x: csi phy status 0x%x\n",
			 hw->phy_cfg_addr, phy_ready);
		udelay(10000);
	}

	if(hw->num_lanes == CSI2_DPHY_4LANES) {
		count = 20;
		phy_ready = 0x0;
		while (count-- && phy_ready != 0x3) {
			phy_ready = readl(hw->combine_dphy_base_addr +0x18000 + 0x4); 
			dev_dbg(hw->dev, "0x%x: csi phy status 0x%x\n", hw->combine_dphy_phy_addr+0x18000, phy_ready);
			udelay(10000);
		}
	}

	if(phy_ready != 0x3) {
		dev_err(hw->dev, "csi2 dphy not ready, phy status 0x%x\n", phy_ready);
		mutex_unlock(&hw->mutex);
		return -EIO;
	}

	writel(0x2c075, hw->phy_cfg_base_addr + 0x0);

	if(hw->num_lanes == CSI2_DPHY_4LANES) {
		dev_dbg(hw->dev, "start combine phy, combine phy phy addr 0x%x, combine phy base addr %p \n", 
			hw->combine_dphy_phy_addr, hw->combine_dphy_base_addr);
		writel(0x2c065, hw->combine_dphy_base_addr + 0x18000);
	}

	atomic_inc(&hw->stream_cnt);

	mutex_unlock(&hw->mutex);

	return 0;
}

//TODO need check stream off config for dwc dphy
static int csi2_dphy_hw_stream_off(struct csi2_dphy *dphy,
				   struct v4l2_subdev *sd)
{
	struct csi2_dphy_hw *hw = dphy->dphy_hw;

	if (atomic_dec_return(&hw->stream_cnt) < 0)
	{
		atomic_set(&hw->stream_cnt, 0);
		dev_warn(hw->dev, "stream off called more than stream on\n");
		return -EINVAL;
	}
	dev_dbg(hw->dev, "stream off\n");
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

static int eswin_csi2_dphy_hw_of_notifier(struct notifier_block *nb,
	unsigned long action, void *data)
{
	struct csi2_dphy_hw *dphy_hw = container_of(nb, struct csi2_dphy_hw, of_notifier);
	struct of_overlay_notify_data *notify_data = data;
	int ret = 0;
	if (!dphy_hw || !notify_data || !notify_data->target)
		return NOTIFY_DONE;

	if (notify_data->target != dphy_hw->dev->of_node)
		return NOTIFY_DONE;

	if (action == OF_OVERLAY_POST_APPLY) {
		msleep(200);

		dphy_hw->num_lanes = of_property_count_elems_of_size(dphy_hw->dev->of_node, "lanes", sizeof(u32));
		dev_dbg(dphy_hw->dev, "dphy num lanes %d\n", dphy_hw->num_lanes);
		if(dphy_hw->num_lanes < 0) {
			dev_warn(dphy_hw->dev, "Failed to get lanes count, use default 2 lanes\n");
			dphy_hw->num_lanes = 2;
		} else {
			ret = of_property_read_u32_array(dphy_hw->dev->of_node, "lanes", dphy_hw->lanes_array, dphy_hw->num_lanes);
			if (ret) {
				dev_err(dphy_hw->dev, "Failed to read lanes array\n");
				return NOTIFY_DONE;
			}
		}

		ret = of_property_count_elems_of_size(dphy_hw->dev->of_node, "lanes-dp-dn", sizeof(u32));
		if(ret < 0) {
			dev_warn(dphy_hw->dev, "Failed to get lanes-dp-dn property, use default positive on dp, negative on dn to hs and lp mode\n");
			memset(dphy_hw->lanes_dp_dn, 0, 4 * dphy_hw->num_lanes);
		} else {
			ret = of_property_read_u32_array(dphy_hw->dev->of_node, "lanes-dp-dn", dphy_hw->lanes_dp_dn, dphy_hw->num_lanes);
			if (ret) {
				dev_err(dphy_hw->dev, "Failed to read dp-dn array\n");
				return NOTIFY_DONE;
			}
		}
	}
	return NOTIFY_DONE;
}

//TODO update cphy hsfreq_ranges
static const struct dphy_hw_drv_data eswin_csi2_dphy_hw_drv_data = {
	.dphy_hsfreq_ranges = eic7700_csi2_dphy_hw_hsfreq_ranges,
	.dphy_num_hsfreq_ranges = ARRAY_SIZE(eic7700_csi2_dphy_hw_hsfreq_ranges),
	.cphy_hsfreq_ranges = eic7700_csi2_dphy_hw_hsfreq_ranges,
	.cphy_num_hsfreq_ranges = ARRAY_SIZE(eic7700_csi2_dphy_hw_hsfreq_ranges),
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
	struct eswin_vi_device* es_vi_dev;
	struct device *parent = pdev->dev.parent;
	u32 ret;

	es_vi_dev = dev_get_drvdata(parent);

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

    dphy_hw->phy_cfg_base_addr = devm_ioremap(dev, dphy_hw->phy_cfg_addr, sizeof(u32));
    if (IS_ERR(dphy_hw->phy_cfg_base_addr)) {
        dev_err(dev, "Failed to map PHY address\n");
        return PTR_ERR(dphy_hw->phy_cfg_base_addr);
    }

	ret = of_property_read_u32(dphy_hw->dev->of_node, "csi_base", &dphy_hw->csi_addr);
    if (ret) {
        dev_err(dphy_hw->dev, "Failed to read csi_base address\n");
        return ret;
    }

	//TODO check the size
    dphy_hw->csi_base_addr = devm_ioremap(dphy_hw->dev, dphy_hw->csi_addr, 0x1000);
    if (IS_ERR(dphy_hw->csi_base_addr)) {
        dev_err(dphy_hw->dev, "Failed to map PHY address\n");
        return PTR_ERR(dphy_hw->csi_base_addr);
    }

	dphy_hw->num_lanes = of_property_count_elems_of_size(dev->of_node, "lanes", sizeof(u32));
	if(dphy_hw->num_lanes < 0) {
		dev_warn(dev, "Failed to get lanes count, use default 2 lanes\n");
		dphy_hw->num_lanes = 2;
	} else {
		ret = of_property_read_u32_array(dev->of_node, "lanes", dphy_hw->lanes_array, dphy_hw->num_lanes);
		if (ret) {
			dev_err(dev, "Failed to read lanes array\n");
			return ret;
		}
	}

	ret = of_property_count_elems_of_size(dev->of_node, "lanes-dp-dn", sizeof(u32));
	if(ret < 0) {
		dev_warn(dev, "Failed to get lanes-dp-dn property, use default positive on dp, negative on dn to hs and lp mode\n");
		memset(dphy_hw->lanes_dp_dn, 0, 4 * dphy_hw->num_lanes);
	} else {
		ret = of_property_read_u32_array(dev->of_node, "lanes-dp-dn", dphy_hw->lanes_dp_dn, dphy_hw->num_lanes);
		if (ret) {
			dev_err(dev, "Failed to read lanes array\n");
			return ret;
		}
	}

	if(dphy_hw->num_lanes == CSI2_DPHY_4LANES) {
		ret = of_property_read_u32(dphy_hw->dev->of_node, "combine_phy", &dphy_hw->combine_dphy_phy_addr);
		if (ret) {
			dev_err(dphy_hw->dev, "Failed to read combine_phy address\n");
			return ret;
		}
		u32 combine_phy_addr_size = 0x20000;

		dphy_hw->combine_dphy_base_addr = devm_ioremap(dphy_hw->dev, dphy_hw->combine_dphy_phy_addr, combine_phy_addr_size);
		if (IS_ERR(dphy_hw->combine_dphy_base_addr)) {
			dev_err(dphy_hw->dev, "Failed to map Combine PHY address\n");
			return PTR_ERR(dphy_hw->combine_dphy_base_addr);
		}
	}

	atomic_set(&dphy_hw->stream_cnt, 0);
	mutex_init(&dphy_hw->mutex);
	platform_set_drvdata(pdev, dphy_hw);

	dphy_hw->of_notifier.notifier_call = eswin_csi2_dphy_hw_of_notifier;
	of_overlay_notifier_register(&dphy_hw->of_notifier);

	dev_info(dev, "csi2 dphy hw probe successfully!\n");
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

MODULE_AUTHOR("luyulin@eswincomputing.com");
MODULE_DESCRIPTION("Eswin dphy platform driver");
MODULE_LICENSE("GPL v2");
