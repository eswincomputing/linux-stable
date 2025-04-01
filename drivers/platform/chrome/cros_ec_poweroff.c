#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_data/cros_ec_commands.h>

#define DRV_NAME "cros-ec-poweroff"

static struct cros_ec_device *ec_dev;
static void (*orig_pm_power_off)(void);

struct cros_ec_poweroff_async {
	struct spi_message msg;
	struct spi_transfer xfer;
	u8 tx_buf[8];
	u8 rx_buf[8];
	int status;
};

static void cros_ec_poweroff_complete(void *arg)
{
	struct cros_ec_poweroff_async *async = arg;
	async->status = async->msg.status;
	dev_dbg(ec_dev->dev, "SPI transfer completed with status: %d\n",
		async->status);
	kfree(async);
}

static int cros_ec_send_shutdown_async(struct cros_ec_device *ec)
{
	struct spi_device *spi;
	struct cros_ec_poweroff_async *async;
	struct cros_ec_command msg = { 0 };
	int len, ret;

	if (!ec) {
		dev_err(ec->dev, "EC device not available\n");
		return -ENODEV;
	}
	if (!ec->dev) {
		dev_err(ec->dev, "EC device structure is invalid\n");
		return -EINVAL;
	}

	spi = to_spi_device(ec->dev);
	if (!spi) {
		dev_err(ec->dev, "Not an SPI-based EC device\n");
		return -EINVAL;
	}

	async = kzalloc(sizeof(*async), GFP_ATOMIC);
	if (!async) {
		dev_err(ec->dev, "Failed to allocate async struct\n");
		return -ENOMEM;
	}

	msg.version = 0;
	msg.command = EC_CMD_REBOOT;
	msg.outsize = 2;
	msg.insize = 0;
	msg.data[0] = EC_REBOOT_HIBERNATE;
	msg.data[1] = 0;

	len = cros_ec_prepare_tx(ec, &msg);
	if (len < 0) {
		dev_err(ec->dev, "Failed to prepare TX data\n");
		kfree(async);
		return len;
	}

	memcpy(async->tx_buf, ec->dout, len);

	async->xfer.tx_buf = async->tx_buf;
	async->xfer.rx_buf = async->rx_buf;
	async->xfer.len = len;
	spi_message_init(&async->msg);
	spi_message_add_tail(&async->xfer, &async->msg);

	async->msg.complete = cros_ec_poweroff_complete;
	async->msg.context = async;

	dev_dbg(ec->dev, "Sending async shutdown command to EC\n");
	ret = spi_async(spi, &async->msg);
	if (ret < 0) {
		dev_err(ec->dev, "SPI async failed: %d\n", ret);
		kfree(async);
		return ret;
	}

	dev_info(ec->dev, "Shutdown command queued to EC via SPI\n");
	return 0;
}

static void cros_ec_power_off(void)
{
	int ret;

	if (!ec_dev || !ec_dev->dev) {
		if (orig_pm_power_off)
			orig_pm_power_off();
		return;
	}

	ret = cros_ec_send_shutdown_async(ec_dev);
	if (ret < 0) {
		dev_err(ec_dev->dev,
			"EC power off failed, falling back to original\n");
		if (orig_pm_power_off)
			orig_pm_power_off();
	} else {
		dev_info(ec_dev->dev, "System halted via EC\n");
		mdelay(1000);
		while (1)
			cpu_relax();
	}
}

static int cros_ec_poweroff_probe(struct platform_device *pdev)
{
	struct spi_device *spi;

	dev_dbg(&pdev->dev, "Probing cros_ec_poweroff\n");

	ec_dev = dev_get_drvdata(pdev->dev.parent);
	if (!ec_dev) {
		dev_err(&pdev->dev, "Failed to get cros_ec_device\n");
		return -ENODEV;
	}

	spi = to_spi_device(ec_dev->dev);
	if (!spi) {
		dev_err(&pdev->dev, "EC device is not SPI-based\n");
		return -EINVAL;
	}

	orig_pm_power_off = pm_power_off;
	pm_power_off = cros_ec_power_off;

	dev_info(&pdev->dev, "CROS EC poweroff driver registered (SPI)\n");
	return 0;
}

static int cros_ec_poweroff_remove(struct platform_device *pdev)
{
	if (pm_power_off == cros_ec_power_off)
		pm_power_off = orig_pm_power_off;

	ec_dev = NULL;
	dev_info(&pdev->dev, "CROS EC poweroff driver unregistered\n");
	return 0;
}

static const struct of_device_id cros_ec_poweroff_of_match[] = {
	{ .compatible = "google,cros-ec-poweroff" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cros_ec_poweroff_of_match);

static struct platform_driver cros_ec_poweroff_driver = {
	.probe = cros_ec_poweroff_probe,
	.remove = cros_ec_poweroff_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = cros_ec_poweroff_of_match,
	},
};

module_platform_driver(cros_ec_poweroff_driver);

MODULE_SOFTDEP("pre: cros_ec_dev cros_ec_spi");
MODULE_DESCRIPTION("Chrome OS EC Poweroff Driver via SPI");
MODULE_AUTHOR("WangYang <yang.wang@deepcomputing.io>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
