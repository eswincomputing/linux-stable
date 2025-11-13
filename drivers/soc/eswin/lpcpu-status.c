// register_misc_driver.c
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/io.h>

#define DRIVER_NAME "register_misc"

struct register_device {
    struct miscdevice misc_dev;
    struct device *dev;
    void __iomem *reg0;
    void __iomem *reg1;
};

static struct register_device *reg_dev;

static ssize_t die0_show(struct device *dev,
                                   struct device_attribute *attr,
                                   char *buf)
{
    struct register_device *reg_dev = dev_get_drvdata(dev);
    uint32_t value;

    value = readl_relaxed(reg_dev->reg0);

    return scnprintf(buf, PAGE_SIZE, "0x%08X\n", value);
}

static DEVICE_ATTR(die0, 0444, die0_show, NULL);

static ssize_t die1_show(struct device *dev,
                                   struct device_attribute *attr,
                                   char *buf)
{
    struct register_device *reg_dev = dev_get_drvdata(dev);
    uint32_t value;

    value = readl_relaxed(reg_dev->reg1);

    return scnprintf(buf, PAGE_SIZE, "0x%08X\n", value);
}

static DEVICE_ATTR(die1, 0444, die1_show, NULL);

static int __init register_driver_init(void)
{
    int ret;

    pr_info("Initializing register misc driver\n");

    reg_dev = kzalloc(sizeof(*reg_dev), GFP_KERNEL);
    if (!reg_dev) {
        pr_err("Failed to allocate device memory\n");
        return -ENOMEM;
    }

    reg_dev->reg0 = ioremap(0x5181066c, 0x04);
    reg_dev->reg1 = ioremap(0x7181066c, 0x04);
    reg_dev->misc_dev.minor = MISC_DYNAMIC_MINOR;
    reg_dev->misc_dev.name = DRIVER_NAME;
    reg_dev->misc_dev.mode = 0666;

    ret = misc_register(&reg_dev->misc_dev);
    if (ret) {
        pr_err("Failed to register misc device\n");
        goto err_misc_register;
    }

    reg_dev->dev = reg_dev->misc_dev.this_device;
    dev_set_drvdata(reg_dev->dev, reg_dev);

    ret = device_create_file(reg_dev->dev, &dev_attr_die0);
    if (ret) {
        pr_err("Failed to create sysfs attribute\n");
        goto err_sysfs;
    }

    ret = device_create_file(reg_dev->dev, &dev_attr_die1);
    if (ret) {
        pr_err("Failed to create sysfs attribute\n");
        goto err_sysfs;
    }

    return 0;

err_sysfs:
    misc_deregister(&reg_dev->misc_dev);
err_misc_register:
    kfree(reg_dev);
    return ret;
}

static void __exit register_driver_exit(void)
{
    pr_info("Unloading register misc driver\n");
    
    if (reg_dev) {
        device_remove_file(reg_dev->dev, &dev_attr_die0);
        device_remove_file(reg_dev->dev, &dev_attr_die1);
        misc_deregister(&reg_dev->misc_dev);
        kfree(reg_dev);
    }
    
    pr_info("Register misc driver unloaded\n");
}

module_init(register_driver_init);
module_exit(register_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("MISC driver for register access via sysfs");
MODULE_VERSION("1.0");