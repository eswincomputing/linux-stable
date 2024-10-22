// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN PCIe root complex driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0
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
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <dla_interface.h>
#include <dla_sched.h>
#include <dla_log.h>
#include "internal_interface.h"
#include "dla_buffer.h"
#include "debug.h"

static int config_sequence_setup(struct win_executor *executor, int op_num,
				 s16 *cfg_seq[])
{
	s16 i, j, pcer, op_type, op_idx;
	struct dla_task *task = executor->task;
	struct processors_interface *pcer_interface;
	int ret, total = 0;
	int rdma;
	u16 op_type_pos[NUM_OP_TYPE] = { 0 };

	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		executor->head_op_idx[i] = INVALID_OP_IDX;
	}
	memset(executor->op_num, 0, sizeof(executor->op_num));
	/* head_op setup */
	for (i = 0; i < op_num; i++) {
		op_type = executor->task->common_desc[i].op_type;
		op_idx = executor->task->common_desc[i].index;
		pcer = processor_dla_convert[op_type];
		executor->op_num[pcer]++;
		if (executor->head_op_idx[pcer] == INVALID_OP_IDX) {
			executor->head_op_idx[pcer] = op_idx;
		}
	}
	for (i = IDX_START; i < NUM_OP_TYPE; i++) {
		total += executor->op_num[i];
		dla_debug(
			"%s %d executor->op_num[%d] %d executor->head_op_idx[%d] %d\n",
			__func__, __LINE__, i, executor->op_num[i], i,
			executor->head_op_idx[i]);
	}
	if (total != op_num) {
		dla_error("%s %d BUG op sum %d != op_num %d\n", __func__,
			  __LINE__, total, op_num);
		return -1;
	}

	/* cfg_seq pointer offset */
	cfg_seq[IDX_EDMA] = cfg_seq[IDX_START];
	cfg_seq[IDX_CONV] = cfg_seq[IDX_EDMA] + executor->op_num[IDX_EDMA];
	cfg_seq[IDX_SDP] = cfg_seq[IDX_CONV] + executor->op_num[IDX_CONV];
	cfg_seq[IDX_PDP] = cfg_seq[IDX_SDP] + executor->op_num[IDX_SDP];
	cfg_seq[IDX_RUBIK] = cfg_seq[IDX_PDP] + executor->op_num[IDX_PDP];
	cfg_seq[IDX_KMD_DSP0] =
		cfg_seq[IDX_RUBIK] + executor->op_num[IDX_RUBIK];
	cfg_seq[IDX_KMD_DSP1] =
		cfg_seq[IDX_KMD_DSP0] + executor->op_num[IDX_KMD_DSP0];
	cfg_seq[IDX_KMD_DSP2] =
		cfg_seq[IDX_KMD_DSP1] + executor->op_num[IDX_KMD_DSP1];
	cfg_seq[IDX_KMD_DSP3] =
		cfg_seq[IDX_KMD_DSP2] + executor->op_num[IDX_KMD_DSP2];
	cfg_seq[IDX_EVENT_SINK] =
		cfg_seq[IDX_KMD_DSP3] + executor->op_num[IDX_KMD_DSP3];
	cfg_seq[IDX_EVENT_SOURCE] =
		cfg_seq[IDX_EVENT_SINK] + executor->op_num[IDX_EVENT_SINK];

	for (i = 0; i < op_num; i++) {
		op_type = executor->task->common_desc[i].op_type;
		op_idx = executor->task->common_desc[i].index;
		ASSERT(op_idx != INVALID_OP_IDX && op_type < HW_OP_NUM);
		pcer = processor_dla_convert[op_type];
		j = op_type_pos[pcer];
		cfg_seq[pcer][j] = op_idx;
		pcer_interface = executor->engine->processors[pcer];
		ret = pcer_interface->tensor_unfold(executor, op_idx,
						    &task->op_desc[op_idx],
						    &task->surface_desc[op_idx],
						    executor->tensor_set[pcer],
						    j);
		if (pcer_interface->rdma_check != NULL) {
			rdma = pcer_interface->rdma_check(
				NULL, &task->op_desc[op_idx],
				&task->surface_desc[op_idx]);
		}

		ret = pcer_interface->prepare_prog_data(
			executor, rdma, j, op_idx, &task->op_desc[op_idx],
			&task->surface_desc[op_idx]);
		op_type_pos[pcer]++;
	}

	dla_debug("%s, %d, done.\n", __func__, __LINE__);
	return 0;
}

static void npu_set_enable_consumer(npu_dep_info_t *npu_info, u16 cons)
{
	npu_info->enable_op_idx = cons;
}

static void npu_set_completion_consumer(npu_dep_info_t *npu_info, u32 type,
					u16 consumer_idx, u16 pos)
{
	npu_info->completion_event_bitmap |= 1U << type;
	npu_info->completion_op_idx[pos] = consumer_idx;
}

#define get_npu_info(pcer_t, PCER_T, k)                                   \
	({                                                                \
		pcer_t##_dev_t *tmp;                                      \
		tmp = (pcer_t##_dev_t *)                                  \
			      executor->prog_data_buf_bobj[IDX_##PCER_T]; \
		npu_info = (npu_dep_info_t *)&tmp[k];                     \
	})

static npu_dep_info_t *npu_get_dep_info(struct win_executor *executor, u8 pcer,
					int k)
{
	npu_dep_info_t *npu_info;
	switch (pcer) {
	case IDX_EDMA:
		get_npu_info(edma, EDMA, k);
		break;
	case IDX_CONV:
		npu_info = (npu_dep_info_t
				    *)((char *)executor
					       ->prog_data_buf_bobj[IDX_CONV] +
				       MAX_CONV_PROG_DATA_SIZE * k +
				       sizeof(rdma_dev_com_inf_t));
		break;
	case IDX_SDP:
		get_npu_info(sdp, SDP, k);
		break;
	case IDX_PDP:
		get_npu_info(pdp, PDP, k);
		break;
	case IDX_RUBIK:
		get_npu_info(rubik, RUBIK, k);
		break;
	case IDX_EVENT_SINK:
		get_npu_info(event_sink, EVENT_SINK, k);
		break;
	case IDX_EVENT_SOURCE:
		get_npu_info(event_source, EVENT_SOURCE, k);
		break;
	case IDX_KMD_DSP0:
		get_npu_info(dsp, KMD_DSP0, k);
		break;
	case IDX_KMD_DSP1:
		get_npu_info(dsp, KMD_DSP1, k);
		break;
	case IDX_KMD_DSP2:
		get_npu_info(dsp, KMD_DSP2, k);
		break;
	case IDX_KMD_DSP3:
		get_npu_info(dsp, KMD_DSP3, k);
		break;
	default:
		dla_error("get npu info err, pcer = %d, k = %d.\n", pcer, k);
		return NULL;
	}
	return npu_info;
}

static inline void init_npu_info(npu_dep_info_t *npu_info)
{
	int i;

	npu_info->enable_op_idx = invalid_op_index;
	for (i = 0; i < MAX_KMD_DEPCNT; i++) {
		npu_info->completion_op_idx[i] = invalid_op_index;
	}
}

static void dependency_consumer2producer(struct win_executor *executor,
					 struct dla_task *task, int op_num)
{
	u16 pcer_cnt[NUM_OP_TYPE];
	u8 pcer, op_type, event;
	u16 i, j, consumer;
	int k = 0;
	u16 pos = 0;
	u16 event_cnt[NUM_OP_TYPE];
	u8 *dependency_count = executor->dependency_count;
	npu_dep_info_t *npu_info;

	memset(pcer_cnt, 0, sizeof(pcer_cnt));
	memset(event_cnt, 0, sizeof(event_cnt));

	for (i = 0; i < op_num; i++) {
		pcer = processor_dla_convert[task->common_desc[i].op_type];
		dla_detail("i:%d op_type:%d\n", i,
			   task->common_desc[i].op_type);
		k = pcer_cnt[pcer];
		npu_info = npu_get_dep_info(executor, pcer, k);
		if (npu_info == NULL) {
			dla_error(
				"get processor %d, op_idx=%d, npu_info is null.\n",
				pcer, k);
			continue;
		}
		if (pcer != IDX_CONV)
			init_npu_info(npu_info);

		if (pcer == IDX_EVENT_SOURCE)
			dependency_count[i]++;

		for (j = IDX_SDP; j <= IDX_PDP; j++) {
			consumer = task->common_desc[i].fused_parent.index;
			event = task->common_desc[i].fused_parent.event;
			if (consumer != invalid_op_index &&
			    event == DLA_EVENT_OP_ENABLED) {
				npu_set_enable_consumer(npu_info, consumer);
			}
		}
		pos = 0;
		for (j = IDX_START; j < NUM_OP_TYPE; j++) {
			op_type = processor_idx_convert[j];
			consumer =
				task->common_desc[i].consumers[op_type].index;
			event = task->common_desc[i].consumers[op_type].event;
			if (consumer == 0xffff) {
				continue;
			}

			if (event == DLA_EVENT_OP_PROGRAMMED) {
				dla_detail(
					"i:%d j:%d consumer:%d dependency_count:%d\n",
					i, j, consumer,
					dependency_count[consumer]);
				dependency_count[consumer]--;
			}
			if (pcer == IDX_CONV) {
				continue;
			}

			if (event == DLA_EVENT_OP_COMPLETED) {
				if (unlikely(pos >= MAX_KMD_DEPCNT)) {
					dla_error(
						"op index:%d dependency cnt over max\n",
						i);
					BUG_ON(false);
					return;
				}
				npu_set_completion_consumer(npu_info, op_type,
							    consumer, pos);
				pos++;
			}

			/* Notice: fused_parent not cause dep bit
			 * sequence of op in op_list will ensure fly op likes
			 * sdp-conv trigger sequence.
			 */
		}
		dependency_count[i]++;
		dla_detail("i:%d dependency_count:%d\n", i,
			   dependency_count[i]);
		npu_info->depcnt = dependency_count[i];
		pcer_cnt[pcer]++;
	}
	return;
}

int generate_small_program(struct win_executor *executor)
{
	int ret = 0, op_num;

	op_num = executor->network->num_operations;
	executor->total_op_num = op_num;

	executor->cfg_seq[IDX_START] =
		kzalloc(op_num * sizeof(u16), GFP_KERNEL);
	if (executor->cfg_seq[IDX_START] == NULL) {
		ret = -ENOMEM;
		return ret;
	}
	dla_debug("%s, %d, op_num=%d.\n", __func__, __LINE__, op_num);
	ret = config_sequence_setup(executor, op_num, executor->cfg_seq);
	if (ret < 0) {
		dla_error("%s %d config_sequence_setup fail\n", __func__,
			  __LINE__);
		goto err_free1;
	}
	dependency_consumer2producer(executor, executor->task, op_num);

	return 0;
err_free1:
	memset(executor->cfg_seq[IDX_START], -1, op_num * sizeof(u16));
	kfree(executor->cfg_seq[IDX_START]);
	executor->cfg_seq[IDX_START] = NULL;
	return ret;
}

int generate_event_map(struct win_executor *executor)
{
	s16 i, op_type, op_idx;
	int op_num, event_idx;
	int ret;

	op_num = executor->network->num_operations;

	executor->total_event_sink_num = 0;
	executor->total_event_source_num = 0;

	for (i = 0; i < op_num; i++) {
		op_type = executor->task->common_desc[i].op_type;

		switch (op_type) {
		case IDX_EVENT_SINK:
			executor->total_event_sink_num++;
			break;
		case IDX_EVENT_SOURCE:
			executor->total_event_source_num++;
			break;
		default:
			continue;
		}
	}
	dla_detail("total_event_sink_num:%d total_event_source_num:%d\n",
		   executor->total_event_sink_num,
		   executor->total_event_source_num);
	executor->event_sink_map = kzalloc(
		executor->total_event_sink_num * sizeof(s16), GFP_KERNEL);
	executor->event_source_map = kzalloc(
		executor->total_event_source_num * sizeof(s16), GFP_KERNEL);

	if (executor->event_sink_map == NULL ||
	    executor->event_source_map == NULL) {
		ret = -ENOMEM;
		return ret;
	}

	for (i = 0; i < op_num; i++) {
		op_type = executor->task->common_desc[i].op_type;
		op_idx = executor->task->common_desc[i].index;

		switch (op_type) {
		case IDX_EVENT_SINK:
			event_idx = executor->task->op_desc[i].event_op.index;
			if (event_idx >= executor->total_event_sink_num) {
				dla_error(
					"Event Sink, event_idx(%d) is wrong in model.\n",
					event_idx);
				ret = -EINVAL;
				goto err_free;
			}
			executor->event_sink_map[event_idx] = op_idx;
			break;
		case IDX_EVENT_SOURCE:
			event_idx = executor->task->op_desc[i].event_op.index;
			if (event_idx >= executor->total_event_source_num) {
				dla_error(
					"Event Source, event_idx(%d) is wrong in model.\n",
					event_idx);
				ret = -EINVAL;
				goto err_free;
			}
			executor->event_source_map[event_idx] = op_idx;
			break;
		default:
			continue;
		}
	}

	return 0;
err_free:
	kfree(executor->event_sink_map);
	kfree(executor->event_source_map);
	executor->event_sink_map = NULL;
	executor->event_source_map = NULL;

	return ret;
}

int set_pause_op_done(struct win_executor *executor, kmd_dump_info_t *dump_info)
{
	int ret = 0, op_num;
	u16 pcer_cnt[NUM_OP_TYPE];
	u8 pcer;
	u16 op_index, i, j;
	int k = 0;
	npu_dep_info_t *npu_info;
	struct dla_task *task = executor->task;

	op_num = executor->network->num_operations;

	memset(pcer_cnt, 0, sizeof(pcer_cnt));
	for (i = 0; i < op_num; i++) {
		pcer = processor_dla_convert[task->common_desc[i].op_type];
		k = pcer_cnt[pcer];
		npu_info = npu_get_dep_info(executor, pcer, k);
		if (npu_info == NULL) {
			dla_error(
				"get processor %d, op_idx=%d, npu_info is null.\n",
				pcer, k);
			continue;
		}

		npu_info->notify_op_done = 0;
		npu_info->pause_op_done = 0;

		for (j = 0; j < dump_info->list_size; j++) {
			op_index = dump_info->op_idx_list[j];
			if (op_index > op_num) {
				continue;
			}

			if (op_index == task->common_desc[i].index) {
				npu_info->notify_op_done = 1;
				npu_info->pause_op_done = 1;
				dla_debug("%s, %d, dump op_index:%d\n",
					  __func__, __LINE__, op_index);
				break;
			}
		}

		pcer_cnt[pcer]++;
	}
	dla_debug("%s, %d, ret=%d.\n\n", __func__, __LINE__, ret);

	return ret;
}

int reset_pause_op_done(struct win_executor *executor)
{
	int ret = 0, op_num;
	u16 pcer_cnt[NUM_OP_TYPE];
	u8 pcer;
	u16 i;
	int k = 0;
	npu_dep_info_t *npu_info;
	struct dla_task *task = executor->task;

	op_num = executor->network->num_operations;

	memset(pcer_cnt, 0, sizeof(pcer_cnt));
	for (i = 0; i < op_num; i++) {
		pcer = processor_dla_convert[task->common_desc[i].op_type];
		k = pcer_cnt[pcer];
		npu_info = npu_get_dep_info(executor, pcer, k);
		if (npu_info == NULL) {
			dla_error(
				"get processor %d, op_idx=%d, npu_info is null.\n",
				pcer, k);
			continue;
		}

		npu_info->notify_op_done = 0;
		npu_info->pause_op_done = 0;

		pcer_cnt[pcer]++;
	}

	return ret;
}
