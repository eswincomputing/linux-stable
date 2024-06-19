// SPDX-License-Identifier: GPL-2.0-only
/*
 * ESWIN Heaps Userspace API
 *
 * Copyright 2024 Beijing ESWIN Computing Technology Co., Ltd.
 *   Authors:
 *    LinMin<linmin@eswincomputing.com>
 *
 */

#ifndef _UAPI_ESWIN_HEAPS_H
#define _UAPI_ESWIN_HEAPS_H

#include <linux/ioctl.h>
#include <linux/types.h>

/**
 * DOC: DMABUF Heaps Userspace API
 */
/* Valid FD_FLAGS are O_CLOEXEC, O_RDONLY, O_WRONLY, O_RDWR, O_SYNC */
#define ESWIN_HEAP_VALID_FD_FLAGS (O_CLOEXEC | O_ACCMODE | O_SYNC)

/* Add HEAP_SPRAM_FORCE_CONTIGUOUS heap flags for ESWIN SPRAM HEAP */
#define HEAP_FLAGS_SPRAM_FORCE_CONTIGUOUS	(1 << 0)
#define ESWIN_HEAP_VALID_HEAP_FLAGS (HEAP_FLAGS_SPRAM_FORCE_CONTIGUOUS)

/**
 * struct eswin_heap_allocation_data - metadata passed from userspace for
 *                                      allocations
 * @len:		size of the allocation
 * @fd:			will be populated with a fd which provides the
 *			handle to the allocated dma-buf
 * @fd_flags:		file descriptor flags used when allocating
 * @heap_flags:		flags passed to heap
 *
 * Provided by userspace as an argument to the ioctl
 */
struct eswin_heap_allocation_data {
	__u64 len;
	__u32 fd;
	__u32 fd_flags;
	__u64 heap_flags;
};

#define ESWIN_HEAP_IOC_MAGIC		'H'

/**
 * DOC: ESWIN_HEAP_IOCTL_ALLOC - allocate memory from pool
 *
 * Takes a eswin_heap_allocation_data struct and returns it with the fd field
 * populated with the dmabuf handle of the allocation.
 */
#define ESWIN_HEAP_IOCTL_ALLOC	_IOWR(ESWIN_HEAP_IOC_MAGIC, 0x0,\
				      struct eswin_heap_allocation_data)

#endif /* _UAPI_ESWIN_HEAPS_H */
