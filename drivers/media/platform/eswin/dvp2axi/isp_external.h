// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI isp_external driver
 *
 * Copyright 2025, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Eswin VI team
 */

#ifndef _ESISP_EXTERNAL_H
#define _ESISP_EXTERNAL_H

#define ESISP_VICAP_CMD_MODE \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 0, struct esisp_vicap_mode)

#define ESISP_VICAP_CMD_INIT_BUF \
	 _IOW('V', BASE_VIDIOC_PRIVATE + 1, int)

#define ESISP_VICAP_CMD_RX_BUFFER_FREE \
	 _IOW('V', BASE_VIDIOC_PRIVATE + 2, struct esisp_rx_buf)

#define ESISP_VICAP_CMD_QUICK_STREAM \
	_IOW('V', BASE_VIDIOC_PRIVATE + 3, int)

#define ESISP_VICAP_CMD_SET_RESET \
	 _IOW('V', BASE_VIDIOC_PRIVATE + 4, int)

#define ESISP_VICAP_CMD_SET_STREAM \
	 _IOW('V', BASE_VIDIOC_PRIVATE + 5, int)

#define ESISP_VICAP_BUF_CNT 3
#define ESISP_VICAP_BUF_CNT_MAX 8
#define ESISP_RX_BUF_POOL_MAX (ESISP_VICAP_BUF_CNT_MAX * 3)

struct esisp_vicap_input {
	u8 merge_num;
	u8 index;
};

enum esisp_vicap_link {
	ESISP_VICAP_ONLINE,
	ESISP_VICAP_RDBK_AIQ,
	ESISP_VICAP_RDBK_AUTO,
	ESISP_VICAP_RDBK_AUTO_ONE_FRAME,
};

struct esisp_vicap_mode {
	char *name;
	enum esisp_vicap_link rdbk_mode;

	struct esisp_vicap_input input;
};

enum rx_buf_type {
	BUF_SHORT,
	BUF_MIDDLE,
	BUF_LONG,
};

struct esisp_rx_buf {
	struct list_head list;
	struct dma_buf *dbuf;
	dma_addr_t dma;
	u64 timestamp;
	u32 sequence;
	u32 type;
	u32 runtime_us;

	bool is_init;
	bool is_first;

	bool is_resmem;
	bool is_switch;

	bool is_uncompact;
};

#endif
