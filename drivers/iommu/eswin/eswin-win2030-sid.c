// SPDX-License-Identifier: GPL-2.0
/*
 * APIs for ESWIN SMMU stream ID and TBU configuration
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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/bitfield.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include <linux/iommu.h>
#include <linux/mfd/syscon.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>

static void trigger_waveform_ioremap_resource(void);

#define DYMN_CSR_EN_REG_OFFSET            0x0
#define DYMN_CSR_GNT_REG_OFFSET           0x4

#define MCPU_SP0_DYMN_CSR_EN_BIT    3
#define MCPU_SP0_DYMN_CSR_GNT_BIT   3

#define AWSMMUSID	GENMASK(31, 24) // The sid of write operation
#define AWSMMUSSID	GENMASK(23, 16) // The ssid of write operation
#define ARSMMUSID	GENMASK(15, 8)	// The sid of read operation
#define ARSMMUSSID	GENMASK(7, 0)	// The ssid of read operation

struct win2030_sid_client {
	const char *name;
	unsigned int sid;
	unsigned int reg_offset;
};

struct win2030_sid_soc {
	const struct win2030_sid_client *clients;
	unsigned int num_clients;
};

/* The syscon registers for tbu power up(down) must be configured so that
    tcu can be aware of tbu up and down.

 */
struct tbu_reg_cfg_info {
	unsigned int reg_offset;
	unsigned int qreqn_pd_bit;
	unsigned int qacceptn_pd_bit;
};

struct tbu_attachment {
	struct device *dev;
	struct list_head list;
	atomic_t f_count;
};

struct tbu_priv {
	atomic_t refcount;
	int nid;
	const struct win2030_tbu_client *tbu_client_p;
	struct list_head attachments;
	struct mutex tbu_priv_lock;
};

struct win2030_tbu_client {
	/* tbu_id: bit[3:0] is for major ID, bit[7:4] is for minor ID;
	   For example, tbu of dsp3 is tbu7_3, the tbu_ID is 0x73. It measn tbu7_3
	*/
	u32 tbu_id;
	struct tbu_reg_cfg_info tbu_reg_info;
	int (*tbu_power_ctl_register) (struct tbu_priv *tbu_priv_p, bool is_powerUp, struct device *dev);
};

struct win2030_tbu_soc {
	const struct win2030_tbu_client *tbu_clients;
	unsigned int num_tbuClients;
};



struct tbu_power_soc {
	struct tbu_priv *tbu_priv_array;
	unsigned int num_tbuClients;
};

struct win2030_sid {
	void __iomem *regs;
	resource_size_t start;
	const struct win2030_sid_soc *soc;
	struct mutex eswin_dynm_sid_cfg_en_lock;
	struct tbu_power_soc *tbu_power_soc;
	struct mutex tbu_reg_lock;
};
struct win2030_sid *syscon_sid_cfg[MAX_NUMNODES] = {NULL};

static int win2030_tbu_power_ctl_register(struct tbu_priv *tbu_priv_p, bool is_powerUp, struct device *dev);

static int win2030_tbu_powr_priv_init(struct tbu_power_soc **tbu_power_soc_pp, int nid);
static int ioremap_tcu_resource(int nid);
void print_tcu_node_status(const char *call_name, int call_line, int nid);
static int __init tcu_proc_init(void);

static int g_nodes_cnt = 0;

int win2030_dynm_sid_enable(int nid)
{
	unsigned long reg_val;
	struct win2030_sid *mc = NULL;

	if (nid == NUMA_NO_NODE) {
	#ifdef CONFIG_NUMA
		pr_err("%s:%d, NUMA_NO_NODE\n", __func__, __LINE__);
		return -EFAULT;
	#else
		pr_info("%s:%d, NUMA_NO_NODE, single DIE\n", __func__, __LINE__);
		nid = 0;
	#endif
	}

	mc = syscon_sid_cfg[nid];
	if (mc == NULL)
		return -EFAULT;

	mutex_lock(&mc->eswin_dynm_sid_cfg_en_lock);
	reg_val = readl(mc->regs + DYMN_CSR_EN_REG_OFFSET);
	set_bit(MCPU_SP0_DYMN_CSR_EN_BIT, &reg_val);
	writel(reg_val, mc->regs + DYMN_CSR_EN_REG_OFFSET);

	while(1) {
		reg_val = readl(mc->regs + DYMN_CSR_GNT_REG_OFFSET) & (1 << MCPU_SP0_DYMN_CSR_GNT_BIT);
		if (reg_val)
			break;

		msleep(10);
	}
	reg_val = readl(mc->regs + DYMN_CSR_EN_REG_OFFSET);
	clear_bit(MCPU_SP0_DYMN_CSR_EN_BIT, &reg_val);
	writel(reg_val, mc->regs + DYMN_CSR_EN_REG_OFFSET);
	mutex_unlock(&mc->eswin_dynm_sid_cfg_en_lock);

	return 0;
}
EXPORT_SYMBOL(win2030_dynm_sid_enable);

int win2030_aon_sid_cfg(struct device *dev)
{
	int ret = 0;
	struct regmap *regmap;
	int aon_sid_reg;
	u32 rdwr_sid_ssid;
	u32 sid;
	int i,sid_count;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct device_node *np_syscon;
	int syscon_cell_size = 0;

	/* not behind smmu, use the default reset value(0x0) of the reg as streamID*/
	if (fwspec == NULL) {
		dev_info(dev, "dev is not behind smmu, skip configuration of sid\n");
		return 0;
	}

	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,syscfg");
	if (IS_ERR(regmap)) {
		dev_err(dev, "No eswin,syscfg phandle specified\n");
		return -1;
	}

	np_syscon = of_parse_phandle(dev->of_node, "eswin,syscfg", 0);
	if (np_syscon) {
		if (of_property_read_u32(np_syscon, "#syscon-cells", &syscon_cell_size)) {
			of_node_put(np_syscon);
			dev_err(dev, "failed to get #syscon-cells of sys_con\n");
			return -1;
		}
		of_node_put(np_syscon);
	}

	sid_count = of_count_phandle_with_args(dev->of_node,
					"eswin,syscfg", "#syscon-cells");

	dev_dbg(dev, "sid_count=%d, fwspec->num_ids=%d, syscon_cell_size=%d\n",
			sid_count, fwspec->num_ids, syscon_cell_size);

	if (sid_count < 0) {
		dev_err(dev, "failed to parse eswin,syscfg property!\n");
		return -1;
	}

	if (fwspec->num_ids != sid_count) {
		dev_err(dev, "num_ids(%d) is NOT equal to num of sid regs(%d)!\n",
			fwspec->num_ids, sid_count);
		return -1;
	}

	for (i = 0; i < sid_count; i++) {
		sid = fwspec->ids[i];
		ret = of_property_read_u32_index(dev->of_node, "eswin,syscfg", (syscon_cell_size + 1)*i+1,
					&aon_sid_reg);
		if (ret) {
			dev_err(dev, "can't get sid cfg reg offset in sys_con(errno:%d)\n", ret);
			return ret;
		}

		/* make the reading sid the same as writing sid, ssid is fixed to zero */
		rdwr_sid_ssid  = FIELD_PREP(AWSMMUSID, sid);
		rdwr_sid_ssid |= FIELD_PREP(ARSMMUSID, sid);
		rdwr_sid_ssid |= FIELD_PREP(AWSMMUSSID, 0);
		rdwr_sid_ssid |= FIELD_PREP(ARSMMUSSID, 0);
		regmap_write(regmap, aon_sid_reg, rdwr_sid_ssid);

		ret = win2030_dynm_sid_enable(dev_to_node(dev));
		if (ret < 0)
			dev_err(dev, "failed to config streamID(%d) for %s!\n", sid, of_node_full_name(dev->of_node));
		else
			dev_info(dev, "success to config dma streamID(%d) for %s!\n", sid, of_node_full_name(dev->of_node));
	}

	return ret;
}
EXPORT_SYMBOL(win2030_aon_sid_cfg);


int win2030_dma_sid_cfg(struct device *dev)
{
	int ret;
	struct regmap *regmap;
	int hsp_mmu_dma_reg;
	u32 rdwr_sid_ssid;
	u32 sid;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	/* not behind smmu, use the default reset value(0x0) of the reg as streamID*/
	if (fwspec == NULL) {
		dev_dbg(dev, "dev is not behind smmu, skip configuration of sid\n");
		return 0;
	}
	sid = fwspec->ids[0];

	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,hsp_sp_csr");
	if (IS_ERR(regmap)) {
		dev_dbg(dev, "No hsp_sp_csr phandle specified\n");
		return 0;
	}

	ret = of_property_read_u32_index(dev->of_node, "eswin,hsp_sp_csr", 1,
					&hsp_mmu_dma_reg);
	if (ret) {
		dev_err(dev, "can't get dma sid cfg reg offset (%d)\n", ret);
		return ret;
	}

	/* make the reading sid the same as writing sid, ssid is fixed to zero */
	rdwr_sid_ssid  = FIELD_PREP(AWSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(AWSMMUSSID, 0);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSSID, 0);
	regmap_write(regmap, hsp_mmu_dma_reg, rdwr_sid_ssid);

	ret = win2030_dynm_sid_enable(dev_to_node(dev));
	if (ret < 0)
		dev_err(dev, "failed to config dma streamID(%d)!\n", sid);
	else
		dev_dbg(dev, "success to config dma streamID(%d)!\n", sid);

	return ret;
}
EXPORT_SYMBOL(win2030_dma_sid_cfg);

static int of_parse_syscon_nodes(struct device_node *np, int *nid_p)
{
	#ifdef CONFIG_NUMA
	int nid;
	int r;

	r = of_property_read_u32(np, "numa-node-id", &nid);
	if (r)
		return -EINVAL;

	pr_debug("Syscon on %u\n", nid);
	if (nid >= MAX_NUMNODES) {
		pr_warn("Node id %u exceeds maximum value\n", nid);
		return -EINVAL;
	}
	else
		*nid_p = nid;
	#else
		*nid_p = 0;
	#endif

	pr_debug("%s, nid = %d\n", __func__, *nid_p);

	return 0;
}

#if 0
static int win2030_program_sid(int nid)
{
	unsigned int i;
	u32 rdwr_sid_ssid;
	struct win2030_sid *mc = NULL;
	int ret = 0;

	if (nid == NUMA_NO_NODE) {
	#ifdef CONFIG_NUMA
		pr_err("%s:%d, NUMA_NO_NODE\n", __func__, __LINE__);
		return -EFAULT;
	#else
		pr_debug("%s:%d, NUMA_NO_NODE, single DIE\n", __func__, __LINE__);
		nid = 0;
	#endif
	}

	mc = syscon_sid_cfg[nid];
	if (mc == NULL)
		return -EFAULT;

	for (i = 0; i < mc->soc->num_clients; i++) {
		const struct win2030_sid_client *client = &mc->soc->clients[i];

		/* make the reading sid the same as writing sid, ssid is fixed to zero */
		rdwr_sid_ssid  = FIELD_PREP(AWSMMUSID, client->sid);
		rdwr_sid_ssid |= FIELD_PREP(ARSMMUSID, client->sid);
		rdwr_sid_ssid |= FIELD_PREP(AWSMMUSSID, 0);
		rdwr_sid_ssid |= FIELD_PREP(ARSMMUSSID, 0);
		pr_debug("smmu_dbg, setting SID %u for %s\n", client->sid,
			client->name);
		writel(rdwr_sid_ssid, mc->regs + client->reg_offset);

		rdwr_sid_ssid = readl(mc->regs + client->reg_offset);

		pr_debug( "smmu_dbg, client %s: rdwr_sid_ssid: 0x%x\n",
			client->name, rdwr_sid_ssid);
	}

	ret = win2030_dynm_sid_enable(nid);
	if (ret < 0)
		pr_err( "smmu_dbg, %s 0x%0llx fail!\n", __func__, mc->start);
	else
		pr_info( "smmu_dbg, %s 0x%0llx done!\n", __func__, mc->start);

	return ret;
}
#endif

#if IS_ENABLED(CONFIG_ARCH_ESWIN_EIC770X_SOC_FAMILY)
static const struct win2030_sid_client win2030_sid_clients[] = {
	{
		.name = "scpu",
		.sid = WIN2030_SID_SCPU,
		.reg_offset = SCPU_SID_REG_OFFSET,
	},
	/* remove the configuration for lcpu, it should be configured by lcpu driver
	*  since there are clk&reset needs to be configured before setting the
	   streamID register in sys_con.
	 */
	//{
		// .name = "lcpu",
		// .sid = WIN2030_SID_LCPU,
		// .reg_offset = LCPU_SID_REG_OFFSET,
	// },
	{
		.name = "dma1",
		.sid = WIN2030_SID_DMA1,
		.reg_offset = DMA1_SID_REG_OFFSET,
	}, {
		.name = "crypt",
		.sid = WIN2030_SID_CRYPT,
		.reg_offset = CRYPT_SID_REG_OFFSET,
	}
};

static const struct win2030_sid_soc win2030_sid_soc = {
	.num_clients = ARRAY_SIZE(win2030_sid_clients),
	.clients = win2030_sid_clients,
};
#endif

static const struct of_device_id win2030_sid_of_match[] = {
	{ .compatible = "eswin,win2030-scu-sys-con", .data = &win2030_sid_soc },
	{ /* sentinel */ }
};

static int __init win2030_init_streamID(void)
{
	const struct of_device_id *match;
	struct device_node *root, *child = NULL;
	struct resource regs;
	struct win2030_sid *mc = NULL;
	int nid;
	int ret = 0;

	/* Mapping trigger reg base for capturing wave in zebu */
	trigger_waveform_ioremap_resource();

	root = of_find_node_by_name(NULL, "soc");
	for_each_child_of_node(root, child) {
		match = of_match_node(win2030_sid_of_match, child);
		if (match && of_node_get(child)) {
			if (of_address_to_resource(child, 0, &regs) < 0) {
				pr_err("failed to get scu register\n");
				of_node_put(child);
				ret = -ENXIO;
				break;
			}
			if (of_parse_syscon_nodes(child, &nid) < 0) {
				pr_err("failed to get numa-node-id\n");
				of_node_put(child);
				ret = -ENXIO;
				break;
			}

			/* program scu sreamID related registers */
			mc = kzalloc(sizeof(*mc), GFP_KERNEL);
			if (!mc) {
				of_node_put(child);
				pr_err("failed to kzalloc\n");
				ret = -ENOMEM;
				break;
			}

			mc->soc = match->data;
			mc->regs = ioremap(regs.start, resource_size(&regs));
			if (IS_ERR(mc->regs)) {
				pr_err("failed to ioremap scu reges\n");
				of_node_put(child);
				ret = PTR_ERR(mc->regs);
				kfree(mc);
				break;
			}
			mc->start = regs.start;
			mutex_init(&mc->eswin_dynm_sid_cfg_en_lock);

			if (win2030_tbu_powr_priv_init(&mc->tbu_power_soc, nid)) {
				pr_err("failed to kzalloc for tbu_power_priv_arry\n");
				of_node_put(child);
				iounmap(mc->regs);
				kfree(mc);
				ret = -ENOMEM;
				WARN_ON(1);
				break;
			}
			mutex_init(&mc->tbu_reg_lock);

			syscon_sid_cfg[nid] = mc;
			g_nodes_cnt++;
			pr_debug("%s, syscon_sid_cfg[%d] addr is 0x%px\n", __func__, nid, syscon_sid_cfg[nid]);

			/* sid configuration was moved into each driver, so skip win2030_program_sid*/
			// win2030_program_sid(nid);

			of_node_put(child);
		}
	}
	of_node_put(root);

	return ret;
}

early_initcall(win2030_init_streamID);



static const struct win2030_tbu_client win2030_tbu_clients[] = {
	{
		.tbu_id = WIN2030_TBUID_0x0, // ISP, DW200 share the tbu0
		.tbu_reg_info = {0x3d8, 7, 6},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x10, // tbu1_0 is only for video decoder
		.tbu_reg_info = {0x3d4, 31, 30},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x11, // tbu1_1 is only video encoder
		.tbu_reg_info = {0x3d4, 23, 22},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x12, // tbu1_2 is only Jpeg encoder
		.tbu_reg_info = {0x3d4, 7, 6},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x13, // tbu1_3 is only Jpeg decoder
		.tbu_reg_info = {0x3d4, 15, 14},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x2, // Ethernet, sata, usb, dma0, emmc, sd, sdio share the tbu2
		.tbu_reg_info = {0x3d8, 15, 14},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x3, // tbu3 is only for pcie
		.tbu_reg_info = {0x3d8, 23, 22},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x4, // scpu, crypto, lpcpu, dma1 share the tbu4
		.tbu_reg_info = {0x3d8, 31, 30},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x5, // tbu5 is only NPU
		.tbu_reg_info = {0x3d0, 15, 14},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x70, // tbu7_0 is only dsp0
		.tbu_reg_info = {0x3f8, 7, 6},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x71, // tbu7_1 is only dsp1
		.tbu_reg_info = {0x3f8, 15, 14},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x72, // tbu7_2 is only dsp2
		.tbu_reg_info = {0x3f8, 23, 22},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
	{
		.tbu_id = WIN2030_TBUID_0x73, // tbu7_3 is only dsp3
		.tbu_reg_info = {0x3f8, 31, 30},
		.tbu_power_ctl_register = win2030_tbu_power_ctl_register,
	},
};

static const struct win2030_tbu_soc win2030_tbu_soc = {
	.num_tbuClients = ARRAY_SIZE(win2030_tbu_clients),
	.tbu_clients = win2030_tbu_clients,
};

static int __do_win2030_tbu_power_ctl(int nid, bool is_powerUp, const struct tbu_reg_cfg_info *tbu_reg_info_p)
{
	int ret = 0;
	unsigned long reg_val;
	struct win2030_sid *mc = NULL;
	int loop_cnt = 0;
	// int bitmask; = BIT(clearbit);
	mc = syscon_sid_cfg[nid];
	if (mc == NULL)
		return -EFAULT;

	mutex_lock(&mc->tbu_reg_lock);
	if (is_powerUp) {
		reg_val = readl(mc->regs + tbu_reg_info_p->reg_offset);
		set_bit(tbu_reg_info_p->qreqn_pd_bit, &reg_val);
		writel(reg_val, mc->regs + tbu_reg_info_p->reg_offset);
		pr_debug("reg_offset=0x%03x, tbu_val=0x%x\n",
			tbu_reg_info_p->reg_offset, readl(mc->regs + tbu_reg_info_p->reg_offset));
		pr_debug("%s, power up!\n", __func__);
	}
	else {
		reg_val = readl(mc->regs + tbu_reg_info_p->reg_offset);
		clear_bit(tbu_reg_info_p->qreqn_pd_bit, &reg_val);
		writel(reg_val, mc->regs + tbu_reg_info_p->reg_offset);
		do {
			reg_val = readl(mc->regs + tbu_reg_info_p->reg_offset);
			pr_debug("reg_offset=0x%03x, tbu_val=0x%lx, BIT(qacceptn_pd_bit)=0x%lx\n",
				tbu_reg_info_p->reg_offset, reg_val, BIT(tbu_reg_info_p->qacceptn_pd_bit));
			if ((reg_val & BIT(tbu_reg_info_p->qacceptn_pd_bit)) == 0) {
				pr_debug("%s, power down!\n", __func__);
				break;
			}
			mdelay(10);
			loop_cnt++;
			if (loop_cnt > 10) {
				WARN_ON(1); // it should never happen.
				break;
			}
		}while (1);

		if(loop_cnt > 10) {
			ret = -1;
		}
	}
	mutex_unlock(&mc->tbu_reg_lock);

	return ret;
}

#define do_win2030_tbu_power_up(nid, tbu_reg_info_p)	__do_win2030_tbu_power_ctl(nid, true, tbu_reg_info_p)
#define do_win2030_tbu_power_down(nid, tbu_reg_info_p)	__do_win2030_tbu_power_ctl(nid, false, tbu_reg_info_p)



static int tbu_power_down_ref_release(atomic_t *ref)
{
	int ret = 0;
	struct tbu_priv *tbu_priv_p = container_of(ref, struct tbu_priv, refcount);
	int nid = tbu_priv_p->nid;
	const struct tbu_reg_cfg_info *tbu_reg_info_p = &tbu_priv_p->tbu_client_p->tbu_reg_info;

	WARN_ON(!tbu_priv_p);
	if (!tbu_priv_p)
		return -1;

	ret = do_win2030_tbu_power_down(nid, tbu_reg_info_p);

	return ret;
}

static int eic770x_tbu_attach(struct device *dev, struct tbu_priv *tbu_priv_p)
{
	struct tbu_attachment *a;
	bool attached = false;

	WARN_ON(!mutex_is_locked(&tbu_priv_p->tbu_priv_lock));

	list_for_each_entry(a, &tbu_priv_p->attachments, list) {
		if (a->dev == dev) {
			attached = true;
			break;
		}
	}

	/* device already attached, then just add refcount */
	if (attached) {
		atomic_add(1, &a->f_count);
		return 0;
	}

	/* allocate tbu_attachement and attach it to the tbu */
	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a) {
		dev_WARN_ONCE(dev, true, "Failed to attach device to tbu %d, errno %d\n", tbu_priv_p->tbu_client_p->tbu_id, -ENOMEM);
		return -ENOMEM;
	}
	a->dev = dev;
	INIT_LIST_HEAD(&a->list);
	list_add(&a->list, &tbu_priv_p->attachments);
	atomic_add(1, &a->f_count);

	return 0;
}

static int eic770x_tbu_detach(struct device *dev, struct tbu_priv *tbu_priv_p)
{
	struct tbu_attachment *a;
	bool attached = false;

	WARN_ON(!mutex_is_locked(&tbu_priv_p->tbu_priv_lock));

	list_for_each_entry(a, &tbu_priv_p->attachments, list) {
		if (a->dev == dev) {
			attached = true;
			break;
		}
	}

	if (!attached) {
		dev_WARN_ONCE(dev, true, "Failed to detach device from tbu %d, dev not in the tbu list!!!\n", tbu_priv_p->tbu_client_p->tbu_id);
		return -ENOENT;
	}

	if (atomic_sub_return(1, &a->f_count) == 0) {
		list_del(&a->list);
		kfree(a);
	}

	return 0;
}

static void eic770x_tbu_dump_attachment(struct tbu_priv *tbu_priv_p)
{
	struct tbu_attachment *a;

	WARN_ON(!mutex_is_locked(&tbu_priv_p->tbu_priv_lock));

	pr_info("------Dump:(node %d) tbu 0x%02x total refcnt %d, attached device(s):------\n",
		tbu_priv_p->nid, tbu_priv_p->tbu_client_p->tbu_id, atomic_read(&tbu_priv_p->refcount));
	list_for_each_entry(a, &tbu_priv_p->attachments, list) {
		pr_info("(node %d) %-32s refcnt %d\n", tbu_priv_p->nid, dev_name(a->dev), atomic_read(&a->f_count));
	}
	pr_info("------End of dump:(node %d) tbu 0x%02x------------------------------------\n",
		tbu_priv_p->nid, tbu_priv_p->tbu_client_p->tbu_id);

}

static int win2030_tbu_power_ctl_register(struct tbu_priv *tbu_priv_p, bool is_powerUp, struct device *dev)
{
	int ret = 0;
	int nid = tbu_priv_p->nid;
	const struct win2030_tbu_client *tbu_client_p = tbu_priv_p->tbu_client_p;
	const struct tbu_reg_cfg_info *tbu_reg_info_p = &tbu_priv_p->tbu_client_p->tbu_reg_info;
	unsigned int old_refcount;

	mutex_lock(&tbu_priv_p->tbu_priv_lock);
	old_refcount = atomic_read(&tbu_priv_p->refcount);

	pr_debug("%s, nid=%d, is_powerUp=%d, tbu_priv_p addr is 0x%px\n",
		__func__, nid, is_powerUp, tbu_priv_p);
	if (is_powerUp == false) { //power down
		if (unlikely(0 == old_refcount)) {
			pr_info("%s, tbu_id 0x%02x is down already!\n", __func__, tbu_client_p->tbu_id);
			goto tbu_finish;
		}

		eic770x_tbu_detach(dev, tbu_priv_p);
		if (atomic_sub_return(1, &tbu_priv_p->refcount) == 0) {
			ret = tbu_power_down_ref_release(&tbu_priv_p->refcount);
		}
		else {
			pr_info("tbu 0x%02x(node %d) is used by other module(s) right now!\n",
				tbu_client_p->tbu_id, tbu_priv_p->nid);
		}
	}
	else { //power up
		if (0 == old_refcount) {
			ret = do_win2030_tbu_power_up(nid, tbu_reg_info_p);
		}
		else {
			pr_info("tbu 0x%02x(node %d) is already power up!",
				tbu_client_p->tbu_id, tbu_priv_p->nid);
		}
		atomic_add(1, &tbu_priv_p->refcount);
		eic770x_tbu_attach(dev, tbu_priv_p);
	}
	eic770x_tbu_dump_attachment(tbu_priv_p);

tbu_finish:
	mutex_unlock(&tbu_priv_p->tbu_priv_lock);

	return ret;

}

static int win2030_tbu_powr_priv_init(struct tbu_power_soc **tbu_power_soc_pp, int nid)
{
	int ret = 0;
	int i;
	unsigned int num_tbuClients = win2030_tbu_soc.num_tbuClients;
	struct tbu_power_soc *tbu_power_soc_p;
	struct tbu_priv *tbu_priv_p;
	unsigned int alloc_size;

	pr_debug("%s:%d\n", __func__, __LINE__);

	tbu_power_soc_p = kzalloc(sizeof(struct tbu_power_soc), GFP_KERNEL);
	if (!tbu_power_soc_p)
		return -ENOMEM;
	pr_debug("%s:%d, tbu_power_soc_p(0x%px)\n", __func__, __LINE__, tbu_power_soc_p);

	alloc_size = num_tbuClients * sizeof(struct tbu_priv);
	tbu_priv_p = kzalloc(alloc_size, GFP_KERNEL);
	if (!tbu_priv_p) {
		ret = -ENOMEM;
		goto err_tbu_priv_p;
	}
	tbu_power_soc_p->tbu_priv_array = tbu_priv_p;
	pr_debug("%s:%d, num_tbu=%d,sizeof(struct tbu_priv)=0x%lx, alloc_size=0x%x, tbu_priv_p=0x%px\n",
		__func__, __LINE__, num_tbuClients, sizeof(struct tbu_priv), alloc_size, tbu_priv_p);

	for (i = 0; i < win2030_tbu_soc.num_tbuClients; i++) {
		tbu_priv_p->nid = nid;
		atomic_set(&tbu_priv_p->refcount, 0);
		tbu_priv_p->tbu_client_p = &win2030_tbu_soc.tbu_clients[i];
		INIT_LIST_HEAD(&tbu_priv_p->attachments);
		mutex_init(&tbu_priv_p->tbu_priv_lock);
		pr_debug("%s, nid %d, tbu 0x%02x, tbu_priv_p(0x%px), sizeof(struct tbu_priv)=0x%lx\n", __func__, nid, tbu_priv_p->tbu_client_p->tbu_id, tbu_priv_p, sizeof(struct tbu_priv));
		tbu_priv_p++;
	}
	tbu_power_soc_p->num_tbuClients = num_tbuClients;

	*tbu_power_soc_pp = tbu_power_soc_p;

	ret = ioremap_tcu_resource(nid);
	if (ret) {
		WARN_ON(1);
	}
	ret = tcu_proc_init();
	if (ret) {
		pr_err("failed to create proc for tcu node %d!!!\n", nid);
	}
	print_tcu_node_status(__func__, __LINE__, nid);
	pr_info("%s finished!\n", __func__);

	return 0;

err_tbu_priv_p:
	kfree(tbu_power_soc_p);

	return ret;

}

static int win2030_get_tbu_priv(int nid, u32 tbu_id, struct tbu_priv **tbu_priv_pp)
{
	int i;
	struct win2030_sid *mc = syscon_sid_cfg[nid];
	struct tbu_power_soc *tbu_power_soc_p = mc->tbu_power_soc;
	struct tbu_priv *tbu_priv_p = tbu_power_soc_p->tbu_priv_array;

	pr_debug("%s,  syscon_sid_cfg[%d] addr is 0x%px, tbu_id=0x%02x, tbu_power_soc_p is 0x%px\n",
		__func__, nid, syscon_sid_cfg[nid], tbu_id, tbu_power_soc_p);

	for (i = 0; i < tbu_power_soc_p->num_tbuClients; i++) {
		if (tbu_id == tbu_priv_p->tbu_client_p->tbu_id) {
			*tbu_priv_pp = tbu_priv_p;
			pr_debug("%s, found tbu_id 0x%02x, tbu_priv_array[%d] tbu_priv_p is 0x%px\n",
				__func__, tbu_id, i, tbu_priv_p);
			return 0;
		}
		tbu_priv_p++;
	}

	return -1;
}

/***********************************************************************************************
   win2030_tbu_power(struct device *dev, bool is_powerUp) is for powering up or down
   the tbus of the device module which is under smmu.
   Drivers should call win2030_tbu_power(dev, true) when probing afer clk of the tbu is on,
   and call call win2030_tbu_power(dev, false) when removing driver before clk of the tbu is off.

   Input:
	struct device *dev	The struct device of the driver that calls this API.
	bool is_powerUp		true: power up the tbus;  false: power down the tbus.
   Return:
	zero:		successfully power up/down
	none zero:	faild to power up/down
***********************************************************************************************/
int win2030_tbu_power(struct device *dev, bool is_powerUp)
{
	int ret = 0;
	struct device_node *node = dev->of_node;
	int nid = dev_to_node(dev);
	u32 tbu_id;
	const struct win2030_tbu_client *tbu_client_p = NULL;
	struct tbu_priv *tbu_priv_p;
	struct property *prop;
	const __be32 *cur;
	int tbu_num = 0;

	if (nid == NUMA_NO_NODE) {
	#ifdef CONFIG_NUMA
		pr_err("%s:%d, NUMA_NO_NODE\n", __func__, __LINE__);
		return -EFAULT;
	#else
		pr_debug("%s:%d, NUMA_NO_NODE, single DIE\n", __func__, __LINE__);
		nid = 0;
	#endif
	}

	dev_info(dev, "%s %s!\n", __func__, (is_powerUp == true)? "up":"down");
	of_property_for_each_u32(node, "tbus", prop, cur, tbu_id) {
		pr_debug("tbus = <0x%02x>\n", tbu_id);
		if (0 == win2030_get_tbu_priv(nid, tbu_id, &tbu_priv_p)) {
			tbu_client_p = tbu_priv_p->tbu_client_p;
			if (tbu_client_p->tbu_power_ctl_register) {
				ret = tbu_client_p->tbu_power_ctl_register(tbu_priv_p, is_powerUp, dev);
				if (ret)
					return ret;
			}
			else {
				ret = __do_win2030_tbu_power_ctl(nid, is_powerUp, &tbu_client_p->tbu_reg_info);
				if (ret)
					return ret;
			}
			tbu_num++;
		}
		else if (tbu_id == WIN2030_TBUID_0xF00) {
			tbu_num++;
		}
		else {
			pr_err("tbu power ctl failed!, Couldn't find tbu 0x%x\n", tbu_id);
			return -1;
		}
	}

	if (tbu_num == 0) {
		pr_err("Err,tbu NOT defined in dts!!!!\n");
		WARN_ON(1);
	}

	return ret;
}
EXPORT_SYMBOL(win2030_tbu_power);

int win2030_tbu_power_by_dev_and_node(struct device *dev, struct device_node *node, bool is_powerUp)
{
	int ret = 0;
	int nid = dev_to_node(dev);
	u32 tbu_id;
	const struct win2030_tbu_client *tbu_client_p = NULL;
	struct tbu_priv *tbu_priv_p;
	struct property *prop;
	const __be32 *cur;
	int tbu_num = 0;

	if (nid == NUMA_NO_NODE) {
	#ifdef CONFIG_NUMA
		pr_err("%s:%d, NUMA_NO_NODE\n", __func__, __LINE__);
		return -EFAULT;
	#else
		pr_info("%s:%d, NUMA_NO_NODE, single DIE\n", __func__, __LINE__);
		nid = 0;
	#endif
	}

	pr_debug("%s called!\n", __func__);
	of_property_for_each_u32(node, "tbus", prop, cur, tbu_id) {
		pr_debug("tbus = <0x%02x>\n", tbu_id);
		if (0 == win2030_get_tbu_priv(nid, tbu_id, &tbu_priv_p)) {
			tbu_client_p = tbu_priv_p->tbu_client_p;
			if (tbu_client_p->tbu_power_ctl_register) {
				ret = tbu_client_p->tbu_power_ctl_register(tbu_priv_p, is_powerUp, dev);
				if (ret)
					return ret;
			}
			else {
				ret = __do_win2030_tbu_power_ctl(nid, is_powerUp, &tbu_client_p->tbu_reg_info);
				if (ret)
					return ret;
			}
			tbu_num++;
		}
		else if (tbu_id == WIN2030_TBUID_0xF00) {
			tbu_num++;
		}
		else {
			pr_err("tbu power ctl failed!, Couldn't find tbu 0x%x\n", tbu_id);
			return -1;
		}
	}

	if (tbu_num == 0) {
		pr_err("Err,tbu NOT defined in dts!!!!\n");
		WARN_ON(1);
	}

	return ret;
}
EXPORT_SYMBOL(win2030_tbu_power_by_dev_and_node);

#define WAVE_TRIGGER_REG_OFFSET    0x668
#define WAVE_TRIGGER_REG_BASE     0x51810000
static void __iomem *trigger_reg_base;
static void trigger_waveform_ioremap_resource(void)
{
	trigger_reg_base = ioremap(WAVE_TRIGGER_REG_BASE, PAGE_SIZE);
}

void trigger_waveform_start(void)
{
	printk("trigger waveform capture!\n");

	writel(0x8000, trigger_reg_base + WAVE_TRIGGER_REG_OFFSET);
}
EXPORT_SYMBOL(trigger_waveform_start);

void trigger_waveform_stop(void)
{
	writel(0x0, trigger_reg_base + WAVE_TRIGGER_REG_OFFSET);
}
EXPORT_SYMBOL(trigger_waveform_stop);

void *tcu_base[MAX_NUMNODES] = {NULL};
#define TCU_NODE_STATUSn	0x9400
static int ioremap_tcu_resource(int nid)
{
	tcu_base[nid] = ioremap(0x50c00000 + nid*0x20000000, 0x40000);
	if (IS_ERR(tcu_base[nid])) {
		pr_err("failed to ioremap tcu reges\n");
		return PTR_ERR(tcu_base[nid]);
	}
	return 0;
}

static int get_tcu_node_status(unsigned long *tcu_node_status_p, int nid)
{
	unsigned long reg_val;
	unsigned long tcu_node_status = 0;
	int i;

	for (i = 0; i < 62; i++) {
		reg_val = readl(tcu_base[nid] + TCU_NODE_STATUSn + (4*i));
		tcu_node_status |= (reg_val & 0x1) << i;
	}
	*tcu_node_status_p = tcu_node_status;

	return 0;
}

void print_tcu_node_status(const char *call_name, int call_line, int nid)
{
	unsigned long tcu_node_status = 0;

	get_tcu_node_status(&tcu_node_status, nid);
	pr_info("%s:%d, (node %d) TCU_NODE_STATUS=0x%016lx\n",
		call_name, call_line, nid, tcu_node_status);
}
EXPORT_SYMBOL(print_tcu_node_status);

static int tcu_proc_show(struct seq_file *m, void *v)
{
	unsigned long tcu_node_status = 0;
	int nid,i;
	struct win2030_sid *mc;
	struct tbu_power_soc *tbu_power_soc_p;
	struct tbu_priv *tbu_priv_p;
	const struct win2030_tbu_client *tbu_client_p = NULL;
	struct tbu_attachment *a;

	seq_printf(m, "--------------------------------------------------------------------------------------------------------------------------------------------\n");
	seq_printf(m, "TCU bits(ID):|13(0x%02x)|12(0x%02x)|11(0x%2x)|10(0x%02x)|9(null) |8(0x%02x) |7(0x%02x) |6(0x%02x) |5(0x%02x) |4(0x%02x) |3(0x%02x) |2(0x%02x) |1(0x%02x) |0(0x%02x) |\n",
			WIN2030_TBUID_DSP3, WIN2030_TBUID_DSP2, WIN2030_TBUID_DSP1, WIN2030_TBUID_DSP0, WIN2030_TBUID_NPU, WIN2030_TBUID_SCPU,
			WIN2030_TBUID_PCIE, WIN2030_TBUID_DMA0, WIN2030_TBUID_JDEC, WIN2030_TBUID_JENC, WIN2030_TBUID_VENC, WIN2030_TBUID_VDEC, WIN2030_TBUID_ISP);
	seq_printf(m, "  Device(s)  |  DSP3  |  DSP2  |  DSP1  |  DSP0  |  null  |  NPU   |  SCPU  |  PCIE  |hsp DMAC|  JDEC  |  JENC  |  VENC  |  VDEC  |  ISP   |\n");
	seq_printf(m, "             |        |        |        |        |        |        |  CRYPT |        |  USB   |        |        |        |        |  DW    |\n");
	seq_printf(m, "             |        |        |        |        |        |        |aon DMAC|        |  ETH   |        |        |        |        |        |\n");
	seq_printf(m, "             |        |        |        |        |        |        |  LPCPU |        |  SATA  |        |        |        |        |        |\n");
	seq_printf(m, "             |        |        |        |        |        |        |        |        |  EMMC  |        |        |        |        |        |\n");
	seq_printf(m, "             |        |        |        |        |        |        |        |        |   SD   |        |        |        |        |        |\n");
	seq_printf(m, "--------------------------------------------------------------------------------------------------------------------------------------------\n");
	for (nid = 0; nid < g_nodes_cnt; nid++) {
		get_tcu_node_status(&tcu_node_status, nid);
		seq_printf(m, "(node %d) TCU:|", nid);
		for (i = 13; i >= 0; i--) {
			seq_printf(m, "   %d    |", (tcu_node_status&(1<<i))?1:0);
		}
		seq_printf(m, "\n");
	}
	seq_printf(m, "--------------------------------------------------------------------------------------------------------------------------------------------\n");

	for (nid = 0; nid < g_nodes_cnt; nid++) {
		seq_printf(m, "------(node %d) TBU attached device(s) list info:------\n", nid);
		mc = syscon_sid_cfg[nid];
		tbu_power_soc_p = mc->tbu_power_soc;
		tbu_priv_p = tbu_power_soc_p->tbu_priv_array;
		for (i = 0; i < tbu_power_soc_p->num_tbuClients; i++) {
			tbu_client_p = tbu_priv_p->tbu_client_p;
			if (tbu_client_p->tbu_power_ctl_register) {
				mutex_lock(&tbu_priv_p->tbu_priv_lock);
				seq_printf(m, "tbu(0x%02x) total refcnt %d, attached device(s):\n",
						tbu_priv_p->tbu_client_p->tbu_id, atomic_read(&tbu_priv_p->refcount));
				list_for_each_entry(a, &tbu_priv_p->attachments, list) {
					seq_printf(m, "  %-32s refcnt %d\n",dev_name(a->dev), atomic_read(&a->f_count));
				}
				seq_printf(m, "\n");
				// seq_printf(m, "<(node %d) end of tbu 0x%02x\n",
						// tbu_priv_p->nid, tbu_priv_p->tbu_client_p->tbu_id);
				mutex_unlock(&tbu_priv_p->tbu_priv_lock);
			}
			tbu_priv_p++;
		}
		seq_printf(m, "----------(node %d) end of list info-------------------\n", nid);
	}

	return 0;
}

static bool tcp_proc_exist = false;
static int __init tcu_proc_init(void)
{
	char proc_name[64];

	if (tcp_proc_exist)
		return 0;

	tcp_proc_exist = true;
	sprintf(proc_name, "%s_info", "tcu");
	pr_debug("%s, proc_name:%s\n", __func__, proc_name);
	if (NULL == proc_create_single_data(proc_name, 0, NULL, tcu_proc_show, NULL)) {
		return -1;
	}

	return 0;
}
