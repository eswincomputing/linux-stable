#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/string.h>

#include "subsys.h"
#include "dts_parser.h"

#define LOG_TAG DEC_DEV_NAME ":dtsp"
#include "vc_drv_log.h"

struct SubsysDesc subsys_array[VDEC_MAX_SUBSYS] = {0};
struct CoreDesc core_array[VDEC_MAX_CORE] = {0};
u8 numa_id_array[4] = {0};

static int vdec_device_node_scan(unsigned char *compatible)
{
	struct property *prop;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, compatible);
	if (!np) {
		LOG_INFO("Unable to find device %s", compatible);
		return -1;
	}

	for_each_property_of_node(np, prop) {
		if (!strcmp(prop->name, "status")) {
			if (!strcmp((char *)prop->value, "disabled")) {
				LOG_INFO("One vdec device disabled on d2d\n");
				return -1;
			}
		}
	}

	of_node_put(np);

	return 0;
}

int vdec_device_nodes_check(void)
{
	int i, vdec_dev_num = 0;
	unsigned char compatible[32] = {0};

	for (i = 0; i < 2; i++) {
		sprintf(compatible, "eswin,video-decoder%d", i);
		if(!vdec_device_node_scan(compatible))
			vdec_dev_num++;
	}

	return vdec_dev_num;
}

#define VDEC_CORE_ARRAY_ASSIGN(index, id, type, addr, size, irqnum) do { \
		core_array[index].subsys = id; \
		core_array[index].core_type = type; \
		core_array[index].offset = (addr & VDEC_ADDR_OFFSET_MASK); \
		core_array[index].iosize = size; \
		core_array[index].irq = irqnum; \
		index++; \
	} while (0)

int vdec_trans_device_nodes(struct platform_device *pdev, u8 numa_id)
{
	extern unsigned int vcmd;
	int jpeg = 0;
	static int subsys_id = 0;
	static int core_index = 0;
	struct fwnode_handle *child;
	unsigned int vcmd_addr[2] = {0}, axife_addr[2] = {0}, vdec_addr[2] = {0};

	if (of_property_read_u32_array(pdev->dev.of_node, "vcmd-core", vcmd_addr, 2)) {
		vcmd = 0;
		LOG_ERR("VCMD core not found\n");
	}

	if (of_property_read_u32_array(pdev->dev.of_node, "axife-core", axife_addr, 2)) {
		LOG_ERR("AXIFE core not found\n");
		return -1;
	}

	if (of_property_read_u32_array(pdev->dev.of_node, "vdec-core", vdec_addr, 2)) {
		LOG_ERR("Decoder core not found\n");
		return -1;
	}

	device_for_each_child_node(&pdev->dev, child) {
		const char *core_name;
		int hw_type = HW_VC8000D;
		unsigned int base_addr, child_irq;

		if (fwnode_property_read_string(child, "core-name", &core_name)) {
			LOG_ERR("Video decoder core name not found.\n");
			fwnode_handle_put(child);
			return -1;
		}

		if (fwnode_property_read_u32(child, "base-addr", &base_addr)) {
			LOG_ERR("Video decoder base addr not found.\n");
			fwnode_handle_put(child);
			return -1;
		}

		child_irq = fwnode_irq_get(child, 0);
		if (child_irq <= 0) {
			LOG_INFO("irq get failed, polling mode instead.\n");
			child_irq = -1;
		}
		LOG_INFO("mapped irq is %d\n", child_irq);

		subsys_array[subsys_id].slice_index = 0;
		subsys_array[subsys_id].index = subsys_id;
		subsys_array[subsys_id].base = base_addr;

		if (vcmd) {
			VDEC_CORE_ARRAY_ASSIGN(core_index, subsys_id, HW_VCMD, (base_addr + vcmd_addr[0]), vcmd_addr[1], child_irq);
			child_irq = -1;
		}

		VDEC_CORE_ARRAY_ASSIGN(core_index, subsys_id, HW_AXIFE, (base_addr + axife_addr[0]), axife_addr[1], -1);

		if (strstr(core_name, "jpeg"))
			jpeg = 1;

		if (jpeg && vcmd)
			hw_type = HW_VC8000DJ;

		numa_id_array[subsys_id] = numa_id;
		VDEC_CORE_ARRAY_ASSIGN(core_index, subsys_id, hw_type, (base_addr + vdec_addr[0]), vdec_addr[1], child_irq);
		subsys_id++;
	}

	return 0;
}

