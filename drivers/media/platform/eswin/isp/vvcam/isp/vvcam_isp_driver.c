/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2024 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2024 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/property.h>
#include <linux/i2c.h>
#include <media/v4l2-subdev.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/mfd/syscon.h>
#include <linux/bitfield.h>
#include <linux/regmap.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include "vvcam_isp_driver.h"
//#include "vvcam_isp_platform.h"
#include "vvcam_isp.h"
#include "vvcam_event.h"
#include "vvcam_isp_procfs.h"

#define ISP_FUSA_DEBUG 0

#define AWSMMUSID	GENMASK(31, 24) // The sid of write operation
#define AWSMMUSSID	GENMASK(23, 16) // The ssid of write operation
#define ARSMMUSID	GENMASK(15, 8)	// The sid of read operation
#define ARSMMUSSID	GENMASK(7, 0)	// The ssid of read operation

extern void vvcam_isp_irq_stat_tasklet(unsigned long);

static irqreturn_t vvcam_isp_irq_handler(int irq, void *isp_dev)
{
    return vvcam_isp_irq_process(isp_dev);
}

static irqreturn_t vvcam_isp_mi_irq_handler(int irq, void *isp_dev)
{
    return vvcam_isp_mi_irq_process(isp_dev);
}

static irqreturn_t vvcam_isp_fe_irq_handler(int irq, void *isp_dev)
{
    return vvcam_isp_fe_irq_process(isp_dev);
}

#if ISP_FUSA_DEBUG
static irqreturn_t vvcam_isp_fusa_irq_handler(int irq, void *isp_dev)
{
    return vvcam_isp_fusa_irq_process(isp_dev);
}
#endif

static int vvcam_isp_open(struct inode *inode, struct file *file)
{
    struct miscdevice *pmisc_dev = file->private_data;
    struct vvcam_isp_dev *isp_dev;
    struct vvcam_isp_fh *isp_fh;

    isp_dev = container_of(pmisc_dev, struct vvcam_isp_dev, miscdev);
    if (!isp_dev)
        return -ENOMEM;

    isp_fh = kzalloc(sizeof(struct vvcam_isp_fh), GFP_KERNEL);
    if (!isp_fh)
        return -ENOMEM;

    isp_fh->isp_dev = isp_dev;
    vvcam_event_fh_init( &isp_dev->event_dev, &isp_fh->event_fh);
    file->private_data = isp_fh;

    mutex_lock(&isp_dev->mlock);
    isp_dev->refcnt++;
    pm_runtime_get_sync(isp_dev->dev);
    mutex_unlock(&isp_dev->mlock);

    return 0;
}

static int vvcam_isp_release(struct inode *inode, struct file *file)
{
    struct vvcam_isp_dev *isp_dev;
    struct vvcam_isp_fh *isp_fh;

    isp_fh = file->private_data;
    isp_dev = isp_fh->isp_dev;

    vvcam_event_unsubscribe_all(&isp_fh->event_fh);

    vvcam_event_fh_destroy(&isp_dev->event_dev, &isp_fh->event_fh);

    mutex_lock(&isp_dev->mlock);
    isp_dev->refcnt--;
    pm_runtime_put(isp_dev->dev);
    mutex_unlock(&isp_dev->mlock);

    kfree(isp_fh);

    return 0;
}

static long vvcam_isp_ioctl(struct file *file,
                        unsigned int cmd, unsigned long arg)
{
    struct vvcam_isp_dev *isp_dev;
    struct vvcam_isp_fh *isp_fh;
    uint32_t reset;
    vvcam_isp_reg_t isp_reg;
    vvcam_subscription_t sub;
    vvcam_event_t event;
    int ret = 0;

    isp_fh = file->private_data;
    isp_dev = isp_fh->isp_dev;

    mutex_lock(&isp_dev->mlock);

    switch(cmd) {
    case VVCAM_ISP_RESET:
        ret = copy_from_user(&reset, (void __user *)arg, sizeof(reset));
        if (ret)
            break;
        ret = vvcam_isp_reset(isp_dev, reset);
        break;
    case VVCAM_ISP_READ_REG:
        ret = copy_from_user(&isp_reg, (void __user *)arg, sizeof(isp_reg));
        if (ret)
            break;
        ret = vvcam_isp_read_reg(isp_dev, &isp_reg);
        if (ret)
            break;
        ret = copy_to_user((void __user *)arg, &isp_reg, sizeof(isp_reg));
        break;
    case VVCAM_ISP_WRITE_REG:
        ret = copy_from_user(&isp_reg, (void __user *)arg, sizeof(isp_reg));
        if (ret)
            break;
        ret = vvcam_isp_write_reg(isp_dev, isp_reg);
        break;
    case VVCAM_ISP_SUBSCRIBE_EVENT:
        ret = copy_from_user(&sub, (void __user *)arg, sizeof(sub));
        if (ret)
            break;
        ret = vvcam_event_subscribe(&isp_fh->event_fh,
                    &sub, VVCAM_ISP_EVENT_ELEMS);
        break;
    case VVCAM_ISP_UNSUBSCRIBE_EVENT:
        ret = copy_from_user(&sub, (void __user *)arg, sizeof(sub));
        if (ret)
            break;
        ret = vvcam_event_unsubscribe(&isp_fh->event_fh, &sub);
        break;
    case VVCAM_ISP_DQEVENT:
        ret = vvcam_event_dequeue(&isp_fh->event_fh, &event);
        if (ret)
            break;
        ret = copy_to_user((void __user *)arg, &event, sizeof(event));
        break;
    default:
        ret = -EINVAL;
        break;
    }

    mutex_unlock(&isp_dev->mlock);

    return ret;
}

static unsigned int vvcam_isp_poll(struct file *file, poll_table *wait)
{
    struct vvcam_isp_fh *isp_fh;

    isp_fh = file->private_data;

    return vvcam_event_poll(file, &isp_fh->event_fh, wait);
}

int vvcam_isp_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct vvcam_isp_dev *isp_dev;
    struct vvcam_isp_fh *isp_fh;

    isp_fh = file->private_data;
    isp_dev = isp_fh->isp_dev;

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    return vm_iomap_memory(vma, isp_dev->paddr, isp_dev->regs_size);
}

static struct file_operations vvcam_isp_fops = {
	.owner          = THIS_MODULE,
	.open           = vvcam_isp_open,
	.release        = vvcam_isp_release,
	.unlocked_ioctl = vvcam_isp_ioctl,
    .poll           = vvcam_isp_poll,
    .mmap           = vvcam_isp_mmap,

};

static int vvcam_isp_parse_params(struct vvcam_isp_dev *isp_dev,
                        struct platform_device *pdev)
{
	int ret;
	struct resource *res;

    ret = device_property_read_u32(&pdev->dev, "id", &pdev->id);
    if (ret) {
        pr_info("isp device id not found, use default\n");
        pdev->id = 0;
    }

    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
		dev_err(&pdev->dev, "can't fetch device resource info\n");
		return -EIO;
	}
    isp_dev->paddr = res->start;
    isp_dev->regs_size = resource_size(res);
    isp_dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(isp_dev->base)) {
        dev_err(&pdev->dev, "can't remap device resource info\n");
        return PTR_ERR(isp_dev->base);
    }

    isp_dev->isp_irq = platform_get_irq(pdev, 0);
    if (isp_dev->isp_irq < 0) {
		dev_err(&pdev->dev, "can't get irq resource\n");
		return -ENXIO;
	}

    isp_dev->mi_irq = platform_get_irq(pdev, 1);
    if (isp_dev->mi_irq < 0) {
		dev_err(&pdev->dev, "can't get mi irq resource\n");
		return -ENXIO;
	}

    isp_dev->fe_irq = platform_get_irq(pdev, 2);
    if (isp_dev->fe_irq < 0) {
		dev_err(&pdev->dev, "can't get fe irq resource\n");
		return -ENXIO;
	}
#if 1 //ISP_FUSA_DEBUG
    // isp_dev->fusa_irq = platform_get_irq(pdev, 3);
    // if (isp_dev->fusa_irq < 0) {
    //     dev_err(&pdev->dev, "can't get fusa irq resource\n");
    //     return -ENXIO;
    // }

    // res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
    // if (!res) {
	// 	dev_err(&pdev->dev, "can't fetch device reset reg\n");
	// 	return -EIO;
	// }
    isp_dev->reset = devm_ioremap(&pdev->dev, res->start, resource_size(res));
    if (IS_ERR(isp_dev->reset)) {
        dev_err(&pdev->dev, "can't remap device reset reg\n");
        return PTR_ERR(isp_dev->reset);
    }
#endif
    return 0;
}

static int isp_smmu_sid_cfg(struct device* dev)
{
    int ret = 0;
    struct regmap* regmap = NULL;
    int mmu_tbu0_vi_isp_reg = 0;
    u32 rdwr_sid_ssid = 0;
    u32 sid = 0;

    struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

    pr_info("%s enter\n", __func__);
	/* not behind smmu, use the default reset value(0x0) of the reg as streamID*/
	if (fwspec == NULL) {
		pr_info("isp is not behind smmu, skip configuration of sid\n");
		return 0;
	}

	sid = fwspec->ids[0];

	regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "eswin,vi_top_csr");
	if (IS_ERR(regmap)) {
		pr_info("No vi_top_csr phandle specified\n");
		return 0;
	}

	ret = of_property_read_u32_index(dev->of_node, "eswin,vi_top_csr", 1,
					&mmu_tbu0_vi_isp_reg);
	if (ret) {
		pr_err("can't get isp sid cfg reg offset (%d)\n", ret);
		return ret;
	}

	/* make the reading sid the same as writing sid, ssid is fixed to zero */
	rdwr_sid_ssid  = FIELD_PREP(AWSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSID, sid);
	rdwr_sid_ssid |= FIELD_PREP(AWSMMUSSID, 0);
	rdwr_sid_ssid |= FIELD_PREP(ARSMMUSSID, 0);
	regmap_write(regmap, mmu_tbu0_vi_isp_reg, rdwr_sid_ssid);

	ret = win2030_dynm_sid_enable(dev_to_node(dev));
	if (ret < 0)
		pr_err("failed to config isp streamID(%d)!\n", sid);
	else
		pr_info("success to config isp streamID(%d)!\n", sid);
    
    pr_info("%s exit\n", __func__);
    return ret;

}

static int vvcam_isp_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct vvcam_isp_dev *isp_dev;
    char *ispdev_name;
    struct device *dev = &pdev->dev;
    struct device_node *sensor_np;
    struct device *sensor_dev;
    struct v4l2_subdev *sd;
    int wait_time = 0;

    sensor_np = of_parse_phandle(dev->of_node, "depends-on", 0);
    if (sensor_np)
    {
        while (wait_time < 20000) {
            sensor_dev = bus_find_device_by_of_node(&i2c_bus_type, sensor_np);
            of_node_put(sensor_np);
            if (sensor_dev) {
                sd = dev_get_drvdata(sensor_dev);
                put_device(sensor_dev);
                if (sd && sd->v4l2_dev) {
                    dev_info(dev, "Sensor is ready.\n");
                    break;
                }
            }
            msleep(1000);
            wait_time += 1000;
        }
    }

    isp_dev = devm_kzalloc(&pdev->dev,
                sizeof(struct vvcam_isp_dev), GFP_KERNEL);
    if (!isp_dev)
        return -ENOMEM;

    (void)isp_smmu_sid_cfg(&pdev->dev);

    ret = vvcam_isp_parse_params(isp_dev, pdev);
    if (ret) {
        dev_err(&pdev->dev, "failed to parse params\n");
        return -EINVAL;
    }

    mutex_init(&isp_dev->mlock);
    spin_lock_init(&isp_dev->stat_lock);
    vvcam_event_dev_init(&isp_dev->event_dev);
	platform_set_drvdata(pdev, isp_dev);

    isp_dev->dev = &pdev->dev;
    isp_dev->id  = pdev->id;

    ispdev_name = devm_kzalloc(&pdev->dev, 16, GFP_KERNEL);
    if (!ispdev_name)
        return -ENOMEM;
    snprintf(ispdev_name, 16, "%s.%d", VVCAM_ISP_NAME, pdev->id);

    isp_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    isp_dev->miscdev.name  = ispdev_name;
    isp_dev->miscdev.fops  = &vvcam_isp_fops;

    ret = misc_register(&isp_dev->miscdev);
    if (ret) {
        dev_err(&pdev->dev, "failed to register device\n");
        return -EINVAL;
    }

    ret = vvcam_isp_procfs_register(isp_dev, &isp_dev->pde);
    if (ret) {
        dev_err(&pdev->dev, "isp register procfs failed.\n");
        goto error_register_procfs;
    }

    tasklet_init(&isp_dev->stat_tasklet,
        vvcam_isp_irq_stat_tasklet, (unsigned long)isp_dev);

    ret = devm_request_irq(&pdev->dev, isp_dev->isp_irq, vvcam_isp_irq_handler,
                IRQF_TRIGGER_HIGH | IRQF_SHARED, dev_name(&pdev->dev), isp_dev);
    if (ret) {
        dev_err(&pdev->dev, "can't request isp irq\n");
        goto error_request_isp_irq;
    }

    ret = devm_request_irq(&pdev->dev, isp_dev->mi_irq, vvcam_isp_mi_irq_handler,
                IRQF_TRIGGER_HIGH | IRQF_SHARED, dev_name(&pdev->dev), isp_dev);
    if (ret) {
        dev_err(&pdev->dev, "can't request mi irq\n");
        goto error_request_mi_irq;
    }

    ret = devm_request_irq(&pdev->dev, isp_dev->fe_irq, vvcam_isp_fe_irq_handler,
                IRQF_TRIGGER_HIGH | IRQF_SHARED, dev_name(&pdev->dev), isp_dev);
    if (ret) {
        dev_err(&pdev->dev, "can't request fe irq\n");
        goto error_request_fe_irq;
    }

#if ISP_FUSA_DEBUG
    ret = devm_request_irq(&pdev->dev, isp_dev->fusa_irq, vvcam_isp_fusa_irq_handler,
                IRQF_TRIGGER_HIGH | IRQF_SHARED, dev_name(&pdev->dev), isp_dev);
    if (ret) {
        dev_err(&pdev->dev, "can't request fusa irq\n");
        goto error_request_fusa_irq;
    }
#endif
    dev_info(&pdev->dev, "vvcam isp driver probe success\n");

    return 0;

#if ISP_FUSA_DEBUG
error_request_fusa_irq:
    devm_free_irq(&pdev->dev, isp_dev->fe_irq, isp_dev);
#endif	
error_request_fe_irq:
    devm_free_irq(&pdev->dev, isp_dev->mi_irq, isp_dev);
error_request_mi_irq:
    devm_free_irq(&pdev->dev, isp_dev->isp_irq, isp_dev);
error_request_isp_irq:
    vvcam_isp_procfs_unregister(isp_dev->pde);
error_register_procfs:
    misc_deregister(&isp_dev->miscdev);

    return ret;
}

static int vvcam_isp_remove(struct platform_device *pdev)
{
    struct vvcam_isp_dev *isp_dev;

    isp_dev = platform_get_drvdata(pdev);

    vvcam_isp_procfs_unregister(isp_dev->pde);

    misc_deregister(&isp_dev->miscdev);
    devm_free_irq(&pdev->dev, isp_dev->isp_irq, isp_dev);
    devm_free_irq(&pdev->dev, isp_dev->mi_irq, isp_dev);
    devm_free_irq(&pdev->dev, isp_dev->fe_irq, isp_dev);
	
#if ISP_FUSA_DEBUG
    devm_free_irq(&pdev->dev, isp_dev->fusa_irq, isp_dev);
#endif
    tasklet_kill(&isp_dev->stat_tasklet);

    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int vvcam_isp_system_suspend(struct device *dev)
{
    int ret = 0;
    ret = pm_runtime_force_suspend(dev);
    if (ret) {
        dev_err(dev, "force suspend %s failed\n", dev_name(dev));
        return ret;
    }
	return ret;
}

static int vvcam_isp_system_resume(struct device *dev)
{
    int ret = 0;
    ret = pm_runtime_force_resume(dev);
    if (ret) {
        dev_err(dev, "force resume %s failed\n", dev_name(dev));
        return ret;
    }
	return ret;
}

static int vvcam_isp_runtime_suspend(struct device *dev)
{
    return 0;
}

static int vvcam_isp_runtime_resume(struct device *dev)
{
    return 0;
}

static const struct dev_pm_ops vvcam_isp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(vvcam_isp_system_suspend, vvcam_isp_system_resume)
	SET_RUNTIME_PM_OPS(vvcam_isp_runtime_suspend, vvcam_isp_runtime_resume, NULL)
};
#endif

static const struct of_device_id vvcam_isp_of_match[] = {
	{.compatible = "esw,win2030-isp",},
	{ /* sentinel */ },
};

static struct platform_driver vvcam_isp_driver = {
	.probe  = vvcam_isp_probe,
	.remove = vvcam_isp_remove,
	.driver = {
		.name           = VVCAM_ISP_NAME,
		.owner          = THIS_MODULE,
        .of_match_table = vvcam_isp_of_match,
#ifdef CONFIG_PM_SLEEP        
        .pm             = &vvcam_isp_pm_ops,
#endif
	}
};

MODULE_DEVICE_TABLE(of, vvcam_isp_of_match);

static int __init vvcam_isp_init_module(void)
{
    int ret;
    ret = platform_driver_register(&vvcam_isp_driver);
    if (ret) {
        printk(KERN_ERR "Failed to register isp driver\n");
        return ret;
    }
#if 0
    ret = vvcam_isp_platform_device_register();
    if (ret) {
		platform_driver_unregister(&vvcam_isp_driver);
		printk(KERN_ERR "Failed to register vvcam isp platform devices\n");
		return ret;
	}
#endif
    return ret;
}

static void __exit vvcam_isp_exit_module(void)
{
    platform_driver_unregister(&vvcam_isp_driver);
//    vvcam_isp_platform_device_unregister();
}

late_initcall(vvcam_isp_init_module);
module_exit(vvcam_isp_exit_module);

MODULE_DESCRIPTION("Verisilicon isp driver");
MODULE_AUTHOR("Verisilicon ISP SW Team");
MODULE_LICENSE("GPL");
