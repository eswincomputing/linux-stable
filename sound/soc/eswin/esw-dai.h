// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN audio driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * Authors: DengLei <denglei@eswincomputing.com>
 */

#ifndef __ESWIN_DAI_H__
#define __ESWIN_DAI_H__

#include <sound/pcm.h>
#include <sound/soc.h>
#include "esw-i2s.h"

struct esw_ring_buffer {
	uint64_t write;
	uint64_t read;
	uint64_t buffer_size;
};

/* ringbuffer total size 2M
 * |<--comp p 512k-->|<--comp c 512k-->|<--pcm p 512k-->|<--pcm c 512k-->|
 */
#define DSP_RB_COMP_SIZE         0x100000
#define DSP_RB_PCM_P_SZIE        0x80000
#define DSP_RB_PCM_C_SZIE        0x80000
#define DSP_RB_POS_SZIE          sizeof(struct esw_ring_buffer)
#define DSP_RB_DATA_SZIE         (DSP_RB_PCM_P_SZIE - DSP_RB_POS_SZIE)

#define DSP_RING_BUFFER_IOVA     0xff600000

int esw_sof_dma_init(struct i2s_dev *chip);
int esw_sof_dma_open(struct snd_soc_component *component,
		     struct snd_pcm_substream *substream);

int esw_sof_dma_trigger(struct snd_soc_component *component,
			struct snd_pcm_substream *substream, int cmd);

int esw_sof_dma_hw_params(struct snd_soc_component *component,
			  struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params);

int esw_sof_dma_close(struct snd_soc_component *component,
		      struct snd_pcm_substream *substream);

int esw_pcm_dma_dai_register(struct i2s_dev *chip);

#endif