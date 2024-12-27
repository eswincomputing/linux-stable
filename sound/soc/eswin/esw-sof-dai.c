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

#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <sound/pcm_params.h>
#include <linux/dma-map-ops.h>
#include "esw-dai.h"

static void esw_sof_dma_tx_callback(void *arg)
{
	struct i2s_dev *chip = arg;
	struct esw_ring_buffer *rb = (struct esw_ring_buffer *)chip->pos[0];
	struct snd_pcm_substream *substream = chip->tx_substream;
	int count = 0;

	if (!rb) {
		dev_err(chip->dev, "pos addr is NULL\n");
		return;
	}

	while(1) {
		if (rb->read + snd_pcm_lib_period_bytes(substream) <= rb->write) {
			rb->read += snd_pcm_lib_period_bytes(substream);
			dev_dbg(chip->dev, "read pos:%lld, write pos:%lld, buffer size:%lld\n",
					rb->read, rb->write, rb->buffer_size);
			break;
		}
		if (count++ == 10000) {
			dev_warn(chip->dev, "dsp update pos timeout, read pos:%lld, write pos:%lld, buffer size:%lld\n",
					rb->read, rb->write, rb->buffer_size);
			break;
		}
	}
}

static void esw_sof_dma_rx_callback(void *arg)
{
	struct i2s_dev *chip = arg;
	struct esw_ring_buffer *rb = (struct esw_ring_buffer *)chip->pos[1];
	struct snd_pcm_substream *substream = chip->rx_substream;

	if (!rb) {
		dev_err(chip->dev, "pos addr is NULL\n");
		return;
	}

	rb->write += snd_pcm_lib_period_bytes(substream);
	dev_dbg(chip->dev, "read pos:%lld, write pos:%lld, buffer size:%lld\n",
			 rb->read, rb->write, rb->buffer_size);
}

static int esw_sof_dma_prepare_and_submit(struct i2s_dev *chip,
										  struct snd_pcm_substream *substream)
{
	struct dma_chan *chan = chip->chan[substream->stream];
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction direction;
	unsigned long flags = DMA_CTRL_ACK;
	dma_addr_t dma_addr;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		direction = DMA_MEM_TO_DEV;
		dma_addr = chip->rb_out_iova;
	} else {
		direction = DMA_DEV_TO_MEM;
		dma_addr = chip->rb_in_iova;
	}

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	desc = dmaengine_prep_dma_cyclic(chan, dma_addr,
									 snd_pcm_lib_buffer_bytes(substream),
									 snd_pcm_lib_period_bytes(substream),
									 direction, flags);

	if (!desc)
		return -ENOMEM;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		desc->callback = esw_sof_dma_tx_callback;
	} else {
		desc->callback = esw_sof_dma_rx_callback;
	}

	desc->callback_param = chip;
	chip->cookie[substream->stream] = dmaengine_submit(desc);

	return 0;
}

int esw_sof_dma_open(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct i2s_dev *chip = dev_get_drvdata(component->dev);
	struct dma_chan *chan = chip->chan[substream->stream];

	if (!chan) {
		dev_err(component->dev,	"%s dma channel is null\n", __func__);
		return -ENXIO;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		chip->tx_substream = substream;
		memset(chip->pos[0], 0, 8);
	} else {
		chip->rx_substream = substream;
		memset(chip->pos[1], 0, 8);
	}

	return 0;
}

static int esw_sof_dma_prepare_slave_config(struct i2s_dev *chip,
											struct snd_pcm_substream *substream,
											struct snd_pcm_hw_params *params,
											struct dma_slave_config *slave_config)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct esw_i2s_dma_data *dma_data;
	enum dma_slave_buswidth buswidth;
	int bits;

	if (rtd->dai_link->num_cpus > 1) {
		dev_err(rtd->dev,
			"%s doesn't support Multi CPU yet\n", __func__);
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dma_data = &chip->play_dma_data;
	else if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		dma_data = &chip->capture_dma_data;

	bits = params_physical_width(params);
	if (bits < 8 || bits > 64)
		return -EINVAL;
	else if (bits == 8)
		buswidth = DMA_SLAVE_BUSWIDTH_1_BYTE;
	else if (bits == 16)
		buswidth = DMA_SLAVE_BUSWIDTH_2_BYTES;
	else if (bits == 24)
		buswidth = DMA_SLAVE_BUSWIDTH_3_BYTES;
	else if (bits <= 32)
		buswidth = DMA_SLAVE_BUSWIDTH_4_BYTES;
	else
		buswidth = DMA_SLAVE_BUSWIDTH_8_BYTES;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		slave_config->direction = DMA_MEM_TO_DEV;
		slave_config->dst_addr_width = buswidth;
	} else {
		slave_config->direction = DMA_DEV_TO_MEM;
		slave_config->src_addr_width = buswidth;
	}

	slave_config->device_fc = false;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		slave_config->dst_addr = dma_data->addr;
		slave_config->dst_maxburst = dma_data->max_burst;
		if (dma_data->addr_width != DMA_SLAVE_BUSWIDTH_UNDEFINED)
			slave_config->dst_addr_width = dma_data->addr_width;
	} else {
		slave_config->src_addr = dma_data->addr;
		slave_config->src_maxburst = dma_data->max_burst;
		if (dma_data->addr_width != DMA_SLAVE_BUSWIDTH_UNDEFINED)
			slave_config->src_addr_width = dma_data->addr_width;
	}

	return 0;
}

int esw_sof_dma_hw_params(struct snd_soc_component *component,
						  struct snd_pcm_substream *substream,
						  struct snd_pcm_hw_params *params)
{
	struct i2s_dev *chip = dev_get_drvdata(component->dev);
	struct dma_slave_config slave_config;
	int ret;

	dev_dbg(chip->dev, "%s\n", __func__);

	memset(&slave_config, 0, sizeof(slave_config));

	ret = esw_sof_dma_prepare_slave_config(chip, substream, params, &slave_config);
	if (ret)
		return ret;

	ret = dmaengine_slave_config(chip->chan[substream->stream], &slave_config);
	if (ret)
		return ret;

	return 0;
}

int esw_sof_dma_close(struct snd_soc_component *component,
					  struct snd_pcm_substream *substream)
{
	struct i2s_dev *chip = dev_get_drvdata(component->dev);

	dmaengine_synchronize(chip->chan[substream->stream]);
	return 0;
}

int esw_sof_dma_trigger(struct snd_soc_component *component,
						struct snd_pcm_substream *substream, int cmd)
{
	struct i2s_dev *chip = dev_get_drvdata(component->dev);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dma_chan *chan = chip->chan[substream->stream];
	int ret;

	dev_dbg(chip->dev, "%s, cmd:%d\n", __func__, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		ret = esw_sof_dma_prepare_and_submit(chip, substream);
		if (ret) {
			dev_err(chip->dev, "dma prepare and submit error\n");
			return ret;
		}
		dma_async_issue_pending(chan);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dmaengine_resume(chan);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (runtime->info & SNDRV_PCM_INFO_PAUSE)
			dmaengine_pause(chan);
		else
			dmaengine_terminate_async(chan);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dmaengine_pause(chan);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dmaengine_terminate_async(chan);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int esw_sof_dma_init(struct i2s_dev *chip)
{
	struct dma_chan *chan0;
	struct dma_chan *chan1;
	const char *channel_names0 = "rx";
	const char *channel_names1 = "tx";

	dev_dbg(chip->dev, "%s\n", __func__);

	chan0 = dma_request_chan(chip->dev, channel_names0);
	if (IS_ERR(chan0)) {
		if (PTR_ERR(chan0) == -EPROBE_DEFER) {
			dev_err(chip->dev, "request dma channel[%d] failed\n", SNDRV_PCM_STREAM_CAPTURE);
			return -EPROBE_DEFER;
		}
		dev_err(chip->dev, "dma channel[%d] is NULL\n", SNDRV_PCM_STREAM_CAPTURE);
	} else {
		chip->chan[SNDRV_PCM_STREAM_CAPTURE] = chan0;
	}

	chan1 = dma_request_chan(chip->dev, channel_names1);
	if (IS_ERR(chan1)) {
		if (PTR_ERR(chan1) == -EPROBE_DEFER) {
			dev_err(chip->dev, "request dma channel[%d] failed\n", SNDRV_PCM_STREAM_PLAYBACK);
			return -EPROBE_DEFER;
		}
		dev_err(chip->dev, "dma channel[%d] is NULL\n", SNDRV_PCM_STREAM_PLAYBACK);
	} else {
		chip->chan[SNDRV_PCM_STREAM_PLAYBACK] = chan1;
	}

	return 0;
}