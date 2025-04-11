// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN DVP2AXI version driver
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

#ifndef _ES_DVP2AXI_VERSION_H
#define _ES_DVP2AXI_VERSION_H
#include <linux/version.h>
#include "../../../../phy/eswin/es-dvp2axi-config.h"

/*
 *ES DVP2AXI DRIVER VERSION NOTE
 *
 *v0.1.0:
 *1. First version;
 *v0.1.1
 *1. Support the mipi vc multi-channel input in dvp2axi driver for es1808
 *v0.1.2
 *1. support output yuyv fmt by setting the input mode to raw8
 *2. Compatible with dvp2axi only have single dma mode in driver
 *3. Support dvp2axi works with mipi channel for es3288
 *4. Support switching between oneframe and pingpong for dvp2axi
 *5. Support sampling raw data for dvp2axi
 *6. fix the bug that dummpy buffer size is error
 *7. Add framesizes and frmintervals callback
 *8. fix dvp camera fails to link with dvp2axi on es1808
 *9. add camera support hotplug for n4
 *10. reconstruct register's reading and writing
 *v0.1.3
 *1. support kernel-4.19 and support vicap single dvp for rv1126
 *2. support vicap + mipi(single) for rv1126
 *3. support vicap + mipi hdr for rv1126
 *4. add luma device node for rv1126 vicap
 *v0.1.4
 *1. support vicap-full lvds interface to work in linear and hdr mode for rv1126
 *2. add vicap-lite device for rv1126
 *v0.1.5
 *1. support crop function
 *2. fix compile error when config with module
 *3. support mipi yuv
 *4. support selection ioctl for cropping
 *5. support dvp2axi compact mode(lvds & mipi) can be set from user space
 *v0.1.6
 *1. add dvp2axi self-defined ioctrl cmd:V4L2_CID_DVP2AXI_DATA_COMPACT
 *v0.1.7
 *1. support dvp and mipi/lvds run simultaneously
 *2. add subdev as interface for isp
 *3. support hdr_x3 mode
 *4. support es1808 mipi interface in kernel-4.19
 *v0.1.8
 *1. add proc interface
 *2. add reset mechanism to resume when csi crc err
 *3. support bt1120 single path
 *v0.1.9
 *1. support es3568 dvp2axi
 *2. support es3568 csi-host
 *3. add dvp sof
 *4. add extended lines to out image for normal & hdr short frame
 *5. modify reset mechanism drivered by real-time frame rate
 *6. support es356x iommu uses vb2 sg type
 *7. register dvp2axi sd itf when pipeline completed
 *v0.1.10
 *1. rv1126/es356x support bt656/bt1120 multi channels function
 *2. add dynamic cropping function
 *3. optimize dts config of dvp2axi's pipeline
 *4. register dvp2axi itf dev when clear unready subdev
 *5. mipi csi host add cru rst
 *6. support wake up mode with mipi
 *7. add keepint time to csi2 err for resetting
 *8. mipi supports pdaf/embedded data
 *9. mipi supports interlaced capture
 *v0.2.0
 *1. vicap support combine multi mipi dev to one dev, this function is mainly used for es3588
 */

#define ES_DVP2AXI_DRIVER_VERSION ES_DVP2AXI_API_VERSION

#endif
