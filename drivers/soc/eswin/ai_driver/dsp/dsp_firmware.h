// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef DSP_FIRMWARE_H
#define DSP_FIRMWARE_H

struct es_dsp;

#if IS_ENABLED(CONFIG_FW_LOADER)
int dsp_request_firmware(struct es_dsp *dsp);
void dsp_release_firmware(struct es_dsp *dsp);
#else
static inline int dsp_request_firmware(struct es_dsp *dsp)
{
	(void)xvp;
	return -EINVAL;
}

static inline void dsp_release_firmware(struct es_dsp *dsp)
{
}
#endif

#endif
