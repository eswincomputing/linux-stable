
// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive composable cache controller Driver
 *
 * Copyright (C) 2018-2022 SiFive, Inc.
 *
 */
#define pr_fmt(fmt) "CCACHE: " fmt
#include <linux/of_reserved_mem.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/device.h>
#include <linux/bitfield.h>
#include <asm/cacheinfo.h>
#include <soc/sifive/sifive_ccache.h>
#include <asm/dma-noncoherent.h>
#define SIFIVE_CCACHE_DIRECCFIX_LOW 0x100
#define SIFIVE_CCACHE_DIRECCFIX_HIGH 0x104
#define SIFIVE_CCACHE_DIRECCFIX_COUNT 0x108
#define SIFIVE_CCACHE_DIRECCFAIL_LOW 0x120
#define SIFIVE_CCACHE_DIRECCFAIL_HIGH 0x124
#define SIFIVE_CCACHE_DIRECCFAIL_COUNT 0x128
#define SIFIVE_CCACHE_DATECCFIX_LOW 0x140
#define SIFIVE_CCACHE_DATECCFIX_HIGH 0x144
#define SIFIVE_CCACHE_DATECCFIX_COUNT 0x148
#define SIFIVE_CCACHE_DATECCFAIL_LOW 0x160
#define SIFIVE_CCACHE_DATECCFAIL_HIGH 0x164
#define SIFIVE_CCACHE_DATECCFAIL_COUNT 0x168
#define SIFIVE_CCACHE_CONFIG 0x00
#define SIFIVE_CCACHE_CONFIG_BANK_MASK GENMASK_ULL(7, 0)
#define SIFIVE_CCACHE_CONFIG_WAYS_MASK GENMASK_ULL(15, 8)
#define SIFIVE_CCACHE_CONFIG_SETS_MASK GENMASK_ULL(23, 16)
#define SIFIVE_CCACHE_CONFIG_BLKS_MASK GENMASK_ULL(31, 24)
#define SIFIVE_CCACHE_WAYENABLE 0x08
#define SIFIVE_CCACHE_ECCINJECTERR 0x40
#define SIFIVE_CCACHE_MAX_ECCINTR 4
#define SIFIVE_CCACHE_FLUSH64 0x200
#define SIFIVE_CCACHE_FLUSH64_LINE_LEN 64
#define SIFIVE_CCACHE_WAYMASK_OFFSET 0x800
enum {
	CACHE_NODE_0 = 0,
	CACHE_NODE_1,
	SHARE_CACHE_NODE_NUM,
};

struct ccache_flush_mem_info {
	int nid;
	int cpuid;
	eic770x_memory_type_t mem_type;
	phys_addr_t paddr;
	size_t size;
	unsigned long line;
};
static void __iomem *ccache_base[SHARE_CACHE_NODE_NUM];
DEFINE_PER_CPU(void __iomem* [SHARE_CACHE_NODE_NUM], per_cpu_ccache_base);

DEFINE_PER_CPU(struct ccache_flush_mem_info, per_cpu_ccache_flush_mem_info);

static void __iomem *zero_device_base[SHARE_CACHE_NODE_NUM] = {NULL};
static int g_irq[SHARE_CACHE_NODE_NUM][SIFIVE_CCACHE_MAX_ECCINTR];
static struct riscv_cacheinfo_ops ccache_cache_ops;
static int level;
enum {
	DIR_CORR = 0,
	DATA_CORR,
	DATA_UNCORR,
	DIR_UNCORR,
};
#ifdef CONFIG_DEBUG_FS
static struct dentry *sifive_test;
static ssize_t ccache_write(struct file *file, const char __user *data,
			    size_t count, loff_t *ppos)
{
	unsigned int val;
	if (kstrtouint_from_user(data, count, 0, &val))
		return -EINVAL;
	if ((val < 0xFF) || (val >= 0x10000 && val < 0x100FF))
		writel(val, ccache_base[0] + SIFIVE_CCACHE_ECCINJECTERR);
	else
		return -EINVAL;
	return count;
}
static const struct file_operations ccache_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = ccache_write
};
static void setup_sifive_debug(void)
{
	sifive_test = debugfs_create_dir("sifive_ccache_cache", NULL);
	debugfs_create_file("sifive_debug_inject_error", 0200,
			    sifive_test, NULL, &ccache_fops);
}
#endif
static void ccache_config_read(int node_id)
{
	u32 cfg;
	cfg = readl(ccache_base[node_id] + SIFIVE_CCACHE_CONFIG);
	pr_info("%llu banks, %llu ways, sets/bank=%llu, bytes/block=%llu\n",
		FIELD_GET(SIFIVE_CCACHE_CONFIG_BANK_MASK, cfg),
		FIELD_GET(SIFIVE_CCACHE_CONFIG_WAYS_MASK, cfg),
		BIT_ULL(FIELD_GET(SIFIVE_CCACHE_CONFIG_SETS_MASK, cfg)),
		BIT_ULL(FIELD_GET(SIFIVE_CCACHE_CONFIG_BLKS_MASK, cfg)));
	cfg = readl(ccache_base[node_id] + SIFIVE_CCACHE_WAYENABLE);
	pr_info("Node %d, index of the largest way enabled: %u\n", node_id, cfg);
}
#if IS_ENABLED(CONFIG_ARCH_ESWIN_EIC770X_SOC_FAMILY)
static void ccache_way_enable(int node_id)
{
	u32 cfg, val;
	cfg = readl(ccache_base[node_id] + SIFIVE_CCACHE_CONFIG);
	val = FIELD_GET(SIFIVE_CCACHE_CONFIG_WAYS_MASK, cfg);
	writel(val -1 , ccache_base[node_id] + SIFIVE_CCACHE_WAYENABLE);
}

#if IS_ENABLED(CONFIG_ARCH_ESWIN_EIC7702_SOC)
static inline void _do_ccache_flush64_range(void *arg)
{
	struct ccache_flush_mem_info *p_mem = (struct ccache_flush_mem_info *)arg;

	p_mem->size = p_mem->size + (p_mem->paddr % SIFIVE_CCACHE_FLUSH64_LINE_LEN);
	p_mem->paddr = ALIGN_DOWN(p_mem->paddr, SIFIVE_CCACHE_FLUSH64_LINE_LEN);

	// pr_debug("%s, mem_nid %d, cpuid %d(smp_processor_id %d)", __func__,
	// 	 nid, cpudid, smp_processor_id());
	mb();	/* sync */
	if (p_mem->mem_type == FLAT_DDR_MEM) {
		for (p_mem->line = p_mem->paddr; p_mem->line < p_mem->paddr + p_mem->size;
		p_mem->line += SIFIVE_CCACHE_FLUSH64_LINE_LEN) {
			writeq_cpu(p_mem->line, (per_cpu(per_cpu_ccache_base, p_mem->cpuid)[p_mem->nid] + SIFIVE_CCACHE_FLUSH64));
		}
	}
	else {
		for (p_mem->line = p_mem->paddr; p_mem->line < p_mem->paddr + p_mem->size;
		p_mem->line += SIFIVE_CCACHE_FLUSH64_LINE_LEN) {
			if((!(!(p_mem->line & 0x40000)))^(!(!(p_mem->line & 0x100)))) {
				writeq_cpu(p_mem->line, (per_cpu(per_cpu_ccache_base, p_mem->cpuid)[CACHE_NODE_1] + SIFIVE_CCACHE_FLUSH64));
			}
			else {
				writeq_cpu(p_mem->line, (per_cpu(per_cpu_ccache_base, p_mem->cpuid)[CACHE_NODE_0] + SIFIVE_CCACHE_FLUSH64));
			}
		}
	}
	mb();
}
static void ccache_flush64_range(phys_addr_t paddr, size_t size)
{
	int nid;
	int cpuid;
	eic770x_memory_type_t mem_type;
	struct ccache_flush_mem_info *p_flush_mem_info;

	cpuid = smp_processor_id();
	arch_get_mem_node_and_type(phys_to_pfn(paddr), &nid, &mem_type);
	if (likely(mem_type == FLAT_DDR_MEM) || (mem_type == INTERLEAVE_DDR_MEM)) {
		p_flush_mem_info = &per_cpu(per_cpu_ccache_flush_mem_info, cpuid);
		p_flush_mem_info->nid = nid;
		p_flush_mem_info->cpuid = cpuid;
		p_flush_mem_info->mem_type = mem_type;
		p_flush_mem_info->paddr = paddr;
		p_flush_mem_info->size = size;
		_do_ccache_flush64_range(p_flush_mem_info);
	}
	else {
		WARN(1, "Sifive ccache: flush64 out of range: %llx(%lx), skip flush\n",
		     paddr, size);
		return;
	}
}
#else
static void ccache_flush64_range(phys_addr_t paddr, size_t size)
{
	unsigned long line;
	size = size + (paddr % SIFIVE_CCACHE_FLUSH64_LINE_LEN);
	paddr = ALIGN_DOWN(paddr, SIFIVE_CCACHE_FLUSH64_LINE_LEN);
	mb();	/* sync */
	if (paddr >= CONFIG_RISCV_DIE0_CACHED_OFFSET && (paddr + size) <= (CONFIG_RISCV_DIE0_CACHED_OFFSET + CONFIG_RISCV_DIE0_MEM_MAX_SIZE)) {
		for (line = paddr; line < paddr + size;
		line += SIFIVE_CCACHE_FLUSH64_LINE_LEN) {
			writeq_cpu(line, (ccache_base[CACHE_NODE_0] + SIFIVE_CCACHE_FLUSH64));
		}
		mb();
	}
	else {
		WARN(1, "Sifive ccache: flush64 out of range: %llx(%lx), skip flush\n",
		     paddr, size);
		return;
	}
}
#endif

static const struct riscv_nonstd_cache_ops ccache_cmo_ops __initdata = {
	.wback = &ccache_flush64_range,
	.inv = &ccache_flush64_range,
	.wback_inv = &ccache_flush64_range,
};
#endif //IS_ENABLED(CONFIG_ARCH_ESWIN_EIC770X_SOC_FAMILY)
static const struct of_device_id sifive_ccache_ids[] = {
	{ .compatible = "sifive,fu540-c000-ccache" },
	{ .compatible = "sifive,fu740-c000-ccache" },
	{ .compatible = "sifive,ccache0" },
	{ /* end of table */ }
};
static ATOMIC_NOTIFIER_HEAD(ccache_err_chain);
int register_sifive_ccache_error_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&ccache_err_chain, nb);
}
EXPORT_SYMBOL_GPL(register_sifive_ccache_error_notifier);
int unregister_sifive_ccache_error_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&ccache_err_chain, nb);
}
EXPORT_SYMBOL_GPL(unregister_sifive_ccache_error_notifier);
static int ccache_largest_wayenabled(void)
{
	return readl(ccache_base[0] + SIFIVE_CCACHE_WAYENABLE) & 0xFF;
}
static ssize_t number_of_ways_enabled_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return sprintf(buf, "%u\n", ccache_largest_wayenabled());
}
static DEVICE_ATTR_RO(number_of_ways_enabled);
static struct attribute *priv_attrs[] = {
	&dev_attr_number_of_ways_enabled.attr,
	NULL,
};
static const struct attribute_group priv_attr_group = {
	.attrs = priv_attrs,
};
static const struct attribute_group *ccache_get_priv_group(struct cacheinfo
							   *this_leaf)
{
	/* We want to use private group for composable cache only */
	if (this_leaf->level == level)
		return &priv_attr_group;
	else
		return NULL;
}
static irqreturn_t ccache_int_handler(int irq, void *device)
{
	unsigned int add_h, add_l;
	int node_id = *(int *)device;
	if (irq == g_irq[node_id][DIR_CORR]) {
		add_h = readl(ccache_base[node_id] + SIFIVE_CCACHE_DIRECCFIX_HIGH);
		add_l = readl(ccache_base[node_id] + SIFIVE_CCACHE_DIRECCFIX_LOW);
		pr_err("DirError @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DirError interrupt sig */
		readl(ccache_base[node_id] + SIFIVE_CCACHE_DIRECCFIX_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_CE,
					   "DirECCFix");
	}
	if (irq == g_irq[node_id][DIR_UNCORR]) {
		add_h = readl(ccache_base[node_id] + SIFIVE_CCACHE_DIRECCFAIL_HIGH);
		add_l = readl(ccache_base[node_id] + SIFIVE_CCACHE_DIRECCFAIL_LOW);
		/* Reading this register clears the DirFail interrupt sig */
		readl(ccache_base[node_id] + SIFIVE_CCACHE_DIRECCFAIL_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_UE,
					   "DirECCFail");
		panic("CCACHE: DirFail @ 0x%08X.%08X\n", add_h, add_l);
	}
	if (irq == g_irq[node_id][DATA_CORR]) {
		add_h = readl(ccache_base[node_id] + SIFIVE_CCACHE_DATECCFIX_HIGH);
		add_l = readl(ccache_base[node_id] + SIFIVE_CCACHE_DATECCFIX_LOW);
		pr_err("DataError @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DataError interrupt sig */
		readl(ccache_base[node_id] + SIFIVE_CCACHE_DATECCFIX_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_CE,
					   "DatECCFix");
	}
	if (irq == g_irq[node_id][DATA_UNCORR]) {
		add_h = readl(ccache_base[node_id] + SIFIVE_CCACHE_DATECCFAIL_HIGH);
		add_l = readl(ccache_base[node_id] + SIFIVE_CCACHE_DATECCFAIL_LOW);
		pr_err("DataFail @ 0x%08X.%08X\n", add_h, add_l);
		/* Reading this register clears the DataFail interrupt sig */
		readl(ccache_base[node_id] + SIFIVE_CCACHE_DATECCFAIL_COUNT);
		atomic_notifier_call_chain(&ccache_err_chain,
					   SIFIVE_CCACHE_ERR_TYPE_UE,
					   "DatECCFail");
	}
	return IRQ_HANDLED;
}

#if IS_ENABLED(CONFIG_ARCH_ESWIN_EIC770X_SOC_FAMILY)
static const struct of_device_id zero_device_id[] = {
	{ .compatible = "l3,zero-device"},
	{ /* sentinel */ }
};

static int zero_device_init(struct device_node *root, int nid)
{
	struct device_node *child = NULL;
	struct device_node *np;
	const struct of_device_id *match;
	struct resource res;
	struct reserved_mem *rmem;
	int len;
	int ret = 0;

	for_each_available_child_of_node(root, child) {
		match = of_match_node(zero_device_id, child);
		if (match) {
			if (!of_get_property(child, "reg", &len))
			{
				np = of_parse_phandle(child, "memory-region", 0);
				if (!np)
					return -ENODEV;

				rmem = of_reserved_mem_lookup(np);
				if (!rmem)
					return -ENODEV;

				pr_info("zero_device %s: base 0x%llx, size 0x%llx\n", rmem->name, rmem->base, rmem->size);
				zero_device_base[nid] = ioremap(rmem->base, rmem->size);
				if (IS_ERR(zero_device_base[nid])) {
					pr_err("failed to ioremap zero device\n");
					of_node_put(child);
					ret = PTR_ERR(zero_device_base[nid]);
					break;
				}
				pr_debug("ioremp zero_device phys 0x%llx, vaddr 0x%lx\n", rmem->base, (unsigned long)zero_device_base[nid]);
			}
			else
			{
				if (of_address_to_resource(child, 0, &res)) {
					of_node_put(child);
					ret = -ENODEV;
					pr_err("failed to get zero_device resource\n");
					break;
				}
				pr_info("zero_device resource: start 0x%llx, size 0x%llx\n", res.start, resource_size(&res));
				zero_device_base[nid] = ioremap(res.start, resource_size(&res));
				if (IS_ERR(zero_device_base[nid])) {
					pr_err("failed to ioremap zero device\n");
					of_node_put(child);
					ret = PTR_ERR(zero_device_base[nid]);
					break;
				}
				pr_debug("ioremp zero_device phys 0x%llx, vaddr 0x%lx\n", res.start, (unsigned long)zero_device_base[nid]);
			}
		}
	}

	return ret;
}

#define SIFIVE_CCACHE_SETS	4096
#define SIFIVE_CCACHE_TAG_SHIFT	(18)
void ccache_flush_all(void *arg)
{
	int hartid, masterid;
	int nid, wayIdx, i;
	void __iomem *zero_dev_vaddr;
	void __iomem *waymaskN_addr;
	u64 zero_val;
#if 0
	int cpuid;
	cpuid = smp_processor_id();
	hartid = cpuid_to_hartid_map(cpuid);
	if (hartid != *(int *)arg) {
		pr_err("%s:%d, invalid para!!!, hartid:%d, arg:%d\n", __func__, __LINE__,
			hartid, *(int *)arg);
	}
#else
	hartid = *(int *)arg;
#endif
	if (hartid > 3) {
		masterid = hartid - 4 + 1;
		nid = 1;
	}
	else {
		masterid = hartid + 1 ;
		nid = 0;
	}
	pr_debug("nid %d, hartid %d, masterid %d\n", nid, hartid, masterid);
	if (NULL !=zero_device_base[nid]) {
		waymaskN_addr = ccache_base[nid] + SIFIVE_CCACHE_WAYMASK_OFFSET + (masterid << 3);
		for (wayIdx = 0; wayIdx < 16; wayIdx++) {
			/* write WayMaskN to allow evictions from only one way */
			writel_cpu((1<<wayIdx), waymaskN_addr);
			mb();

			/* store all the sets in this way*/
			zero_dev_vaddr = zero_device_base[nid] + (wayIdx << SIFIVE_CCACHE_TAG_SHIFT);
			for (i = 0; i < SIFIVE_CCACHE_SETS; i++) {
				zero_val = readq_cpu(zero_dev_vaddr);
				zero_dev_vaddr += SIFIVE_CCACHE_FLUSH64_LINE_LEN;
			}
			mb();
		}
		/* restore the wayMaskN register to the original value*/
		writel_cpu(0xffff, waymaskN_addr);
		mb();
	}
}
#endif

static void ccache_per_cpu_data_init(int nid)
{
	int i;

	for_each_online_cpu(i) {
		per_cpu(per_cpu_ccache_base, i)[nid] = ccache_base[nid];
		pr_debug("---%s, cpu %d, per_cpu_ccache_base[%d] addr 0x%llx\n",
			__func__, i, nid,
			(u64)__pa(&per_cpu(per_cpu_ccache_base, i)[nid]));
	}
}

static int node_index = 0;
static int __init sifive_ccache_init(void)
{
	struct device_node *np;
	struct resource res;
	int i, rc, intr_num;
	for_each_matching_node(np, sifive_ccache_ids) {
		if (!np)
			return -ENODEV;
		if (of_address_to_resource(np, 0, &res)) {
			rc = -ENODEV;
			goto err_node_put;
		}
		if (of_property_read_u32(np, "numa-node-id", &node_index))
			return -ENODEV;
		if (node_index >= SHARE_CACHE_NODE_NUM) {
			rc = -ENODEV;
			goto err_node_put;
		}
		ccache_base[node_index] = ioremap(res.start, resource_size(&res));
		if (!ccache_base[node_index]) {
			rc = -ENOMEM;
			goto err_node_put;
		}
		if (of_property_read_u32(np, "cache-level", &level)) {
			rc = -ENOENT;
			goto err_unmap;
		}
		intr_num = of_property_count_u32_elems(np, "interrupts");
		if (!intr_num) {
			pr_err("No interrupts property\n");
			rc = -ENODEV;
			goto err_unmap;
		}
		for (i = 0; i < intr_num; i++) {
			g_irq[node_index][i] = irq_of_parse_and_map(np, i);
			rc = request_irq(g_irq[node_index][i], ccache_int_handler, 0, "ccache_ecc",
					&node_index);
			if (rc) {
				pr_err("Could not request IRQ %d\n", g_irq[node_index][i]);
				goto err_free_irq;
			}
		}
		#if IS_ENABLED(CONFIG_ARCH_ESWIN_EIC770X_SOC_FAMILY)
		rc = zero_device_init(np, node_index);
		if (rc)
			goto err_free_irq;
		#endif
		of_node_put(np);
		#if IS_ENABLED(CONFIG_ARCH_ESWIN_EIC770X_SOC_FAMILY)
		ccache_way_enable(node_index);
		riscv_noncoherent_register_cache_ops(&ccache_cmo_ops);
		#endif
		ccache_config_read(node_index);
		ccache_cache_ops.get_priv_group = ccache_get_priv_group;
		riscv_set_cacheinfo_ops(&ccache_cache_ops);
	#ifdef CONFIG_DEBUG_FS
		setup_sifive_debug();
	#endif
		ccache_per_cpu_data_init(node_index);
	}
	return 0;
err_free_irq:
	while (--i >= 0)
		free_irq(g_irq[node_index][i], NULL);
err_unmap:
	iounmap(ccache_base[node_index]);
err_node_put:
	of_node_put(np);
	return rc;
}
device_initcall(sifive_ccache_init);
