#ifndef _NPU_DSP_H_
#define _NPU_DSP_H_
#include "internal_interface.h"

/* npu add dsp device info. */
int resolve_dsp_data(struct win_executor *executor);

/* npu set frame dsp io info */
int npu_set_dsp_iobuf(struct win_executor *executor, struct host_frame_desc *f);

/* when destroy model, need destroy dsp info */
void dsp_resource_destroy(struct win_executor * executor);

/* when release frame info ,need free dsp io info */
void destroy_frame_dsp_info(struct win_executor *executor, struct host_frame_desc *f);

#endif
