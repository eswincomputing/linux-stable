// SPDX-License-Identifier: GPL-2.0-only
/*
 * ESWIN heap APIs
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/xarray.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/nospec.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/eswin_rsvmem_common.h>
#include "include/uapi/linux/eswin_rsvmem_common.h"

#define DEVNAME "eswin_heap"

#define NUM_HEAP_MINORS 128

/**
 * struct eswin_heap - represents a dmabuf heap in the system
 * @name:		used for debugging/device-node name
 * @ops:		ops struct for this heap
 * @heap_devt		heap device node
 * @list		list head connecting to list of heaps
 * @heap_cdev		heap char device
 *
 * Represents a heap of memory from which buffers can be made.
 */
struct eswin_heap {
	const char *name;
	const struct eswin_heap_ops *ops;
	void *priv;
	dev_t heap_devt;
	struct list_head list;
	struct cdev heap_cdev;
};

static LIST_HEAD(heap_list);
static DEFINE_MUTEX(heap_list_lock);
static dev_t eswin_heap_devt;
static struct class *eswin_heap_class;
static DEFINE_XARRAY_ALLOC(eswin_heap_minors);

static int eswin_heap_buffer_alloc(struct eswin_heap *heap, size_t len,
				 unsigned int fd_flags,
				 unsigned int heap_flags)
{
	struct dma_buf *dmabuf;
	int fd;

	/*
	 * Allocations from all heaps have to begin
	 * and end on page boundaries.
	 */
	len = PAGE_ALIGN(len);
	if (!len)
		return -EINVAL;

	dmabuf = heap->ops->allocate(heap, len, fd_flags, heap_flags);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	fd = dma_buf_fd(dmabuf, fd_flags);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		/* just return, as put will call release and that will free */
	}
	return fd;
}

static int eswin_heap_open(struct inode *inode, struct file *file)
{
	struct eswin_heap *heap;

	heap = xa_load(&eswin_heap_minors, iminor(inode));
	if (!heap) {
		pr_err("eswin_heap: minor %d unknown.\n", iminor(inode));
		return -ENODEV;
	}

	/* instance data as context */
	file->private_data = heap;
	nonseekable_open(inode, file);

	return 0;
}

static long eswin_heap_ioctl_allocate(struct file *file, void *data)
{
	struct eswin_heap_allocation_data *heap_allocation = data;
	struct eswin_heap *heap = file->private_data;
	int fd;

	if (heap_allocation->fd)
		return -EINVAL;

	if (heap_allocation->fd_flags & ~ESWIN_HEAP_VALID_FD_FLAGS)
		return -EINVAL;

	if (heap_allocation->heap_flags & ~ESWIN_HEAP_VALID_HEAP_FLAGS)
		return -EINVAL;

	fd = eswin_heap_buffer_alloc(heap, heap_allocation->len,
				   heap_allocation->fd_flags,
				   heap_allocation->heap_flags);
	if (fd < 0)
		return fd;

	heap_allocation->fd = fd;

	return 0;
}

static unsigned int eswin_heap_ioctl_cmds[] = {
	ESWIN_HEAP_IOCTL_ALLOC,
};

static long eswin_heap_ioctl(struct file *file, unsigned int ucmd,
			   unsigned long arg)
{
	char stack_kdata[128];
	char *kdata = stack_kdata;
	unsigned int kcmd;
	unsigned int in_size, out_size, drv_size, ksize;
	int nr = _IOC_NR(ucmd);
	int ret = 0;

	if (nr >= ARRAY_SIZE(eswin_heap_ioctl_cmds))
		return -EINVAL;

	nr = array_index_nospec(nr, ARRAY_SIZE(eswin_heap_ioctl_cmds));
	/* Get the kernel ioctl cmd that matches */
	kcmd = eswin_heap_ioctl_cmds[nr];

	/* Figure out the delta between user cmd size and kernel cmd size */
	drv_size = _IOC_SIZE(kcmd);
	out_size = _IOC_SIZE(ucmd);
	in_size = out_size;
	if ((ucmd & kcmd & IOC_IN) == 0)
		in_size = 0;
	if ((ucmd & kcmd & IOC_OUT) == 0)
		out_size = 0;
	ksize = max(max(in_size, out_size), drv_size);

	/* If necessary, allocate buffer for ioctl argument */
	if (ksize > sizeof(stack_kdata)) {
		kdata = kmalloc(ksize, GFP_KERNEL);
		if (!kdata)
			return -ENOMEM;
	}

	if (copy_from_user(kdata, (void __user *)arg, in_size) != 0) {
		ret = -EFAULT;
		goto err;
	}

	/* zero out any difference between the kernel/user structure size */
	if (ksize > in_size)
		memset(kdata + in_size, 0, ksize - in_size);

	switch (kcmd) {
	case ESWIN_HEAP_IOCTL_ALLOC:
		ret = eswin_heap_ioctl_allocate(file, kdata);
		break;
	default:
		ret = -ENOTTY;
		goto err;
	}

	if (copy_to_user((void __user *)arg, kdata, out_size) != 0)
		ret = -EFAULT;
err:
	if (kdata != stack_kdata)
		kfree(kdata);
	return ret;
}

static const struct file_operations eswin_heap_fops = {
	.owner          = THIS_MODULE,
	.open		= eswin_heap_open,
	.unlocked_ioctl = eswin_heap_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= eswin_heap_ioctl,
#endif
};

/**
 * eswin_heap_get_drvdata() - get per-subdriver data for the heap
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The per-subdriver data for the heap.
 */
void *eswin_heap_get_drvdata(struct eswin_heap *heap)
{
	return heap->priv;
}

/**
 * eswin_heap_get_name() - get heap name
 * @heap: DMA-Heap to retrieve private data for
 *
 * Returns:
 * The char* for the heap name.
 */
const char *eswin_heap_get_name(struct eswin_heap *heap)
{
	return heap->name;
}

struct eswin_heap *eswin_heap_add(const struct eswin_heap_export_info *exp_info)
{
	struct eswin_heap *heap, *h = NULL, *err_ret;
	struct device *dev_ret;
	unsigned int minor;
	int ret;

	if (!exp_info->name || !strcmp(exp_info->name, "")) {
		pr_err("eswin_heap: Cannot add heap without a name\n");
		return ERR_PTR(-EINVAL);
	}

	if (!exp_info->ops || !exp_info->ops->allocate) {
		pr_err("eswin_heap: Cannot add heap with invalid ops struct\n");
		return ERR_PTR(-EINVAL);
	}

	/* check the name is unique */
	mutex_lock(&heap_list_lock);
	list_for_each_entry(h, &heap_list, list) {
		if (!strcmp(h->name, exp_info->name)) {
			mutex_unlock(&heap_list_lock);
			pr_err("eswin_heap: Already registered heap named %s\n",
			       exp_info->name);
			return ERR_PTR(-EINVAL);
		}
	}
	mutex_unlock(&heap_list_lock);

	heap = kzalloc(sizeof(*heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);

	heap->name = exp_info->name;
	heap->ops = exp_info->ops;
	heap->priv = exp_info->priv;

	/* Find unused minor number */
	ret = xa_alloc(&eswin_heap_minors, &minor, heap,
		       XA_LIMIT(0, NUM_HEAP_MINORS - 1), GFP_KERNEL);
	if (ret < 0) {
		pr_err("eswin_heap: Unable to get minor number for heap\n");
		err_ret = ERR_PTR(ret);
		goto err0;
	}

	/* Create device */
	heap->heap_devt = MKDEV(MAJOR(eswin_heap_devt), minor);

	cdev_init(&heap->heap_cdev, &eswin_heap_fops);
	ret = cdev_add(&heap->heap_cdev, heap->heap_devt, 1);
	if (ret < 0) {
		pr_err("eswin_heap: Unable to add char device\n");
		err_ret = ERR_PTR(ret);
		goto err1;
	}

	dev_ret = device_create(eswin_heap_class,
				NULL,
				heap->heap_devt,
				NULL,
				heap->name);
	if (IS_ERR(dev_ret)) {
		pr_err("eswin_heap: Unable to create device\n");
		err_ret = ERR_CAST(dev_ret);
		goto err2;
	}
	/* Add heap to the list */
	mutex_lock(&heap_list_lock);
	list_add(&heap->list, &heap_list);
	mutex_unlock(&heap_list_lock);

	return heap;

err2:
	cdev_del(&heap->heap_cdev);
err1:
	xa_erase(&eswin_heap_minors, minor);
err0:
	kfree(heap);
	return err_ret;
}

int eswin_heap_delete(struct eswin_heap *heap)
{
	struct eswin_heap *h, *tmp;
	int ret = -1;

	if (!heap->name || !strcmp(heap->name, "")) {
		pr_err("eswin_heap: Cannot delet heap without a name\n");
		return -EINVAL;
	}

	/* find the heaplist by the heap name */
	mutex_lock(&heap_list_lock);
	list_for_each_entry_safe(h, tmp, &heap_list, list) {
		if (!strcmp(h->name, heap->name)) {
			pr_info("eswin_heap: deleted heap %s\n",
			       heap->name);
			device_destroy(eswin_heap_class, h->heap_devt);
			cdev_del(&h->heap_cdev);
			xa_erase(&eswin_heap_minors, MINOR(h->heap_devt));
			list_del(&h->list);
			kfree(h);
			ret = 0;
		}
	}
	mutex_unlock(&heap_list_lock);

	if (ret) {
		pr_err("eswin_heap: heap named %s NOT found!\n", heap->name);
	}

	return ret;

}

int eswin_heap_delete_by_name(const char *name)
{
	struct eswin_heap *h, *tmp;
	int ret = -1;

	if (!name || !strcmp(name, "")) {
		pr_err("eswin_heap: Cannot delet heap without a name\n");
		return -EINVAL;
	}

	/* find the heaplist by the heap name */
	mutex_lock(&heap_list_lock);
	list_for_each_entry_safe(h, tmp, &heap_list, list) {
		if (!strcmp(h->name, name)) {
			pr_info("eswin_heap: deleted heap %s\n",
			       name);
			device_destroy(eswin_heap_class, h->heap_devt);
			cdev_del(&h->heap_cdev);
			xa_erase(&eswin_heap_minors, MINOR(h->heap_devt));
			list_del(&h->list);
			kfree(h);
			ret = 0;
		}
	}
	mutex_unlock(&heap_list_lock);

	if (ret) {
		pr_err("eswin_heap: heap named %s NOT found!\n", name);
	}

	return ret;
}

int eswin_heap_kalloc(char *name, size_t len, unsigned int fd_flags, unsigned int heap_flags)
{
	struct eswin_heap *heap = NULL;
#if 0
	struct eswin_heap *h = NULL;
	/* check the name is unique */
	mutex_lock(&heap_list_lock);
	list_for_each_entry(h, &heap_list, list) {
		if (!strcmp(h->name, name)) {
			heap = h;
			break;
		}
	}
	mutex_unlock(&heap_list_lock);
#else
	char *dev_path = NULL;
	struct file *file;
	int ret;

	dev_path = kasprintf(GFP_KERNEL, "/dev/dma_heap/%s", name);
	file = filp_open(dev_path, O_RDWR, 0);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		pr_err("failed to open file %s: (%d)\n",
			dev_path, ret);
		return ret;
	}
	heap = file->private_data;
#endif
	if (!heap) {
		printk("ERROR: Can't find this heap %s\n", name);
		return -ENODEV;
	}

	return eswin_heap_buffer_alloc(heap, len, fd_flags, heap_flags);
}
EXPORT_SYMBOL(eswin_heap_kalloc);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
static char *eswin_heap_devnode(const struct device *dev, umode_t *mode)
#else
static char *eswin_heap_devnode(struct device *dev, umode_t *mode)
#endif

{
	// return kasprintf(GFP_KERNEL, "eswin_heap/%s", dev_name(dev));
	/* create device node under dma_heap instead of eswin_heap, so that memory lib can
	   avoid the diverseness.
	*/
	return kasprintf(GFP_KERNEL, "dma_heap/%s", dev_name(dev));
}

int eswin_heap_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&eswin_heap_devt, 0, NUM_HEAP_MINORS, DEVNAME);
	if (ret)
		return ret;
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
	eswin_heap_class = class_create(DEVNAME);
	#else
	eswin_heap_class = class_create(THIS_MODULE, DEVNAME);
	#endif
	if (IS_ERR(eswin_heap_class)) {
		unregister_chrdev_region(eswin_heap_devt, NUM_HEAP_MINORS);
		return PTR_ERR(eswin_heap_class);
	}
	eswin_heap_class->devnode = eswin_heap_devnode;

	return 0;
}

void eswin_heap_uninit(void)
{
	class_destroy(eswin_heap_class);
}
