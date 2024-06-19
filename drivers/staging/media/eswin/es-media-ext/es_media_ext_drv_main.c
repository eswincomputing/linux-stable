#include "./common/dev_common.h"
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <asm/atomic.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include "./module/dev_module.h"
#include "./chn/dev_channel.h"

#define DRV_DESC   "es media driver"

dev_t first_dev_number = 0;
static struct class *media_dev_class;
static struct cdev media_cdev[NO_OF_DEVICES];

/*
** Function Prototypes
*/
static int  __init es_media_init(void);
static void __exit es_media_exit(void);

/*
** Module Init function
*/
static int __init es_media_init(void) {
    int i = 0;

    /*Allocating Major number*/
    if ((alloc_chrdev_region(&first_dev_number, 0, NO_OF_DEVICES, "es_media_ext_drv")) <0) {
        pr_info("Cannot allocate major number\n");
        return -1;
    }
    pr_info("%s ver: Major %d, Minor %d \n", DRV_DESC, MAJOR(first_dev_number), MINOR(first_dev_number));

     /* Create device class under /sys/class */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
    if (IS_ERR(media_dev_class = class_create("es_media_ext_class"))) {
#else
    if (IS_ERR(media_dev_class = class_create(THIS_MODULE, "es_media_ext_class"))) {
#endif
        pr_info("Cannot create the struct class\n");
        goto unreg_chrdev;
    }

    for (i = 0; i < NO_OF_DEVICES; i++) {
        //Initialize the cdev structure with fops
        switch(i) {
        case DEC_MODULE:
        case ENC_MODULE:
        case BMS_MODULE:
        case VPS_MODULE:
        case VO_MODULE:
            dev_module_init(i);
            dev_module_init_cdec(&media_cdev[i]);
            break;
        case DEC_CHANNEL:
        case ENC_CHANNEL:
        case VPS_CHANNEL:
        case VO_CHANNEL:
            dev_channel_init(i);
            dev_channel_init_cdec(&media_cdev[i]);
            break;
        default:  //never be here
            break;
        }
        media_cdev[i].owner = THIS_MODULE;

        //Register a device (cdev structure) with VFS
        if (cdev_add(&media_cdev[i], MKDEV(MAJOR(first_dev_number), i), 1) < 0) {
            pr_info("Cannot add the device %d to the system\n", i);
            goto cdev_del;
        }

        if(IS_ERR(device_create(media_dev_class, NULL, MKDEV(MAJOR(first_dev_number), i), NULL, "%s%d",
            get_dev_name(i), get_dev_sub_name(i)))) {
            pr_info("Cannot create the Device %s%d\n", ES_DEVICE_DEC, i);
            goto class_del;
        }
    }

    pr_info("%s insert...done!!!\n", DRV_DESC);
    return 0;

cdev_del:
class_del:
    for (; i >= 0; i--) {
        device_destroy(media_dev_class, MKDEV(MAJOR(first_dev_number), i));
        cdev_del(&media_cdev[i]);
    }
    class_destroy(media_dev_class);

unreg_chrdev:
    unregister_chrdev_region(first_dev_number, NO_OF_DEVICES);
    return -1;
}

/*
** Module exit function
*/
static void __exit es_media_exit(void) {
    int i = 0;

    dev_module_deinit();
    dev_channel_deinit();

    for (; i < NO_OF_DEVICES; i++) {
        device_destroy(media_dev_class, MKDEV(MAJOR(first_dev_number), i));
        cdev_del(&media_cdev[i]);
    }
    class_destroy(media_dev_class);

    unregister_chrdev_region(first_dev_number, NO_OF_DEVICES);
    pr_info("%s remove...done!!!\n", DRV_DESC);
}

module_init(es_media_init);
module_exit(es_media_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eswin");
MODULE_DESCRIPTION(DRV_DESC);
MODULE_VERSION("1.0");
