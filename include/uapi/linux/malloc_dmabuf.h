/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ESWIN Malloc To DMA-BUF Driver
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
 * Authors: Yuyang Cong <congyuyang@eswincomputing.com>
 *
 */

#ifndef ES_DMA_MALLOC_DMABUF_H
#define ES_DMA_MALLOC_DMABUF_H

/**
 * struct esw_userptr - Represents a user pointer structure for memory mapping.
 *
 * @user_ptr: The user space pointer to the memory region.
 * @length: The size of the memory region, in bytes.
 * @aligned_length: The size of the aligned memory region, in bytes.
 * @offset_in_first_page: The offset within the first page of the user memory region.
 * @dma_fd: File descriptor associated with the DMA buffer for the memory region.
 * @mem_type: Memory type flag. If true, it indicates the memory is allocated via malloc.
 *            If false, the memory is non-malloc.
 */
struct esw_userptr {
    unsigned long user_ptr;
    unsigned long long length;
    unsigned long long aligned_length;
    unsigned int offset_in_first_page;
    int dma_fd;
    unsigned long fd_flags;
    bool mem_type;
};

#define ESW_MALLOC_DMABUF_MAGIC 			'N'

#define ESW_CONVERT_USERPTR_TO_DMABUF       _IOWR(ESW_MALLOC_DMABUF_MAGIC, 1, struct esw_userptr)
#define ESW_GET_DMABUF_INFO                 _IOWR(ESW_MALLOC_DMABUF_MAGIC, 2, struct esw_userptr)

#endif