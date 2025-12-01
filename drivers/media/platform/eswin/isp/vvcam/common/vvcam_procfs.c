#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include "vvcam_procfs.h"

static DEFINE_MUTEX(vsi_proc_mutex);
static struct proc_dir_entry *vsi_dir = NULL;

int vvcam_procfs_register(struct device *dev)
{
	mutex_lock(&vsi_proc_mutex);
	if(vsi_dir) {
		mutex_unlock(&vsi_proc_mutex);
		return 0;
	} else {
		vsi_dir = proc_mkdir_data("vsi", 0555, NULL, NULL);
		if(!vsi_dir) {
			dev_err(dev, "VVCAM Failed to create /proc/vsi directory \n");
			mutex_unlock(&vsi_proc_mutex);
			return -EFAULT;
		}
	}
	mutex_unlock(&vsi_proc_mutex);
	return 0;
}
EXPORT_SYMBOL(vvcam_procfs_register);