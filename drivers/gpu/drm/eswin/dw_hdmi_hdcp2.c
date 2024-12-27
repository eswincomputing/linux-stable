// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN hdcp driver
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
 * Authors: DengLei <denglei@eswincomputing.com>
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/platform_device.h>

#include <linux/version.h>
#include <linux/netlink.h>
#include <linux/proc_fs.h>
#include <linux/moduleparam.h>
#include <linux/clk.h>
#include <linux/of.h>

#include "host_lib_driver_linux_if.h"
#include "dw_hdmi_hdcp.h"

/**
 * \file
 * \ingroup HL_Driver_Kernel
 * \brief Sample Linux Host Library Driver
 * \copydoc HL_Driver_Kernel
 */

/**
 * \defgroup HL_Driver_Linux Sample Linux Host Library Driver
 * \ingroup HL_Driver
 * \brief Sample code for the Linux Host Library Driver.
 * The Linux Host Library Driver is composed of 2 parts:
 * 1. A kernel driver.
 * 2. A file access instance.
 *
 * The kernel driver is the kernel executable code enabling the firmware to execute.
 * It provides the access to the hardware register to interact with the firmware.
 *
 * The file access instance initializes the #hl_driver_t structure for the
 * host library access.  The Host Library references the file access to request the
 * kernel operations.
 */

/**
 * \defgroup HL_Driver_Kernel Sample Linux Kernel Host Library Driver
 * \ingroup HL_Driver_Linux
 * \brief Example code for the Linux Kernel Host Library Driver.
 *
 * The Sample Linux Kernel Driver operates on the linux kernel.
 * To install (requires root access):
 * \code
 insmod bin/linux_hld_module.ko verbose=0
 * \endcode
 *
 * To remove (requires root access):
 * \code
 rmmod linux_hld_module
 * \endcode
 *
 * Example Linux Host Library Code:
 * \code
 */

#define MAX_HL_DEVICES 16
#define TROOT_GRIFFIN

static hl_device hl_devices[2][MAX_HL_DEVICES];

static bool randomize_mem = false;
module_param(randomize_mem, bool, 0);
MODULE_PARM_DESC(noverify, "Wipe memory allocations on startup (for debug)");

static void dw_hdcp2_stop(struct dw_hdcp2 *hdcp2)
{
	dev_info(hdcp2->dev, "%s\n", __func__);
	hdcp2->hot_plug = 0;
	dw_hdmi_hdcp2_start(0, hdcp2->numa_id);
}

static void dw_hdcp2_start(struct dw_hdcp2 *hdcp2)
{
	dev_info(hdcp2->dev, "%s\n", __func__);
	dw_hdmi_hdcp2_start(1, hdcp2->numa_id);
	hdcp2->hot_plug = 1;
}

/* HL_DRV_IOC_MEMINFO implementation */
static long get_meminfo(hl_device *hl_dev, void __user *arg)
{
	struct hl_drv_ioc_meminfo info;

	if ((hl_dev == 0) || (arg == 0) || (hl_dev->hpi_resource == 0)) {
		return -EFAULT;
	}

	info.hpi_base = hl_dev->hpi_resource->start;
	info.code_base = hl_dev->code_base;
	info.code_size = hl_dev->code_size;
	info.data_base = hl_dev->data_base;
	info.data_size = hl_dev->data_size;

	if (copy_to_user(arg, &info, sizeof info) != 0) {
		return -EFAULT;
	}

	return 0;
}

/* HL_DRV_IOC_LOAD_CODE implementation */
static long load_code(hl_device *hl_dev, struct hl_drv_ioc_code __user *arg)
{
	struct hl_drv_ioc_code head;

	if ((hl_dev == 0) || (arg == 0) || (hl_dev->code == 0) ||
	    (hl_dev->data == 0)) {
		return -EFAULT;
	}

	if (copy_from_user(&head, arg, sizeof head) != 0)
		return -EFAULT;

	if (head.len > hl_dev->code_size)
		return -ENOSPC;

	if (hl_dev->code_loaded)
		return -EBUSY;

	if (randomize_mem) {
		get_random_bytes(hl_dev->code, hl_dev->code_size);
		get_random_bytes(hl_dev->data, hl_dev->data_size);
	}

	if (copy_from_user(hl_dev->code, &arg->data, head.len) != 0)
		return -EFAULT;

	hl_dev->code_loaded = 1;
	return 0;
}

/* HL_DRV_IOC_WRITE_DATA implementation */
static long write_data(hl_device *hl_dev, struct hl_drv_ioc_data __user *arg)
{
	struct hl_drv_ioc_data head;

	if ((hl_dev == 0) || (arg == 0) || (hl_dev->data == 0)) {
		return -EFAULT;
	}

	if (copy_from_user(&head, arg, sizeof head) != 0)
		return -EFAULT;

	if (hl_dev->data_size < head.len)
		return -ENOSPC;
	if (hl_dev->data_size - head.len < head.offset)
		return -ENOSPC;

	if (copy_from_user(hl_dev->data + head.offset, &arg->data, head.len) !=
	    0)
		return -EFAULT;

	return 0;
}

/* HL_DRV_IOC_READ_DATA implementation */
static long read_data(hl_device *hl_dev, struct hl_drv_ioc_data __user *arg)
{
	struct hl_drv_ioc_data head;

	if ((hl_dev == 0) || (arg == 0) || (hl_dev->data == 0)) {
		return -EFAULT;
	}

	if (copy_from_user(&head, arg, sizeof head) != 0)
		return -EFAULT;

	if (hl_dev->data_size < head.len)
		return -ENOSPC;
	if (hl_dev->data_size - head.len < head.offset)
		return -ENOSPC;

	if (copy_to_user(&arg->data, hl_dev->data + head.offset, head.len) != 0)
		return -EFAULT;

	return 0;
}

/* HL_DRV_IOC_MEMSET_DATA implementation */
static long set_data(hl_device *hl_dev, void __user *arg)
{
	union {
		struct hl_drv_ioc_data data;
		unsigned char buf[sizeof(struct hl_drv_ioc_data) + 1];
	} u;

	if ((hl_dev == 0) || (arg == 0) || (hl_dev->data == 0)) {
		return -EFAULT;
	}

	if (copy_from_user(&u.data, arg, sizeof u.buf) != 0)
		return -EFAULT;

	if (hl_dev->data_size < u.data.len)
		return -ENOSPC;
	if (hl_dev->data_size - u.data.len < u.data.offset)
		return -ENOSPC;

	memset(hl_dev->data + u.data.offset, u.data.data[0], u.data.len);
	return 0;
}

/* HL_DRV_IOC_READ_HPI implementation */
static long hpi_read(hl_device *hl_dev, void __user *arg)
{
	struct hl_drv_ioc_hpi_reg reg;

	if ((hl_dev == 0) || (arg == 0) || (hl_dev->hpi_resource == 0)) {
		return -EFAULT;
	}

	if (copy_from_user(&reg, arg, sizeof reg) != 0)
		return -EFAULT;

	if ((reg.offset & 3) ||
	    reg.offset >= resource_size(hl_dev->hpi_resource))
		return -EINVAL;

	reg.value = ioread32(hl_dev->hpi + reg.offset);

	if (copy_to_user(arg, &reg, sizeof reg) != 0)
		return -EFAULT;

	return 0;
}

/* HL_DRV_IOC_WRITE_HPI implementation */
static long hpi_write(hl_device *hl_dev, void __user *arg)
{
	struct hl_drv_ioc_hpi_reg reg;

	if ((hl_dev == 0) || (arg == 0)) {
		return -EFAULT;
	}

	if (copy_from_user(&reg, arg, sizeof reg) != 0)
		return -EFAULT;

	if ((reg.offset & 3) ||
	    reg.offset >= resource_size(hl_dev->hpi_resource))
		return -EINVAL;

	iowrite32(reg.value, hl_dev->hpi + reg.offset);

#ifdef TROOT_GRIFFIN
	//  If Kill command
	//  (HL_GET_CMD_EVENT(krequest.data) == TROOT_CMD_SYSTEM_ON_EXIT_REQ))
	//
	if ((reg.offset == 0x38) && ((reg.value & 0x000000ff) == 0x08)) {
		hl_dev->code_loaded = 0;
	}
#endif
	return 0;
}

static hl_device *alloc_hl_dev_slot(const struct hl_drv_ioc_meminfo *info, int numa_id)
{
	int i;

	if (info == 0) {
		return 0;
	}

	/* Check if we have a matching device (same HPI base) */
	for (i = 0; i < MAX_HL_DEVICES; i++) {
		hl_device *slot = &hl_devices[numa_id][i];
		if (slot->allocated &&
		    (info->hpi_base == slot->hpi_resource->start))
			return slot;
	}

	/* Find unused slot */
	for (i = 0; i < MAX_HL_DEVICES; i++) {
		hl_device *slot = &hl_devices[numa_id][i];
		if (!slot->allocated) {
			slot->allocated = 1;
			return slot;
		}
	}

	return 0;
}

static void free_dma_areas(struct dw_hdcp2 *hdcp2)
{
	hl_device *hl_dev =  hdcp2->hld;

	if (hl_dev == 0) {
		return;
	}

	if (!hdcp2->numa_id) {
		if (!hl_dev->code_is_phys_mem && hl_dev->code) {
			dma_free_coherent(0, hl_dev->code_size, hl_dev->code,
					hl_dev->code_base);
			hl_dev->code = 0;
		}

		if (!hl_dev->data_is_phys_mem && hl_dev->data) {
			dma_free_coherent(0, hl_dev->data_size, hl_dev->data,
					hl_dev->data_base);
			hl_dev->data = 0;
		}
	} else {
		if (!hl_dev->code_is_phys_mem && hl_dev->code) {
			dev_mem_vunmap(&hdcp2->code_buffer, hl_dev->code);

			if (hdcp2->code_attach) {
				dev_mem_detach(hdcp2->code_attach, DMA_BIDIRECTIONAL);
			}

			if (hdcp2->code_buffer.buf) {
				dev_mem_free(hdcp2->code_buffer.buf);
			}
		}

		if (!hl_dev->data_is_phys_mem && hl_dev->data) {
			dev_mem_vunmap(&hdcp2->data_buffer, hl_dev->data);

			if (hdcp2->data_attach) {
				dev_mem_detach(hdcp2->data_attach, DMA_BIDIRECTIONAL);
			}

			if (hdcp2->data_buffer.buf) {
				dev_mem_free(hdcp2->data_buffer.buf);
			}
		}
	}
}

static int alloc_dma_areas(struct dw_hdcp2 *hdcp2, const struct hl_drv_ioc_meminfo *info)
{
	int ret;
	hl_device *hl_dev = hdcp2->hld;
	struct device *dev = hdcp2->dev;

	if ((hl_dev == 0) || (info == 0)) {
		return -EFAULT;
	}

	hl_dev->code_size = info->code_size;
	hl_dev->code_is_phys_mem =
		(info->code_base != HL_DRIVER_ALLOCATE_DYNAMIC_MEM);

	if (hl_dev->code_is_phys_mem && (hl_dev->code == 0)) {
		/* TODO: support highmem */
		hl_dev->code_base = info->code_base;
		hl_dev->code = phys_to_virt(hl_dev->code_base);
	} else {
		if (!hdcp2->numa_id) {
			hl_dev->code = dma_alloc_coherent(dev, hl_dev->code_size,
											  &hl_dev->code_base, GFP_KERNEL);
			if (!hl_dev->code) {
				return -ENOMEM;
			}
			dev_info(dev,"code dma addr:0x%llx, size:0x%x\n", hl_dev->code_base, hl_dev->code_size);
		} else {
			ret = dev_mem_alloc(hl_dev->code_size, ES_MEM_ALLOC_SPRAM_DIE1,
								&hdcp2->code_buffer.buf);
			if (ret < 0) {
				dev_err(dev, "dev_mem_alloc code buf failed!\n");
				return -ENOMEM;
			}

			hl_dev->code_base = dev_mem_attach(hdcp2->code_buffer.buf, dev,
											   DMA_BIDIRECTIONAL, &hdcp2->code_attach);
			if (hl_dev->code_base == 0) {
				dev_err(dev, "dev_mem_attach code buf failed!\n");
				goto release_code_buf;
			}

			hl_dev->code = dev_mem_vmap(&hdcp2->code_buffer);
			if (!hl_dev->code) {
				dev_err(dev, "dev_mem_vmap code buf failed!\n");
				goto release_code_attach;
			}
			dev_info(dev, "code spram addr:0x%llx, size:0x%x\n", hl_dev->code_base, hl_dev->code_size);
		}
	}

	hl_dev->data_size = info->data_size;
	hl_dev->data_is_phys_mem =
		(info->data_base != HL_DRIVER_ALLOCATE_DYNAMIC_MEM);

	if (hl_dev->data_is_phys_mem && (hl_dev->data == 0)) {
		hl_dev->data_base = info->data_base;
		hl_dev->data = phys_to_virt(hl_dev->data_base);
	} else {
		if (!hdcp2->numa_id) {
			hl_dev->data = dma_alloc_coherent(dev, hl_dev->data_size,
											  &hl_dev->data_base, GFP_KERNEL);
			if (!hl_dev->data) {
				free_dma_areas(hdcp2);
				return -ENOMEM;
			}
			dev_info(dev,"data dma addr:0x%llx, size:0x%x\n", hl_dev->code_base, hl_dev->data_size);
		} else {
			ret = dev_mem_alloc(hl_dev->data_size, ES_MEM_ALLOC_SPRAM_DIE1,
								&hdcp2->data_buffer.buf);
			if (ret < 0) {
				dev_err(dev, "dev_mem_alloc data buf failed!\n");
				goto release_code_vmap;
			}

			hl_dev->data_base = dev_mem_attach(hdcp2->data_buffer.buf, dev,
											   DMA_BIDIRECTIONAL, &hdcp2->data_attach);
			if (hl_dev->data_base == 0) {
				dev_err(dev, "dev_mem_attach data buf failed!\n");
				goto release_data_buf;
			}

			hl_dev->data = dev_mem_vmap(&hdcp2->data_buffer);
			if (!hl_dev->data) {
				dev_err(dev, "dev_mem_vmap data buf failed!\n");
				goto release_data_attach;
			}
			dev_info(dev, "data spram addr:0x%llx, size:0x%x\n", hl_dev->data_base, hl_dev->data_size);
		}
	}

	return 0;

release_data_attach:
	dev_mem_detach(hdcp2->data_attach, DMA_BIDIRECTIONAL);
release_data_buf:
	dev_mem_free(hdcp2->data_buffer.buf);
release_code_vmap:
	dev_mem_vunmap(&hdcp2->code_buffer, hl_dev->code);
release_code_attach:
	dev_mem_detach(hdcp2->code_attach, DMA_BIDIRECTIONAL);
release_code_buf:
	dev_mem_free(hdcp2->code_buffer.buf);

	return -ENOMEM;
}

/* HL_DRV_IOC_INIT implementation */
static long hld_init(struct dw_hdcp2 *hdcp2, void __user *arg)
{
	struct resource *hpi_mem;
	struct hl_drv_ioc_meminfo info;
	int rc;

	if ((arg == 0)) {
		dev_err(hdcp2->dev, "param error!\n");
		return -EFAULT;
	}

	if (copy_from_user(&info, arg, sizeof info) != 0)
		return -EFAULT;

	hdcp2->hld = alloc_hl_dev_slot(&info, hdcp2->numa_id);
	if (!hdcp2->hld)
		return -EMFILE;

	if (!hdcp2->hld->initialized) {
		rc = alloc_dma_areas(hdcp2, &info);
		if (rc < 0)
			goto err_free;

		hpi_mem = request_mem_region(info.hpi_base, 128, "hl_dev-hpi");
		if (!hpi_mem) {
			rc = -EADDRNOTAVAIL;
			goto err_free;
		}

		hdcp2->hld->hpi = ioremap(hpi_mem->start, resource_size(hpi_mem));
		if (!hdcp2->hld->hpi) {
			rc = -ENOMEM;
			goto err_release_region;
		}
		hdcp2->hld->hpi_resource = hpi_mem;
		hdcp2->hld->initialized = 1;
	}

	return 0;

err_release_region:
	release_resource(hpi_mem);
err_free:
	free_dma_areas(hdcp2);
	hdcp2->hld->initialized = 0;
	hdcp2->hld->allocated = 0;
	hdcp2->hld->hpi_resource = 0;
	hdcp2->hld->hpi = 0;

	return rc;
}

static void free_hl_dev_slot(hl_device *slot)
{
	if (slot == 0) {
		return;
	}

	if (!slot->allocated)
		return;

	if (slot->initialized) {
		if (slot->hpi) {
			iounmap(slot->hpi);
			slot->hpi = 0;
		}

		if (slot->hpi_resource) {
			release_mem_region(slot->hpi_resource->start, 128);
			slot->hpi_resource = 0;
		}
	}

	slot->initialized = 0;
	slot->allocated = 0;
}

static long hld_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *misc_hld = f->private_data;
	struct dw_hdcp2 *hdcp2 = dev_get_drvdata(misc_hld->parent);
	void __user *data;

	if (f == 0) {
		return -EFAULT;
	}

	data = (void __user *)arg;

	if (cmd == HL_DRV_IOC_INIT) {
		return hld_init(hdcp2, data);
	} else if (!hdcp2->hld) {
		return -EAGAIN;
	}

	switch (cmd) {
	case HL_DRV_IOC_INIT:
		return hld_init(hdcp2, data);
	case HL_DRV_IOC_MEMINFO:
		return get_meminfo(hdcp2->hld, data);
	case HL_DRV_IOC_READ_HPI:
		return hpi_read(hdcp2->hld, data);
	case HL_DRV_IOC_WRITE_HPI:
		return hpi_write(hdcp2->hld, data);
	case HL_DRV_IOC_LOAD_CODE:
		return load_code(hdcp2->hld, data);
	case HL_DRV_IOC_WRITE_DATA:
		return write_data(hdcp2->hld, data);
	case HL_DRV_IOC_READ_DATA:
		return read_data(hdcp2->hld, data);
	case HL_DRV_IOC_MEMSET_DATA:
		return set_data(hdcp2->hld, data);

	case DW_DRV_IOC_CONNECT_STATUS:
		return hdcp2->hot_plug;
	case DW_DRV_IOC_CONNECT_SET:
		dev_info(hdcp2->dev, "set hdcp2 reset one\n");
		hdcp2->wait_hdcp2_reset = 1;
		dw_hdmi_hdcp2_start(1, hdcp2->numa_id);
		return 0;
	case DW_DRV_IOC_DISCONNECT_SET:
		if (hdcp2->wait_hdcp2_reset == 1) {
			printk("set hdcp2 reset zero\n");
			hdcp2->wait_hdcp2_reset = 0;
			dw_hdmi_hdcp2_start(0, hdcp2->numa_id);
		}
		if (hdcp2->auth_sucess == 1) {
			hdcp2->auth_sucess = 0;
		}
		return 0;
	case DW_DRV_IOC_AUTH_SUCCESS:
		hdcp2->auth_sucess = 1;
		return 0;
	case DW_DRV_IOC_AUTH_FAIL:
		hdcp2->auth_sucess = 0;
		return 0;
	case DW_DRV_IOC_NO_CAPACITY:
		printk("set hdcp2 reset zero 3005\n");
		hdcp2->hot_plug = 0;
		hdcp2->wait_hdcp2_reset = 0;
		dw_hdmi_hdcp2_start(0, hdcp2->numa_id);
		dw_hdmi_hdcp2_start(2, hdcp2->numa_id);
		return 0;
	}

	return -ENOTTY;
}

static const struct file_operations hld_file_operations = {
#ifdef CONFIG_COMPAT
	.compat_ioctl = hld_ioctl,
#endif
	.unlocked_ioctl = hld_ioctl,
	.owner = THIS_MODULE,
};

static int rigister_hld_device(struct dw_hdcp2 *hdcp2)
{
	int i;
	int numa_id = hdcp2->numa_id;
	char name[20];

	for (i = 0; i < MAX_HL_DEVICES; i++) {
		hl_devices[numa_id][i].allocated = 0;
		hl_devices[numa_id][i].initialized = 0;
		hl_devices[numa_id][i].code_loaded = 0;
		hl_devices[numa_id][i].code = 0;
		hl_devices[numa_id][i].data = 0;
		hl_devices[numa_id][i].hpi_resource = 0;
		hl_devices[numa_id][i].hpi = 0;
	}

	sprintf(name, "hl_dev%d", numa_id);
	hdcp2->mics_hld.minor = MISC_DYNAMIC_MINOR;
	hdcp2->mics_hld.name = name;
	hdcp2->mics_hld.fops = &hld_file_operations;
	hdcp2->mics_hld.parent = hdcp2->dev;

	dev_info(hdcp2->dev, "%s numa id:%d\n", __func__, numa_id);

	if (misc_register(&hdcp2->mics_hld)) {
		dev_err(hdcp2->dev, "Could not add character driver\n");
		return -EINVAL;
	}

	return 0;
}

static void hld_exit(struct dw_hdcp2 *hdcp2)
{
	int i;

	for (i = 0; i < MAX_HL_DEVICES; i++) {
		free_hl_dev_slot(&hl_devices[hdcp2->numa_id][i]);
	}

	free_dma_areas(hdcp2);

	misc_deregister(&hdcp2->mics_hld);
}

static int dw_hdmi2_hdcp2_clk_enable(struct dw_hdcp2 *hdcp2)
{
	hdcp2->aclk = devm_clk_get(hdcp2->dev, "aclk");
	if (IS_ERR(hdcp2->aclk)) {
		dev_err(hdcp2->dev, "Unable to get hdcp2 aclk\n");
		return -1;
	}
	clk_prepare_enable(hdcp2->aclk);

	hdcp2->iesmclk = devm_clk_get(hdcp2->dev, "iesmclk");
	if (IS_ERR(hdcp2->iesmclk)) {
		dev_err(hdcp2->dev, "Unable to get hdcp2_clk_hdmi\n");
		return -1;
	}
	clk_prepare_enable(hdcp2->iesmclk);

	return 0;
}

static ssize_t hdcp2_show_enable(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct miscdevice *mdev= dev_get_drvdata(dev);
	struct dw_hdcp2 *hdcp2 = dev_get_drvdata(mdev->parent);

	return snprintf(buf, PAGE_SIZE, "%d\n", hdcp2->enable);
}

static ssize_t hdcp2_store_enable(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t size)
{
	int enable;
	struct miscdevice *mdev= dev_get_drvdata(dev);
	struct dw_hdcp2 *hdcp2 = dev_get_drvdata(mdev->parent);

	if (kstrtoint(buf, 0, &enable))
		return size;

	if (hdcp2->enable != enable) {
		hdcp2->enable = enable;
		if (enable) {
			dw_hdmi_hdcp2_start(3, hdcp2->numa_id);
		} else {
			if (hdcp2->hot_plug) {
				hdcp2->stop(hdcp2);
				dw_hdmi_hdcp2_start(2, hdcp2->numa_id);
			}
		}
	}
	return size;
}

static ssize_t hdcp2_show_status(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct miscdevice *mdev= dev_get_drvdata(dev);
	struct dw_hdcp2 *hdcp2 = dev_get_drvdata(mdev->parent);

	if (hdcp2->enable != 1) {
		return snprintf(buf, PAGE_SIZE, "%s\n", "no enable hdcp2");
	} else if (!hdcp2->hot_plug) {
		return snprintf(buf, PAGE_SIZE, "%s\n", "hdcp2 no auth");
	} else {
		if (hdcp2->auth_sucess)
			return snprintf(buf, PAGE_SIZE, "%s\n",
					"hdcp2 auth sucess");
		else
			return snprintf(buf, PAGE_SIZE, "%s\n",
					"no already auth sucess");
	}
}

static DEVICE_ATTR(enable, 0644, hdcp2_show_enable, hdcp2_store_enable);
static DEVICE_ATTR(status, 0444, hdcp2_show_status, NULL);

static int create_device_node(struct dw_hdcp2 *hdcp2)
{
	int ret;
	char name[20];

	sprintf(name, "hdcp2-%d-node", hdcp2->numa_id);
	hdcp2->mdev.minor = MISC_DYNAMIC_MINOR;
	hdcp2->mdev.name = name;
	hdcp2->mdev.mode = 0666;
	hdcp2->mdev.parent = hdcp2->dev;
	if (misc_register(&(hdcp2->mdev))) {
		dev_err(hdcp2->dev, "HDCP2: Could not add character driver\n");
		return -1;
	}

	ret = device_create_file(hdcp2->mdev.this_device,
				 &dev_attr_enable);
	if (ret) {
		dev_err(hdcp2->dev, "HDCP: Could not add sys file enable\n");
		ret = -EINVAL;
		goto error0;
	}

	ret = device_create_file(hdcp2->mdev.this_device,
				 &dev_attr_status);
	if (ret) {
		dev_err(hdcp2->dev, "HDCP: Could not add sys file status\n");
		ret = -EINVAL;
		goto error1;
	}

	return 0;

error1:
	device_remove_file(hdcp2->mdev.this_device, &dev_attr_enable);
error0:
	misc_deregister(&hdcp2->mdev);
	return ret;
}

static void end_device_node(struct dw_hdcp2 *hdcp2)
{
	device_remove_file(hdcp2->mdev.this_device, &dev_attr_enable);
	device_remove_file(hdcp2->mdev.this_device, &dev_attr_status);
	misc_deregister(&(hdcp2->mdev));
}

static int eswin_hdmi_hdcp2_probe(struct platform_device *pdev)
{
	struct dw_hdcp2 *hdcp2;
	struct device *dev = &pdev->dev;
	int numa_id;

	hdcp2 = devm_kzalloc(dev, sizeof(*hdcp2), GFP_KERNEL);
	if (!hdcp2)
		return -ENOMEM;

	platform_set_drvdata(pdev, hdcp2);
	if(of_property_read_u32(dev->of_node, "numa-node-id", &numa_id)) {
		numa_id = 0;
	}
	dev_info(dev, "%s numa_id:%d\n", __func__, numa_id);
	hdcp2->numa_id = numa_id;

	hdcp2->dev = dev;
	hdcp2->stop = dw_hdcp2_stop;
	hdcp2->start = dw_hdcp2_start;
	rigister_hld_device(hdcp2);
	dw_hdmi2_hdcp2_clk_enable(hdcp2);
	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	dw_hdmi_hdcp2_init(hdcp2);
	dw_hdmi_hdcp2_start(3, numa_id);

	create_device_node(hdcp2);
	return 0;
}

static int eswin_hdmi_hdcp2_remove(struct platform_device *pdev)
{
	struct dw_hdcp2 *hdcp2 = platform_get_drvdata(pdev);
	dev_info(hdcp2->dev, "%s numa id:%d\n", __func__, hdcp2->numa_id);
	dw_hdmi_hdcp2_remove(hdcp2);
	end_device_node(hdcp2);
	hld_exit(hdcp2);

	clk_disable_unprepare(hdcp2->aclk);
	clk_disable_unprepare(hdcp2->iesmclk);

	return 0;
}

static int __maybe_unused dw_hdcp2_suspend(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
	struct dw_hdcp2 *hdcp2 = dev_get_drvdata(dev);

	clk_disable(hdcp2->aclk);
	clk_disable(hdcp2->iesmclk);

	return 0;
}

static int __maybe_unused dw_hdcp2_resume(struct device *dev)
{
	dev_dbg(dev, "%s\n", __func__);
	struct dw_hdcp2 *hdcp2 = dev_get_drvdata(dev);

	clk_enable(hdcp2->aclk);
	clk_enable(hdcp2->iesmclk);

	return 0;
}

static const struct dev_pm_ops dw_hdcp2_pm = {
	.resume = dw_hdcp2_resume,
	.suspend = dw_hdcp2_suspend,
};

static const struct of_device_id dw_hdmi_hdcp2_dt_ids[] = {
	{
		.compatible = "eswin,dw-hdmi-hdcp2",
	},
};
MODULE_DEVICE_TABLE(of, dw_hdmi_hdcp2_dt_ids);

struct platform_driver dw_hdmi_hdcp2_driver = {
	.probe = eswin_hdmi_hdcp2_probe,
	.remove = eswin_hdmi_hdcp2_remove,
	.driver = {
        .name = "dw-hdmi-hdcp2",
        .owner = THIS_MODULE,
		.pm = &dw_hdcp2_pm,
        .of_match_table = of_match_ptr(dw_hdmi_hdcp2_dt_ids),
    },
};

static int __init hdmi_hdcp2_init(void)
{
	return platform_driver_register(&dw_hdmi_hdcp2_driver);
}

static void hdmi_hdcp2_exit(void)
{
	platform_driver_unregister(&dw_hdmi_hdcp2_driver);
}

late_initcall_sync(hdmi_hdcp2_init);
module_exit(hdmi_hdcp2_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Synopsys, Inc.");
MODULE_DESCRIPTION("Linux Host Library Driver");
