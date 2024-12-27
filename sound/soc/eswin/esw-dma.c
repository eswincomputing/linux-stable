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
#include "esw-i2s.h"
#include "esw-dai.h"


static void esw_pcm_dma_complete(void *arg)
{
	unsigned int new_pos;
	struct snd_pcm_substream *substream = arg;
	struct i2s_dev *chip = substream->runtime->private_data;

	new_pos = chip->pcm_pos[substream->stream] + snd_pcm_lib_period_bytes(substream);
	if (new_pos >= snd_pcm_lib_buffer_bytes(substream))
		new_pos = 0;
	chip->pcm_pos[substream->stream] = new_pos;

	snd_pcm_period_elapsed(substream);
}

static int esw_pcm_dma_prepare_and_submit(struct i2s_dev *chip,
										  struct snd_pcm_substream *substream)
{
	struct dma_chan *chan = chip->chan[substream->stream];
	struct dma_async_tx_descriptor *desc;
	enum dma_transfer_direction direction;
	unsigned long flags = DMA_CTRL_ACK;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		direction = DMA_MEM_TO_DEV;
	else
		direction = DMA_DEV_TO_MEM;

	if (!substream->runtime->no_period_wakeup)
		flags |= DMA_PREP_INTERRUPT;

	chip->pcm_pos[substream->stream] = 0;
	desc = dmaengine_prep_dma_cyclic(chan,
				substream->runtime->dma_addr,
				snd_pcm_lib_buffer_bytes(substream) * 64 / substream->runtime->frame_bits,
				snd_pcm_lib_period_bytes(substream) * 64 / substream->runtime->frame_bits,
				direction, flags);

	if (!desc)
		return -ENOMEM;

	desc->callback = esw_pcm_dma_complete;
	desc->callback_param = substream;
	chip->cookie[substream->stream] = dmaengine_submit(desc);

	return 0;
}


int esw_pcm_dma_refine_runtime_hwparams(
			struct snd_pcm_substream *substream,
			struct snd_pcm_hardware *hw, struct dma_chan *chan)
{
	struct dma_slave_caps dma_caps;
	u32 addr_widths = BIT(DMA_SLAVE_BUSWIDTH_1_BYTE) |
			  BIT(DMA_SLAVE_BUSWIDTH_2_BYTES) |
			  BIT(DMA_SLAVE_BUSWIDTH_4_BYTES);
	snd_pcm_format_t i;
	int ret = 0;

	if (!hw || !chan)
		return -EINVAL;

	ret = dma_get_slave_caps(chan, &dma_caps);
	if (ret == 0) {
		if (dma_caps.cmd_pause && dma_caps.cmd_resume)
			hw->info |= SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME;
		if (dma_caps.residue_granularity <= DMA_RESIDUE_GRANULARITY_SEGMENT)
			hw->info |= SNDRV_PCM_INFO_BATCH;

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			addr_widths = dma_caps.dst_addr_widths;
		else
			addr_widths = dma_caps.src_addr_widths;
	}

	pcm_for_each_format(i) {
		int bits = snd_pcm_format_physical_width(i);

		/*
			* Enable only samples with DMA supported physical
			* widths
			*/
		switch (bits) {
		case 8:
		case 16:
		case 24:
		case 32:
		case 64:
			if (addr_widths & (1 << (bits / 8)))
				hw->formats |= pcm_format_to_bits(i);
			break;
		default:
			/* Unsupported types */
			break;
		}
	}

	return ret;
}

int esw_pcm_dma_open(struct snd_soc_component *component,
					 struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct i2s_dev *chip = dev_get_drvdata(component->dev);
	struct snd_pcm_hardware hw;
	struct dma_chan *chan = chip->chan[substream->stream];
	struct device *dma_dev = chan->device->dev;
	struct esw_i2s_dma_data *dma_data;

	dev_dbg(chip->dev, "%s\n", __func__);

	dma_data = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), substream);

	memset(&hw, 0, sizeof(hw));
	hw.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
			SNDRV_PCM_INFO_INTERLEAVED;
	hw.periods_min = 2;
	hw.periods_max = 16;
	hw.period_bytes_min = dma_data->max_burst * DMA_SLAVE_BUSWIDTH_8_BYTES;
	if (!hw.period_bytes_min)
		hw.period_bytes_min = 256;
	hw.period_bytes_max = dma_get_max_seg_size(dma_dev);
	hw.buffer_bytes_max = hw.period_bytes_max * 16;
	hw.fifo_size = dma_data->fifo_size;
	hw.info |= SNDRV_PCM_INFO_BATCH;

	esw_pcm_dma_refine_runtime_hwparams(substream, &hw, chan);

	substream->runtime->hw = hw;
	substream->runtime->private_data = chip;

	return 0;
}

static int esw_pcm_dma_prepare_slave_config(struct i2s_dev *chip,
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

int esw_pcm_dma_hw_params(struct snd_soc_component *component,
						  struct snd_pcm_substream *substream,
						  struct snd_pcm_hw_params *params)
{
	struct i2s_dev *chip = dev_get_drvdata(component->dev);
	struct dma_slave_config slave_config;
	int ret;

	dev_dbg(chip->dev, "%s, period size:%d, period cnt:%d\n", __func__,
			 params_period_size(params), params_periods(params));

	memset(&slave_config, 0, sizeof(slave_config));

	ret = esw_pcm_dma_prepare_slave_config(chip, substream, params, &slave_config);
	if (ret)
		return ret;

	ret = dmaengine_slave_config(chip->chan[substream->stream], &slave_config);
	if (ret)
		return ret;

	return 0;
}

int esw_pcm_dma_close(struct snd_soc_component *component,
					  struct snd_pcm_substream *substream)
{
	struct i2s_dev *chip = dev_get_drvdata(component->dev);

	dev_dbg(chip->dev, "%s\n", __func__);

	dmaengine_synchronize(chip->chan[substream->stream]);

	return 0;
}

int esw_pcm_dma_trigger(struct snd_soc_component *component,
						struct snd_pcm_substream *substream, int cmd)
{
	struct i2s_dev *chip = dev_get_drvdata(component->dev);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dma_chan *chan = chip->chan[substream->stream];
	int ret;

	dev_dbg(chip->dev, "%s, cmd:%d, sample bits:%d\n", __func__, cmd, runtime->sample_bits);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		ret = esw_pcm_dma_prepare_and_submit(chip, substream);
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

snd_pcm_uframes_t esw_pcm_dma_pointer(struct snd_soc_component *component,
									  struct snd_pcm_substream *substream)
{
	struct i2s_dev *chip =  substream->runtime->private_data;
	return bytes_to_frames(substream->runtime, chip->pcm_pos[substream->stream]);
}

static int esw_pcm_dma_process(struct snd_soc_component *component,
			       struct snd_pcm_substream *substream,
			       int channel, unsigned long hwoff,
			       struct iov_iter *iter, unsigned long bytes)
{
	struct i2s_dev *chip = container_of(component, struct i2s_dev, pcm_component);
	struct snd_pcm_runtime *runtime = substream->runtime;
	bool is_playback = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	char *dma_ptr = (char *)runtime->dma_area + hwoff * 64 / runtime->frame_bits +
			channel * (runtime->dma_bytes / runtime->channels);
	snd_pcm_uframes_t frames;
	u16 *ptr_16;
	u8 *ptr_24_raw;
	u32 *ptr_24;
	u32 *ptr_32;
	int i;

	if (is_playback) {
		if (runtime->sample_bits == 32) {
			if (copy_from_iter(dma_ptr, bytes, iter) != bytes)
				return -EFAULT;
		} else {
			if (copy_from_iter(chip->conv_buf[0], bytes, iter) != bytes)
				return -EFAULT;
			if (runtime->sample_bits == 16) {
				ptr_16 = (u16 *)chip->conv_buf[0];
				ptr_32 = (u32 *)dma_ptr;
				frames = bytes_to_frames(runtime, bytes);
				for (i = 0; i < 2 * frames; i++) {
					ptr_32[i] = (u32)ptr_16[i];
				}
			} else if (runtime->sample_bits == 24) {
				ptr_24_raw = (u8 *)chip->conv_buf[0];
				ptr_32 = (u32 *)dma_ptr;
				for (i = 0; i < bytes / 3; i++) {
					ptr_24 = (u32 *)(ptr_24_raw + i * 3);
					ptr_32[i] = (*ptr_24) & 0xffffff;
				}
			}
		}
	} else {
		if (runtime->sample_bits == 32) {
			if (copy_to_iter(dma_ptr, bytes, iter) != bytes)
				return -EFAULT;
		} else {
			if (runtime->sample_bits == 16) {
				frames = bytes_to_frames(runtime, bytes);
				ptr_16 = (u16 *)chip->conv_buf[1];
				ptr_32 = (u32 *)dma_ptr;
				for (i = 0; i < 2 * frames; i++) {
					ptr_16[i] = ptr_32[i];
				}
			} else if (runtime->sample_bits == 24) {
				ptr_24_raw = (u8 *)chip->conv_buf[1];
				ptr_32 = (u32 *)dma_ptr;
				for (i = 0; i < bytes / 3; i++) {
					memcpy(&ptr_24_raw[i * 3], &ptr_32[i], 3);
				}
			}
			if (copy_to_iter(chip->conv_buf[1], bytes, iter) != bytes)
				return -EFAULT;
		}
	}

	return 0;
}

static int esw_pcm_dma_new(struct snd_soc_component *component,
			     struct snd_soc_pcm_runtime *rtd)
{
	struct i2s_dev *chip = container_of(component, struct i2s_dev, pcm_component);
	size_t prealloc_buffer_size;
	size_t max_buffer_size;
	unsigned int i;

	prealloc_buffer_size = 512 * 1024;
	max_buffer_size = SIZE_MAX;

	for_each_pcm_streams(i) {
		struct snd_pcm_substream *substream = rtd->pcm->streams[i].substream;
		if (!substream)
			continue;

		snd_pcm_set_managed_buffer(substream,
				SNDRV_DMA_TYPE_DEV_IRAM,
				chip->chan[substream->stream]->device->dev,
				prealloc_buffer_size,
				max_buffer_size);
	}

	return 0;
}

static const struct snd_soc_component_driver esw_pcm_component_driver = {
	.name		= "esw-dma",
	.probe_order	= SND_SOC_COMP_ORDER_LATE,
	.open		= esw_pcm_dma_open,
	.close		= esw_pcm_dma_close,
	.hw_params	= esw_pcm_dma_hw_params,
	.trigger	= esw_pcm_dma_trigger,
	.pointer	= esw_pcm_dma_pointer,
	.copy		= esw_pcm_dma_process,
	.pcm_construct	= esw_pcm_dma_new,
};

int esw_pcm_dma_dai_register(struct i2s_dev *chip)
{
	struct dma_chan *chan0;
	struct dma_chan *chan1;
	const char *channel_names0 = "tx";
	const char *channel_names1 = "rx";
	int ret;
	u32 period_bytes_max;

	dev_dbg(chip->dev, "%s\n", __func__);

	chan0 = dma_request_chan(chip->dev, channel_names0);
	if (IS_ERR(chan0)) {
		if (PTR_ERR(chan0) == -EPROBE_DEFER) {
			dev_err(chip->dev, "request dma channel[%s] failed\n", channel_names0);
			return -EPROBE_DEFER;
		}
		dev_err(chip->dev, "dma channel[%s] is NULL\n", channel_names0);
	} else {
		chip->chan[SNDRV_PCM_STREAM_PLAYBACK] = chan0;
		period_bytes_max = dma_get_max_seg_size(chan0->device->dev);
		chip->conv_buf[SNDRV_PCM_STREAM_PLAYBACK] = kzalloc(period_bytes_max, GFP_KERNEL);
		if (!chip->conv_buf[SNDRV_PCM_STREAM_PLAYBACK]) {
			ret = -ENOMEM;
			dev_err(chip->dev, "alloc conv buf0 err:%d\n", ret);
			goto release_chan0;
		}
	}

	chan1 = dma_request_chan(chip->dev, channel_names1);
	if (IS_ERR(chan1)) {
		if (PTR_ERR(chan1) == -EPROBE_DEFER) {
			dev_err(chip->dev, "request dma channel[%s] failed\n", channel_names1);
			ret = -EPROBE_DEFER;
			goto release_buf0;
		}
		dev_err(chip->dev, "dma channel[%s] is NULL\n", channel_names1);
	} else {
		chip->chan[SNDRV_PCM_STREAM_CAPTURE] = chan1;
		period_bytes_max = dma_get_max_seg_size(chan1->device->dev);
		chip->conv_buf[SNDRV_PCM_STREAM_CAPTURE] = kzalloc(period_bytes_max, GFP_KERNEL);
		if (!chip->conv_buf[SNDRV_PCM_STREAM_CAPTURE]) {
			dev_err(chip->dev, "alloc conv buf0 err:%d\n", ret);
			ret = -ENOMEM;
			goto release_chan1;
		}
	}

	ret = snd_soc_component_initialize(&chip->pcm_component, &esw_pcm_component_driver, chip->dev);
	if (ret)
		goto release_buf1;

	ret = snd_soc_add_component(&chip->pcm_component, NULL, 0);
	if (ret) {
		dev_err(chip->dev, "add pcm component failed\n");
		goto release_buf1;
	}

	return 0;

release_buf1:
	kfree(chip->conv_buf[1]);
release_chan1:
	dma_release_channel(chip->chan[1]);
release_buf0:
	kfree(chip->conv_buf[0]);
release_chan0:
	dma_release_channel(chip->chan[0]);

	return ret;
}