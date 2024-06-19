#ifndef _DW200_FE_H
#define _DW200_FE_H

#include "dwe_regs.h"
#include "vvdefs.h"
#include "dw200_subdev.h"

int dw200_fe_reset(struct dw200_subdev *dev);
int dw200_fe_init(struct dw200_subdev *dev);
int dw200_fe_destory(struct dw200_subdev *dev);
void dw200_fe_isp_irq_work(struct dw200_subdev *dev);
int dw200_fe_read_reg(struct dw200_subdev *dev, u32 offset, u32 *val);
int dw200_fe_write_reg(struct dw200_subdev *dev, u32 offset, u32 val);
int dw200_fe_write_tbl(struct dw200_subdev *dev, u32 offset, u32 val);

#endif