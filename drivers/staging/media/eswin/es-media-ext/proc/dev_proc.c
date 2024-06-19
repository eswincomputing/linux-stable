#include "dev_proc.h"
#include <linux/version.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/ktime.h>
#include "include/es_media_ext_drv.h"
#include "dev_module.h"
#include "class_proc_data.h"
#include "class_module.h"

typedef struct {
    struct proc_dir_entry *proc_file;  // should be freed
    class_proc_data_t *proc_inst;      // should be freed
    class_module_t *module_inst;       // malloc by other
    int minor;
} proc_t;
static proc_t dev_proc[MAX_PROC];

typedef struct {
    proc_t *proc;
    unsigned int ref;
} proc_private_t;

static int token_init = 0;
static atomic_t proc_token;  // both event and data should have the same token
static atomic_t proc_ref;    // how many proc inst exist

static int dev_proc_show(struct seq_file *m, void *v);
static int dev_proc_open(struct inode *inode, struct file *file);
static ssize_t dev_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos);
ssize_t dev_proc_read(struct file *file, char __user *buf, size_t len, loff_t *off);

struct proc_dir_entry *umap_proc_dir = NULL;

static int dev_proc_show(struct seq_file *m, void *v) {
    return 0;
}

static int dev_proc_open(struct inode *inode, struct file *file) {
    proc_private_t *private = NULL;
    int i = 0;
    char proc_name[][8] = {DEC_PROC_NAME, ENC_PROC_NAME, BMS_PROC_NAME, VPS_PROC_NAME, VO_PROC_NAME};
    for (; i < MAX_PROC; i++) {
        if (0 == strcmp(file->f_path.dentry->d_iname, proc_name[i])) {
            break;
        }
    }
    if (i >= MAX_PROC) {
        pr_warn("%s: invalid name %s\n", __func__, file->f_path.dentry->d_iname);
        return -1;
    }

    private = vzalloc(sizeof(proc_private_t));
    if (!private) {
        pr_err("%s: vzalloc %ld failed\n", __func__, sizeof(proc_private_t));
        return -ENOMEM;
    }
    private->proc = &dev_proc[i];
    private->ref = atomic_add_return(1, &proc_ref);

    return single_open(file, dev_proc_show, private);
}

static ssize_t dev_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *f_pos) {
    return 0;
}

ssize_t dev_proc_read(struct file *file, char __user *buf, size_t len, loff_t *off) {
    ktime_t cur;
    es_module_event_t event;
    class_proc_data_t *proc_inst = NULL;
    proc_t *proc = NULL;
    proc_private_t *private = NULL;
    int read = 0;
    int cnt = 0;
    unsigned int cur_ref;
    int ret;
#ifdef CMODEL_PAGE_SIZE
    int loop_read = 0;
    int loops = len / CMODEL_PAGE_SIZE + ((len % CMODEL_PAGE_SIZE) ? 1 : 0);
    int want_read = 0;
    int i = 0;
#endif

    private = (proc_private_t *)((struct seq_file *)file->private_data)->private;
    if (!private || !private->proc || !private->proc->proc_inst || !private->proc->module_inst) {
        pr_warn("<<<<< end of cat, inst is not ready yet\n");
        return 0;
    }
    proc = private->proc;
    proc_inst = proc->proc_inst;

    // 1.get token and ref
    cur_ref = atomic_read(&proc_ref);
    pr_debug(">>>>> received cat cmd, len=%zu, pos: %lld, off: %lld, minor: %d, ref: %u, cur_ref: %u\n",
            len,
            file->f_pos,
            *off,
            proc->minor,
            private->ref,
            cur_ref);

    // 4. next read
    if (*off != 0) {
        read = proc_inst->read(proc_inst, buf, len, off);
        goto end_read;
    }

    // 2. send event
    if (private->ref == 1 || proc_inst->is_timeout(proc_inst)) {
        proc_inst->reset(proc_inst);
        cnt = proc->module_inst->get_count(proc->module_inst);
        proc_inst->set_module_count(proc_inst, cnt);
        proc->module_inst->remove_event(proc->module_inst, MODULE_PROC);
        if (cnt == 0) {
            goto end_read;
        }

        event.id = MODULE_PROC;
        event.token = atomic_add_return(1, &proc_token);
        event.value = 0;
        proc->module_inst->send_event(proc->module_inst, &event);
    }

    // 3. wait for read info
    cur = ktime_get_boottime();
    ret = wait_event_interruptible_timeout(
        proc_inst->wait.wait_q,
        proc_inst->is_all_module_responsed(proc_inst),
        msecs_to_jiffies((proc_inst->wait.stamp + proc_inst->time_out - cur) / 1000000));

#ifdef CMODEL_PAGE_SIZE
    pr_debug("CMODEL_PAGE_SIZE: %d, len: %zu, loops: %d\n", CMODEL_PAGE_SIZE, len, loops);
    if (CMODEL_PAGE_SIZE < len) {
        for (i = 0; i < loops; i++) {
            want_read = (loops - 1 == i) ? ((len % CMODEL_PAGE_SIZE) ? (len % CMODEL_PAGE_SIZE) : CMODEL_PAGE_SIZE)
                                         : CMODEL_PAGE_SIZE;
            loop_read = proc_inst->read(proc_inst, buf + read, want_read, off);
            if (loop_read > 0) {
                read += loop_read;
                if (loop_read != want_read) {
                    pr_debug("loop read finish\n");
                    break;
                }
            } else {
                break;
            }
        }
    } else {
        read = proc_inst->read(proc_inst, buf, len, off);
    }
#else
    read = proc_inst->read(proc_inst, buf, len, off);
#endif

end_read:
    pr_debug("<<<<< %s[%d:%s], modules=%d, read: %d, minor: %d, ref: %d\n",
            read ? "cat return" : "end of cat",
            ret,
            (ret > 1) ? "on time" : ((*off > 0) ? "on time" : "timeout"),
            proc_inst->module_count,
            read,
            proc->minor,
            private->ref);

    return read;
}

static int dev_proc_release(struct inode *ino, struct file *file) {
    proc_private_t *private = NULL;

    private = (proc_private_t *)((struct seq_file *)file->private_data)->private;
    if (private) {
        vfree(private);
        atomic_dec(&proc_ref);
    }
    return single_release(ino, file);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static struct proc_ops dec_proc_fops = {
    //.owner = THIS_MODULE,
    .proc_open = dev_proc_open,
    .proc_release = dev_proc_release,
    .proc_read = dev_proc_read,
    .proc_lseek = seq_lseek,
    .proc_write = dev_proc_write,
};
#else
static struct file_operations dec_proc_fops = {
    .owner = THIS_MODULE,
    .open = dev_proc_open,
    .release = dev_proc_release,
    .read = dev_proc_read,
    .llseek = seq_lseek,
    .write = dev_proc_write,
};
#endif

int es_media_dev_proc_init(dev_minor_t minor, void *module) {
    char *proc_name = NULL;
    int idx = dev_minor_to_proc_index(minor);
    if (idx < 0 || idx >= MAX_PROC) {
        pr_warn("dev proc init minor %d failed\n", minor);
        return -EINVAL;
    }

    if (!dev_proc[idx].proc_inst) {
        dev_proc[idx].proc_inst = init_class_proc_data();
        if (!dev_proc[idx].proc_inst) {
            return -ENOMEM;
        }
    }

    if (!umap_proc_dir) {
        umap_proc_dir = proc_mkdir(UMAP_LOG_PROC_DIR, NULL);
        if (umap_proc_dir == NULL) {
            pr_info("proc create %s failed, minor %d\n", UMAP_LOG_PROC_DIR, minor);
            goto free_mem;
        }
    }

    if (!token_init) {
        atomic_set(&proc_token, 0);
        atomic_set(&proc_ref, 0);
        token_init = 1;
        pr_debug("%s: init token, minor %d\n", __func__, minor);
    }
    proc_name = get_proc_name(idx);
    dev_proc[idx].proc_file = proc_create(proc_name, 0777, umap_proc_dir, &dec_proc_fops);
    if (!dev_proc[idx].proc_file) {
        pr_info("proc_create %s failed! minor %d\n", proc_name, minor);
        goto free_mem;
    }
    dev_proc[idx].module_inst = (class_module_t *)module;
    dev_proc[idx].minor = minor;

    pr_info("%s: %s succeed, minor %d, index %d, proc %px\n", __func__, proc_name, minor, idx, dev_proc[idx].proc_inst);
    return 0;

free_mem:
    if (dev_proc[idx].proc_inst) {
        deinit_class_proc_data(dev_proc[idx].proc_inst);
        dev_proc[idx].proc_inst = NULL;
    }
    return -EINVAL;
}

void dev_proc_deinit(void) {
    int i = 0;
    pr_info("dev proc - start deinit\n");

    for (; i < MAX_PROC; i++) {
        if (dev_proc[i].proc_inst) {
            deinit_class_proc_data(dev_proc[i].proc_inst);
            dev_proc[i].proc_inst = NULL;
        }
        dev_proc[i].module_inst = NULL;  // come from dev_module, donot free it here.
        if (dev_proc[i].proc_file) {
            proc_remove(dev_proc[i].proc_file);
            dev_proc[i].proc_file = NULL;
        }
    }

    if (umap_proc_dir) {
        remove_proc_entry(UMAP_LOG_PROC_DIR, NULL);
        umap_proc_dir = NULL;
    }
    pr_info("dev proc - deinit succeed\n");
}

static long get_proc_inst(int minor, class_proc_data_t **proc_inst, unsigned int *token) {
    int idx;
    RETURN_VAL_IF_FAIL(proc_inst, -EINVAL);
    RETURN_VAL_IF_FAIL(token, -EINVAL);

    idx = dev_minor_to_proc_index(minor);
    if (idx < 0 || idx >= MAX_PROC || !dev_proc[idx].proc_inst) {
        pr_warn("%s failed, minor %d\n", __func__, minor);
        return -ENOTTY;
    }

    *token = atomic_read(&proc_token);
    *proc_inst = dev_proc[idx].proc_inst;
    return 0;
}

long dev_proc_recv_grp_data(unsigned long arg, int minor) {
    long ret = 0;
    class_proc_data_t *proc_inst = NULL;
    unsigned int token;
    RETURN_VAL_IF_FAIL(arg, -EINVAL);

    ret = get_proc_inst(minor, &proc_inst, &token);
    if (ret) {
        return ret;
    }

    ret = proc_inst->write_data(proc_inst, (char __user *)arg, token);
    if (proc_inst->is_all_module_responsed(proc_inst)) {
        wake_up_interruptible(&proc_inst->wait.wait_q);
    }
    return ret;
}

long dev_proc_recv_grp_title(unsigned long arg, int minor) {
    long ret = 0;
    class_proc_data_t *proc_inst = NULL;
    unsigned int token;
    RETURN_VAL_IF_FAIL(arg, -EINVAL);

    ret = get_proc_inst(minor, &proc_inst, &token);
    if (ret) {
        return ret;
    }
    ret = proc_inst->write_title(proc_inst, (char __user *)arg, token);
    return ret;
}

long dev_proc_recv_module(unsigned long arg, int minor) {
    long ret = 0;
    class_proc_data_t *proc_inst = NULL;
    unsigned int token;
    RETURN_VAL_IF_FAIL(arg, -EINVAL);

    ret = get_proc_inst(minor, &proc_inst, &token);
    if (ret) {
        return ret;
    }
    proc_inst->write_module(proc_inst, (char __user *)arg, token);
    return 0;
}

long dev_proc_recv_section(unsigned long arg, int minor) {
    long ret = 0;
    class_proc_data_t *proc_inst = NULL;
    unsigned int token;
    RETURN_VAL_IF_FAIL(arg, -EINVAL);

    ret = get_proc_inst(minor, &proc_inst, &token);
    if (ret) {
        return ret;
    }
    proc_inst->set_section(proc_inst, (char __user *)arg, token);
    return 0;
}

long dev_proc_set_timeout(unsigned long arg, int minor) {
    long ret = 0;
    class_proc_data_t *proc_inst = NULL;
    unsigned int token;
    unsigned int timeout = PROC_INTERVAL_NS;
    RETURN_VAL_IF_FAIL(arg, -EINVAL);

    ret = get_proc_inst(minor, &proc_inst, &token);
    if (ret) {
        return ret;
    }

    __get_user(timeout, (__u32 __user *)arg);
    pr_debug("dev proc: minor %d, change timeout, %llu --> %u (ms)\n", minor, proc_inst->time_out / 1000000L, timeout);
    proc_inst->time_out = 1000000L * timeout;

    return 0;
}
