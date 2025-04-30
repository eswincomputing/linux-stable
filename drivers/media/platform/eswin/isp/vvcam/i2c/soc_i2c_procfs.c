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
#include "soc_i2c_procfs.h"

#define GPIO_I2C_PROCFS_BUF_SIZE    128

#define PROC_RW_ERROR           0x01
#define PROC_PARSE_ERROR        0x02

struct rw_cmd {
    bool is_write;
    int8_t flags;
    uint8_t slave_addr;
    uint8_t num_elems;
    uint8_t *rw_buffer;
    struct list_head list;
};

struct gpio_i2c_status {
    int8_t flags;
    struct list_head cmd_list;
};

struct gpio_i2c_procfs {
    struct proc_dir_entry *pde;
    struct i2c_adapter *adapter;
    struct mutex lock;
    struct gpio_i2c_status *status;
};

static int32_t gpio_i2c_scan_slave(struct i2c_adapter *adapter, uint8_t addr)
{
    int ret = 0;
    char buf = 0;
    struct i2c_msg msg = {.addr = 0x07, .flags = I2C_M_RD, .buf = &buf, .len=1};

    msg.addr = addr;
    ret = i2c_transfer(adapter, &msg, 1);
    if (ret != msg.len)
        return -1;
    return 0;
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

static int gpio_i2c_procfs_info_show(struct seq_file *sfile, void *offset)
{
	struct gpio_i2c_procfs *i2c_proc;
    struct i2c_adapter *adapter;
    struct gpio_i2c_status *status;
    struct rw_cmd *cmd = NULL;
    uint32_t ret = 0;
    uint8_t base = 0;
    uint8_t shift = 0;
    uint8_t i = 0;

	i2c_proc = (struct gpio_i2c_procfs *) sfile->private;

    mutex_lock(&i2c_proc->lock);
    adapter = i2c_proc->adapter;
    status = i2c_proc->status;

    if (status && (status->flags & PROC_PARSE_ERROR)) {
        seq_printf(sfile, "please check input!\n");
        seq_printf(sfile, "Usage: echo w2@0x3c 0x30 0x0a r1>/proc/vsi/i2c8\n");
        status->flags = 0;
        mutex_unlock(&i2c_proc->lock);
        return 0;
    }

    if (status && !list_empty(&status->cmd_list)) {
        list_for_each_entry(cmd, &status->cmd_list, list) {
            if (cmd->is_write) {
                if (cmd->flags & PROC_RW_ERROR) {
                    seq_printf(sfile, "write slave 0x%02x failed.\n",
                        cmd->slave_addr);
                } else {
                    seq_printf(sfile, "write slave 0x%02x successed.\n",
                        cmd->slave_addr);
                }
            } else {
                if (cmd->flags & PROC_RW_ERROR) {
                    seq_printf(sfile, "read slave 0x%02x failed.\n",
                        cmd->slave_addr);
                } else {
                    seq_printf(sfile, "read %d bytes from slave 0x%02x:\n",
                        cmd->num_elems, cmd->slave_addr);
                    for (i = 0; i < cmd->num_elems; i++) {
                        seq_printf(sfile, "value:%02x\n", cmd->rw_buffer[i]);
                    }
                }
            }
            cmd->flags = 0;
        }
        release_cmd_list(&status->cmd_list);
        status->flags = 0;
     } else {
        seq_printf(sfile, "DeviceName:\t%s.%d\n", adapter->name, adapter->nr);
        seq_printf(sfile, "Device:\t\ti2c-%d\n", adapter->nr);
        seq_printf(sfile, "Slaves:\n");

        seq_printf(sfile, "     ");
        for (i = 0; i < 15; i++) {
            seq_printf(sfile, "%2x  ", i);
        }
        seq_printf(sfile, "%2x\n", i);

        for (base = 0x00; base < 0x80; base += 16) {
            seq_printf(sfile, "%02x:  ", base);
            for (shift = 0; shift < 16; shift++) {
                ret = gpio_i2c_scan_slave(adapter, base + shift);
                if (ret) {
                    seq_printf(sfile, "--  ");
                    continue;
                }
                seq_printf(sfile, "%02x  ", base + shift);
            }
            seq_printf(sfile, "\n");
        }
     }
     mutex_unlock(&i2c_proc->lock);

	return 0;
}


static int gpio_i2c_procfs_open(struct inode *inode, struct file *file)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
	return single_open(file, gpio_i2c_procfs_info_show, PDE_DATA(inode));
#else
	return single_open(file, gpio_i2c_procfs_info_show, pde_data(inode));
#endif
}

static char *gpio_i2c_next_arg(char *str_buf, char **param)
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

static int gpio_i2c_parse_args(char *str_buf, struct list_head *cmd_list)
{
    char *param;
    unsigned char address = 0x00, read_write = 0;
    struct rw_cmd *cmd;
    uint8_t  index = 0, num_elems = 0;
    char *end;

    while (*str_buf) {
        param = NULL;
        str_buf = gpio_i2c_next_arg(str_buf, &param);
        if ((*param == 'r') || (*param == 'w')) {

            if (index != 0)
                goto error_i2c_msgs;

            read_write = *param;
            num_elems = (uint8_t)simple_strtoul((param+1), &end, 0);
            if (num_elems != 0) {
                param = end;
                if (*end == '@')
                    address = (unsigned char)simple_strtoul(param + 1, &end, 0);
            } else {
                num_elems = 1;
            }

            if (*end == '@') {
                address = (uint8_t)simple_strtoul(param + 1, &end, 0);
                if (((end - param - 1) > 4) || ((end - param - 1) <= 3)) {
                    goto error_i2c_msgs;
                }
            }

            if (*end != '\0')
                goto error_i2c_msgs;

            if ((address == 0x00) || (num_elems == 0) || (address > 0x7F))
                goto error_i2c_msgs;

            cmd = kzalloc(sizeof(struct rw_cmd), GFP_KERNEL);
            if (!cmd)
                return -ENOMEM;

            cmd->is_write = (read_write == 'w' ? true : false);
            cmd->slave_addr = address;
            cmd->num_elems = num_elems;
            cmd->rw_buffer =
                kzalloc(sizeof(int32_t) * cmd->num_elems, GFP_KERNEL);
            if (!cmd->rw_buffer)
                goto error_i2c_msgs;

            list_add_tail(&cmd->list, cmd_list);
            if (read_write == 'w')
                index = num_elems;

            continue;
        }

        if (*param != '\0' && read_write != 0) {
            if (read_write == 'r')
                goto error_i2c_msgs;

            if (index == 0)
                goto error_i2c_msgs;

            cmd->rw_buffer[num_elems - index] =
                                (uint8_t)simple_strtoul(param, &end, 0);
            
            if ((*end != '\0') || ((end - param) > 4) ||
                ((end - param) <= 3))
                goto error_i2c_msgs;
            index--;
        }
    }
    return 0;

error_i2c_msgs:
    release_cmd_list(cmd_list);
    return -EFAULT;;
}

static int32_t gpio_i2c_procfs_process(struct seq_file *sfile,
                        struct gpio_i2c_procfs *i2c_proc, char *str_buf)
{
    struct i2c_msg msg;
    struct gpio_i2c_status *status;
    struct rw_cmd *cmd = NULL;
    int32_t ret = 0;

    mutex_lock(&i2c_proc->lock);
    if (!i2c_proc->status) {
        i2c_proc->status = devm_kzalloc(i2c_proc->adapter->dev.parent,
                            sizeof(struct gpio_i2c_status), GFP_KERNEL);
        if (!i2c_proc->status) {
            mutex_unlock(&i2c_proc->lock);
            return -ENOMEM;
        }
    } else {
        release_cmd_list(&i2c_proc->status->cmd_list);
    }

    status = i2c_proc->status;
    INIT_LIST_HEAD(&status->cmd_list);

    ret = gpio_i2c_parse_args(str_buf, &status->cmd_list);
    if (ret) {
        status->flags = PROC_PARSE_ERROR;
        mutex_unlock(&i2c_proc->lock);
        return ret;
    }

    list_for_each_entry(cmd, &status->cmd_list, list) {
        msg.addr = cmd->slave_addr;
        msg.len = cmd->num_elems;
        msg.buf = cmd->rw_buffer;
        if (cmd->is_write) {
            msg.flags |= 0; 
        } else {
            msg.flags |= I2C_M_RD;
        }

        ret = i2c_transfer(i2c_proc->adapter, &msg, 1);
        if (ret != 1) {
            cmd->flags = PROC_RW_ERROR;
        }
    }
    mutex_unlock(&i2c_proc->lock);
    return 0;
}

static ssize_t gpio_i2c_procfs_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
    struct gpio_i2c_procfs *i2c_proc;
    struct seq_file *sfile;
    char *str_buf;

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 17, 0)
	i2c_proc = (struct gpio_i2c_procfs *) PDE_DATA(file_inode(file));
#else
	i2c_proc = (struct gpio_i2c_procfs *) pde_data(file_inode(file));
#endif
    sfile = file->private_data;

    if (count > GPIO_I2C_PROCFS_BUF_SIZE)
        count = GPIO_I2C_PROCFS_BUF_SIZE - 1;

    str_buf = (char *)kzalloc(count, GFP_KERNEL);
    if (!str_buf)
        return -ENOMEM;
    
    if (copy_from_user(str_buf, buffer, count))
        return -EFAULT;

    *(str_buf + count) = '\0';

    gpio_i2c_procfs_process(sfile, i2c_proc, str_buf);

    kfree(str_buf);

    return count;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
static const struct file_operations gpio_i2c_procfs_ops = {
    .open = gpio_i2c_procfs_open,
    .release = seq_release,
    .read = seq_read,
    .write = gpio_i2c_procfs_write,
    .llseek = seq_lseek,
};
#else
static const struct proc_ops gpio_i2c_procfs_ops = {
	.proc_open = gpio_i2c_procfs_open,
	.proc_release = seq_release,
	.proc_read = seq_read,
	.proc_write = gpio_i2c_procfs_write,
	.proc_lseek = seq_lseek,
};
#endif

struct finddir_callback {
    struct dir_context ctx;
    const char *name;
    int32_t files_cnt;
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
    fc->files_cnt++;
    return 0;
}

#else
static bool readdir_callback(struct dir_context *ctx, const char *name,
        int namelen, loff_t offset, u64 ino, unsigned int d_type) {

    struct finddir_callback *fc = container_of(ctx, struct finddir_callback, ctx);
    if (fc->found)
        return true;
    if(strcmp(name, fc->name) == 0) {
        fc->found = true;
    }
    fc->files_cnt++;
    return true;
}
#endif


static int find_proc_dir_by_name(const char *root,
                const char *name, bool *found, int32_t *files_cnt) {
    struct file *pfile;
    int ret = 0;
    struct finddir_callback fc = {
        .ctx.actor = readdir_callback,
        .name = name,
        .found = false,
        .files_cnt = -2,
    };

    pfile = filp_open(root, O_RDONLY | O_DIRECTORY, 0);
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

    if (files_cnt != NULL) {
        *files_cnt = fc.files_cnt;
    }

    filp_close(pfile, NULL);
    return ret;
}

int soc_gpio_i2c_procfs_register(struct i2c_adapter *adapter,
                                    struct proc_dir_entry **pde)
{
    struct gpio_i2c_procfs *i2c_proc;
    char i2c_proc_name[32];
    int ret = 0;
    bool found = false;

    if (!adapter)
        return -1;
    sprintf(i2c_proc_name, "vsi/i2c%d", adapter->nr);
    i2c_proc = devm_kzalloc(adapter->dev.parent,
                        sizeof(struct gpio_i2c_procfs), GFP_KERNEL);
    if (!i2c_proc)
        return -ENOMEM;

    ret = find_proc_dir_by_name("/proc", "vsi", &found, NULL);
    if (ret == 0) {
        if (!found)
            proc_mkdir("vsi", NULL);    
    } else {
        return -EFAULT;
    }

    i2c_proc->adapter = adapter;
    *pde = proc_create_data(i2c_proc_name, 0664, NULL,
                                &gpio_i2c_procfs_ops, i2c_proc);
    if (!*pde)
        return -EFAULT;

    i2c_proc->pde = *pde;
    mutex_init(&(i2c_proc->lock));

	return 0;
}

void soc_gpio_i2c_procfs_unregister(struct proc_dir_entry **pde)
{
    int ret = 0;
    bool found = false;
    int32_t files_cnt;

    ret = find_proc_dir_by_name("/proc", "vsi", &found, NULL);
    if (ret == 0) {
        if (found) {
            proc_remove(*pde);
            ret = find_proc_dir_by_name("/proc/vsi", "", &found, &files_cnt);
            if (files_cnt == 0) {
                remove_proc_subtree("vsi", NULL);
            }
        }
    }
}
