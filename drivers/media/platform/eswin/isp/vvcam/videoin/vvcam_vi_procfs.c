/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2024 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2024 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/ctype.h>
#include "vvcam_vi_driver.h"
#include "vvcam_vi_procfs.h"
#include "vvcam_vi_hal.h"

#define VVCAM_VI_PROCFS_BUF_SIZE    256

#define PROC_OUT_OF_RANGE       0x01
#define PROC_PARSE_ERROR        0x02

#define PROC_PRINT_STAT(stat, sfile, name) \
    seq_printf(sfile, "|%-32s|%-16d|\n", #name, stat.name); \
    seq_printf(sfile, "|--------------------------------|----------------|\n");


enum {
    PROC_CMD_RW,
    PROC_CMD_CLEAR
};

struct rw_cmd {
    bool is_write;
    int8_t flags;
    uint32_t start_addr;
    uint8_t num_elems;
    uint32_t *rw_buffer;
    struct list_head list;
};

struct vvcam_vi_irq_stat {
    uint32_t isp_frame_in_cnt;
    uint32_t isp_frame_out_cnt;

    uint32_t mp_frame_out_cnt;
    uint32_t sp_frame_out_cnt;
    uint32_t sp2_frame_out_cnt;

    uint32_t mcm_raw0_frame_cnt;
    uint32_t mcm_raw1_frame_cnt;
    uint32_t mcm_g2_raw0_frame_cnt;
    uint32_t mcm_g2_raw1_frame_cnt;
    uint32_t mcm_rdma_frame_cnt;

    uint32_t fe_irq_cnt;

    unsigned long long start;
    unsigned long long end;
};

struct vvcam_vi_status {
    int8_t flags;
    int8_t type;
    struct vvcam_vi_irq_stat  stat;
    struct list_head cmd_list;
};

struct vvcam_vi_procfs {
    struct proc_dir_entry *pde;
    struct vvcam_vi_dev *vi_dev;
    struct mutex lock;
    struct vvcam_vi_status status;
};

void vvcam_vi_proc_stat(unsigned long pde,
                    const uint32_t *irq_mis, const int len)
{
    struct vvcam_vi_procfs *vi_proc = (struct vvcam_vi_procfs*)pde;
    struct vvcam_vi_irq_stat *stat = &vi_proc->status.stat;
    int eid = 0;

    //mutex_lock(&vi_proc->lock);
    for (eid = 0; eid < len; eid++) {
        if (!irq_mis[eid])
            continue;

        switch (eid)
        {
        case VVCAM_EID_ISP_MIS:
            if (irq_mis[eid] & ISP_MIS_FRAME_IN_MASK)
                stat->isp_frame_in_cnt++;

            if (irq_mis[eid] & ISP_MIS_FRAME_OUT_MASK)
                stat->isp_frame_out_cnt++;

            break;

        case VVCAM_EID_MIV2_MIS:
            /* reserve */
            break;

        case VVCAM_EID_MIV2_MIS1:
            /* reserve */
            break;

        case VVCAM_EID_MIV2_MIS2:
            /* reserve */
            break;

        case VVCAM_EID_MIV2_MIS3:
            /* reserve */
            break;

        case VVCAM_EID_RDMA_MIS:
            if (irq_mis[eid] & MIV2_MIS_MCM_RAW_RADY_MASK)
                stat->mcm_rdma_frame_cnt++;

            if (irq_mis[eid] & MIV2_MIS_MP_FRAME_END_MASK)
                stat->mp_frame_out_cnt++;

            if (irq_mis[eid] & MIV2_MIS_SP_FRAME_END_MASK)
                stat->sp_frame_out_cnt++;

            if (irq_mis[eid] & MIV2_MIS_SP2_FRAME_END_MASK)
                stat->sp2_frame_out_cnt++;

            break;

        case VVCAM_EID_MCM_WR_RAW0_MIS:
            stat->mcm_raw0_frame_cnt++;
            break;

        case VVCAM_EID_MCM_WR_RAW1_MIS:
            stat->mcm_raw1_frame_cnt++;
            break;

        case VVCAM_EID_MCM_WR_G2_RAW0_MIS:
            stat->mcm_g2_raw0_frame_cnt++;
            break;

        case VVCAM_EID_MCM_WR_G2_RAW1_MIS:
            stat->mcm_g2_raw1_frame_cnt++;
            break;

        case VVCAM_EID_FE_MIS:
            stat->fe_irq_cnt++;
            break;
        }
    }
    //mutex_unlock(&vi_proc->lock);
}


static void release_cmd_list(struct list_head *cmd_list)
{
    struct rw_cmd *cmd = NULL, *old = NULL;

    if (!list_empty( cmd_list)) {
        list_for_each_entry(cmd, cmd_list, list) {
            if (old) {
                list_del(&old->list);
                kfree(old);
                old = NULL;
            }
            if (cmd->rw_buffer)
                kfree(cmd->rw_buffer);
            old = cmd;
        }

        if (old) {
            list_del(&old->list);
            kfree(old);
            old = NULL;
        }
    }
}

static int vvcam_vi_procfs_info_show(struct seq_file *sfile, void *offset)
{
	struct vvcam_vi_procfs *vi_proc;
    struct vvcam_vi_dev *vi_dev;
    struct vvcam_vi_status *status;
    struct rw_cmd *cmd = NULL;
    uint8_t i = 0;

	vi_proc = (struct vvcam_vi_procfs *) sfile->private;

    mutex_lock(&vi_proc->lock);
    vi_dev = vi_proc->vi_dev;
    status = &vi_proc->status;

    if (status && (status->flags & PROC_PARSE_ERROR)) {
        seq_printf(sfile, "please check input!\n");
        seq_printf(sfile,
        "Usage: echo [r|w][num_of_32bit_value]@[addr] [values]>[proc path]\n");
        seq_printf(sfile,
        "eg: echo w2@0x00005400 0x00412745 0x80252941 r2>/proc/vsi/vsiisp0 && cat /proc/vsi/vsiisp0\n");
        status->flags = 0;
        mutex_unlock(&vi_proc->lock);
        return 0;
    }

    if (status && !list_empty(&status->cmd_list)) {
        list_for_each_entry(cmd, &status->cmd_list, list) {
            if (cmd->is_write) {
                if (cmd->flags & PROC_OUT_OF_RANGE) {
                    seq_printf(sfile, "write addr %08x ~ %08x out of range.\n",
                    cmd->start_addr, cmd->start_addr +
                    (uint32_t)sizeof(cmd->start_addr) * (cmd->num_elems - 1));
                } else {
                    seq_printf(sfile, "write addr %08x ~ %08x successed.\n",
                    cmd->start_addr, cmd->start_addr +
                    (uint32_t)sizeof(cmd->start_addr) * (cmd->num_elems - 1));
                }
            } else {
                if (cmd->flags & PROC_OUT_OF_RANGE) {
                    seq_printf(sfile, "read addr %08x ~ %08x out of range.\n",
                    cmd->start_addr, cmd->start_addr +
                    (uint32_t)sizeof(cmd->start_addr) * (cmd->num_elems - 1));
                } else {
                    seq_printf(sfile, "read addr %08x ~ %08x successed:\n",
                    cmd->start_addr, cmd->start_addr +
                    (uint32_t)sizeof(cmd->start_addr) * (cmd->num_elems - 1));
                    for (i = 0; i < cmd->num_elems; i++) {
                        seq_printf(sfile, "addr:0x%08x value:0x%08x\n",
                        cmd->start_addr +
                        (uint32_t)sizeof(cmd->start_addr) * i,
                        cmd->rw_buffer[i]);
                    }
                }
            }
            cmd->flags = 0;
        }
        release_cmd_list(&status->cmd_list);
        status->flags = 0;
    } else {
        status->stat.end = ktime_get_ns();
        seq_printf(sfile,
        "/***statistic for %s time(ns):%lld)***/\n",
        vi_dev->miscdev.name, status->stat.end - status->stat.start);

    seq_printf(sfile, "|--------------------------------|----------------|\n");
        seq_printf(sfile, "|%-32s|%-16s|\n",
        "              Name", "     Number"); \
    seq_printf(sfile, "|--------------------------------|----------------|\n");
        PROC_PRINT_STAT(status->stat, sfile, isp_frame_in_cnt);
        PROC_PRINT_STAT(status->stat, sfile, isp_frame_out_cnt);
        PROC_PRINT_STAT(status->stat, sfile, mp_frame_out_cnt);
        PROC_PRINT_STAT(status->stat, sfile, sp_frame_out_cnt);
        PROC_PRINT_STAT(status->stat, sfile, sp2_frame_out_cnt);
        PROC_PRINT_STAT(status->stat, sfile, mcm_raw0_frame_cnt);
        PROC_PRINT_STAT(status->stat, sfile, mcm_raw1_frame_cnt);
        PROC_PRINT_STAT(status->stat, sfile, mcm_g2_raw0_frame_cnt);
        PROC_PRINT_STAT(status->stat, sfile, mcm_g2_raw1_frame_cnt);
        PROC_PRINT_STAT(status->stat, sfile, mcm_rdma_frame_cnt);
        PROC_PRINT_STAT(status->stat, sfile, fe_irq_cnt);
    }
    mutex_unlock(&vi_proc->lock);

	return 0;
}

static int vvcam_vi_procfs_open(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
	return single_open(file, vvcam_vi_procfs_info_show, PDE_DATA(inode));
#else
	return single_open(file, vvcam_vi_procfs_info_show, pde_data(inode));
#endif
}

static char *vvcam_vi_next_arg(char *str_buf, char **param)
{
    int i = 0;
    for (; str_buf[i]; i++) {
        if (isspace(str_buf[i]))
            break;
    }
    *param = str_buf;

    if (str_buf[i]) {
        str_buf[i] = '\0';
        str_buf += i + 1;
    } else
        str_buf += i;

    return skip_spaces(str_buf);
}

static int vvcam_vi_proc_parse_args(char *str_buf,
                struct vvcam_vi_status *status)
{
    char *param, *end = NULL;
    int32_t address = -1;
    uint8_t read_write = 0;
    uint8_t num_elems = 0;
    int8_t index= 0;
    struct rw_cmd *cmd = NULL;

    if ((strlen(str_buf) == 1) || (strcmp(str_buf, "clear\n") == 0)) {
        status->type = PROC_CMD_CLEAR;
        return 0;
    }

    status->type = PROC_CMD_RW;
    while (*str_buf) {
        param = NULL;
        str_buf = vvcam_vi_next_arg(str_buf, &param);
        if ((*param == 'r') || (*param == 'w')) {
            if (index != 0)
                goto error_vi_proc_cmd;

            read_write = *param;
            num_elems = (uint8_t)simple_strtoul((param + 1), &end, 0);
            if (num_elems != 0)
                param = end;
            else
                num_elems = 1;

            if (*end == '@') {
                address = (uint32_t)simple_strtoul(param + 1, &end, 0);
                if (((end - param - 1) > 10) || ((end - param - 1) <= 6)) {
                    goto error_vi_proc_cmd;
                }
            }

            if (*end != '\0')
                goto error_vi_proc_cmd;

            if (address < 0) {
                goto error_vi_proc_cmd;
            }

            cmd = kzalloc(sizeof(struct rw_cmd), GFP_KERNEL);
            if (!cmd)
                goto error_vi_proc_cmd;

            cmd->is_write = (read_write == 'w' ? true : false);
            cmd->start_addr = address;
            cmd->num_elems = num_elems;
            cmd->rw_buffer =
                kzalloc(sizeof(int32_t) * cmd->num_elems, GFP_KERNEL);
            if (!cmd->rw_buffer)
                goto error_vi_proc_cmd;

            list_add_tail(&cmd->list, &status->cmd_list);
            if (read_write == 'w')
                index = num_elems;
            continue;
        }

         if (*param != '\0' && read_write != 0) {
            if (read_write == 'r')
                goto error_vi_proc_cmd;

            if (index <= 0)
                goto error_vi_proc_cmd;

            cmd->rw_buffer[num_elems - index] =
                                (uint32_t)simple_strtoul(param, &end, 0);

            if ((*end != '\0') || ((end - param) > 10) ||
                ((end - param) <= 6))
                goto error_vi_proc_cmd;

            index--;
        }
    }
    return 0;

error_vi_proc_cmd:
    release_cmd_list(&status->cmd_list);
    return -EFAULT;
}

static int32_t vvcam_vi_proc_process(struct seq_file *sfile,
                        struct vvcam_vi_procfs *vi_proc, char *str_buf)
{
    struct vvcam_vi_status *status;
    vvcam_vi_reg_t reg;
    struct rw_cmd *cmd = NULL;
    int32_t ret = 0;
    uint8_t i = 0;

    status = &vi_proc->status;

    mutex_lock(&vi_proc->lock);
    if (!list_empty(&status->cmd_list)) {
        release_cmd_list(&status->cmd_list);
    }

    status->flags = 0;
    status->type = -1;

    ret = vvcam_vi_proc_parse_args(str_buf, status);
    if (ret) {
        status->flags = PROC_PARSE_ERROR;
        mutex_unlock(&vi_proc->lock);
        return ret;
    }

    if (status->type == PROC_CMD_CLEAR) {
        memset(&status->stat, 0, sizeof(status->stat));
        status->stat.start = ktime_get_ns();
    } else {
        mutex_lock(&vi_proc->vi_dev->mlock);
        list_for_each_entry(cmd, &status->cmd_list, list) {
            for (i = 0; i < cmd->num_elems; i++) {
                reg.addr = cmd->start_addr + sizeof(cmd->start_addr) * i;
                if (reg.addr >= vi_proc->vi_dev->regs_size) {
                    cmd->flags |= PROC_OUT_OF_RANGE;
                    continue;
                }
                if (cmd->is_write) {
                    reg.value = cmd->rw_buffer[i];
                    ret = vvcam_vi_write_reg(vi_proc->vi_dev, reg);
                } else {
                    ret = vvcam_vi_read_reg(vi_proc->vi_dev, &reg);
                    cmd->rw_buffer[i] = reg.value;
                }
            }
        }
        mutex_unlock(&vi_proc->vi_dev->mlock);
    }

    mutex_unlock(&vi_proc->lock);

    return 0;
}

static ssize_t vvcam_vi_procfs_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
    struct vvcam_vi_procfs *vi_proc;
    struct seq_file *sfile;
    char *str_buf;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
	vi_proc = (struct vvcam_vi_procfs *) PDE_DATA(file_inode(file));
#else
	vi_proc = (struct vvcam_vi_procfs *) pde_data(file_inode(file));
#endif
    sfile = file->private_data;

    if (count > VVCAM_VI_PROCFS_BUF_SIZE)
        count = VVCAM_VI_PROCFS_BUF_SIZE - 1;

    str_buf = (char *)kzalloc(count, GFP_KERNEL);
    if (!str_buf)
        return -ENOMEM;

    if (copy_from_user(str_buf, buffer, count))
        return -EFAULT;

    *(str_buf + count) = '\0';

    vvcam_vi_proc_process(sfile, vi_proc, str_buf);

    kfree(str_buf);

    return count;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations vvcam_vi_procfs_ops = {
    .open = vvcam_vi_procfs_open,
    .release = seq_release,
    .read = seq_read,
    .write = vvcam_vi_procfs_write,
    .llseek = seq_lseek,
};
#else
static const struct proc_ops vvcam_vi_procfs_ops = {
	.proc_open = vvcam_vi_procfs_open,
	.proc_release = seq_release,
	.proc_read = seq_read,
	.proc_write = vvcam_vi_procfs_write,
	.proc_lseek = seq_lseek,
};
#endif

struct finddir_callback {
    struct dir_context ctx;
    const char *name;
    bool found;
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0)
static int readdir_callback(struct dir_context *ctx, const char *name,
    int namelen, loff_t offset, u64 ino, unsigned int d_type) {
    struct finddir_callback *fc =
        container_of(ctx, struct finddir_callback, ctx);
    if (fc->found)
        return 0;

    if(strcmp(name, fc->name) == 0) {
        fc->found = true;
    }
    return 0;
}

#else
static bool readdir_callback(struct dir_context *ctx, const char *name,
        int namelen, loff_t offset, u64 ino, unsigned int d_type) {

    struct finddir_callback *fc =
        container_of(ctx, struct finddir_callback, ctx);
    if (fc->found)
        return true;
    if(strcmp(name, fc->name) == 0) {
        fc->found = true;
    }
    return true;
}
#endif


static int find_proc_dir_by_name(const char *name, bool *found) {
    struct file *pfile;
    int ret = 0;
    struct finddir_callback fc = {
        .ctx.actor = readdir_callback,
        .name = name,
        .found = false
    };

    pfile = filp_open("/proc", O_RDONLY | O_DIRECTORY, 0);
    if(IS_ERR(pfile)) {
        pr_err("Failed to open %s\n", root);
        return -1;
    }
    if (pfile->f_op->iterate_shared) {
        ret = pfile->f_op->iterate_shared(pfile, &fc.ctx);
#if LINUX_VERSION_CODE <= KERNEL_VERSION(6, 2, 0)
    } else {
        ret = pfile->f_op->iterate(pfile, &fc.ctx);
#endif
    }

    if (ret == 0) {
        *found = fc.found;
    }

    filp_close(pfile, NULL);
    return ret;
}

int vvcam_vi_procfs_register(struct vvcam_vi_dev *vi_dev, unsigned long *pde)
{
    struct vvcam_vi_procfs *vi_proc;
    char vi_proc_name[32];
    int ret = 0;
    bool found = false;

    if (!vi_dev)
        return -1;
    sprintf(vi_proc_name, "vsi/vi");

    vi_proc = devm_kzalloc(vi_dev->dev,
                        sizeof(struct vvcam_vi_procfs), GFP_KERNEL);

    if (!vi_proc)
        return -ENOMEM;

    ret = find_proc_dir_by_name("vsi", &found);
    if (ret == 0) {
        if (!found)
            proc_mkdir("vsi", NULL);
    } else {
        return -EFAULT;
    }

    vi_proc->vi_dev = vi_dev;
    vi_proc->pde = proc_create_data(vi_proc_name, 0444, NULL,
                                &vvcam_vi_procfs_ops, vi_proc);
    if (!vi_proc->pde)
        return -EFAULT;
    *pde = (unsigned long)&vi_proc->pde;

    INIT_LIST_HEAD(&vi_proc->status.cmd_list);
    mutex_init(&(vi_proc->lock));
	return 0;
}

void vvcam_vi_procfs_unregister(unsigned long pde)
{
    int ret = 0;
    bool found = false;
    ret = find_proc_dir_by_name("vsi", &found);
    if (ret == 0) {
        if (found)
            remove_proc_subtree("vsi", NULL);
    }
}
