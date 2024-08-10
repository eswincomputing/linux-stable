#ifndef __DTS_PARSER_H__
#define __DTS_PARSER_H__

#define VDEC_MAX_SUBSYS 4
#define	VDEC_MAX_CORE 12
#define VDEC_ADDR_OFFSET_MASK 0xffff

int vdec_device_nodes_check(void);
extern struct SubsysDesc subsys_array[VDEC_MAX_SUBSYS];
extern struct CoreDesc core_array[VDEC_MAX_CORE];
extern u8 numa_id_array[4];
extern int vdec_trans_device_nodes(struct platform_device *pdev, u8 numa_id);

#endif /* __DTS_PARSER_H__ */
