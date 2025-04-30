#include <asm/io.h>
#include <linux/cdev.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/poll.h>

#include "vicap_ioctl.h"

#define VICAP_NAME "vicap"

static unsigned int vvcam_vicap_major = 0;
static unsigned int vvcam_vicap_minor = 0;
static struct class *vvcam_vicap_class = NULL;

vicap_dev_info_t *vicap_dev_info[VICAP_DEV_MAXCNT] = {NULL,NULL,NULL,NULL,NULL,NULL};

static int vicap_open(struct inode *inode, struct file *file)
{
	struct vicap_device *vicap_dev = NULL;

	vicap_dev = container_of(inode->i_cdev, struct vicap_device, cdev);
	if (!vicap_dev) {
		pr_err("vicap dev cannot found\n");
		return -ENODEV;
	}
	file->private_data = vicap_dev;

	return 0;
}

static long vicap_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret;
	struct vicap_device *vicap_dev = (struct vicap_device *)file->private_data;

	if (!vicap_dev) {
		pr_err("vicap dev cannot found\n");
		return -ENODEV;
	}

	ret = vicap_priv_ioctl(vicap_dev, cmd, (void __user *)arg);

	return ret;
}

static unsigned int vicap_poll(struct file *file, poll_table *wait)
{
	int mask = 0;

	return mask;
}

static int vicap_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations vicap_fops = {
	.owner          = THIS_MODULE,
	.open           = vicap_open,
	.release        = vicap_release,
	.unlocked_ioctl = vicap_ioctl,
	.poll           = vicap_poll,
};

static int vvcam_vicap_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *mem;
	struct vicap_device *vicap_dev;

	pr_info("enter %s\n", __func__);

	if (device_property_read_u32(&pdev->dev, "id", &pdev->id)) {
		dev_err(&pdev->dev, "No device id found\n");
		return -EINVAL;
	}

	if (pdev->id >= VICAP_DEV_MAXCNT) {
		dev_err(&pdev->dev, "Device id %d error!\n", pdev->id);
		return -EINVAL;
	}

	vicap_dev = devm_kzalloc(&pdev->dev, sizeof(struct vicap_device), GFP_KERNEL);
	if (!vicap_dev) {
		pr_err("%s: kzalloc failed\n", __func__);
		return -ENOMEM;
	}

	if (!vicap_dev_info[pdev->id]) {
		vicap_dev_info[pdev->id] = devm_kzalloc(&pdev->dev,
				    sizeof(vicap_dev_info_t), GFP_KERNEL);
		if (!vicap_dev_info[pdev->id]) {
			pr_err("%s: kzalloc failed\n", __func__);
			return -ENOMEM;
		}

		vicap_dev_info[1] = devm_kzalloc(&pdev->dev,
				    sizeof(vicap_dev_info_t), GFP_KERNEL);
	} else {
		pr_err("vicap dev info already been alloced\n");
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	vicap_dev->base = devm_ioremap_resource(&pdev->dev, mem);
	vicap_dev->id = pdev->id;
	vicap_dev_info[pdev->id]->id = pdev->id;
	mutex_init(&vicap_dev->vicap_mutex);

	platform_set_drvdata(pdev, vicap_dev);

	if (vvcam_vicap_major == 0) {
		ret = alloc_chrdev_region(&vicap_dev->devt, 0, VICAP_DEV_MAXCNT, VICAP_NAME);
		if (ret != 0) {
			pr_err("%s: alloc_chrdev_region error\n", __func__);
			return ret;
		}
		vvcam_vicap_major = MAJOR(vicap_dev->devt);
		vvcam_vicap_minor = MINOR(vicap_dev->devt);
	} else {
		vicap_dev->devt = MKDEV(vvcam_vicap_major, vvcam_vicap_minor);
		ret = register_chrdev_region(vicap_dev->devt, VICAP_DEV_MAXCNT,	VICAP_NAME);
		if (ret) {
			pr_err("%s:register_chrdev_region error\n",	__func__);
			return ret;
		}
	}

// #if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
	vvcam_vicap_class = class_create(VICAP_NAME);
// #else
// 	vvcam_vicap_class = class_create(THIS_MODULE, VICAP_NAME);
// #endif
	if (IS_ERR(vvcam_vicap_class)) {
		pr_err("%s[%d]:class_create error!\n", __func__, __LINE__);
		return -EINVAL;
	}

	vicap_dev->devt = MKDEV(vvcam_vicap_major, vvcam_vicap_minor + pdev->id);

	cdev_init(&vicap_dev->cdev, &vicap_fops);
	ret = cdev_add(&vicap_dev->cdev, vicap_dev->devt, 1);
	if (ret) {
		pr_err("%s[%d]:cdev_add error!\n", __func__, __LINE__);
		return ret;
	}
	vicap_dev->class = vvcam_vicap_class;
	device_create(vicap_dev->class, NULL, vicap_dev->devt,
		      vicap_dev, "%s%d", VICAP_NAME, pdev->id);

	pr_info("exit %s\n", __func__);

	return ret;
}

static int vvcam_vicap_remove(struct platform_device *pdev)
{
    struct vicap_device *vicap_dev;

    vicap_dev = platform_get_drvdata(pdev);

    cdev_del(&vicap_dev->cdev);
    device_destroy(vicap_dev->class, vicap_dev->devt);
    unregister_chrdev_region(vicap_dev->devt, VICAP_DEV_MAXCNT);
    class_destroy(vicap_dev->class);

	return 0;
}

static const struct of_device_id vicap_of_match[] = {
	{ .compatible = "eswin,vicap" },
	{},
};

MODULE_DEVICE_TABLE(of, vicap_of_match);

static struct platform_driver vicap_driver = {
	.probe = vvcam_vicap_probe,
	.remove = vvcam_vicap_remove,
	.driver = {
		   .name = VICAP_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = vicap_of_match,
	}
};

static int __init vicap_init_module(void)
{
	int ret = 0;

	pr_info("enter %s\n", __func__);

	ret = platform_driver_register(&vicap_driver);
	if (ret) {
		pr_err("register platform driver failed.\n");
		return ret;
	}

	return ret;
}

static void __exit vicap_exit_module(void)
{
	pr_info("enter %s\n", __func__);

	platform_driver_unregister(&vicap_driver);
}

late_initcall(vicap_init_module);
module_exit(vicap_exit_module);

MODULE_DESCRIPTION("VICAP");
MODULE_LICENSE("GPL");

