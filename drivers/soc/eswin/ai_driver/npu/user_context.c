// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
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
#include <nvdla_linux.h>
#include <opendla.h>
#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/poll.h>
#include "hetero_ioctl.h"
#include "npu_spram.h"
#include "dla_driver.h"
#include "nvdla_lowlevel.h"
#include "dla_engine.h"
#include "dla_log.h"
#include "debug.h"
#include "internal_interface.h"
#include "dla_buffer.h"
#include "hetero_arch.h"
#include "hetero_host.h"
extern void handle_perf_switch(struct nvdla_device *ndev, bool enable);
extern int get_perf_data(struct nvdla_device *ndev);

static int setup_model_task(struct user_model *model)
{
	int ret = -1;
	struct dla_network_desc *network;
	struct dla_task *task;

	network = model->network;
	task = &model->task;
	ret = dla_data_get_vaddr(&model->mem_handles,
				 network->dependency_graph_index,
				 (void **)&task->common_desc);
	if (ret < 0) {
		dla_error("err:get network vaddr failed!\n");
		return ret;
	}

	ret = dla_data_get_vaddr(&model->mem_handles,
				 network->operation_desc_index,
				 (void **)&task->op_desc);
	if (ret < 0) {
		dla_error("err:get op_desc vaddr failed!\n");
		return ret;
	}

	ret = dla_data_get_vaddr(&model->mem_handles,
				 network->surface_desc_index,
				 (void **)&task->surface_desc);
	if (ret < 0) {
		dla_error("err: surface_desc read failed\n");
		return ret;
	}
	return 0;
}

static void npu_put_dmabuf_from_model(struct user_model *model)
{
	dla_dmabuf_vunmap_for_model(model);
	dla_detach_dmabuf_from_model(model);
}

static void npu_put_model_bobj(struct user_model *model)
{
	dla_release_bobj(model->model_bobj);
}

static void npu_model_release(struct khandle *handle)
{
	struct user_model *model =
		container_of(handle, struct user_model, handle);
	struct user_context *uctx;
	uctx = model->uctx;

	executor_clearup(model->executor);
	npu_put_dmabuf_from_model(model);
	npu_put_model_bobj(model);
	kfree(model);
	dla_debug("npu_release_model ok.\n");
}

static int npu_get_model_bobj(int shm_fd, struct user_model *model)
{
	struct nvdla_device *nvdla_dev = model->nvdla_dev;
	int ret;

	dla_debug("shm_fd=%d\n", shm_fd);
	model->model_bobj =
		dla_import_fd_to_device(shm_fd, &nvdla_dev->pdev->dev);
	if (IS_ERR(model->model_bobj)) {
		dla_error("err:import shm fd failed!\n");
		return -EFAULT;
	}

	model->model_shm = dla_dmabuf_vmap(model->model_bobj);
	if (model->model_shm == NULL) {
		dla_error("err:model_bobj vamp failed!\n");
		ret = -EFAULT;
		goto err_release_shm;
	}
	model->mem_handles.addrlist = &model->model_shm->addrList;
	return 0;

err_release_shm:
	dla_release_bobj(model->model_bobj);
	return ret;
}

static int npu_get_dmabuf_from_model(struct user_model *model)
{
	int ret;
	ret = dla_import_dmabuf_from_model(model);
	if (ret < 0) {
		dla_error("err:import dma buf from model failed!\n");
		return ret;
	}

	ret = dla_dmabuf_vmap_for_model(model);
	if (ret < 0) {
		dla_error("err:vmap dma buf for model failed!\n");
		goto err_dmabuf_bobj;
	}
	return 0;
err_dmabuf_bobj:
	dla_detach_dmabuf_from_model(model);
	return ret;
}

static int npu_check_model_version(struct user_model *model)
{
	if (model->network->version.major_version !=
		    NPU_INTERFACE_MAJOR_VERSION ||
	    model->network->version.minor_version !=
		    NPU_INTERFACE_MINOR_VERSION ||
	    model->network->version.subminor_version !=
		    NPU_INTERFACE_SUBMINOR_VERSION) {
		dla_error(
			"error:model's version(%d.%d.%d) not equal npu interface version(%d.%d.%d)\n",
			model->network->version.major_version,
			model->network->version.minor_version,
			model->network->version.subminor_version,
			NPU_INTERFACE_MAJOR_VERSION,
			NPU_INTERFACE_MINOR_VERSION,
			NPU_INTERFACE_SUBMINOR_VERSION);
		return -EFAULT;
	}
	return 0;
}

static int submit_model(struct nvdla_device *nvdla_dev,
			struct user_context *uctx, void *arg)
{
	int ret;
	struct user_model *model;
	struct win_ioctl_args *win_arg = arg;
	int shm_fd = win_arg->shm_fd;
	int idx;

	model = kzalloc(sizeof(struct user_model), GFP_KERNEL);
	if (model == NULL) {
		dla_error("%s %d nomem\n", __func__, __LINE__);
		return -ENOMEM;
	}
	model->uctx = uctx;
	model->nvdla_dev = nvdla_dev;
	model->engine = npu_get_win_engine(nvdla_dev);

	dla_detail("model=0x%px model_size=%ld\n", model,
		   sizeof(struct user_model));

	ret = npu_get_model_bobj(shm_fd, model);
	if (ret < 0) {
		goto err_model_bobj;
	}

	ret = npu_get_dmabuf_from_model(model);
	if (ret < 0) {
		goto err_dmabuf_model;
	}

	ret = dla_data_get_vaddr(&model->mem_handles,
				 model->model_shm->kmdNetworkAddrId,
				 (void **)&model->network);

	if (ret < 0) {
		dla_error("err:get network vaddr failed!\n");
		goto err_netowrk;
	}

	ret = npu_check_model_version(model);
	if (ret < 0) {
		goto err_netowrk;
	}

	ret = setup_model_task(model);
	if (ret < 0) {
		dla_error("Failed setup model task\n");
		goto err_netowrk;
	}

	ret = create_executor(&model->task, model->network, &model->executor,
			      &model->mem_handles, model->engine, nvdla_dev,
			      model);
	if (ret < 0) {
		dla_error("Failed to extract executor\n");
		goto err_netowrk;
	}
	prepare_e31_frame_info(model->executor, model);

	ret = init_kernel_handle(&model->handle, npu_model_release,
				 NPU_MODEL_KHANDLE_MAGIC, &uctx->handle);
	if (ret < 0) {
		dla_error("init kernel handle for model ,error.\n");
		goto err_init_handle;
	}
	idx = model->handle.fd;
	dla_debug("%s, %d, model id=%d, done.\n", __func__, __LINE__, idx);
	kernel_handle_decref(&model->handle);
	return idx;

err_init_handle:
	executor_clearup(model->executor);
err_netowrk:
	npu_put_dmabuf_from_model(model);
err_dmabuf_model:
	npu_put_model_bobj(model);
err_model_bobj:
	kfree(model);
	return ret;
}

static struct user_model *npu_get_model_by_id(struct user_context *uctx, int id)
{
	struct khandle *m_handle;
	struct user_model *model;

	spin_lock(&uctx->model_lock);
	dla_debug("%s, %d, id=%d.\n", __func__, __LINE__, id);
	m_handle =
		find_kernel_handle(&uctx->handle, id, NPU_MODEL_KHANDLE_MAGIC);
	if (m_handle == NULL) {
		spin_unlock(&uctx->model_lock);
		dla_error("%s, %d, invalid model id = 0x%x.\n", __func__,
			  __LINE__, id);
		return NULL;
	}
	model = container_of(m_handle, struct user_model, handle);
	spin_unlock(&uctx->model_lock);
	return model;
}
static void npu_put_model(struct user_model *model)
{
	kernel_handle_decref(&model->handle);
}
static int release_model(struct user_context *uctx, void *arg)
{
	struct win_ioctl_args *win_arg = arg;
	struct user_model *model;
	uint64_t idx = win_arg->model_idx;

	model = npu_get_model_by_id(uctx, idx);
	if (model == NULL) {
		dla_error("cannot get model.\n");
		return -EINVAL;
	}

	kernel_handle_release_family(&model->handle);
	npu_put_model(model);
	dla_debug("%s, %d, done.\n", __func__, __LINE__);
	return 0;
}

static DECLARE_WAIT_QUEUE_HEAD(npu_waitq);

int get_event_idx(struct win_executor *executor, int op_index)
{
	if (op_index >= executor->network->num_operations) {
		dla_error("error:bad op_index(%d).\n", op_index);
		return -EFAULT;
	}
	return executor->task->op_desc[op_index].event_op.index;
}

static int save_event_to_cache(struct user_context *uctx, int16_t event_idx)
{
	if (uctx->event_desc.len == MAX_EVENT_SINK_SAVE_NUM) {
		dla_error("err:no memory for new event.\n");
		return -1;
	}

	uctx->event_desc.event_sinks[(uctx->event_desc.produce_idx++) %
				     MAX_EVENT_SINK_SAVE_NUM] = event_idx;
	uctx->event_desc.len++;
	dla_detail("save event cache. after++ len=%d\n", uctx->event_desc.len);

	return 0;
}

static int get_event_from_cache(struct user_context *uctx)
{
	int16_t event_idx;

	dla_detail("get cache len=%d\n", uctx->event_desc.len);
	if (uctx->event_desc.len == 0) {
		return -1;
	}

	event_idx =
		uctx->event_desc.event_sinks[(uctx->event_desc.consumer_idx++) %
					     MAX_EVENT_SINK_SAVE_NUM];
	uctx->event_desc.len--;

	return event_idx;
}

void handle_event_sink_from_e31(struct win_engine *engine, u32 tiktok,
				u16 op_index)
{
	struct host_frame_desc *f;
	struct user_model *model;
	struct win_executor *executor;
	int16_t event_idx = 0;
	unsigned long flags;

	if (engine == NULL) {
		dla_error("err:engine is NULL.\n");
		return;
	}

	f = engine->tiktok_frame[tiktok];
	if (f == NULL) {
		dla_debug("err:frame is null.\n");
		return;
	}

	model = f->model;
	executor = engine->cur[tiktok];
	if (executor == NULL) {
		dla_error("err:executor is NULL.\n");
		return;
	}
	event_idx = get_event_idx(executor, op_index);
	if (event_idx < 0) {
		return;
	}
	if (!executor) {
		dla_error("%s, %d, executor is null.\n", __func__, __LINE__);
		return;
	}
	dla_detail("op_index:%d event_idx:%d.\n", op_index, event_idx);

	spin_lock_irqsave(&model->uctx->event_desc.spinlock, flags);

	if (save_event_to_cache(model->uctx, event_idx)) {
		dla_error("err:save event failed.\n");
		spin_unlock_irqrestore(&model->uctx->event_desc.spinlock,
				       flags);
		return;
	}
	spin_unlock_irqrestore(&model->uctx->event_desc.spinlock, flags);

	//wakeup poll
	wake_up_interruptible(&npu_waitq);
}

static int get_event_sink_val(struct user_context *uctx,
			      struct win_ioctl_args *win_arg)
{
	unsigned long flags;
	union event_union event;
	int i;

	event.event_data = -1ULL;
	spin_lock_irqsave(&uctx->event_desc.spinlock, flags);
	for (i = 0; i < sizeof(union event_union) / sizeof(u16); i++) {
		event.event_sinks[i] = get_event_from_cache(uctx);
		if (event.event_sinks[i] < 0) {
			break;
		}
	}
	spin_unlock_irqrestore(&uctx->event_desc.spinlock, flags);

	if (event.event_data == -1ULL) {
		dla_error("err:bad event val.\n");
		return -EFAULT;
	}

	dla_detail("get event sink:0x%llx\n", event.event_data);

	if (copy_to_user((void __user *)(win_arg->data), &event.event_data,
			 sizeof(u64))) {
		dla_error("err:bad user data address.\n");
		return -EFAULT;
	}

	return 0;
}

static void send_event_source_req_to_hw(struct win_engine *engine, u8 tiktok,
					u16 op_index)
{
	msg_payload_t payload;
	payload.type = DEC_OP_REF;
	payload.param = tiktok;
	payload.param |= IDX_EVENT_SOURCE << 4;
	payload.lparam = op_index;
	send_mbx_msg_to_e31(engine, payload);
}

#define NPU_SET_BIT(bitmap, pos) (bitmap[pos / 8] |= (1 << (pos % 8)))
int send_event_source_to_e31(struct user_context *ctx, int model_id,
			     u16 event_val)
{
	struct user_model *model;
	struct win_engine *engine;
	struct win_executor *executor;
	u16 op_index = 0;
	u32 tiktok;
	struct host_frame_desc *frame;

	model = npu_get_model_by_id(ctx, model_id);
	if (model == NULL) {
		dla_error("model id = %d, is error.\n", model_id);
		return -EINVAL;
	}

	engine = model->engine;
	executor = model->executor;
	tiktok = (engine->tiktok + 1) % NUM_TIKTOK;  // get current tiktok

	if (event_val >= executor->total_event_source_num) {
		dla_error("err:bad event_val:%d.\n", event_val);
		return -EFAULT;
	}

	op_index = executor->event_source_map[event_val];
	if (op_index == INVALID_OP_IDX) {
		dla_error("err:bad event_val:%d.\n", event_val);
		return -EFAULT;
	}

	frame = engine->tiktok_frame[tiktok];
	NPU_SET_BIT(frame->is_event_source_done, event_val);
	dla_detail("%s, %d, event_id=%hu.\n", __func__, __LINE__, event_val);

#if (NPU_DEV_SIM == NPU_REAL_ENV)
	send_event_source_req_to_hw(engine, tiktok, op_index);
	// send_event_source_req(tiktok, op_index,
	// 		(void *)((struct nvdla_device *)engine->nvdla_dev)
	// 				->e31_mmio_base,
	// 			engine->host_node);
#elif (NPU_DEV_SIM == NPU_MCU_HOST)
	hetero_send_event_source_req(tiktok, op_index);
#else
	dla_error("%s, %d, Wrong NPU_DEV_SIM = %d.\n", __func__, __LINE__,
		  NPU_DEV_SIM);
	npu_put_model(model);
	return -1;
#endif

	npu_put_model(model);
	return 0;
}

static int set_dump_info(struct win_executor *executor, void *arg)
{
	struct win_ioctl_args *win_arg = arg;
	kmd_dump_info_t dump_info;

	if (win_arg->dump_enable) {
		if (copy_from_user(&dump_info,
				   (void __user *)win_arg->dump_info,
				   sizeof(kmd_dump_info_t))) {
			dla_error("%s %d bad user data address\n", __func__,
				  __LINE__);
			return -EFAULT;
		}

		if (copy_from_user(executor->dump_info.op_idx_list,
				   (void __user *)dump_info.op_idx_list,
				   sizeof(u16) * dump_info.list_size)) {
			dla_error("%s %d bad user data address\n", __func__,
				  __LINE__);
			return -EFAULT;
		}
		executor->dump_info.process_id = dump_info.process_id;
		executor->dump_info.model_id = dump_info.model_id;
		executor->dump_info.is_dump_enable = kmd_dump_enable;
		executor->dump_info.list_size = dump_info.list_size;
		memset(executor->dump_info.path, 0, DUMP_PATH_LEN);
		memcpy(executor->dump_info.path, dump_info.path, DUMP_PATH_LEN);
		set_pause_op_done(executor, &executor->dump_info);
	} else {
		if (executor->dump_info.is_dump_enable == kmd_dump_enable) {
			executor->dump_info.is_dump_enable = kmd_dump_disable;
			reset_pause_op_done(executor);
		}
	}

	return 0;
}
static int commit_new_io_tensor(struct user_context *uctx, void *arg)
{
	struct user_model *model;
	struct win_ioctl_args *win_arg = arg;
	u16 idx = win_arg->model_idx;
	struct host_frame_desc *f;
	struct win_executor *executor;
	int ret;
	bool result;
	int new_state;

	new_state = (NPU_RT_MUTX_LOCKED | NPU_RT_MUTX_FRAME_DONE);
	if (atomic_cmpxchg(&uctx->lock_status, NPU_RT_MUTX_LOCKED, new_state) !=
	    NPU_RT_MUTX_LOCKED) {
		dla_error("%s, %d, invalid status\n", __func__, __LINE__);
		return -EINVAL;
	}

	model = npu_get_model_by_id(uctx, idx);
	if (model == NULL) {
		dla_error("%s, %d, invalid model id = 0x%x.\n", __func__,
			  __LINE__, idx);
		ret = -EINVAL;
		goto err_model_id;
	}

	ret = create_new_frame(model->executor, &f, model);
	if (unlikely(ret != win_arg->tensor_size)) {
		dla_error(
			"%s %d model %d io_tensor_size %d != win_arg->tensor_size %d\n",
			__func__, __LINE__, idx, ret, win_arg->tensor_size);
		npu_put_model(model);
		goto err_model_id;
	}
	kernel_handle_release_family(&f->handle);
	if (copy_from_user(f->io_tensor_list, (void __user *)win_arg->data,
			   ret)) {
		dla_error("%s %d bad user data address\n", __func__, __LINE__);
		ret = -EFAULT;
		goto clean_out;
	}
	ret = io_tensor_to_io_addr(model->executor, f);
	if (unlikely(ret < 0)) {
		goto clean_out;
	}

	ret = set_dump_info(model->executor, arg);
	if (ret) {
		goto clean_out;
	}

	f->model = model;
	f->frame_idx = win_arg->frame_idx;
	f->executor = model->executor;
	f->state = frame_state_queued;
	init_waitqueue_head(&f->frame_done);

	executor = (struct win_executor *)f->executor;
	memcpy(f->io_tensor.tensor_addr, f->io_tensors_addr_list,
	       (executor->input_num + executor->output_num) * sizeof(u64));

	result = send_new_frame(model->engine, model->executor, f);
	if (!result) {
		dla_error("schedule new frame err.\n");
		ret = result;
		goto clean_out;
	}
	dla_debug("%s, %d, done.\n", __func__, __LINE__);
	return 0;

clean_out:
	kernel_handle_decref(&f->handle);
err_model_id:
	atomic_set(&uctx->lock_status, NPU_RT_MUTX_LOCKED);

	return ret;
}

static struct win_engine *get_engine_from_file(struct file *file);

static int runtime_lock_request(struct user_context *uctx, struct file *file,
				unsigned int cmd)
{
	unsigned long last_state;
	struct win_engine *engine;

	engine = get_engine_from_file(file);
	if (cmd == ES_NPU_IOCTL_MUTEX_TRYLOCK) {
		if (down_trylock(&engine->runtime_sem)) {
			return -EINTR;
		}
		BUG_ON(atomic_read(&uctx->lock_status) != NPU_RT_MUTX_IDLE);
		atomic_set(&uctx->lock_status, NPU_RT_MUTX_LOCKED);
		dla_debug("try %s, %d locked\n", __func__, __LINE__);

	} else if (cmd == ES_NPU_IOCTL_MUTEX_LOCK) {
		if (down_interruptible(&engine->runtime_sem)) {
			return -EINTR;
		}

		BUG_ON(atomic_read(&uctx->lock_status) != NPU_RT_MUTX_IDLE);
		atomic_set(&uctx->lock_status, NPU_RT_MUTX_LOCKED);
		dla_debug("%s, %d locked\n", __func__, __LINE__);
	} else {
		last_state = atomic_fetch_and(~NPU_RT_MUTX_LOCKED,
					      &uctx->lock_status);
		if (last_state == NPU_RT_MUTX_LOCKED) {
			up(&engine->runtime_sem);
			dla_debug("%s, %d unlocked\n", __func__, __LINE__);
		}
	}

	return 0;
}

static int get_sram_fd(struct nvdla_device *nvdla_dev,
		       struct win_ioctl_args *win_arg)
{
	int dmabuf_fd;
	struct dma_buf *sram_dmabuf;
	sram_info_t sram_info;

	sram_dmabuf = nvdla_dev->spram_bobj->dmabuf;
	get_dma_buf(sram_dmabuf);

	/* allocate a new dmabuf_fd linked to this dmabuf, the refcount of the dmabuf
	 * will NOT be increased by dma_buf_fd */
	dmabuf_fd = dma_buf_fd(sram_dmabuf, sram_dmabuf->file->f_flags);
	if (dmabuf_fd < 0) {
		dma_buf_put(sram_dmabuf);  // put the dmabuf back
		return dmabuf_fd;
	}

	dla_detail("sram_dmabuf fd:%d size:0x%x.\n", dmabuf_fd,
		   nvdla_dev->spram_bobj->size);
	sram_info.fd = dmabuf_fd;
	sram_info.size = nvdla_dev->spram_bobj->size;

	if (copy_to_user((void __user *)(win_arg->data), &sram_info,
			 sizeof(sram_info_t))) {
		dla_error("err:bad user data address.\n");
		return -EFAULT;
	}

	return 0;
}

static int handle_perf(struct nvdla_device *nvdla_dev,
		       struct win_ioctl_args *win_arg)
{
	bool enable;

	enable = win_arg->data ? 1 : 0;
	handle_perf_switch(nvdla_dev, enable);

	return 0;
}

static int send_perf_data_to_usr(struct nvdla_device *nvdla_dev,
				 struct win_ioctl_args *win_arg)
{
	struct win_engine *engine;
	int ret;

	engine = (struct win_engine *)nvdla_dev->win_engine;

	ret = get_perf_data(nvdla_dev);
	if (ret)
		goto fail;

	if (copy_to_user((void __user *)(win_arg->data), engine->perf_data_buf,
			 sizeof(npu_e31_perf_t) * MAX_OP_NUM)) {
		dla_error("err:bad user data address.\n");
		ret = -EFAULT;
		goto fail;
	}

fail:

	return ret;
}

#define NPU_DEV_NAME "npu"

struct npu_cdev_t {
	struct class *class;
	struct device *device;
	struct cdev dev;
	dev_t devid;
	char *name;
	int major;
	int minor;
	struct nvdla_device *nvdla_dev;
};
static struct npu_cdev_t npu_cdev[2];

struct npu_cdev_t *get_npu_dev_by_devid(int major, int minor)
{
	int i = 0;

	for (i = 0; i < 2; i++) {
		if ((npu_cdev[i].major == major) &&
		    (npu_cdev[i].minor == minor)) {
			return &npu_cdev[i];
		}
	}

	return NULL;
}

static void npu_dma_buf_release(struct khandle *h)
{
	struct npu_dma_buf_ex *entry =
		container_of(h, struct npu_dma_buf_ex, handle);
	dla_unmapdma_buf_to_dev(&entry->obj);
	kfree(entry);
}

int npu_prepare_dma_buf(struct nvdla_device *ndev, struct user_context *uctx,
			void *arg)
{
	int ret;
	ES_DEV_BUF_S buf;
	struct npu_dma_buf_ex *entry;
	struct khandle *khandle;

	if (copy_from_user(&buf, (void *)arg, sizeof(buf))) {
		dla_error("%s, %d, copy_from_user err.\n", __func__,
			  __LINE__) return -EINVAL;
	}
	mutex_lock(&uctx->dma_lock);
	entry = xa_load(&uctx->buf_xrray, (int)buf.memFd);
	if (entry) {
		khandle = find_kernel_handle(&uctx->handle, entry->handle.fd,
					     NPU_DMABUF_HANDLE_MAGIC);
		if (khandle) {
			dla_debug("find 0x%x entry, handle fd=%d.\n",
				  (int)entry->buf_info.memFd, entry->handle.fd);
			kernel_handle_addref(&entry->handle);
			mutex_unlock(&uctx->dma_lock);
			return 0;
		} else {
			xa_erase(&uctx->buf_xrray, (int)entry->buf_info.memFd);
		}
	}
	entry = kzalloc(sizeof(struct npu_dma_buf_ex), GFP_KERNEL);
	if (!entry) {
		dla_error("%s, %d, alloc npu dma buf ex err.\n", __func__,
			  __LINE__);
		mutex_unlock(&uctx->dma_lock);
		return -ENOMEM;
	}
	ret = init_kernel_handle(&entry->handle, npu_dma_buf_release,
				 NPU_DMABUF_HANDLE_MAGIC, &uctx->handle);
	if (ret) {
		dla_error(
			"%s, %d, init kernel handle for npu dma buf ex err.\n",
			__func__, __LINE__);
		kfree(entry);
		mutex_unlock(&uctx->dma_lock);
		return ret;
	}
	xa_store(&uctx->buf_xrray, (int)buf.memFd, entry, GFP_KERNEL);
	dla_debug("%s, %d, entry handle_fd=%d.\n", __func__, __LINE__,
		  entry->handle.fd);
	ret = dla_mapdma_buf_to_dev((int)buf.memFd, &entry->obj,
				    &ndev->pdev->dev);
	if (ret) {
		ret = -ENODEV;
		goto err;
	}
	entry->buf_info = buf;
	kernel_handle_decref(&entry->handle);
	mutex_unlock(&uctx->dma_lock);
	dla_debug("%s, %d, entry fd=0x%x.\n", __func__, __LINE__,
		  (int)entry->buf_info.memFd);
	return 0;
err:
	mutex_unlock(&uctx->dma_lock);
	kernel_handle_release_family(&entry->handle);
	kernel_handle_decref(&entry->handle);
	return ret;
}

int npu_unprepare_dma_buf(struct user_context *uctx, void *arg)
{
	u32 fd;
	struct khandle *khandle;
	struct npu_dma_buf_ex *entry;

	if (copy_from_user(&fd, arg, sizeof(u32))) {
		dla_error("%s, %d, copy_from_user err.\n", __func__, __LINE__);
		return -EINVAL;
	}
	mutex_lock(&uctx->dma_lock);
	entry = xa_load(&uctx->buf_xrray, fd);
	if (!entry) {
		dla_error("%s, %d, ,cannot find %d dma buf entry.\n", __func__,
			  __LINE__, fd);
		mutex_unlock(&uctx->dma_lock);
		return -EINVAL;
	}
	xa_erase(&uctx->buf_xrray, fd);
	mutex_unlock(&uctx->dma_lock);

	khandle = find_kernel_handle(&uctx->handle, entry->handle.fd,
				     NPU_DMABUF_HANDLE_MAGIC);
	if (!khandle) {
		dla_error("%s, %d, cannot find dmabuf=%d handle.\n", __func__,
			  __LINE__, (int)entry->buf_info.memFd);
		return 0;
	}
	kernel_handle_release_family(khandle);
	kernel_handle_decref(khandle);
	return 0;
}

static long npu_dev_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	int ret = 0;
	int major = 0;
	int minor = 0;
	struct npu_cdev_t *npu_cdev;
	struct user_context *uctx;
	struct win_ioctl_args win_arg;
	uint64_t data = 0;

	major = imajor(file->f_inode);
	minor = iminor(file->f_inode);

	npu_cdev = get_npu_dev_by_devid(major, minor);
	if (npu_cdev == NULL) {
		dla_error("%s %d nodev\n", __func__, __LINE__);
		return -EFAULT;
	}
	uctx = file->private_data;
	if (uctx == NULL) {
		dla_error("error:uctx is NULL!\n");
		return -EFAULT;
	}

	if (copy_from_user(&win_arg, (void *)arg,
			   sizeof(struct win_ioctl_args))) {
		dla_error("bad user data address\n");
		return -EFAULT;
	}

	switch (cmd) {
	case ES_NPU_IOCTL_MODEL_LOAD:
		ret = submit_model(npu_cdev->nvdla_dev, uctx, &win_arg);
		data = ret;
		if (copy_to_user((void __user *)(win_arg.pret), &data, 8)) {
			dla_error("bad user data address\n");
			return -EFAULT;
		}

		break;
	case ES_NPU_IOCTL_MODEL_UNLOAD:
		ret = release_model(uctx, &win_arg);
		break;
	case ES_NPU_IOCTL_SET_EVENT:
		dla_debug("model_idx=%d event_source_val=%d\n",
			  win_arg.model_idx, win_arg.event_source_val);
		ret = send_event_source_to_e31(uctx, win_arg.model_idx,
					       win_arg.event_source_val);
		break;
	case ES_NPU_IOCTL_GET_EVENT:
		ret = get_event_sink_val(uctx, &win_arg);
		break;
	case ES_NPU_IOCTL_TASK_SUBMIT:
		ret = commit_new_io_tensor(uctx, &win_arg);
		break;
	case ES_NPU_IOCTL_MUTEX_LOCK:
	case ES_NPU_IOCTL_MUTEX_UNLOCK:
	case ES_NPU_IOCTL_MUTEX_TRYLOCK:
		ret = runtime_lock_request(uctx, file, cmd);
		break;
	case ES_NPU_IOCTL_HETERO_CMD:
		ret = npu_hetero_cmd(npu_cdev->nvdla_dev, &win_arg);
		break;
	case ES_NPU_IOCTL_GET_SRAM_FD:
		ret = get_sram_fd(npu_cdev->nvdla_dev, &win_arg);
		break;
	case ES_NPU_IOCTL_HANDLE_PERF:
		ret = handle_perf(npu_cdev->nvdla_dev, &win_arg);
		break;
	case ES_NPU_IOCTL_GET_PERF_DATA:
		ret = send_perf_data_to_usr(npu_cdev->nvdla_dev, &win_arg);
		break;
	case ES_NPU_IOCTL_PREPARE_DMA_BUF:
		ret = npu_prepare_dma_buf(npu_cdev->nvdla_dev, uctx,
					  (void *)arg);
		break;
	case ES_NPU_IOCTL_UNPREPARE_DMA_BUF:
		ret = npu_unprepare_dma_buf(uctx, (void *)arg);
		break;
	default:
		dla_error("err:ioctl cmd err!cmd=0x%x\n", cmd);
		ret = -EFAULT;
		break;
	}
	if (ret < 0) {
		dla_error("cmd 0x%x fail, ret:%d\n", cmd, ret);
		return ret;
	}

	return 0;
}

static void npu_uctx_release(struct khandle *h)
{
	struct user_context *uctx =
		container_of(h, struct user_context, handle);
	struct nvdla_device *ndev;

	if (uctx == NULL) {
		return;
	}
	ndev = uctx->ndev;
	dla_debug("npu_uctx_release ok. pid=%d, uctx=0x%px.\n", current->pid,
		  uctx);
	kfree(uctx);
	module_put(THIS_MODULE);
	npu_pm_put(ndev);
}

int npu_dev_open(struct inode *inode, struct file *file)
{
	struct user_context *uctx;
	struct npu_cdev_t *npu_cdev;
	struct nvdla_device *ndev;
	struct win_engine *engine;
	int major = 0;
	int minor = 0;
	int ret;
	unsigned long flags;

	dla_debug("%s, %d, current pid=%d.\n\n", __func__, __LINE__,
		  current->pid);

	major = imajor(file->f_inode);
	minor = iminor(file->f_inode);

	npu_cdev = get_npu_dev_by_devid(major, minor);
	if (npu_cdev == NULL) {
		dla_error("cannot find npu device. \n");
		return -ENODEV;
	}

	ndev = npu_cdev->nvdla_dev;
	engine = ndev->win_engine;
	ret = npu_pm_get(ndev);
	if (ret < 0) {
		dla_error("%s, %d, npu_pm_get failed, ret = %d.\n", __func__,
			  __LINE__, ret);
		return ret;
	}
	spin_lock_irqsave(&engine->executor_lock, flags);
	if (engine->engine_is_alive == false) {
		dla_error("npu engine is not ok, please restart.\n");
		npu_pm_put(ndev);
		spin_unlock_irqrestore(&engine->executor_lock, flags);
		return -ENODEV;
	}
	spin_unlock_irqrestore(&engine->executor_lock, flags);

	if (!try_module_get(THIS_MODULE)) {
		dla_error("%s, %d, cannot get module.\n", __func__, __LINE__);
		npu_pm_put(ndev);
		return -ENODEV;
	}

	uctx = kzalloc(sizeof(struct user_context), GFP_KERNEL);
	if (uctx == NULL) {
		module_put(THIS_MODULE);
		npu_pm_put(ndev);
		dla_error("%s %d nomem\n", __func__, __LINE__);
		return -ENOMEM;
	}
	spin_lock_init(&uctx->model_lock);
	ret = init_kernel_handle(&uctx->handle, npu_uctx_release,
				 NPU_UCTX_KHANDLE_MAGIC, NULL);
	if (ret != 0) {
		dla_error("init kernel handle for user context error.\n");
		module_put(THIS_MODULE);
		npu_pm_put(ndev);
		kfree(uctx);
		return ret;
	}
	atomic_set(&uctx->lock_status, NPU_RT_MUTX_IDLE);
	spin_lock_init(&uctx->event_desc.spinlock);
	mutex_init(&uctx->dma_lock);
	xa_init(&uctx->buf_xrray);
	uctx->uctx_is_alive = true;
	file->private_data = uctx;
	uctx->ndev = ndev;
	kernel_handle_decref(&uctx->handle);
	dla_debug("npu_dev_open success!\n");
	return 0;
}

static struct win_engine *get_engine_from_file(struct file *file)
{
	int major = 0;
	int minor = 0;
	struct npu_cdev_t *npu_cdev;
	struct win_engine *engine;

	major = imajor(file->f_inode);
	minor = iminor(file->f_inode);

	npu_cdev = get_npu_dev_by_devid(major, minor);
	if (npu_cdev == NULL) {
		dla_error("%s %d nodev\n", __func__, __LINE__);
		return NULL;
	}

	engine = npu_get_win_engine(npu_cdev->nvdla_dev);
	return engine;
}

#define NPU_CHECK_BIT(bitmap, pos) ((bitmap[pos / 8]) & (1 << (pos % 8)))
int npu_dev_release(struct inode *inode, struct file *file)
{
	struct user_context *uctx = file->private_data;
	struct win_engine *engine;
	struct host_frame_desc *frame;
	struct user_model *model;
	struct win_executor *executor;
	unsigned long last_state;
	int model_id;
	u32 tiktok = 0;
	u16 eid = 0;
	engine = get_engine_from_file(file);
	uctx->uctx_is_alive = false;

	for (tiktok = 0; tiktok < NUM_TIKTOK; tiktok++) {
		frame = engine->tiktok_frame[tiktok];
		if (frame) {
			model = frame->model;
			model_id = model->handle.fd;
			if (model && model_id != -1 && uctx == model->uctx) {
				executor = model->executor;
				for (eid = 0;
				     eid < executor->total_event_source_num;
				     eid++) {
					if (!NPU_CHECK_BIT(
						    frame->is_event_source_done,
						    eid)) {
						dla_debug(
							"%s %d addr of is_event_source_done=0x%px, src_done_val=0x%x\n",
							__func__, __LINE__,
							frame->is_event_source_done,
							*(u32 *)frame
								 ->is_event_source_done);
						send_event_source_to_e31(
							uctx, model_id, eid);
						dla_info("%s %d: eid=%hu\n",
							 __FUNCTION__, __LINE__,
							 eid);
					}
				}
			}
		}
	}

	last_state = atomic_fetch_and(~NPU_RT_MUTX_LOCKED, &uctx->lock_status);
	if (last_state == NPU_RT_MUTX_LOCKED) {
		up(&engine->runtime_sem);
		dla_debug("%s, %d unlocked\n", __func__, __LINE__);
	}

	kernel_handle_release_family(&uctx->handle);

	file->private_data = NULL;
	dla_debug("%s, %d, Done, pid=%d.\n", __func__, __LINE__, current->pid);
	return 0;
}

static ssize_t npu_dev_write(struct file *file, const char __user *buf,
			     size_t size, loff_t *ppos)
{
	return 0;
}
static ssize_t npu_dev_read(struct file *file, char __user *buf, size_t size,
			    loff_t *ppos)
{
	return 0;
}

#ifdef CONFIG_COMPAT
static long npu_dev_ioctl_compat(struct file *flip, unsigned int cmd,
				 unsigned long arg)
{
	return npu_dev_ioctl(flip, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static __poll_t npu_dev_poll(struct file *file, poll_table *wait)
{
	__poll_t mask = 0;
	struct user_context *uctx = file->private_data;
	poll_wait(file, &npu_waitq, wait);

	if (uctx->event_desc.len > 0) {
		dla_detail("event sinks len:%d\n", uctx->event_desc.len);
		mask = EPOLLIN | EPOLLRDNORM;
	}

	return mask;
}

static const struct file_operations npu_cdev_fops = {
	.owner = THIS_MODULE,
	.open = npu_dev_open,
	.release = npu_dev_release,
	.read = npu_dev_read,
	.write = npu_dev_write,
	.unlocked_ioctl = npu_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = npu_dev_ioctl_compat,
#endif
	.poll = npu_dev_poll,
};

int create_npu_dev(int node_id, struct nvdla_device *nvdla_dev)
{
	int ret = 0;
	char *name;

	npu_cdev[node_id].name = kzalloc(16, GFP_KERNEL);
	if (!npu_cdev[node_id].name) {
		dla_error("alloc memory for %d node err.\n", node_id);
		return -ENOMEM;
	}
	name = npu_cdev[node_id].name;
	snprintf(name, 16, "%s%d", NPU_DEV_NAME, node_id);

	if (npu_cdev[node_id].major) {
		npu_cdev[node_id].devid = MKDEV(npu_cdev[node_id].major, 0);
		ret = register_chrdev_region(npu_cdev[node_id].devid, 1, name);
	} else {
		ret = alloc_chrdev_region(&npu_cdev[node_id].devid, 0, 1, name);
		npu_cdev[node_id].major = MAJOR(npu_cdev[node_id].devid);
		npu_cdev[node_id].minor = MINOR(npu_cdev[node_id].devid);

		dla_debug("major=%d, minor=%d!\n", npu_cdev[node_id].major,
			  npu_cdev[node_id].minor);
	}

	if (ret < 0) {
		dla_error("alloc_chrdev_region failed for npu%d\n", node_id);
		kfree(name);
		npu_cdev[node_id].name = NULL;
		return ret;
	}

	npu_cdev[node_id].dev.owner = THIS_MODULE;
	cdev_init(&npu_cdev[node_id].dev, &npu_cdev_fops);
	cdev_add(&npu_cdev[node_id].dev, npu_cdev[node_id].devid, 1);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	npu_cdev[node_id].class = class_create(name);
#else
	npu_cdev[node_id].class = class_create(THIS_MODULE, name);
#endif
	if (IS_ERR(npu_cdev[node_id].class)) {
		dla_error("class_create failed for npu%d\n", node_id);
		goto class_err;
	}

	npu_cdev[node_id].device = device_create(npu_cdev[node_id].class, NULL,
						 npu_cdev[node_id].devid, NULL,
						 name);
	if (IS_ERR(npu_cdev[node_id].device)) {
		dla_error("device_create failed for npu%d\n", node_id);
		goto device_err;
	}

	npu_cdev[node_id].nvdla_dev = nvdla_dev;

	dla_debug("dev %s create ok!\n", name);

	return 0;
device_err:
	class_destroy(npu_cdev[node_id].class);
class_err:
	unregister_chrdev_region(npu_cdev[node_id].devid, 1);
	kfree(name);
	npu_cdev[node_id].name = NULL;

	return -1;
}

void destory_npu_dev(int node_id)
{
	unregister_chrdev_region(npu_cdev[node_id].devid, 1);
	cdev_del(&npu_cdev[node_id].dev);
	device_destroy(npu_cdev[node_id].class, npu_cdev[node_id].devid);
	class_destroy(npu_cdev[node_id].class);
	if (npu_cdev[node_id].name) {
		kfree(npu_cdev[node_id].name);
		npu_cdev[node_id].name = NULL;
	}
	dla_debug("destory_npu_dev!\n");
}
