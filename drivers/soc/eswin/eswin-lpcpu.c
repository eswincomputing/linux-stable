// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 ESWIN
 *
 * Implementation of the WIN2030 lpcpu (client side).
 *
 */

#include <linux/types.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <uapi/linux/dma-heap.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/firmware.h>
#include <linux/of_address.h>
#include <linux/dma-map-ops.h>
#include <linux/of_reserved_mem.h>
#include <linux/iommu.h>
// #include <linux/mailbox/eswin-ipc-scpu.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <soc/eswin/eswin-lpcpu.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>

#define LPCPU_FW_RESERVED
#define FW_BOOT_ADDR 0x80000000

struct lowpower_info {
	int level;
};

#define LPCPU_IOC_MAGIC 'L'
#define LPCPU_IOC_SAVEPOWER						  \
	_IOWR(LPCPU_IOC_MAGIC, 0x1, struct lowpower_info)

#define FW_LOAD_UNKNOW 0
#define FW_LOAD_SUCC   0xacce55

struct lpcpu_dev {
	struct miscdevice mdev;
	struct mutex lock;
	struct mbox_chan *mbox_channel;
	// struct dma_allocation_data send_buff;
	wait_queue_head_t waitq;
	u32 num;
	u8 *req_msg;
	u32 res_size;
	int numa_id;
	struct clk *core_clk;
	struct clk *bus_clk;
	struct reset_control *core_rst;
	struct reset_control *bus_rst;
	struct reset_control *dbg_rst;
	void __iomem *mmio;
	size_t fw_size;
	struct gpio_desc *irq_gpio;
};

static struct lpcpu_dev *lpcpu;
static u32 load_event = FW_LOAD_UNKNOW;

struct mbox_msg{
	u32 data_l;
	u32 data_h;
};

static void eswin_lpcpu_rx_callback(struct mbox_client *client, void *msg)
{
	struct mbox_msg *umsg = msg;
	struct device *dev = lpcpu->mdev.parent;
	dev_dbg(dev, "lpcpu rx callback : %llx\n",*(u64 *)msg);
	printk("data_l= %x, data_h = %x\n",umsg->data_l,umsg->data_h);
	if (load_event != FW_LOAD_SUCC) {
		load_event = *(u32 *)msg;
		wake_up(&lpcpu->waitq);
		printk("eswin_lpcpu_rx_callback returned \n");
	}

	return;
}

static void eswin_lpcpu_tx_done(struct mbox_client *client, void *msg, int r)
{
	if (r)
		dev_warn(client->dev, "Client: Message could not be sent:%d\n", r);
	else
		dev_dbg(client->dev, "Client: Message sent\n");
}

static struct mbox_chan *eswin_lpcpu_request_channel(struct platform_device *pdev,
						   const char *name)
{
	struct mbox_client *client;
	struct mbox_chan *channel;

	client = devm_kzalloc(&pdev->dev, sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);

	client->dev = &pdev->dev;
	client->rx_callback = eswin_lpcpu_rx_callback;
	client->tx_prepare = NULL;
	client->tx_done = eswin_lpcpu_tx_done;
	client->tx_block = false;
	client->knows_txdone = false;

	channel = mbox_request_channel_byname(client, name);
	if (IS_ERR(channel)) {
		dev_warn(&pdev->dev, "Failed to request %s channel\n", name);
		return NULL;
	}
	dev_dbg(&pdev->dev, "request mbox chan %s\n", name);

	return channel;
}

static int eswin_lpcpu_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct device *dev = NULL;

	dev = lpcpu->mdev.parent;
	dev_info(dev, "%s\n", __func__);

	return ret;
}

static ssize_t eswin_lpcpu_read(struct file *filp, char __user *userbuf,
					  size_t count, loff_t *ppos)
{
	printk("lpcpu read\n");
	return 0;
}

static ssize_t eswin_lpcpu_write(struct file *filp,
					   const char __user *userbuf, size_t count,
					   loff_t *ppos)
{
	printk("lpcpu write\n");
	u8 msg[8];
	msg[0] = 0xca;
	msg[1] = 0xec;
	msg[2] = 0x55;
	int ret = 0;
	ret = mbox_send_message(lpcpu->mbox_channel, msg);
	if (ret < 0){
		ret = -EAGAIN;
		// dev_dbg(dev, "Failed to send message via mailbox\r\n");
	}
	mbox_client_txdone(lpcpu->mbox_channel, 0);
	return count;
}

static __poll_t eswin_lpcpu_poll(struct file *filp,
					  struct poll_table_struct *wait)
{
	poll_wait(filp, &lpcpu->waitq, wait);

	// if (eswin_ipc_service_ready(session))
	// 	return EPOLLIN | EPOLLRDNORM;
	return 0;
}

static int eswin_lpcpu_release(struct inode *inode, struct file *filp)
{

	pr_info("%s called!\n", __func__);

	return 0;
}

static long eswin_lpcpu_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct lowpower_info lowpower;
	struct device *dev = lpcpu->mdev.parent;
	u8 msg[8];
	// void *cpu_vaddr = NULL;
	// struct dmabuf_bank_info *info, *info_free;
	// unsigned int cmd_size = 0;
	// size_t buf_size = 0;
	int ret = 0;
	printk("ioctl: %x",cmd);

	if (cmd & IOC_IN) {
		if (copy_from_user(&lowpower, (void __user *)arg, sizeof(struct lowpower_info)) != 0) {
			pr_err("ioctl copy_from_user failed.\n");
			ret = -EFAULT;
			return ret;
		}
	}
	// else if (cmd & IOC_OUT) {
	// 	memset(kdata, 0, usize);
	// }

	switch (cmd) {
		/* alloc memory by driver using dmabuf heap helper API  */
		case LPCPU_IOC_SAVEPOWER: {
			msg[0] = 0x55;
			msg[1] = 0xaa;

			ret = mbox_send_message(lpcpu->mbox_channel, msg);
			if (ret < 0){
				ret = -EAGAIN;
				dev_dbg(dev, "Failed to send message via mailbox\r\n");
			}
			mbox_client_txdone(lpcpu->mbox_channel, 0);

			break;
		}

		default: {
			dev_err(dev, "Invalid IOCTL command %u\n", cmd);
			return -ENOTTY;
		}
	}

	return ret;
}

int eswin_lpcpu_service_ctl(int fid)
{
	u8 msg[8];
	int ret = 0;

	if(NULL == lpcpu)
	{
		ret = -EINVAL;
		return ret;
	}

	switch (fid) {
		/* alloc memory by driver using dmabuf heap helper API  */
		case PM_SUSPEND_MEM_ENTER: {
			msg[0] = 0xcb;
			msg[1] = 0xec;
			msg[2] = 0x55;

			ret = mbox_send_message(lpcpu->mbox_channel, msg);
			if (ret < 0){
				ret = -EAGAIN;
				printk("Failed to send message via mailbox\r\n");
			}
			mbox_client_txdone(lpcpu->mbox_channel, 0);

			break;
		}

		default: {
			return -ENOTTY;
		}
	}

	return ret;
}

static const struct file_operations eswin_lpcpu_ops = {
	.owner = THIS_MODULE,
	.write = eswin_lpcpu_write,
	.read = eswin_lpcpu_read,
	.open = eswin_lpcpu_open,
	.poll = eswin_lpcpu_poll,
	.release = eswin_lpcpu_release,
	.unlocked_ioctl = eswin_lpcpu_ioctl,
};

static int lpcpu_boot_status(struct mbox_chan *mbox_channel, u32 boot_config_msg,
		u32 boot_config_msg_l)
{
	int ret = 0;
	u8 msg[8];
	msg[0] = 0xca;
	msg[1] = 0xec;
	msg[2] = 0x55;
	// boot config msg
	msg[3] = (boot_config_msg_l >> 24) & 0xff;
	msg[4] = boot_config_msg & 0xff;
	msg[5] = (boot_config_msg >> 8) & 0xff;
	msg[6] = (boot_config_msg >> 16) & 0xff;
	msg[7] = (boot_config_msg >> 24) & 0xff;

	ret = mbox_send_message(mbox_channel, msg);
	if (ret < 0){
		ret = -EAGAIN;
		printk("Failed to send message via mailbox\r\n");
		return ret;
	}
	mbox_client_txdone(lpcpu->mbox_channel, 0);

	return 0;
}

// TODO: add clk, reset tbu config
static int eswin_lpcpu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *mbox_channel_name;
	int numa_id = 0;
	int ret;
	long timeout;
	u32 config_gpio = 0, config_die = 0, config_polarity = 0, buff[3];
	u32 boot_config_msg = 0, boot_config_msg_l = 0;

	lpcpu = devm_kzalloc(dev, sizeof(*lpcpu), GFP_KERNEL);
	if (!lpcpu)
		return -ENOMEM;
	platform_set_drvdata(pdev, lpcpu);

	lpcpu->irq_gpio = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (IS_ERR(lpcpu->irq_gpio)) {
		dev_err(dev, "Failed to get IRQ GPIO\n");
		return PTR_ERR(lpcpu->irq_gpio);
	}
	enable_irq_wake(gpiod_to_irq(lpcpu->irq_gpio));

	lpcpu->mdev.minor = MISC_DYNAMIC_MINOR;
	lpcpu->mdev.name = "lpcpu";
	lpcpu->mdev.fops = &eswin_lpcpu_ops;
	lpcpu->mdev.parent = dev;

	if(of_property_read_u32(pdev->dev.of_node, "numa-node-id", &numa_id)) {
		numa_id = 0;
	}
	dev_dbg(&pdev->dev, "numa_id=%d\n", numa_id);
	lpcpu->numa_id = numa_id;

	ret = misc_register(&lpcpu->mdev);
	if (ret) {
		dev_err(dev, "failed to register misc device: %d\n", ret);
		goto err_misc;
	}

	/*use eswin mailbox0 to send msg and mailbox1 receive msg*/
	ret = device_property_read_string(&pdev->dev, "mbox-names",
					  &mbox_channel_name);
	if (ret == 0) {
		lpcpu->mbox_channel = eswin_lpcpu_request_channel(pdev, mbox_channel_name);
	}else{
		dev_err(dev, "given arguments are not valid: %d\n", ret);
		goto err_mailbox;
	}

	/* Get boot config message
	 * data_h           31      can not be used
	 *                  30      boot config exists 1: exist 0: not exist
	 *                  29:28   reserved
	 *                  27      lcd power polarity 0: positive 1: negative
	 *                  26      npu power polarity 0: positive 1: negative
	 *                  25      d2d power polarity 0: positive 1: negative
	 *                  24      soc vdd polarity 0: positive 1: negative
	 *                  23      npu power die number 0-1, ignored on single die chip
	 *                  22:16   npu power gpio number 1-127, 0 for invalid
	 *                  15      d2d power die number 0-1, ignored on single die chip
	 *                  14:8    d2d power gpio number 1-127, 0 for invalid
	 *                  7       soc vdd die number 0-1, ignored on single die chips
	 *                  6:0     soc vdd gpio number 1-127, 0 for invalid
	 * data_l           31      lcd power die number 0-1, ignored on single die chip
	 *                  30:24   lcd power gpio number 1-127, 0 for invalid
	 *                  23:0    BOOT_STATUS message: fixed 0x55ecca
	 * of_property:     soc-vdd-ctrl = <GPIO [POLARITY [DIE]}>;
	 *                  d2d-power-ctrl = <GPIO [POLARITY [DIE]]>;
	 *                  npu-power-ctrl = <GPIO [POLARITY [DIE]]>;
	 *                  lcd-power-ctrl = <GPIO [POLARITY [DIE]]>;
	 *                  GPIO:       gpio port number which d2d power control uses,
	 *                              range: 1-127, 0 for invalid
	 *                  POLARITY:   gpio level polarity, default: 0
	 *                              0: positive, high -> high/on, low -> low/off
	 *                              1: negative, high -> low/off, low -> high/on
	 *                  DIE:        die number, range 0-1, default: 0 */
	ret = of_property_read_variable_u32_array(pdev->dev.of_node, "soc-vdd-ctrl", buff, 1, 3);
	if (ret > 0) {
		config_gpio = buff[0] & 0x7f;
		if (ret > 1)
			config_polarity = buff[1] ? 1 : 0;
		if (ret > 2)
			config_die = buff[2] ? 1 : 0;
		boot_config_msg = config_gpio | (config_die << 7) | (config_polarity << 24) |
            (1 << 30);
		dev_info(dev, "soc vdd config gpio: die %d, port %d, polarity %s\n",
                config_die, config_gpio, !config_polarity ? "positive" : "negative");
	}
	ret = of_property_read_variable_u32_array(pdev->dev.of_node, "d2d-power-ctrl", buff, 1, 3);
	if (ret > 0) {
		config_gpio = buff[0] & 0x7f;
		if (ret > 1)
			config_polarity = buff[1] ? 1 : 0;
		if (ret > 2)
			config_die = buff[2] ? 1 : 0;
		boot_config_msg |= (config_gpio << 8) | (config_die << 15) |
            (config_polarity << 25) | (1 << 30);
		dev_info(dev, "d2d power config gpio: die %d, port %d, polarity %s\n",
                config_die, config_gpio, !config_polarity ? "positive" : "negative");
	}

	ret = of_property_read_variable_u32_array(pdev->dev.of_node, "npu-power-ctrl", buff, 1, 3);
	if (ret > 0) {
		config_gpio = buff[0] & 0x7f;
		if (ret > 1)
			config_polarity = buff[1] ? 1 : 0;
		if (ret > 2)
			config_die = buff[2] ? 1 : 0;
		boot_config_msg |= (config_gpio << 16) | (config_die << 23) |
			(config_polarity << 26) | (1 << 30);
		dev_info(dev, "npu power config gpio: die %d, port %d, polarity %s\n",
                config_die, config_gpio, !config_polarity ? "positive" : "negative");
	}

	ret = of_property_read_variable_u32_array(pdev->dev.of_node, "lcd-power-ctrl", buff, 1, 3);
	if (ret > 0) {
		config_gpio = buff[0] & 0x7f;
		if (ret > 1)
			config_polarity = buff[1] ? 1 : 0;
		if (ret > 2)
			config_die = buff[2] ? 1 : 0;
		boot_config_msg_l = (config_gpio << 24) | (config_die << 31);
		boot_config_msg |= (config_polarity << 27) | (1 << 30);
		dev_info(dev, "lcd power config gpio: die %d, port %d, polarity %s\n",
                config_die, config_gpio, !config_polarity ? "positive" : "negative");
	}

	mutex_init(&lpcpu->lock);
	init_waitqueue_head(&lpcpu->waitq);

	lpcpu->core_clk = devm_clk_get(dev, "core_clk");
	if (IS_ERR(lpcpu->core_clk)) {
		dev_err(dev, "core clock source missing or invalid\n");
		goto err_clkrst;
	}

	lpcpu->bus_clk = devm_clk_get(dev, "bus_clk");
	if (IS_ERR(lpcpu->bus_clk)) {
		dev_err(dev, "bus clock source missing or invalid\n");
		goto err_clkrst;
	}

	lpcpu->core_rst = devm_reset_control_get_optional(&pdev->dev, "core_rst");
	if (IS_ERR_OR_NULL(lpcpu->core_rst)) {
		dev_err_probe(dev, PTR_ERR(lpcpu->core_rst), "unable to get core reset\n");
		goto err_clkrst;
	}

	lpcpu->bus_rst = devm_reset_control_get_optional(&pdev->dev, "bus_rst");
	if (IS_ERR_OR_NULL(lpcpu->bus_rst)) {
		dev_err_probe(dev, PTR_ERR(lpcpu->bus_rst), "unable to get bus reset\n");
		goto err_clkrst;
	}

	lpcpu->dbg_rst = devm_reset_control_get_optional(&pdev->dev, "dbg_rst");
	if (IS_ERR_OR_NULL(lpcpu->dbg_rst)) {
		dev_err_probe(dev, PTR_ERR(lpcpu->dbg_rst), "unable to get dbg reset\n");
		goto err_clkrst;
	}
	#define LPCPU_BOOT_ADDR 0x51828314
	if (!lpcpu->numa_id)
		lpcpu->mmio = ioremap(LPCPU_BOOT_ADDR, 4);
	else
		lpcpu->mmio = ioremap(LPCPU_BOOT_ADDR+0x20000000, 4);

	if (!lpcpu->mmio) {
		pr_err("lpcpu ioremap fail.\n");
		ret = -ENOMEM;
		goto err_mmio;
	}

	ret = lpcpu_boot_status(lpcpu->mbox_channel, boot_config_msg, boot_config_msg_l);
	if (ret < 0) {
		dev_err(dev, "Send message to lpcpu via mailbox failed!\n");
		goto err_mmio;
	}
	timeout = wait_event_timeout(lpcpu->waitq,
			load_event == FW_LOAD_SUCC,usecs_to_jiffies(100000));

	if (!timeout) {
		dev_err(dev, "Lpcpu is not boot!\n");
		ret = -EBUSY;
		goto err_mmio;
	}

	dev_info(dev, "eswin lpcpu initialized\n");

	return 0;

err_mmio:
err_clkrst:
	mbox_free_channel(lpcpu->mbox_channel);
err_mailbox:
	misc_deregister(&lpcpu->mdev);
err_misc:
	devm_kfree(&pdev->dev,lpcpu);
	lpcpu = NULL;
	return ret;
}

static int eswin_lpcpu_remove(struct platform_device *pdev)
{
	struct lpcpu_dev *_dev = platform_get_drvdata(pdev);

	if (_dev->mbox_channel)
		mbox_free_channel(_dev->mbox_channel);
	misc_deregister(&_dev->mdev);
	iounmap(_dev->mmio);
	devm_kfree(&pdev->dev,_dev);
	dev_dbg(&pdev->dev, "%s remove!\n", pdev->name);

	return 0;
}

static const struct of_device_id eswin_lpcpu_match[] = {
	{
		.compatible = "eswin,win2030-lpcpu",
	},
	{ /* Sentinel */ }
};

static struct platform_driver eswin_lpcpu_driver = {
	.driver = {
	.name = "win2030-lpcpu",
	.of_match_table = eswin_lpcpu_match,
	},
	.probe = eswin_lpcpu_probe,
	.remove = eswin_lpcpu_remove,
};

static int __init lpcpu_modules_init(void)
{
	int err;

	err = platform_driver_register(&eswin_lpcpu_driver);
	if (err < 0){
		pr_err("lpcpu:platform_register_drivers failed!err=%d\n",err);
	}

	return err;
}
module_init(lpcpu_modules_init);

static void __exit lpcpu_modules_exit(void)
{
	platform_driver_unregister(&eswin_lpcpu_driver);
}
module_exit(lpcpu_modules_exit);

MODULE_DESCRIPTION("ESWIN WIN2030 lpcpu driver");
MODULE_LICENSE("GPL v2");
