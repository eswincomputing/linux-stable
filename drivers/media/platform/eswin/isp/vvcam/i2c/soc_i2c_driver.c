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


#include <asm/io.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/i2c.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include "soc_i2c_platform.h"
#include "soc_i2c_procfs.h"

struct soc_gpio_i2c {
	struct i2c_adapter adap;
	void __iomem *virt_base_addr;
	struct proc_dir_entry *pde;
};

static void i2c_gpio_setsda_val(void *source, int state)
{
	struct soc_gpio_i2c *i2c = source;
	int reg_data;

	reg_data = readl(i2c->virt_base_addr);
	reg_data = (reg_data & (~0x1)) | (0x10 | (state & 0x01));

	writel(reg_data, i2c->virt_base_addr);
	return;
}

static void i2c_gpio_setscl_val(void *source, int state)
{
	struct soc_gpio_i2c *i2c = source;
	int reg_data;

	reg_data = readl(i2c->virt_base_addr);
	reg_data = (reg_data & (~(0x1 << 8))) | ((0x10 | (state & 0x01)) << 8);

	writel(reg_data, i2c->virt_base_addr);
	return;
}

static int i2c_gpio_getsda(void *source)
{
	struct soc_gpio_i2c *i2c = source;
	unsigned int reg_data;

	reg_data = readl(i2c->virt_base_addr);
	reg_data = (reg_data & (~0x10));
	writel(reg_data, i2c->virt_base_addr);

	return (readl(i2c->virt_base_addr) & 0x01);
}

// Start :When SCL is High ,SDA Change to low
static void i2c_gpio_start(void *source)
{
	i2c_gpio_setscl_val(source, 1);
	i2c_gpio_setsda_val(source, 1);
	udelay(5);
	i2c_gpio_setsda_val(source, 0);
	udelay(1);
	return;
}

//Send Bit : When SCL is LOW, sda change;When SCL is HIgh,SDA hold;
static void i2c_gpio_send_byte(void *source, unsigned char val)
{
	int bit_idx;
	for (bit_idx = 7; bit_idx >= 0; bit_idx--) {
		i2c_gpio_setscl_val(source, 0);
		i2c_gpio_setsda_val(source, (val >> bit_idx) & 0x01);
		udelay(1);
		i2c_gpio_setscl_val(source, 1);
		udelay(1);
	}
	i2c_gpio_setscl_val(source, 0);
	i2c_gpio_setsda_val(source, 1);
	return;
}

//Read Bit: When SCL is High read data;
static unsigned char i2c_gpio_read_byte(void *source)
{
	int bit_idx;
	unsigned char data = 0;

	i2c_gpio_getsda(source);	//SDA change to read

	for (bit_idx = 7; bit_idx >= 0; bit_idx--) {

		i2c_gpio_setscl_val(source, 1);
		udelay(1);
		data = (data << 1) | i2c_gpio_getsda(source);
		i2c_gpio_setscl_val(source, 0);
		udelay(1);
	}
	return data;
}

static int i2c_gpio_wait_ack(void *source)
{
	unsigned int i2c_retry_ack_cnt = 0;

	i2c_gpio_getsda(source);	//SDA change to read
	udelay(1);
	i2c_gpio_setscl_val(source, 1);

	while (i2c_gpio_getsda(source) == 1) {
		udelay(1);
		if (i2c_retry_ack_cnt++ > 20) {
			return 0;
		}
	}
	i2c_gpio_setscl_val(source, 0);
	return 1;
}

static void i2c_gpio_ack(void *source)
{
	i2c_gpio_setsda_val(source, 0);
	i2c_gpio_setscl_val(source, 1);
	udelay(1);
	i2c_gpio_setscl_val(source, 0);
	udelay(1);
}

static void i2c_gpio_nack(void *source)
{
	i2c_gpio_setsda_val(source, 1);
	i2c_gpio_setscl_val(source, 1);
	udelay(1);
	i2c_gpio_setscl_val(source, 0);
	udelay(1);
}

static void i2c_gpio_stop(void *source)
{
	i2c_gpio_setsda_val(source, 0);
	udelay(1);
	i2c_gpio_setscl_val(source, 1);
	udelay(1);
	i2c_gpio_setsda_val(source, 1);
	return;
}

static int i2c_gpio_write(void *source, struct i2c_msg *msg)
{

	unsigned char slave_address;
	unsigned char *buf;
	int i;
	slave_address = (msg->addr) << 1;
	buf = msg->buf;

	i2c_gpio_start(source);
	i2c_gpio_send_byte(source, slave_address);
	if (i2c_gpio_wait_ack(source) == 0)
		return -1;

	for (i = 0; i < msg->len; i++) {
		i2c_gpio_send_byte(source, buf[i]);
		i2c_gpio_wait_ack(source);
	}

	i2c_gpio_stop(source);
	return 0;
}

static int i2c_gpio_read(void *source, struct i2c_msg *msg)
{
	unsigned char slave_address;
	unsigned char *buf;
	int i;
	slave_address = ((msg->addr) << 1) | 0x01;
	buf = msg->buf;

	i2c_gpio_start(source);
	i2c_gpio_send_byte(source, slave_address);
	if (i2c_gpio_wait_ack(source) == 0)
		return -1;

	for (i = 0; i < msg->len; i++) {
		buf[i] = i2c_gpio_read_byte(source);
		if (i < msg->len - 1) {
			i2c_gpio_ack(source);
		} else {
			i2c_gpio_nack(source);
		}
	}

	i2c_gpio_stop(source);

	return 0;
}

static int i2c_gpio_xfer(struct i2c_adapter *adapter,
						 struct i2c_msg *msgs, int num)
{
	int result = 0;
	int i;
	void *source;

	source = i2c_get_adapdata(adapter);

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD) {
			result = i2c_gpio_read(source, &(msgs[i]));
		} else {
			result = i2c_gpio_write(source, &(msgs[i]));
		}

		if (result < 0)
			break;
	}

	return (result < 0) ? result : num;
}

static u32 i2c_gpio_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C |
		(I2C_FUNC_SMBUS_EMUL & ~I2C_FUNC_SMBUS_QUICK) |
		I2C_FUNC_PROTOCOL_MANGLING;
}

static const struct i2c_algorithm gpio_i2c_algo = {
	.master_xfer   = i2c_gpio_xfer,
	.functionality = i2c_gpio_func,
};

static int soc_gpio_i2c_probe(struct platform_device *pdev)
{
	int ret;
	struct soc_gpio_i2c *i2c;
	struct i2c_adapter *adap;
	struct resource *res;

	i2c = devm_kzalloc(&pdev->dev, sizeof(struct soc_gpio_i2c), GFP_KERNEL);
	if (!i2c) {
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't fetch device resource info\n");
		return -EIO;
	}

	i2c->virt_base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->virt_base_addr)) {
		dev_err(&pdev->dev, "can't remap device resource info\n");
		return PTR_ERR(i2c->virt_base_addr);
	}

	adap = &i2c->adap;
	platform_set_drvdata(pdev, i2c);
	i2c_set_adapdata(adap, i2c);

	strlcpy(adap->name, "soc_gpio_i2c", sizeof(adap->name));
	adap->owner       = THIS_MODULE;
	adap->algo        = &gpio_i2c_algo;
	adap->dev.parent  = &pdev->dev;
	adap->nr          = pdev->id;
	adap->dev.of_node = pdev->dev.of_node;
	adap->timeout     = 2 * HZ;
	adap->retries     = 0;
	adap->class       = I2C_CLASS_HWMON | I2C_CLASS_SPD;

	ret = i2c_add_numbered_adapter(adap);
	if (ret) {
		dev_err(&pdev->dev, "i2c_add_adapter failed\n");
		return ret;
	}

	ret = soc_gpio_i2c_procfs_register(adap, &(i2c->pde));
	if (ret) {
		dev_err(&pdev->dev, "i2c register procfs failed\n");
		return ret;
	}

	dev_info(&pdev->dev, "i2c bus probe success\n");

	return 0;
}

static int soc_gpio_i2c_remove(struct platform_device *pdev)
{
	struct soc_gpio_i2c *i2c;

	i2c = platform_get_drvdata(pdev);

	soc_gpio_i2c_procfs_unregister(&i2c->pde);

	i2c_del_adapter(&(i2c->adap));

	return 0;
}

static struct platform_driver soc_gpio_i2c_driver = {
	.probe  = soc_gpio_i2c_probe,
	.remove = soc_gpio_i2c_remove,
	.driver = {
		.name = "soc_gpio_i2c",
		.owner = THIS_MODULE,
	},
};

static int __init soc_gpio_i2c_driver_init_module(void)
{
	int ret = 0;

	ret = platform_driver_register(&soc_gpio_i2c_driver);
	if (ret) {
		printk(KERN_ERR "Failed to register gpio i2c driver\n");
		return ret;
	}

	ret = soc_gpio_i2c_platform_device_register();
	if (ret) {
		platform_driver_unregister(&soc_gpio_i2c_driver);
		printk(KERN_ERR "Failed to register gpio i2c platform devices\n");
		return ret;
	}

	return ret;
}

static void __exit soc_gpio_i2c_driver_exit_module(void)
{
	platform_driver_unregister(&soc_gpio_i2c_driver);
	soc_gpio_i2c_platform_device_unregister();
	return;
}

late_initcall(soc_gpio_i2c_driver_init_module);
module_exit(soc_gpio_i2c_driver_exit_module);

MODULE_DESCRIPTION("Verisilicon soc_gpio_i2c driver");
MODULE_AUTHOR("Verisilicon ISP SW Team");
MODULE_LICENSE("GPL");
