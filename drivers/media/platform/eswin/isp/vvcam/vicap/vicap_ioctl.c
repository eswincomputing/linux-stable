#include <linux/io.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

#include "vicap_ioctl.h"

extern vicap_dev_info_t *vicap_dev_info[VICAP_DEV_MAXCNT];

static int vicap_reg_write_part(struct vicap_device *dev, uint32_t addr, uint32_t data, uint8_t shift, uint8_t width)
{
    uint32_t temp = 0;
    uint32_t mask = (1 << width) - 1;

    // TODO: Remove 2 times reading on soc
    readl(dev->base + addr);
    temp = readl(dev->base + addr);

    temp &= ~(mask << shift);
    temp |= (data & mask) << shift;

    writel(temp, dev->base + addr);

    return 0;
}

static long vicap_sw_reset_ctrl(struct vicap_device *dev)
{
	if (!vicap_dev_info[dev->id]) {
		pr_err("%s: vicap dev info not allocated\n", __func__);
		return -ENODEV;
	}

	memset(vicap_dev_info[dev->id], 0, sizeof(vicap_dev_info_t));

	writel(0, dev->base + VICAP_SW_RESET);
	fsleep(100);
	writel(1, dev->base + VICAP_SW_RESET);

	vicap_reg_write_part(dev, VICAP_PHY_CTRL_CFG8, 0, VIN_UNLOCK_RST_FIFO_LSB, 1);
	vicap_reg_write_part(dev, VICAP_PHY_CTRL_CFG8, 0, VIN_SOFT_RST_LSB, 1);
	fsleep(100);
	vicap_reg_write_part(dev, VICAP_PHY_CTRL_CFG8, 1, VIN_SOFT_RST_LSB, 1);
	vicap_reg_write_part(dev, VICAP_PHY_CTRL_CFG8, 1, VIN_UNLOCK_RST_FIFO_LSB, 1);

	return 0;
}

static long check_phy_mode_4_lane(struct vicap_device *dev)
{
	vicap_dev_info[dev->id]->phy_num = 2;

	if (!vicap_dev_info[dev->id + 1]) { 
		pr_err("%s: vicap dev info not allocated\n", __func__);
		return -ENODEV;
	} else {
		vicap_dev_info[dev->id + 1]->dev_mode = DEV_MODE_SLAVE;
		vicap_dev_info[dev->id + 1]->phy_num = 2;
	}

	return 0;
}

static long check_phy_mode_8_lane(struct vicap_device *dev)
{
	vicap_dev_info[dev->id]->phy_num = 4;

	if (!vicap_dev_info[dev->id + 1] || 
		    !vicap_dev_info[dev->id + 2] ||
		    !vicap_dev_info[dev->id + 3]) {
		pr_err("vicap dev info not allocated\n");
		return -ENODEV;
	} else {
		vicap_dev_info[dev->id + 1]->dev_mode = DEV_MODE_SLAVE;
		vicap_dev_info[dev->id + 2]->dev_mode = DEV_MODE_SLAVE;
		vicap_dev_info[dev->id + 3]->dev_mode = DEV_MODE_SLAVE;
		vicap_dev_info[dev->id + 1]->phy_num = 4;
		vicap_dev_info[dev->id + 2]->phy_num = 4;
		vicap_dev_info[dev->id + 3]->phy_num = 4;
	}

	return 0;
}

static long vicap_ioc_dev_attr(struct vicap_device *dev, vicap_dev_attr_t *dev_attr)
{
	long ret;

	if (!vicap_dev_info[dev->id]) {
		pr_err("%s: vicap dev info not allocated\n", __func__);
		return -ENODEV;
	}

	if (vicap_dev_info[dev->id]->dev_mode == DEV_MODE_SLAVE) {
		pr_err("vicap dev already configured ");
		return -EINVAL;
	}

	memcpy(&vicap_dev_info[dev->id]->dev_attr, dev_attr, sizeof(vicap_dev_attr_t));
	vicap_dev_info[dev->id]->dev_mode = DEV_MODE_MASTER;
	vicap_dev_info[dev->id]->phy_num = 1;

	switch(dev_attr->phy_mode) {
	case PHY_MODE_LANE_8_4:
		if (dev->id == 0) {
			ret = check_phy_mode_8_lane(dev);
			if (ret)
				return ret;
		} else if(dev->id == 4) {
			ret = check_phy_mode_4_lane(dev);
			if (ret)
				return ret;
		} else {
			goto DEV_ERR;
		}
		break;
	case PHY_MODE_LANE_8_2_2:
		if (dev->id == 0) {
			ret = check_phy_mode_8_lane(dev);
			if (ret)
				return ret;
		} else if (dev->id == 1 || dev->id == 2 || dev->id == 3) {
			goto DEV_ERR;
		}
		break;
	case PHY_MODE_LANE_4_4_4:
		if (dev->id == 0 || dev->id == 2 || dev->id == 4) {
			ret = check_phy_mode_4_lane(dev);
			if (ret)
				return ret;
		} else {
			goto DEV_ERR;
		}
		break;
	case PHY_MODE_LANE_4_4_2_2:
		if (dev->id == 0 || dev->id == 2) {
			ret = check_phy_mode_4_lane(dev);
			if (ret)
				return ret;
		} else if (dev->id == 1 || dev->id == 3) {
			goto DEV_ERR;
		}
		break;
	case PHY_MODE_LANE_4_2_2_2_2:
		if (dev->id == 0) {
			ret = check_phy_mode_4_lane(dev);
			if (ret)
				return ret;
		} else if (dev->id == 1) {
			goto DEV_ERR;
		}
		break;
	case PHY_MODE_LANE_2_2_2_2_2_2:
		break;
	default:
		return -EINVAL;
	}

	return 0;
DEV_ERR:
	pr_err("Dev %d cannot support current mode\n", dev->id);
	return -EINVAL;
}

static void vicap_set_common_attr(struct vicap_device *dev)
{
	uint32_t pic_res, lane_en;
	vicap_dev_attr_t *dev_attr = &vicap_dev_info[dev->id]->dev_attr;

	vicap_reg_write_part(dev, VICAP_COMMON_SET, 2, LANE_NUM_LSB, BIT_WIDTH_2); // We only support 2 lane per phy
	//vicap_reg_write_part(dev, VICAP_COMMON_SET, vicap_dev_info[dev->id]->phy_num / 2, PHY_NUM_LSB, BIT_WIDTH_2); // phy_num support 1, 2, 4, lvds ok
	vicap_reg_write_part(dev, VICAP_COMMON_SET, 0, PHY_NUM_LSB, BIT_WIDTH_2); // phy_num support 1, 2, 4
	vicap_reg_write_part(dev, VICAP_COMMON_SET, dev_attr->combo_mode, COMBO_MODE_LSB, BIT_WIDTH_1);
	vicap_reg_write_part(dev, VICAP_COMMON_SET, dev_attr->data_type, L_BIT_LSB, BIT_WIDTH_3);
	vicap_reg_write_part(dev, VICAP_COMMON_SET, dev_attr->data_endian, BIT_REORDER_LSB, BIT_WIDTH_1);
	
	pic_res = ((dev_attr->frame_info.width + dev_attr->frame_info.h_pad) << 16) | (dev_attr->frame_info.height + dev_attr->frame_info.v_pad);
//	pic_res = (dev_attr->frame_info.width << 16) | dev_attr->frame_info.height;
	writel(pic_res, dev->base + VICAP_PIC_RES);
	vicap_reg_write_part(dev, VICAP_HDR_ARG1,
		    (dev_attr->frame_info.height + dev_attr->frame_info.v_pad), FRAME_HEIGHT_LSB, FRAME_HEIGHT_BITS);

	if (dev_attr->crop_info.crop_en) {
		uint32_t crop_x = (dev_attr->crop_info.crop_x1 << 16) | dev_attr->crop_info.crop_x2;
		uint32_t crop_y = (dev_attr->crop_info.crop_y1 << 16) | dev_attr->crop_info.crop_y2;

		vicap_reg_write_part(dev, VICAP_COMMON_SET, 1, CROP_EN_LSB, BIT_WIDTH_1);
		writel(crop_x, dev->base + VICAP_CROP_X);
		writel(crop_y, dev->base + VICAP_CROP_Y);
	}

    // 2 lane per phy
	lane_en = (1 << (vicap_dev_info[dev->id]->phy_num * 2)) - 1;
	vicap_reg_write_part(dev, VICAP_PHY_CTRL_CFG8, lane_en, VIN_LANE_ENABLE_LSB, VIN_LANE_EN_BITS);

	if (vicap_dev_info[dev->id]->dev_mode == DEV_MODE_MASTER) {
		mutex_lock(&dev->vicap_mutex);
		vicap_dev_info[dev->id]->enable = 1;
		mutex_unlock(&dev->vicap_mutex);
	}
}

static void vicap_set_slvs_attr(struct vicap_device *dev)
{
	slvs_dev_attr_t *slvs_attr = &vicap_dev_info[dev->id]->dev_attr.v.slvs_attr;

	vicap_reg_write_part(dev, VICAP_SLVS_ARG, 0, SLVS_MAP_LSB, BIT_WIDTH_1);
	vicap_reg_write_part(dev, VICAP_SLVS_ARG, 0, SLVS_5WORD_LSB, BIT_WIDTH_1); // TODO: Add 5 word sync code support
	vicap_reg_write_part(dev, VICAP_SLVS_ARG, slvs_attr->sync_mode, SLVS_MODE_LSB, BIT_WIDTH_1);

	vicap_reg_write_part(dev, VICAP_SLVS_ARG, slvs_attr->ob_line_en, OB_LINE_EN_LSB, LINE_EN_BITS);
	vicap_reg_write_part(dev, VICAP_SLVS_ARG, slvs_attr->info_line_en, INFO_LINE_EN_LSB, LINE_EN_BITS);
	vicap_reg_write_part(dev, VICAP_SLVS_ARG, slvs_attr->ob_line_num, OB_LINE_NUM_LSB, LINE_NUM_BITS);
	vicap_reg_write_part(dev, VICAP_SLVS_ARG, slvs_attr->info_line_num, INFO_LINE_NUM_LSB, LINE_NUM_BITS);

	// 4 word sync code configuration
	if (slvs_attr->sync_mode == SLVS_SYNC_MODE_SAV) {
		vicap_reg_write_part(dev, VICAP_REG_SC1, slvs_attr->sync_code[0], SC_W1_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC4, slvs_attr->sync_code[1], SC_W7_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC7, slvs_attr->sync_code[2], SC_W13_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC7, slvs_attr->sync_code[3], SC_W14_LSB, SYNC_CODE_BITS);
	} else if (slvs_attr->sync_mode == SLVS_SYNC_MODE_SOF) {
		// sol: sc2.w4, eol: sc5.w10, sof: sc1.w1, eof: sc4.w7
		vicap_reg_write_part(dev, VICAP_REG_SC2, slvs_attr->sync_code[0], SC_W4_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC5, slvs_attr->sync_code[1], SC_W10_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC1, slvs_attr->sync_code[2], SC_W1_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC4, slvs_attr->sync_code[3], SC_W7_LSB, SYNC_CODE_BITS);
	}
}

static void vicap_set_hispi_attr(struct vicap_device *dev)
{
	uint16_t *sync_code;
	hispi_dev_attr_t *hispi_attr = &vicap_dev_info[dev->id]->dev_attr.v.hispi_attr;

	vicap_reg_write_part(dev, VICAP_COMMON_SET, 1, CAP_EN_LSB, BIT_WIDTH_1);
	vicap_reg_write_part(dev, VICAP_COMMON_SET, hispi_attr->ail_endian, AIL_ENDIAN_LSB, BIT_WIDTH_1);
	// flr and crc are optional for all hispi modes
	vicap_reg_write_part(dev, VICAP_HISPI_ARG, hispi_attr->flr_en, FLR_EN_LSB, BIT_WIDTH_1);
	vicap_reg_write_part(dev, VICAP_HISPI_ARG, hispi_attr->crc_en, CRC_EN_LSB, BIT_WIDTH_1);
	vicap_reg_write_part(dev, VICAP_HISPI_ARG, hispi_attr->hispi_mode, HISPI_MODE_LSB, HISPI_MODE_BITS);

	switch(hispi_attr->hispi_mode) {
	case HISPI_MODE_PACKETIZED_SP:
		// SOF: sc1.w1, SOL: sc1.w2, EOF: sc2.w3, EOL: sc2.w4
		sync_code = hispi_attr->sc.pack_sp_sc;
		vicap_reg_write_part(dev, VICAP_REG_SC1, sync_code[0], SC_W1_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC1, sync_code[1], SC_W2_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC2, sync_code[2], SC_W3_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC2, sync_code[3], SC_W4_LSB, SYNC_CODE_BITS);
		break;
	case HISPI_MODE_STREAMING_SP:
		// SOF: sc1.w1, SOL: sc1.w2, SOV: sc3.w5
		sync_code = hispi_attr->sc.stream_sp_sc;
		vicap_reg_write_part(dev, VICAP_REG_SC1, sync_code[0], SC_W1_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC1, sync_code[1], SC_W2_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC3, sync_code[2], SC_W5_LSB, SYNC_CODE_BITS);
		break;
	case HISPI_MODE_STREAMING_S:
		break;
	case HISPI_MODE_ACTIVESTART_SP8:
		sync_code = hispi_attr->sc.act_sp_sc;
		vicap_reg_write_part(dev, VICAP_REG_SC1, sync_code[0], SC_W1_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC1, sync_code[1], SC_W2_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC2, sync_code[2], SC_W3_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC2, sync_code[3], SC_W4_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC3, sync_code[4], SC_W5_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC3, sync_code[5], SC_W6_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC4, sync_code[6], SC_W7_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC4, sync_code[7], SC_W8_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC5, sync_code[8], SC_W9_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC5, sync_code[9], SC_W10_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC6, sync_code[10], SC_W11_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC6, sync_code[11], SC_W12_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC7, sync_code[12], SC_W13_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC7, sync_code[13], SC_W14_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC8, sync_code[14], SC_W15_LSB, SYNC_CODE_BITS);
		vicap_reg_write_part(dev, VICAP_REG_SC8, sync_code[15], SC_W16_LSB, SYNC_CODE_BITS);
		break;
	default:
		break;
	}
}

static long vicap_ioc_dev_enable(struct vicap_device *dev)
{
	vicap_dev_attr_t *dev_attr;

	if (!vicap_dev_info[dev->id]) {
		pr_err("%s: vicap dev info not allocated\n", __func__);
		return -ENODEV;
	}

	dev_attr = &vicap_dev_info[dev->id]->dev_attr;
	if (dev_attr->combo_mode == MODE_SLVS_SUBLVDS) {
		vicap_set_slvs_attr(dev);
	} else if (dev_attr->combo_mode == MODE_HISPI) {
		vicap_set_hispi_attr(dev);
	}

	vicap_set_common_attr(dev);

	return 0;
}

static long vicap_ioc_dev_disable(struct vicap_device *dev)
{
	if (!vicap_dev_info[dev->id]) {
		pr_err("%s: vicap dev info not allocated\n", __func__);
		return -ENODEV;
	}

	if (vicap_dev_info[dev->id]->dev_mode == DEV_MODE_MASTER) {
		mutex_lock(&dev->vicap_mutex);
		vicap_dev_info[dev->id]->enable = 0;
		mutex_unlock(&dev->vicap_mutex);
	}

	vicap_reg_write_part(dev, VICAP_PHY_CTRL_CFG8, 0, VIN_LANE_ENABLE_LSB, VIN_LANE_EN_BITS);

	return 0;
}

long vicap_priv_ioctl(struct vicap_device *dev, unsigned int cmd, void __user *args)
{
	long ret = 0, retval;

	switch(cmd) {
	case VICAP_IOC_S_RESET_CTRL:
		ret = vicap_sw_reset_ctrl(dev);
		break;
	case VICAP_IOC_S_DEV_ATTR:
		vicap_dev_attr_t dev_attr;
		retval = copy_from_user(&dev_attr, args, sizeof(vicap_dev_attr_t));
		if (retval < 0) {
			pr_err("Copy from user failed\n");
			return -EFAULT;
		}
		ret = vicap_ioc_dev_attr(dev, &dev_attr);
		break;
	case VICAP_IOC_S_DEV_ENABLE:
		ret = vicap_ioc_dev_enable(dev);
		break;
	case VICAP_IOC_S_DEV_DISABLE:
		ret = vicap_ioc_dev_disable(dev);
		break;
	default:
		break;
	}

	return ret;
}

