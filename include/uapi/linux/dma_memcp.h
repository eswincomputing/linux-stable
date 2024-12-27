/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ESWIN DMA MEMCP Driver
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
 * SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
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
 * Authors: Zonglin Geng <gengzonglin@eswincomputing.com>
 *          Yuyang Cong <congyuyang@eswincomputing.com>
 */

#ifndef ES_DMA_MEMCP_H
#define ES_DMA_MEMCP_H

/**
 * struct esw_memcp_f2f_cmd - Represents a memory copy command from source to destination.
 *
 * @src_fd: File descriptor of the source buffer.
 * @src_offset: Offset in the source buffer from which data copy starts.
 * @dst_fd: File descriptor of the destination buffer.
 * @dst_offset: Offset in the destination buffer where data will be copied to.
 * @len: Length of the data to be copied, in bytes.
 * @timeout: Timeout for the memory copy operation, in milliseconds.
 */
struct esw_memcp_f2f_cmd {
    int src_fd;
    int src_offset;
    int dst_fd;
    int dst_offset;
    size_t len;
    int timeout;
};

/**
 * struct esw_cmdq_query - Represents the command queue status structure.
 *
 * @status: Status of the queue, 0 indicates idle, 1 indicates busy.
 * @task_count: Current number of tasks in the queue.
 */
struct esw_cmdq_query {
    int status;
    int task_count;
};

#define ESW_MEMCP_MAGIC 			'M'
#define ESW_CMDQ_ADD_TASK           _IOW(ESW_MEMCP_MAGIC, 1, struct esw_memcp_f2f_cmd)
#define ESW_CMDQ_SYNC               _IO(ESW_MEMCP_MAGIC, 2)
#define ESW_CMDQ_QUERY              _IOR(ESW_MEMCP_MAGIC, 3, struct esw_cmdq_query)

#endif
