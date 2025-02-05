// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN OTP debugfs Driver
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
 * Authors: HuangYiFeng<huangyifeng@eswincomputing.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "otp_eic770x.h"

#define OTP_DEBUGFS_DIR		"eswin_otp"
#define FIELD_NAME_MAX_LEN	  32

static struct dentry *otp_debugfs_dir;
static char user_field_name[FIELD_NAME_MAX_LEN] = {0};

/* Function to read a specific OTP bit */
static int eswin_otp_read_bit(int bit_addr, uint8_t *bit_data)
{
	int byte_addr;
	unsigned int mask;
	uint8_t byte_data;
	int ret;

	byte_addr = bit_addr / 8;
	mask = 1 << (bit_addr % 8);

	ret = otp_read_data(&byte_data, byte_addr, 1, 0);
	if (ret != 0) {
		pr_err("Failed to read OTP bit at addr %d\n", byte_addr);
		return ret;
	}

	*bit_data = (byte_data & mask) >> (bit_addr % 8);
	return 0;
}

/* Function to read a specific OTP field */
static void eswin_otp_read_field(struct seq_file *m, const char *field_name, uint8_t *buffer, int buf_size)
{
	int field;
	const struct OTP_MAP_ITEM *map = NULL;
	int ret;
	int i;

	if (0 == field_name[0]) {
		seq_printf(m, "please echo <field name> to /sys/kernel/debug/eswin_otp/read_field first, legal field name include:\n");
		for (i = OTP_RSA_PUBLIC_KEY; i <= OTP_DFT_INFORMATION; i++) {
			seq_printf(m, "%-12s%-52s%-s","","", otp_map_eic770x[i].name);
			if (otp_map_eic770x[i].enxor) {
				seq_printf(m, "(4bit XOR)");
			}
			seq_printf(m, "\n");
		}
		return;
	}
	for (field = OTP_RSA_PUBLIC_KEY; field <= OTP_DFT_INFORMATION; field++) {
		map = &otp_map_eic770x[field];
		if (!strcmp(field_name, map->name)) {
			break;
		}
	}
	if (field > OTP_DFT_INFORMATION) {
		seq_printf(m, "field name %s is illegal, legal field name include:\n", field_name);
		for (i = OTP_RSA_PUBLIC_KEY; i <= OTP_DFT_INFORMATION; i++) {
			seq_printf(m, "%-12s%-52s%-s","","", otp_map_eic770x[i].name);
			if (otp_map_eic770x[i].enxor) {
				seq_printf(m, "(4bit XOR)");
			}
			seq_printf(m, "\n");
		}
		return;
	}

	if (buf_size < map->otp_size) {
		pr_err("Buffer too small for OTP field: %s\n", field_name);
		return;
	}

	memset(buffer, 0, map->otp_size);
	ret = get_otp_value(buffer, field, map->otp_size, map->enxor);
	if (ret) {
		pr_err("Failed to read OTP value, ret %d\n", ret);
		return;
	}

	seq_printf(m, "Field: %s\n", field_name);
	for (i = 0; i < map->otp_size; i++) {
		seq_printf(m, "  0x%02X ", buffer[i]);
		if ((i + 1) % 16 == 0)
			seq_putc(m, '\n');
	}
	seq_putc(m, '\n');
	return;
}

/* seq_file show function for reading OTP field */
static int otp_read_field_show(struct seq_file *m, void *v)
{
	uint8_t *buffer;
	const int buffer_size = 256;

	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer) {
		seq_puts(m, "Memory allocation failed\n");
		return -ENOMEM;
	}
	eswin_otp_read_field(m, user_field_name, buffer, buffer_size);
	kfree(buffer);
	memset(user_field_name, 0, FIELD_NAME_MAX_LEN);
	return 0;
}

static int otp_read_field_open(struct inode *inode, struct file *file)
{
	return single_open(file, otp_read_field_show, NULL);
}

/* Allow users to write the field name they want to read */
static ssize_t otp_read_field_write(struct file *file, const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	if (count >= FIELD_NAME_MAX_LEN)
		return -EINVAL;

	if (copy_from_user(user_field_name, user_buf, count))
		return -EFAULT;

	user_field_name[count - 1] = '\0';	/* Ensure null termination */
	return count;
}

static const struct file_operations otp_read_field_fops = {
	.open		= otp_read_field_open,
	.read		= seq_read,
	.write		= otp_read_field_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int bit_addr = -1;

/* seq_file show function for reading OTP bit */
static int otp_read_bit_show(struct seq_file *m, void *v)
{
	uint8_t bit_data;

	if (bit_addr == -1) {
		seq_puts(m, "Please echo <bit offset> to /sys/kernel/debug/eswin_otp/read_bit first!\n");
	} else {
		if (eswin_otp_read_bit(bit_addr, &bit_data) < 0)
			return -EFAULT;

		seq_printf(m, "OTP Bit [%d]: %d\n", bit_addr, bit_data);
		bit_addr = -1;
	}

	return 0;
}

/* Open function for seq_file */
static int otp_read_bit_open(struct inode *inode, struct file *file)
{
	return single_open(file, otp_read_bit_show, NULL);
}

/* Write function to allow users to write a bit address */
static ssize_t otp_read_bit_write(struct file *file, const char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	char buf[32];

	if (copy_from_user(buf, user_buf, min(count, sizeof(buf) - 1)))
		return -EFAULT;

	buf[min(count, sizeof(buf) - 1)] = '\0';
	if (kstrtoint(buf, 10, &bit_addr) < 0)
		return -EINVAL;

	return count;
}

/* File operations using seq_file */
static const struct file_operations otp_read_bit_fops = {
	.owner   = THIS_MODULE,
	.open    = otp_read_bit_open,
	.read    = seq_read,
	.write   = otp_read_bit_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Initialize debugfs */
static int __init otp_debugfs_init(void)
{
	otp_debugfs_dir = debugfs_create_dir(OTP_DEBUGFS_DIR, NULL);
	if (!otp_debugfs_dir) {
		pr_err("Failed to create OTP debugfs directory\n");
		return -ENOMEM;
	}

	debugfs_create_file("read_field", 0666, otp_debugfs_dir, NULL, &otp_read_field_fops);
	debugfs_create_file("read_bit", 0666, otp_debugfs_dir, NULL, &otp_read_bit_fops);

	pr_info("ESWIN OTP debugfs initialized\n");
	return 0;
}

/* Cleanup debugfs */
static void __exit otp_debugfs_exit(void)
{
	debugfs_remove_recursive(otp_debugfs_dir);
	pr_info("ESWIN OTP debugfs removed\n");
}

module_init(otp_debugfs_init);
module_exit(otp_debugfs_exit);

MODULE_AUTHOR("huangyifeng<huangyifeng@eswincomputing.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("ESWIN OTP Linux DebugFS Driver");
MODULE_VERSION("1.0");
