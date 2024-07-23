// Copyright © 2023 ESWIN. All rights reserved.
//
// Beijing ESWIN Computing Technology Co., Ltd and its affiliated companies ("ESWIN") retain
// all intellectual property and proprietary rights in and to this software. Except as expressly
// authorized by ESWIN, no part of the software may be released, copied, distributed, reproduced,
// modified, adapted, translated, or created derivative work of, in whole or in part.

#include <linux/module.h>
#include "dsp_log.h"

#if BUILD_RELEASE == 1
int dsp_log_level = LOG_ERROR;
#else
#if DEBUG_LEVEL == 1
int dsp_log_level = LOG_ERROR;
#elif DEBUG_LEVEL == 2
int dsp_log_level = LOG_INFO;
#else
int dsp_log_level = LOG_DEBUG;
#endif
#endif

module_param(dsp_log_level, int, 0644);
MODULE_PARM_DESC(dsp_log_level,
		 "Log level (0:DISABLE 1: ERROR, 2: INFO, 3: DEBUG)");
