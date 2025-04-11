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

#include "vvcam_vi_driver.h"
#include "vvcam_vi_platform.h"
#include "vvcam_vi.h"
#include "vvcam_event.h"
#include "vvcam_vi_procfs.h"

extern void vvcam_vi_irq_stat_tasklet(unsigned long);


static irqreturn_t vvcam_vi_irq_handler(int irq, void *vi_dev)
{

    return vvcam_vi_irq_process(vi_dev);
}

static int vvcam_vi_open(struct inode *inode, struct file *file)
{
    struct miscdevice *pmisc_dev = file->private_data;
    struct vvcam_vi_dev *vi_dev;
    struct vvcam_vi_fh *vi_fh;
    printk("enter %s\n", __func__);
    vi_dev = container_of(pmisc_dev, struct vvcam_vi_dev, miscdev);
    if (!vi_dev)
        return -ENOMEM;

    vi_fh = kzalloc(sizeof(struct vvcam_vi_dev), GFP_KERNEL);
    if (!vi_fh)
        return -ENOMEM;

    vi_fh->vi_dev = vi_dev;
    vvcam_event_fh_init( &vi_dev->event_dev, &vi_fh->event_fh);
    file->private_data = vi_fh;

    mutex_lock(&vi_dev->mlock);

    if (vi_dev->refcnt == 0) {
        enable_irq(vi_dev->vi_irq);
    }
    vi_dev->refcnt++;
    printk("%s vi_dev->refcnt %d\n ", __func__, vi_dev->refcnt);
    pm_runtime_get_sync(vi_dev->dev);
    mutex_unlock(&vi_dev->mlock);
    printk("exit %s\n", __func__);

    return 0;
}

static int vvcam_vi_release(struct inode *inode, struct file *file)
{
    struct vvcam_vi_dev *vi_dev;
    struct vvcam_vi_fh *vi_fh;
    printk("enter %s\n", __func__);

    vi_fh = file->private_data;
    vi_dev = vi_fh->vi_dev;

    vvcam_event_unsubscribe_all(&vi_fh->event_fh);

    vvcam_event_fh_destroy(&vi_dev->event_dev, &vi_fh->event_fh);

    mutex_lock(&vi_dev->mlock);
    vi_dev->refcnt--;

    if (vi_dev->refcnt == 0) {
        disable_irq(vi_dev->vi_irq);
    }

    printk("%s vi_dev->refcnt %d\n ", __func__, vi_dev->refcnt);

    pm_runtime_put(vi_dev->dev);
    mutex_unlock(&vi_dev->mlock);

    kfree(vi_fh);
    printk("exit %s\n", __func__);

    return 0;
}

static long vvcam_vi_ioctl(struct file *file,
                        unsigned int cmd, unsigned long arg)
{
    struct vvcam_vi_dev *vi_dev;
    struct vvcam_vi_fh *vi_fh;
    uint32_t reset;
    vvcam_vi_reg_t vi_reg;
    vvcam_subscription_t sub;
    vvcam_event_t event;
    int ret = 0;

    vi_fh = file->private_data;
    vi_dev = vi_fh->vi_dev;

    mutex_lock(&vi_dev->mlock);
    switch(cmd) {
    case VVCAM_VI_RESET:
        ret = copy_from_user(&reset, (void __user *)arg, sizeof(reset));
        if (ret)
            break;
        ret = vvcam_vi_reset(vi_dev, reset);
        break;
    case VVCAM_VI_READ_REG:
        ret = copy_from_user(&vi_reg, (void __user *)arg, sizeof(vi_reg));
        if (ret)
            break;
        ret = vvcam_vi_read_reg(vi_dev, &vi_reg);
        if (ret)
            break;
        ret = copy_to_user((void __user *)arg, &vi_reg, sizeof(vi_reg));
        break;
    case VVCAM_VI_WRITE_REG:
        ret = copy_from_user(&vi_reg, (void __user *)arg, sizeof(vi_reg));
        if (ret)
            break;
        ret = vvcam_vi_write_reg(vi_dev, vi_reg);
        break;
    case VVCAM_VI_SUBSCRIBE_EVENT:
        ret = copy_from_user(&sub, (void __user *)arg, sizeof(sub));
        if (ret)
            break;
        
        ret = vvcam_event_subscribe(&vi_fh->event_fh,
                    &sub, VVCAM_VI_EVENT_ELEMS);
        break;
    case VVCAM_VI_UNSUBSCRIBE_EVENT:
        ret = copy_from_user(&sub, (void __user *)arg, sizeof(sub));
        if (ret)
            break;
        ret = vvcam_event_unsubscribe(&vi_fh->event_fh, &sub);
        break;
    case VVCAM_VI_DQEVENT:
        ret = vvcam_event_dequeue(&vi_fh->event_fh, &event);
        if (ret)
            break;
        ret = copy_to_user((void __user *)arg, &event, sizeof(event));
        break;
    default:
        ret = -EINVAL;
        break;
    }

    mutex_unlock(&vi_dev->mlock);

    return ret;
}

static unsigned int vvcam_vi_poll(struct file *file, poll_table *wait)
{
    struct vvcam_vi_fh *vi_fh;
    vi_fh = file->private_data;

    return vvcam_event_poll(file, &vi_fh->event_fh, wait);
}

int vvcam_vi_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct vvcam_vi_dev *vi_dev;
    struct vvcam_vi_fh *vi_fh;

    vi_fh = file->private_data;
    vi_dev = vi_fh->vi_dev;

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    return vm_iomap_memory(vma, vi_dev->paddr, vi_dev->regs_size);
}

static struct file_operations vvcam_vi_fops = {
    .owner          = THIS_MODULE,
    .open           = vvcam_vi_open,
    .release        = vvcam_vi_release,
    .unlocked_ioctl = vvcam_vi_ioctl,
    .poll           = vvcam_vi_poll,
    .mmap           = vvcam_vi_mmap,

};

static int vvcam_vi_parse_params(struct vvcam_vi_dev *vi_dev,
                        struct platform_device *pdev)
{
    struct resource *res;

    res =  platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        dev_err(&pdev->dev, "can't fetch device resource info\n");
        return -EIO;
    }
    vi_dev->paddr = res->start;
    vi_dev->regs_size = resource_size(res);
    vi_dev->base = devm_ioremap_resource(&pdev->dev, res);
    if (IS_ERR(vi_dev->base)) {
        dev_err(&pdev->dev, "can't remap device resource info\n");
        return PTR_ERR(vi_dev->base);
    }

    vi_dev->vi_irq = platform_get_irq(pdev, 0);
    if (vi_dev->vi_irq < 0) {
        dev_err(&pdev->dev, "can't get irq resource\n");
        return -ENXIO;
    }

    return 0;
}

static int vvcam_vi_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct vvcam_vi_dev *vi_dev;
    char *videv_name;

    vi_dev = devm_kzalloc(&pdev->dev,
                sizeof(struct vvcam_vi_dev), GFP_KERNEL);
    if (!vi_dev)
        return -ENOMEM;

    ret = vvcam_vi_parse_params(vi_dev, pdev);
    if (ret) {
        dev_err(&pdev->dev, "failed to parse params\n");
        return -EINVAL;
    }

    mutex_init(&vi_dev->mlock);
    spin_lock_init(&vi_dev->stat_lock);
    vvcam_event_dev_init(&vi_dev->event_dev);
    platform_set_drvdata(pdev, vi_dev);

    vi_dev->dev = &pdev->dev;
    vi_dev->id = pdev->id;

    videv_name = devm_kzalloc(&pdev->dev, 16, GFP_KERNEL);
    if (!videv_name)
        return -ENOMEM;
    snprintf(videv_name, 16, "%s.%d", VVCAM_VI_NAME, pdev->id);

    vi_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    vi_dev->miscdev.name  = videv_name;
    vi_dev->miscdev.fops  = &vvcam_vi_fops;

    ret = misc_register(&vi_dev->miscdev);
    if (ret) {
        dev_err(&pdev->dev, "failed to register device\n");
        return -EINVAL;
    }

    // ret = vvcam_vi_procfs_register(vi_dev, &vi_dev->pde);
    // if (ret) {
    //     dev_err(&pdev->dev, "vi register procfs failed.\n");
    //     goto error_request_vi_irq;
    // }

    // tasklet_init(&vi_dev->stat_tasklet,
    //     vvcam_vi_irq_stat_tasklet, (unsigned long)vi_dev);

    ret = devm_request_irq(&pdev->dev, vi_dev->vi_irq, vvcam_vi_irq_handler,
                IRQF_TRIGGER_HIGH | IRQF_SHARED, dev_name(&pdev->dev), vi_dev);
    if (ret) {
        dev_err(&pdev->dev, "can't request vi irq\n");
        goto error_request_vi_irq;
    }

    disable_irq(vi_dev->vi_irq);
    dev_info(&pdev->dev, "vvcam vi driver probe success\n");

    return 0;


error_request_vi_irq:
    misc_deregister(&vi_dev->miscdev);
    return ret;
}

static int vvcam_vi_remove(struct platform_device *pdev)
{
    struct vvcam_vi_dev *vi_dev;

    vi_dev = platform_get_drvdata(pdev);

    // vvcam_vi_procfs_unregister(vi_dev->pde);

    misc_deregister(&vi_dev->miscdev);
    enable_irq(vi_dev->vi_irq);

    devm_free_irq(&pdev->dev, vi_dev->vi_irq, vi_dev);

    return 0;
}

static int vvcam_vi_system_suspend(struct device *dev)
{
    int ret = 0;
    ret = pm_runtime_force_suspend(dev);
    if (ret) {
        dev_err(dev, "force suspend %s failed\n", dev_name(dev));
        return ret;
    }
    return ret;
}

static int vvcam_vi_system_resume(struct device *dev)
{
    int ret = 0;
    ret = pm_runtime_force_resume(dev);
    if (ret) {
        dev_err(dev, "force resume %s failed\n", dev_name(dev));
        return ret;
    }
    return ret;
}

static int vvcam_vi_runtime_suspend(struct device *dev)
{
    return 0;
}

static int vvcam_vi_runtime_resume(struct device *dev)
{
    return 0;
}

static const struct dev_pm_ops vvcam_vi_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(vvcam_vi_system_suspend, vvcam_vi_system_resume)
    SET_RUNTIME_PM_OPS(vvcam_vi_runtime_suspend, vvcam_vi_runtime_resume, NULL)
};

static const struct of_device_id vvcam_vi_of_match[] = {
    {.compatible = "verisilicon,vi",},
    { /* sentinel */ },
};

static struct platform_driver vvcam_vi_driver = {
    .probe  = vvcam_vi_probe,
    .remove = vvcam_vi_remove,
    .driver = {
        .name           = VVCAM_VI_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = vvcam_vi_of_match,
        .pm             = &vvcam_vi_pm_ops,
    }
};

static int __init vvcam_vi_init_module(void)
{
    int ret;
    ret = platform_driver_register(&vvcam_vi_driver);
    if (ret) {
        printk(KERN_ERR "Failed to register vi driver\n");
        return ret;
    }

    ret = vvcam_vi_platform_device_register();
    if (ret) {
        platform_driver_unregister(&vvcam_vi_driver);
        printk(KERN_ERR "Failed to register vvcam vi platform devices\n");
        return ret;
    }

    return ret;
}

static void __exit vvcam_vi_exit_module(void)
{
    platform_driver_unregister(&vvcam_vi_driver);
    vvcam_vi_platform_device_unregister();
}

late_initcall(vvcam_vi_init_module);
module_exit(vvcam_vi_exit_module);

MODULE_DESCRIPTION("Verisilicon video in driver");
MODULE_AUTHOR("Verisilicon ISP SW Team");
MODULE_LICENSE("GPL");
