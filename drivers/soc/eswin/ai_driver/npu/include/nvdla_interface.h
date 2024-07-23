// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef NVDLA_INTERFACE_H
#define NVDLA_INTERFACE_H

#include <linux/types.h>

/**
 * @brief			Register driver to firmware
 *
 * Implementation in firmware, called by portability layer
 *
 * This function must be called once during boot to initialize DLA
 * engine scheduler and register driver with firmware before submitting
 * any task. Pass pointer to driver context in @param driver_context
 * which is passed as param when firmware calls any function
 * of portability layer. It also updates pointer to engine context
 * which must be passed in any function call to firmware after this point.
 *
 * @param engine_context	Pointer to engine specific data
 * @param driver_context	Pointer to driver specific data
 *
 * @return			0 on success and negative on error
 */
int32_t dla_register_driver(void **engine_context, void *driver_context);

/**
 * @brief			Read data from DMA mapped memory in local buffer
 *
 * Implementation in portability layer, called by firmware
 *
 * This function reads data from buffers passed by UMD in local memory.
 * Addresses for buffers passed by are shared in address list and network
 * descriptor contains index in address list for those buffers. Firmware
 * reads this data from buffer shared by UMD into local buffer to consume
 * the information.
 *
 * @param driver_context	Driver specific data received in dla_register_driver
 * @param task_data		Task specific data received in dla_execute_task
 * @param src			Index in address list
 * @param dst			Pointer to local memory
 * @param size			Size of data to copy
 * @param offset		Offset from start of UMD buffer
 *
 * @return			0 on success and negative on error
 *
 */
int32_t dla_data_read(void *driver_context, void *task_data, void *handle,
		      uint16_t index, void *dst, uint32_t size,
		      uint64_t offset);
int32_t dla_data_get_vaddr(void *task_data, uint16_t index, void **vaddr);

/**
 * @brief			Read DMA address
 *
 * Implementation in portability layer, called by firmware
 *
 * Some buffers shared by UMD are accessed by processor responsible for
 * programming DLA HW. It would be companion micro-controller in case of
 * headed config while main CPU in case of headless config. Also, some
 * buffers are accessed by DLA DMA engines inside sub-engines. This function
 * should return proper address accessible by destination user depending
 * on config.
 *
 * @param driver_context	Driver specific data received in dla_register_driver
 * @param task_data		Task specific data received in dla_execute_task
 * @param index			Index in address list
 * @param dst_ptr		Pointer to update address
 * @param destination		Destination user for DMA address
 *
 * @return			0 on success and negative on error
 *
 */
int32_t dla_get_dma_address(void *driver_context, void *task_data,
			    int16_t index, void *dst_ptr, u32 *is_io_tensor);

/**
 * @brief			Read time value in micro-seconds
 *
 * Implementation in portability layer, called by firmware
 *
 * Read system time in micro-seconds
 *
 * @return			Time value in micro-seconds
 *
 */

int32_t dla_get_sram_address(void *driver_context, void *task_data,
			     int16_t index, uint64_t *dst_ptr,
			     u32 *is_io_tensor);

int64_t dla_get_time_us(void);

/* KMD and DSP ops register */
struct module_public_life_cycle_interface {
	int die_seq;
	void *priv;
	int (*setup)(void *p);
	int (*release)(void *p);
	int (*reset)(void *p);
};
struct module_public_interface {
	int (*enable)(int (*done_callback)(void *group), void *group);
	int (*program)(void *group);
	void (*dump_config)(void *group);
};

#endif
