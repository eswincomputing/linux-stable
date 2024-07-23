// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/printk.h>
#include <linux/module.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include "dla_log.h"

/*/sys/module/npu/parameters/*/
#if NPU_PERF_STATS
int npu_debug_control = NPU_PRINT_ERROR;
#else
#if LOG_LEVEL == 0
int npu_debug_control = NPU_PRINT_ERROR;
#elif LOG_LEVEL == 1
int npu_debug_control = NPU_PRINT_WARN;
#elif LOG_LEVEL == 2
int npu_debug_control = NPU_PRINT_INFO;
#else
int npu_debug_control = NPU_PRINT_DETAIL;
#endif
#endif

int loop_buf_enable = 0;
module_param(loop_buf_enable, int, 0644);
MODULE_PARM_DESC(loop_buf_enable, "loop buf enable");

static int loop_buf_cnt = 1500;
module_param(loop_buf_cnt, int, 0644);
MODULE_PARM_DESC(loop_buf_cnt, "loop buf cnt");

module_param(npu_debug_control, int, 0644);
MODULE_PARM_DESC(npu_debug_control, "npu debug control");

#define LP_BUF_LEN 128

static char *loop_buf = NULL;
static int lb_pointer = -1;
static spinlock_t lb_spin_lock;
static int lp_buf_cnt = 0;

static int dla_loop_buf_init(void)
{
	lp_buf_cnt = loop_buf_cnt;

	if (loop_buf != NULL) {
		vfree(loop_buf);
		loop_buf = NULL;
	}

	loop_buf = vmalloc(lp_buf_cnt * LP_BUF_LEN);
	if (loop_buf == NULL) {
		printk("malloc loop_buf failed!lp_buf_cnt=%d\n", lp_buf_cnt);
		loop_buf_enable = 0;
		return -1;
	}
	memset(loop_buf, 0, lp_buf_cnt * LP_BUF_LEN);

	spin_lock_init(&lb_spin_lock);

	return 0;
}

void dla_loop_buf_exit(void)
{
	if (loop_buf != NULL) {
		vfree(loop_buf);
		loop_buf = NULL;
	}

	return;
}

void dla_loop_buf_enable(void)
{
	loop_buf_enable = 1;
}

void dla_loop_buf_disable(void)
{
	loop_buf_enable = 0;
	lb_pointer = -1;
	if (loop_buf != NULL) {
		memset(loop_buf, 0, lp_buf_cnt * LP_BUF_LEN);
	}
}

static size_t print_time(u64 ts, char *buf)
{
	unsigned long rem_nsec = do_div(ts, 1000000000);

	return sprintf(buf, "[%5lu.%06lu]", (unsigned long)ts, rem_nsec / 1000);
}

void dla_print_to_loopbuf(const char *str, ...)
{
	int ret = 0;
	size_t len = 0;
	char *t_buf = NULL;
	u64 ts_nsec;
	unsigned long flags;
	va_list args;

	if (loop_buf_enable == 0)
		return;
	if ((loop_buf == NULL) || (lp_buf_cnt != loop_buf_cnt)) {
		ret = dla_loop_buf_init();
		if (ret)
			return;
	}

	va_start(args, str);
	ts_nsec = local_clock();

	spin_lock_irqsave(&lb_spin_lock, flags);
	if (lb_pointer == lp_buf_cnt - 1 || lb_pointer == -1) {
		lb_pointer = 0;
	}

	t_buf = loop_buf + lb_pointer * LP_BUF_LEN;

	memset(t_buf, 0, LP_BUF_LEN);
	len = print_time(ts_nsec, t_buf);
	len += vsnprintf(t_buf + len, LP_BUF_LEN - len, str, args);

	lb_pointer++;
	spin_unlock_irqrestore(&lb_spin_lock, flags);

	va_end(args);

	return;
}

void dla_output_loopbuf(void)
{
	int i = 0;
	unsigned long flags;

	if (lb_pointer == -1)
		return;

	spin_lock_irqsave(&lb_spin_lock, flags);
	for (i = lb_pointer; i < lp_buf_cnt; i++) {
		if (loop_buf[i * LP_BUF_LEN] == 0)
			break;
		printk("%s", loop_buf + i * LP_BUF_LEN);
	}

	for (i = 0; i < lb_pointer; i++) {
		printk("%s", loop_buf + i * LP_BUF_LEN);
	}

	lb_pointer = -1;
	spin_unlock_irqrestore(&lb_spin_lock, flags);

	return;
}

void set_debug_level(int level)
{
	npu_debug_control = level;
}
