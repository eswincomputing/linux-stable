#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/version.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include <linux/dma-mapping.h>
#include <linux/eswin-win2030-sid-cfg.h>

#if defined(CONFIG_PM_DEVFREQ)
#include <linux/devfreq.h>
#include <linux/pm_opp.h>
#endif

#include "vc8000_driver.h"

#define LOG_TAG  VENC_DEV_NAME ":main"
#include "vc_drv_log.h"

#define VC_ACLK_HIGHEST                 1040000000
#define VENC_SYS_CLK_HIGHEST            800000000
#define VENC_MMU_AWSSID_OFF             0x600
#define VENC_MMU_ARSSID_OFF             0x604
#define JENC_MMU_AWSSID_OFF             0xa00
#define JENC_MMU_ARSSID_OFF             0xa04
#define MCPU_SP0_DYMN_CSR_EN_BIT        3
#define MCPU_SP0_DYMN_CSR_GNT_BIT       3

#define CORE_NAME_VE                    ("video-enc0")
#define CORE_NAME_JE                    ("jpeg-enc0")

typedef struct _venc_clk_rst {
	struct reset_control        *rstc_cfg;
	struct reset_control        *rstc_axi;
	struct reset_control        *rstc_moncfg;
	struct reset_control        *rstc_je_cfg;
	struct reset_control        *rstc_je_axi;
	struct reset_control        *rstc_ve_cfg;
	struct reset_control        *rstc_ve_axi;
	struct clk          *cfg_clk;
	struct clk          *aclk;
	struct clk          *je_clk;
	struct clk          *ve_clk;
	struct clk          *vc_mux;
	struct clk          *spll0_fout1;
	struct clk          *spll2_fout1;
	struct clk          *je_pclk;
	struct clk          *ve_pclk;
	struct clk          *mon_pclk;
} venc_clk_rst_t;

typedef struct {
	venc_clk_rst_t vcrt;

	u8 numa_id;
	atomic_t dev_close_gate;
	atomic_t dev_open_gate;
	u8 dev_closed;
} venc_dev_prvdata;

SUBSYS_CONFIG vc8000e_subsys_array[4] = {0};
CORE_CONFIG vc8000e_core_array[8] = {0};

struct platform_device *venc_pdev = NULL;
struct platform_device *venc_pdev_d1 = NULL;

static u32 vcmd_supported = 1;
module_param(vcmd_supported, uint, 0);

extern int hantroenc_normal_init(void);
extern void hantroenc_normal_cleanup(void);
extern int hantroenc_wait_core_idle(u32 core_id);
extern int vc8000e_vcmd_init(void);
extern int vc8000e_vcmd_cleanup(void);
extern int vc8000e_vcmd_wait_core_idle(u32 core_id);

static int venc_dev_open(struct device *dev);
static int venc_dev_close(struct device *dev);
static int venc_pm_enable(struct platform_device *pdev);
static void enc_pm_disable(struct platform_device *pdev);

extern VCMD_CONFIG vc8000e_vcmd_core_array[];
extern int venc_vcmd_core_num;

/** <TODO> the je & ve should be seperated as two devices*/
static u8 numa_id_array[4] = {0};
static struct platform_device *venc_get_platform_device(u32 core_id)
{
	struct platform_device *pdev = NULL;
	u8 numa_id = 0xff;

	if (core_id >= venc_vcmd_core_num) {
		LOG_ERR("invalid core_id = %u, venc_vcmd_core_num = %u\n", core_id, venc_vcmd_core_num);
		return NULL;
	}
	numa_id = numa_id_array[core_id];

	if (0 == numa_id)
		pdev = venc_pdev;
	else if (1 == numa_id)
		pdev = venc_pdev_d1;

	return pdev;
}

static int venc_wait_core_idle(u32 core_id) {
	if (0 == vcmd_supported) {
		return hantroenc_wait_core_idle(core_id);
	} else {
		return vc8000e_vcmd_wait_core_idle(core_id);
	}
}

/** <TODO> the je & ve should be seperated as two devices*/
static int venc_wait_device_idle(struct platform_device *pdev)
{
	int ret = 0;

	if (pdev == venc_pdev) {
		ret = venc_wait_core_idle(0);
		if (ret <= 0)
			return ret;
		ret = venc_wait_core_idle(1);
		return ret;
	}
	else if (pdev == venc_pdev_d1) {
		ret = venc_wait_core_idle(2);
		if (ret <= 0)
			return ret;
		ret = venc_wait_core_idle(3);
		return ret;
	}

	LOG_ERR("Unknown platform device = %p\n", pdev);
	return 1;
}

static int venc_device_node_scan(unsigned char *compatible)
{
	struct property *prop = NULL;
	struct device_node *np = NULL;

	np = of_find_compatible_node(NULL, NULL, compatible);
	if (!np) {
		LOG_INFO("Unable to find device %s", compatible);
		return -1;
	}

	for_each_property_of_node(np, prop) {
		if (!strcmp(prop->name, "status")) {
			if (!strcmp((char *)prop->value, "disabled")) {
				LOG_INFO("One venc device disabled on d2d\n");
				return -1;
			}
		}
	}

	of_node_put(np);

	return 0;
}

static int venc_device_nodes_check(void)
{
	int i = 0, venc_dev_num = 0;
	unsigned char compatible[32] = {0};

	for (i = 0; i < 2; i++) {
		sprintf(compatible, "eswin,video-encoder%d", i);
		if(!venc_device_node_scan(compatible))
			venc_dev_num++;
	}

	return venc_dev_num;
}

#define VENC_CORE_ARRAY_ASSIGN(index, id, type, addr, size, irqnum) do { \
		vc8000e_core_array[index].subsys_idx = id; \
		vc8000e_core_array[index].core_type = type; \
		vc8000e_core_array[index].offset = addr; \
		vc8000e_core_array[index].reg_size = size; \
		vc8000e_core_array[index].irq = irqnum; \
		index++; \
	} while (0)

static int venc_trans_device_nodes(struct platform_device *pdev, u8 numa_id)
{
	static int subsys_id = 0;
	static int core_index = 0;
	struct fwnode_handle *child = NULL;
	unsigned int vcmd_addr[2] = {0}, axife_addr[2] = {0}, venc_addr[2] = {0};

	if (of_property_read_u32_array(pdev->dev.of_node, "vcmd-core", vcmd_addr, 2)) {
		LOG_ERR("Encoder VCMD core not found\n");
		vcmd_supported = 0;
	}

	if (of_property_read_u32_array(pdev->dev.of_node, "axife-core", axife_addr, 2)) {
		LOG_ERR("AXIFE core not found\n");
		return -1;
	}

	if (of_property_read_u32_array(pdev->dev.of_node, "venc-core", venc_addr, 2)) {
		LOG_ERR("Encoder core not found\n");
		return -1;
	}

	device_for_each_child_node(&pdev->dev, child) {
		const char *core_name;
		int hw_type = CORE_VC8000E;
		unsigned int base_addr, child_irq;

		if (fwnode_property_read_string(child, "core-name", &core_name)) {
			LOG_ERR("Video encoder core name not found.\n");
			fwnode_handle_put(child);
			return -1;
		}

		if (fwnode_property_read_u32(child, "base-addr", &base_addr)) {
			LOG_ERR("Video encoder base addr not found.\n");
			fwnode_handle_put(child);
			return -1;
		}

		child_irq = fwnode_irq_get(child, 0);
		if (child_irq <= 0) {
			LOG_INFO("irq get failed, polling mode instead.\n");
			child_irq = -1;
		}

		vc8000e_subsys_array[subsys_id].base_addr = base_addr;
		vc8000e_subsys_array[subsys_id].iosize = 0x3000;
		vc8000e_subsys_array[subsys_id].resource_shared = 0;

		if (vcmd_supported) {
			venc_vcmd_core_num++;
			vc8000e_vcmd_core_array[subsys_id].vcmd_base_addr = base_addr + vcmd_addr[0];
			vc8000e_vcmd_core_array[subsys_id].vcmd_irq = child_irq;
		}

		if (strstr(core_name, "jpeg"))
			hw_type = CORE_VC8000EJ;

		numa_id_array[subsys_id] = numa_id;

		VENC_CORE_ARRAY_ASSIGN(core_index, subsys_id, hw_type, venc_addr[0], venc_addr[1], child_irq);
		VENC_CORE_ARRAY_ASSIGN(core_index, subsys_id, CORE_AXIFE, axife_addr[0], axife_addr[1], -1);
		subsys_id++;
	}

	return 0;
}

static int venc_sys_reset_init(struct platform_device *pdev, venc_clk_rst_t *vcrt)
{
	vcrt->rstc_cfg = devm_reset_control_get_shared(&pdev->dev, "cfg");
	if (IS_ERR_OR_NULL(vcrt->rstc_cfg)) {
		dev_err(&pdev->dev, "Failed to get cfg reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_axi = devm_reset_control_get_shared(&pdev->dev, "axi");
	if (IS_ERR_OR_NULL(vcrt->rstc_axi)) {
		dev_err(&pdev->dev, "Failed to get axi reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_moncfg = devm_reset_control_get_shared(&pdev->dev, "moncfg");
	if (IS_ERR_OR_NULL(vcrt->rstc_moncfg)) {
		dev_err(&pdev->dev, "Failed to get moncfg reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_je_cfg = devm_reset_control_get_optional(&pdev->dev, "je_cfg");
	if (IS_ERR_OR_NULL(vcrt->rstc_je_cfg)) {
		dev_err(&pdev->dev, "Failed to get je_cfg reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_je_axi = devm_reset_control_get_optional(&pdev->dev, "je_axi");
	if (IS_ERR_OR_NULL(vcrt->rstc_je_axi)) {
		dev_err(&pdev->dev, "Failed to get je_axi reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_ve_cfg = devm_reset_control_get_optional(&pdev->dev, "ve_cfg");
	if (IS_ERR_OR_NULL(vcrt->rstc_ve_cfg)) {
		dev_err(&pdev->dev, "Failed to get ve_cfg reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_ve_axi = devm_reset_control_get_optional(&pdev->dev, "ve_axi");
	if (IS_ERR_OR_NULL(vcrt->rstc_ve_axi)) {
		dev_err(&pdev->dev, "Failed to get ve_axi reset handle\n");
		return -EFAULT;
	}

	return 0;
}

static int venc_sys_reset_release(venc_clk_rst_t *vcrt)
{
	int ret = 0;

	ret = reset_control_deassert(vcrt->rstc_cfg);
	WARN_ON(0 != ret);

	ret = reset_control_deassert(vcrt->rstc_axi);
	WARN_ON(0 != ret);

	ret = reset_control_deassert(vcrt->rstc_moncfg);
	WARN_ON(0 != ret);

	ret = reset_control_reset(vcrt->rstc_je_cfg);
	WARN_ON(0 != ret);

	ret = reset_control_reset(vcrt->rstc_je_axi);
	WARN_ON(0 != ret);

	ret = reset_control_reset(vcrt->rstc_ve_cfg);
	WARN_ON(0 != ret);

	ret = reset_control_reset(vcrt->rstc_ve_axi);
	WARN_ON(0 != ret);

	return 0;
}

static int venc_sys_clk_init(struct platform_device *pdev, venc_clk_rst_t *vcrt)
{
	int ret = 0;

	vcrt->aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(vcrt->aclk)) {
		ret = PTR_ERR(vcrt->aclk);
		dev_err(&pdev->dev, "failed to get aclk: %d\n", ret);
		return ret;
	}

	vcrt->cfg_clk = devm_clk_get(&pdev->dev, "cfg_clk");
	if (IS_ERR(vcrt->cfg_clk)) {
		ret = PTR_ERR(vcrt->cfg_clk);
		dev_err(&pdev->dev, "failed to get cfg_clk: %d\n", ret);
		return ret;
	}

	vcrt->je_clk = devm_clk_get(&pdev->dev, "je_clk");
	if (IS_ERR(vcrt->je_clk)) {
		ret = PTR_ERR(vcrt->je_clk);
		dev_err(&pdev->dev, "failed to get je_clk: %d\n", ret);
		return ret;
	}

	vcrt->ve_clk = devm_clk_get(&pdev->dev, "ve_clk");
	if (IS_ERR(vcrt->ve_clk)) {
		ret = PTR_ERR(vcrt->ve_clk);
		dev_err(&pdev->dev, "failed to get ve_clk: %d\n", ret);
		return ret;
	}

	vcrt->vc_mux = devm_clk_get(&pdev->dev, "vc_mux");
	if (IS_ERR(vcrt->vc_mux)) {
		ret = PTR_ERR(vcrt->vc_mux);
		dev_err(&pdev->dev, "failed to get vc_mux: %d\n", ret);
		return ret;
	}

	vcrt->spll0_fout1 = devm_clk_get(&pdev->dev, "spll0_fout1");
	if (IS_ERR(vcrt->spll0_fout1)) {
		ret = PTR_ERR(vcrt->spll0_fout1);
		dev_err(&pdev->dev, "failed to get spll0_fout1: %d\n", ret);
		return ret;
	}

	vcrt->spll2_fout1 = devm_clk_get(&pdev->dev, "spll2_fout1");
	if (IS_ERR(vcrt->spll2_fout1)) {
		ret = PTR_ERR(vcrt->spll2_fout1);
		dev_err(&pdev->dev, "failed to get spll2_fout1: %d\n", ret);
		return ret;
	}

	vcrt->je_pclk = devm_clk_get(&pdev->dev, "je_pclk");
	if (IS_ERR(vcrt->je_pclk)) {
		ret = PTR_ERR(vcrt->je_pclk);
		dev_err(&pdev->dev, "failed to get je_pclk: %d\n", ret);
		return ret;
	}

	vcrt->ve_pclk = devm_clk_get(&pdev->dev, "ve_pclk");
	if (IS_ERR(vcrt->ve_pclk)) {
		ret = PTR_ERR(vcrt->ve_pclk);
		dev_err(&pdev->dev, "failed to get ve_pclk: %d\n", ret);
		return ret;
	}

	vcrt->mon_pclk = devm_clk_get(&pdev->dev, "mon_pclk");
	if (IS_ERR(vcrt->mon_pclk)) {
		ret = PTR_ERR(vcrt->mon_pclk);
		dev_err(&pdev->dev, "failed to get mon_pclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static int venc_sys_clk_enable(venc_clk_rst_t *vcrt)
{
	int ret = 0;
	long rate = 0;

	ret = clk_set_parent(vcrt->vc_mux, vcrt->spll2_fout1);
	if (ret < 0) {
		LOG_ERR("Video encoder: failed to set vc_mux parent: %d\n", ret);
		return ret;
	}

	rate = clk_round_rate(vcrt->aclk, VC_ACLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(vcrt->aclk, rate);
		if (ret) {
			LOG_ERR("Video encoder: failed to set aclk: %d\n", ret);
			return ret;
		}
		LOG_INFO("VE set aclk to %ldHZ\n", rate);
	} else {
		LOG_ERR("Video encoder: failed to round rate for aclk %ld\n", rate);
		return -1;
	}

	rate = clk_round_rate(vcrt->je_clk, VENC_SYS_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(vcrt->je_clk, rate);
		if (ret) {
			LOG_ERR("Video encoder: failed to set je_clk: %d\n", ret);
			return ret;
		}
		LOG_INFO("VE set je_clk to %ldHZ\n", rate);
	} else {
		LOG_ERR("Video encoder: failed to round rate for je_clk %ld\n", rate);
		return -1;
	}

	rate = clk_round_rate(vcrt->ve_clk, VENC_SYS_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(vcrt->ve_clk, rate);
		if (ret) {
			LOG_ERR("Video encoder: failed to set ve_clk: %d\n", ret);
			return ret;
		}
		LOG_INFO("VE set ve_clk to %ldHZ\n", rate);
	} else {
		LOG_ERR("Video encoder: failed to round rate for ve_clk %ld\n", rate);
		return -1;
	}

	ret = clk_prepare_enable(vcrt->aclk);
	if (ret) {
		LOG_ERR("Video encoder: failed to enable aclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->je_clk);
	if (ret) {
		LOG_ERR("Video encoder: failed to enable je_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->ve_clk);
	if (ret) {
		LOG_ERR("Video encoder: failed to enable ve_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->cfg_clk);
	if (ret) {
		LOG_ERR("Video encoder: failed to enable cfg_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->je_pclk);
	if (ret) {
		LOG_ERR("Video encoder: failed to enable je_pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->ve_pclk);
	if (ret) {
		LOG_ERR("Video encoder: failed to enable ve_pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->mon_pclk);
	if (ret) {
		LOG_ERR("Video encoder: failed to enable mon_pclk: %d\n", ret);
		return ret;
	}

	return 0;
}

#ifdef SUPPORT_DMA_HEAP
static int venc_clk_disable(venc_clk_rst_t *vcrt)
{
	clk_disable_unprepare(vcrt->ve_pclk);
	clk_disable_unprepare(vcrt->je_pclk);
	clk_disable_unprepare(vcrt->je_clk);
	clk_disable_unprepare(vcrt->ve_clk);
	clk_disable_unprepare(vcrt->mon_pclk);
	clk_disable_unprepare(vcrt->cfg_clk);
	clk_disable_unprepare(vcrt->aclk);

	return 0;
}

static int venc_hardware_reset(venc_clk_rst_t *vcrt)
{
	reset_control_assert(vcrt->rstc_je_cfg);
	reset_control_assert(vcrt->rstc_ve_cfg);
	reset_control_assert(vcrt->rstc_je_axi);
	reset_control_assert(vcrt->rstc_ve_axi);
	reset_control_assert(vcrt->rstc_moncfg);
	reset_control_assert(vcrt->rstc_axi);
	reset_control_assert(vcrt->rstc_cfg);

	return 0;
}

static int venc_smmu_dynm_sid_init(struct platform_device *pdev, u16 module_type)
{
	int ret;
	unsigned int reg_val, vccsr_addr[4] = {0};
	unsigned int dynm_csr_en_off, dynm_csr_gnt_off;
	struct regmap *regmap;
	void __iomem *venc_csr_reg = NULL;

	if (VCMD_TYPE_ENCODER != module_type && VCMD_TYPE_JPEG_ENCODER != module_type)
	{
		LOG_ERR("Unknown module type %u, while smmu sid init\n", module_type);
		return -1;
	}
	regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "eswin,syscfg");
	if (IS_ERR(regmap)) {
		dev_err(&pdev->dev, "No syscfg phandle specified\n");
		return PTR_ERR(regmap);
	}

	ret = of_property_read_u32_index(pdev->dev.of_node, "eswin,syscfg", 1, &dynm_csr_en_off);
	if (ret) {
		dev_err(&pdev->dev, "No dynm csr enable offset found\n");
		return -1;
	}

	ret = of_property_read_u32_index(pdev->dev.of_node, "eswin,syscfg", 2, &dynm_csr_gnt_off);
	if (ret) {
		dev_err(&pdev->dev, "No dynm csr gnt offset found\n");
		return -1;
	}

	if (of_property_read_u32_array(pdev->dev.of_node, "vccsr-reg", vccsr_addr, 4)) {
		dev_err(&pdev->dev, "vc csr region not found\n");
		return -1;
	}

	venc_csr_reg = ioremap(vccsr_addr[1], vccsr_addr[3]);
	if (!venc_csr_reg) {
		LOG_ERR("venc_csr_reg not initialized\n");
		return -1;
	}

	if (VCMD_TYPE_ENCODER == module_type) {
		writel(WIN2030_SID_VENC, (venc_csr_reg + VENC_MMU_AWSSID_OFF));
		writel(WIN2030_SID_VENC, (venc_csr_reg + VENC_MMU_ARSSID_OFF));
	} else {
		writel(WIN2030_SID_JENC, (venc_csr_reg + JENC_MMU_AWSSID_OFF));
		writel(WIN2030_SID_JENC, (venc_csr_reg + JENC_MMU_ARSSID_OFF));
	}

	regmap_read(regmap, dynm_csr_en_off, &reg_val);
	reg_val |= (1 << MCPU_SP0_DYMN_CSR_EN_BIT);
	regmap_write(regmap, dynm_csr_en_off, reg_val);

	while(1) {
		regmap_read(regmap, dynm_csr_gnt_off, &reg_val);
		reg_val &= (1 << MCPU_SP0_DYMN_CSR_GNT_BIT);
		if (reg_val)
			break;

		msleep(10);
	}

	regmap_read(regmap, dynm_csr_en_off, &reg_val);
	reg_val &= (~(1U << MCPU_SP0_DYMN_CSR_EN_BIT));
	regmap_write(regmap, dynm_csr_en_off, reg_val);

	return 0;
}
#endif /** end of SUPPORT_DMA_HEAP*/

/* Temporary using this func to do crg init for d1 */
static int venc_d1_clk_reset_init(void)
{
	void __iomem *d1_crg_reg = NULL;

	d1_crg_reg = ioremap(0x71828000, 0x1000);
	writel(0x80000020, (d1_crg_reg + 0x1c4));
	writel(0x30003f, (d1_crg_reg + 0x1d0));
	writel(0x80000020, (d1_crg_reg + 0x1d4));
	writel(0x80000020, (d1_crg_reg + 0x1e0));
	writel(0x7, (d1_crg_reg + 0x458));
	writel(0x3, (d1_crg_reg + 0x460));
	writel(0x3, (d1_crg_reg + 0x468));

	return 0;
}

static int enc_reset_core(struct device *dev, u16 module_type)
{
	venc_dev_prvdata *prvdata = dev_get_drvdata(dev);
	venc_clk_rst_t *vcrt = &prvdata->vcrt;
	int ret = 0;

	if (VCMD_TYPE_ENCODER == module_type) {
		ret = reset_control_reset(vcrt->rstc_ve_cfg);
		WARN_ON(0 != ret);
		ret = reset_control_reset(vcrt->rstc_ve_axi);
		WARN_ON(0 != ret);
	} else if (VCMD_TYPE_JPEG_ENCODER == module_type) {
		ret = reset_control_reset(vcrt->rstc_je_cfg);
		WARN_ON(0 != ret);
		ret = reset_control_reset(vcrt->rstc_je_axi);
		WARN_ON(0 != ret);
	} else {
		LOG_ERR("Unknown module type %u, while core reset\n", module_type);
		return -1;
	}

	return 0;
}

static int enc_tbu_power(struct device *dev, u16 mod_type, bool powerUp)
{
	struct device_node *chi = NULL;
	char *core_name_tag = "";

	if (VCMD_TYPE_ENCODER == mod_type)
		core_name_tag = CORE_NAME_VE;
	else
		core_name_tag = CORE_NAME_JE;
	for_each_child_of_node(dev->of_node, chi) {
		const char *core_name;
		if (of_property_read_string(chi, "core-name", &core_name)) {
			LOG_ERR("Sub dev core not found, mod_type = %u\n", mod_type);
			return -1;
		}

		if (!strcmp(core_name, core_name_tag)) {
			LOG_INFO("ve tbu power on = %u, mod_type = %u\n", powerUp, mod_type);
			win2030_tbu_power_by_dev_and_node(dev, chi, powerUp);
		}
	}

	return 0;
}

static int venc_pm_enable(struct platform_device *pdev) {
	/** enable runtime PM */
	WARN_ON(pm_runtime_enabled(&pdev->dev));
	pm_runtime_set_autosuspend_delay(&pdev->dev, 1000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static void enc_pm_disable(struct platform_device *pdev) {
	pm_runtime_disable(&pdev->dev);
}

static int venc_dev_open(struct device *dev)
{
	venc_dev_prvdata *prvdata = dev_get_drvdata(dev);
	venc_clk_rst_t *vcrt = &prvdata->vcrt;
	int ret = -1;

	if (atomic_dec_return(&prvdata->dev_open_gate) < 0) {
		LOG_DBG("The device is opening\n");
		atomic_inc(&prvdata->dev_open_gate);
		return 0;
	}
	ret = venc_sys_clk_enable(vcrt);
	if (ret) {
		LOG_ERR("open device, venc enable clock failed\n");
		goto end;
	}
	ret = enc_tbu_power(dev, VCMD_TYPE_ENCODER, true);
	if (ret != 0) {
		LOG_ERR("ve: open device, tbu power up failed\n");
		goto end;
	}
	ret = enc_tbu_power(dev, VCMD_TYPE_JPEG_ENCODER, true);
	if (ret != 0) {
		LOG_ERR("je: open device, tbu power up failed\n");
		goto end;
	}
	prvdata->dev_closed = 0;

end:
	LOG_DBG("dev open, numa_id = %u, ret = %d\n", prvdata->numa_id, ret);
	atomic_inc(&prvdata->dev_open_gate);
	return ret;
}

static int venc_dev_close(struct device *dev)
{
	struct platform_device *pdev = container_of(dev, struct platform_device, dev);
	venc_dev_prvdata *prvdata = dev_get_drvdata(dev);
	venc_clk_rst_t *vcrt = &prvdata->vcrt;
	int ret;

	if (atomic_dec_return(&prvdata->dev_close_gate) < 0) {
		LOG_DBG("The device is closing\n");
		atomic_inc(&prvdata->dev_close_gate);
		return 0;
	}
	/** check the device be idle*/
	ret = venc_wait_device_idle(pdev);
	if (0 == ret) {
		/** timeout*/
		LOG_ERR("Timeout for venc_suspend\n");
		ret = -ETIMEDOUT;
		goto end;
	} else if (ret < 0) {
		LOG_ERR("Interrupt triggered while venc_suspend\n");
		ret = -ERESTARTSYS;
		goto end;
	}
	ret = enc_tbu_power(dev, VCMD_TYPE_ENCODER, false);
	if (ret != 0) {
		LOG_ERR("ve: close device, tbu power down failed\n");
		goto end;
	}
	ret = enc_tbu_power(dev, VCMD_TYPE_JPEG_ENCODER, false);
	if (ret != 0) {
		LOG_ERR("je: close device, tbu power down failed\n");
		goto end;
	}
	ret = venc_clk_disable(vcrt);
	if (ret) {
		LOG_ERR("close device, venc disable clock failed\n");
		goto end;
	}
	prvdata->dev_closed = 1;

end:
	LOG_DBG("dev closed, numa_id = %u, ret = %d\n", prvdata->numa_id, ret);
	atomic_inc(&prvdata->dev_close_gate);
	return ret;
}

/** interface functions might be called by others files*/
int enc_pm_runtime_sync(u32 core_id) {
	struct platform_device *pdev = venc_get_platform_device(core_id);

	if (!pdev) {
		LOG_ERR("get platform device failed for pm sync, core_id = %u\n", core_id);
	}

	return pm_runtime_get_sync(&pdev->dev);
}

int enc_pm_runtime_put(u32 core_id) {
	struct platform_device *pdev = venc_get_platform_device(core_id);

	if (!pdev) {
		LOG_ERR("get platform device failed for pm put, core_id = %u\n", core_id);
	}

	return pm_runtime_put(&pdev->dev);
}

int enc_reset_system(u32 core_id) {
	struct platform_device *pdev = venc_get_platform_device(core_id);
	int ret = 0;
	u16 mod_type = vc8000e_vcmd_core_array[core_id].sub_module_type;

	if (!pdev) {
		LOG_ERR("get platform device failed for reset system, core_id = %u\n", core_id);
		return -1;
	}
	if (VCMD_TYPE_ENCODER != mod_type && VCMD_TYPE_JPEG_ENCODER != mod_type) {
		LOG_ERR("Unsupported module type %u, while reset system\n", mod_type);
		return -1;
	}
	/** enc tbu power down*/
	ret = enc_tbu_power(&pdev->dev, mod_type, false);
	if (ret != 0) {
		LOG_ERR("mod_type=%u: reset system tbu power down failed\n", mod_type);
		return -1;
	}
	/** enc core reset*/
	ret = enc_reset_core(&pdev->dev, mod_type);
	if (ret != 0) {
		LOG_ERR("mod_type=%u: core reset failed\n", mod_type);
		return -1;
	}
	/** enc tbu power up*/
	ret = enc_tbu_power(&pdev->dev, mod_type, true);
	if (ret != 0) {
		LOG_ERR("mod_type=%u: reset system tbu power up failed\n", mod_type);
		return -1;
	}
#ifdef SUPPORT_DMA_HEAP
	venc_smmu_dynm_sid_init(pdev, mod_type);
#endif

	return 0;
}
/** end of interface functions*/

#if defined(CONFIG_PM_DEVFREQ)
static int venc_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	int ret;
	venc_dev_prvdata *prvdata = dev_get_drvdata(dev);
	venc_clk_rst_t *vcrt = &prvdata->vcrt;

	LOG_DBG("%s:%d, dev = %p, freq = %lu\n", __func__, __LINE__, dev, *freq);
	*freq = clk_round_rate(vcrt->je_clk, *freq);
	if (*freq > 0) {
		ret = clk_set_rate(vcrt->je_clk, *freq);
		if (ret) {
			LOG_ERR("Video encoder: failed to set je_clk: %d\n", ret);
			return ret;
		}
		LOG_DBG("VE set je_clk to %ldHZ\n", *freq);
	} else {
		LOG_ERR("Video encoder: failed to round rate for je_clk %ld\n", *freq);
		return -1;
	}

	*freq = clk_round_rate(vcrt->ve_clk, *freq);
	if (*freq > 0) {
		ret = clk_set_rate(vcrt->ve_clk, *freq);
		if (ret) {
			LOG_ERR("Video encoder: failed to set ve_clk: %d\n", ret);
			return ret;
		}
		LOG_DBG("VE set ve_clk to %ldHZ\n", *freq);
	} else {
		LOG_ERR("Video encoder: failed to round rate for ve_clk %ld\n", *freq);
		return -1;
	}

	return 0;
}

static int venc_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	venc_dev_prvdata *prvdata = dev_get_drvdata(dev);
	venc_clk_rst_t *vcrt = &prvdata->vcrt;

	*freq = clk_get_rate(vcrt->ve_clk);

	return 0;
}

/** devfreq profile */
static struct devfreq_dev_profile venc_devfreq_profile = {
	.initial_freq = VENC_SYS_CLK_HIGHEST,
	.timer = DEVFREQ_TIMER_DELAYED,
	.polling_ms = 1000, /** Poll every 1000ms to monitor load */
	.target = venc_devfreq_target,
	.get_cur_freq = venc_devfreq_get_cur_freq,
};
#endif /** CONFIG_PM_DEVFREQ*/

static int hantro_venc_probe(struct platform_device *pdev)
{
	static int pdev_count = 0;
	int ret, numa_id, venc_dev_num = 0;
	venc_dev_prvdata *prvdata = devm_kzalloc(&pdev->dev, sizeof(venc_dev_prvdata), GFP_KERNEL);
	venc_clk_rst_t *vcrt = &prvdata->vcrt;
#if defined(CONFIG_PM_DEVFREQ)
	struct devfreq *df = NULL;
#endif

	venc_dev_num = venc_device_nodes_check();
	if (venc_dev_num <= 0) {
		LOG_ERR("Invalid video encoder device number\n");
		return -1;
	}

	platform_set_drvdata(pdev, (void *)prvdata);

	if(of_property_read_u32(pdev->dev.of_node, "numa-node-id", &numa_id)) {
		numa_id = 0;
	}

	LOG_INFO("initializing venc, numa id %d\n", numa_id);

#if defined(CONFIG_PM_DEVFREQ)
	/* Add OPP table from device tree */
	ret = dev_pm_opp_of_add_table(&pdev->dev);
	if (ret) {
		LOG_ERR("%s, %d, Failed to add OPP table\n", __func__, __LINE__);
		return -1;
	}
	df = devm_devfreq_add_device(&pdev->dev, &venc_devfreq_profile, "userspace", NULL);
	if (IS_ERR(df)) {
		LOG_ERR("%s, %d, add devfreq failed\n", __func__, __LINE__);
		return -1;
	}
#endif /** CONFIG_PM_DEVFREQ*/

	if (!numa_id) {
		ret = venc_sys_reset_init(pdev, vcrt);
		if (ret < 0) {
			LOG_ERR("venc: reset initialization failed");
			return -1;
		}

		ret = venc_sys_clk_init(pdev, vcrt);
		if (ret < 0) {
			LOG_ERR("venc: clk init failed");
			return -1;
		}

		ret = venc_sys_clk_enable(vcrt);
		if (ret < 0) {
			LOG_ERR("venc: clk enable failed");
			return -1;
		}

		ret = venc_sys_reset_release(vcrt);
		if (ret < 0) {
			LOG_ERR("venc: reset release failed");
			return -1;
		}
	} else {
		venc_d1_clk_reset_init();
	}

	ret = venc_trans_device_nodes(pdev, numa_id);
	if (ret < 0) {
		LOG_ERR("venc: dts parse failed");
		return -1;
	}

	if (!numa_id)
		venc_pdev = pdev;
	else
		venc_pdev_d1 = pdev;

#ifdef SUPPORT_DMA_HEAP
	ret = enc_tbu_power(&pdev->dev, VCMD_TYPE_ENCODER, true);
	if (ret != 0) {
		LOG_ERR("ve: tbu power up failed\n");
		return -1;
	}
	ret = enc_tbu_power(&pdev->dev, VCMD_TYPE_JPEG_ENCODER, true);
	if (ret != 0) {
		LOG_ERR("je: tbu power up failed\n");
		return -1;
	}
	ret = venc_smmu_dynm_sid_init(pdev, VCMD_TYPE_ENCODER);
	if (ret < 0) {
		LOG_ERR("ve: dynamic smmu sid set failed");
		return -1;
	}
	ret = venc_smmu_dynm_sid_init(pdev, VCMD_TYPE_JPEG_ENCODER);
	if (ret < 0) {
		LOG_ERR("je: dynamic smmu sid set failed");
		return -1;
	}

	of_dma_configure(&pdev->dev, pdev->dev.of_node, true);
	if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(41)))
		LOG_ERR("41bit esdma dev: No suitable DMA available\n");

	if (dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(41)))
		LOG_ERR("41bit esdma dev: No suitable DMA available\n");
#endif

	atomic_set(&prvdata->dev_open_gate, 1);
	atomic_set(&prvdata->dev_close_gate, 1);
	prvdata->dev_closed = 1;
	prvdata->numa_id = numa_id;

	pdev_count++;
	if (venc_dev_num > pdev_count) {
		LOG_INFO("VENC: The first core loaded, waiting for another...");
		return 0;
	}
	if (vcmd_supported == 0)
		ret = hantroenc_normal_init();
	else
		ret = vc8000e_vcmd_init();

	if (0 == ret) {
		if (venc_pdev && venc_pm_enable(venc_pdev) < 0) {
			LOG_WARN("venc: enable pm for venc_pdev failed\n");
		}
		if (venc_pdev_d1 && venc_pm_enable(venc_pdev_d1) < 0) {
			LOG_WARN("venc: enable pm for venc_pdev_d1 failed\n");
		}
	}

	return ret;
}

static int hantro_venc_remove(struct platform_device *pdev)
{
#ifdef SUPPORT_DMA_HEAP
	int ret;
	venc_clk_rst_t *vcrt;
#endif
	venc_dev_prvdata *prvdata = platform_get_drvdata(pdev);

	enc_pm_disable(pdev);
	if (vcmd_supported == 0)
		hantroenc_normal_cleanup();
	else
		vc8000e_vcmd_cleanup();

#ifdef SUPPORT_DMA_HEAP
	ret = enc_tbu_power(&pdev->dev, VCMD_TYPE_ENCODER, false);
	if (ret != 0) {
		LOG_ERR("ve: tbu power down failed\n");
		return -1;
	}
	ret = enc_tbu_power(&pdev->dev, VCMD_TYPE_JPEG_ENCODER, false);
	if (ret != 0) {
		LOG_ERR("je: tbu power down failed\n");
		return -1;
	}
	vcrt = &prvdata->vcrt;
	venc_hardware_reset(vcrt);
#endif

	return 0;
}

int venc_pm_runtime_sync(u32 core_id) {
	struct platform_device *pdev = venc_get_platform_device(core_id);

	if (!pdev) {
		LOG_ERR("get platform device failed for pm sync, core_id = %u\n", core_id);
	}

	return pm_runtime_get_sync(&pdev->dev);
}

int venc_pm_runtime_put(u32 core_id) {
	struct platform_device *pdev = venc_get_platform_device(core_id);

	if (!pdev) {
		LOG_ERR("get platform device failed for pm put, numa_id = %u\n", core_id);
	}

	return pm_runtime_put(&pdev->dev);
}

static int venc_runtime_suspend(struct device *dev) {
	LOG_DBG("runtime suspend\n");
	return venc_dev_close(dev);
}

static int venc_runtime_resume(struct device *dev) {
	LOG_DBG("runtime resume\n");
	return venc_dev_open(dev);
}

static int venc_suspend(struct device *dev) {
	LOG_DBG("generic suspend\n");
	if (pm_runtime_status_suspended(dev)) {
		LOG_DBG("generic suspend, venc is suspended already\n");
		return 0;
	}
	return venc_dev_close(dev);
}

static int venc_resume(struct device *dev) {
	LOG_DBG("generic resume\n");
	if (pm_runtime_status_suspended(dev)) {
		LOG_DBG("generic resume, venc is resumed already\n");
		return 0;
	}
	return venc_dev_open(dev);
}

static const struct dev_pm_ops venc_pm_ops = {
	SET_RUNTIME_PM_OPS(venc_runtime_suspend, venc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(venc_suspend, venc_resume)
};

static const struct of_device_id eswin_venc_match[] = {
    { .compatible = "eswin,video-encoder0", },
    { .compatible = "eswin,video-encoder1", },
	{/* sentinel */},
};

static struct platform_driver eswin_venc_driver = {
    .probe      = hantro_venc_probe,
    .remove     = hantro_venc_remove,
    .driver = {
        .name   = "Eswinenc",
        .of_match_table = eswin_venc_match,
		.pm = &venc_pm_ops,
    },
};

static int __init hantro_venc_init(void)
{
    return platform_driver_register(&eswin_venc_driver);
}

static void __exit hantro_venc_exit(void)
{
    platform_driver_unregister(&eswin_venc_driver);
}

module_init(hantro_venc_init);
module_exit(hantro_venc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ESWIN");
MODULE_DESCRIPTION("Eswin Venc Driver");
