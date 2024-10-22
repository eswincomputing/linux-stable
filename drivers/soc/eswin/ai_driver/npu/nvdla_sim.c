// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe root complex driver
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
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

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
#include <drm/drm_prime.h>
#include <linux/mailbox_client.h>
#include <linux/mailbox/eswin-mailbox.h>
#include <linux/delay.h>
#include <linux/bitfield.h>
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
#include <linux/dma-map-ops.h>
#include <linux/cma.h>

#include "dla_log.h"
#include "dla_engine.h"
#include "dla_engine_internal.h"
#include "dla_interface.h"
#include "npu_spram.h"
#include "dla_driver.h"
#include "npu_top_csr.h"
#include "edma.h"
#include "debug.h"
#include "hetero_common.h"
#include "internal_interface.h"

#define MAX_NPU_CORE 1
static frame_info_t g_frame_info[NUM_TIKTOK];
struct win_executor *g_executor[NUM_TIKTOK];
struct npu_op_done_info {
	msg_payload_t payload;
	bool op_resume_flag;
};

extern void handle_event_sink_from_e31(struct win_engine *engine, u32 tiktok,
				       u16 op_index);

static struct npu_op_done_info op_done_info[NUM_OP_TYPE];

static u16 event_source_idx[NUM_TIKTOK] = { invalid_op_index,
					    invalid_op_index };

struct npu_hw_info {
	int numa_id;
};

int npu_disable_clock(struct nvdla_device *ndev)
{
	return 0;
}

int npu_enable_clock(struct nvdla_device *ndev)
{
	return 0;
}

void npu_dma_sid_cfg(void __iomem *npu_subsys_base, u32 sid)
{
}
int npu_init_mbox(struct nvdla_device *ndev)
{
	return 0;
}
int npu_uninit_mbox(struct nvdla_device *ndev)
{
	return 0;
}

int npu_create_sysfs(struct platform_device *pdev)
{
	return 0;
}

int npu_remove_sysfs(struct platform_device *pdev)
{
	return 0;
}
int npu_dt_node_resources(struct nvdla_device *ndev)
{
	return 0;
}

void npu_tbu_power(struct device *dev, bool flag)
{
}

void npu_hw_init(struct nvdla_device *ndev)
{
}

int npu_hardware_reset(struct nvdla_device *ndev)
{
	return 0;
}

int npu_dev_reset(struct nvdla_device *ndev)
{
	return 0;
}

int npu_e31_load_fw(struct platform_device *pdev, void __iomem *e31_mmio_base)
{
	return 0;
}

int npu_init_reset_and_clk(struct nvdla_device *ndev)
{
	return 0;
}

int npu_spram_get(struct nvdla_device *nvdla_dev)
{
	struct dla_buffer_object *spram_bobj = NULL;
	int err = 0;

	//TODO(zhangyizhong): jira 13174: on simulator, SRAM size should be got from DTS.
	nvdla_dev->spram_size = 4 * 1024 * 1024;
	dla_info("set spram_size to %u KB for simulation.\n",
		 nvdla_dev->spram_size / 1024);

	spram_bobj = dla_alloc_dmabuf(nvdla_dev->spram_size, ES_MEM_ALLOC_RSV);
	if (spram_bobj < 0) {
		dla_error(
			"spram_dma_fd dev_mem_alloc failed!,spram_size=0x%x\n",
			nvdla_dev->spram_size);
		return -1;
	}

	err = dla_attach_dmabuf(spram_bobj, &nvdla_dev->pdev->dev);
	if (err) {
		dla_error("dla_attach_dmabuf failed!,err=%d\n", err);
		dla_release_bobj(spram_bobj);
		return err;
	}

	nvdla_dev->spram_base_addr = sg_phys(spram_bobj->attach->sgt->sgl);
	dla_debug("spram phy_addr=0x%llx\n", nvdla_dev->spram_base_addr);
	dla_debug("nvdla_dev->spram_base_addr=0x%llx\n",
		  nvdla_dev->spram_base_addr);

	nvdla_dev->spram_bobj = spram_bobj;

	return 0;
}

int npu_spram_release(struct nvdla_device *nvdla_dev)
{
	if (nvdla_dev->spram_bobj) {
		dla_release_bobj(nvdla_dev->spram_bobj);
	}
	return 0;
}

int npu_cmd_check(struct cma *cma, void *data)
{
	struct npu_cma_info *tmp;

	if (data == NULL) {
		dla_error("data is null.\n");
		return 0;
	}
	tmp = (struct npu_cma_info *)data;

	if (cma != NULL) {
		if (strcmp(cma_get_name(cma), "reserved") != 0) {
			return 0;
		}

		tmp->base = cma_get_base(cma);
		tmp->size = (uint32_t)cma_get_size(cma);
		return 1;
	}

	return 0;
}

static int npu_get_cma_info(void *arg)
{
	struct npu_cma_info info;
	int ret;

	ret = cma_for_each_area(npu_cmd_check, &info);
	if (!ret) {
		dla_error("cannot find cma info.\n");
		return -ENODEV;
	}

	if (copy_to_user((void __user *)arg, &info, sizeof(info))) {
		dla_error("copy to user err.\n");
		return -EFAULT;
	}
	return 0;
}

void *npu_alloc_dma_addr(struct win_executor *executor, size_t size,
			 dma_addr_t *dma_handle, int i, gfp_t gfp)
{
	struct nvdla_device *nvdla_dev =
		(struct nvdla_device *)(executor->engine->nvdla_dev);
	struct device *dev = &nvdla_dev->pdev->dev;
	void *cpu_addr = NULL;
	int ret = 0;

	executor->tmp_for_simu[i] = dla_alloc_dmabuf(size, ES_MEM_ALLOC_RSV);
	if (executor->tmp_for_simu[i] == NULL) {
		printk("%s,%d, i=%d.\n", __func__, __LINE__, i);
		return NULL;
	}

	ret = dla_attach_dmabuf(executor->tmp_for_simu[i], dev);
	if (ret) {
		printk("%s,%d, i=%d.\n", __func__, __LINE__, i);
		goto err;
	}

	cpu_addr = dla_dmabuf_vmap(executor->tmp_for_simu[i]);
	if (cpu_addr == NULL) {
		printk("%s,%d, i=%d.\n", __func__, __LINE__, i);
		goto err;
	}
	*dma_handle = executor->tmp_for_simu[i]->dma_addr;

	return cpu_addr;
err:
	return NULL;
}

void npu_free_dma_addr(struct win_executor *executor, int i)
{
	if (executor->tmp_for_simu[i] != NULL) {
		dla_release_bobj(executor->tmp_for_simu[i]);
	}
}

void hetero_send_frame_to_npu(u8 tiktok, struct host_frame_desc *f)
{
	struct user_model *model = (struct user_model *)f->model;
	hetero_ipc_frame_t *frame_desc =
		(hetero_ipc_frame_t *)&model->e31_frame_info;
	npu_io_tensor_t *io_tensor = &f->io_tensor;

	if (g_frame_info[tiktok].is_ready) {
		dla_error("%s, tiktok:%d is ready and not processed\n",
			  __func__, tiktok);
		return;
	}

	g_frame_info[tiktok].tiktok = tiktok;
	memcpy(&g_frame_info[tiktok].frame, frame_desc,
	       sizeof(hetero_ipc_frame_t));
	memcpy(&g_frame_info[tiktok].io_tensor, io_tensor,
	       sizeof(npu_io_tensor_t));
	g_frame_info[tiktok].is_ready = true;
	g_executor[tiktok] = f->executor;
}

static long npu_ioctl_query_frame_status(void *arg)
{
	int idx;

	for (idx = 0; idx < NUM_TIKTOK; idx++) {
		if (g_frame_info[idx].is_ready) {
			break;
		}
	}

	if (idx == NUM_TIKTOK) {
		if (copy_to_user((void __user *)arg, &g_frame_info[0],
				 sizeof(frame_info_t))) {
			dla_error("copy to user err.\n");
			return -EFAULT;
		}
	} else {
		if (copy_to_user((void __user *)arg, &g_frame_info[idx],
				 sizeof(frame_info_t))) {
			dla_error("copy to user err.\n");
			return -EFAULT;
		}
		dla_debug("send frame to e31:idx:%d\n", idx);
		g_frame_info[idx].is_ready = false;
	}

	return 0;
}

static long npu_ioctl_send_frame_done(void *arg)
{
	u32 tiktok;
	bool stat;
	host_frame_done_t frame_done;
	model_stat_t *stat_data;

	if (copy_from_user(&frame_done, arg, sizeof(host_frame_done_t))) {
		printk("copy from user err.\n");
		return -EFAULT;
	}
	tiktok = frame_done.param & 0x1;
	stat = frame_done.param >> 1 & 0x1;
	if (stat) {
		stat_data = &(g_executor[tiktok]
				      ->engine->host_node->model_stat[tiktok]);
		if (copy_from_user(stat_data, (void *)frame_done.stat_addr,
				   sizeof(model_stat_t))) {
			printk("copy from user stat info err.\n");
			return -EFAULT;
		}
	}

	mbx_irq_frame_done(g_executor[tiktok]->engine, tiktok, stat);

	return 0;
}

static long npu_ioctl_send_op_done(void *arg)
{
	int i;
	struct host_frame_desc *f;
	struct npu_op_done_info *info = NULL;
	msg_payload_t payload;
	if (copy_from_user(&payload, (u32 __user *)arg,
			   sizeof(msg_payload_t))) {
		dla_error("copy from user err.\n");
		return -EFAULT;
	}

	// dump output data
	f = g_executor[payload.param]->engine->tiktok_frame[payload.param];
	dla_dump_src_data(g_executor[payload.param], f, payload.lparam);
	dla_dump_dst_data(g_executor[payload.param], f, payload.lparam);

	// update op_done info list
	for (i = 0; i < NUM_OP_TYPE; i++) {
		if (!op_done_info[i].op_resume_flag) {
			info = &op_done_info[i];
			break;
		}
	}

	if (info != NULL) {
		info->payload = payload;
		info->op_resume_flag = true;
	} else {
		dla_error("%s(), send op_done failed\n", __func__);
		return -EFAULT;
	}

	return 0;
}

static long npu_ioctl_event_sink_done(struct nvdla_device *nvdla_dev, void *arg)
{
	// int i;
	// struct npu_op_done_info *info = NULL;
	msg_payload_t payload;
	u16 op_index;
	u32 tiktok;

	if (copy_from_user(&payload, (u32 __user *)arg,
			   sizeof(msg_payload_t))) {
		dla_error("copy from user err.\n");
		return -EFAULT;
	}
#if 0
	// update op_done info list
	for (i = 0; i < NUM_OP_TYPE; i++) {
		if (!op_done_info[i].op_resume_flag) {
			info = &op_done_info[i];
			break;
		}
	}

	if (info != NULL) {
		info->payload = payload;
		info->op_resume_flag = true;
	} else {
		dla_error("%s(), send op_done failed\n", __func__);
		return -EFAULT;
	}
#endif
	op_index = payload.lparam;
	tiktok = payload.param;
	dla_detail("get event sink tiktok:%d, op_index:%d\n", tiktok, op_index);

	handle_event_sink_from_e31(npu_get_win_engine(nvdla_dev), tiktok,
				   op_index);

	return 0;
}

static long npu_ioctl_query_op_resume(void *arg)
{
	int i;
	struct npu_op_done_info *info = NULL;

	for (i = 0; i < NUM_OP_TYPE; i++) {
		if (op_done_info[i].op_resume_flag) {
			info = &op_done_info[i];
			break;
		}
	}

	if (info != NULL) {
		resume_info_t resume_info;
		resume_info.op_index = info->payload.lparam;
		resume_info.tiktok = info->payload.param;
		resume_info.resume_flag = 1;
		if (copy_to_user((u32 __user *)arg, &resume_info,
				 sizeof(msg_payload_t))) {
			dla_error("%s,%d, copy from user err.\n", __func__,
				  __LINE__);
			return -EFAULT;
		}

		info->op_resume_flag = false;
	}

	return 0;
}

void hetero_send_event_source_req(u8 tiktok, u16 op_index)
{
	dla_info("%s(%d) tiktok=%d op_index=%u\n", __func__, __LINE__, tiktok,
		 op_index);
	event_source_idx[tiktok] = op_index;
}

static long npu_ioctl_query_event_source(void *arg)
{
	event_op_info_t event_info;
	int i;
	for (i = 0; i < NUM_TIKTOK; i++) {
		if (event_source_idx[i] != invalid_op_index) {
			event_info.tiktok = i;
			event_info.op_index = event_source_idx[i];
			event_source_idx[i] = invalid_op_index;
			if (copy_to_user((u32 __user *)arg, &event_info,
					 sizeof(msg_payload_t))) {
				dla_error("%s,%d, copy from user err.\n",
					  __func__, __LINE__);
				return -EFAULT;
			}
		}
	}

	return 0;
}

int npu_hetero_cmd(struct nvdla_device *nvdla_dev, struct win_ioctl_args *args)
{
	int ret;

	switch (args->hetero_cmd) {
	case NPU_HETERO_GET_CMA_INFO:
		ret = npu_get_cma_info((void *)args->data);
		break;
	case NPU_HETERO_QUERY_FRAME_READY:
		ret = npu_ioctl_query_frame_status((void *)args->data);
		break;
	case NPU_HETERO_QUERY_EVENT_SOURCE:
		ret = npu_ioctl_query_event_source((void *)args->data);
		break;
	case NPU_HETERO_SEND_FRAME_DONE:
		ret = npu_ioctl_send_frame_done((void *)args->data);
		break;
	case NPU_HETERO_SEND_OP_DONE:
		ret = npu_ioctl_send_op_done((void *)args->data);
		break;
	case NPU_HETERO_QUERY_OP_RESUME:
		ret = npu_ioctl_query_op_resume((void *)args->data);
		break;
	case NPU_HETERO_SEND_EVENT_SINK_DONE:
		ret = npu_ioctl_event_sink_done(nvdla_dev, (void *)args->data);
		break;
	default:
		dla_error("error hetero_cmd: 0x%x.\n", args->hetero_cmd);
		return -EINVAL;
	}
	return ret;
}

static struct resource npu_res[]= {
	[0] = {
		.flags = IORESOURCE_MEM,
		.name = "npu_base",
		.start = 0x51c00000,
		.end = 0x51c00000 + 0x400000 - 1,
	},
	[1] = {
		.flags = IORESOURCE_IRQ,
		.start = 256,
		.name = "npu_irq",
	},
};

static struct platform_device *npu_plat_dev[MAX_NPU_CORE];
int npu_platform_init(void)
{
	int i;
	int ret;
	struct npu_hw_info info;

	for (i = 0; i < MAX_NPU_CORE; i++) {
		npu_plat_dev[i] = platform_device_alloc("eswin_npu", i);
		if (!npu_plat_dev[i]) {
			i--;
			while (i >= 0) {
				platform_device_put(npu_plat_dev[i--]);
			}
			ret = -EIO;
			goto err_dev_alloc;
		}
		npu_plat_dev[i]->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	}

	for (i = 0; i < MAX_NPU_CORE; i++) {
		info.numa_id = i;
		ret = platform_device_add_resources(npu_plat_dev[i], npu_res,
						    ARRAY_SIZE(npu_res));
		if (ret) {
			goto release_pdev;
		}
		ret = platform_device_add_data(npu_plat_dev[i], (void *)&info,
					       sizeof(info));
		if (ret) {
			goto err_add_pdata;
		}
	}

	for (i = 0; i < MAX_NPU_CORE; i++) {
		ret = platform_device_add(npu_plat_dev[i]);
		if (ret < 0) {
			i--;
			while (i >= 0) {
				platform_device_del(npu_plat_dev[i--]);
			}
			goto err_add_dev;
		}
	}

	dla_debug("%s, plat init done.\n", __func__);
	return 0;
err_add_dev:
err_add_pdata:
release_pdev:
	for (i = 0; i < MAX_NPU_CORE; i++) {
		platform_device_put(npu_plat_dev[i]);
	}
err_dev_alloc:
	return ret;
}

int npu_platform_uninit(void)
{
	int i;

	for (i = 0; i < MAX_NPU_CORE; i++) {
		if (npu_plat_dev[i]) {
			platform_device_unregister(npu_plat_dev[i]);
			npu_plat_dev[i] = NULL;
		}
	}
	dla_debug("%s, ok\n", __func__);
	return 0;
}
