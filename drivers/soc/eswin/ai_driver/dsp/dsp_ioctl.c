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

#include <linux/fs.h>
#include <linux/compat.h>
#include <linux/pm_runtime.h>
#include <linux/dma-direct.h>
#include <linux/slab.h>
#include "eswin-khandle.h"
#include "dsp_dma_buf.h"
#include <linux/list.h>
#include <linux/poll.h>
#include "dsp_platform.h"
#include "dsp_main.h"
#include "dsp_ioctl.h"
#include <linux/fdtable.h>

MODULE_IMPORT_NS(DMA_BUF);

static void dsp_user_release_fn(struct khandle *handle)
{
	struct dsp_user *user = container_of(handle, struct dsp_user, h);
	struct dsp_op_desc *op;
	struct es_dsp *dsp;
	struct dsp_file *dsp_file;

	dsp_debug("%s, %d.\n", __func__, __LINE__);
	if (!user) {
		dsp_err("%s, user is null, error.\n", __func__);
		return;
	}
	op = user->op;
	dsp = op->dsp;
	dsp_file = user->dsp_file;

	kfree(user);

	mutex_lock(&dsp->op_list_mutex);
	kref_put(&op->refcount, dsp_op_release);
	mutex_unlock(&dsp->op_list_mutex);

	dsp_debug("%s,%d, user release done.\n", __func__, __LINE__);
	return;
}

static struct dsp_user *dsp_op_add_user(struct dsp_file *dsp_file,
					struct dsp_op_desc *op)
{
	struct dsp_user *user;
	int ret;
	struct es_dsp *dsp = op->dsp;

	user = kzalloc(sizeof(struct dsp_user), GFP_KERNEL);
	if (!user) {
		dsp_err("alloc dsp user mem failed.\n");
		return NULL;
	}

	ret = init_kernel_handle(&user->h, dsp_user_release_fn,
				 DSP_USER_HANDLE_MAGIC, &dsp_file->h);
	if (ret) {
		dsp_err("init user khandle error.\n");
		kfree(user);
		return NULL;
	}
	dsp_debug("%s, op refcount=%d.\n", __func__, kref_read(&op->refcount));
	dsp_debug("%s, user handle addr=0x%px.\n", __func__, &user->h);
	user->op = op;
	user->dsp_file = dsp_file;
	return user;
}

static long dsp_ioctl_load_op(struct file *flip, dsp_ioctl_load_s __user *arg)
{
	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;
	int ret;
	dsp_ioctl_load_s dsp_load;
	u64 handle;
	struct dsp_op_desc *op;
	struct dsp_user *user;
	char op_name[OPERATOR_NAME_MAXLEN];
	char *op_dir = NULL;

	if (copy_from_user(&dsp_load, arg, sizeof(dsp_ioctl_load_s))) {
		ret = -EFAULT;
		goto err;
	}

	if (copy_from_user(op_name, (void *)dsp_load.op_name,
			   OPERATOR_NAME_MAXLEN)) {
		ret = -EFAULT;
		goto err;
	}

	if (dsp_load.op_lib_dir) {
		op_dir = kzalloc(OPERATOR_DIR_MAXLEN, GFP_KERNEL);
		if (!op_dir) {
			ret = -ENOMEM;
			goto err;
		}
		if (copy_from_user(op_dir, (void *)dsp_load.op_lib_dir,
				   OPERATOR_DIR_MAXLEN)) {
			ret = -EFAULT;
			goto err;
		}
		dsp_debug("%s, op_dir=%s.\n", __func__, op_dir);
	}

	ret = load_operator(dsp->dev, op_dir, op_name, &handle);
	if (op_dir) {
		kfree(op_dir);
	}

	if (ret) {
		dsp_err("ioctl load oper %s, error.\n", op_name);
		goto err;
	}
	op = (struct dsp_op_desc *)handle;
	user = dsp_op_add_user(dsp_file, op);
	if (user == NULL) {
		ret = -EIO;
		dsp_err("ioctl add user error.\n");
		kref_put(&op->refcount, dsp_op_release);
		return ret;
	}
	dsp_load.op_handle = user->h.fd;
	if (copy_to_user(arg, &dsp_load, sizeof(dsp_ioctl_load_s))) {
		kernel_handle_release_family(&user->h);
		kernel_handle_decref(&user->h);
		dsp_err("ioctl load op copy_from_user err.\n");
		ret = -EFAULT;
		return ret;
	}

	dsp_debug("%s, ok, user fd=%d, refcount=%d.\n\n", __func__,
		  dsp_load.op_handle, kref_read(&user->h.refcount));
	kernel_handle_decref(&user->h);
err:
	return ret;
}

static u32 dsp_get_dma_addr(struct es_dsp *dsp, int fd,
			    struct dsp_dma_buf *map_buf)
{
	struct dma_buf *dmabuf;
	u32 dma_addr;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		dsp_err("fd = %d,  get dma buf error.\n", fd);
		return 0;
	}

	dma_addr = dev_mem_attach(dmabuf, dsp->dev, DMA_BIDIRECTIONAL,
				  &map_buf->attach);
	if (!dma_addr) {
		dsp_err("dev mem attach fd=%d failed.\n", fd);
		map_buf->attach = NULL;
		goto err_attach;
	}
	map_buf->dma_addr = dma_addr;
	map_buf->dmabuf = dmabuf;
	return dma_addr;

err_attach:
	dma_buf_put(dmabuf);
	return 0;
}

static void dsp_put_dma_addr(struct es_dsp *dsp, struct dsp_dma_buf *buf)
{
	int ret;
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;

	if (!buf || !buf->dmabuf || !buf->attach) {
		return;
	}
	attach = buf->attach;
	ret = dev_mem_detach(attach, DMA_BIDIRECTIONAL);

	dmabuf = buf->dmabuf;
	dma_buf_put(dmabuf);
	buf->dmabuf = NULL;
	buf->attach = NULL;
}

int dsp_unmap_dmabuf(struct dsp_file *dsp_file, struct dsp_dma_buf **buf,
		     int count)
{
	int i;
	struct es_dsp *dsp = dsp_file->dsp;
	struct dsp_dma_buf_ex *entry;

	for (i = 0; i < count; i++) {
		if (buf[i] == NULL) {
			continue;
		}
		if (buf[i]->fd == -1) {
			dsp_put_dma_addr(dsp, buf[i]);
			kfree(buf[i]);
			buf[i] = NULL;
		} else {
			entry = container_of(buf[i], struct dsp_dma_buf_ex,
					     buf);
			dsp_debug("%s, %d, fd=%d, handl cnt=%d.\n", __func__,
				  __LINE__, buf[i]->fd,
				  kref_read(&entry->handle.refcount));
			kernel_handle_decref(&entry->handle);
		}
	}
	return 0;
}
EXPORT_SYMBOL(dsp_unmap_dmabuf);

struct dsp_dma_buf *dsp_get_dma_buf_ex(struct dsp_file *dsp_file, int memfd)
{
	struct dsp_dma_buf_ex *entry;
	struct khandle *handle;
	struct dsp_dma_buf *dma_buf = NULL;
	struct dsp_dma_buf *dma_entry = NULL;
	u32 addr = 0;

	mutex_lock(&dsp_file->xrray_lock);
	entry = xa_load(&dsp_file->buf_xrray, memfd);
	if (entry) {
		dsp_debug("%s, %d. entry->fd=%d.\n", __func__, __LINE__,
			  entry->buf.fd);
		handle = find_kernel_handle(&dsp_file->h, entry->handle.fd,
					    DSP_DMABUF_HANDLE_MAGIC);
		dsp_debug("%s, %d, handle_fd=0x%x, refcount=%d.\n", __func__,
			  __LINE__, entry->handle.fd,
			  kref_read(&handle->refcount));
	}
	mutex_unlock(&dsp_file->xrray_lock);

	if (!entry || !handle) {
		dsp_debug("%s, %d, slow path.\n", __func__, __LINE__);
		dma_entry = kzalloc(sizeof(struct dsp_dma_buf), GFP_KERNEL);
		if (!dma_entry) {
			dsp_err("%s, %d. alloc dsp device buf struct err.\n",
				__func__, __LINE__);
			goto err;
		}
		addr = dsp_get_dma_addr(dsp_file->dsp, memfd, dma_entry);
		dma_entry->fd = -1;
	} else {
		dsp_debug("%s, %d, fast path, dma fd=%d, iova=0x%x.\n",
			  __func__, __LINE__, entry->buf.fd,
			  entry->buf.dma_addr);
		dma_entry = &entry->buf;
		addr = dma_entry->dma_addr;
	}
	if (addr == 0) {
		dsp_err("%s, %d, addr is err.\n", __func__, __LINE__);
		goto err;
	}
	return dma_entry;
err:
	return NULL;
}
EXPORT_SYMBOL(dsp_get_dma_buf_ex);

static int dsp_ioctl_set_flat(struct dsp_file *dsp_file, dsp_ioctl_task_s *req,
			      struct es_dsp_flat1_desc *flat,
			      struct dsp_dma_buf **dma_entry)
{
	int buffer_count;
	int i;
	struct es_dsp *dsp = dsp_file->dsp;
	struct dsp_dma_buf *entry = NULL;

	buffer_count = req->task.bufferCntCfg + req->task.bufferCntInput +
		       req->task.bufferCntOutput;

	for (i = 0; i < buffer_count; i++) {
		u32 addr;
		dsp_debug("%s, i=%d, new fd=%d, offset=0x%x.\n", __func__, i,
			  (int)req->task.dspBuffers[i].memFd,
			  req->task.dspBuffers[i].offset);

		entry = dsp_get_dma_buf_ex(dsp_file,
					   req->task.dspBuffers[i].memFd);
		if (entry == NULL) {
			goto err;
		}
		dma_entry[i] = entry;
		flat->buffers[i].addr =
			entry->dma_addr + req->task.dspBuffers[i].offset;
		flat->buffers[i].size = req->task.dspBuffers[i].size;
		dsp_debug("%s, i=%d, addr=0x%x, len=0x%x\n", __func__, i,
			  flat->buffers[i].addr, flat->buffers[i].size);
	}
	return 0;
err:
	dsp_unmap_dmabuf(dsp_file, dma_entry, i - 1);
	return -EINVAL;
}

static struct dsp_user *dsp_find_user_by_fd(struct dsp_file *dsp_file, int fd)
{
	struct khandle *user_handle;
	struct dsp_user *user;

	user_handle =
		find_kernel_handle(&dsp_file->h, fd, DSP_USER_HANDLE_MAGIC);
	if (!user_handle) {
		dsp_err("cannot find dsp operator for %d.\n", fd);
		return NULL;
	}

	user = (struct dsp_user *)container_of(user_handle, struct dsp_user, h);
	return user;
}

static int dsp_set_task_req(struct es_dsp *dsp, dsp_request_t *dsp_req,
			    dsp_ioctl_task_s *task)
{
	u32 dma_len;
	dma_addr_t dma_addr;
	struct es_dsp_flat1_desc *flat;
	int ret;
	u32 buffer_count;

	buffer_count = task->task.bufferCntCfg + task->task.bufferCntInput +
		       task->task.bufferCntOutput;
	dma_len = sizeof(struct es_dsp_flat1_desc) +
		  buffer_count * sizeof(es_dsp_buffer);

	flat = (struct es_dsp_flat1_desc *)dsp_alloc_flat_mem(dsp, dma_len,
							      &dma_addr);
	if (!flat) {
		dsp_err("cannot dma alloc mem for desc.\n");
		ret = -ENOMEM;
		return ret;
	}
	dsp_req->dsp_flat1_iova = dma_addr;

	dsp_req->sync_cache = true;
	dsp_req->poll_mode = task->task.pollMode;
	dsp_req->allow_eval = 1;
	dsp_req->flat_virt = (void *)flat;
	dsp_req->prio = task->task.priority;
	if (dsp_req->prio >= DSP_MAX_PRIO) {
		dsp_err("%s, %d, dsp request prio = %d is err.\n", __func__,
			__LINE__, dsp_req->prio);
		dsp_free_flat_mem(dsp, dma_len, (void *)flat, dma_addr);
		return -EINVAL;
	}
	dsp_req->flat_size = dma_len;

	flat->num_buffer = buffer_count;

	flat->input_index = task->task.bufferCntCfg;
	flat->output_index =
		task->task.bufferCntCfg + task->task.bufferCntInput;
	return 0;
}

static void dsp_user_async_req_complete(struct device *dev, dsp_request_t *req)
{
	struct dsp_user_req_async *user_req;
	struct es_dsp *dsp;
	struct dsp_file *dsp_file;
	struct dsp_user *user;

	if (!req) {
		dsp_err("%s, err, req is NULL.\n", __func__);
		return;
	}

	user_req = container_of(req, struct dsp_user_req_async, dsp_req);
	dsp = dev_get_drvdata(dev);
	if (!dsp) {
		dsp_err("%s, request dsp dev is null.\n", __func__);
		return;
	}
	user = user_req->user;
	dsp_file = user_req->dsp_file;
	dsp_unmap_dmabuf(dsp_file, user_req->dma_entry,
			 user_req->dma_buf_count);
	dsp_free_flat_mem(dsp, req->flat_size, req->flat_virt,
			  req->dsp_flat1_iova);
	dsp_debug("%s, %d.\n", __func__, __LINE__);
	kernel_handle_decref(&user_req->handle);
	kernel_handle_decref(&user->h);

	dsp_debug("%s, done.\n", __func__);
}

static void dsp_async_task_release(struct khandle *handle)
{
	struct dsp_user_req_async *user_req =
		container_of(handle, struct dsp_user_req_async, handle);
	struct dsp_user *user;
	struct es_dsp *dsp;

	if (!user_req) {
		dsp_err("%s, user_req is null.\n", __func__);
		return;
	}
	dsp = user_req->es_dsp;
	user = user_req->user;
	if(user_req->need_notify) {
		module_put(THIS_MODULE);
	}
	kfree(user_req);
	dsp_debug("%s, done.\n", __func__);
}

static void dsp_hw_complete_task(struct device *dev, dsp_request_t *req)
{
	struct dsp_file *dsp_file;
	struct dsp_user_req_async *async_task;
	unsigned long flags;
	struct es_dsp *dsp;
	async_task = container_of(req, struct dsp_user_req_async, dsp_req);

	dsp_file = async_task->dsp_file;
	dsp = dsp_file->dsp;

	spin_lock_irqsave(&dsp_file->async_ll_lock, flags);
	if (dsp_file->h.fd != INVALID_HANDLE_VALUE && async_task->need_notify) {
		list_add_tail(&async_task->async_ll,
			      &dsp_file->async_ll_complete);
		spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);
		wake_up_interruptible(&dsp_file->async_ll_wq);
	} else {
		spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);
		kernel_handle_decref(&async_task->handle);
	}

	spin_lock_irqsave(&dsp->complete_lock, flags);
	list_add_tail(&req->cpl_list, &dsp->complete_list);
	spin_unlock_irqrestore(&dsp->complete_lock, flags);
	if (!work_pending(&dsp->task_work)) {
		schedule_work(&dsp->task_work);
	}
}

static struct dsp_user_req_async *dsp_set_task_info(struct dsp_file *dsp_file,
						    dsp_ioctl_task_s *task,
						    bool need_notify)
{
	dsp_request_t *dsp_req;
	struct dsp_user_req_async *user_req;
	int ret;
	struct dsp_user *user;
	u32 buffer_count;
	struct dsp_dma_buf **dma_entry = NULL;
	struct es_dsp *dsp = dsp_file->dsp;

	user = dsp_find_user_by_fd(dsp_file, task->task.operatorHandle);
	if (!user) {
		dsp_err("cannot get user.\n");
		module_put(THIS_MODULE);
		return NULL;
	}

	buffer_count = task->task.bufferCntCfg + task->task.bufferCntInput +
		       task->task.bufferCntOutput;

	user_req = kzalloc(sizeof(struct dsp_user_req_async) +
				   sizeof(struct dsp_dma_buf *) * buffer_count,
			   GFP_KERNEL);
	if (!user_req) {
		kernel_handle_decref(&user->h);
		module_put(THIS_MODULE);
		dsp_err("kmalloc dsp request struct error.\n");
		return NULL;
	}

	ret = init_kernel_handle(&user_req->handle, dsp_async_task_release,
				 DSP_REQ_HANDLE_MAGIC, &dsp_file->h);
	if (ret) {
		dsp_err("init async task khandle error.\n");
		kernel_handle_decref(&user->h);
		kfree(user_req);
		module_put(THIS_MODULE);
		return NULL;
	}

	user_req->user = user;
	user_req->es_dsp = dsp;
	dma_entry = (struct dsp_dma_buf **)(user_req + 1);
	user_req->dma_entry = dma_entry;
	user_req->dma_buf_count = buffer_count;
	user_req->dsp_file = dsp_file;
	user_req->callback = task->task.callback;
	user_req->cbarg = task->task.cbArg;
	INIT_LIST_HEAD(&user_req->async_ll);

	dsp_debug("%s, user_req=0x%px.\n", __func__, user_req);
	dsp_req = &user_req->dsp_req;

	dsp_debug("%s,%d, dsp_req=0x%px.\n", __func__, __LINE__, dsp_req);

	ret = dsp_set_task_req(dsp, dsp_req, task);
	if (ret) {
		dsp_err("%s, %d, err, ret = %d.\n", __func__, __LINE__, ret);
		goto err_req;
	}
	user_req->req_cpl_handler = dsp_user_async_req_complete;
	dsp_req->cpl_handler = dsp_hw_complete_task;
	dsp_req->handle = (u64)(user->op);
	ret = dsp_ioctl_set_flat(dsp_file, task, dsp_req->flat_virt, dma_entry);
	if (ret != 0) {
		dsp_err("%s, %d, ret = %d.\n", __func__, __LINE__, ret);
		goto err_flat;
	}
	user_req->need_notify = need_notify;
	return user_req;
err_flat:
	dsp_free_flat_mem(dsp, dsp_req->flat_size, dsp_req->flat_virt,
			  dsp_req->dsp_flat1_iova);
err_req:
	kernel_handle_release_family(&user_req->handle);
	kernel_handle_decref(&user_req->handle);
	kernel_handle_decref(&user->h);
	if (need_notify) {
		module_put(THIS_MODULE);
	}
	return NULL;
}

static void dsp_free_task(struct dsp_file *dsp_file,
			  struct dsp_user_req_async *user_req)
{
	struct dsp_dma_buf **dma_entry = user_req->dma_entry;
	struct es_dsp *dsp = dsp_file->dsp;
	u32 buffer_count = user_req->dma_buf_count;
	dsp_request_t *dsp_req = &user_req->dsp_req;
	struct dsp_user *user = user_req->user;

	if (dma_entry) {
		dsp_unmap_dmabuf(dsp_file, dma_entry, buffer_count);
	}
	dsp_free_flat_mem(dsp, dsp_req->flat_size, dsp_req->flat_virt,
			  dsp_req->dsp_flat1_iova);

	kernel_handle_release_family(&user_req->handle);
	kernel_handle_decref(&user_req->handle);
	kernel_handle_decref(&user->h);
}

static long dsp_ioctl_submit_tsk_async(struct file *flip,
				       dsp_ioctl_task_s __user *arg)
{
	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;
	dsp_ioctl_task_s req;
	dsp_ioctl_task_s *task;
	dsp_request_t *dsp_req;
	struct dsp_user_req_async *user_req;
	int i, ret;

	if (copy_from_user(&req, arg, sizeof(dsp_ioctl_task_s))) {
		dsp_err("%s, %d, copy_from_user err.\n", __func__, __LINE__);
		ret = -EINVAL;
		return ret;
	}
	task = &req;

	// using reserved for op_idx
	dsp->op_idx = task->task.reserved;
	if ((dsp_perf_enable || dsp->perf_enable) &&
	    dsp->op_idx < MAX_DSP_TASKS) {
		dsp->op_perf[dsp->op_idx].OpStartCycle =
			0;  //get_perf_timer_cnt();
		dsp->op_perf[dsp->op_idx].Die = dsp->numa_id;
		dsp->op_perf[dsp->op_idx].CoreId = dsp->process_id;
		dsp->op_perf[dsp->op_idx].OpIndex = dsp->op_idx;
		dsp->op_perf[dsp->op_idx].OpType =
			dsp->process_id + 7;  // IDX_DSP0
	}

	if (!try_module_get(THIS_MODULE)) {
		dsp_err("%s, %d, cannot get module.\n", __func__, __LINE__);
		return -ENODEV;
	}

	user_req = dsp_set_task_info(dsp_file, task, true);
	if (user_req == NULL) {
		dsp_err("%s, %d, err\n", __func__, __LINE__);
		return -EIO;
	}
	req.task.taskHandle = user_req->handle.fd;

	kernel_handle_addref(&user_req->handle);
	ret = submit_task(dsp->dev, &user_req->dsp_req);
	if (ret) {
		kernel_handle_decref(&user_req->handle);
		dsp_err("submit task error.\n");
		goto err_task;
	}
	if (copy_to_user(arg, &req, sizeof(dsp_ioctl_task_s))) {
		dsp_err("copy to user err.\n");
		ret = -EINVAL;
	}
	return 0;
err_task:
	dsp_free_task(dsp_file, user_req);
	return ret;
}

static long
dsp_ioctl_process_complete_task(struct file *flip,
				dsp_ioctl_async_process_s __user *arg)
{
	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;
	struct dsp_user_req_async *async_req = NULL;
	long ret;
	dsp_ioctl_async_process_s query_task;
	dsp_task_status_s *task = NULL;
	struct dsp_user_req_async *async_task = NULL;
	unsigned long flags;

	if (copy_from_user(&query_task, arg,
			   sizeof(dsp_ioctl_async_process_s))) {
		ret = -EINVAL;
		return ret;
	}
	if (query_task.task_num <= 0) {
		dsp_err("parameter task num is invalid.\n");
		return -EINVAL;
	}
	query_task.return_num = 0;
	task = kzalloc(query_task.task_num * sizeof(dsp_task_status_s),
		       GFP_KERNEL);
	if (!task) {
		dsp_err("alloc memory for dsp_task_status error.\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&dsp_file->async_ll_lock, flags);
	if (list_empty_careful(&dsp_file->async_ll_complete)) {
		spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);
		if (query_task.timeout < 0) {
			ret = wait_event_interruptible(
				dsp_file->async_ll_wq,
				!list_empty(&dsp_file->async_ll_complete));
		} else {
			ret = wait_event_interruptible_timeout(
				dsp_file->async_ll_wq,
				!list_empty(&dsp_file->async_ll_complete),
				msecs_to_jiffies(query_task.timeout));
		}
	} else {
		spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);
	}

	if (ret == -ERESTARTSYS) {
		dsp_debug("interrupt by signal.\n");
		goto err_int;
	}
	spin_lock_irqsave(&dsp_file->async_ll_lock, flags);
	while (!list_empty_careful(&dsp_file->async_ll_complete)) {
		if (query_task.return_num >= query_task.task_num) {
			break;
		}

		async_req = list_first_entry(&dsp_file->async_ll_complete,
					     struct dsp_user_req_async,
					     async_ll);
		list_del_init(&async_req->async_ll);
		task[query_task.return_num].callback = async_req->callback;
		task[query_task.return_num].cbArg = async_req->cbarg;
		task[query_task.return_num].finish =
			async_req->dsp_req.d2h_msg.return_value;
		query_task.return_num++;
		spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);
		kernel_handle_release_family(&async_req->handle);
		kernel_handle_decref(&async_req->handle);
		spin_lock_irqsave(&dsp_file->async_ll_lock, flags);
	}
	spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);

	ret = 0;
	if (query_task.return_num != 0) {
		if (copy_to_user((void __user *)(query_task.task), task,
				 sizeof(dsp_task_status_s) *
					 query_task.return_num)) {
			ret = -EINVAL;
			dsp_err("copy to user task status err.\n");
		}
	}
err_int:
	kfree(task);
	if (copy_to_user(arg, &query_task, sizeof(dsp_ioctl_async_process_s))) {
		ret = -EINVAL;
	}
	return ret;
}

static long dsp_query_task(struct file *flip, dsp_ioctl_query_s *query_task)
{
	long ret = 0;
	struct dsp_file *dsp_file = flip->private_data;
	struct dsp_user_req_async *async_task = NULL;
	unsigned long flags;
	struct khandle *task_handle = NULL;

	query_task->finish = -ENODATA;
	task_handle = find_kernel_handle(&dsp_file->h, query_task->task_handle,
					 DSP_REQ_HANDLE_MAGIC);
	if (task_handle == NULL) {
		return -ENODATA;
	}

again:

	spin_lock_irqsave(&dsp_file->async_ll_lock, flags);
	if (list_empty_careful(&dsp_file->async_ll_complete)) {
		spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);
		if (query_task->block != 0) {
			ret = wait_event_interruptible(
				dsp_file->async_ll_wq,
				!list_empty(&dsp_file->async_ll_complete));
		} else {
			kernel_handle_decref(task_handle);
			return 0;
		}
	} else {
		spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);
	}

	if (ret == -ERESTARTSYS) {
		query_task->finish = -EINTR;
		dsp_debug("interrupt by signal.\n");
		goto err_int;
	}
	async_task =
		container_of(task_handle, struct dsp_user_req_async, handle);
	spin_lock_irqsave(&dsp_file->async_ll_lock, flags);
	if (list_empty(&async_task->async_ll)) {
		async_task = NULL;
	} else {
		list_del_init(&async_task->async_ll);
	}
	spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);

	if (async_task != NULL) {
		ret = 0;
		query_task->finish = async_task->dsp_req.d2h_msg.return_value;
		kernel_handle_release_family(&async_task->handle);
		kernel_handle_decref(&async_task->handle);
	} else if (query_task->block != 0) {
		goto again;
	}

err_int:
	kernel_handle_decref(task_handle);
	return ret;
}

static long dsp_ioctl_query_tsk(struct file *flip,
				dsp_ioctl_query_s __user *arg)
{
	int ret;
	dsp_ioctl_query_s query_task;

	if (copy_from_user(&query_task, arg, sizeof(dsp_ioctl_query_s))) {
		ret = -EINVAL;
		return ret;
	}
	ret = dsp_query_task(flip, &query_task);
	if (copy_to_user(arg, &query_task, sizeof(dsp_ioctl_query_s))) {
		ret = -EINVAL;
	}
	return ret;
}

static void dsp_release_user_completed_task(struct dsp_file *dsp_file)
{
	struct dsp_user_req_async *tmp = NULL, *async_task = NULL;
	unsigned long flags;
	struct list_head *tmp_list;
	spin_lock_irqsave(&dsp_file->async_ll_lock, flags);
	list_for_each_entry_safe(async_task, tmp, &dsp_file->async_ll_complete,
				 async_ll) {
		spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);
		list_del_init(&async_task->async_ll);
		kernel_handle_decref(&async_task->handle);
		spin_lock_irqsave(&dsp_file->async_ll_lock, flags);
	}
	spin_unlock_irqrestore(&dsp_file->async_ll_lock, flags);
}

static long dsp_ioctl_unload_op(struct file *flip, void *arg)
{
	int fd;
	struct dsp_file *dsp_file = (struct dsp_file *)flip->private_data;
	struct dsp_user *user;

	if (copy_from_user(&fd, arg, sizeof(fd))) {
		dsp_err("%s, %d, copy_from_user err.\n", __func__, __LINE__);
		return -EINVAL;
	}
	dsp_debug("", __func__, __LINE__);
	user = dsp_find_user_by_fd(dsp_file, fd);
	if (user == NULL) {
		dsp_debug("%s, %d, cannot find user fd=%d.\n", __func__,
			  __LINE__, fd);
		return -EINVAL;
	}
	kernel_handle_release_family(&user->h);
	kernel_handle_decref(&user->h);
	dsp_debug("%s, %d, done.\n", __func__, __LINE__);
	return 0;
}

static void dsp_dma_buf_release(struct khandle *h)
{
	struct dsp_dma_buf_ex *entry =
		container_of(h, struct dsp_dma_buf_ex, handle);
	struct dsp_file *dsp_file = entry->buf.dsp_file;
	struct es_dsp *dsp = dsp_file->dsp;

	dsp_put_dma_addr(dsp, &entry->buf);
	dsp_debug("%s, %d, fd=0x%x, iova=0x%x.\n", __func__, __LINE__,
		  entry->buf.fd, entry->buf.dma_addr);
	kfree(entry);
}

static long dsp_ioctl_prepare_dma(struct file *flip, dsp_ioctl_pre_dma_s *arg)
{
	struct dsp_file *dsp_file = (struct dsp_file *)flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;
	dsp_ioctl_pre_dma_s buf;
	struct dsp_dma_buf_ex *entry, *tmp;
	struct khandle *khandle;
	u32 addr;
	int ret;

	if (copy_from_user(&buf, arg, sizeof(buf))) {
		dsp_err("%s, %d, copy from user err.\n", __func__, __LINE__);
		return -EINVAL;
	}
	mutex_lock(&dsp_file->xrray_lock);
	entry = xa_load(&dsp_file->buf_xrray, (int)buf.desc.memFd);
	if (entry) {
		khandle = find_kernel_handle(&dsp_file->h, entry->handle.fd,
					     DSP_DMABUF_HANDLE_MAGIC);
		BUG_ON(khandle == NULL);
		dsp_debug("%s, %d, find %d entry, handle fd=%d.\n", __func__,
			  __LINE__, entry->buf.fd, entry->handle.fd);
		entry->count++;
		kernel_handle_decref(khandle);
		mutex_unlock(&dsp_file->xrray_lock);
		return 0;
	}

	entry = kzalloc(sizeof(struct dsp_dma_buf_ex), GFP_KERNEL);
	if (!entry) {
		dsp_err("%s, %d, alloc memory for device dma buf err.\n",
			__func__, __LINE__);
		mutex_unlock(&dsp_file->xrray_lock);
		return -ENOMEM;
	}

	ret = init_kernel_handle(&entry->handle, dsp_dma_buf_release,
				 DSP_DMABUF_HANDLE_MAGIC, &dsp_file->h);
	if (ret) {
		dsp_err("%s, %d, init kernel handle err.\n", __func__,
			__LINE__);
		kfree(entry);
		mutex_unlock(&dsp_file->xrray_lock);
		return ret;
	}

	dsp_debug("%s, %d, handle_fd=%d.\n", __func__, __LINE__,
		  entry->handle.fd);
	addr = dsp_get_dma_addr(dsp, (int)buf.desc.memFd, &entry->buf);
	if (addr == 0) {
		dsp_err("%s, %d, addr err.\n", __func__, __LINE__);
		ret = -ENODEV;
		goto err;
	}
	entry->offset = buf.desc.offset;
	entry->buf.dsp_file = dsp_file;
	entry->buf.fd = (int)buf.desc.memFd;
	xa_store(&dsp_file->buf_xrray, (int)buf.desc.memFd, entry, GFP_KERNEL);

	kernel_handle_decref(&entry->handle);
	entry->count = 1;
	mutex_unlock(&dsp_file->xrray_lock);

	dsp_debug("%s,%d, fd=0x%x, buf_desc_fd=%d,  dma_addr=0x%x.\n", __func__,
		  __LINE__, entry->buf.fd, buf.desc.memFd, entry->buf.dma_addr);

	return 0;
err:
	kernel_handle_release_family(&entry->handle);
	kernel_handle_decref(&entry->handle);
	mutex_unlock(&dsp_file->xrray_lock);

	return ret;
}
static long dsp_ioctl_unprepare_dma(struct file *flip, u32 *arg)
{
	u32 fd;
	struct dsp_file *dsp_file = (struct dsp_file *)flip->private_data;
	struct dsp_dma_buf_ex *entry;
	struct khandle *khandle;
	dsp_debug("%s, %d.\n", __func__, __LINE__);

	if (copy_from_user(&fd, arg, sizeof(fd))) {
		dsp_err("%s, %d, copy from user err.\n", __func__, __LINE__);
		return -EINVAL;
	}
	mutex_lock(&dsp_file->xrray_lock);
	entry = xa_load(&dsp_file->buf_xrray, fd);
	if (!entry) {
		dsp_err("%s,%d, cannot find %d dmabuf entry.\n", __func__,
			__LINE__, fd);
		mutex_unlock(&dsp_file->xrray_lock);
		return -EINVAL;
	}
	BUG_ON(entry->count <= 0);
	entry->count--;
	if (entry->count) {
		mutex_unlock(&dsp_file->xrray_lock);
		return 0;
	}
	dsp_debug("%s, %d, entry->fd=%d.\n\n", __func__, __LINE__,
		  entry->buf.fd);
	khandle = find_kernel_handle(&dsp_file->h, entry->handle.fd,
				     DSP_DMABUF_HANDLE_MAGIC);

	BUG_ON(!khandle);
	xa_erase(&dsp_file->buf_xrray, entry->buf.fd);
	kernel_handle_release_family(khandle);
	kernel_handle_decref(khandle);
	mutex_unlock(&dsp_file->xrray_lock);
	return 0;
}

static long dsp_ioctl_enable_perf(struct file *flip, u32 *arg)
{
	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;

	if (copy_from_user(&dsp->perf_enable, arg, sizeof(dsp->perf_enable))) {
		dsp_err("%s, %d, copy from user err.\n", __func__, __LINE__);
		return -EINVAL;
	}

	return 0;
}

static long dsp_ioctl_get_perf_data(struct file *flip, dsp_kmd_perf_t *data)
{
	int ret = 0;

	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;

	if (copy_to_user(data, dsp->op_perf,
			 sizeof(dsp_kmd_perf_t) * MAX_DSP_TASKS)) {
		dsp_err("copy perf data to user err.\n");
		ret = -EINVAL;
	}
	dsp_debug("get dsp kmd perf data done.\n");

	return ret;
}

extern void get_dsp_perf_info(es_dsp_perf_info *perf_info, int die_num,
			      int dsp_num);

static long dsp_ioctl_get_fw_perf_data(struct file *flip, dsp_fw_perf_t *data)
{
	int ret = 0;

	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;
	es_dsp_perf_info perf_info;

	get_dsp_perf_info(&perf_info, 0, 0);

	dsp->op_fw_perf[0].Die = 0;
	dsp->op_fw_perf[0].CoreId = perf_info.core_id;
	dsp->op_fw_perf[0].OpIndex = perf_info.op_index;
	dsp->op_fw_perf[0].OpType = perf_info.op_type;
	// dsp->op_fw_perf[0].OpName = "";
	dsp->op_fw_perf[0].OpStartCycle = perf_info.flat1_start_time;
	dsp->op_fw_perf[0].OpPrepareStartCycle = perf_info.prepare_start_time;
	dsp->op_fw_perf[0].OpPrepareEndCycle = perf_info.prepare_end_time;
	dsp->op_fw_perf[0].OpEvalStartCycle = perf_info.eval_start_time;
	dsp->op_fw_perf[0].OpEvalEndCycle = perf_info.eval_end_time;
	dsp->op_fw_perf[0].OpNotifyStartCycle = perf_info.notify_start_time;
	dsp->op_fw_perf[0].OpEndCycle = perf_info.flat1_end_time;

	if (copy_to_user(data, dsp->op_fw_perf,
			 sizeof(dsp_fw_perf_t) * MAX_DSP_TASKS)) {
		dsp_err("copy perf data to user err.\n");
		ret = -EINVAL;
	}
	dsp_debug("get dsp%u hardware perf data done.\n", perf_info.op_index);

	return ret;
}

static long dsp_ioctl_multi_tasks_submit(struct file *flip,
					 dsp_ioctl_task_s __user *arg)
{
	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp = dsp_file->dsp;
	dsp_ioctl_task_s req;
	dsp_ioctl_task_s *tasks;
	dsp_request_t *dsp_req;
	struct dsp_user_req_async **user_req;
	int i, ret;
	unsigned long flags;

	if (copy_from_user(&req, arg, sizeof(dsp_ioctl_task_s))) {
		dsp_err("%s, %d, copy_from_user err.\n", __func__, __LINE__);
		ret = -EINVAL;
		return ret;
	}

	if (req.task_num <= 0) {
		dsp_err("%s, %d, task num below zero, err,\n", __func__,
			__LINE__);
		return -EINVAL;
	}
	tasks = kzalloc(req.task_num * (sizeof(dsp_ioctl_task_s) +
					sizeof(struct dsp_user_req_async *)),
			GFP_KERNEL);
	if (tasks == NULL) {
		dsp_err("", __func__, __LINE__);
		return -ENOMEM;
	}
	if (copy_from_user(tasks, arg,
			   req.task_num * sizeof(dsp_ioctl_task_s))) {
		dsp_err("%s, %d, copy_from_user for multi tasks err.\n",
			__func__, __LINE__);
		kfree(tasks);
		return -EINVAL;
	}
	if (!try_module_get(THIS_MODULE)) {
		dsp_err("%s, %d, cannot get module.\n", __func__, __LINE__);
		kfree(tasks);
		return -ENODEV;
	}
	user_req = (struct dsp_user_req_async **)(tasks + req.task_num);

	for (i = 0; i < req.task_num; i++) {
		user_req[i] = dsp_set_task_info(dsp_file, &tasks[i], false);
		if (user_req[i] == NULL) {
			dsp_err("%s, %d, and ,i = %d.\n", __func__, __LINE__,
				i);
			goto free_task;
		}
		tasks[i].task.taskHandle = user_req[i]->handle.fd;
	}
	user_req[req.task_num - 1]->need_notify = true;

	if (dsp->off) {
		dsp_err("es dsp off.\n");
		ret = -ENODEV;
		goto free_task;
	}

	spin_lock_irqsave(&dsp->send_lock, flags);
	for (i = 0; i < req.task_num; i++) {
		kernel_handle_addref(&user_req[i]->handle);
		dsp_req = &user_req[i]->dsp_req;
		dsp_set_flat_func(dsp_req->flat_virt, dsp_req->handle);
		__dsp_enqueue_task(dsp, dsp_req);
	}
	spin_unlock_irqrestore(&dsp->send_lock, flags);

	dsp_schedule_task(dsp);

	if (copy_to_user(arg, tasks, req.task_num * sizeof(dsp_ioctl_task_s))) {
		dsp_err("copy to user err.\n");
		ret = -EINVAL;
	}

	kfree(tasks);
	return 0;

free_task:
	for (i = 0; i < req.task_num; i++) {
		if (user_req[i] != NULL)
			dsp_free_task(dsp_file, user_req[i]);
	}
	kfree(tasks);
	return ret;
}
static long dsp_ioctl(struct file *flip, unsigned int cmd, unsigned long arg)
{
	long retval;

	switch (cmd) {
	case DSP_IOCTL_LOAD_OP:
		retval = dsp_ioctl_load_op(flip, (dsp_ioctl_load_s *)arg);
		break;
	case DSP_IOCTL_UNLOAD_OP:
		retval = dsp_ioctl_unload_op(flip, arg);
		break;
	case DSP_IOCTL_SUBMIT_TSK_ASYNC:
		retval = dsp_ioctl_submit_tsk_async(
			flip, (dsp_ioctl_task_s __user *)arg);
		break;
	case DSP_IOCTL_SUBMIT_TSKS_ASYNC:
		// TODO
		retval = dsp_ioctl_multi_tasks_submit(
			flip, (dsp_ioctl_task_s __user *)arg);
		break;
	case DSP_IOCTL_PROCESS_REPORT:
		retval = dsp_ioctl_process_complete_task(
			flip, (dsp_ioctl_async_process_s __user *)arg);
		break;
	case DSP_IOCTL_QUERY_TASK:
		retval = dsp_ioctl_query_tsk(flip,
					     (dsp_ioctl_query_s __user *)arg);
		break;
	case DSP_IOCTL_PREPARE_DMA:
		retval = dsp_ioctl_prepare_dma(
			flip, (dsp_ioctl_pre_dma_s __user *)arg);
		break;
	case DSP_IOCTL_UNPREPARE_DMA:
		retval = dsp_ioctl_unprepare_dma(flip, (u32 __user *)arg);
		break;
	case DSP_IOCTL_ENABLE_PERF:
		retval = dsp_ioctl_enable_perf(flip, (u32 __user *)arg);
		break;
	case DSP_IOCTL_GET_PERF_DATA:
		retval = dsp_ioctl_get_perf_data(flip,
						 (dsp_kmd_perf_t __user *)arg);
		break;
	case DSP_IOCTL_GET_FW_PERF_DATA:
		retval = dsp_ioctl_get_fw_perf_data(
			flip, (dsp_fw_perf_t __user *)arg);
		break;
	default:
		retval = es_dsp_hw_ioctl(flip, cmd, arg);
		break;
	}
	return retval;
}

static void dsp_file_release(struct khandle *handle)
{
	struct dsp_file *dsp_file = container_of(handle, struct dsp_file, h);
	struct es_dsp *dsp;

	if (!dsp_file) {
		return;
	}

	dsp = dsp_file->dsp;
	kfree(dsp_file);
	es_dsp_pm_put_sync(dsp);
	dsp_debug("release dsp_file done.\n");
}

static int dsp_open(struct inode *inode, struct file *flip)
{
	struct es_dsp *dsp =
		container_of(flip->private_data, struct es_dsp, miscdev);
	struct dsp_file *dsp_file;
	int ret;

	dsp_info("%s %d, pid %d\n", __func__, __LINE__, current->pid);
	if (dsp->off) {
		dsp_err("%s, %d, dsp fw is offline.\n", __func__, __LINE__);
		return -EIO;
	}
	ret = es_dsp_pm_get_sync(dsp);
	if (ret < 0) {
		dsp_err("%s, %d, pm get sync err, ret=%d.\n", __func__,
			__LINE__, ret);
		return ret;
	}
	dsp_file = kzalloc(sizeof(*dsp_file), GFP_KERNEL);
	if (!dsp_file) {
		es_dsp_pm_put_sync(dsp);
		return -ENOMEM;
	}

	ret = init_kernel_handle(&dsp_file->h, dsp_file_release,
				 DSP_FILE_HANDLE_MAGIC, NULL);
	if (ret != 0) {
		dsp_err("%s, init kernel handle error.\n", __func__);
		kfree(dsp_file);
		es_dsp_pm_put_sync(dsp->dev);
		return ret;
	}
	INIT_LIST_HEAD(&dsp_file->async_ll_complete);
	spin_lock_init(&dsp_file->async_ll_lock);
	init_waitqueue_head(&dsp_file->async_ll_wq);
	mutex_init(&dsp_file->xrray_lock);
	xa_init(&dsp_file->buf_xrray);

	dsp_file->dsp = dsp;
	flip->private_data = dsp_file;
	kernel_handle_decref(&dsp_file->h);
	return 0;
}

static int dsp_close(struct inode *inode, struct file *flip)
{
	struct dsp_file *dsp_file = flip->private_data;
	struct es_dsp *dsp;

	if (!dsp_file) {
		return 0;
	}

	dsp = dsp_file->dsp;
	dsp_info("%s %d, pid %d\n", __func__, __LINE__, current->pid);
	mutex_lock(&dsp_file->xrray_lock);
	xa_destroy(&dsp_file->buf_xrray);
	mutex_unlock(&dsp_file->xrray_lock);

	kernel_handle_addref(&dsp_file->h);
	kernel_handle_release_family(&dsp_file->h);
	dsp_release_user_completed_task(dsp_file);
	kernel_handle_decref(&dsp_file->h);

	flip->private_data = NULL;
	dsp_info("%s, close done.\n", __func__);
	return 0;
}

#ifdef CONFIG_COMPAT
static long dsp_ioctl_compat(struct file *flip, unsigned int cmd,
			     unsigned long arg)
{
	return dsp_ioctl(flip, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static unsigned int dsp_poll(struct file *flip, poll_table *wait)
{
	struct dsp_file *dsp_file = flip->private_data;

	if (!dsp_file) {
		return EPOLLERR;
	}

	poll_wait(flip, &dsp_file->async_ll_wq, wait);

	if (!list_empty_careful(&dsp_file->async_ll_complete)) {
		return EPOLLIN;
	}
	return 0;
}

const struct file_operations dsp_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.unlocked_ioctl = dsp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = dsp_ioctl_compat,
#endif
	.poll = dsp_poll,
	.open = dsp_open,
	.release = dsp_close,
};
