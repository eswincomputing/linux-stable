// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#ifndef DLA_LOG_H
#define DLA_LOG_H

void dla_loop_buf_enable(void);
void dla_loop_buf_disable(void);
void dla_loop_buf_exit(void);
void dla_output_loopbuf(void);
void dla_print_to_loopbuf(const char *str, ...);

#define DEBUG_SHIFT_EDMA 0
#define DEBUG_SHIFT_CONV 4
#define DEBUG_SHIFT_SDP 8
#define DEBUG_SHIFT_PDP 12
#define DEBUG_SHIFT_DLA 16
#define DEBUG_SHIFT_DSP 20

#define DEBUG_BIT_MASK 0xf

#define NPU_PRINT_ERROR 1
#define NPU_PRINT_WARN 2
#define NPU_PRINT_INFO 3
#define NPU_PRINT_DEBUG 4
#define NPU_PRINT_DETAIL 5

extern int npu_debug_control;
extern int loop_buf_enable;

#define npu_log(level, fmt, ...)                                          \
	do {                                                              \
		if (level <= npu_debug_control)                           \
			printk(KERN_INFO "[NPU][DRIVER][%s](%s:%d) " fmt, \
			       level == NPU_PRINT_ERROR ? "ERROR" :       \
			       level == NPU_PRINT_WARN	? "WARNING" :     \
			       level == NPU_PRINT_INFO	? "INFO" :        \
			       level == NPU_PRINT_DEBUG ? "DEBUG" :       \
								"DETAIL",       \
			       __func__, __LINE__, ##__VA_ARGS__);        \
	} while (0)

#define dla_error(fmt, ...) npu_log(NPU_PRINT_ERROR, fmt, ##__VA_ARGS__);
#define dla_warn(fmt, ...) npu_log(NPU_PRINT_WARN, fmt, ##__VA_ARGS__);
#define dla_info(fmt, ...) npu_log(NPU_PRINT_INFO, fmt, ##__VA_ARGS__);
#define dla_debug(fmt, ...) npu_log(NPU_PRINT_DEBUG, fmt, ##__VA_ARGS__);
#define dla_detail(fmt, ...) npu_log(NPU_PRINT_DETAIL, fmt, ##__VA_ARGS__);

#endif
