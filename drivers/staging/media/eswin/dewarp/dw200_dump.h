#ifndef _DW200_DUMP_H__
#define _DW200_DUMP_H__
#include <linux/io.h>

#ifdef _ES_DW200_DEBUG_PRINT
#define DEBUG_PRINT(...) printk(__VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

void printDweInfo(struct dwe_hw_info *pDweHwInfo);
void printVseInfo(struct vse_params *pVseHwInfo);
void readVseReg(struct dw200_subdev *pdwe_dev);

#endif