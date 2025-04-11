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
#include <linux/fs.h>
#include "vvcam_mipi.h"
#include "vvcam_mipi_driver.h"
#include "vvcam_mipi_platform.h"

static int vvcam_mipi_open(struct inode *inode, struct file *file)
{
    struct miscdevice *pmisc_dev = file->private_data;
    struct vvcam_mipi_dev *mipi_dev;

    mipi_dev = container_of(pmisc_dev, struct vvcam_mipi_dev, miscdev);
    if (!mipi_dev)
        return -ENOMEM;

    file->private_data = mipi_dev;

    mutex_lock(&mipi_dev->mlock);
    pm_runtime_get_sync(mipi_dev->dev);
    mutex_unlock(&mipi_dev->mlock);

    return 0;
}

static int vvcam_mipi_release(struct inode *inode, struct file *file)
{
    struct vvcam_mipi_dev *mipi_dev;

    mipi_dev = file->private_data;

    mutex_lock(&mipi_dev->mlock);
    pm_runtime_put(mipi_dev->dev);
    mutex_unlock(&mipi_dev->mlock);

    return 0;
}

static long vvcam_mipi_ioctl(struct file *file,
                        unsigned int cmd, unsigned long arg)
{
    struct vvcam_mipi_dev *mipi_dev;
    int ret = 0;
    uint32_t reset;
    uint32_t stream_enable;
    vvcam_input_dev_attr_t mipi_cfg;

    mipi_dev = file->private_data;

    mutex_lock(&mipi_dev->mlock);

    switch(cmd) {
    case VVCAM_MIPI_RESET:
        ret = copy_from_user(&reset, (void __user *)arg, sizeof(reset));
        if (ret)
            break;
        ret = vvcam_mipi_reset(mipi_dev, reset);
        break;
    case VVCAM_MIPI_SET_DEV_ATTR:
        ret = copy_from_user(&mipi_cfg, (void __user *)arg, sizeof(mipi_cfg));
        if (ret)
            break;
        ret = vvcam_mipi_set_cfg(mipi_dev, mipi_cfg);
        break;
    case VVCAM_MIPI_SET_STREAM:
        ret = copy_from_user(&stream_enable,
                    (void __user *)arg, sizeof(stream_enable));
        if (ret)
            break;
        ret = vvcam_mipi_set_stream(mipi_dev, stream_enable);
        break;
    default:
        break;
    }

    mutex_unlock(&mipi_dev->mlock);

    return ret;
}

static struct file_operations vvcam_mipi_fops = {
	.owner          = THIS_MODULE,
	.open           = vvcam_mipi_open,
	.release        = vvcam_mipi_release,
	.unlocked_ioctl = vvcam_mipi_ioctl,
};

static int vvcam_mipi_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct vvcam_mipi_dev *mipi_dev;
    struct resource *res;
    char *miscdev_name;

    mipi_dev = devm_kzalloc(&pdev->dev,
                sizeof(struct vvcam_mipi_dev), GFP_KERNEL);
    if (!mipi_dev)
        return -ENOMEM;

    res =  platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
		dev_err(&pdev->dev, "can't fetch device resource info\n");
		return -EIO;
	}
    mipi_dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mipi_dev->base)) {
        dev_err(&pdev->dev, "can't remap device resource info\n");
        return PTR_ERR(mipi_dev->base);
    }

    res =  platform_get_resource(pdev, IORESOURCE_MEM, 1);
    if (!res) {
		dev_err(&pdev->dev, "can't fetch tpg resource info\n");
		return -EIO;
	}
    mipi_dev->tpg_reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mipi_dev->tpg_reg)) {
        dev_err(&pdev->dev, "can't remap tpg resource info\n");
        return PTR_ERR(mipi_dev->tpg_reg);
    }

    res =  platform_get_resource(pdev, IORESOURCE_MEM, 2);
    if (!res) {
		dev_err(&pdev->dev, "can't fetch csi ctrl resource info\n");
		return -EIO;
	}
    mipi_dev->csi_ctrl_reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(mipi_dev->csi_ctrl_reg)) {
        dev_err(&pdev->dev, "can't remap csi ctrl resource info\n");
        return PTR_ERR(mipi_dev->csi_ctrl_reg);
    }

    res =  platform_get_resource(pdev, IORESOURCE_MEM, 3);
    if (!res) {
        dev_err(&pdev->dev, "can't fetch csi mux ctrl resource info\n");
        return -EIO;
    }
    mipi_dev->csi_mux_reg = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(mipi_dev->csi_mux_reg)) {
        dev_err(&pdev->dev, "can't remap csi mux ctrl resource info\n");
        return PTR_ERR(mipi_dev->csi_mux_reg);
    }

    mutex_init(&mipi_dev->mlock);
	platform_set_drvdata(pdev, mipi_dev);

    mipi_dev->dev = &pdev->dev;
    mipi_dev->id  = pdev->id;

    vvcam_mipi_default_cfg(mipi_dev);

    miscdev_name = devm_kzalloc(&pdev->dev, 16, GFP_KERNEL);
    if (!miscdev_name)
        return -ENOMEM;
    snprintf(miscdev_name, 16, "%s.%d", VVCAM_MIPI_NAME, pdev->id);

    mipi_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    mipi_dev->miscdev.name  = miscdev_name;
    mipi_dev->miscdev.fops  = &vvcam_mipi_fops;

    ret = misc_register(&mipi_dev->miscdev);
    if (ret) {
        dev_err(&pdev->dev, "failed to register device\n");
        return -EINVAL;
    }

    dev_info(&pdev->dev, "vvcam mipi driver probe success\n");

    return 0;
}

static int vvcam_mipi_remove(struct platform_device *pdev)
{
    struct vvcam_mipi_dev *mipi_dev;

    mipi_dev = platform_get_drvdata(pdev);
    misc_deregister(&mipi_dev->miscdev);
    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int vvcam_mipi_system_suspend(struct device *dev)
{
    int ret = 0;
    ret = pm_runtime_force_suspend(dev);
    if (ret) {
        dev_err(dev, "force suspend %s failed\n", dev_name(dev));
        return ret;
    }
	return ret;
}

static int vvcam_mipi_system_resume(struct device *dev)
{
    int ret = 0;
    ret = pm_runtime_force_resume(dev);
    if (ret) {
        dev_err(dev, "force resume %s failed\n", dev_name(dev));
        return ret;
    }
	return ret;
}

static int vvcam_mipi_runtime_suspend(struct device *dev)
{
    return 0;
}

static int vvcam_mipi_runtime_resume(struct device *dev)
{
    return 0;
}

static const struct dev_pm_ops vvcam_mipi_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(vvcam_mipi_system_suspend, vvcam_mipi_system_resume)
	SET_RUNTIME_PM_OPS(vvcam_mipi_runtime_suspend, vvcam_mipi_runtime_resume, NULL)
};
#endif

static const struct of_device_id vvcam_mipi_of_match[] = {
	{.compatible = "verisilicon,mipi",},
	{ /* sentinel */ },
};

static struct platform_driver vvcam_mipi_driver = {
	.probe  = vvcam_mipi_probe,
	.remove = vvcam_mipi_remove,
	.driver = {
		.name           = VVCAM_MIPI_NAME,
		.owner          = THIS_MODULE,
        .of_match_table = vvcam_mipi_of_match,
#ifdef CONFIG_PM_SLEEP
        .pm             = &vvcam_mipi_pm_ops,
#endif
	}
};

static int __init vvcam_mipi_init_module(void)
{
    int ret;
    ret = platform_driver_register(&vvcam_mipi_driver);
    if (ret) {
        printk(KERN_ERR "Failed to register mipi driver\n");
        return ret;
    }

    ret = vvcam_mipi_platform_device_register();
    if (ret) {
		platform_driver_unregister(&vvcam_mipi_driver);
		printk(KERN_ERR "Failed to register vvcam mipi platform devices\n");
		return ret;
	}

    return ret;
}

static void __exit vvcam_mipi_exit_module(void)
{
    platform_driver_unregister(&vvcam_mipi_driver);
    vvcam_mipi_platform_device_unregister();
    return;
}

late_initcall(vvcam_mipi_init_module);
module_exit(vvcam_mipi_exit_module);

MODULE_DESCRIPTION("Verisilicon mipi driver");
MODULE_AUTHOR("Verisilicon ISP SW Team");
MODULE_LICENSE("GPL");
