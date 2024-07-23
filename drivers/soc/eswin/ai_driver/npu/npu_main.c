// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/elf.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/eswin-mailbox.h>
#include <linux/delay.h>
#include <linux/bitfield.h>
#include <linux/pm_runtime.h>
#include <nvdla_interface.h>
#include <nvdla_linux.h>
#include "hetero_ioctl.h"
#include <opendla.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include <dt-bindings/interconnect/eswin,win2030.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <linux/win2030_noc.h>
#include <linux/iommu.h>
#include <linux/es_iommu_rsv.h>
#include "dla_log.h"
#include "dla_engine.h"
#include "dla_engine_internal.h"
#include "dla_interface.h"
#include "npu_spram.h"
#include "dla_driver.h"
#include "npu_top_csr.h"
#include "dla_log.h"
#include "edma.h"
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include "debug.h"
#include "internal_interface.h"
#include "nvdla_lowlevel.h"
#include "npu_base_regs.h"
#include "conv_regs.h"
#include "hetero_host.h"
#include "hetero_ipc.h"
#include "nvdla_linux.h"
#include "dla_buffer.h"
#include "mailbox_regs.h"
#include "nvdla_proc.h"

MODULE_IMPORT_NS(DMA_BUF);
#define DRIVER_NAME "eswin_npu"

int64_t dla_get_time_us(void)
{
	return 0;
}

int32_t dla_data_read(void *driver_context, void *task_data, void *handle,
		      uint16_t index, void *dst, uint32_t size, uint64_t offset)
{
	int32_t ret;
	void *ptr = NULL;
	struct dma_buf *buf;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	struct iosys_map map;
#else
	struct dma_buf_map map;
#endif
	int32_t fd;
	struct nvdla_task *task = (struct nvdla_task *)task_data;
	addrDesc_t *h;

	if (handle != NULL) {
		h = handle;
	} else {
		h = &task->addrlist->addrDesc[index];
	}

	fd = h->devBuf.memFd;

	buf = dma_buf_get(fd);
	if (IS_ERR(buf)) {
		pr_err("Failed get dma_buf for handle=%d\n", fd);
		return -EFAULT;
	}

	ret = dma_buf_begin_cpu_access(buf, DMA_BIDIRECTIONAL);
	if (ret) {
		dla_error("dma_buf_begin_cpu_access error\n");
		goto put_dma_buf;
	}
	ret = dma_buf_vmap(buf, &map);
	ptr = ret ? NULL : map.vaddr;
	if (!ptr) {
		pr_err("Failed to vmap dma_buf for fd=%d\n", fd);
		ret = -ENOMEM;
		goto end_cpu_access;
	}

	if ((buf->size < offset) || (buf->size - offset < size)) {
		dla_error(
			"error:dma buf wrong!fd:%d offset=%lld dma_buf size:%ld size:%d\n",
			fd, offset, buf->size, size);
		ret = -EFAULT;
	} else {
		memcpy(dst, (void *)(((uint8_t *)ptr) + offset), size);
	}
	dma_buf_vunmap(buf, &map);

end_cpu_access:
	dma_buf_end_cpu_access(buf, DMA_BIDIRECTIONAL);
put_dma_buf:
	dma_buf_put(buf);
	return ret;
}

int32_t dla_data_get_fd(void *driver_context, void *task_data, void *handle,
			uint16_t index)
{
	int32_t fd;
	struct nvdla_task *task = (struct nvdla_task *)task_data;
	addrDesc_t *h;

	if (handle != NULL) {
		h = handle;
	} else {
		h = &task->addrlist->addrDesc[index];
	}

	fd = h->devBuf.memFd;

	return fd;
}

int32_t dla_data_get_vaddr(void *task_data, uint16_t index, void **vaddr)
{
	struct nvdla_task *task = (struct nvdla_task *)task_data;

	if (index >= task->addrlist->numAddress) {
		pr_err("index(%d) is large than task->addrlist_desc->numAddress(%d)\n",
		       index, task->addrlist->numAddress);
		return -EFAULT;
	}
	if (task->bobjs[index].vaddr == NULL) {
		pr_err("bobj of index(%d) not vmap!\n", index);
		return -EFAULT;
	}

	*vaddr = (uint8_t *)task->bobjs[index].vaddr +
		 task->addrlist->addrDesc[index].devBuf.offset;
	dla_detail("index:%d offset:0x%llx fd:%lld virtAddr:%px\n", index,
		   task->addrlist->addrDesc[index].devBuf.offset,
		   task->addrlist->addrDesc[index].devBuf.memFd,
		   task->addrlist->addrDesc[index].virtAddr);

	return 0;
}

int32_t dla_get_dma_address(void *driver_context, void *task_data,
			    int16_t index, void *dst_ptr, u32 *is_io_tensor)
{
	int32_t ret = 0;
	addrDesc_t *address;
	dma_addr_t *phys_addr = (dma_addr_t *)dst_ptr;
	struct nvdla_task *task = (struct nvdla_task *)task_data;
	if (index == -1 || index > task->addrlist->numAddress) {
		dla_error("dma address index is invalid, %d\n", index);
		return -EINVAL;
	}

	address = (addrDesc_t *)task->addrlist->addrDesc;

	if (address[index].memoryType != 0) {
		dla_error(
			"memory handle type is error:index=%d,memory_type=%d\n",
			index, address[index].memoryType);
		return -1;
	}

	if (io_tensor_record(task->executor, &address[index], is_io_tensor) >
	    0) {
		dla_info(
			"io tensor detected index=%d,memory_type=%d flag=%d bind_id=%d\n",
			index, address[index].memoryType, address[index].flag,
			address[index].bindId);
		*phys_addr = -1ull;
		return 1;
	}

	*phys_addr = task->bobjs[index].dma_addr;
	*phys_addr = *phys_addr + address[index].devBuf.offset;

	return ret;
}

int32_t dla_get_sram_address(void *driver_context, void *task_data,
			     int16_t index, uint64_t *dst_ptr,
			     u32 *is_io_tensor)
{
	int32_t ret = 0;
	addrDesc_t *address;
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)driver_context;
	struct nvdla_task *task = (struct nvdla_task *)task_data;

	if (index == -1 || index > task->addrlist->numAddress) {
		dla_error("dma address index is invalid, %d\n", index);
		return -EINVAL;
	}

	address = (addrDesc_t *)task->addrlist->addrDesc;
	if (address[index].memoryType != 1) {
		dla_error(
			"memory handle type is error:index=%d,memory_type=%d\n",
			index, address[index].memoryType);
		return -1;
	}

	if (io_tensor_record(task->executor, &address[index], is_io_tensor) >
	    0) {
		dla_debug(
			"io tensor detected index=%d,memory_type=%d flag=%d bind_id=%d\n",
			index, address[index].memoryType, address[index].flag,
			address[index].bindId);
		*dst_ptr = -1ull;
		return 1;
	}
	*dst_ptr = nvdla_dev->spram_base_addr + address[index].devBuf.offset;

	return ret;
}

static const struct of_device_id edla_of_match[] = {
	{
		.compatible = "eswin,npu0",
	},
	{},
};

void *npu_get_win_engine(void *arg_nvdla_dev)
{
	struct nvdla_device *nvdla_dev = arg_nvdla_dev;

	return nvdla_dev->win_engine;
}

irqreturn_t npu_mbox_irq(int irq, void *dev_id)
{
	struct nvdla_device *nvdla_dev = (struct nvdla_device *)dev_id;
	msg_payload_t payload;
	u32 tiktok;
	u16 op_index;
	u32 stat;
	u32 data1;

	while (true) {
		*(u32 *)&payload = readl(nvdla_dev->mbox_rx_base +
					 MBOX_NPU_RD_DATA0_OFFSET);
		data1 = readl(nvdla_dev->mbox_rx_base +
			      MBOX_NPU_RD_DATA1_OFFSET);
		if (!data1) {
			break;
		}
		tiktok = payload.param & 0x1;
		op_index = payload.lparam;
		// notify data is retrieved by bit clear of data[63].
		writel(0x0, nvdla_dev->mbox_rx_base + MBOX_NPU_RD_DATA1_OFFSET);

		switch (payload.type) {
		case FRAME_DONE:
			stat = payload.param >> 1 & 0x1;
			mbx_irq_frame_done(nvdla_dev->win_engine, tiktok, stat);
			break;
		case NOTIFY_OP_DONE:
			mbx_irq_op_done(nvdla_dev->win_engine, tiktok,
					op_index);
			break;
		case NOTIFY_EVENT_SINK_DONE:
			mbx_irq_event_sink_done(nvdla_dev->win_engine, tiktok,
						op_index);
			break;
		default:
			dla_error("invalid payload.type= %hhu\n", payload.type);
			ASSERT(false);
		}
	}
	return IRQ_HANDLED;
}

static struct nvdla_device *static_nvdla_dev[2];

struct nvdla_device *get_nvdla_dev(int i)
{
	if (i < 0 || i > 2) {
		return NULL;
	}
	return static_nvdla_dev[i];
}

static int32_t edla_probe(struct platform_device *pdev)
{
	int32_t err = 0;
	struct resource *res;
	struct nvdla_device *nvdla_dev;
	struct device *dev = &pdev->dev;
	uint32_t version;

	dla_debug("%s enter.\n", __FUNCTION__);
#if SMALL_PEC_MAT
	dla_debug("load npu driver with PEC2x2.\n");
#else
	dla_debug("load npu driver with PEC4x8.\n");
#endif

	pr_err("Probe Eswin NPU.\n");

	nvdla_dev = devm_kzalloc(dev, sizeof(*nvdla_dev), GFP_KERNEL);
	if (!nvdla_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, nvdla_dev);
	nvdla_dev->pdev = pdev;
	spin_lock_init(&nvdla_dev->nvdla_lock);
	mutex_init(&nvdla_dev->task_mutex);
	init_waitqueue_head(&nvdla_dev->event_wq);
	err = npu_dt_node_resources(nvdla_dev);
	if (err) {
		dla_error("error, get hw resource, ret=%d\n", err);
		platform_set_drvdata(pdev, NULL);
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no io resource\n");
		err = PTR_ERR(res);
		goto err_mem0;
	}

	//npu configuration space, start from 0x51c00000
	nvdla_dev->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(nvdla_dev->base)) {
		err = PTR_ERR(nvdla_dev->base);
		goto err_mem0;
	}
	if (request_mem_region(E31_EMISSION_DTIM_BASE, E31_EMISSION_DTIM_SIZE,
			       "EMISSION_BASE") == NULL) {
		dev_err(&pdev->dev, "request_mem_region error\n");
		err = -EBUSY;
		goto err_mem0;
	}
	nvdla_dev->emission_base = devm_ioremap(
		&pdev->dev, E31_EMISSION_DTIM_BASE, E31_EMISSION_DTIM_SIZE);
	if (!nvdla_dev->emission_base) {
		dev_err(&pdev->dev, "ioremap error\n");
		err = -ENOMEM;
		goto err_iomap_emission;
	}

	if (request_mem_region(E31_PROGRAM_DTIM_BASE, E31_PROGRAM_DTIM_SIZE,
			       "PROGRAM_BASE") == NULL) {
		dev_err(&pdev->dev, "request_mem_region error\n");
		err = -EBUSY;
		goto err_iomap_emission;
	}
	nvdla_dev->program_base = devm_ioremap(
		&pdev->dev, E31_PROGRAM_DTIM_BASE, E31_PROGRAM_DTIM_SIZE);
	if (!nvdla_dev->program_base) {
		dev_err(&pdev->dev, "ioremap error\n");
		err = -ENOMEM;
		goto err_iomap_program;
	}

	nvdla_dev->uart_mutex_base = devm_ioremap(
		&pdev->dev, UART_MUTEX_BASE_ADDR, UART_MUTEX_ADDR_SIZE);
	if (!nvdla_dev->uart_mutex_base) {
		dev_err(&pdev->dev, "ioremap error\n");
		err = -ENOMEM;
		goto err_iomap_program;
	}

	err = npu_enable_clock(nvdla_dev);
	if (err < 0) {
		dev_err(&pdev->dev, "%s, %d, npu enable clock err, ret = %d.\n",
			__func__, __LINE__, err);
		goto err_iomap_program;
	}

	pm_runtime_set_autosuspend_delay(dev, 5000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_get_noresume(dev);

	err = npu_init_reset(nvdla_dev);
	if (err)
		goto err_init_reset;

	err = npu_init_mbox(nvdla_dev);
	if (err) {
		dev_err(&pdev->dev, "npu init mailbox error, ret = %d.\n", err);
		goto err_init_mbox;
	}
	npu_tbu_power(dev, true);

	switch (nvdla_dev->numa_id) {
	case 0:
		nvdla_dev->e31_mmio_base = devm_ioremap(dev, NPU_CFG_BASE_ADDR,
							NPU_CFG_ADDR_RANGE);
		break;
	case 1:
		nvdla_dev->e31_mmio_base =
			devm_ioremap(dev, NPU_CFG_BASE_ADDR + 0x20000000,
				     NPU_CFG_ADDR_RANGE);
		break;
	default:
		dla_error(
			"parameter numaid=%d is not correct, please use 0 or 1.\n",
			nvdla_dev->numa_id);
		goto err_iomap_e31;
	}
	if (!nvdla_dev->e31_mmio_base) {
		dla_error("Eswin e31 ioremap fail.\n");
		goto err_iomap_e31;
	}

	err = npu_e31_load_fw(pdev, nvdla_dev->e31_mmio_base);
	if (err) {
		dev_err(&pdev->dev, "load e31 fw error.\n");
		goto err_load_firm;
	}

	err = npu_spram_get(nvdla_dev);
	if (err) {
		dla_error("error get  spram.\n");
		goto err_spram;
	}
	version = dla_reg_read(nvdla_dev, 0x150000);
	dla_info("edla version: 0x%x\n", version);

	/* config streamID of NPU_DMA */
	npu_dma_sid_cfg(nvdla_dev->base, WIN2030_SID_NPU_DMA);

	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(41));
	if (err)
		dev_warn(dev, "Unable to set coherent mask\n");

	npu_hw_init(nvdla_dev);
	err = edma_init(nvdla_dev);
	if (err) {
		dev_warn(dev, "edma_init fail\n");
		goto err_edma_init;
	}
	err = npu_create_sysfs(pdev);
	if (err) {
		dev_err(&pdev->dev, "unable to create sysfs files\n");
	}

	err = win_engine_init(nvdla_dev, &nvdla_dev->win_engine);
	if (err) {
		dev_err(&pdev->dev, "failed to init win_engine\n");
		goto err_engine_init;
	}

	dev_info(&pdev->dev, "win_engine 0x%px\n", nvdla_dev->win_engine);

	nvdla_dev->pause_op_list = vmalloc(MAX_OP_NUM * sizeof(u16));
	static_nvdla_dev[nvdla_dev->numa_id] = nvdla_dev;

	err = create_npu_dev(0, nvdla_dev);
	if (err) {
		dev_err(&pdev->dev, "failed to register npu device\n");
		goto err_create_dev;
	}
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return err;
err_create_dev:
	vfree(nvdla_dev->pause_op_list);
	win_engine_destroy(nvdla_dev);
err_engine_init:
	edma_free(nvdla_dev);
err_edma_init:
	npu_spram_release(nvdla_dev);
err_spram:
err_load_firm:
err_iomap_e31:
	npu_tbu_power(dev, false);
err_init_mbox:
err_init_reset:
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(dev);
	pm_runtime_dont_use_autosuspend(dev);
	npu_disable_clock(nvdla_dev);
err_iomap_program:
	release_mem_region(E31_PROGRAM_DTIM_BASE, E31_PROGRAM_DTIM_SIZE);
err_iomap_emission:
	release_mem_region(E31_EMISSION_DTIM_BASE, E31_EMISSION_DTIM_SIZE);
err_mem0:
	npu_put_dt_resources(nvdla_dev);
	return err;
}

static int32_t __exit edla_remove(struct platform_device *pdev)
{
	struct nvdla_device *nvdla_dev = dev_get_drvdata(&pdev->dev);
	int ret;

	if (nvdla_dev == NULL) {
		return 0;
	}

	ret = npu_hardware_reset(nvdla_dev);
	if (ret) {
		dla_error("hardware reset error, ret=%d.\n", ret);
		return -EIO;
	}

	destory_npu_dev(0);
	npu_uninit_mbox(nvdla_dev);
	npu_dev_reset(nvdla_dev);
	pm_runtime_disable(&nvdla_dev->pdev->dev);
	pm_runtime_set_suspended(&nvdla_dev->pdev->dev);
	pm_runtime_dont_use_autosuspend(&nvdla_dev->pdev->dev);

	/* reset the uart1 mutex lock */
	reset_uart_mutex(nvdla_dev);

	if (nvdla_dev->mbx_chan) {
		mbox_free_channel(nvdla_dev->mbx_chan);
	}
	win_engine_destroy(nvdla_dev);
	edma_free(nvdla_dev);
	release_mem_region(E31_EMISSION_DTIM_BASE, E31_EMISSION_DTIM_SIZE);
	release_mem_region(E31_PROGRAM_DTIM_BASE, E31_PROGRAM_DTIM_SIZE);
	npu_spram_release(nvdla_dev);

	npu_tbu_power(&pdev->dev, false);
	ret = npu_disable_clock(nvdla_dev);
	npu_put_dt_resources(nvdla_dev);
	npu_remove_sysfs(pdev);
	if (nvdla_dev->pause_op_list) {
		vfree(nvdla_dev->pause_op_list);
		nvdla_dev->pause_op_list = NULL;
	}
	return 0;
}

int npu_runtime_suspend(struct device *dev)
{
	struct nvdla_device *ndev = dev_get_drvdata(dev);
	int ret;

	if (!ndev) {
		dla_error("%s, %d, ndev is null.\n", __func__, __LINE__);
		return -EIO;
	}
	ret = npu_disable_clock(ndev);
	dla_debug("%s, %d, ret=%d.\n", __func__, __LINE__, ret);
	return ret;
}

int npu_runtime_resume(struct device *dev)
{
	struct nvdla_device *ndev = dev_get_drvdata(dev);
	int ret;

	if (!ndev) {
		dla_error("%s, %d, ndev is null.\n", __func__, __LINE__);
		return -EIO;
	}
	ret = npu_enable_clock(ndev);
	dla_debug("%s, %d, ret=%d.\n", __func__, __LINE__, ret);
	return ret;
}

int npu_suspend(struct device *dev)
{
	int ret;
	struct nvdla_device *nvdla_dev = dev_get_drvdata(dev);
	struct win_engine *engine = (struct win_engine *)nvdla_dev->win_engine;

	dla_debug("%s, %d, into..\n", __func__, __LINE__);
	ret = npu_pm_get(nvdla_dev);
	if (ret < 0) {
		dla_error("%s, %d, npu pm get err.\n", __func__, __LINE__);
		return ret;
	}
	memset(engine->host_node, 0, sizeof(host_node_t));
	memset(nvdla_dev->emission_base, 0, E31_EMISSION_DTIM_SIZE);
	memset(nvdla_dev->program_base, 0, E31_PROGRAM_DTIM_SIZE);

	engine->tiktok = 0;

	npu_uninit_mbox(nvdla_dev);
	npu_dev_reset(nvdla_dev);
	npu_uninit_ipc(nvdla_dev);
	reset_uart_mutex(nvdla_dev);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_noidle(dev);

	npu_tbu_power(dev, false);
	npu_disable_clock(nvdla_dev);
	return 0;
}

int npu_resume(struct device *dev)
{
	int ret;
	struct nvdla_device *ndev = dev_get_drvdata(dev);

	ret = npu_enable_clock(ndev);
	if (ret < 0) {
		return ret;
	}
	ret = npu_hardware_reset(ndev);
	if (ret) {
		dla_error("hardware reset error, ret=%d.\n", ret);
		return -EIO;
	}

	pm_runtime_get_noresume(dev);

	ret = npu_init_reset(ndev);
	if (ret < 0) {
		goto err_reset;
	}
	ret = npu_init_mbox(ndev);
	if (ret) {
		dev_err(dev, "npu init mailbox error, ret = %d.\n", ret);
		goto err_reset;
	}
	npu_tbu_power(dev, true);
	/* config streamID of NPU_DMA */

	ret = npu_e31_load_fw(ndev->pdev, ndev->e31_mmio_base);
	if (ret) {
		dev_err(dev, "load e31 fw error.\n");
		goto err_load_firm;
	}
	npu_dma_sid_cfg(ndev->base, WIN2030_SID_NPU_DMA);
	npu_hw_init(ndev);
	ret = npu_init_ipc(ndev);
	if (ret) {
		dev_err(dev, "npu init ipc error.\n");
		goto err_ipc;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
	return 0;

err_ipc:
	npu_init_reset(ndev);
err_load_firm:
	npu_tbu_power(dev, false);
err_reset:
	npu_disable_clock(ndev);
	return ret;
}

static const struct dev_pm_ops npu_hw_pm_ops = { SYSTEM_SLEEP_PM_OPS(
	npu_suspend, npu_resume) SET_RUNTIME_PM_OPS(npu_runtime_suspend,
						    npu_runtime_resume, NULL) };

static struct platform_driver edla_driver =
{
	.probe = edla_probe,
	.remove = edla_remove,
	.driver =
	{
		.name = DRIVER_NAME,
		.of_match_table = of_match_ptr(edla_of_match),
		.pm = &npu_hw_pm_ops,
	},
};

static int __init npu_modules_init(void)
{
	int err;

	err = platform_driver_register(&edla_driver);
	if (err < 0) {
		dla_error("NPU:platform_register_drivers failed!err=%d\n", err);
		return err;
	}
	err = npu_platform_init();
	if (err) {
		dla_error("npu platform init err, err=%d.\n", err);
		platform_driver_unregister(&edla_driver);
		return err;
	}
	npu_create_procfs();
	return 0;
}
module_init(npu_modules_init);

static void __exit npu_modules_exit(void)
{
	npu_remove_procfs();

	npu_platform_uninit();

	platform_driver_unregister(&edla_driver);

	dla_loop_buf_exit();
}
module_exit(npu_modules_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("EDLA");
MODULE_DESCRIPTION("Eswin Deep Learning Accelerator driver");
