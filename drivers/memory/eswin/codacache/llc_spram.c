/*
 * ESWIN LLC_SPRAM on-chip SRAM allocation driver
 * Copyright 2023, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0-only
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
 */

#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/regmap.h>
#include <linux/io.h>
#include <linux/list_sort.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <uapi/linux/dma-heap.h>
#include <linux/dma-map-ops.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mfd/syscon.h>
#include <linux/memblock.h>
#include <linux/version.h>

#include <linux/eswin_npu.h>
#include <linux/regulator/consumer.h>
#include "llc_spram.h"

#define HAVE_LLC_HARDWARE	1

MODULE_IMPORT_NS(DMA_BUF);

#define DEVICE_NAME	"llc_spram"

#define SPRAM_GRANULARITY	PAGE_SIZE

static int npu_spram_size = 4 * 0x100000; //4M
module_param(npu_spram_size, int, 0644);
MODULE_PARM_DESC(npu_spram_size, "npu spram size");

struct platform_device *pdevs[2] = {NULL, NULL};
const static uint32_t npu_llc_offset[2] = {NPU_LLC0_OFFSET, NPU_LLC1_OFFSET};

static const struct of_device_id spram_dt_ids[] = {
	{ .compatible = "eswin,llc" },
	{}
};

static struct llc_cache_ops llc_ops = {
	.llc_flush_all = NULL,
};

struct llc_user {
	struct list_head node;
	const char *name;
};

struct llc_user_list {
	struct device *dev;
	atomic_t refcount;
	struct mutex ref_lock;
	struct list_head head;
};

static struct llc_user_list llc_user_lists[2];

struct spram_dev {
	struct device *dev;
	char *name;
	void __iomem *virt_base;
	phys_addr_t phys_addr;
	struct gen_pool *pool;
	void __iomem *npu_base; // The vaddr of the npu
	int nid;
	struct dma_heap *heap;
	struct clk *aclk;
	struct clk *cfg_clk;
	struct clk *llc_clk;
	struct clk *core_clk;
	struct clk *mux_u_npu_core_3mux1_gfree;
	struct clk *fixed_rate_clk_spll2_fout2;
	struct clk *fixed_rate_clk_spll1_fout1;
	struct reset_control *rstc_axi;
	struct reset_control *rstc_cfg;
	struct reset_control *rstc_core;
	struct reset_control *rstc_llc;
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
#define dma_buf_map		iosys_map
#define dma_buf_map_set_vaddr	iosys_map_set_vaddr
#define dma_buf_map_clear	iosys_map_clear
#endif
#define spram_phys_to_virt(spram, phys) \
	 (spram->virt_base + phys - spram->phys_addr)

static struct llc_reg_base  llc_base[MAX_LLC_CACHE_NODE_NUM];


static void llc_reg_write(struct spram_dev *spram, uint32_t addr, uint32_t value)
{
	writel(value, spram->npu_base + addr);
}

static uint32_t llc_reg_read(struct spram_dev *spram, uint32_t addr)
{
	return readl(spram->npu_base + addr);
}

static void llc_write(struct spram_dev *spram, uint32_t device, uint32_t addr, uint32_t value)
{
	llc_reg_write(spram, npu_llc_offset[device] + addr, value);
	dev_dbg(spram->dev, "%s:addr(0x%x)=0x%x\n", __func__, npu_llc_offset[device] + addr, value);
}

static uint32_t llc_read(struct spram_dev *spram, uint32_t device, uint32_t addr)
{
	return llc_reg_read(spram, npu_llc_offset[device] + addr);
}

static int llc_interleave_enable(struct spram_dev *spram)
{
	uint32_t regval, reg;
	struct regmap *regmap;
	struct device *dev = spram->dev;
	int ret = 0;

	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,syscfg");
	if (IS_ERR(regmap)) {
		dev_err(dev, "No syscfg phandle specified\n");
		return PTR_ERR(regmap);
	}

	ret = of_property_read_u32_index(dev->of_node, "eswin,syscfg", 1, &reg);
	if (ret) {
		dev_err(dev, "can't get llc interleave enable reg offset(%d)\n", ret);
		return ret;
	}

	ret = regmap_read(regmap, reg, &regval);
	if (ret) {
		dev_err(dev, "failed to read llc interleave enable reg(%d)\n", ret);
		return -EIO;
	}
	regval |= (1 << 0);
	ret = regmap_write(regmap, reg, regval);
	if (ret) {
		dev_err(dev, "failed to write llc interleave enable reg(%d)\n", ret);
		return -EIO;
	}

	ret = regmap_read(regmap, reg, &regval);
	if (!ret) {
		dev_dbg(dev, "write, interleave reg_offset=0x%x, val=0x%x\n", reg, regval);
	}

	return 0;

}

static int do_llc_spram_init(struct spram_dev *spram, uint32_t spram_size, uint32_t device)
{
	uint32_t val = 0;
	uint32_t num_of_ways = 0;
	uint32_t spram_num_of_ways = 0;

	dev_dbg(spram->dev, "---%s\n", __func__);

	//spramSzie must be intergral multiple of SIZE_OF_PER_WAY
	if((spram_size > MAX_CACHE_SIZE)   || (spram_size % SIZE_OF_PER_WAY)) {
		dev_err(spram->dev,"Invalid spramSize\n");
		return -1;
	}

	num_of_ways = MAX_CACHE_SIZE/SIZE_OF_PER_WAY;
	spram_num_of_ways = spram_size / SIZE_OF_PER_WAY;

	llc_write(spram, device, CODA_CACHE_REG_CCUCMWVR, GENMASK_ULL(num_of_ways - 1, 0));
	llc_write(spram, device, CODA_CACHE_REG_CCUCMCR, 0x0);
	do {
		val = llc_read(spram, device, CODA_CACHE_REG_CCUCMAR);
		msleep(1);
	}while(val & 0x1);

	llc_write(spram, device, CODA_CACHE_REG_CCUCMCR, 0x10000);
	do {
		val = llc_read(spram, device, CODA_CACHE_REG_CCUCMAR);
		msleep(1);
	}while(val & 0x1);

	llc_write(spram, device, CODA_CACHE_REG_CCUSPBR0, 0);
	llc_write(spram, device, CODA_CACHE_REG_CCUSPBR1, 0);

	if (spram_num_of_ways) {
		llc_write(spram, device, CODA_CACHE_REG_CCUSPCR0, (spram_num_of_ways - 1) << 16); // num of  ways are used as spram
		/*number of cachelines, taking 2048 sets as an example*/
		llc_write(spram, device, CODA_CACHE_REG_CCUSPCR1, MAX_SETS * spram_num_of_ways - 1);
		val = llc_read(spram, device, CODA_CACHE_REG_CCUSPCR0);
		val |= 0x1;      //enable Spram
		llc_write(spram, device, CODA_CACHE_REG_CCUSPCR0, val);
	}

	llc_write(spram, device, CODA_CACHE_REG_CCUCTCR, 0x3);     // enable codacache ip lookups and fill
	llc_write(spram, device, CODA_CACHE_REG_CCUUEDR, 0x3);     // enable codacache ip error detection

	/*  0xbf0007:enable codacache ip write allocation partial
	    0xff0007:enable codacache ip write-back Read and Write-allocate
	*/
	llc_write(spram, device, CODA_CACHE_REG_CCUCAOR, 0xff0007);

	return 0;
}

static int llc_spram_init(struct spram_dev *spram)
{
	dev_dbg(spram->dev, "---%s\n", __func__);

	if (llc_interleave_enable(spram) < 0)
	{
		dev_err(spram->dev, "llc_interleave_enable error\n");
		return -1;
	}

	if (do_llc_spram_init(spram, npu_spram_size / 2 , 0) < 0)
	{
		dev_err(spram->dev, "do_llc_spram_init0 error\n");
		return -1;
	}

	if (do_llc_spram_init(spram, npu_spram_size / 2, 1) < 0)
	{
		dev_err(spram->dev, "do_llc_spram_init1 error\n");
		return -1;
	}

	return 0;
}

int llc_flush_operation(unsigned long start, unsigned long len)
{
	if (unlikely(!llc_ops.llc_flush_all)) {
		pr_err("LLC cache ops is NULL!!!\n");
		return -1;
	}

	llc_ops.llc_flush_all(start, len);

	return 0;
}
EXPORT_SYMBOL(llc_flush_operation);

static void devm_llc_ops_unregister(struct device *dev, void *res)
{
	llc_ops.llc_flush_all = NULL;
	dev_info(dev, "%s done!\n", __func__);
}

static int devm_llc_ops_register(struct device *dev, llc_flush_all_t llc_flush_all)
{
	void *ptr;

	if (unlikely(!llc_flush_all))
		return -1;

	ptr = devres_alloc(devm_llc_ops_unregister, 0, GFP_KERNEL);
	if (!ptr)
		return -1;

	llc_ops.llc_flush_all = llc_flush_all;

	dev_info(dev, "%s, done sucessfully!\n", __func__);
	devres_add(dev, ptr);

	return 0;
}

#if defined(HAVE_LLC_HARDWARE)
static bool llc_maint_is_activity(onwhich_die_t node_id)
{
	if (((readl(llc_base[node_id].CodaCache0_RegBase + CODA_CACHE_REG_CCUCMAR) & 0x1) == 1) || (readl(llc_base[node_id].CodaCache0_RegBase + CODA_CACHE_REG_CCUCTAR) & 0x3))
		return true;

	if (((readl(llc_base[node_id].CodaCache1_RegBase + CODA_CACHE_REG_CCUCMAR) & 0x1) == 1) || (readl(llc_base[node_id].CodaCache1_RegBase + CODA_CACHE_REG_CCUCTAR) & 0x3))
		return true;

	return false;
}


void llc_flush_all(unsigned long start, unsigned long len)
{
	int node_id = 0;

	// mb();	/* sync */
	if (start >= CONFIG_RISCV_DIE0_CACHED_OFFSET && (start + len) <= (CONFIG_RISCV_DIE0_CACHED_OFFSET + CONFIG_RISCV_DIE0_MEM_MAX_SIZE)) {
		node_id = LLC_CACHE_NODE_0;
	}else if (start >= CONFIG_RISCV_DIE1_CACHED_OFFSET && (start + len) <= (CONFIG_RISCV_DIE1_CACHED_OFFSET + CONFIG_RISCV_DIE1_MEM_MAX_SIZE)) {
		node_id = LLC_CACHE_NODE_1;
	}
	else if (start >= CONFIG_RISCV_INTERLEAVE_CACHED_OFFSET && (start + len) <= (CONFIG_RISCV_INTERLEAVE_CACHED_OFFSET + CONFIG_RISCV_INTERLEAVE_MEM_MAX_SIZE)) {
		node_id = -1;;
	}
	else {
		WARN(1, "llc: out of range: %lx(%lx), skip flush\n",
		     start, len);
		return;
	}

	if (node_id == -1) {
		writeq(0x4, (llc_base[LLC_CACHE_NODE_0].CodaCache0_RegBase + CODA_CACHE_REG_CCUCMCR));
		writeq(0x4, (llc_base[LLC_CACHE_NODE_0].CodaCache1_RegBase + CODA_CACHE_REG_CCUCMCR));
		writeq(0x4, (llc_base[LLC_CACHE_NODE_1].CodaCache0_RegBase + CODA_CACHE_REG_CCUCMCR));
		writeq(0x4, (llc_base[LLC_CACHE_NODE_1].CodaCache1_RegBase + CODA_CACHE_REG_CCUCMCR));
		while (llc_maint_is_activity(LLC_CACHE_NODE_0)) {
			ndelay(100);
		}
		while (llc_maint_is_activity(LLC_CACHE_NODE_1)) {
			ndelay(100);
		}
	}
	else {
		writeq(0x4, (llc_base[node_id].CodaCache0_RegBase + CODA_CACHE_REG_CCUCMCR));
		writeq(0x4, (llc_base[node_id].CodaCache1_RegBase + CODA_CACHE_REG_CCUCMCR));
		while (llc_maint_is_activity(node_id)) {
			ndelay(100);
		}
	}
	pr_debug("---%s, node_id %d, start 0x%lx, len 0x%lx\n", __func__, node_id, start, len);
}
#else
static void llc_flush_all(unsigned long start, unsigned long len)
{
	pr_info("%s\n", __func__);
	return;
}
#endif

static void llc_user_init(struct spram_dev *spram)
{
	struct llc_user_list *llc_user_list = &llc_user_lists[spram->nid];

	llc_user_list->dev = spram->dev;
	atomic_set(&llc_user_list->refcount, 0);
	mutex_init(&llc_user_list->ref_lock);
	INIT_LIST_HEAD(&llc_user_list->head);

	dev_dbg(spram->dev, "%s\n", __func__);
}

static void llc_user_unregister(struct device *user_dev, void *res)
{
	int nid = dev_to_node(user_dev);
	struct llc_user *user, *tmp;
	struct llc_user_list *llc_user_list = NULL;

	if (nid == NUMA_NO_NODE) {
	#ifdef CONFIG_NUMA
		dev_err(user_dev, "%s:%d, NUMA_NO_NODE\n", __func__, __LINE__);
		return;
	#else
		dev_dbg(user_dev, "%s:%d, NUMA_NO_NODE, single DIE\n", __func__, __LINE__);
		nid = 0;
	#endif
	}
	llc_user_list = &llc_user_lists[nid];

	mutex_lock(&llc_user_list->ref_lock);
	list_for_each_entry_safe(user, tmp, &llc_user_list->head, node) {
		if (0 == strcmp(user->name, dev_name(user_dev))) {
			list_del(&user->node);
			kfree(user);
			atomic_sub(1, &llc_user_list->refcount);
			dev_dbg(user_dev, "llc_user_unregistered!\n");
			break;
		}
	}
	mutex_unlock(&llc_user_list->ref_lock);

	dev_dbg(user_dev, "%s done!\n", __func__);
}

int llc_user_register(struct device *user_dev)
{
	struct device *dev;
	int nid = dev_to_node(user_dev);
	struct llc_user_list *llc_user_list = NULL;
	struct llc_user *new_user = NULL;
	int ret = 0;
	void *ptr;

	if (nid == NUMA_NO_NODE) {
	#ifdef CONFIG_NUMA
		dev_err(user_dev, "%s:%d, NUMA_NO_NODE\n", __func__, __LINE__);
		return -EFAULT;
	#else
		dev_dbg(user_dev, "%s:%d, NUMA_NO_NODE, single DIE\n", __func__, __LINE__);
		nid = 0;
	#endif
	}

	llc_user_list = &llc_user_lists[nid];
	dev = llc_user_list->dev;

	mutex_lock(&llc_user_list->ref_lock);
	new_user = kzalloc(sizeof(*new_user), GFP_KERNEL);
	if (!new_user) {
		dev_err(dev, "%s failed to alloc user for %s\n", __func__, dev_name(user_dev));
		ret = -ENOMEM;
		goto llc_user_unlock;
	}
	new_user->name = dev_name(user_dev);
	list_add_tail(&new_user->node, &llc_user_list->head);
	atomic_add(1, &llc_user_list->refcount);

	ptr = devres_alloc(llc_user_unregister, 0, GFP_KERNEL);
	if (!ptr) {
		dev_err(dev, "%s devres_alloc failed for %s\n", __func__, dev_name(user_dev));
		kfree(new_user);
		ret = -ENOMEM;
		goto llc_user_unlock;
	}

	devres_add(user_dev, ptr);

llc_user_unlock:
	mutex_unlock(&llc_user_list->ref_lock);

	return ret;
}
EXPORT_SYMBOL(llc_user_register);

int npu_cfg_rst(int nid, bool enable)
{
	struct platform_device *pdev = pdevs[nid];
	struct spram_dev *spram;
	int ret;

	if (NULL == pdev) {
		pr_err("%s, Invalid node id:%d\n", __func__, nid);
		return -EINVAL;
	}
	spram = platform_get_drvdata(pdev);
	if (spram == NULL)
		return -EINVAL;

	if (true == enable) {
		/*reset npu cfg*/
		ret = reset_control_deassert(spram->rstc_cfg);
		WARN_ON(0 != ret);
	}
	else
		reset_control_assert(spram->rstc_cfg);

	return 0;
}
EXPORT_SYMBOL(npu_cfg_rst);

int npu_core_rst(int nid, bool enable)
{
	struct platform_device *pdev = pdevs[nid];
	struct spram_dev *spram;
	int ret;

	if (NULL == pdev) {
		pr_err("%s, Invalid node id:%d\n", __func__, nid);
		return -EINVAL;
	}
	spram = platform_get_drvdata(pdev);
	if (spram == NULL)
		return -EINVAL;

	if (true == enable) {
		/*reset npu cfg*/
		ret = reset_control_deassert(spram->rstc_core);
		WARN_ON(0 != ret);
	}
	else
		reset_control_assert(spram->rstc_core);

	return 0;
}
EXPORT_SYMBOL(npu_core_rst);

int llc_spram_avail_size(int nid, uint32_t *pSpramSize)
{
	struct platform_device *pdev = pdevs[nid];
	struct spram_dev *spram;

	if (NULL == pdev) {
		pr_err("%s, Invalid node id:%d\n", __func__, nid);
		return -EINVAL;
	}
	spram = platform_get_drvdata(pdev);
	if (spram == NULL)
		return -EINVAL;

	*pSpramSize = gen_pool_avail(spram->pool);

	return 0;
}
EXPORT_SYMBOL(llc_spram_avail_size);

static int spram_proc_show(struct seq_file *m, void *v)
{
	struct spram_dev *spram = m->private;
	int nid = spram->nid;
	struct llc_user_list *llc_user_list = &llc_user_lists[nid];
	struct llc_user *user;

	seq_printf(m, "SRAM pool: %zu KiB, Available: %zu KiB\n",
		gen_pool_size(spram->pool) / 1024,
		gen_pool_avail(spram->pool) / 1024);
#if 1
	seq_printf(m, "LLC Users(%d): \n", atomic_read(&llc_user_list->refcount));
	list_for_each_entry(user, &llc_user_list->head, node) {
		seq_printf(m, "%s\n", user->name);
	}
#endif
	return 0;
}

static int __init proc_spram_init(struct spram_dev *spram)
{
	char proc_name[64];

	sprintf(proc_name, "%s_info", spram->name);
	dev_dbg(spram->dev, "%s, proc_name:%s\n", __func__, proc_name);
	if (NULL == proc_create_single_data(proc_name, 0, NULL, spram_proc_show, spram)) {
		return -1;
	}

	return 0;
}

static int llc_clk_init(struct platform_device *pdev)
{
	struct spram_dev *spram = platform_get_drvdata(pdev);
	int ret = 0;

	if (spram == NULL)
		return -EINVAL;

	spram->aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(spram->aclk)) {
		ret = PTR_ERR(spram->aclk);
		spram->aclk = NULL;
		dev_err(&pdev->dev, "failed to get aclk: %d\n", ret);
		return ret;
	}

	spram->cfg_clk = devm_clk_get(&pdev->dev, "cfg_clk");
	if (IS_ERR(spram->cfg_clk)) {
		ret = PTR_ERR(spram->cfg_clk);
		spram->cfg_clk = NULL;
		dev_err(&pdev->dev, "failed to get cfg_clk: %d\n", ret);
		return ret;
	}

	spram->llc_clk = devm_clk_get(&pdev->dev, "llc_clk");
	if (IS_ERR(spram->llc_clk)) {
		ret = PTR_ERR(spram->llc_clk);
		spram->llc_clk = NULL;
		dev_err(&pdev->dev, "failed to get llc_clk: %d\n", ret);
		return ret;
	}



	spram->core_clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(spram->core_clk)) {
		ret = PTR_ERR(spram->core_clk);
		spram->core_clk = NULL;
		dev_err(&pdev->dev, "failed to get core_clk: %d\n", ret);
		return ret;
	}

	spram->mux_u_npu_core_3mux1_gfree = devm_clk_get(&pdev->dev, "mux_u_npu_core_3mux1_gfree");
	if (IS_ERR(spram->mux_u_npu_core_3mux1_gfree)) {
		ret = PTR_ERR(spram->mux_u_npu_core_3mux1_gfree);
		dev_err(&pdev->dev, "failed to get mux_u_npu_core_3mux1_gfree: %d\n", ret);
		return ret;
	}

	spram->fixed_rate_clk_spll2_fout2 = devm_clk_get(&pdev->dev, "fixed_rate_clk_spll2_fout2");
	if (IS_ERR(spram->fixed_rate_clk_spll2_fout2)) {
		ret = PTR_ERR(spram->fixed_rate_clk_spll2_fout2);
		dev_err(&pdev->dev, "failed to get fixed_rate_clk_spll2_fout2: %d\n", ret);
		return ret;
	}
	spram->fixed_rate_clk_spll1_fout1 =
		devm_clk_get(&pdev->dev, "fixed_rate_clk_spll1_fout1");
	if (IS_ERR(spram->fixed_rate_clk_spll1_fout1))
	{
		ret = PTR_ERR(spram->fixed_rate_clk_spll1_fout1);
		dev_err(&pdev->dev, "failed to get fixed_rate_clk_spll1_fout1: %d\n", ret);
		return ret;
	}

	return 0;
}

static int llc_clk_enable(struct platform_device *pdev)
{
	struct spram_dev *spram = platform_get_drvdata(pdev);
	int ret = 0;

	if (spram == NULL)
		return -EINVAL;

	/*enable clk*/
	ret = clk_prepare_enable(spram->aclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable aclk: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(spram->cfg_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable cfg_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(spram->llc_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable llc_clk: %d\n", ret);
		return ret;
	}
	ret = clk_prepare_enable(spram->core_clk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable core_clk: %d\n", ret);
		return ret;
	}

	return 0;
}

static int llc_rst_init(struct platform_device *pdev)
{
	struct spram_dev *spram = platform_get_drvdata(pdev);

	if (spram == NULL)
		return -EINVAL;

	spram->rstc_axi = devm_reset_control_get_optional(&pdev->dev, "axi");
	if (IS_ERR_OR_NULL(spram->rstc_axi)) {
		dev_err(&pdev->dev, "Failed to get cfg reset handle\n");
		return -EFAULT;
	}

	spram->rstc_cfg = devm_reset_control_get_optional(&pdev->dev, "cfg");
	if (IS_ERR_OR_NULL(spram->rstc_cfg)) {
		dev_err(&pdev->dev, "Failed to get llc reset handle\n");
		return -EFAULT;
	}

	spram->rstc_core = devm_reset_control_get_optional(&pdev->dev, "core");
	if (IS_ERR_OR_NULL(spram->rstc_core)) {
		dev_err(&pdev->dev, "Failed to get cfg reset handle\n");
		return -EFAULT;
	}

	spram->rstc_llc = devm_reset_control_get_optional(&pdev->dev, "llc");
	if (IS_ERR_OR_NULL(spram->rstc_llc)) {
		dev_err(&pdev->dev, "Failed to get llc reset handle\n");
		return -EFAULT;
	}

	return 0;
}

static int llc_clk_set_parent(struct platform_device *pdev, u8 *is_high_freq)
{
	int ret;
	struct spram_dev *spram = platform_get_drvdata(pdev);

	struct device_node *np;
	struct regulator *npu_regulator;
	struct device *dev = &pdev->dev;

	if (spram == NULL)
		return -EINVAL;
	np = of_node_get(dev->of_node);
	npu_regulator = devm_regulator_get_exclusive(dev, "NPU_SVCC");

	if ((NULL == npu_regulator) || (IS_ERR(npu_regulator)))
	{
		dev_warn(dev, "failed to get npu regulator\n");
		*is_high_freq = 0;
	}
	else
	{
		*is_high_freq = of_property_read_bool(np, "apply_npu_high_freq");
		dev_dbg(dev, "success to get npu regulator,apply_npu_high_freq:%d\n",
				 *is_high_freq);
	}
	if (1 == *is_high_freq)
	{
		regulator_set_voltage(npu_regulator, NPU_1P5G_VOLTAGE, NPU_1P5G_VOLTAGE);
		dev_dbg(dev, "set volt:%duV ret:%d\n", NPU_1P5G_VOLTAGE,ret);
		/* devm_regulator_put(npu_regulator); */
		mdelay(10);
		ret = clk_set_parent(spram->mux_u_npu_core_3mux1_gfree,
							 spram->fixed_rate_clk_spll1_fout1);
	}
	else
	{
		if (((NULL != npu_regulator)) && (!IS_ERR(npu_regulator)))
		{
			regulator_set_voltage(npu_regulator, NPU_DEFAULT_VOLTAGE, NPU_DEFAULT_VOLTAGE);
			dev_dbg(dev, "set volt:%duV ret:%d\n", NPU_1P5G_VOLTAGE,ret);
			/* devm_regulator_put(npu_regulator); */
			mdelay(10);
		}
		ret = clk_set_parent(spram->mux_u_npu_core_3mux1_gfree,
							 spram->fixed_rate_clk_spll2_fout2);
	}
	if (ret)
	{
		dev_err(&pdev->dev, "failed to set mux_u_npu_core_3mux1_gfree parent: %d\n",
				ret);
		return ret;
	}

	return 0;
}
static int llc_clk_set_frq(struct platform_device *pdev, u8 is_high_freq)
{
	int ret;
	unsigned long rate = 0;
	struct spram_dev *spram = platform_get_drvdata(pdev);

	if (spram == NULL)
		return -EINVAL;

	rate = clk_round_rate(spram->aclk, NPU_ACLK_RATE);
	ret = clk_set_rate(spram->aclk, rate);
	if (ret != 0)
	{
		dev_err(&pdev->dev, "failed to set aclk: %d\n", ret);
		return ret;
	}

	if (1 == is_high_freq)
	{
		rate = clk_round_rate(spram->llc_clk, NPU_LLC_CLK_1P5G_RATE);
		ret = clk_set_rate(spram->llc_clk, rate);

		if (ret != 0)
		{
			dev_err(&pdev->dev, "failed to set llc_clk: %d\n", ret);
			return ret;
		}
		rate = clk_round_rate(spram->core_clk, NPU_CORE_CLK_1P5G_RATE);
		ret = clk_set_rate(spram->core_clk, rate);
		if (ret != 0)
		{
			dev_err(&pdev->dev, "failed to set core_clk: %d\n", ret);
			return ret;
		}
	}
	else
	{
		rate = clk_round_rate(spram->llc_clk, NPU_LLC_CLK_RATE);

		ret = clk_set_rate(spram->llc_clk, rate);
		if (ret != 0)
		{
			dev_err(&pdev->dev, "failed to set llc_clk: %d\n", ret);
			return ret;
		}
		rate = clk_round_rate(spram->core_clk, NPU_CORE_CLK_RATE);
		ret = clk_set_rate(spram->core_clk, rate);
		if (ret != 0)
		{
			dev_err(&pdev->dev, "failed to set core_clk: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int llc_rst_deassert(struct platform_device *pdev)
{
	int ret = 0;
	struct spram_dev *spram = platform_get_drvdata(pdev);

	if (spram == NULL)
		return -EINVAL;

	/*reset npu bus*/
	ret = reset_control_deassert(spram->rstc_axi);
	WARN_ON(0 != ret);

	/*reset npu core*/
	ret = reset_control_deassert(spram->rstc_core);
	WARN_ON(0 != ret);

	/*reset npu llc*/
	ret = reset_control_deassert(spram->rstc_llc);
	WARN_ON(0 != ret);

	/*reset npu cfg*/
	ret = reset_control_deassert(spram->rstc_cfg);
	WARN_ON(0 != ret);

	return 0;
}

static int llc_clk_rst_print(struct platform_device *pdev)
{
	uint32_t regval[5];
	struct regmap *regmap;
	struct spram_dev *spram = platform_get_drvdata(pdev);
	struct device *dev;
	int ret = 0;

	if (spram == NULL)
		return -EINVAL;

	dev = spram->dev;
	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,syscrg_csr");
	if (IS_ERR(regmap)) {
		dev_err(dev, "No syscrg phandle specified\n");
		return PTR_ERR(regmap);
	}

	ret = regmap_read(regmap, 0x178, &regval[0]);
	if (ret) {
		dev_err(dev, "%s:%d, failed to read reg(%d)\n", __func__, __LINE__, ret);
		return -EIO;
	}

	/* llc clken*/
	ret = regmap_read(regmap, 0x17c, &regval[1]);
	if (ret) {
		dev_err(dev, "%s:%d, failed to read reg(%d)\n", __func__, __LINE__, ret);
		return -EIO;
	}

	ret = regmap_read(regmap, 0x180, &regval[2]);
	if (ret) {
		dev_err(dev, "%s:%d, failed to read reg(%d)\n", __func__, __LINE__, ret);
		return -EIO;
	}

	ret = regmap_read(regmap, 0x184, &regval[3]);
	if (ret) {
		dev_err(dev, "%s:%d, failed to read reg(%d)\n", __func__, __LINE__, ret);
		return -EIO;
	}

	ret = regmap_read(regmap, 0x418, &regval[4]);
	if (ret) {
		dev_err(dev, "%s:%d, failed to read reg(%d)\n", __func__, __LINE__, ret);
		return -EIO;
	}

	dev_info(dev, "[0x178]=0x%08x [0x17c]=0x%08x [0x180]=0x%08x [0x184]=0x%08x [0x418]=0x%08x\n",
			regval[0], regval[1], regval[2], regval[3],regval[4]);

	return 0;
}

/* Release the reset signal to let llc run */
static int llc_clk_rst_init(struct platform_device *pdev)
{
	int ret = 0;
	u8 is_high_freq = 0;

	dev_dbg(&pdev->dev, "---%s\n", __func__);

	ret = llc_clk_init(pdev);
	if(ret != 0){
		dev_err(&pdev->dev, "llc_clk_init error: %d\n", ret);
		return ret;
	}

	ret = llc_clk_set_parent(pdev, &is_high_freq);
	if(ret != 0){
		dev_err(&pdev->dev, "llc_clk_set_parent error: %d\n", ret);
		return ret;
	}

	ret = llc_rst_init(pdev);
	if(ret != 0){
		dev_err(&pdev->dev, "llc_rst_init error: %d\n", ret);
		return ret;
	}

	ret = llc_clk_set_frq(pdev, is_high_freq);
	if(ret != 0){
		dev_err(&pdev->dev, "llc_clk_set_frq error: %d\n", ret);
		return ret;
	}

	ret = llc_clk_enable(pdev);
	if(ret != 0){
		dev_err(&pdev->dev, "llc_clk_enable error: %d\n", ret);
		return ret;
	}

	llc_rst_deassert(pdev);

	llc_clk_rst_print(pdev);
	dev_dbg(&pdev->dev, "%s done successfully!\n", __func__);

	return ret;
}

static int llc_resource_parse(struct platform_device *pdev)
{
	struct spram_dev *spram = platform_get_drvdata(pdev);
	struct device* dev = &pdev->dev;
	struct resource *res;
	struct resource res_spram;
	struct device_node *spram_node;
	int nid = dev_to_node(dev);
	void __iomem *npu_base;
	struct page *page = NULL;
	phys_addr_t phys;
	int ret = 0;

	dev_dbg(dev, "----%s:%d\n", __func__, __LINE__);

	#ifdef CONFIG_NUMA
	nid = dev_to_node(dev);
	if (nid == NUMA_NO_NODE) {
		dev_err(dev, "%s:%d, numa-node-id was not defined!\n", __func__, __LINE__);
		return -EFAULT;
	}
	#else
	if (of_property_read_s32(dev->of_node, "numa-node-id", &nid)) {
		dev_err(dev, "%s:%d, numa-node-id was not defined!\n", __func__, __LINE__);
		return -EFAULT;
	}
	#endif
	spram->nid = nid;

	spram_node = of_parse_phandle(dev->of_node, "spram-region", 0);
	of_address_to_resource(spram_node, 0, &res_spram);

	if (npu_spram_size > resource_size(&res_spram)) {
		dev_err(dev, "Invalid spram size(0x%x), max spram size is 0x%x\n",
			npu_spram_size, (unsigned int)resource_size(&res_spram));
		ret = -EINVAL;
		goto out_spram_region;
	}

	dev_info(dev, "res_spram.start=0x%x, max_size=0x%x, configured size=0x%x\n",
		(unsigned int)res_spram.start,
		(unsigned int)resource_size(&res_spram),
		npu_spram_size);

	spram->phys_addr = res_spram.start;
	spram->virt_base = devm_ioremap(&pdev->dev, spram->phys_addr, npu_spram_size);
	if (spram->virt_base == NULL) {
		dev_err(dev, "failed to ioremap() spram-region\n");
		ret = -ENODEV;
		goto out_spram_region;
	}
	dev_dbg(dev, "---%s:%d, spram->virt_base=0x%px\n",
		__func__, __LINE__, spram->virt_base);

	page = phys_to_page(spram->phys_addr);
	phys = page_to_phys(page);
	dev_dbg(dev, "---%s:%d, spram->phys_addr=0x%x, phys_to_page->page_to_phys:0x%x\n",
		__func__, __LINE__, (unsigned int)spram->phys_addr, (unsigned int)phys);

	dev_dbg(dev, "%s:%d,---page:0x%px\n", __func__, __LINE__, page);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dev_dbg(&pdev->dev, "npu_base resource phys_start=0x%llx, size=0x%llx\n", res->start, resource_size(res));
	npu_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (IS_ERR(npu_base)) {
		dev_err(&pdev->dev, "could not map NPU registers\n");
		ret = PTR_ERR(npu_base);
		goto out_spram_region;
	}
	spram->npu_base = npu_base;

	llc_base[nid].CodaCache0_RegBase = npu_base + NPU_LLC0_OFFSET;
	llc_base[nid].CodaCache1_RegBase = npu_base + NPU_LLC1_OFFSET;

out_spram_region:
	of_node_put(spram_node);

	return ret;
}
struct spram_heap {
	struct dma_heap *heap;
	struct spram_dev *spram;
};

struct spram_heap_buffer {
	struct spram_heap *heap;
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	struct sg_table sg_table;
	int vmap_cnt;
	void *vaddr;
};

struct dma_heap_attachment {
	struct device *dev;
	/* For spram, sgt is useless infact, just to adapt to the dma-buf.c */
	struct sg_table *table;
	phys_addr_t phys_addr;
	struct list_head list;
	bool mapped;
};

static struct sg_table *dup_sg_table(struct sg_table *table)
{
	struct sg_table *new_table;
	int ret, i;
	struct scatterlist *sg, *new_sg;

	new_table = kzalloc(sizeof(*new_table), GFP_KERNEL);
	if (!new_table)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(new_table, table->orig_nents, GFP_KERNEL);
	if (ret) {
		kfree(new_table);
		return ERR_PTR(-ENOMEM);
	}

	new_sg = new_table->sgl;
	for_each_sgtable_sg(table, sg, i) {
		sg_set_page(new_sg, sg_page(sg), sg->length, sg->offset);
		new_sg = sg_next(new_sg);
	}

	return new_table;
}

static int spram_heap_attach(struct dma_buf *dmabuf,
			   struct dma_buf_attachment *attachment)
{
	struct spram_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a;
	struct sg_table *table;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	table = dup_sg_table(&buffer->sg_table);
	if (IS_ERR(table)) {
		kfree(a);
		return -ENOMEM;
	}

	a->table = table;
	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);
	a->mapped = false;

	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void spram_heap_detach(struct dma_buf *dmabuf,
			    struct dma_buf_attachment *attachment)
{
	struct spram_heap_buffer *buffer = dmabuf->priv;
	struct dma_heap_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(a->table);
	kfree(a->table);
	kfree(a);
}

static struct sg_table *spram_heap_map_dma_buf(struct dma_buf_attachment *attachment,
					     enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;
	struct sg_table *table = a->table;
	int ret;

	ret = dma_map_sgtable(attachment->dev, table, direction, DMA_ATTR_SKIP_CPU_SYNC);
	if (ret)
		return ERR_PTR(-ENOMEM);


	a->mapped = true;
	return table;
}

static void spram_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				      struct sg_table *table,
				      enum dma_data_direction direction)
{
	struct dma_heap_attachment *a = attachment->priv;

	a->mapped = false;
	dma_unmap_sgtable(attachment->dev, table, direction, DMA_ATTR_SKIP_CPU_SYNC);
}

static int spram_heap_dma_buf_begin_cpu_access(struct dma_buf *dmabuf,
						enum dma_data_direction direction)
{
	/* Do Nothing, because spram is uncached memory space */
	return 0;
}

static int spram_heap_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					      enum dma_data_direction direction)
{
	/* Do Nothing, because spram is uncached memory space */
	return 0;
}

static int spram_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct spram_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	unsigned long addr = vma->vm_start;
	struct sg_page_iter piter;
	int ret;

	/* vm_private_data will be used by eswin-ipc-scpu.c to get iova and
	   by mmz_vb.c to retrieve mem node
	*/
	vma->vm_private_data = dmabuf;

	for_each_sgtable_page(table, &piter, vma->vm_pgoff) {
		struct page *page = sg_page_iter_page(&piter);
		ret = remap_pfn_range(vma, addr, page_to_pfn(page), PAGE_SIZE,
				      vma->vm_page_prot);
		if (ret)
			return ret;
		addr += PAGE_SIZE;
		if (addr >= vma->vm_end)
			return 0;
	}
	return 0;
}

static void *spram_heap_do_vmap(struct spram_heap_buffer *buffer)
{
	struct sg_table *table = &buffer->sg_table;
	int npages = PAGE_ALIGN(buffer->len) / PAGE_SIZE;
	struct page **pages = vmalloc(sizeof(struct page *) * npages);
	struct page **tmp = pages;
	struct page *page;
	struct sg_page_iter piter;
	void *vaddr;

	if (!pages)
		return ERR_PTR(-ENOMEM);

	for_each_sgtable_page(table, &piter, 0) {
		WARN_ON(tmp - pages >= npages);
		*tmp++ = sg_page_iter_page(&piter);
		page = sg_page_iter_page(&piter);
	}

	vaddr = vmap(pages, npages, VM_MAP, PAGE_KERNEL);
	vfree(pages);

	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

static int spram_heap_vmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct spram_heap_buffer *buffer = dmabuf->priv;
	void *vaddr;
	int ret = 0;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		dma_buf_map_set_vaddr(map, buffer->vaddr);
		goto out;
	}

	vaddr = spram_heap_do_vmap(buffer);

	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto out;
	}

	buffer->vaddr = vaddr;
	buffer->vmap_cnt++;
	dma_buf_map_set_vaddr(map, buffer->vaddr);
out:
	mutex_unlock(&buffer->lock);

	return ret;
}

static void spram_heap_vunmap(struct dma_buf *dmabuf, struct dma_buf_map *map)
{
	struct spram_heap_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
	dma_buf_map_clear(map);
}

static void spram_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct spram_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table;
	struct spram_dev *spram = buffer->heap->spram;
	struct scatterlist *sg;
	int i;

	table = &buffer->sg_table;
	for_each_sgtable_sg(table, sg, i) {
		struct page *page = sg_page(sg);
		#ifdef WANT_PAGE_VIRTUAL
		void *vaddr = page_address(page);
		#else
		void *vaddr = spram_phys_to_virt(spram, page_to_phys(page));
		#endif
		gen_pool_free(spram->pool, (unsigned long)vaddr, page_size(page));
	}
	sg_free_table(table);
	kfree(buffer);
}

static const struct dma_buf_ops spram_heap_buf_ops = {
	.attach = spram_heap_attach,
	.detach = spram_heap_detach,
	.map_dma_buf = spram_heap_map_dma_buf,
	.unmap_dma_buf = spram_heap_unmap_dma_buf,
	.begin_cpu_access = spram_heap_dma_buf_begin_cpu_access,
	.end_cpu_access = spram_heap_dma_buf_end_cpu_access,
	.mmap = spram_heap_mmap,
	.vmap = spram_heap_vmap,
	.vunmap = spram_heap_vunmap,
	.release = spram_heap_dma_buf_release,
};

static int spram_noncontiguous_alloc(struct spram_dev *spram, size_t len, struct sg_table *table)
{
	struct gen_pool *pool = spram->pool;
	struct list_head pages;
	struct page *page, *tmp_page;
	struct scatterlist *sg;
	unsigned long size_remaining = len;
	phys_addr_t phys_addr;
	void *vaddr;
	int i, ret = -ENOMEM;

	INIT_LIST_HEAD(&pages);
	i = 0;
	while (size_remaining > 0) {
		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by SIGKILL
		 */
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto free_spram;
		}

		vaddr = gen_pool_dma_alloc(pool, PAGE_SIZE, &phys_addr);
		if (!vaddr)
			goto free_spram;

		page = phys_to_page(phys_addr);
		pr_debug("---%s:%d, phys_to_page->page_to_phys:0x%x,page:0x%px\n",
			__func__, __LINE__, (unsigned int)page_to_phys(page), page);
		/* page->virtual is used for recording the gen pool vaddr which is needed when releasing spram memory */
		#ifdef WANT_PAGE_VIRTUAL
		page->virtual = vaddr;
		set_page_address(page, vaddr);
		#endif

		list_add_tail(&page->lru, &pages);
		size_remaining -= PAGE_SIZE;
		i++;
	}

	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_spram;

	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, PAGE_SIZE, 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	return 0;
free_spram:
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		#ifdef WANT_PAGE_VIRTUAL
		vaddr = page_address(page);
		#else
		vaddr = spram_phys_to_virt(spram, page_to_phys(page));
		#endif
		gen_pool_free(pool, (unsigned long)vaddr, PAGE_SIZE);
	}
	return ret;
}

#if 0
static int spram_contiguous_alloc(struct spram_dev *spram, size_t len, struct sg_table *table)
{
	struct gen_pool *pool = spram->pool;
	int npages = PAGE_ALIGN(len) / PAGE_SIZE;
	void *vaddr, *page_vaddr;
	phys_addr_t phys_addr;
	struct page *page;
	struct scatterlist *sg;
	int i, ret = -ENOMEM;

	vaddr =  gen_pool_dma_alloc(pool, len, &phys_addr);
	if (!vaddr) {
		return ret;
	}

	if (sg_alloc_table(table, npages, GFP_KERNEL))
		goto free_spram;

	page_vaddr = vaddr;
	sg = table->sgl;
	for(i = 0; i < npages; i++) {
		page = phys_to_page(phys_addr);
		/* page->virtual is used for recording the gen pool vaddr which is needed when 
		* releasing spram memory
		*/
		#ifdef WANT_PAGE_VIRTUAL
		set_page_address(page, page_vaddr);
		#endif
		sg_set_page(sg, page, PAGE_SIZE, 0);
		sg = sg_next(sg);
		page_vaddr += PAGE_SIZE;
		phys_addr += PAGE_SIZE;
	}

	return 0;

free_spram:
	gen_pool_free(pool, (unsigned long)vaddr, len);
	return ret;
}
#endif

static struct dma_buf *spram_heap_allocate(struct dma_heap *heap,
					 unsigned long len,
					 unsigned long fd_flags,
					 unsigned long heap_flags)
{
	struct spram_heap *spram_heap = dma_heap_get_drvdata(heap);
	struct spram_dev *spram = spram_heap->spram;
	struct spram_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	struct sg_table *table;
	struct scatterlist *sg;
	int i, ret = -ENOMEM;

	if (!PAGE_ALIGNED(len)) {
		dev_err(spram->dev, "Err, request len is NOT aligned with PAGE_SIZE\n");
		return ERR_PTR(-EINVAL);
	}

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->heap = spram_heap;
	buffer->len = len;

	table = &buffer->sg_table;

	/* Todo: contiguous alloc flag is not permitted by dma-heap. */
#if 0
	if ((heap_flags & DMA_HEAP_VALID_HEAP_FLAGS) == HEAP_FLAGS_SPRAM_FORCE_CONTIGUOUS) {
		dev_dbg(spram->dev, "force contiguous allocation!\n");
		ret = spram_contiguous_alloc(spram, len, table);
	}
	else
#endif
	{
		dev_dbg(spram->dev, "non-contiguous allocation!\n");
		ret = spram_noncontiguous_alloc(spram, len, table);
	}

	if (ret) {
		dev_err(spram->dev, "failed to alloc spram, Errcode:%d\n", ret);
		return ERR_PTR(ret);
	}

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.ops = &spram_heap_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pages;
	}

	dev_dbg(spram->dev, "%s successfully!\n", __func__);

	return dmabuf;

free_pages:
	for_each_sgtable_sg(table, sg, i) {
		struct page *page = sg_page(sg);
		#ifdef WANT_PAGE_VIRTUAL
		void *vaddr = page_address(page);
		#else
		void *vaddr = spram_phys_to_virt(spram, page_to_phys(page));
		#endif
		gen_pool_free(spram->pool, (unsigned long)vaddr, PAGE_SIZE);
	}
	sg_free_table(table);
	kfree(buffer);

	dev_err(spram->dev, "%s:%d, failed to alloc spram buffer, ret=%d\n", __func__, __LINE__, ret);

	return ERR_PTR(ret);
}

static const struct dma_heap_ops spram_heap_ops = {
	.allocate = spram_heap_allocate,
};

static int __add_spram_heap(struct spram_dev *spram, void *data)
{
	struct spram_heap *spram_heap;
	struct dma_heap_export_info exp_info;

	spram_heap = devm_kzalloc(spram->dev, sizeof(*spram_heap), GFP_KERNEL);
	if (!spram_heap)
		return -ENOMEM;
	spram_heap->spram = spram;

	exp_info.name = spram->name;
	exp_info.ops = &spram_heap_ops;
	exp_info.priv = spram_heap;

	dev_dbg(spram->dev, "%s:%d, spram->name:%s\n", __func__, __LINE__, spram->name);

	spram_heap->heap = dma_heap_add(&exp_info);
	if (IS_ERR(spram_heap->heap)) {
		int ret = PTR_ERR(spram_heap->heap);

		return ret;
	}

	/* spram->heap is only for the purpose of deleting heap when driver removed */
	spram->heap = spram_heap->heap;

	dev_info(spram->dev, "%s, Done adding spram heap name:%s\n", __func__, exp_info.name);

	return 0;
}

static int llc_probe(struct platform_device *pdev)
{
	struct spram_dev *spram;
	int ret = 0;

	dev_info(&pdev->dev, "%s start!\n", __func__);

	spram = devm_kzalloc(&pdev->dev, sizeof(*spram), GFP_KERNEL);
	if (!spram)
		return -ENOMEM;

	spram->dev = &pdev->dev;

	platform_set_drvdata(pdev, spram);

	ret = llc_resource_parse(pdev);
	if (ret) {
		return ret;
	}

	#if defined(CONFIG_RISCV) && defined(HAVE_LLC_HARDWARE)
	/* Init llc controller */
	ret = llc_clk_rst_init(pdev);
	if (ret)
		return ret;

	ret = llc_spram_init(spram);
	if (ret) {
		return ret;
	}
	#endif
	if (devm_llc_ops_register(&pdev->dev, llc_flush_all)) {
		dev_err(&pdev->dev, "register llc ops failed!!!\n");
		return -EFAULT;
	}

	/* Create spram pool */

	spram->pool = devm_gen_pool_create(spram->dev, ilog2(SPRAM_GRANULARITY),
						dev_to_node(spram->dev), NULL);
	if (IS_ERR(spram->pool)) {
		dev_err(spram->dev, "devm_gen_pool_create() failed!!!\n");
		return PTR_ERR(spram->pool);
	}
	ret = gen_pool_add_virt(spram->pool, (unsigned long)spram->virt_base,
				spram->phys_addr, npu_spram_size, dev_to_node(spram->dev));
	if (ret < 0) {
		dev_err(spram->dev, "gen_pool_add_virt failed with %d\n", ret);
		return ret;
	}

	if (spram->pool)
		dev_info(spram->dev, "SRAM pool: %zu KiB @ vaddr 0x%px, phys 0x%x\n",
			gen_pool_size(spram->pool) / 1024, spram->virt_base, (unsigned int)spram->phys_addr);

	// vaddr = gen_pool_dma_alloc(spram->pool, PAGE_SIZE, &dma);
	// dev_info(spram->dev, "gen_pool_alloc, vaddr=0x%px, dma=0x%x\n", vaddr, dma);

	spram->name = devm_kasprintf(spram->dev, GFP_KERNEL, "%s%d", DEVICE_NAME, spram->nid);
	ret = __add_spram_heap(spram, NULL);
	if (ret) {
		dev_err(spram->dev, "failed to add spram heap\n");
		return ret;
	}

	llc_user_init(spram);

	proc_spram_init(spram);

	#ifdef WANT_PAGE_VIRTUAL
	dev_dbg(&pdev->dev, "WANT_PAGE_VIRTUAL is defined!\n");
	#else
	dev_dbg(&pdev->dev, "WANT_PAGE_VIRTUAL is NOT defined!\n");
	#endif

	#if defined(HAVE_LLC_HARDWARE)
	dev_dbg(&pdev->dev, "HAVE_LLC_HARDWARE is defined!\n");
	#else
	dev_dbg(&pdev->dev, "HAVE_LLC_HARDWARE is NOT defined!\n");
	#endif

	#if defined(CONFIG_RISCV)
	dev_dbg(&pdev->dev, "CONFIG_RISCV is defined!\n");
	#else
	dev_dbg(&pdev->dev, "CONFIG_RISCV is NOT defined!\n");
	#endif

	#if defined(CONFIG_RISCV) && defined(HAVE_LLC_HARDWARE)
	dev_dbg(&pdev->dev, "CONFIG_RISCV && HAVE_LLC_HARDWARE is defined!\n");
	#else
	dev_dbg(&pdev->dev, "CONFIG_RISCV && HAVE_LLC_HARDWARE is NOT defined!\n");
	#endif

	dev_info(&pdev->dev, "%s Done!\n", __func__);

	pdevs[spram->nid] = pdev;

	return 0;
}

static const struct of_device_id llc_dt_ids[] = {
	{ .compatible = "eswin,llc" },
	{}
};
MODULE_DEVICE_TABLE(of, llc_dt_ids);

// static struct platform_driver llc_driver __initdata = {
static struct platform_driver llc_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.of_match_table = llc_dt_ids,
	},
	.probe = llc_probe,
};

builtin_platform_driver(llc_driver);
MODULE_DESCRIPTION("ESWIN LLC driver");
MODULE_AUTHOR("Lin MIn <linmin@eswincomputing.com>");
MODULE_LICENSE("GPL");
