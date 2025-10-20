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
#include <linux/mailbox_controller.h>

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
#define CFG_RECV_SUCC  0x366676

#define LPCPU_BOOT_ADDR         0x51828314
#define LPCPU_CONFIG_ADDR       0x5880d400
#define LPCPU_CONFIG_MAGIC      0x4c435055
#define LPCPU_CONFIG_VERSION    3
#define LPCPU_NPU_FW_MAX_SIZE   (64*1024)
#define LPCPU_RSV_MEM_MIN_SIZE  (5*1024*1024)

#define ALIGN_UP_TO_MB(x)       ((((x)-1) & ~((1ull<<20)-1)) + (1ull<<20))
#define ALIGN_DOWN_TO_MB(x)     ((x) & ~((1ull<<20)-1))

#define LPCPU_NPU_FW_DIE0   "eic7702_lpcpu_npu_fw_die0.bin"
#define LPCPU_NPU_FW_DIE1   "eic7702_lpcpu_npu_fw_die1.bin"

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
	u64 rsv_mem_addr;
	u64 rsv_mem_size;
};

static struct lpcpu_dev *primary_lpcpu;
static u32 load_event = FW_LOAD_UNKNOW;

struct mbox_msg {
	u32 data_l;
	u32 data_h;
};

struct control_gpio_config {
	char name[7];
	struct {
		u8 gpio : 7;
		u8 polarity : 1;
	};
};

struct lpcpu_config {
	u32 magic;
	u32 version;
	u64 npu_fw_addr;
	u64 npu_buf_1;
	u64 npu_buf_2;
	struct control_gpio_config gpio_config[16];
};

static void eswin_lpcpu_rx_callback(struct mbox_client *client, void *msg)
{
	struct mbox_msg *umsg = msg;
	struct device *dev = client->dev;
	struct lpcpu_dev *lpcpu = platform_get_drvdata(container_of(dev, struct platform_device, dev));
	dev_dbg(dev, "lpcpu rx callback : %llx\n",*(u64 *)msg);
	dev_dbg(dev, "data_l= %x, data_h = %x\n",umsg->data_l,umsg->data_h);
	load_event = *(u32 *)msg;
	wake_up(&lpcpu->waitq);
	dev_dbg(dev, "eswin_lpcpu_rx_callback returned \n");

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

static int lpcpu_send_message(struct mbox_chan *mbox_channel, u8 *msg)
{
	int ret;
	ret = mbox_send_message(mbox_channel, msg);
	if (ret < 0){
		ret = -EAGAIN;
		printk("Failed to send message via mailbox\r\n");
		return ret;
	}
	if (mbox_channel->txdone_method & BIT(2)/*TXDONE_BY_ACK*/)
		mbox_client_txdone(mbox_channel, 0);

	return 0;
}

static int eswin_lpcpu_open(struct inode *inode, struct file *filp)
{
	struct lpcpu_dev *lpcpu = (struct lpcpu_dev *)filp->private_data;
	struct device *dev = lpcpu->mdev.parent;
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}

static ssize_t eswin_lpcpu_read(struct file *filp, char __user *userbuf,
					  size_t count, loff_t *ppos)
{
	struct lpcpu_dev *lpcpu = (struct lpcpu_dev *)filp->private_data;
	struct device *dev = lpcpu->mdev.parent;
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}

static ssize_t eswin_lpcpu_write(struct file *filp,
					   const char __user *userbuf, size_t count,
					   loff_t *ppos)
{
	struct lpcpu_dev *lpcpu = (struct lpcpu_dev *)filp->private_data;
	struct device *dev = lpcpu->mdev.parent;
	dev_dbg(dev, "%s\n", __func__);
	u8 msg[8];
	msg[0] = 0xca;
	msg[1] = 0xec;
	msg[2] = 0x55;
	int ret = 0;
	ret = lpcpu_send_message(lpcpu->mbox_channel, msg);
	return count;
}

static __poll_t eswin_lpcpu_poll(struct file *filp,
					  struct poll_table_struct *wait)
{
	struct lpcpu_dev *lpcpu = (struct lpcpu_dev *)filp->private_data;
	poll_wait(filp, &lpcpu->waitq, wait);

	// if (eswin_ipc_service_ready(session))
	// 	return EPOLLIN | EPOLLRDNORM;
	return 0;
}

static int eswin_lpcpu_release(struct inode *inode, struct file *filp)
{

	struct lpcpu_dev *lpcpu = (struct lpcpu_dev *)filp->private_data;
	struct device *dev = lpcpu->mdev.parent;
	dev_dbg(dev, "%s\n", __func__);
	return 0;
}

static long eswin_lpcpu_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg)
{
	struct lowpower_info lowpower;
	struct lpcpu_dev *lpcpu = (struct lpcpu_dev *)filp->private_data;
	struct device *dev = lpcpu->mdev.parent;
	u8 msg[8];
	// void *cpu_vaddr = NULL;
	// struct dmabuf_bank_info *info, *info_free;
	// unsigned int cmd_size = 0;
	// size_t buf_size = 0;
	int ret = 0;
	dev_dbg(dev, "ioctl: %x",cmd);

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
			ret = lpcpu_send_message(lpcpu->mbox_channel, msg);
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
	struct lpcpu_dev *lpcpu = primary_lpcpu;

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
			ret = lpcpu_send_message(lpcpu->mbox_channel, msg);
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

static int lpcpu_boot_status(struct mbox_chan *mbox_channel)
{
	u8 msg[8];
	msg[0] = 0xca;
	msg[1] = 0xec;
	msg[2] = 0x55;

	return lpcpu_send_message(mbox_channel, msg);
}

static int lpcpu_config_send(struct mbox_chan *mbox_channel)
{
	u8 msg[8];
	msg[0] = 0x63;
	msg[1] = 0x66;
	msg[2] = 0x67;

	return lpcpu_send_message(mbox_channel, msg);
}

static u64 lpcpu_npu_fw_prepare(struct platform_device *pdev)
{
	void __iomem *mmio;
	u64 fw_addr;
	const struct firmware *fw_p;
	struct lpcpu_dev *lpcpu = platform_get_drvdata(pdev);

	// npu firmware prepare
	if (lpcpu->rsv_mem_size < LPCPU_NPU_FW_MAX_SIZE) {
		dev_err(&pdev->dev, "Reserved memory region to small for npu-fw!\n");
		return 0;
	}

	fw_addr = lpcpu->rsv_mem_addr + lpcpu->rsv_mem_size - LPCPU_NPU_FW_MAX_SIZE;
	mmio = ioremap(fw_addr, LPCPU_NPU_FW_MAX_SIZE);
	if (!mmio) {
		dev_err(&pdev->dev, "Lpcpu npu-fw memory map error!\n");
		return 0;
	}
	if (!request_firmware_into_buf(&fw_p,
			lpcpu->numa_id ? LPCPU_NPU_FW_DIE1 : LPCPU_NPU_FW_DIE0,
			&pdev->dev, mmio, LPCPU_NPU_FW_MAX_SIZE)) {
		dev_info(&pdev->dev, "Lpcpu npu fw for die%d loaded!\n", lpcpu->numa_id);
	} else {
		dev_err(&pdev->dev, "Lpcpu npu-fw load error!\n");
		iounmap(mmio);
		return 0;
	}
	iounmap(mmio);

	return fw_addr;
}

static void lpcpu_parse_ctrl_gpio(struct platform_device *pdev, struct lpcpu_config *cfg)
{
	int cnt_n, cnt_s, i;
	u32 buf[32];
	const char *sbuf[16];

	cnt_n = of_property_read_variable_u32_array(pdev->dev.of_node, "control-gpio-ports", buf, 2, 32);
	cnt_s = of_property_read_string_array(pdev->dev.of_node, "control-gpio-names", sbuf, 16);

	if (cnt_n <= 0 || cnt_s <= 0) {
		return;
	}

	for (i = 0; i < (cnt_n/2 < cnt_s ? cnt_n/2 : cnt_s); i ++) {
		cfg->gpio_config[i].gpio = buf[2*i] & 0x7f;
		cfg->gpio_config[i].polarity = buf[2*i+1] & 0x1;
		strncpy(cfg->gpio_config[i].name, sbuf[i], 6);
		dev_info(&pdev->dev, "gpio config: name: %s, port: %d polarity: %s\n",
				cfg->gpio_config[i].name, cfg->gpio_config[i].gpio,
				!cfg->gpio_config[i].polarity ? "positive" : "negative");
	}
}

static int lpcpu_config_prepare(struct platform_device *pdev)
{
	void __iomem *mmio;
	struct lpcpu_config cfg = {0};
	struct lpcpu_dev *lpcpu = platform_get_drvdata(pdev);

	cfg.magic = LPCPU_CONFIG_MAGIC;
	cfg.version = LPCPU_CONFIG_VERSION;
	lpcpu_parse_ctrl_gpio(pdev, &cfg);

	// prepare npu fw and generate npu buffer address
	if (lpcpu->rsv_mem_addr && lpcpu->rsv_mem_size >= LPCPU_RSV_MEM_MIN_SIZE) {
		cfg.npu_fw_addr = lpcpu_npu_fw_prepare(pdev);
		if (!cfg.npu_fw_addr) {
			dev_err(&pdev->dev, "npu firmware prepare error!\n");
		} else {
			cfg.npu_buf_1 = ALIGN_UP_TO_MB(lpcpu->rsv_mem_addr);
			cfg.npu_buf_2 = ALIGN_UP_TO_MB(lpcpu->rsv_mem_addr)
				+ ALIGN_DOWN_TO_MB(lpcpu->rsv_mem_size / 2);
			dev_dbg(&pdev->dev, "fw_addr: 0x%llx, buf_1: 0x%llx, buf_2: 0x%llx\n",
					cfg.npu_fw_addr, cfg.npu_buf_1, cfg.npu_buf_2);
		}
	}

	// copy lpcpu config
	mmio = ioremap(LPCPU_CONFIG_ADDR + lpcpu->numa_id * 0x20000000,
			sizeof(struct lpcpu_config));
	if (!mmio) {
		dev_err(&pdev->dev, "Lpcpu config memory map error!\n");
		return -ENOMEM;
	}
	memcpy(mmio, &cfg, sizeof(struct lpcpu_config));
	iounmap(mmio);

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
	struct device_node *np;
	struct resource rsc = {0};

	struct lpcpu_dev *lpcpu = devm_kzalloc(dev, sizeof(*lpcpu), GFP_KERNEL);
	if (!lpcpu)
		return -ENOMEM;
	platform_set_drvdata(pdev, lpcpu);

	lpcpu->irq_gpio = devm_gpiod_get(dev, "irq", GPIOD_IN);
	if (!IS_ERR(lpcpu->irq_gpio)) {
		enable_irq_wake(gpiod_to_irq(lpcpu->irq_gpio));
	}

	if(of_property_read_u32(pdev->dev.of_node, "numa-node-id", &numa_id)) {
		numa_id = 0;
	}
	dev_dbg(&pdev->dev, "numa_id=%d\n", numa_id);
	lpcpu->numa_id = numa_id;

	lpcpu->mdev.minor = MISC_DYNAMIC_MINOR;
	if (numa_id) {
		lpcpu->mdev.name = "lpcpu1";
	} else {
		lpcpu->mdev.name = "lpcpu0";
		primary_lpcpu = lpcpu;
	}
	lpcpu->mdev.fops = &eswin_lpcpu_ops;
	lpcpu->mdev.parent = dev;

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

	/* parse reserved memory for npu fw  */
	np = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!IS_ERR(np)) {
		ret = of_address_to_resource(np, 0, &rsc);
		if (!ret) {
			lpcpu->rsv_mem_addr = rsc.start;
			lpcpu->rsv_mem_size = resource_size(&rsc);
			dev_info(dev, "Reserved memory region: 0x%llx, size: 0x%llx \n",
					rsc.start, resource_size(&rsc));
		}
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

	if (!lpcpu->numa_id)
		lpcpu->mmio = ioremap(LPCPU_BOOT_ADDR, 4);
	else
		lpcpu->mmio = ioremap(LPCPU_BOOT_ADDR+0x20000000, 4);

	if (!lpcpu->mmio) {
		pr_err("lpcpu ioremap fail.\n");
		ret = -ENOMEM;
		goto err_mmio;
	}

	ret = lpcpu_boot_status(lpcpu->mbox_channel);
	if (ret < 0) {
		dev_err(dev, "Send boot message to lpcpu via mailbox failed!\n");
		goto err_mmio;
	}

	timeout = wait_event_timeout(lpcpu->waitq,
			load_event == FW_LOAD_SUCC,usecs_to_jiffies(100000));

	if (!timeout) {
		dev_err(dev, "Lpcpu is not boot!\n");
		ret = -EBUSY;
		goto err_mmio;
	}

	ret = lpcpu_config_prepare(pdev);
	if (ret < 0) {
		dev_warn(dev, "Prepare lpcpu config failed!\n");
		goto finish_probe;
	}

	ret = lpcpu_config_send(lpcpu->mbox_channel);
	if (ret < 0) {
		dev_warn(dev, "Send config message to lpcpu via mailbox failed!\n");
		goto finish_probe;
	}

	timeout = wait_event_timeout(lpcpu->waitq,
			load_event == CFG_RECV_SUCC,usecs_to_jiffies(100000));

	if (!timeout) {
		dev_warn(dev, "Send config to lpcpu not ack!\n");
	}

finish_probe:
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
