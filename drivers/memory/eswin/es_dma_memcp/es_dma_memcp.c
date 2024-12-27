// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DMA MEMCP Driver
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
 * Authors: Zonglin Geng <gengzonglin@eswincomputing.com>
 *          Yuyang Cong <congyuyang@eswincomputing.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#include <linux/dmaengine.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <linux/dma-buf.h>
#include <linux/workqueue.h>
#include <uapi/linux/dma_memcp.h>

#define DRIVER_NAME 		"es_memcp"

#define MAX_NUM_USED_DMA_CH       (4)

static struct device *esw_memcp_dev;

struct esw_memcp_dma_buf_info {
    struct dma_buf *dma_buf;
    int mem_nid;
    struct dma_buf_attachment *attach;
    struct sg_table *sgt;
};

struct cmdq {
    struct workqueue_struct *wq;
    struct mutex lock;
    atomic_t total_tasks;
    atomic_t completed_tasks;
};

struct esw_cmdq_task {
    struct cmdq *cmdq;
    struct esw_memcp_f2f_cmd f2f_cmd;
    struct esw_memcp_dma_buf_info src_buf;
    struct esw_memcp_dma_buf_info dst_buf;
    struct work_struct work;
    struct dma_chan *dma_ch; /* pointer to the dma channel. */
    struct completion dma_finished;
};

#ifdef CONFIG_NUMA
static int memcp_attach_dma_buf(struct device *dma_dev, struct esw_memcp_dma_buf_info *buf_info);
static int memcp_detach_dma_buf(struct dma_buf_attachment *attach, struct sg_table *sgt);
#define PAGE_IN_SPRAM_DIE0(page) ((page_to_phys(page)>=0x59000000) && (page_to_phys(page)<0x59400000))
#define PAGE_IN_SPRAM_DIE1(page) ((page_to_phys(page)>=0x79000000) && (page_to_phys(page)<0x79400000))
static int esw_memcp_get_mem_nid(struct esw_memcp_dma_buf_info *buf_info)
{
    int ret = 0;
    struct page *page = NULL;
    int nid = -1;

    ret = memcp_attach_dma_buf(esw_memcp_dev, buf_info);
    if(ret) {
        pr_err("Failed to attach DMA buffer, ret=%d\n", ret);
        return ret;
    }

    page = sg_page(buf_info->sgt->sgl);
    if (unlikely(PAGE_IN_SPRAM_DIE0(page))) {
        nid = 0;
    }
    else if(unlikely(PAGE_IN_SPRAM_DIE1(page))) {
        nid = 1;
    }
    else {
        nid = page_to_nid(page);
    }

    ret = memcp_detach_dma_buf(buf_info->attach, buf_info->sgt);
    if(ret) {
        pr_err("Failed to detach DMA buffer, , ret=%d\n", ret);
        return ret;
    }

    buf_info->mem_nid = nid;

    return ret;
}
#else
static int esw_memcp_get_mem_nid(struct esw_memcp_dma_buf_info *buf_info)
{
    buf_info->mem_nid = 0;
    return 0;
}
#endif

static bool filter(struct dma_chan *chan, void *param)
{
#ifdef CONFIG_NUMA
    if((*(int *)param) == 2)
        return true;
    else
        return (*(int *)param) == dev_to_node(chan->device->dev);
#else
    return true;
#endif
}


int esw_memcp_alloc_dma(struct esw_cmdq_task *task)
{
    int ret = 0;
    dma_cap_mask_t mask;
    struct dma_chan *dma_ch = NULL;

    ret = esw_memcp_get_mem_nid(&task->src_buf);
    if(ret) {
        return ret;
    }

    dma_cap_zero(mask);
    dma_cap_set(DMA_MEMCPY, mask);

    dma_ch = dma_request_channel(mask, filter, &task->src_buf.mem_nid);
    if (IS_ERR(dma_ch)) {
        pr_warn("dma request channel failed, Try using any of them.\n");
        dma_ch = dma_request_channel(mask, NULL, NULL);
    }

    if (IS_ERR(dma_ch)) {
        pr_err("dma request channel failed\n");
        return -ENODEV;
    }

    task->dma_ch = dma_ch;
    return 0;
}

static int memcp_attach_dma_buf(struct device *dma_dev, struct esw_memcp_dma_buf_info *buf_info)
{
    int ret = 0;
    struct dma_buf *dma_buf;
    struct dma_buf_attachment *attach;
    struct sg_table *sgt;

    if (!buf_info || !dma_dev) {
        pr_err("Invalid parameters: buf_info or dma_dev is NULL\n");
        return -EINVAL;
    }

    dma_buf = buf_info->dma_buf;
    if (IS_ERR(dma_buf)) {
        return PTR_ERR(dma_buf);
    }
    /* Ref + 1 */
    attach = dma_buf_attach(dma_buf, dma_dev);
    if (IS_ERR(attach)) {
        ret = PTR_ERR(attach);
        return ret;
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    sgt = dma_buf_map_attachment_unlocked(attach, DMA_BIDIRECTIONAL);
#else
    sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
#endif
    if (IS_ERR(sgt)) {
        ret = PTR_ERR(sgt);
        dma_buf_detach(dma_buf, attach);
        return ret;
    }
#ifdef DMA_MEMCP_DEBUG_EN
    struct scatterlist *sg = NULL;
    u64 addr;
    int len;
    int i = 0;
    for_each_sgtable_dma_sg(sgt, sg, i) {
        addr = sg_dma_address(sg);
        len = sg_dma_len(sg);
    }
#endif

    buf_info->attach = attach;
    buf_info->sgt = sgt;
    return ret;
}

static int memcp_detach_dma_buf(struct dma_buf_attachment *attach, struct sg_table *sgt)
{
    int ret = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    dma_buf_unmap_attachment_unlocked(attach, sgt, DMA_BIDIRECTIONAL);
#else
    dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
#endif
    /* detach attach->dma_buf*/
    dma_buf_detach(attach->dmabuf, attach);
    return ret;
}

static int memcp_detach_dma_buf_check(struct esw_memcp_dma_buf_info *buf_info)
{
    int ret = 0;
    if(buf_info->attach) {
        ret = memcp_detach_dma_buf(buf_info->attach, buf_info->sgt);
        buf_info->attach = NULL;
    }
    return ret;
}

static int esw_memcp_release_cmdq_task(struct esw_cmdq_task  *cmdq_task)
{
    memcp_detach_dma_buf_check(&cmdq_task->src_buf);
    memcp_detach_dma_buf_check(&cmdq_task->dst_buf);

    if (cmdq_task->src_buf.dma_buf) {
        dma_buf_put(cmdq_task->src_buf.dma_buf);
        cmdq_task->src_buf.dma_buf = NULL;
    }
    if (cmdq_task->dst_buf.dma_buf) {
        dma_buf_put(cmdq_task->dst_buf.dma_buf);
        cmdq_task->dst_buf.dma_buf = NULL;
    }

    if (cmdq_task->dma_ch) {
        dma_release_channel(cmdq_task->dma_ch);
        cmdq_task->dma_ch = NULL;
    }

    kfree(cmdq_task);
    return 0;
}


static void esw_memcp_complete_func(void *cb_param)
{
    struct esw_cmdq_task *cmdq_task = (struct esw_cmdq_task *)cb_param;

    complete(&cmdq_task->dma_finished);
}

static int esw_memcp_start_dma_f2f_trans(struct esw_cmdq_task  *cmdq_task)
{
    int ret = 0;
    struct esw_memcp_f2f_cmd *f2f_cmd;
    struct dma_buf_attachment *attach;
    struct sg_table *sgt;
    struct dma_buf *dma_buf;
    struct dma_chan *dma_ch;
    struct device *dma_dev;
    enum dma_ctrl_flags flags;
    dma_cookie_t cookie;  //used for judge whether trans has been completed.
    struct dma_async_tx_descriptor *tx = NULL;
    dma_addr_t src_buf_addr, dst_buf_addr;
    size_t src_size, dst_size;

    ret = esw_memcp_alloc_dma(cmdq_task);
    if(ret) {
        goto release_cmdq_task;
    }

    f2f_cmd = &cmdq_task->f2f_cmd;
    dma_ch = cmdq_task->dma_ch;
    dma_dev = dmaengine_get_dma_device(dma_ch);

    ret = memcp_attach_dma_buf(dma_dev, &cmdq_task->src_buf);
    if(ret) {
        goto release_cmdq_task;
    }
    attach = cmdq_task->src_buf.attach;
    sgt = cmdq_task->src_buf.sgt;
    src_buf_addr = sg_dma_address(sgt->sgl) + f2f_cmd->src_offset;
    dma_buf = attach->dmabuf;
    src_size = dma_buf->size;

    ret = memcp_attach_dma_buf(dma_dev, &cmdq_task->dst_buf);
    if(ret) {
        goto release_cmdq_task;
    }
    attach = cmdq_task->dst_buf.attach;
    sgt = cmdq_task->dst_buf.sgt;
    dst_buf_addr = sg_dma_address(sgt->sgl) + f2f_cmd->dst_offset;
    dma_buf = attach->dmabuf;
    dst_size = dma_buf->size;

    init_completion(&cmdq_task->dma_finished);
    dma_ch = cmdq_task->dma_ch;
    flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
    tx = dmaengine_prep_dma_memcpy(dma_ch, dst_buf_addr, src_buf_addr, f2f_cmd->len, flags);
    if(!tx){
        pr_err("Failed to prepare DMA memcp\n");
        ret = -EFAULT;
        goto release_cmdq_task;
    }

    tx->callback = esw_memcp_complete_func;
    tx->callback_param = cmdq_task;

    cookie = dmaengine_submit(tx);
    if(dma_submit_error(cookie)){
        pr_err("Failed to dma tx_submit\n");
        ret = -EFAULT;
        goto release_cmdq_task;
    }

    dma_async_issue_pending(dma_ch);
    if (wait_for_completion_interruptible_timeout(&cmdq_task->dma_finished,
        msecs_to_jiffies(cmdq_task->f2f_cmd.timeout)) == 0) {
        pr_err("DMA transfer timeout.\n");
        ret = -ETIMEDOUT;
        dmaengine_terminate_sync(dma_ch);
        goto release_cmdq_task;
    }

release_cmdq_task:
    esw_memcp_release_cmdq_task(cmdq_task);
    return ret;
}


static void cmdq_task_worker(struct work_struct *work) {
    struct esw_cmdq_task *task = container_of(work, struct esw_cmdq_task, work);
    struct cmdq *q = task->cmdq;
    int ret;

    if (!task) {
        pr_err("cmdq_task_worker: Invalid task pointer\n");
        return;
    }

    // Start DMA Transfer
    ret = esw_memcp_start_dma_f2f_trans(task);

    if (ret) {
        pr_err("cmdq_task_worker: DMA transfer failed with error code %d\n", ret);
    }
    atomic_inc(&q->completed_tasks);

}

static int esw_cmdq_add_task(struct file *filp, void __user *user_arg) {
    struct cmdq *q = (struct cmdq *)filp->private_data;
    struct esw_cmdq_task *cmdq_task;
    struct esw_memcp_f2f_cmd memcp_f2f_cmd;
    int ret;

    if (!q || !q->wq) {
        pr_err("CMDQ or workqueue is NULL\n");
        return -EINVAL;
    }

    cmdq_task = kzalloc(sizeof(struct esw_cmdq_task), GFP_KERNEL);
    if (!cmdq_task) {
        pr_err("Failed to allocate cmdq_task\n");
        return -ENOMEM;
    }

    if (copy_from_user(&memcp_f2f_cmd, user_arg, sizeof(struct esw_memcp_f2f_cmd))) {
        pr_err("Failed to copy data from user space\n");
        kfree(cmdq_task);
        return -EFAULT;
    }

    cmdq_task->cmdq = q;
    cmdq_task->f2f_cmd = memcp_f2f_cmd;

    cmdq_task->src_buf.dma_buf = dma_buf_get(memcp_f2f_cmd.src_fd);
    if (IS_ERR(cmdq_task->src_buf.dma_buf)) {
        pr_err("Failed to get src dma_buf, src_fd=%d, err=%ld\n",
                memcp_f2f_cmd.src_fd, PTR_ERR(cmdq_task->src_buf.dma_buf));
        kfree(cmdq_task);
        return PTR_ERR(cmdq_task->src_buf.dma_buf);
    }

    if ((memcp_f2f_cmd.src_offset + memcp_f2f_cmd.len) > cmdq_task->src_buf.dma_buf->size) {
        pr_err("Source buffer overflow: src_offset=%d, len=%lu, buf_size=%lu\n",
                memcp_f2f_cmd.src_offset, memcp_f2f_cmd.len, cmdq_task->src_buf.dma_buf->size);
        dma_buf_put(cmdq_task->src_buf.dma_buf);
        kfree(cmdq_task);
        return -EINVAL;
    }

    cmdq_task->dst_buf.dma_buf = dma_buf_get(memcp_f2f_cmd.dst_fd);
    if (IS_ERR(cmdq_task->dst_buf.dma_buf)) {
        pr_err("Failed to get dst dma_buf, dst_fd=%d, err=%ld\n",
                memcp_f2f_cmd.dst_fd, PTR_ERR(cmdq_task->dst_buf.dma_buf));
        dma_buf_put(cmdq_task->src_buf.dma_buf);
        kfree(cmdq_task);
        return PTR_ERR(cmdq_task->dst_buf.dma_buf);
    }

    if ((memcp_f2f_cmd.dst_offset + memcp_f2f_cmd.len) > cmdq_task->dst_buf.dma_buf->size) {
        pr_err("Destination buffer overflow: dst_offset=%d, len=%lu, buf_size=%lu\n",
                memcp_f2f_cmd.dst_offset, memcp_f2f_cmd.len, cmdq_task->dst_buf.dma_buf->size);
        dma_buf_put(cmdq_task->src_buf.dma_buf);
        dma_buf_put(cmdq_task->dst_buf.dma_buf);
        kfree(cmdq_task);
        return -EINVAL;
    }

    INIT_WORK(&cmdq_task->work, cmdq_task_worker);
    ret = queue_work(cmdq_task->cmdq->wq, &cmdq_task->work);
    if (!ret) {
        pr_err("Failed to queue work\n");
        dma_buf_put(cmdq_task->src_buf.dma_buf);
        dma_buf_put(cmdq_task->dst_buf.dma_buf);
        kfree(cmdq_task);
        return -EFAULT;
    }

    atomic_inc(&q->total_tasks);

    return 0;
}

static int esw_cmdq_sync(struct file *filp) {
    struct cmdq *q = (struct cmdq *)filp->private_data;

    if (!q) {
        pr_err("esw_cmdq_sync: Invalid cmdq\n");
        return -EINVAL;
    }

    flush_workqueue(q->wq);

    return 0;
}



static int esw_cmdq_query(struct file *file, void __user *user_arg)
{
    struct cmdq *q;
    struct esw_cmdq_query query;

    if (!file || !user_arg) {
        pr_err("esw_cmdq_query: Invalid arguments\n");
        return -EINVAL;
    }

    q = file->private_data;
    if (!q) {
        pr_err("esw_cmdq_query: No cmdq associated with this file descriptor\n");
        return -EINVAL;
    }

    int total = atomic_read(&q->total_tasks);
    int completed = atomic_read(&q->completed_tasks);

    query.status = (total == completed) ? 0 : 1; // 0 FREE，1 BUSY
    query.task_count = total - completed;

    if (copy_to_user(user_arg, &query, sizeof(query))) {
        pr_err("esw_cmdq_query: Failed to copy data to user space\n");
        return -EFAULT;
    }

    return 0;
}

static long esw_memcp_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0;

    switch (cmd) {
    case (ESW_CMDQ_ADD_TASK):
        ret = esw_cmdq_add_task(filp, (void *)arg);
        break;

    case (ESW_CMDQ_SYNC):
        ret = esw_cmdq_sync(filp);
        break;

    case (ESW_CMDQ_QUERY):
        ret = esw_cmdq_query(filp, (void *)arg);
        break;

    default:
        dev_err(esw_memcp_dev, "invalid cmd! cmd=0x%x, arg=0x%lx\n", cmd, arg);
        ret = -EINVAL;
        break;
    }

    return ret;
}

static ssize_t esw_memcp_write(struct file *file, const char __user *data, size_t len, loff_t *ppos)
{
    dev_info(esw_memcp_dev, "Write: operation not supported\n");
    return -EINVAL;
}

static int esw_memcp_open(struct inode *inode, struct file *filp)
{
    struct cmdq *q;

    q = kzalloc(sizeof(struct cmdq), GFP_KERNEL);
    if (!q) {
        pr_err("Failed to allocate cmdq\n");
        return -ENOMEM;
    }

    q->wq = alloc_workqueue("cmdq_wq", WQ_UNBOUND | WQ_FREEZABLE | WQ_MEM_RECLAIM, 0);
    if (!q->wq) {
        pr_err("Failed to allocate workqueue\n");
        kfree(q);
        return -ENOMEM;
    }

    atomic_set(&q->total_tasks, 0);
    atomic_set(&q->completed_tasks, 0);

    filp->private_data = q;

    return 0;
}



static int esw_memcp_release(struct inode *inode, struct file *filp)
{
    struct cmdq *q = (struct cmdq *)filp->private_data;

    if (q) {
        if (q->wq) {
            destroy_workqueue(q->wq);
        }

        kfree(q);
    }

    return 0;
}


static struct file_operations esw_memcp_fops = {
    .owner          = THIS_MODULE,
    .llseek         = no_llseek,
    .write          = esw_memcp_write,
    .unlocked_ioctl = esw_memcp_ioctl,
    .open           = esw_memcp_open,
    .release        = esw_memcp_release,
};

static struct miscdevice esw_memcp_miscdev = {
    .minor         = MISC_DYNAMIC_MINOR,
    .name          = DRIVER_NAME,
    .mode          = 0666,
    .fops          = &esw_memcp_fops,
};

static int __init esw_memcp_init(void)
{
    int ret = 0;

    ret = misc_register(&esw_memcp_miscdev);
    if (ret) {
        pr_err("%s: Failed to register misc device, err=%d\n", __func__, ret);
        return ret;
    }

    esw_memcp_dev = esw_memcp_miscdev.this_device;

    if (!esw_memcp_dev->dma_mask) {
        esw_memcp_dev->dma_mask = &esw_memcp_dev->coherent_dma_mask;
    }
    ret = dma_set_mask_and_coherent(esw_memcp_dev, DMA_BIT_MASK(64));
    if (ret)
        dev_err(esw_memcp_dev, "Failed to set coherent mask.\n");
    return ret;
}

static void __exit esw_memcp_exit(void)
{
    misc_deregister(&esw_memcp_miscdev);
}
module_init(esw_memcp_init);
module_exit(esw_memcp_exit);

MODULE_AUTHOR("Geng Zonglin <gengzonglin@eswincomputing.com>");
MODULE_AUTHOR("Yuyang Cong <congyuyang@eswincomputing.com>");
MODULE_DESCRIPTION("ESW DMA MEMCP Driver");
MODULE_LICENSE("GPL v2");