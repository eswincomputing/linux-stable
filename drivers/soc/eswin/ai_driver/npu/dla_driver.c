#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of_irq.h>
#include "dla_driver.h"

void dla_reg_write(struct nvdla_device *dev, uint32_t addr, uint32_t value)
{
	writel(value, dev->base + addr);
}

uint32_t dla_reg_read(struct nvdla_device *dev, uint32_t addr)
{
	return readl(dev->base + addr);
}
