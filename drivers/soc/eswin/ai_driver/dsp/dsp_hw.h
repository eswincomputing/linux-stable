// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef __DSP_HW_H__
#define __DSP_HW_H__

#include <linux/completion.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/irqreturn.h>
#include <linux/dma-buf.h>
#include "dsp_hw_if.h"

struct device;
typedef struct dsp_request dsp_request_t;
typedef void (*cpl_handler_fn)(struct device *, dsp_request_t *);

typedef struct dsp_request {
	u64 handle;
	/**
	*  This structure can be chainned in a linked list and this variable
	*  specifies the list node.
	*/
	struct list_head cpl_list;
	struct list_head pending_list;
	int prio;
	cpl_handler_fn cpl_handler;
	void *private;

	dma_addr_t dsp_flat1_iova;
	struct es_dsp_flat1_desc *flat_virt;
	u32 flat_size;
	es_dsp_d2h_msg d2h_msg;
	/**
	*  If true, then the driver is responsible for host side cache 
	*  synchronization.
	*/
	bool sync_cache;
	/**
	*  If true, then DSP framework is allowed to start evaluation function call
	*  directly after completing prepare call. Otherwise DSP framework should
	*  wait for further instruction from DSP driver or E31 auxiliary CPU core.
	*/
	bool allow_eval;
	/**
	*  If true, the DSP framework should expect to communicate with E31
	*  auxiliary CPU core for starting evaluation and notifying `prepare` and
	*  `eval` completion events.
	*/
	bool poll_mode;
} __attribute__((aligned(SMP_CACHE_BYTES))) dsp_request_t;

/**
 * @brief Submit a DSP task to the given DSP device.
 * 
 * @param dsp_dev   The device handle to load an operator.
 * @param req  The task to be delivered. Note if the task has `allow_eval`
 *             field set to true, then DSP device will execute `prepare`
 *             and `eval` in order when this task is scheduled to DSP
 *             device. Otherwise, it will execute `prepare` and wait for
 *             further notification.
 *
 * @return ES_S32 The execution status.
 */
int submit_task(struct device *dsp_dev, dsp_request_t *req);

/**
 * @brief Load a given operator into system. This includes load elf data from
 * firmware folder into memory and allocate IOVA for executable code.
 * 
 * @param dsp_dev   The device handle to load an operator.
 * @param op_name Authoritative name of the given operator.
 * @param handle If operator is loaded successfully, the this pointer operator desc address.
 *
 * @return ES_S32 The execution status.
 */
int load_operator(struct device *dsp_dev, char *op_dir, char *op_name,
		  u64 *handle);
/**
 * @brief Unload the given operator from system. If the operator is not longer
 * in use, then it will be removed entirely.
 * 
 * @param dsp_dev   The device handle to load an operator.
 * @param op_name Authoritative name of the given operator.
 *
 * @return ES_S32 The execution status.
 */
int unload_operator(struct device *dsp_dev, u64 handle);

/**
 * @brief Signal DSP device that the given DSP task is ready for `eval` step.
 * 
 * @param dsp_dev   The device handle to load an operator.
 * @param req  The task to be signaled. Call start_eval only if the task is
 *             submitted without setting `allow_eval` to true.
 *
 * @return ES_S32 The execution status.
 */
int start_eval(struct device *dsp_dev, dsp_request_t *req);

/**
 * @brief DSP unmap dma buf when no need this dma buf
 * 
 * @param dsp_file   The process open dsp node file.
 * @param buf  buf saved dsp mapped dmabufs.
 * @param count How many dma buf in buf.
 *
 * @return 0
 */
int dsp_unmap_dmabuf(struct dsp_file *dsp_file, struct dsp_dma_buf **buf, int count);

/**
 * @brief DSP get dma buf when need mapped dma buf.
 * 
 * @param dsp_file   The process open dsp node file.
 * @param memfd memfd reference dma file.
 *
 * @return NULL or pointer that point the dsp dma buf.
 */
struct dsp_dma_buf *dsp_get_dma_buf_ex(struct dsp_file *dsp_file, int memfd);

/**
 * @brief Dsp set flat descriptor prepare and eval func address.
 * @param flat is a pointer that is alloced by dma alloc.
 * @param handle is a pointer that point dsp op desc.
 */
void dsp_set_flat_func(struct es_dsp_flat1_desc *flat, u64 handle);

/**
 * @brief get sram iova when buffer comes from SRAM.
 * @param dev represent dsp device.
 * @param phy_addr is sram physical address.
 * @return dsp iova addr or 0(when no iova mapped).
 */
u32 dsp_get_sram_iova_by_addr(struct device *dev, u64 phy_addr);

/**
 * @brief unmap dsp used sram buf.
 * @param dev is dsp device.
 * @return 0 is successful, other failed.
 */
int dsp_detach_sram_dmabuf(struct device *dev);


/**
 * @brief map sram for dsp.
 * @param dev is dsp device.
 * @param dmabuf is sram dma buf.
 * @return 0 is successful or failed.
 */
int dsp_attach_sram_dmabuf(struct device *dev, struct dma_buf *dmabuf);

/**
 * @brief Subscribe the service of the given DSP core. NPU device can call this
 * function to bind it to a given DSP core in NPU's probe function. If binding
 * is successful, then NPU can use the service from this DSP core. Note device
 * link is added in this function.
 *
 * @param die_id    The die ID (0, 1) of the subscriber.
 * @param dspId    The DSP ID (0, 1, 2, 3) of the subscriber.
 * @param subscriber The device instance of subscriber.
 * @param dsp_device Return the DSP device instance if the call returns
 *             successfully.
 * @return int  The execution status. Return Linux errorno.
 */
int subscribe_dsp_device(u32 die_id, u32 dspId, struct device *subscrib,
			 struct device **dsp_dev);

/**
 * @brief Unsubscribe the service of the given DSP core. NPU device can call
 * this function to unbind it from a given DSP core in NPU's release function.
 * Note device link is deleted in this function.
 *
 * @param subscriber The device instance of subscriber.
 * @param dsp_device Return the DSP device instance if the call returns
 *             successfully.
 * @return int  The execution status. Return Linux errorno.
 */
int unsubscribe_dsp_device(struct device *subscrib, struct device *dsp_dev);

#endif
