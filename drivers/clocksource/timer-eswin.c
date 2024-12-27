// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ESWIN timer driver
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
 * Authors: Xuxiang <xuxiang@eswincomputing.com>
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/mfd/stm32-lptimer.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/reset.h>
#include <linux/miscdevice.h>

#define APBT_MIN_PERIOD         4
#define APBT_MIN_DELTA_USEC     200

#define APBTMR_N_LOAD_COUNT     0x00
#define APBTMR_N_CURRENT_VALUE  0x04
#define APBTMR_N_CONTROL        0x08
#define APBTMR_N_EOI            0x0c
#define APBTMR_N_INT_STATUS     0x10

#define APBTMRS_INT_STATUS      0xa0
#define APBTMRS_EOI             0xa4
#define APBTMRS_RAW_INT_STATUS  0xa8
#define APBTMRS_COMP_VERSION    0xac

#define APBTMR_CONTROL_ENABLE   (1 << 0)
/* 1: periodic, 0:free running. */
#define APBTMR_CONTROL_MODE_PERIODIC    (1 << 1)
#define APBTMR_CONTROL_INT              (1 << 2)

#define APBTMR_MAX_CNT  0xFFFFFFFF
#define APBTMR_EACH_OFS 0x14

struct eswin_timer {
    /* Memory Mapped Register */
    struct resource *mem;
    void __iomem *mmio_base;
    u32 perf_count;
    u32 numa_id;
};

static struct eswin_timer *perf_timer[2] = {NULL, NULL};
static void __iomem *perf_cnt_base[2] = {NULL, NULL};

static inline u32 eswin_readl(struct eswin_timer *timer, unsigned long offs)
{
    return readl(timer->mmio_base + offs);
}

static inline void eswin_writel(struct eswin_timer *timer, u32 val,
            unsigned long offs)
{
    writel(val, timer->mmio_base + offs);
}

static inline u32 eswin_readl_relaxed(struct eswin_timer *timer, unsigned long offs)
{
    return readl_relaxed(timer->mmio_base + offs);
}

static inline void eswin_writel_relaxed(struct eswin_timer *timer, u32 val,
            unsigned long offs)
{
    writel_relaxed(val, timer->mmio_base + offs);
}

static irqreturn_t timer_irq_handler(int irq, void *dev_id)
{
    struct eswin_timer *time = dev_id;
    pr_err("Enter timer_irq_handler  irq = %x, time->mmio_base = %lx\n",irq,(long)time->mmio_base);
    /*
    * clear interrupt read(0xa4)
    * read intr status read(0xa0)
    */
    eswin_readl(time,APBTMRS_INT_STATUS);
    eswin_readl(time,APBTMRS_EOI);
    return IRQ_HANDLED;
}

static int __init timer_init(struct device_node *np)
{
    struct clk *pclk, *timer_aclk, *timer3_clk8;
    struct reset_control *trstc0,*trstc1,*trstc2,*trstc3,*trstc4,*trstc5,*trstc6,*trstc7,*prstc;

    /*
    * Reset the timer if the reset control is available, wiping
    * out the state the firmware may have left it
    */
    trstc0 = of_reset_control_get(np, "trst0");
    if (!IS_ERR(trstc0)) {
        reset_control_assert(trstc0);
        reset_control_deassert(trstc0);
    }
    trstc1 = of_reset_control_get(np, "trst1");
    if (!IS_ERR(trstc1)) {
        reset_control_assert(trstc1);
        reset_control_deassert(trstc1);
    }
    trstc2 = of_reset_control_get(np, "trst2");
    if (!IS_ERR(trstc2)) {
        reset_control_assert(trstc2);
        reset_control_deassert(trstc2);
    }
    trstc3 = of_reset_control_get(np, "trst3");
    if (!IS_ERR(trstc3)) {
        reset_control_assert(trstc3);
        reset_control_deassert(trstc3);
    }
    trstc4 = of_reset_control_get(np, "trst4");
    if (!IS_ERR(trstc4)) {
        reset_control_assert(trstc4);
        reset_control_deassert(trstc4);
    }
    trstc5 = of_reset_control_get(np, "trst5");
    if (!IS_ERR(trstc5)) {
        reset_control_assert(trstc5);
        reset_control_deassert(trstc5);
    }
    trstc6 = of_reset_control_get(np, "trst6");
    if (!IS_ERR(trstc6)) {
        reset_control_assert(trstc6);
        reset_control_deassert(trstc6);
    }
    trstc7 = of_reset_control_get(np, "trst7");
    if (!IS_ERR(trstc7)) {
        reset_control_assert(trstc7);
        reset_control_deassert(trstc7);
    }
    prstc = of_reset_control_get(np, "prst");
    if (!IS_ERR(prstc)) {
        reset_control_assert(prstc);
        reset_control_deassert(prstc);
    }

    /*
    * Not all implementations use a peripheral clock, so don't panic
    * if it's not present
    */
    pclk = of_clk_get_by_name(np, "pclk");
    if (!IS_ERR(pclk))
        if (clk_prepare_enable(pclk))
            pr_warn("pclk for %pOFn is present, but could not be activated\n",np);
    timer_aclk = of_clk_get_by_name(np, "timer_aclk");
    if (!IS_ERR(timer_aclk))
        if (clk_prepare_enable(timer_aclk))
            pr_warn("timer_aclk for %pOFn is present, but could not be activated\n",np);
    timer3_clk8 = of_clk_get_by_name(np, "timer3_clk8");
    if (!IS_ERR(timer3_clk8))
        if (clk_prepare_enable(timer3_clk8))
            pr_warn("timer3_clk8 for %pOFn is present, but could not be activated\n",np);

    return 0;
}

static int timer_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct resource *res;
    u64 base;
    if (perf_timer[0] == NULL) {
        return -EIO;
    }

    res = perf_timer[0]->mem;

    base = res->start + perf_timer[0]->perf_count * 0x14;
    remap_pfn_range(vma, vma->vm_start, base >> 12,
            vma->vm_end - vma->vm_start, vma->vm_page_prot);
    return 0;
}

static int timer_mmap_die1(struct file *file, struct vm_area_struct *vma)
{
    struct resource *res;
    u64 base;
    if (perf_timer[1] == NULL) {
        return -EIO;
    }

    res = perf_timer[1]->mem;

    base = res->start + perf_timer[1]->perf_count * 0x14;
    remap_pfn_range(vma, vma->vm_start, base >> 12,
            vma->vm_end - vma->vm_start, vma->vm_page_prot);
    return 0;
}

static struct file_operations timer_fops[2] = {
    {
        .owner = THIS_MODULE,
        .mmap = timer_mmap,
    },
    {
        .owner = THIS_MODULE,
        .mmap = timer_mmap_die1,
    },
};

static struct miscdevice timer_misc[2] = {
    {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "perf_count",
        .fops = &timer_fops[0],
    },
    {
        .minor = MISC_DYNAMIC_MINOR,
        .name = "perf_count_die1",
        .fops = &timer_fops[1],
    },
};

/*
 * use timer0 channel 7 for performance statics counter.
 *
 * This counter freq is 24MHz, and max cnt value is 0xFFFF_FFFF, it decrease one every timer_clk, and when
 * decrease zero, it will load the max cnt, and repeat this.
 *
 * resolution: about 42ns per cnt. So the max time that will not overflow is 42 * 0xFFFF_FFFF ~= 180s
 */

u32 get_perf_timer_cnt(u32 numa_id)
{
    return APBTMR_MAX_CNT - readl(perf_cnt_base[numa_id]);
}
EXPORT_SYMBOL(get_perf_timer_cnt);

static int init_timer_perf_counter(struct eswin_timer *time, u32 chan)
{
    int ret;

    time->perf_count = chan;
    eswin_writel(time, APBTMR_MAX_CNT, chan * APBTMR_EACH_OFS + APBTMR_N_LOAD_COUNT);
    eswin_writel(time, 0x7, chan * APBTMR_EACH_OFS + APBTMR_N_CONTROL);
    ret = misc_register(&timer_misc[time->numa_id]);
    perf_timer[time->numa_id] = time;
    perf_cnt_base[time->numa_id] = perf_timer[time->numa_id]->mmio_base + chan * APBTMR_EACH_OFS + APBTMR_N_CURRENT_VALUE;
    return 0;
}

static int eswin_timer_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    struct eswin_timer *time;
    struct resource *res;
    int error, irq, ret;
    u32 val;

    dev_info(&pdev->dev, "eswin_timer_probe\n");
    /*add eswin timer*/
    time = devm_kzalloc(&pdev->dev, sizeof(struct eswin_timer), GFP_KERNEL);
    if (!time)
        return -ENOMEM;

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        dev_err(&pdev->dev, "failed to get register memory\n");
        return -EINVAL;
    }
    time->mem = res;

    ret = device_property_read_u32(&pdev->dev, "numa-node-id", &time->numa_id);
	if (0 != ret) {
		dev_err(&pdev->dev, "failed to get numa node id\n");
		return ret;
	}

    time->mmio_base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(time->mmio_base))
        return PTR_ERR(time->mmio_base);

    irq = platform_get_irq(pdev, 0);
    if (irq < 0)
        return -ENXIO;

    error = devm_request_irq(&pdev->dev, irq, timer_irq_handler, 0,
                                pdev->name, time);
    if (error) {
        dev_err(&pdev->dev, "could not request IRQ %d\n", irq);
        return error;
    }

    ret = timer_init(np);
    if (ret)
        return ret;

    time->perf_count = 0xff;

    ret = of_property_read_u32(np, "perf_count", &val);
    if (!ret) {
        init_timer_perf_counter(time, val);
    }

    dev_info(&pdev->dev, "eswin_timer_probe success\n");
    return 0;
}

static int eswin_timer_remove(struct platform_device *pdev)
{
    return -EBUSY; /* cannot unregister clockevent */
}

static const struct of_device_id eswin_timer_of_match[] = {
    { .compatible = "eswin,eswin-timer", },
    {},
};
MODULE_DEVICE_TABLE(of, eswin_timer_of_match);

static struct platform_driver eswin_timer_driver = {
    .probe  = eswin_timer_probe,
    .remove = eswin_timer_remove,
    .driver = {
        .name = "eswin-timer",
        .of_match_table = of_match_ptr(eswin_timer_of_match),
    },
};
module_platform_driver(eswin_timer_driver);

MODULE_ALIAS("platform:eswin-timer");
MODULE_DESCRIPTION("eswin timer driver");
MODULE_LICENSE("GPL v2");
