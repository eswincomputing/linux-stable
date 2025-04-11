#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>

#include "soc_ioctl.h"
#include "vivsoc_dvp2axi.h"
#include "vivsoc_hub.h"

/*
 * Save frame number from sensor, buffer size should be +1 frame size
 */
#define SENSOR_FRAME_NUM 1

struct dvp2axi_mem_attr {
	unsigned int vchn_mask;
	unsigned int chn0_reg;
	unsigned int chn1_reg;
	unsigned int chn2_reg;
	unsigned int chn0_baddr;
	unsigned int chn1_baddr;
	unsigned int chn2_baddr;
	unsigned int frame_size;
	unsigned int intrid;
};

#define GET_VIRT_CHN(vchn_mask, vchn) do { \
	if (vchn_mask & 0x1) { vchn = 0; } \
	if (vchn_mask & 0x2) { vchn = 1; } \
	if (vchn_mask & 0x4) { vchn = 2; } \
} while(0)

#define DVP2AXI_IRQ_HANDLE(intr, flush_mask, done_mask, csr0, csr1, csr2, mask_reg, flush_shift, done_shift) do { \
		unsigned int vchn = 0; \
		soc_dev->soc_access.read(soc_dev, intr, &int_status); \
		if (int_status & flush_mask) { \
			mem_attr.intrid = intrid; \
			mem_attr.vchn_mask = (int_status & flush_mask) >> flush_shift; \
			mem_attr.frame_size = soc_dev->dvp2axi_framesize[intrid]; \
			mem_attr.chn0_reg = csr0; \
			mem_attr.chn1_reg = csr1; \
			mem_attr.chn2_reg = csr2; \
			soc_dev->soc_access.read(soc_dev, csr0, &mem_attr.chn0_baddr); \
			soc_dev->soc_access.read(soc_dev, csr1, &mem_attr.chn1_baddr); \
			soc_dev->soc_access.read(soc_dev, csr2, &mem_attr.chn2_baddr); \
			dvp2axi_update_mem_addr(soc_dev, &mem_attr); \
			GET_VIRT_CHN(mem_attr.vchn_mask, vchn); \
			if (atomic_read(&soc_dev->dvp2axi_vchn_fidx[intrid][vchn]) >= SENSOR_FRAME_NUM) \
				vivsoc_hub_write_part(soc_dev, mask_reg, 1, flush_shift + vchn, 1); \
		} \
		if (int_status & done_mask) { \
			mem_attr.vchn_mask = (int_status & done_mask) >> done_shift; \
			GET_VIRT_CHN(mem_attr.vchn_mask, vchn); \
			if (atomic_read(&soc_dev->dvp2axi_vchn_didx[intrid][vchn]) >= SENSOR_FRAME_NUM) \
				vivsoc_hub_write_part(soc_dev, mask_reg, 1, done_shift + vchn, 1); \
			else { \
				wake_up_interruptible(&soc_dev->irq_wait); \
			} \
			atomic_inc(&soc_dev->dvp2axi_vchn_didx[intrid][vchn]); \
		} \
		soc_dev->soc_access.write(soc_dev, intr, int_status & (flush_mask | done_mask)); \
	} while(0)

static void dvp2axi_update_mem_addr(struct vvcam_soc_dev *dev, struct dvp2axi_mem_attr *mem_attr)
{
	if ((mem_attr->vchn_mask & 0x1) && mem_attr->chn0_baddr) {
		atomic_inc(&dev->dvp2axi_vchn_fidx[mem_attr->intrid][0]);
		if (atomic_read(&dev->dvp2axi_vchn_fidx[mem_attr->intrid][0]) <= SENSOR_FRAME_NUM) {
			dev->soc_access.write(dev, mem_attr->chn0_reg, mem_attr->chn0_baddr +
				    mem_attr->frame_size * atomic_read(&dev->dvp2axi_vchn_fidx[mem_attr->intrid][0]));
		}
	}

	if ((mem_attr->vchn_mask & 0x2) && mem_attr->chn1_baddr) {
		atomic_inc(&dev->dvp2axi_vchn_fidx[mem_attr->intrid][1]);
		if (atomic_read(&dev->dvp2axi_vchn_fidx[mem_attr->intrid][1]) <= SENSOR_FRAME_NUM) {
			dev->soc_access.write(dev, mem_attr->chn1_reg, mem_attr->chn1_baddr +
				    mem_attr->frame_size * atomic_read(&dev->dvp2axi_vchn_fidx[mem_attr->intrid][1]));
		}
	}

	if ((mem_attr->vchn_mask & 0x4) && mem_attr->chn2_baddr) {
		atomic_inc(&dev->dvp2axi_vchn_fidx[mem_attr->intrid][2]);
		if (atomic_read(&dev->dvp2axi_vchn_fidx[mem_attr->intrid][2]) <= SENSOR_FRAME_NUM) {
			dev->soc_access.write(dev, mem_attr->chn2_reg, mem_attr->chn2_baddr +
				    mem_attr->frame_size * atomic_read(&dev->dvp2axi_vchn_fidx[mem_attr->intrid][2]));
		}
	}
}

static irqreturn_t dvp2axi_irq_handler(int irq, void *dev_id)
{
	unsigned int int_status, i;
	unsigned int intrid = *(int *)dev_id;
	struct vvcam_soc_dev *soc_dev;
	struct dvp2axi_mem_attr mem_attr;

	if (intrid > 8) {
		pr_err("%s: Error interrupt id\n", __func__);
		return IRQ_NONE;
	}

	memset(&mem_attr, 0, sizeof(mem_attr));
	soc_dev = (struct vvcam_soc_dev *)container_of(dev_id,
			    struct vvcam_soc_dev, dvp2axi_intrid[intrid]);
	switch(intrid) {
	case 0:
		DVP2AXI_IRQ_HANDLE(VI_DVP2AXI_INT0, DVP0_FLUSH_MASK, DVP0_DONE_MASK,
			    VI_DVP2AXI_CTRL9_CSR, VI_DVP2AXI_CTRL10_CSR, VI_DVP2AXI_CTRL11_CSR, VI_DVP2AXI_INT_MASK0, 0, 9);
		break;
	case 1:
		DVP2AXI_IRQ_HANDLE(VI_DVP2AXI_INT0, DVP1_FLUSH_MASK, DVP1_DONE_MASK,
			    VI_DVP2AXI_CTRL12_CSR, VI_DVP2AXI_CTRL13_CSR, VI_DVP2AXI_CTRL14_CSR, VI_DVP2AXI_INT_MASK0, 3, 12);
		break;
	case 2:
		DVP2AXI_IRQ_HANDLE(VI_DVP2AXI_INT0, DVP2_FLUSH_MASK, DVP2_DONE_MASK,
			    VI_DVP2AXI_CTRL15_CSR, VI_DVP2AXI_CTRL16_CSR, VI_DVP2AXI_CTRL17_CSR, VI_DVP2AXI_INT_MASK0, 6, 15);
		break;
	case 3:
		DVP2AXI_IRQ_HANDLE(VI_DVP2AXI_INT1, DVP3_FLUSH_MASK, DVP3_DONE_MASK,
			    VI_DVP2AXI_CTRL18_CSR, VI_DVP2AXI_CTRL19_CSR, VI_DVP2AXI_CTRL20_CSR, VI_DVP2AXI_INT_MASK1, 0, 9);
		break;
	case 4:
		DVP2AXI_IRQ_HANDLE(VI_DVP2AXI_INT1, DVP4_FLUSH_MASK, DVP4_DONE_MASK,
			    VI_DVP2AXI_CTRL21_CSR, VI_DVP2AXI_CTRL22_CSR, VI_DVP2AXI_CTRL23_CSR, VI_DVP2AXI_INT_MASK1, 3, 12);
		break;
	case 5:
		DVP2AXI_IRQ_HANDLE(VI_DVP2AXI_INT1, DVP3_FLUSH_MASK, DVP3_DONE_MASK,
			    VI_DVP2AXI_CTRL24_CSR, VI_DVP2AXI_CTRL25_CSR, VI_DVP2AXI_CTRL26_CSR, VI_DVP2AXI_INT_MASK1, 6, 15);
		break;
	case 6:
	case 7:
		unsigned int err_status, err_flag = 0;

		soc_dev->soc_access.read(soc_dev, VI_DVP2AXI_INT2, &err_status);
		if (err_status & 0x1) {
			err_flag = 1;
			if (atomic_read(&soc_dev->dvp2axi_errirq[0]) > 0)
				vivsoc_hub_write_part(soc_dev, VI_DVP2AXI_INT_MASK2, 1, 0, 1);
			atomic_inc(&soc_dev->dvp2axi_errirq[0]);
			pr_err("AXI id buffer full\n");
		}

		if (err_status & 0x2) {
			err_flag = 1;
			if (atomic_read(&soc_dev->dvp2axi_errirq[1]) > 0)
				vivsoc_hub_write_part(soc_dev, VI_DVP2AXI_INT_MASK2, 1, 1, 1);
			atomic_inc(&soc_dev->dvp2axi_errirq[1]);
			pr_err("AXI response error\n");
		}

		for (i = 2; i < 8; i++) {
			if (err_status & (0x1 << i)) {
				err_flag = 1;
				if (atomic_read(&soc_dev->dvp2axi_errirq[i]) > 0)
					vivsoc_hub_write_part(soc_dev, VI_DVP2AXI_INT_MASK2, 1, i, 1);
				atomic_inc(&soc_dev->dvp2axi_errirq[i]);
				pr_err("DVP%d frame error\n", (i - 2));
			}
		}

		if (err_flag) {
			soc_dev->soc_access.write(soc_dev, VI_DVP2AXI_INT2, err_status);
			atomic_inc(&soc_dev->irqc_err);
			wake_up_interruptible(&soc_dev->irq_wait);
		}

		if (err_status & 0x100) {
			pr_info("AXI id buffer is about to be full\n");
			if (atomic_read(&soc_dev->dvp2axi_errirq[8]) > 0)
				vivsoc_hub_write_part(soc_dev, VI_DVP2AXI_INT_MASK2, 1, 8, 1);
			atomic_inc(&soc_dev->dvp2axi_errirq[8]);
			soc_dev->soc_access.write(soc_dev, VI_DVP2AXI_INT2, err_status);
			return IRQ_HANDLED;
		}

		break;
	default:
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

int vvcam_soc_irq_init(struct platform_device *pdev, struct vvcam_soc_dev *soc_dev)
{
	int i;

	for (i = 0; i < 8; i++) {
		int ret;

		soc_dev->dvp2axi_irqnum[i] = platform_get_irq(pdev, i);
		soc_dev->dvp2axi_intrid[i] = i;

		ret = devm_request_irq(&pdev->dev, soc_dev->dvp2axi_irqnum[i],
			    dvp2axi_irq_handler, IRQF_SHARED | IRQF_ONESHOT, "dvp2axi_irq", (void *)&soc_dev->dvp2axi_intrid[i]);
		if (ret < 0) {
			pr_err("IRQ request failed for id %d\n", i);
			return -1;
		}
	}

	return 0;
}
