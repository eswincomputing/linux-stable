// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN drm driver
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
 * Authors: Eswin Driver team
 */

#ifndef _HOST_LIB_DRIVER_LINUX_IF_H_
#define _HOST_LIB_DRIVER_LINUX_IF_H_

#include <linux/ioctl.h>
#include <linux/types.h>

#define HL_DRIVER_ALLOCATE_DYNAMIC_MEM 0xffffffff
// hl_drv_ioctl numbers
enum {
	HL_DRV_NR_MIN = 0x10,
	HL_DRV_NR_INIT,
	HL_DRV_NR_MEMINFO,
	HL_DRV_NR_LOAD_CODE,
	HL_DRV_NR_READ_DATA,
	HL_DRV_NR_WRITE_DATA,
	HL_DRV_NR_MEMSET_DATA,
	HL_DRV_NR_READ_HPI,
	HL_DRV_NR_WRITE_HPI,

	DW_DRV_NR_CONNECT_STATUS,
	DW_DRV_NR_CONNECT_SET,
	DW_DRV_NR_DISCONNECT_SET,
	DW_DRV_NR_AUTH_SUCCESS,
	DW_DRV_NR_AUTH_FAIL,
	DW_DRV_NR_NO_CAPACITY,

	HL_DRV_NR_MAX
};

/*
 * HL_DRV_IOC_INIT: associate file descriptor with the indicated memory.  This
 * must be called before any other hl_drv_ioctl on the file descriptor.
 *
 *   - hpi_base = base address of HPI registers.
 *   - code_base = base address of firmware memory (0 to allocate internally)
 *   - data_base = base address of data memory (0 to allocate internally)
 *   - code_len, data_len = length of firmware and data memory, respectively.
 */
#define HL_DRV_IOC_INIT _IOW('H', HL_DRV_NR_INIT, struct hl_drv_ioc_meminfo)

/*
 * HL_DRV_IOC_MEMINFO: retrieve memory information from file descriptor.
 *
 * Fills out the meminfo struct, returning the values passed to HL_DRV_IOC_INIT
 * except that the actual base addresses of internal allocations (if any) are
 * reported.
 */
#define HL_DRV_IOC_MEMINFO                                                     \
	_IOR('H', HL_DRV_NR_MEMINFO, struct hl_drv_ioc_meminfo)

struct hl_drv_ioc_meminfo {
	__u32 hpi_base;
	__u32 code_base;
	__u32 code_size;
	__u32 data_base;
	__u32 data_size;
};

/*
 * HL_DRV_IOC_LOAD_CODE: write the provided buffer to the firmware memory.
 *
 *   - len = number of bytes in data buffer
 *   - data = data to write to firmware memory.
 *
 * This can only be done once (successfully).  Subsequent attempts will
 * return -EBUSY.
 */
#define HL_DRV_IOC_LOAD_CODE                                                   \
	_IOW('H', HL_DRV_NR_LOAD_CODE, struct hl_drv_ioc_code)

struct hl_drv_ioc_code {
	__u32 len;
	__u8 data[];
};

/*
 * HL_DRV_IOC_READ_DATA: copy from data memory.
 * HL_DRV_IOC_WRITE_DATA: copy to data memory.
 *
 *   - offset = start copying at this byte offset into the data memory.
 *   - len    = number of bytes to copy.
 *   - data   = for write, buffer containing data to copy.
 *              for read, buffer to which read data will be written.
 *
 */
#define HL_DRV_IOC_READ_DATA                                                   \
	_IOWR('H', HL_DRV_NR_READ_DATA, struct hl_drv_ioc_data)
#define HL_DRV_IOC_WRITE_DATA                                                  \
	_IOW('H', HL_DRV_NR_WRITE_DATA, struct hl_drv_ioc_data)

/*
 * HL_DRV_IOC_MEMSET_DATA: initialize data memory.
 *
 *   - offset  = start initializatoin at this byte offset into the data memory.
 *   - len     = number of bytes to set.
 *   - data[0] = byte value to write to all indicated memory locations.
 */
#define HL_DRV_IOC_MEMSET_DATA                                                 \
	_IOW('H', HL_DRV_NR_MEMSET_DATA, struct hl_drv_ioc_data)

struct hl_drv_ioc_data {
	__u32 offset;
	__u32 len;
	__u8 data[];
};

/*
 * HL_DRV_IOC_READ_HPI: read HPI register.
 * HL_DRV_IOC_WRITE_HPI: write HPI register.
 *
 *   - offset = byte offset of HPI register to access.
 *   - value  = for write, value to write.
 *              for read, location to which result is stored.
 */
#define HL_DRV_IOC_READ_HPI                                                    \
	_IOWR('H', HL_DRV_NR_READ_HPI, struct hl_drv_ioc_hpi_reg)
#define HL_DRV_IOC_WRITE_HPI                                                   \
	_IOW('H', HL_DRV_NR_WRITE_HPI, struct hl_drv_ioc_hpi_reg)

struct hl_drv_ioc_hpi_reg {
	__u32 offset;
	__u32 value;
};

#define DW_DRV_IOC_CONNECT_STATUS                                              \
	_IOR('H', DW_DRV_NR_CONNECT_STATUS, struct hl_drv_ioc_info)
#define DW_DRV_IOC_CONNECT_SET                                                 \
	_IOW('H', DW_DRV_NR_CONNECT_SET, struct hl_drv_ioc_info)
#define DW_DRV_IOC_DISCONNECT_SET                                              \
	_IOW('H', DW_DRV_NR_DISCONNECT_SET, struct hl_drv_ioc_info)
#define DW_DRV_IOC_AUTH_SUCCESS                                                \
	_IOW('H', DW_DRV_NR_AUTH_SUCCESS, struct hl_drv_ioc_info)
#define DW_DRV_IOC_AUTH_FAIL                                                   \
	_IOW('H', DW_DRV_NR_AUTH_FAIL, struct hl_drv_ioc_info)
#define DW_DRV_IOC_NO_CAPACITY                                                 \
	_IOW('H', DW_DRV_NR_NO_CAPACITY, struct hl_drv_ioc_info)

struct hl_drv_ioc_info {
	union {
		__u32 connect;
		__u32 auth;
		__u32 capacity;
	};
};

#endif // _HOST_LIB_DRIVER_LINUX_IF_H_
