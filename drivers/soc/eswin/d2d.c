// SPDX-License-Identifier: GPL-2.0
/*
 * D2D error monitor of core driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
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
 * Authors: Yu Ning <ningyu@eswincomputing.com>
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
/**
 * D2D error unit register map
 * 0x28 : d2d common interrupt
 * 0x30 : d2d common interrupt2
 */

#define PHY_OFFSET              0x40000
#define REG_D2D_INTR_SUMMARY    0x810
#define REG_D2D_INTR2_SUMMARY   0x814
#define REG_D2D_INTR_MASK       0x818
#define REG_D2D_INTR2_MASK      0x81c
#define REG_D2D_COMMON_INTR     0x828
#define REG_D2D_COMMON_INTR2    0x830
#define REG_D2D_SPARE_FIRMWARE  0x4034

#define D2D_CNTRL_TOP_INTR  (0x1<<8)
#define SERDES_INTR  (0x1<<9)

#define D2D_HW_INTR     287
#define D2D_HW_INTR2    288

struct d2d_device {
	struct device *dev;
	void __iomem *control;
	int plic_irq;
    int numa_id;
	struct delayed_work	delay_work;
};

static char *d2d_err_desc[] = {
    "RX_ERR_ALL_INT",
    "RX_LOCAL_LINKUP_INT",
    "RX_REMOTE_LINKUP_INT",
    "CMN_BRIDGE_RX_AFIFO_OVF_ERR_INT",
    "CMN_BRIDGE_RX_AFIFO_UNF_ERR_INT",
    "CMN_BRIDGE_TX_AFIFO_OVF_ERR_INT",
    "CMN_BRIDGE_TX_AFIFO_UNF_ERR_INT",
};

char *d2d_irq_src[] = {
	"d2d interrupt",
	"d2d 2nd interrupt",
};

#define 	D2D_IRQ_NUMBER			(ARRAY_SIZE(d2d_irq_src))

static irqreturn_t d2d_irqhandle(int irq, void *dev_id)
{
	struct d2d_device *d2d_dev = dev_id;
	void __iomem *base = d2d_dev->control;
    unsigned int intr_status, index;
    struct irq_data *data = NULL;
	// char *irq_src = NULL;
    // int ret, nid;

	data = irq_get_irq_data(irq);
	if (NULL == data) {
		pr_err("D2D: invalid irq data\n");
	}

    if (data->hwirq == D2D_HW_INTR) {
        printk(KERN_ERR "%s , hw irq %ld!\n", d2d_irq_src[0], data->hwirq);
        intr_status = readl(base+REG_D2D_INTR_SUMMARY);
        printk(KERN_ERR "D2D-%d interrupt summary : 0x%x, d2d top interrupt: 0x%x\n",
                d2d_dev->numa_id,intr_status,readl(base+REG_D2D_COMMON_INTR));
        if (intr_status & D2D_CNTRL_TOP_INTR) {
            for(index=0,intr_status=readl(base+REG_D2D_COMMON_INTR);index<7;index++) {
                if (intr_status & (0x1<<index)) {
                    printk(KERN_ERR "D2D error[%s]\n", d2d_err_desc[index]);
                    writel((0x1<<index),base+REG_D2D_COMMON_INTR);
                }
            }
        }
    } else if (data->hwirq == D2D_HW_INTR2) {
        printk(KERN_ERR "%s , hw irq %ld!\n", d2d_irq_src[1], data->hwirq);
        intr_status = readl(base+REG_D2D_INTR2_SUMMARY);
        printk(KERN_ERR "D2D-%d interrupt2 summary : 0x%x, d2d top interrupt: 0x%x\n",
                d2d_dev->numa_id,intr_status,readl(base+REG_D2D_COMMON_INTR2));
        if (intr_status & D2D_CNTRL_TOP_INTR) {
            for(index=0,intr_status=readl(base+REG_D2D_COMMON_INTR2);index<7;index++) {
                if (intr_status & (0x1<<index)) {
                    printk(KERN_ERR "D2D error[%s]\n", d2d_err_desc[index]);
                    writel((0x1<<index),base+REG_D2D_COMMON_INTR2);
                }
            }
        }
    }

	return IRQ_HANDLED;
}

static void adaptation_delay_work_fn(struct work_struct *work)
{
	struct d2d_device *d2d_dev = container_of(to_delayed_work(work), struct d2d_device, delay_work);
	void __iomem *base = d2d_dev->control;
	void *reg_addr = (void *)(base + PHY_OFFSET + REG_D2D_SPARE_FIRMWARE);
	unsigned int rdata = readl(reg_addr);
	rdata = rdata | 0x1004000;
	writel(rdata, reg_addr);
	while(rdata & 0x1004000) {
		schedule_timeout_interruptible(msecs_to_jiffies(10));
		rdata = readl(reg_addr);
	}
	schedule_delayed_work(&d2d_dev->delay_work, msecs_to_jiffies(2 * MSEC_PER_SEC));
}

static const struct of_device_id eic7x_d2d_error_of_match[] = {
	{.compatible = "eswin,eic7x-d2d", },
	{ /* sentinel value */ }
};

static int  d2d_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct d2d_device *d2d_dev;
	int ret, i, req_irq;
	struct resource *res;

	d2d_dev = devm_kcalloc(dev, 1,
		sizeof(struct d2d_device), GFP_KERNEL);
	if (!d2d_dev)
		return -ENOMEM;

	d2d_dev->dev = dev;
	dev_set_drvdata(dev, d2d_dev);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "Error while get mem resource\n");
		return -ENODEV;
	}
	d2d_dev->control = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR_OR_NULL(d2d_dev->control)) {
		dev_err(dev, "Fail to get resource %s from 0x%llx!\n",
			node->name, res->start);
		ret = -EINVAL;
		goto free_d2d_err_dev;
	}

    ret = device_property_read_u32(d2d_dev->dev, "numa-node-id", &(d2d_dev->numa_id));
	if (ret) {
        dev_err(dev, "Can not get numa node!\n");
        goto free_d2d_err_dev;
    }

    /* mask interrupts except D2D controller status and SerDes */
	writel(0xff,d2d_dev->control+REG_D2D_INTR_MASK);
	writel(0xff,d2d_dev->control+REG_D2D_INTR2_MASK);

    if (readl(d2d_dev->control+REG_D2D_INTR_SUMMARY)||readl(d2d_dev->control+REG_D2D_INTR2_SUMMARY)) {
        dev_info(dev, "D2D has intterupt  intr:0x%x, intr2:0x%x\n",
                readl(d2d_dev->control+REG_D2D_INTR_SUMMARY),readl(d2d_dev->control+REG_D2D_INTR2_SUMMARY));
    }

    /* clean any interrupt before */
	writel(0,d2d_dev->control+REG_D2D_COMMON_INTR);
	writel(0,d2d_dev->control+REG_D2D_COMMON_INTR2);

    for (i = 0; i < D2D_IRQ_NUMBER; i++) {
		req_irq = platform_get_irq(pdev, i);
		if (req_irq < 0)
			return req_irq;

		ret = devm_request_irq(&pdev->dev, req_irq, &d2d_irqhandle,
				IRQF_SHARED |IRQF_ONESHOT ,
				d2d_irq_src[i], d2d_dev);
		if (ret) {
			dev_err(&pdev->dev, "cannot register irq %d, ret %d\n", req_irq, ret);
			return ret;
		}
		dev_dbg(&pdev->dev,"registered irq %s, base %d, num %ld\n", d2d_irq_src[i],
			platform_get_irq(pdev, 0), D2D_IRQ_NUMBER);
	}
	INIT_DELAYED_WORK(&d2d_dev->delay_work, adaptation_delay_work_fn);
	schedule_delayed_work(&d2d_dev->delay_work, msecs_to_jiffies(2 * MSEC_PER_SEC));

	dev_info(dev, "D2D-%d init OK\n",d2d_dev->numa_id);
	return 0;

free_d2d_err_dev:
	return ret;

}

static struct platform_driver d2d_driver = {
	.probe = d2d_probe,
	// .remove = d2d_remove,
	.driver = {
		.name = "d2d_monitor",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(eic7x_d2d_error_of_match),},
};

static int __init init_d2d_unit(void)
{
	return platform_driver_register(&d2d_driver);
}

subsys_initcall(init_d2d_unit);
