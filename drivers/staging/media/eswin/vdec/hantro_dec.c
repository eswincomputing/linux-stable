// SPDX-License-Identifier: GPL-2.0
/****************************************************************************
 *
 *    The MIT License (MIT)
 *
 *    Copyright (C) 2020  VeriSilicon Microelectronics Co., Ltd.
 *
 *    Permission is hereby granted, free of charge, to any person obtaining a
 *    copy of this software and associated documentation files (the "Software"),
 *    to deal in the Software without restriction, including without limitation
 *    the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *    and/or sell copies of the Software, and to permit persons to whom the
 *    Software is furnished to do so, subject to the following conditions:
 *
 *    The above copyright notice and this permission notice shall be included in
 *    all copies or substantial portions of the Software.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *    DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************
 *
 *    The GPL License (GPL)
 *
 *    Copyright (C) 2020  VeriSilicon Microelectronics Co., Ltd.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version 2
 *    of the License, or (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software Foundation,
 *    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *****************************************************************************
 *
 *    Note: This software is released under dual MIT and GPL licenses. A
 *    recipient may use this file under the terms of either the MIT license or
 *    GPL License. If you wish to use only one license not the other, you can
 *    indicate your decision by deleting one of the above license notices
 *    in your version of this file.
 *
 *****************************************************************************
 */

#include "hantrodec.h"
#include "dwl_defs.h"

#include <asm/io.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/mod_devicetable.h>
#if (KERNEL_VERSION(5, 10, 0) > LINUX_VERSION_CODE)
#include <linux/dma-contiguous.h>
#else
#include <linux/dma-map-ops.h>
#endif
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/dma-buf.h>
#include <linux/of_device.h>
#include <linux/dmabuf-heap-import-helper.h>
#include <linux/reset.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/eswin-win2030-sid-cfg.h>
#include <dt-bindings/memory/eswin-win2030-sid.h>
#include <linux/pm_runtime.h>

#include "subsys.h"
#include "hantroaxife.h"
#include "hantroafbc.h"
#include "dts_parser.h"

#define LOG_TAG DEC_DEV_NAME ":main"
#include "vc_drv_log.h"

#undef PDEBUG
#ifdef HANTRODEC_DEBUG
#ifdef __KERNEL__
#define PDEBUG(fmt, args...) pr_info("es_vdec: " fmt, ##args)
#else
#define PDEBUG(fmt, args...) fprintf(stderr, fmt, ##args)
#endif
#else
#define PDEBUG(fmt, args...)
#endif

#ifdef EMU
  #define PCI_VENDOR_ID_HANTRO            0x1d9b// 0x1ae0//0x16c3
  #define PCI_DEVICE_ID_HANTRO_PCI        0xface//0x001a// 0xabcd

  /* Base address DDR register */
  #define PCI_DDR_BAR 4

  /* Base address got control register */
  #define PCI_CONTROL_BAR                 2

  /* PCIe hantro driver offset in control register */
  #define HANTRO_REG_OFFSET0               0x6000000
  #define HANTRO_REG_OFFSET1               0x6010000
#else
#ifdef PLATFORM_GEN7
  #define PCI_VENDOR_ID_HANTRO            0x10ee// 0x1ae0//0x16c3
  #define PCI_DEVICE_ID_HANTRO_PCI        0x9014//0x001a// 0xabcd

  /* Base address DDR register */
  #define PCI_DDR_BAR 2

  /* Base address got control register */
  #define PCI_CONTROL_BAR                 0

  /* PCIe hantro driver offset in control register */
  #define HANTRO_REG_OFFSET0               0x0090000
  #define HANTRO_REG_OFFSET1               -1
#else
  #define PCI_VENDOR_ID_HANTRO            0x10ee// 0x1ae0//0x16c3
  #define PCI_DEVICE_ID_HANTRO_PCI        0x8014//0x001a// 0xabcd

  /* Base address DDR register */
  #define PCI_DDR_BAR 0

  /* Base address got control register */
  #define PCI_CONTROL_BAR                 4

  /* PCIe hantro driver offset in control register */
//  #define HANTRO_REG_OFFSET0               0x600000
//  #define HANTRO_REG_OFFSET1               0x700000
  #define HANTRO_REG_OFFSET0               0x100000
  #define HANTRO_REG_OFFSET1               0x200000
#endif
#endif

#ifndef VDEC_CLASS_NAME
# define VDEC_CLASS_NAME "es_vdec_class"
#endif

/* TODO(mheikkinen) Implement multicore support. */
static struct pci_dev *gDev; /* PCI device structure. */
/* PCI base register address (Hardware address) */
static unsigned long gBaseHdwr;
unsigned long gBaseDDRHw; /* PCI base register address (memalloc) */
static u32 gBaseLen; /* Base register address Length */

/* hantro G1 regs config including dec and pp */
//#define HANTRO_DEC_ORG_REGS             60
//#define HANTRO_PP_ORG_REGS              41

#define HANTRO_DEC_EXT_REGS             27
#define HANTRO_PP_EXT_REGS              9

//#define HANTRO_G1_DEC_TOTAL_REGS (HANTRO_DEC_ORG_REGS + HANTRO_DEC_EXT_REGS)
#define HANTRO_PP_TOTAL_REGS     (HANTRO_PP_ORG_REGS + HANTRO_PP_EXT_REGS)
#define HANTRO_G1_DEC_REGS              155 /*G1 total regs*/

#define HANTRO_PP_ORG_FIRST_REG         60
#define HANTRO_PP_ORG_LAST_REG          100
#define HANTRO_PP_EXT_FIRST_REG         146
#define HANTRO_PP_EXT_LAST_REG          154

/* hantro G2 reg config */
#define HANTRO_G2_DEC_REGS 337 /*G2 total regs*/
#define HANTRO_G2_DEC_FIRST_REG 0
#define HANTRO_G2_DEC_LAST_REG (HANTRO_G2_DEC_REGS - 1)

/* hantro VC8000D reg config */
#define HANTRO_VC8000D_REGS             MAX_REG_COUNT /*VC8000D total regs*/
#define HANTRO_VC8000D_FIRST_REG        0
#define HANTRO_VC8000D_LAST_REG         (HANTRO_VC8000D_REGS - 1)
#define HANTRODEC_HWBUILD_ID_OFF        (309 * 4)

/* Logic module IRQs */
#define HXDEC_NO_IRQ                    -1

#define MAX(a, b)                       (((a) > (b)) ? (a) : (b))

#define DEC_IO_SIZE_MAX                                   \
	(MAX(MAX(HANTRO_G2_DEC_REGS, HANTRO_G1_DEC_REGS), \
	HANTRO_VC8000D_REGS) * 4)

/* User should modify these configuration if do porting to own platform. */
/* Please guarantee the base_addr, io_size, dec_irq belong to same core. */

/* Defines use kernel clk cfg or not**/
//#define CLK_CFG
#ifdef CLK_CFG
/*this id should conform with platform define*/
#define CLK_ID   "hantrodec_clk"
#endif

/* Logic module base address */
#define SOCLE_LOGIC_0_BASE              0x38300000
#define SOCLE_LOGIC_1_BASE              0x38310000

#define VEXPRESS_LOGIC_0_BASE           0xFC010000
#define VEXPRESS_LOGIC_1_BASE           0xFC020000

#define DEC_IO_SIZE_0                   DEC_IO_SIZE_MAX /* bytes */
#define DEC_IO_SIZE_1                   DEC_IO_SIZE_MAX /* bytes */

#define DEC_IRQ_0                       HXDEC_NO_IRQ
#define DEC_IRQ_1                       HXDEC_NO_IRQ

#define IS_G1(hw_id)                    (((hw_id) == 0x6731) ? 1 : 0)
#define IS_G2(hw_id)                    (((hw_id) == 0x6732) ? 1 : 0)
#define IS_VC8000D(hw_id)               (((hw_id) == 0x9001) ? 1 : 0)
#define IS_BIGOCEAN(hw_id)              (((hw_id) == 0xB16D) ? 1 : 0)

/* Some IPs HW configuration paramters for APB Filter */
/* Because now such information can't be
 * read from APB filter configuration registers
 */
/* The fixed value have to be used */
#define VC8000D_NUM_MASK_REG            336
#define VC8000D_NUM_MODE                4
#define VC8000D_MASK_REG_OFFSET         4096
#define VC8000D_MASK_BITS_PER_REG       1

#define VC8000DJ_NUM_MASK_REG           332
#define VC8000DJ_NUM_MODE               1
#define VC8000DJ_MASK_REG_OFFSET        4096
#define VC8000DJ_MASK_BITS_PER_REG      1

#define AV1_NUM_MASK_REG                303
#define AV1_NUM_MODE                    1
#define AV1_MASK_REG_OFFSET             4096
#define AV1_MASK_BITS_PER_REG           1

#define AXIFE_NUM_MASK_REG              144
#define AXIFE_NUM_MODE                  1
#define AXIFE_MASK_REG_OFFSET           4096
#define AXIFE_MASK_BITS_PER_REG         1

#define VC_ACLK_HIGHEST                 1040000000
#define VDEC_SYS_CLK_HIGHEST            800000000
#define VDEC_MMU_AWSSID_OFF             0x400
#define VDEC_MMU_ARSSID_OFF             0x404
#define JDEC_MMU_AWSSID_OFF             0x800
#define JDEC_MMU_ARSSID_OFF             0x804
#define MCPU_SP0_DYMN_CSR_EN_BIT        3
#define MCPU_SP0_DYMN_CSR_GNT_BIT       3


/***********local variable declaration********/

static const int DecHwId[] = {
	0x6731, /* G1 */
	0x6732, /* G2 */
	0xB16D, /* BigOcean */
	0x9001 /* VC8000D */
};

static struct class *vdec_class = NULL;
static unsigned long base_port = -1;
unsigned int pcie = 0;
//static volatile unsigned char *reg;
static unsigned int reg_access_opt;
unsigned int vcmd = 1;
unsigned long alloc_size = 0xb0000000;
unsigned long alloc_base = 16;

unsigned long multicorebase[HXDEC_MAX_CORES] = {
	HANTRO_REG_OFFSET0,
	HANTRO_REG_OFFSET1,
	0,
	0
};

int irq[HXDEC_MAX_CORES] = {
	DEC_IRQ_0,
	DEC_IRQ_1,
	-1,
	-1
};

unsigned int iosize[HXDEC_MAX_CORES] = {
	DEC_IO_SIZE_0,
	DEC_IO_SIZE_1,
	-1,
	-1
};

/* Because one core may contain multi-pipeline,
 * so multicore base may be changed
 */
static unsigned long multicorebase_actual[HXDEC_MAX_CORES];

static struct subsys_config vpu_subsys[MAX_SUBSYS_NUM];

static struct apbfilter_cfg apbfilter_cfg[MAX_SUBSYS_NUM][HW_CORE_MAX];

static struct axife_cfg axife_cfg[MAX_SUBSYS_NUM];
static int elements = 2;

#ifdef CLK_CFG
struct clk *clk_cfg;
int is_clk_on;
struct timer_list timer;
#endif

/* for dma_alloc_coherent to allocate mmu &
 * vcmd linear buffers for non-pcie env
 */
static const struct platform_device_info hantro_platform_info = {
	.name = DEC_DRIVER_NAME,
	.id = -1,
	.dma_mask = DMA_BIT_MASK(32),
};

struct platform_device *platformdev = NULL;
struct platform_device *platformdev_d1 = NULL;

/* module_param(name, type, perm) */
module_param(base_port, ulong, 0);
module_param(pcie, uint, 0);
module_param_array(irq, int, &elements, 0644);
module_param_array(multicorebase, ulong, &elements, 0644);
module_param(reg_access_opt, uint, 0);
module_param(vcmd, uint, 0);
module_param(alloc_base, ulong, 0);
module_param(alloc_size, ulong, 0);

static int hantrodec_major; /* dynamic allocation */

/* here's all the must remember stuff */
typedef struct {
	char *buffer;
	volatile unsigned int iosize[HXDEC_MAX_CORES];
	/* mapped address to different HW cores regs*/
	volatile u8 *hwregs[HXDEC_MAX_CORES][HW_CORE_MAX];
	/* mapped address to different HW cores regs*/
	volatile u8 *apbfilter_hwregs[HXDEC_MAX_CORES][HW_CORE_MAX];
	volatile int irq[HXDEC_MAX_CORES];
	int hw_id[HXDEC_MAX_CORES][HW_CORE_MAX];
	/* Requested client type for given core,
	 * used when a subsys has multiple
	 * decoders, e.g., VC8000D+VC8000DJ+BigOcean
	 */
	int client_type[HXDEC_MAX_CORES];
	int cores;
	struct fasync_struct *async_queue_dec;
	struct fasync_struct *async_queue_pp;
} hantrodec_t;

typedef struct {
	u32 cfg[HXDEC_MAX_CORES]; /* indicate the supported format */
	u32 cfg_backup[HXDEC_MAX_CORES]; /* back up of cfg */
	/* indicate if main core exist */
	int its_main_core_id[HXDEC_MAX_CORES];
	/* indicate if aux core exist */
	int its_aux_core_id[HXDEC_MAX_CORES];
} core_cfg;

typedef struct _vdec_clk_rst {
	struct reset_control        *rstc_cfg;
	struct reset_control        *rstc_axi;
	struct reset_control        *rstc_moncfg;
	struct reset_control        *rstc_jd_cfg;
	struct reset_control        *rstc_jd_axi;
	struct reset_control        *rstc_vd_cfg;
	struct reset_control        *rstc_vd_axi;
	struct clk          *cfg_clk;
	struct clk          *aclk;
	struct clk          *jd_clk;
	struct clk          *vd_clk;
	struct clk          *vc_mux;
	struct clk          *spll0_fout1;
	struct clk          *spll2_fout1;
	struct clk          *jd_pclk;
	struct clk          *vd_pclk;
	struct clk          *mon_pclk;
} vdec_clk_rst_t;

static hantrodec_t hantrodec_data; /* dynamic allocation? */

static int ReserveIO(void);
static void ReleaseIO(void);

static void ResetAsic(hantrodec_t *dev);

static int vdec_clk_enable(vdec_clk_rst_t *vcrt);
static int vdec_pm_enable(struct platform_device *pdev);

#ifdef HANTRODEC_DEBUG
static void dump_regs(hantrodec_t *dev);
#endif

/* IRQ handler */
#if (KERNEL_VERSION(2, 6, 18) > LINUX_VERSION_CODE)
static irqreturn_t hantrodec_isr(int irq, void *dev_id, struct pt_regs *regs);
#else
static irqreturn_t hantrodec_isr(int irq, void *dev_id);
#endif

static u32 dec_regs[HXDEC_MAX_CORES][DEC_IO_SIZE_MAX / 4];
static u32 apbfilter_regs[HXDEC_MAX_CORES][DEC_IO_SIZE_MAX / 4 + 1];
/* shadow_regs used to compare whether
 * it's necessary to write to registers
 */
static u32 shadow_dec_regs[HXDEC_MAX_CORES][DEC_IO_SIZE_MAX / 4];

static struct semaphore dec_core_sem;
static struct semaphore pp_core_sem;

static int dec_irq;
static int pp_irq;

static atomic_t irq_rx = ATOMIC_INIT(0);
static atomic_t irq_tx = ATOMIC_INIT(0);

static struct file *dec_owner[HXDEC_MAX_CORES];
static struct file *pp_owner[HXDEC_MAX_CORES];
static int CoreHasFormat(const u32 *cfg, int core, u32 format);

/* spinlock_t owner_lock = SPIN_LOCK_UNLOCKED; */
static DEFINE_SPINLOCK(owner_lock);

static DECLARE_WAIT_QUEUE_HEAD(dec_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(pp_wait_queue);
static DECLARE_WAIT_QUEUE_HEAD(hw_queue);
#ifdef CLK_CFG
DEFINE_SPINLOCK(clk_lock);
#endif

#define DWL_CLIENT_TYPE_H264_DEC        1U
#define DWL_CLIENT_TYPE_MPEG4_DEC       2U
#define DWL_CLIENT_TYPE_JPEG_DEC        3U
#define DWL_CLIENT_TYPE_PP              4U
#define DWL_CLIENT_TYPE_VC1_DEC         5U
#define DWL_CLIENT_TYPE_MPEG2_DEC       6U
#define DWL_CLIENT_TYPE_VP6_DEC         7U
#define DWL_CLIENT_TYPE_AVS_DEC         8U
#define DWL_CLIENT_TYPE_RV_DEC          9U
#define DWL_CLIENT_TYPE_VP8_DEC         10U
#define DWL_CLIENT_TYPE_VP9_DEC         11U
#define DWL_CLIENT_TYPE_HEVC_DEC        12U
#define DWL_CLIENT_TYPE_ST_PP           14U
#define DWL_CLIENT_TYPE_H264_MAIN10     15U
#define DWL_CLIENT_TYPE_AVS2_DEC        16U
#define DWL_CLIENT_TYPE_AV1_DEC         17U
#define DWL_CLIENT_TYPE_BO_AV1_DEC      31U

#define BIGOCEANDEC_CFG 1
#define BIGOCEANDEC_AV1_E 5

static core_cfg config;

#define CORE_TYPE_STR_CASE(ct) case (ct): return(#ct + 3)

static char *CoreTypeStr(enum CoreType ct)
{
	switch (ct) {
		CORE_TYPE_STR_CASE(HW_VC8000D);
		CORE_TYPE_STR_CASE(HW_VC8000DJ);
		CORE_TYPE_STR_CASE(HW_BIGOCEAN);
		CORE_TYPE_STR_CASE(HW_VCMD);
		CORE_TYPE_STR_CASE(HW_MMU);
		CORE_TYPE_STR_CASE(HW_MMU_WR);
		CORE_TYPE_STR_CASE(HW_DEC400);
		CORE_TYPE_STR_CASE(HW_L2CACHE);
		CORE_TYPE_STR_CASE(HW_SHAPER);
		CORE_TYPE_STR_CASE(HW_AXIFE);
		CORE_TYPE_STR_CASE(HW_AFBC);
	default:
		return "Invalid core type";
	}
}

#ifdef HANTRODEC_DEBUG

#define IOCTL_CMD_STR_CASE(cmd) case (cmd): return(#cmd)

static char *IoctlCmdStr(unsigned int cmd)
{
	switch (cmd) {
		IOCTL_CMD_STR_CASE(HANTRODEC_IOC_MC_CORES);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCGHWOFFSET);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCGHWIOSIZE);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOC_MC_OFFSETS);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOC_CLI);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOC_STI);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCS_DEC_PUSH_REG);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCS_DEC_PULL_REG);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCH_DEC_RESERVE);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCT_DEC_RELEASE);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCX_DEC_WAIT);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCG_CORE_WAIT);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOX_ASIC_ID);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCG_CORE_ID);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCS_DEC_WRITE_REG);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCS_DEC_READ_REG);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOX_ASIC_BUILD_ID);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCX_POLL);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOX_SUBSYS);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOCS_DEC_WRITE_APBFILTER_REG);
		IOCTL_CMD_STR_CASE(HANTRODEC_DEBUG_STATUS);
		/* MMU */
		IOCTL_CMD_STR_CASE(HANTRO_IOCS_MMU_MEM_MAP);
		IOCTL_CMD_STR_CASE(HANTRO_IOCS_MMU_MEM_UNMAP);
		IOCTL_CMD_STR_CASE(HANTRO_IOCS_MMU_FLUSH);
		/* VCMD */
		IOCTL_CMD_STR_CASE(HANTRO_VCMD_IOCH_GET_CMDBUF_PARAMETER);
		IOCTL_CMD_STR_CASE(HANTRO_VCMD_IOCH_GET_CMDBUF_POOL_SIZE);
		IOCTL_CMD_STR_CASE(HANTRO_VCMD_IOCH_SET_CMDBUF_POOL_BASE);
		IOCTL_CMD_STR_CASE(HANTRO_VCMD_IOCH_GET_VCMD_PARAMETER);
		IOCTL_CMD_STR_CASE(HANTRO_VCMD_IOCH_RESERVE_CMDBUF);
		IOCTL_CMD_STR_CASE(HANTRO_VCMD_IOCH_LINK_RUN_CMDBUF);
		IOCTL_CMD_STR_CASE(HANTRO_VCMD_IOCH_WAIT_CMDBUF);
		IOCTL_CMD_STR_CASE(HANTRO_VCMD_IOCH_RELEASE_CMDBUF);
		IOCTL_CMD_STR_CASE(HANTRO_VCMD_IOCH_POLLING_CMDBUF);
		/* AXI FE / APB filter */
		IOCTL_CMD_STR_CASE(HANTRODEC_IOC_APBFILTER_CONFIG);
		IOCTL_CMD_STR_CASE(HANTRODEC_IOC_AXIFE_CONFIG);
#ifdef SUPPORT_DMA_HEAP
		IOCTL_CMD_STR_CASE(HANTRODEC_IOC_DMA_HEAP_GET_IOVA);
#endif
	default:
		return "Invalid ioctl cmd";
	}
}
#endif

static void ReadCoreConfig(hantrodec_t *dev)
{
	int c, j;
	u32 reg, tmp, mask;

	memset(config.cfg, 0, sizeof(config.cfg));

	for (c = 0; c < dev->cores; c++) {
		for (j = 0; j < HW_CORE_MAX; j++) {
			if (j != HW_VC8000D && j != HW_VC8000DJ &&
			    j != HW_BIGOCEAN)
				continue;
			/* NOT defined core type */
			if (!dev->hwregs[c][j])
				continue;
			/* Decoder configuration */
			if (IS_G1(dev->hw_id[c][j])) {
				reg = ioread32(
				  (void __iomem *)(dev->hwregs[c][j] +
				   HANTRODEC_SYNTH_CFG * 4));

				tmp = (reg >> DWL_H264_E) & 0x3U;
				if (tmp) {
					LOG_INFO("subsys[%d] has H264\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

				tmp = (reg >> DWL_JPEG_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has JPEG\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

				tmp = (reg >> DWL_HJPEG_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has HJPEG\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

				tmp = (reg >> DWL_MPEG4_E) & 0x3U;
				if (tmp) {
					LOG_INFO("subsys[%d] has MPEG4\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_MPEG4_DEC : 0;

				tmp = (reg >> DWL_VC1_E) & 0x3U;
				if (tmp) {
					LOG_INFO("subsys[%d] has VC1\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_VC1_DEC : 0;

				tmp = (reg >> DWL_MPEG2_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has MPEG2\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_MPEG2_DEC : 0;

				tmp = (reg >> DWL_VP6_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has VP6\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_VP6_DEC : 0;

				reg = ioread32((void __iomem *)(dev->hwregs[c][j] +
						HANTRODEC_SYNTH_CFG_2 * 4));

				/* VP7 and WEBP is part of VP8 */
				mask = (1 << DWL_VP8_E) | (1 << DWL_VP7_E) |
							(1 << DWL_WEBP_E);
				tmp = (reg & mask);
				if (tmp & (1 << DWL_VP8_E)) {
					LOG_INFO("subsys[%d] has VP8\n", c);
				}
				if (tmp & (1 << DWL_VP7_E)) {
					LOG_INFO("subsys[%d] has VP7\n", c);
				}
				if (tmp & (1 << DWL_WEBP_E)) {
					LOG_INFO("subsys[%d] has WebP\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_VP8_DEC : 0;

				tmp = (reg >> DWL_AVS_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has AVS\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_AVS_DEC : 0;

				tmp = (reg >> DWL_RV_E) & 0x03U;
				if (tmp) {
					LOG_INFO("subsys[%d] has RV\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_RV_DEC : 0;

				/* Post-processor configuration */
				reg = ioread32((void __iomem *)(dev->hwregs[c][j] +
							   HANTROPP_SYNTH_CFG * 4));

				tmp = (reg >> DWL_G1_PP_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has PP\n", c);
				}
				config.cfg[c] |=
				  tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;
			} else if ((IS_G2(dev->hw_id[c][j]))) {
				reg = ioread32((void __iomem *)
							   (dev->hwregs[c][j] +
							   HANTRODEC_CFG_STAT * 4));

				tmp = (reg >> DWL_G2_HEVC_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has HEVC\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_HEVC_DEC : 0;

				tmp = (reg >> DWL_G2_VP9_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has VP9\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_VP9_DEC : 0;

				/* Post-processor configuration */
				reg = ioread32((void __iomem *)
					(dev->hwregs[c][j] +
					HANTRODECPP_SYNTH_CFG * 4));

				tmp = (reg >> DWL_G2_PP_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has PP\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;
			} else if ((IS_VC8000D(dev->hw_id[c][j])) &&
					config.its_main_core_id[c] < 0) {
				reg = ioread32((void __iomem *)(dev->hwregs[c][j] +
					HANTRODEC_SYNTH_CFG * 4));

				LOG_INFO("subsys[%d] swreg[%d] = 0x%08x\n",
					c, HANTRODEC_SYNTH_CFG, reg);

				tmp = (reg >> DWL_H264_E) & 0x3U;
				if (tmp) {
					LOG_INFO("subsys[%d] has H264\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

				tmp = (reg >> DWL_H264HIGH10_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has H264HIGH10\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC : 0;

				tmp = (reg >> DWL_AVS2_E) & 0x03U;
				if (tmp) {
					LOG_INFO("subsys[%d] has AVS2\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_AVS2_DEC : 0;

				tmp = (reg >> DWL_AV1_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has AV1\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_AV1_DEC : 0;

				tmp = (reg >> DWL_JPEG_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has JPEG\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

				tmp = (reg >> DWL_HJPEG_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has HJPEG\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC : 0;

				tmp = (reg >> DWL_MPEG4_E) & 0x3U;
				if (tmp) {
					LOG_INFO("subsys[%d] has MPEG4\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_MPEG4_DEC : 0;

				tmp = (reg >> DWL_VC1_E) & 0x3U;
				if (tmp) {
					LOG_INFO("subsys[%d] has VC1\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_VC1_DEC : 0;

				tmp = (reg >> DWL_MPEG2_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has MPEG2\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_MPEG2_DEC : 0;

				tmp = (reg >> DWL_VP6_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has VP6\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_VP6_DEC : 0;

				reg = ioread32((void __iomem *)
					(dev->hwregs[c][j] +
					HANTRODEC_SYNTH_CFG_2 * 4));

				LOG_INFO("subsys[%d] swreg[%d] = 0x%08x\n",
					c, HANTRODEC_SYNTH_CFG_2, reg);

				if (ioread32((void __iomem *)
					(dev->hwregs[c][j] +
					HANTRODEC_HWBUILD_ID_OFF)) !=
					0x1F70) {
					/* VP7 and WEBP is part of VP8 */
					mask = (1 << DWL_VP8_E) |
						(1 << DWL_VP7_E) |
						(1 << DWL_WEBP_E);
					tmp = (reg & mask);
					if (tmp & (1 << DWL_VP8_E)) {
						LOG_INFO("subsys[%d] has VP8\n", c);
					}
					if (tmp & (1 << DWL_VP7_E)) {
						LOG_INFO("subsys[%d] has VP7\n", c);
					}
					if (tmp & (1 << DWL_WEBP_E)) {
						LOG_INFO("subsys[%d] has WebP\n", c);
					}
					config.cfg[c] |=
					  tmp ? 1 << DWL_CLIENT_TYPE_VP8_DEC :
					  0;
				}

				tmp = (reg >> DWL_AVS_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has AVS\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_AVS_DEC : 0;

				tmp = (reg >> DWL_RV_E) & 0x03U;
				if (tmp) {
					LOG_INFO("subsys[%d] has RV\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_RV_DEC : 0;

				reg = ioread32((void __iomem *)
					  (dev->hwregs[c][j] +
					  HANTRODEC_SYNTH_CFG_3 * 4));
				LOG_INFO("subsys[%d] swreg[%d] = 0x%08x\n",
					c, HANTRODEC_SYNTH_CFG_3, reg);

				tmp = (reg >> DWL_HEVC_E) & 0x07U;
				if (tmp) {
					LOG_INFO("subsys[%d] has HEVC\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_HEVC_DEC : 0;

				tmp = (reg >> DWL_VP9_E) & 0x07U;
				if (tmp) {
					LOG_INFO("subsys[%d] has VP9\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_VP9_DEC : 0;

				/* Post-processor configuration */
				reg = ioread32((void __iomem *)
					  (dev->hwregs[c][j] +
					  HANTRODECPP_CFG_STAT * 4));

				tmp = (reg >> DWL_PP_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has PP\n", c);
				}
				config.cfg[c] |=
					tmp ? 1 << DWL_CLIENT_TYPE_PP : 0;

				config.cfg[c] |= 1 << DWL_CLIENT_TYPE_ST_PP;

				if (config.its_aux_core_id[c] >= 0) {
					/* set main_core_id and aux_core_id */
					reg = ioread32((void __iomem *)
						(dev->hwregs[c][j] +
						HANTRODEC_SYNTH_CFG_2 *
						4));

					tmp = (reg >> DWL_H264_PIPELINE_E) &
					0x01U;
					if (tmp) {
						LOG_INFO("subsys[%d] has pipeline H264\n", c);
					}
					config.cfg[config.its_aux_core_id[c]] |=
					  tmp ? 1 << DWL_CLIENT_TYPE_H264_DEC :
					  0;

					tmp = (reg >> DWL_JPEG_PIPELINE_E) &
					0x01U;
					if (tmp) {
						LOG_INFO("subsys[%d] has pipeline JPEG\n", c);
					}
					config.cfg[config.its_aux_core_id[c]] |=
					  tmp ? 1 << DWL_CLIENT_TYPE_JPEG_DEC :
					  0;
				}
			} else if (IS_BIGOCEAN(dev->hw_id[c][j])) {
				reg = ioread32((void __iomem *)
					  (dev->hwregs[c][j] +
					  BIGOCEANDEC_CFG * 4));

				tmp = (reg >> BIGOCEANDEC_AV1_E) & 0x01U;
				if (tmp) {
					LOG_INFO("subsys[%d] has AV1 (BigOcean)\n", c);
				}
				config.cfg[c] |=
					tmp ? 1U << DWL_CLIENT_TYPE_BO_AV1_DEC :
					0;
			}
		}
	}
	memcpy(config.cfg_backup, config.cfg, sizeof(config.cfg));
}

static int CoreHasFormat(const u32 *cfg, int core, u32 format)
{
	return (cfg[core] & (1 << format)) ? 1 : 0;
}

static int GetDecCore(long core, hantrodec_t *dev, struct file *filp,
		      unsigned long format)
{
	int success = 0;
	unsigned long flags;

	spin_lock_irqsave(&owner_lock, flags);
	if (CoreHasFormat(config.cfg, core, format) &&
	    !dec_owner[core] /*&& config.its_main_core_id[core] >= 0*/) {
		dec_owner[core] = filp;
		success = 1;

		/* If one main core takes one format which
		 * doesn't supported by aux core, set aux
		 * core's cfg to none video format support
		 */
		if (config.its_aux_core_id[core] >= 0 &&
		    !CoreHasFormat(config.cfg, config.its_aux_core_id[core],
		    format)) {
			config.cfg[config.its_aux_core_id[core]] = 0;
		}
		/* If one aux core takes one format, set main core's
		 * cfg to aux core supported video format
		 */
		else if (config.its_main_core_id[core] >= 0) {
			config.cfg[config.its_main_core_id[core]] =
				config.cfg[core];
		}
	}

	spin_unlock_irqrestore(&owner_lock, flags);

	return success;
}

static int GetDecCoreAny(long *core, hantrodec_t *dev,
			 struct file *filp,
			 unsigned long format)
{
	int success = 0;
	long c;

	*core = -1;

	for (c = 0; c < dev->cores; c++) {
		/* a free core that has format */
		if (GetDecCore(c, dev, filp, format)) {
			success = 1;
			*core = c;
			break;
		}
	}

	return success;
}

static int GetDecCoreID(hantrodec_t *dev,
			struct file *filp,
			unsigned long format)
{
	long c;
	unsigned long flags;

	int core_id = -1;

	for (c = 0; c < dev->cores; c++) {
		/* a core that has format */
		spin_lock_irqsave(&owner_lock, flags);
		if (CoreHasFormat(config.cfg, c, format)) {
			core_id = c;
			spin_unlock_irqrestore(&owner_lock, flags);
			break;
		}
		spin_unlock_irqrestore(&owner_lock, flags);
	}
	return core_id;
}

#if 0
static int hantrodec_choose_core(int is_g1)
{
		volatile unsigned char *reg = NULL;
		unsigned int blk_base = 0x38320000;

		LOG_DBG("%s\n", __func__);
		if (!request_mem_region(blk_base, 0x1000, "blk_ctl")) {
			LOG_INFO("blk_ctl: failed to reserve HW regs\n");
	return -EBUSY;
		}

		reg = (volatile u8 *)ioremap_nocache(blk_base, 0x1000);

		if (!reg) {
			LOG_INFO("blk_ctl: failed to ioremap HW regs\n");
			if (reg)
				iounmap((void *)reg);
				release_mem_region(blk_base, 0x1000);
				return -EBUSY;
		}

		// G1 use, set to 1; G2 use, set to 0,
		//choose the one you are using
		if (is_g1)
		// VPUMIX only use G1, user should modify
		//the reg according to platform design
			iowrite32(0x1, (void *)(reg + 0x14));
		else
		// VPUMIX only use G2, user should modify
		//the reg according to platform design
			iowrite32(0x0, (void *)(reg + 0x14));

		if (reg)
			iounmap((void *)reg);
		release_mem_region(blk_base, 0x1000);
		LOG_DBG("%s OK!\n", __func__);
		return 0;
}
#endif

static long ReserveDecoder(hantrodec_t *dev,
			   struct file *filp,
	 unsigned long format)
{
	long core = -1;

	/* reserve a core */
	if (down_interruptible(&dec_core_sem))
		return -ERESTARTSYS;

	/* lock a core that has specific format*/
	if (wait_event_interruptible(hw_queue,
				     GetDecCoreAny(&core, dev,
						   filp, format) != 0))

		return -ERESTARTSYS;

#if 0
	if (IS_G1(dev->hw_id[core])) {
		if (hantrodec_choose_core(1) == 0)
			LOG_INFO("G1 is reserved\n");
		else
			return -1;
	} else {
		if (hantrodec_choose_core(0) == 0)
			LOG_INFO("G2 is reserved\n");
		else
			return -1;
	}
#endif

	dev->client_type[core] = format;
	return core;
}

#define L2CACHE_E_OFF 0x204
#define SHAPER_E_OFF 0x20
#define SHAPER_INT_OFF 0x2C

/* Release L2Cache when process is killed. */
static void ReleaseL2Cache(hantrodec_t *dev, long core)
{
	/* If L2Cache is enabled, disable cache/shaper. */
	u32 asic_id;

	if (!vpu_subsys[core].submodule_hwregs[HW_L2CACHE])
		return;

	asic_id = ioread32((void __iomem *)
	  vpu_subsys[core].submodule_hwregs[HW_L2CACHE]);
	asic_id = (asic_id >> 16) & 0x3;
	if (asic_id != 2) {
		/* Disable cache if it exists. */
		LOG_INFO("DEC[%li] disabled L2Cache\n", core);
		iowrite32(0, (void __iomem *)
		  (vpu_subsys[core].submodule_hwregs[HW_L2CACHE] +
		  L2CACHE_E_OFF));
	}
	if (asic_id != 1) {
		int i = 0, tmp;
		/* Disable shaper if it exists. */
		iowrite32(0, (void __iomem *)
		  (vpu_subsys[core].submodule_hwregs[HW_L2CACHE] +
		  SHAPER_E_OFF));
		/* Check shaper abort interrupt */
		for (i = 0; i < 100; i++) {
			tmp = ioread32((void __iomem *)
				  (vpu_subsys[core].submodule_hwregs[HW_L2CACHE] +
				  SHAPER_INT_OFF));
			if (tmp & 0x2) {
				LOG_INFO(
				 "DEC[%li] disabled shaper DONE\n",
				 core);
				iowrite32(tmp, (void __iomem *)
				  (vpu_subsys[core].submodule_hwregs[HW_L2CACHE] +
				  SHAPER_INT_OFF));
				break;
			}
		}
		if (i == 100) {
			LOG_INFO("DEC[%li] disabled shaper FAILED!\n", core);
		}
	}
}

static void ReleaseDecoder(hantrodec_t *dev, long core)
{
	u32 status;
	unsigned long flags;

	LOG_DBG("%s %ld\n", __func__, core);

	if (dev->client_type[core] == DWL_CLIENT_TYPE_BO_AV1_DEC)
		status = ioread32((void __iomem *)
			(dev->hwregs[core][HW_BIGOCEAN] +
			BIGOCEAN_IRQ_STAT_DEC_OFF));
	else
		status = ioread32((void __iomem *)
			(dev->hwregs[core][HW_VC8000D] +
			HANTRODEC_IRQ_STAT_DEC_OFF));

	/* make sure HW is disabled */
	if (status & HANTRODEC_DEC_E) {
		LOG_INFO("DEC[%li] still enabled -> reset\n", core);

		/* abort decoder */
		status |= HANTRODEC_DEC_ABORT | HANTRODEC_DEC_IRQ_DISABLE;
		iowrite32(status, (void __iomem *)
		  (dev->hwregs[core][HW_VC8000D] +
		  HANTRODEC_IRQ_STAT_DEC_OFF));
	}

	spin_lock_irqsave(&owner_lock, flags);

	/* If aux core released, revert main core's config back */
	if (config.its_main_core_id[core] >= 0) {
		config.cfg[config.its_main_core_id[core]] =
			config.cfg_backup[config.its_main_core_id[core]];
	}

	/* If main core released, revert aux core's config back */
	if (config.its_aux_core_id[core] >= 0) {
		config.cfg[config.its_aux_core_id[core]] =
			config.cfg_backup[config.its_aux_core_id[core]];
	}

	dec_owner[core] = NULL;
	dec_irq &= ~(1 << core);

	spin_unlock_irqrestore(&owner_lock, flags);

	up(&dec_core_sem);

	wake_up_interruptible_all(&hw_queue);
}

#if 0
static long ReservePostProcessor(hantrodec_t *dev, struct file *filp)
{
	unsigned long flags;

	long core = 0;

	/* single core PP only */
	if (down_interruptible(&pp_core_sem))
		return -ERESTARTSYS;

	spin_lock_irqsave(&owner_lock, flags);

	pp_owner[core] = filp;

	spin_unlock_irqrestore(&owner_lock, flags);

	return core;
}
#endif

static void ReleasePostProcessor(hantrodec_t *dev, long core)
{
	unsigned long flags;

	u32 status = ioread32((void __iomem *)(dev->hwregs[core][HW_VC8000D] +
			 HANTRO_IRQ_STAT_PP_OFF));

	/* make sure HW is disabled */
	if (status & HANTRO_PP_E) {
		LOG_INFO("PP[%li] still enabled -> reset\n", core);

		/* disable IRQ */
		status |= HANTRO_PP_IRQ_DISABLE;

		/* disable postprocessor */
		status &= (~HANTRO_PP_E);
		iowrite32(0x10, (void __iomem *)(dev->hwregs[core][HW_VC8000D] +
				  HANTRO_IRQ_STAT_PP_OFF));
	}

	spin_lock_irqsave(&owner_lock, flags);

	pp_owner[core] = NULL;

	spin_unlock_irqrestore(&owner_lock, flags);

	up(&pp_core_sem);
}

#if 0
static long ReserveDecPp(hantrodec_t *dev, struct file *filp,
			 unsigned long format)
{
	/* reserve core 0, DEC+PP for pipeline */
	unsigned long flags;

	long core = 0;

	/* check that core has the requested dec format */
	if (!CoreHasFormat(config.cfg, core, format))
		return -EFAULT;

	/* check that core has PP */
	if (!CoreHasFormat(config.cfg, core, DWL_CLIENT_TYPE_PP))
		return -EFAULT;

	/* reserve a core */
	if (down_interruptible(&dec_core_sem))
		return -ERESTARTSYS;

	/* wait until the core is available */
	if (wait_event_interruptible(hw_queue, GetDecCore(core, dev, filp,
							  format) != 0)) {
		up(&dec_core_sem);
		return -ERESTARTSYS;
	}

	if (down_interruptible(&pp_core_sem)) {
		ReleaseDecoder(dev, core);
		return -ERESTARTSYS;
	}

	spin_lock_irqsave(&owner_lock, flags);
	pp_owner[core] = filp;
	spin_unlock_irqrestore(&owner_lock, flags);

	return core;
}
#endif

#ifdef HANTRODEC_DEBUG
static u32 flush_count; /* times of calling of DecFlushRegs */
static u32 flush_regs; /* total number of registers flushed */
#endif

static long DecFlushRegs(hantrodec_t *dev, struct core_desc *core)
{
	long ret = 0, i;
#ifdef HANTRODEC_DEBUG
	int reg_wr = 2;
#endif
	u32 id = core->id;
	u32 type = core->type;

	LOG_DBG("%s\n", __func__);
	LOG_DBG("id = %d, type = %d [ %s ], size = %d, reg_id = %d\n",
	       core->id, core->type, CoreTypeStr(core->type), core->size,
	       core->reg_id);

	if (type == HW_VC8000D && !vpu_subsys[id].submodule_hwregs[type])
		type = HW_VC8000DJ;
	if (dev->client_type[id] == DWL_CLIENT_TYPE_BO_AV1_DEC)
		type = HW_BIGOCEAN;

	if (id >= MAX_SUBSYS_NUM || !vpu_subsys[id].base_addr ||
	    core->type >= HW_CORE_MAX || !vpu_subsys[id].submodule_hwregs[type])
		return -EINVAL;

	LOG_DBG("submodule_iosize = %d\n",
	       vpu_subsys[id].submodule_iosize[type]);
//	LOG_DBG("reg count = %d\n", reg_count[id]);

	ret = copy_from_user(dec_regs[id], (__u32 __user *)core->regs,
			     vpu_subsys[id].submodule_iosize[type]);
	if (ret) {
		LOG_DBG("copy_from_user failed, returned %li\n", ret);
		return -EFAULT;
	}

	if (type == HW_VC8000D || type == HW_BIGOCEAN || type == HW_VC8000DJ) {
		/* write all regs but the status reg[1] to hardware */
		if (reg_access_opt) {
			for (i = 3;
			     i < vpu_subsys[id].submodule_iosize[type] / 4;
			     i++) {
				/* check whether register value is updated. */
				if (dec_regs[id][i] != shadow_dec_regs[id][i]) {
					iowrite32(dec_regs[id][i], (void __iomem *)
					  (dev->hwregs[id][type] + i * 4));
					shadow_dec_regs[id][i] = dec_regs[id][i];

#ifdef HANTRODEC_DEBUG
					reg_wr++;
#endif
				}
			}
		} else {
			for (i = 3;
			     i < vpu_subsys[id].submodule_iosize[type] / 4;
			     i++) {
				iowrite32(dec_regs[id][i], (void __iomem *)
						  (dev->hwregs[id][type] + i * 4));

#ifdef VALIDATE_REGS_WRITE
	if (dec_regs[id][i] !=
			ioread32((void *)(dev->hwregs[id][type] +
			  i * 4))) {
		LOG_INFO("swreg[%ld]: read %08x != write %08x *\n",
			i,
			ioread32((void *)(dev->hwregs[id][type] + i * 4)),
			dec_regs[id][i]);
	}
#endif
			}
#ifdef HANTRODEC_DEBUG
			reg_wr = vpu_subsys[id].submodule_iosize[type] / 4 - 1;
#endif
		}

		/* write swreg2 for AV1, in which bit0 is the start bit */
		iowrite32(dec_regs[id][2],
			  (void __iomem *)(dev->hwregs[id][type] + 8));
		shadow_dec_regs[id][2] = dec_regs[id][2];

		/* write the status register, which may start the decoder */
		iowrite32(dec_regs[id][1],
			  (void __iomem *)(dev->hwregs[id][type] + 4));
		shadow_dec_regs[id][1] = dec_regs[id][1];

#ifdef HANTRODEC_DEBUG
		flush_count++;
		flush_regs += reg_wr;
#endif

		LOG_DBG("flushed registers on core %d\n", id);
#ifdef HANTRODEC_DEBUG
		LOG_DBG("%d %s: flushed %d/%d registers (dec_mode = %d, avg %d regs per flush)\n",
		       flush_count, __func__,
			   reg_wr, flush_regs,
			   dec_regs[id][3] >> 27,
			   flush_regs / flush_count);
#endif
	} else {
		/* write all regs but the status reg[1] to hardware */
		for (i = 0; i < vpu_subsys[id].submodule_iosize[type] / 4;
		     i++) {
			iowrite32(dec_regs[id][i],
				  (void __iomem *)(dev->hwregs[id][type] +
				  i * 4));
#ifdef VALIDATE_REGS_WRITE
			if (dec_regs[id][i] !=
			    ioread32((void *)(dev->hwregs[id][type] + i * 4))) {
				LOG_INFO(
				       "swreg[%ld]: read %08x != write %08x *\n",
				       i,
				       ioread32((void *)(dev->hwregs[id][type] +
				       i * 4)),
				       dec_regs[id][i]);
				}
#endif
		}
	}

	return 0;
}

static long DecWriteRegs(hantrodec_t *dev, struct core_desc *core)
{
	long ret = 0;
	u32 i = core->reg_id;
	u32 id = core->id;
	u32 type = core->type;

	LOG_DBG("%s\n", __func__);
	LOG_DBG("id = %d, type = %d [ %s ], size = %d, reg_id = %d\n",
	       core->id, core->type, CoreTypeStr(core->type), core->size,
	       core->reg_id);

	if (type == HW_VC8000D && !vpu_subsys[id].submodule_hwregs[type])
		type = HW_VC8000DJ;
	if (dev->client_type[id] == DWL_CLIENT_TYPE_BO_AV1_DEC)
		type = HW_BIGOCEAN;

	if (id >= MAX_SUBSYS_NUM || !vpu_subsys[id].base_addr ||
	    type >= HW_CORE_MAX || !vpu_subsys[id].submodule_hwregs[type] ||
	    (core->size & 0x3) || core->reg_id * 4 + core->size >
	    vpu_subsys[id].submodule_iosize[type])

		return -EINVAL;

	ret = copy_from_user(dec_regs[id], (__u32 __user *)core->regs,
			     core->size);
	if (ret) {
		LOG_DBG("copy_from_user failed, returned %li\n", ret);
		return -EFAULT;
	}

	for (i = core->reg_id; i < core->reg_id + core->size / 4; i++) {
		LOG_DBG("write %08x to reg[%d] core %d\n",
		       dec_regs[id][i - core->reg_id], i, id);
		iowrite32(dec_regs[id][i - core->reg_id],
			  (void __iomem *)(dev->hwregs[id][type] + i * 4));
		if (type == HW_VC8000D)
			shadow_dec_regs[id][i] = dec_regs[id][i - core->reg_id];
	}
	return 0;
}

static long DecWriteApbFilterRegs(hantrodec_t *dev, struct core_desc *core)
{
	long ret = 0;
	u32 i = core->reg_id;
	u32 id = core->id;

	LOG_DBG("%s\n", __func__);
	LOG_DBG("id = %d, type = %d, size = %d, reg_id = %d\n",
	       core->id, core->type, core->size, core->reg_id);

	if (id >= MAX_SUBSYS_NUM || !vpu_subsys[id].base_addr ||
	    core->type >= HW_CORE_MAX ||
	    !vpu_subsys[id].submodule_hwregs[core->type] ||
	    (core->size & 0x3) ||
	    core->reg_id * 4 + core->size >
	    vpu_subsys[id].submodule_iosize[core->type] + 4)

		return -EINVAL;

	ret = copy_from_user(apbfilter_regs[id], (__u32 __user *)core->regs,
			     core->size);
	if (ret) {
		LOG_DBG("copy_from_user failed, returned %li\n", ret);
		return -EFAULT;
	}

	for (i = core->reg_id; i < core->reg_id + core->size / 4; i++) {
		LOG_DBG("write %08x to reg[%d] core %d\n",
		       dec_regs[id][i - core->reg_id], i, id);
		iowrite32(apbfilter_regs[id][i - core->reg_id],
			  (void __iomem *)
				  (dev->apbfilter_hwregs[id][core->type] +
				  i * 4));
	}
	return 0;
}

static long DecReadRegs(hantrodec_t *dev, struct core_desc *core)
{
	long ret;
	u32 id = core->id;
	u32 i = core->reg_id;
	u32 type = core->type;

	LOG_DBG("%s\n", __func__);
	LOG_DBG("id = %d, type = %d [ %s ], size = %d, reg_id = %d\n",
	       core->id, core->type, CoreTypeStr(core->type), core->size,
	 core->reg_id);

	if (type == HW_VC8000D && !vpu_subsys[id].submodule_hwregs[type])
		type = HW_VC8000DJ;
	if (dev->client_type[id] == DWL_CLIENT_TYPE_BO_AV1_DEC)
		type = HW_BIGOCEAN;

	if (id >= MAX_SUBSYS_NUM || !vpu_subsys[id].base_addr ||
	    type >= HW_CORE_MAX || !vpu_subsys[id].submodule_hwregs[type] ||
			(core->size & 0x3) ||
			core->reg_id * 4 + core->size >
			vpu_subsys[id].submodule_iosize[type])
		return -EINVAL;

	/* read specific registers from hardware */
	for (i = core->reg_id; i < core->reg_id + core->size / 4; i++) {
		dec_regs[id][i - core->reg_id] = ioread32(
			(void __iomem *)(dev->hwregs[id][type] + i * 4));
		LOG_DBG("read %08x from reg[%d] core %d\n",
		       dec_regs[id][i - core->reg_id], i, id);
		if (type == HW_VC8000D)
			shadow_dec_regs[id][i] = dec_regs[id][i];
	}

	/* put registers to user space*/
	ret = copy_to_user((__u32 __user *)core->regs, dec_regs[id],
			   core->size);
	if (ret) {
		LOG_DBG("copy_to_user failed, returned %li\n", ret);
		return -EFAULT;
	}
	return 0;
}

static long DecRefreshRegs(hantrodec_t *dev, struct core_desc *core)
{
	long ret, i;
	u32 id = core->id;
	u32 type = core->type;

	LOG_DBG("%s\n", __func__);
	LOG_DBG("id = %d, type = %d [ %s ], size = %d, reg_id = %d\n",
	       core->id, core->type, CoreTypeStr(core->type), core->size,
	 core->reg_id);

	if (type == HW_VC8000D && !vpu_subsys[id].submodule_hwregs[type])
		type = HW_VC8000DJ;
	if (dev->client_type[id] == DWL_CLIENT_TYPE_BO_AV1_DEC)
		type = HW_BIGOCEAN;

	if (id >= MAX_SUBSYS_NUM || !vpu_subsys[id].base_addr ||
	    type >= HW_CORE_MAX || !vpu_subsys[id].submodule_hwregs[type])
		return -EINVAL;

	LOG_DBG("submodule_iosize = %d\n",
	       vpu_subsys[id].submodule_iosize[type]);

	if (!reg_access_opt) {
		for (i = 0; i < vpu_subsys[id].submodule_iosize[type] / 4; i++) {
			dec_regs[id][i] = ioread32((void __iomem *)(dev->hwregs[id][type] + i * 4));
		}
	} else {
		// only need to read swreg1,62(?),63,168,169
#define REFRESH_REG(idx)                                                       \
	do {                                                                   \
		i = (idx);                                                     \
		shadow_dec_regs[id][i] = dec_regs[id][i] = ioread32(           \
			(void __iomem *)(dev->hwregs[id][type] + i * 4));      \
	} while (0)

		REFRESH_REG(0);
		REFRESH_REG(1);
		REFRESH_REG(62);
		REFRESH_REG(63);
		REFRESH_REG(168);
		REFRESH_REG(169);
#undef REFRESH_REG
	}

	ret = copy_to_user((__u32 __user *)core->regs, dec_regs[id],
			   vpu_subsys[id].submodule_iosize[type]);
	if (ret) {
		LOG_DBG("copy_to_user failed, returned %li\n", ret);
		return -EFAULT;
	}
	return 0;
}

static int CheckDecIrq(hantrodec_t *dev, int id)
{
	unsigned long flags;
	int rdy = 0;

	const u32 irq_mask = (1 << id);

	spin_lock_irqsave(&owner_lock, flags);

	if (dec_irq & irq_mask) {
		/* reset the wait condition(s) */
		dec_irq &= ~irq_mask;
		rdy = 1;
	}

	spin_unlock_irqrestore(&owner_lock, flags);

	return rdy;
}

static long WaitDecReadyAndRefreshRegs(hantrodec_t *dev, struct core_desc *core)
{
	u32 id = core->id;
	long ret;

	LOG_DBG("wait_event_interruptible DEC[%d]\n", id);
#ifdef USE_SW_TIMEOUT
	u32 status;

	ret = wait_event_interruptible_timeout(dec_wait_queue,
					       CheckDecIrq(dev, id),
		  msecs_to_jiffies(2000));
	if (ret < 0) {
		LOG_DBG("DEC[%d]  wait_event_interruptible interrupted\n", id);
		return -ERESTARTSYS;
	} else if (ret == 0) {
		LOG_DBG("DEC[%d]  wait_event_interruptible timeout\n", id);
		status = ioread32((void *)(dev->hwregs[id][HW_VC8000D] +
		HANTRODEC_IRQ_STAT_DEC_OFF));
		/* check if HW is enabled */
		if (status & HANTRODEC_DEC_E) {
			LOG_INFO("DEC[%d] reset becuase of timeout\n", id);

			/* abort decoder */
			status |= HANTRODEC_DEC_ABORT |
					  HANTRODEC_DEC_IRQ_DISABLE;
			iowrite32(status, (void *)(dev->hwregs[id][HW_VC8000D] +
		HANTRODEC_IRQ_STAT_DEC_OFF));
		}
	}
#else
	ret = wait_event_interruptible(dec_wait_queue, CheckDecIrq(dev, id));
	if (ret) {
		LOG_DBG("DEC[%d]  wait_event_interruptible interrupted\n", id);
		return -ERESTARTSYS;
	}
#endif
	atomic_inc(&irq_tx);

	/* refresh registers */
	return DecRefreshRegs(dev, core);
}

#if 0
long PPFlushRegs(hantrodec_t *dev, struct core_desc *core)
{
		long ret = 0;
		u32 id = core->id;
		u32 i;

		/* copy original dec regs to kernal space*/
		ret = copy_from_user(dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
				     core->regs +
				     HANTRO_PP_ORG_FIRST_REG,
	HANTRO_PP_ORG_REGS * 4);
		if (sizeof(void *) == 8) {
			/* copy extended dec regs to kernal space*/
			ret = copy_from_user(dec_regs[id] +
					     HANTRO_PP_EXT_FIRST_REG,
				       core->regs + HANTRO_PP_EXT_FIRST_REG,
					     HANTRO_PP_EXT_REGS * 4);
		}
		if (ret) {
			LOG_DBG("copy_from_user failed, returned %li\n", ret);
	return -EFAULT;
		}

		/* write all regs but the status reg[1] to hardware */
		/* both original and extended regs need to be written */
		for (i = HANTRO_PP_ORG_FIRST_REG + 1;
	 i <= HANTRO_PP_ORG_LAST_REG; i++)
			iowrite32(dec_regs[id][i],
				  (void *)(dev->hwregs[id] + i * 4));
		if (sizeof(void *) == 8) {
			for (i = HANTRO_PP_EXT_FIRST_REG;
			     i <= HANTRO_PP_EXT_LAST_REG; i++)
				iowrite32(dec_regs[id][i],
					  (void *)(dev->hwregs[id] + i * 4));
		}
		/* write the stat reg, which may start the PP */
		iowrite32(dec_regs[id][HANTRO_PP_ORG_FIRST_REG],
			  (void *)(dev->hwregs[id] +
			  HANTRO_PP_ORG_FIRST_REG * 4));

		return 0;
}

long PPRefreshRegs(hantrodec_t *dev, struct core_desc *core)
{
		long i, ret;
		u32 id = core->id;

		if (sizeof(void *) == 8) {
			/* user has to know exactly what they are asking for */
			if (core->size != (HANTRO_PP_TOTAL_REGS * 4))
				return -EFAULT;
		}
		/* user has to know exactly what they are asking for */
		if (core->size != (HANTRO_PP_ORG_REGS * 4))
			return -EFAULT;

		/* read all registers from hardware */
		/* both original and extended regs need to be read */
		for (i = HANTRO_PP_ORG_FIRST_REG;
		     i <= HANTRO_PP_ORG_LAST_REG; i++)
			dec_regs[id][i] = ioread32((void *)
				 (dev->hwregs[id] + i * 4));
		if (sizeof(void *) == 8) {
			for (i = HANTRO_PP_EXT_FIRST_REG;
			     i <= HANTRO_PP_EXT_LAST_REG; i++)
				dec_regs[id][i] = ioread32((void *)
					 (dev->hwregs[id] + i * 4));
		}
		/* put registers to user space*/
		/* put original registers to user space*/
		ret = copy_to_user(core->regs + HANTRO_PP_ORG_FIRST_REG,
				   dec_regs[id] + HANTRO_PP_ORG_FIRST_REG,
			HANTRO_PP_ORG_REGS * 4);
		if (sizeof(void *) == 8) {
			/* put extended registers to user space*/
			ret = copy_to_user(core->regs + HANTRO_PP_EXT_FIRST_REG,
					   dec_regs[id] +
			 HANTRO_PP_EXT_FIRST_REG,
			HANTRO_PP_EXT_REGS * 4);
		}
		if (ret) {
			LOG_DBG("copy_to_user failed, returned %li\n", ret);
			return -EFAULT;
		}

		return 0;
}

static int CheckPPIrq(hantrodec_t *dev, int id)
{
		unsigned long flags;
		int rdy = 0;

		const u32 irq_mask = (1 << id);

		spin_lock_irqsave(&owner_lock, flags);

		if (pp_irq & irq_mask) {
			/* reset the wait condition(s) */
			pp_irq &= ~irq_mask;
			rdy = 1;
		}

		spin_unlock_irqrestore(&owner_lock, flags);

		return rdy;
}

long WaitPPReadyAndRefreshRegs(hantrodec_t *dev, struct core_desc *core)
{
		u32 id = core->id;

		LOG_DBG("wait_event_interruptible PP[%d]\n", id);

		if (wait_event_interruptible(pp_wait_queue,
					     CheckPPIrq(dev, id))) {
			LOG_DBG("PP[%d]  wait_event_interruptible interrupted\n",
			       id);
			return -ERESTARTSYS;
		}

		atomic_inc(&irq_tx);

		/* refresh registers */
		return PPRefreshRegs(dev, core);
}
#endif

static int CheckCoreIrq(hantrodec_t *dev, const struct file *filp, int *id)
{
	unsigned long flags;
	int rdy = 0, n = 0;

	do {
		u32 irq_mask = (1 << n);

		spin_lock_irqsave(&owner_lock, flags);

		if (dec_irq & irq_mask) {
			if (dec_owner[n] == filp) {
				/* we have an IRQ for our client */

				/* reset the wait condition(s) */
				dec_irq &= ~irq_mask;

				/* signal ready core no. for our client */
				*id = n;

				rdy = 1;

				spin_unlock_irqrestore(&owner_lock, flags);
				break;
			} else if (!dec_owner[n]) {
				/* zombie IRQ */
				LOG_INFO("IRQ on core[%d], but no owner!!!\n",
					n);

				/* reset the wait condition(s) */
				dec_irq &= ~irq_mask;
			}
		}

		spin_unlock_irqrestore(&owner_lock, flags);

		n++; /* next core */
	} while (n < dev->cores);

	return rdy;
}

static long WaitCoreReady(hantrodec_t *dev, const struct file *filp, int *id)
{
	long ret;

	LOG_DBG("wait_event_interruptible CORE\n");
#ifdef USE_SW_TIMEOUT
	u32 i, status;

	ret = wait_event_interruptible_timeout(dec_wait_queue,
					       CheckCoreIrq(dev, filp, id),
		msecs_to_jiffies(2000));
	if (ret < 0) {
		LOG_DBG("CORE  wait_event_interruptible interrupted\n");
		return -ERESTARTSYS;
	} else if (ret == 0) {
		LOG_DBG("CORE  wait_event_interruptible timeout\n");
		for (i = 0; i < dev->cores; i++) {
			status = ioread32((void *)(dev->hwregs[i][HW_VC8000D] +
			HANTRODEC_IRQ_STAT_DEC_OFF));
			/* check if HW is enabled */
			if ((status & HANTRODEC_DEC_E) &&
			    dec_owner[i] == filp) {
				LOG_INFO("CORE[%d] reset becuase of timeout\n",
					i);
				*id = i;
				/* abort decoder */
				status |= HANTRODEC_DEC_ABORT |
						HANTRODEC_DEC_IRQ_DISABLE;
				iowrite32(status,
					  (void *)(dev->hwregs[i][HW_VC8000D] +
					HANTRODEC_IRQ_STAT_DEC_OFF));
				break;
			}
		}
	}
#else
	ret = wait_event_interruptible(dec_wait_queue,
				       CheckCoreIrq(dev, filp, id));
	if (ret) {
		LOG_DBG("CORE[%d] wait_event_interruptible interrupted with 0x%lx\n",
		       *id, ret);
		return -ERESTARTSYS;
	}
#endif
	atomic_inc(&irq_tx);

	return 0;
}

/*---------------------------------------------------------
 * Function name : hantrodec_ioctl
 * Description   : communication method to/from the user space
 * Return type   : long
 *----------------------------------------------------------
 */
static long hantrodec_ioctl(struct file *filp, unsigned int cmd,
			    unsigned long arg)
{
	int err = 0;
	long tmp;
	u32 i = 0;
#ifdef CLK_CFG
	unsigned long flags;
#endif

#ifdef HW_PERFORMANCE
	struct timeval *end_time_arg;
#endif
#ifdef HANTRODEC_DEBUG
	LOG_DBG("ioctl cmd 0x%08x [ %s ]\n", cmd, IoctlCmdStr(cmd));
#endif
	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl)
	 * before access_ok()
	 */
	if (_IOC_TYPE(cmd) != HANTRODEC_IOC_MAGIC &&
	    _IOC_TYPE(cmd) != HANTRO_IOC_MMU &&
	    _IOC_TYPE(cmd) != HANTRO_VCMD_IOC_MAGIC)

		return -ENOTTY;
	if ((_IOC_TYPE(cmd) == HANTRODEC_IOC_MAGIC &&
	     _IOC_NR(cmd) > HANTRODEC_IOC_MAXNR) ||
	     (_IOC_TYPE(cmd) == HANTRO_IOC_MMU &&
	     _IOC_NR(cmd) > HANTRO_IOC_MMU_MAXNR) ||
	     (_IOC_TYPE(cmd) == HANTRO_VCMD_IOC_MAGIC &&
	     _IOC_NR(cmd) > HANTRO_VCMD_IOC_MAXNR))

		return -ENOTTY;

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
#if (KERNEL_VERSION(5, 0, 0) > LINUX_VERSION_CODE)
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg,
						 _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg,
						 _IOC_SIZE(cmd));
#else
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok((void *)arg, _IOC_SIZE(cmd));
#endif

	if (err)
		return -EFAULT;

#ifdef CLK_CFG
	spin_lock_irqsave(&clk_lock, flags);
	if (clk_cfg && !IS_ERR(clk_cfg) && (is_clk_on == 0)) {
		LOG_INFO("turn on clock by user\n");
		if (clk_enable(clk_cfg)) {
			spin_unlock_irqrestore(&clk_lock, flags);
			return -EFAULT;
		}
		is_clk_on = 1;

	spin_unlock_irqrestore(&clk_lock, flags);
	/*the interval is 10s*/
	mod_timer(&timer, jiffies + 10 * HZ);
#endif

	switch (cmd) {
	case HANTRODEC_IOC_CLI: {
		__u32 id;

		__get_user(id, (__u32 __user *)arg);

		if (id >= hantrodec_data.cores)
			return -EFAULT;
		disable_irq(hantrodec_data.irq[id]);
		break;
	}
	case HANTRODEC_IOC_STI: {
		__u32 id;

		__get_user(id, (__u32 __user *)arg);

		if (id >= hantrodec_data.cores)
			return -EFAULT;
		enable_irq(hantrodec_data.irq[id]);
		break;
	}
	case HANTRODEC_IOCGHWOFFSET: {
		__u32 id;

		__get_user(id, (__u32 __user *)arg);

		if (id >= hantrodec_data.cores)
			return -EFAULT;

		__put_user(multicorebase_actual[id],
			   (unsigned long __user *)arg);
		break;
	}
	case HANTRODEC_IOCGHWIOSIZE: {
		struct regsize_desc core;

		/* get registers from user space*/
		tmp = copy_from_user(&core, (void __user *)arg,
				     sizeof(struct regsize_desc));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}
		/*hantrodec_data.cores*/
		if (core.id >= MAX_SUBSYS_NUM)
			return -EFAULT;

		if (core.type == HW_SHAPER) {
			u32 asic_id;
			/* Shaper is configured with l2cache. */
			if (vpu_subsys[core.id].submodule_hwregs[HW_L2CACHE]) {
				asic_id = ioread32(
					(void __iomem *)vpu_subsys[core.id]
					.submodule_hwregs[HW_L2CACHE]);
				switch ((asic_id >> 16) & 0x3) {
				case 1: /* cache only */
					core.size = 0;
					break;
				case 0: /* cache + shaper */
				case 2: /* shaper only*/
					core.size =
						vpu_subsys[core.id]
						.submodule_iosize
						[HW_L2CACHE];
					break;
				default:
					return -EFAULT;
					}
			} else {
				core.size = 0;
			}
		} else {
			core.size =
			  vpu_subsys[core.id].submodule_iosize[core.type];
			if (core.type == HW_VC8000D && !core.size &&
			    vpu_subsys[core.id].submodule_hwregs[HW_VC8000DJ])
				/* If VC8000D doesn't exists, while VC8000DJ
				 * exists, return VC8000DJ.
				 */
				core.size = vpu_subsys[core.id].submodule_iosize[HW_VC8000DJ];
		}
		tmp = copy_to_user((u32 __user *)arg, &core,
			     sizeof(struct regsize_desc));
		if (tmp) {
			LOG_DBG("%s %d: copy_from_user failed, returned %li\n", __func__, __LINE__, tmp);
			return -EFAULT;
		}

		return 0;
	}
	case HANTRODEC_IOC_MC_OFFSETS: {
		tmp = copy_to_user((unsigned long __user *)arg,
				   multicorebase_actual,
		 sizeof(multicorebase_actual));
		if (err) {
			LOG_DBG("copy_to_user failed, returned %li\n", tmp);
			return -EFAULT;
		}
		break;
	}
	case HANTRODEC_IOC_MC_CORES:
		__put_user(hantrodec_data.cores, (unsigned int __user *)arg);
		LOG_DBG("hantrodec_data.cores=%d\n", hantrodec_data.cores);
		break;
	case HANTRODEC_IOCS_DEC_PUSH_REG: {
		struct core_desc core;

		/* get registers from user space*/
		tmp = copy_from_user(&core, (void __user *)arg,
				     sizeof(struct core_desc));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}

		return DecFlushRegs(&hantrodec_data, &core);
	}
	case HANTRODEC_IOCS_DEC_WRITE_REG: {
		struct core_desc core;
		int ret = 0;

		/* get registers from user space*/
		tmp = copy_from_user(&core, (void __user *)arg,
				     sizeof(struct core_desc));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}

		ret = DecWriteRegs(&hantrodec_data, &core);
		return ret;
	}
	case HANTRODEC_IOCS_DEC_WRITE_APBFILTER_REG: {
		struct core_desc core;

		/* get registers from user space*/
		tmp = copy_from_user(&core, (void __user *)arg,
				     sizeof(struct core_desc));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}
		return DecWriteApbFilterRegs(&hantrodec_data, &core);
	}
	case HANTRODEC_IOCS_PP_PUSH_REG: {
#if 0
	struct core_desc core;

	/* get registers from user space*/
	tmp = copy_from_user(&core, (void *)arg,
			     sizeof(struct core_desc));
	if (tmp) {
		LOG_DBG("copy_from_user failed, returned %li\n", tmp);
		return -EFAULT;
	}

			PPFlushRegs(&hantrodec_data, &core);
#else
		return -EINVAL;
#endif
	}
	case HANTRODEC_IOCS_DEC_PULL_REG: {
		struct core_desc core;

		/* get registers from user space*/
		tmp = copy_from_user(&core, (void __user *)arg,
				     sizeof(struct core_desc));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}

		return DecRefreshRegs(&hantrodec_data, &core);
	}
	case HANTRODEC_IOCS_DEC_READ_REG: {
		struct core_desc core;

		/* get registers from user space*/
		tmp = copy_from_user(&core, (void __user *)arg,
				     sizeof(struct core_desc));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}

		return DecReadRegs(&hantrodec_data, &core);
	}
	case HANTRODEC_IOCS_PP_PULL_REG: {
#if 0
	struct core_desc core;

	/* get registers from user space*/
	tmp = copy_from_user(&core, (void *)arg,
			     sizeof(struct core_desc));
	if (tmp) {
		LOG_DBG("copy_from_user failed, returned %li\n", tmp);
		return -EFAULT;
	}

	return PPRefreshRegs(&hantrodec_data, &core);
#else
		return -EINVAL;
#endif
	}
	case HANTRODEC_IOCH_DEC_RESERVE: {
		u32 format = 0;

		__get_user(format, (unsigned long __user *)arg);
		LOG_DBG("Reserve DEC core, format = %d\n", format);
		return ReserveDecoder(&hantrodec_data, filp, format);
	}
	case HANTRODEC_IOCT_DEC_RELEASE: {
		u32 core = 0;

		__get_user(core, (unsigned long __user *)arg);
		if (core >= hantrodec_data.cores || dec_owner[core] != filp) {
			LOG_DBG("bogus DEC release, core = %d\n", core);
			return -EFAULT;
		}

		LOG_DBG("Release DEC, core = %d\n", core);

		ReleaseDecoder(&hantrodec_data, core);

		break;
	}
	case HANTRODEC_IOCQ_PP_RESERVE:
#if 0
	return ReservePostProcessor(&hantrodec_data, filp);
#else
		return -EINVAL;
#endif
	case HANTRODEC_IOCT_PP_RELEASE: {
#if 0
	if (arg != 0 || pp_owner[arg] != filp) {
		LOG_DBG("bogus PP release %li\n", arg);
		return -EFAULT;
	}
	ReleasePostProcessor(&hantrodec_data, arg);
	break;
#else
		return -EINVAL;
#endif
	}
	case HANTRODEC_IOCX_DEC_WAIT: {
		struct core_desc core;

		/* get registers from user space */
		tmp = copy_from_user(&core, (void __user *)arg,
				     sizeof(struct core_desc));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}

		return WaitDecReadyAndRefreshRegs(&hantrodec_data, &core);
	}
	case HANTRODEC_IOCX_PP_WAIT: {
#if 0
	struct core_desc core;

			/* get registers from user space */
			tmp = copy_from_user(&core, (void *)arg,
					     sizeof(struct core_desc));
	if (tmp) {
		LOG_DBG("copy_from_user failed, returned %li\n", tmp);
		return -EFAULT;
	}

	return WaitPPReadyAndRefreshRegs(&hantrodec_data, &core);
#else
		return -EINVAL;
#endif
	}
	case HANTRODEC_IOCG_CORE_WAIT: {
		int id;

		tmp = WaitCoreReady(&hantrodec_data, filp, &id);
		__put_user(id, (int __user *)arg);
		return tmp;
	}
	case HANTRODEC_IOX_ASIC_ID: {
		struct core_param core;

		/* get registers from user space*/
		tmp = copy_from_user(&core, (void __user *)arg,
				     sizeof(struct core_param));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}
		/*hantrodec_data.cores*/
		if (core.id >= MAX_SUBSYS_NUM  ||
		    ((core.type == HW_VC8000D ||
	core.type == HW_VC8000DJ) &&
	!vpu_subsys[core.id]
	.submodule_iosize[core.type == HW_VC8000D] &&
	!vpu_subsys[core.id]
	.submodule_iosize[core.type == HW_VC8000DJ]) ||
	((core.type != HW_VC8000D && core.type != HW_VC8000DJ) &&
	!vpu_subsys[core.id].submodule_iosize[core.type]))
			return -EFAULT;

		core.size = vpu_subsys[core.id].submodule_iosize[core.type];
		if (vpu_subsys[core.id].submodule_hwregs[core.type]) {
			core.asic_id = ioread32((void __iomem *)
				hantrodec_data.hwregs[core.id][core.type]);
		} else if (core.type == HW_VC8000D &&
			 hantrodec_data.hwregs[core.id][HW_VC8000DJ]) {
			core.asic_id =
				ioread32((void __iomem *)
				hantrodec_data.hwregs[core.id][HW_VC8000DJ]);
		} else {
			core.asic_id = 0;
		}
		tmp = copy_to_user((u32 __user *)arg, &core,
			     sizeof(struct core_param));
		if (tmp) {
			LOG_DBG("%s %d: copy_from_user failed, returned %li\n", __func__, __LINE__, tmp);
			return -EFAULT;
		}

		return 0;
	}
	case HANTRODEC_IOCG_CORE_ID: {
		u32 format = 0;

		__get_user(format, (unsigned long __user *)arg);

		LOG_DBG("Get DEC Core_id, format = %d\n", format);
		return GetDecCoreID(&hantrodec_data, filp, format);
	}
	case HANTRODEC_IOX_ASIC_BUILD_ID: {
		u32 id, hw_id;

		__get_user(id, (u32 __user *)arg);

		if (id >= hantrodec_data.cores)
			return -EFAULT;
		if (hantrodec_data.hwregs[id][HW_VC8000D] ||
		    hantrodec_data.hwregs[id][HW_VC8000DJ]) {
			volatile u8 *hwregs;
			/* VC8000D first if it exists, otherwise VC8000DJ. */
			if (hantrodec_data.hwregs[id][HW_VC8000D])
				hwregs = hantrodec_data.hwregs[id][HW_VC8000D];
			else
				hwregs = hantrodec_data.hwregs[id][HW_VC8000DJ];
			hw_id = ioread32((void __iomem *)hwregs);
			if (IS_G1(hw_id >> 16) || IS_G2(hw_id >> 16) ||
			    (IS_VC8000D(hw_id >> 16) &&
			    ((hw_id & 0xFFFF) == 0x6010))) {
				__put_user(hw_id, (u32 __user *)arg);
			} else {
				hw_id = ioread32((void __iomem *)(hwregs +
					    HANTRODEC_HW_BUILD_ID_OFF));
				__put_user(hw_id, (u32 __user *)arg);
			}
		} else if (hantrodec_data.hwregs[id][HW_BIGOCEAN]) {
			hw_id = ioread32((void __iomem *)
				    (hantrodec_data.hwregs[id][HW_BIGOCEAN]));
			if (IS_BIGOCEAN(hw_id >> 16))
				__put_user(hw_id, (u32 __user *)arg);
			else
				return -EFAULT;
		}
		return 0;
	}
	case HANTRODEC_DEBUG_STATUS: {
		LOG_INFO("dec_irq = 0x%08x\n", dec_irq);
		LOG_INFO("pp_irq = 0x%08x\n", pp_irq);

		LOG_INFO("IRQs received/sent2user = %d / %d\n",
			atomic_read(&irq_rx), atomic_read(&irq_tx));

		for (tmp = 0; tmp < hantrodec_data.cores; tmp++) {
			LOG_INFO("dec_core[%li] %s\n", tmp,
				!dec_owner[tmp] ? "FREE" : "RESERVED");
			LOG_INFO("pp_core[%li]  %s\n", tmp,
				!pp_owner[tmp] ? "FREE" : "RESERVED");
		}
		return 0;
	}
	case HANTRODEC_IOX_SUBSYS: {
		struct subsys_desc subsys = { 0 };
		/* TODO(min): check all the subsys */
		if (vcmd) {
			subsys.subsys_vcmd_num = 1;
			subsys.subsys_num = subsys.subsys_vcmd_num;
		} else {
			subsys.subsys_num = hantrodec_data.cores;
			subsys.subsys_vcmd_num = 0;
		}
		tmp = copy_to_user((u32 __user *)arg, &subsys,
			     sizeof(struct subsys_desc));
		if (tmp) {
			LOG_DBG("%s %d: copy_from_user failed, returned %li\n", __func__, __LINE__, tmp);
			return -EFAULT;
		}

		return 0;
	}
	case HANTRODEC_IOCX_POLL: {
		hantrodec_isr(0, &hantrodec_data);
		return 0;
	}
	case HANTRODEC_IOC_APBFILTER_CONFIG: {
		struct apbfilter_cfg tmp_apbfilter;

		/* get registers from user space*/
		tmp = copy_from_user(&tmp_apbfilter, (void __user *)arg,
				     sizeof(struct apbfilter_cfg));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}

		if (tmp_apbfilter.id >= MAX_SUBSYS_NUM ||
		    tmp_apbfilter.type >= HW_CORE_MAX)
			return -EFAULT;

		apbfilter_cfg[tmp_apbfilter.id][tmp_apbfilter.type].id =
			tmp_apbfilter.id;
		apbfilter_cfg[tmp_apbfilter.id][tmp_apbfilter.type].type =
			tmp_apbfilter.type;

		memcpy(&tmp_apbfilter,
		       &apbfilter_cfg[tmp_apbfilter.id][tmp_apbfilter.type],
		 sizeof(struct apbfilter_cfg));

		tmp = copy_to_user((u32 __user *)arg, &tmp_apbfilter,
			     sizeof(struct apbfilter_cfg));
		if (tmp) {
			LOG_DBG("%s %d: copy_from_user failed, returned %li\n", __func__, __LINE__, tmp);
			return -EFAULT;
		}

		return 0;
	}
	case HANTRODEC_IOC_AXIFE_CONFIG: {
		struct axife_cfg tmp_axife;

		/* get registers from user space*/
		tmp = copy_from_user(&tmp_axife, (void __user *)arg,
				     sizeof(struct axife_cfg));
		if (tmp) {
			LOG_DBG("copy_from_user failed, returned %li\n", tmp);
			return -EFAULT;
		}

		if (tmp_axife.id >= MAX_SUBSYS_NUM)
			return -EFAULT;

		axife_cfg[tmp_axife.id].id = tmp_axife.id;

		memcpy(&tmp_axife, &axife_cfg[tmp_axife.id],
		       sizeof(struct axife_cfg));

		tmp = copy_to_user((u32 __user *)arg, &tmp_axife,
			     sizeof(struct axife_cfg));
		if (tmp) {
			LOG_DBG("%s %d: copy_from_user failed, returned %li\n", __func__, __LINE__, tmp);
			return -EFAULT;
		}

		return 0;
	}
#ifdef SUPPORT_DMA_HEAP
	case HANTRODEC_IOC_DMA_HEAP_GET_IOVA: {
		struct dmabuf_cfg dbcfg;
		size_t buf_size = 0;
		void *cpu_vaddr = NULL;
		struct heap_mem *hmem, *hmem_d1;
		struct filp_priv *fp_priv = (struct filp_priv *)filp->private_data;

		if (copy_from_user(&dbcfg, (void __user *)arg, sizeof(struct dmabuf_cfg)) != 0)
			return -EFAULT;

		LOG_DBG("import dmabuf_fd = %d\n", dbcfg.dmabuf_fd);

		/* map the pha to dma addr(iova)*/
		/* syscoherent <-> false, flush one time when import; sys,cma <-> true, no fush */
		hmem = common_dmabuf_heap_import_from_user(&fp_priv->root, dbcfg.dmabuf_fd);
		if(IS_ERR(hmem)) {
			LOG_ERR("dmabuf-heap alloc from userspace failed\n");
			return -ENOMEM;
		}
		//LOG_INFO("import dmabuf_fd = %d, hmem=%px, filp=%px, platformdev_d1=%px\n", dbcfg.dmabuf_fd, hmem, filp,
		//	platformdev_d1);

		if (platformdev_d1) {
			hmem_d1 = common_dmabuf_heap_import_from_user(&fp_priv->root_d1, dbcfg.dmabuf_fd);
			if(IS_ERR(hmem_d1)) {
				common_dmabuf_heap_release(hmem);
				LOG_ERR("dmabuf-heap alloc from userspace failed for d1\n");
				return -ENOMEM;
			}
		}

		/* map the pha to cpu vaddr*/
		cpu_vaddr = common_dmabuf_heap_map_vaddr(hmem);
		if (cpu_vaddr == NULL) {
			LOG_ERR("map to cpu_vaddr failed\n");
			common_dmabuf_heap_release(hmem);
			if (platformdev_d1)
				common_dmabuf_heap_release(hmem_d1);

			return -ENOMEM;
		}

		/* get the size of the dmabuf allocated by dmabuf_heap */
		buf_size = common_dmabuf_heap_get_size(hmem);
		LOG_TRACE("dmabuf info: CPU VA:0x%lx, PA:0x%lx, DMA addr(iova):0x%lx, size=0x%lx\n",
				(unsigned long)hmem->vaddr, (unsigned long)sg_phys(hmem->sgt->sgl), (unsigned long)sg_dma_address(hmem->sgt->sgl), (unsigned long)buf_size);

		dbcfg.iova = (unsigned long)sg_dma_address(hmem->sgt->sgl);
		if (platformdev_d1) {
			unsigned long iova_d1;

			iova_d1 = (unsigned long)sg_dma_address(hmem_d1->sgt->sgl);
			if (dbcfg.iova != iova_d1) {
				common_dmabuf_heap_release(hmem);
				common_dmabuf_heap_release(hmem_d1);
				LOG_ERR("IOVA addrs of d0 and d1 are not the same\n");
				return -EFAULT;
			}
		}

		tmp = copy_to_user((u32 __user *)arg, &dbcfg, sizeof(struct dmabuf_cfg));
		if (tmp) {
			common_dmabuf_heap_release(hmem);
			if (platformdev_d1)
				common_dmabuf_heap_release(hmem_d1);
			LOG_ERR("%s %d: copy_from_user failed, returned %li\n", __func__, __LINE__, tmp);
			return -EFAULT;
		}

		return 0;
	}
	case HANTRODEC_IOC_DMA_HEAP_PUT_IOVA: {
		struct heap_mem *hmem, *hmem_d1;
		unsigned int dmabuf_fd;
		struct filp_priv *fp_priv = (struct filp_priv *)filp->private_data;

		if (copy_from_user(&dmabuf_fd, (void __user *)arg, sizeof(int)) != 0)
			return -EFAULT;

		LOG_DBG("release dmabuf_fd = %d\n", dmabuf_fd);

		/* find the heap_mem */
		hmem = common_dmabuf_lookup_heapobj_by_fd(&fp_priv->root, dmabuf_fd);
		if (IS_ERR(hmem)) {
			LOG_ERR("cannot find dmabuf-heap for dmabuf_fd %d, %px\n", dmabuf_fd, hmem);
			return -ENOMEM;
		}

		if (platformdev_d1) {
			hmem_d1 = common_dmabuf_lookup_heapobj_by_fd(&fp_priv->root_d1, dmabuf_fd);
			if (IS_ERR(hmem_d1)) {
				LOG_ERR("cannot find dmabuf-heap for dmabuf_fd %d on d1\n", dmabuf_fd);
				return -EFAULT;
			}
			common_dmabuf_heap_release(hmem_d1);
		}
		//LOG_INFO("release dmabuf_fd = %d, hmem=%px, filp=%px, platformdev_d1=%px\n", dmabuf_fd, hmem, filp,
		//	platformdev_d1);
		common_dmabuf_heap_release(hmem);

		return 0;
	}
	case HANTRODEC_IOC_DMA_HEAP_FD_SPLIT: {
		struct dmabuf_split db_split;
		unsigned int slice_fd;
		unsigned int dmabuf_fd;
		unsigned char name[16] = {0};

		if (copy_from_user(&db_split, (void __user *)arg, sizeof(db_split)) != 0)
			return -EFAULT;

		LOG_DBG("To be split dmabuf_fd = %d, offset %d, len %d\n", db_split.dmabuf_fd, db_split.offset, db_split.length);

		sprintf(name, "fd_%d", db_split.dmabuf_fd);
		slice_fd = esw_common_dmabuf_split_export(db_split.dmabuf_fd, db_split.offset, (size_t)db_split.length, O_RDWR, name);
		if (slice_fd < 0) {
			LOG_ERR("Split dmabuf_fd %d failed\n", dmabuf_fd);
			return -EFAULT;
		}

		db_split.slice_fd = slice_fd;
		tmp = copy_to_user((u32 __user *)arg, &db_split, sizeof(struct dmabuf_split));
		if (tmp) {
			LOG_DBG("%s %d: copy_from_user failed, returned %li\n", __func__, __LINE__, tmp);
			return -EFAULT;
		}

		return 0;
	}
#endif
	default: {
		if (_IOC_TYPE(cmd) == HANTRO_IOC_MMU) {
			volatile u8 *mmu_hwregs[MAX_SUBSYS_NUM][2];

			for (i = 0; i < MAX_SUBSYS_NUM; i++) {
				mmu_hwregs[i][0] =
				  hantrodec_data.hwregs[i][HW_MMU];
				mmu_hwregs[i][1] =
				  hantrodec_data.hwregs[i][HW_MMU_WR];
			}
			return (MMUIoctl(cmd, filp, arg, mmu_hwregs));
		} else if (_IOC_TYPE(cmd) == HANTRO_VCMD_IOC_MAGIC) {
			return hantrovcmd_ioctl(filp, cmd, arg);
		}
		return -ENOTTY;
	}
	}

	return 0;
}

static int hantrodec_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return hantrovcmd_mmap(filp, vma);
}

/*
 * Function name   : hantrodec_open
 * Description     : open method

 * Return type     : int
 */
static int hantrodec_open(struct inode *inode, struct file *filp)
{
	struct filp_priv *fp_priv;

	fp_priv = kzalloc(sizeof(struct filp_priv), GFP_KERNEL);
	if (!fp_priv) {
		pr_err("%s: alloc failed\n", __func__);
		return -ENOMEM;
	}

#ifdef SUPPORT_DMA_HEAP
	common_dmabuf_heap_import_init(&fp_priv->root, &platformdev->dev);
	if (platformdev_d1) {
		common_dmabuf_heap_import_init(&fp_priv->root_d1, &platformdev_d1->dev);
	}
#endif
	for (u32 core_id = 0; core_id < DEC_CORE_NUM; core_id ++) {
		atomic_set(&(fp_priv->core_tasks[core_id]), 0);
	}
	filp->private_data = (void *)fp_priv;
	LOG_DBG("dev opened\n");

	if (vcmd)
		hantrovcmd_open(inode, filp);

	return 0;
}

/*
 * Function name   : hantrodec_release
 * Description     : Release driver

 * Return type     : int
 */

static int hantrodec_release(struct inode *inode,
			     struct file *filp)
{
	int n;
	hantrodec_t *dev = &hantrodec_data;
	struct filp_priv *fp_priv = (struct filp_priv *)filp->private_data;

	LOG_DBG("closing ...\n");

	if (vcmd) {
		hantrovcmd_release(inode, filp);
		goto end;
	}

	for (n = 0; n < dev->cores; n++) {
		if (dec_owner[n] == filp) {
			LOG_DBG("releasing dec core %i lock\n", n);
			ReleaseDecoder(dev, n);
			ReleaseL2Cache(dev, n);
		}
	}

	for (n = 0; n < 1; n++) {
		if (pp_owner[n] == filp) {
			LOG_DBG("releasing pp core %i lock\n", n);
			ReleasePostProcessor(dev, n);
		}
	}

	MMURelease(filp, hantrodec_data.hwregs[0][HW_MMU]);

end:
#ifdef SUPPORT_DMA_HEAP
	common_dmabuf_heap_import_uninit(&fp_priv->root);
	if (platformdev_d1) {
		common_dmabuf_heap_import_uninit(&fp_priv->root_d1);
	}
#endif
	for (u32 core_id = 0; core_id < DEC_CORE_NUM; core_id ++) {
		/** clear the tasks for pm*/
		while (atomic_dec_return(&(fp_priv->core_tasks[core_id])) >= 0) {
			vdec_pm_runtime_put(core_id);
		}
	}
	kfree(fp_priv);

	LOG_DBG("closed\n");
	return 0;
}

#ifdef CLK_CFG
void hantrodec_disable_clk(unsigned long value)
{
	unsigned long flags;
	/* entering this function means decoder
	 * is idle over expiry.So disable clk
	 */
	if (clk_cfg && !IS_ERR(clk_cfg)) {
		spin_lock_irqsave(&clk_lock, flags);
		if (is_clk_on == 1) {
			clk_disable(clk_cfg);
			is_clk_on = 0;
			LOG_INFO("turned off clk\n");
		}
		spin_unlock_irqrestore(&clk_lock, flags);
	}
}
#endif

/* VFS methods */
static const struct file_operations hantrodec_fops = {
	.owner = THIS_MODULE,
	.open = hantrodec_open,
	.release = hantrodec_release,
	.unlocked_ioctl = hantrodec_ioctl,
	.mmap = hantrodec_mmap,
	.fasync = NULL,
};

static int PcieInit(void)
{
	int i;

	gDev = pci_get_device(PCI_VENDOR_ID_HANTRO,
			      PCI_DEVICE_ID_HANTRO_PCI,
			gDev);
	if (!gDev) {
		LOG_INFO("Init: Hardware not found.\n");
		goto out;
	}

	if (pci_enable_device(gDev) < 0) {
		LOG_INFO("%s: Device not enabled.\n", __func__);
		goto out;
	}

	gBaseHdwr = pci_resource_start(gDev, PCI_CONTROL_BAR);
	if (gBaseHdwr == 0) {
		LOG_INFO("%s: Base Address not set.\n", __func__);
		goto out_pci_disable_device;
	}
	LOG_INFO("Base hw val 0x%lX\n", gBaseHdwr);

	gBaseLen = pci_resource_len(gDev, PCI_CONTROL_BAR);
	LOG_INFO("Base hw len 0x%x\n", gBaseLen);

	for (i = 0; i < MAX_SUBSYS_NUM; i++) {
		if (vpu_subsys[i].base_addr) {
			vpu_subsys[i].base_addr += gBaseHdwr;
			multicorebase[i] += gBaseHdwr;
		}
	}

	gBaseDDRHw = pci_resource_start(gDev, PCI_DDR_BAR);
	if (gBaseDDRHw == 0) {
		LOG_INFO("%s: Base Address not set.\n", __func__);
		goto out_pci_disable_device;
	}
	LOG_INFO("Base memory val 0x%lx\n", gBaseDDRHw);

	gBaseLen = pci_resource_len(gDev, PCI_DDR_BAR);
	LOG_INFO("Base memory len 0x%x\n", gBaseLen);

	return 0;

out_pci_disable_device:
	pci_disable_device(gDev);

out:
	return -1;
}

/*
 *Function name   : hantrodec_init
 *Description     : Initialize the driver

 *Return type     : int
 */

static int hantrodec_init(void)
{
	int result = 0, i;
	enum MMUStatus status = 0;
	enum MMUStatus mmu_status = MMU_STATUS_FALSE;
	volatile u8 *mmu_hwregs[MAX_SUBSYS_NUM][2];

	CheckSubsysCoreArray(vpu_subsys, &vcmd);

	if (pcie) {
		result = PcieInit();
		if (result)
			goto err;
	} else {
#if 0
		if (vcmd && (alloc_base == -1 || alloc_size == -1)) {
			LOG_INFO("set alloc_base/alloc_size correctly for non-PCIe env.\n");
			goto err;
		}
#endif

#ifndef SUPPORT_DMA_HEAP
		platformdev =
			platform_device_register_full(&hantro_platform_info);
		if (!platformdev) {
			LOG_ERR("create platform device fail\n");
			status = MMU_STATUS_FALSE;
			return -ENODEV;
		}
		LOG_INFO("Create platform device success\n");
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0)
		of_dma_configure(&platformdev->dev, platformdev->dev.of_node, true);
#else
		of_dma_configure(&platformdev->dev, platformdev->dev.of_node);
#endif

		if (dma_set_mask_and_coherent(&platformdev->dev,
					      DMA_BIT_MASK(41))) {
			LOG_ERR("41bit dma dev: No suitable DMA available\n");
		}

		if (dma_set_coherent_mask(&platformdev->dev, DMA_BIT_MASK(41))) {
			LOG_ERR("41bit dma dev: No suitable DMA available\n");
		}

		if (platformdev_d1) {
			of_dma_configure(&platformdev_d1->dev, platformdev_d1->dev.of_node, true);
			if (dma_set_mask_and_coherent(&platformdev_d1->dev, DMA_BIT_MASK(48))) {
				LOG_ERR("48bit dma dev: No suitable DMA available\n");
			}

			if (dma_set_coherent_mask(&platformdev_d1->dev, DMA_BIT_MASK(48))) {
				LOG_ERR("48bit dma dev: No suitable DMA available\n");
			}
		}
	}

	LOG_INFO("dec/pp kernel module.\n");

	/* If base_port is set when insmod,
	 * use that for single core legacy mode.
	 */
	if (base_port != -1) {
		multicorebase[0] = base_port;
		if (pcie)
			multicorebase[0] += HANTRO_REG_OFFSET0;
		elements = 1;
		vpu_subsys[0].base_addr = base_port;
		LOG_INFO("Init single core at 0x%08lx IRQ=%i\n",
			multicorebase[0], irq[0]);
	} else {
		LOG_INFO("Init multi core[0] at 0x%16lx\n"
				" core[1] at 0x%16lx\n"
				" core[2] at 0x%16lx\n"
				" core[3] at 0x%16lx\n"
				" IRQ_0=%i\n"
				" IRQ_1=%i\n"
				" IRQ_2=%i\n"
				" IRQ_3=%i\n",
				multicorebase[0], multicorebase[1],
				multicorebase[2], multicorebase[3],
				irq[0], irq[1], irq[2], irq[3]);
	}

	hantrodec_data.cores = 0;

	hantrodec_data.iosize[0] = DEC_IO_SIZE_0;
	hantrodec_data.irq[0] = irq[0];
	hantrodec_data.iosize[1] = DEC_IO_SIZE_1;
	hantrodec_data.irq[1] = irq[1];

	for (i = 0; i < HXDEC_MAX_CORES; i++) {
		int j;

		for (j = 0; j < HW_CORE_MAX; j++)
			hantrodec_data.hwregs[i][j] = NULL;
		/* If user gave less core bases that we have
		 * by default,invalidate default bases
		 */
		if (elements && i >= elements)
			multicorebase[i] = 0;
	}

	hantrodec_data.async_queue_dec = NULL;
	hantrodec_data.async_queue_pp = NULL;

	result = register_chrdev(hantrodec_major,
				DEC_DEV_NAME,
	 			&hantrodec_fops);
	if (result < 0) {
		LOG_ERR("unable to get major %d\n", hantrodec_major);
		goto err;
	} else if (result != 0) {
		/* this is for dynamic major */
		hantrodec_major = result;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,6,0)
    vdec_class = class_create(VDEC_CLASS_NAME);
#else
    vdec_class = class_create(THIS_MODULE, VDEC_CLASS_NAME);
#endif
    if (IS_ERR(vdec_class)) {
        vdec_class = NULL;
        LOG_ERR("%s(%d): Failed to create the class(%s).\n", __func__, __LINE__, VDEC_CLASS_NAME);
        goto err;
    }else{
        device_create(vdec_class, NULL, MKDEV(hantrodec_major, 0), NULL, DEC_DEV_NAME);
        LOG_NOTICE("create device node-%s done\n", DEC_DEV_NAME);
    }

#ifdef CLK_CFG
	/* first get clk instance pointer */
	clk_cfg = clk_get(NULL, CLK_ID);
	if (!clk_cfg || IS_ERR(clk_cfg)) {
		LOG_ERR("get clk failed!\n");
		goto err;
	}

	/* prepare and enable clk */
	if (clk_prepare_enable(clk_cfg)) {
		LOG_ERR("try to enable clk failed!\n");
		goto err;
	}
	is_clk_on = 1;

	/* init a timer to disable clk */
	init_timer(&timer);
	timer.function = &hantrodec_disable_clk;
	/* the expires time is 100s */
	timer.expires = jiffies + 100 * HZ;
	add_timer(&timer);
#endif

	result = ReserveIO();
	if (result < 0)
		goto err;

	for (i = 0; i < hantrodec_data.cores; i++)
		AXIFEEnable(hantrodec_data.hwregs[i][HW_AXIFE]);

#if 0
		for (i = 0; i < hantrodec_data.cores; i++)
			AFBCBypass(hantrodec_data.hwregs[i][HW_AFBC]);

#endif

	/* MMU only initial once No matter how many MMU we have */
	if (hantrodec_data.hwregs[0][HW_MMU]) {
		status = MMUInit(hantrodec_data.hwregs[0][HW_MMU]);
		if (status == MMU_STATUS_NOT_FOUND) {
			LOG_INFO("MMU does not exist!\n");
		} else if (status != MMU_STATUS_OK)
			goto err;
		else {
			LOG_INFO("MMU detected!\n");
		}

		for (i = 0; i < MAX_SUBSYS_NUM; i++) {
			mmu_hwregs[i][0] = hantrodec_data.hwregs[i][HW_MMU];
			mmu_hwregs[i][1] = hantrodec_data.hwregs[i][HW_MMU_WR];
		}
		mmu_status = MMUEnable(mmu_hwregs);
	}

	if (vcmd) {
		/* unmap and release mem region for VCMD,
		 * since it will be mapped and
		 * reserved again in hantro_vcmd.c
		 */
		for (i = 0; i < hantrodec_data.cores; i++) {
			if (hantrodec_data.hwregs[i][HW_VCMD]) {
				iounmap((void __iomem *)
				  hantrodec_data.hwregs[i][HW_VCMD]);
				release_mem_region(
				  vpu_subsys[i].base_addr +
				  vpu_subsys[i].submodule_offset
				  [HW_VCMD],
				vpu_subsys[i].submodule_iosize[HW_VCMD]);
				hantrodec_data.hwregs[i][HW_VCMD] = NULL;
			}
		}
		result = hantrovcmd_init();
		if (result)
			goto err;
		return 0;
	}

	memset(dec_owner, 0, sizeof(dec_owner));
	memset(pp_owner, 0, sizeof(pp_owner));

	sema_init(&dec_core_sem, hantrodec_data.cores);
	sema_init(&pp_core_sem, 1);

	/* read configuration fo all cores */
	ReadCoreConfig(&hantrodec_data);

	/* reset hardware */
	ResetAsic(&hantrodec_data);

	/* register irq for each core */
	for (i = 0; i < hantrodec_data.cores; i++) {
		if (irq[i] > 0) {
			result = request_irq(irq[i], hantrodec_isr, IRQF_SHARED, DEC_DEV_NAME, (void *)&hantrodec_data);

			if (result != 0) {
				if (result == -EINVAL) {
					LOG_ERR("Bad irq number or handler\n");
				} else if (result == -EBUSY) {
					LOG_ERR("IRQ <%d> busy, change your config\n", hantrodec_data.irq[i]);
				}
				goto err;
			} else {
				LOG_INFO("Registered irq %d\n", irq[i]);
			}

		} else {
			LOG_INFO("IRQ not in use!\n");
		}
	}

	for (i = 0; i < hantrodec_data.cores; i++) {
		volatile u8 *hwregs = hantrodec_data.hwregs[i][HW_VC8000D];

		if (hwregs) {
			LOG_INFO("dec [%d] has build id 0x%08x\n",
				i,
				ioread32((void __iomem *)
				(hwregs + HANTRODEC_HWBUILD_ID_OFF)));
		}
	}

	/* Please call the TEE functions to
	 * set VC8000D DRM relative registers here
	 */

	return 0;

err:
	ReleaseIO();

#ifndef SUPPORT_DMA_HEAP
	if (platformdev)
		platform_device_unregister(platformdev);
#endif
	LOG_WARN("module not inserted\n");
	unregister_chrdev(hantrodec_major, DEC_DEV_NAME);
	return result;
}

/*
 * Function name   : hantrodec_cleanup
 * Description     : clean up
 * Return type     : int
 */
static void hantrodec_cleanup(void)
{
	hantrodec_t *dev = &hantrodec_data;
	int i, n = 0;
	volatile u8 *mmu_hwregs[MAX_SUBSYS_NUM][2];
	int has_mmu = 0;

	for (i = 0; i < MAX_SUBSYS_NUM; i++) {
		mmu_hwregs[i][0] = dev->hwregs[i][HW_MMU];
		mmu_hwregs[i][1] = dev->hwregs[i][HW_MMU_WR];
		if (dev->hwregs[i][HW_DEC400]) {
			/* disable dec400 when rmmod driver. */
			iowrite32(0x00810002,
				  (void __iomem *)(dev->hwregs[i][HW_DEC400] +
				 0x800));
		}
		if (dev->hwregs[i][HW_MMU])
			has_mmu = 1;
	}
	if (has_mmu)
		MMUCleanup(mmu_hwregs);

	if (vcmd) {
		hantrovcmd_cleanup();
	} else {
		/* reset hardware */
		ResetAsic(dev);

		/* free the IRQ */
		for (n = 0; n < dev->cores; n++) {
			if (dev->irq[n] != -1)
				free_irq(dev->irq[n], (void *)dev);
		}
	}
	ReleaseIO();

#ifdef CLK_CFG
	if (clk_cfg && !IS_ERR(clk_cfg)) {
		clk_disable_unprepare(clk_cfg);
		is_clk_on = 0;
		LOG_INFO("turned off clk\n");
	}

	/*delete timer*/
	del_timer(&timer);
#endif

#ifndef SUPPORT_DMA_HEAP
	if (!pcie) {
		platform_device_unregister(platformdev);
		//platform_driver_unregister(&hantro_drm_platform_driver);
		LOG_INFO("Unregister platform device.\n");
	}
#endif

	unregister_chrdev(hantrodec_major, DEC_DEV_NAME);

    if (vdec_class) {
        LOG_NOTICE("destroy device node - %s\n", DEC_DEV_NAME);
        device_destroy(vdec_class, MKDEV(hantrodec_major, 0));
        class_destroy(vdec_class);
        vdec_class = NULL;
    }

	LOG_INFO("module removed\n");
	return;
}

/*
 *Function name   : CheckHwId
 *Return type     : int
 */
static int CheckHwId(hantrodec_t *dev)
{
	int hwid;
	int i, j;
	size_t num_hw = sizeof(DecHwId) / sizeof(*DecHwId);

	int found = 0;

	for (i = 0; i < dev->cores; i++) {
		for (j = 0; j < HW_CORE_MAX; j++) {
			if ((j == HW_VC8000D || j == HW_BIGOCEAN ||
			     j == HW_VC8000DJ) &&
			     dev->hwregs[i][j]) {
				hwid = readl((volatile void __iomem *)
					dev->hwregs[i][j]);
				LOG_DBG("core %d:%d HW ID=0x%08x [ %s ]\n",
					i, j, hwid, CoreTypeStr(j));
				/* product version only */
				hwid = (hwid >> 16) & 0xFFFF;
				while (num_hw--) {
					if (hwid == DecHwId[num_hw]) {
						LOG_DBG("Supported HW found at 0x%16lx\n",
							vpu_subsys[i].base_addr +
							vpu_subsys[i].submodule_offset[j]);
						found++;
						dev->hw_id[i][j] = hwid;
						break;
					}
				}

				if (!found) {
					LOG_INFO("Unknown HW found at 0x%16lx\n",
						multicorebase_actual[i]);
					return 0;
				}

				found = 0;
				num_hw = sizeof(DecHwId) / sizeof(*DecHwId);
			}
		}
	}

	return 1;
}

/*
 * Function name   : ReserveIO
 * Description     : IO reserve
 * Return type     : int
 */
static int ReserveIO(void)
{
	int i, j;
	long hwid;
	u32 axife_config;

	memcpy(multicorebase_actual, multicorebase,
	       HXDEC_MAX_CORES * sizeof(unsigned long));
	memcpy((unsigned int *)(hantrodec_data.iosize), iosize,
	       HXDEC_MAX_CORES * sizeof(unsigned int));
	memcpy((unsigned int *)(hantrodec_data.irq), irq,
	       HXDEC_MAX_CORES * sizeof(int));

	for (i = 0; i < MAX_SUBSYS_NUM; i++) {
		if (!vpu_subsys[i].base_addr)
			continue;
		for (j = 0; j < HW_CORE_MAX; j++) {
			if (vpu_subsys[i].submodule_iosize[j]) {
				LOG_DBG("base=0x%16lx, iosize=%d\n",
						vpu_subsys[i].base_addr +
						vpu_subsys[i].submodule_offset[j],
						vpu_subsys[i].submodule_iosize[j]);
				if (!request_mem_region(vpu_subsys[i].base_addr +
							vpu_subsys[i].submodule_offset[j],
							vpu_subsys[i].submodule_iosize[j],
							"es_vdec0")) {
					LOG_INFO("failed to reserve HW %d regs\n",
							j);
					return -EBUSY;
				}
#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
	vpu_subsys[i].submodule_hwregs[j] =
		hantrodec_data.hwregs[i][j] =
		(volatile u8 __force *)
		ioremap_nocache(vpu_subsys[i].base_addr +
		vpu_subsys[i].submodule_offset[j],
		vpu_subsys[i].submodule_iosize[j]);
#else
	vpu_subsys[i].submodule_hwregs[j] =
		hantrodec_data.hwregs[i][j] =
			(volatile u8 *)ioremap(vpu_subsys[i].base_addr +
				vpu_subsys[i].submodule_offset[j],
				vpu_subsys[i].submodule_iosize[j]);
#endif

	if (!hantrodec_data.hwregs[i][j]) {
		LOG_INFO("failed to ioremap HW %d regs\n",
			j);
		release_mem_region(
			vpu_subsys[i].base_addr +
			vpu_subsys[i].submodule_offset[j],
			vpu_subsys[i].submodule_iosize[j]);
		return -EBUSY;
	}

	if (vpu_subsys[i].has_apbfilter[j]) {
		apbfilter_cfg[i][j].has_apbfilter = 1;
		hwid = ioread32((void __iomem *)
			   (hantrodec_data.hwregs[i][HW_VC8000D]));
		if (IS_BIGOCEAN(hwid & 0xFFFF)) {
			if (j == HW_BIGOCEAN) {
				apbfilter_cfg[i][j].nbr_mask_regs =
				  AV1_NUM_MASK_REG;
				apbfilter_cfg[i][j].num_mode =
				  AV1_NUM_MODE;
				apbfilter_cfg[i][j].mask_reg_offset =
				  AV1_MASK_REG_OFFSET;
				apbfilter_cfg[i][j].mask_bits_per_reg =
				  AV1_MASK_BITS_PER_REG;
				apbfilter_cfg[i][j].page_sel_addr =
				  apbfilter_cfg[i][j].mask_reg_offset +
				  apbfilter_cfg[i][j].nbr_mask_regs * 4;
			}
			if (j == HW_AXIFE) {
				apbfilter_cfg[i][j].nbr_mask_regs =
				  AXIFE_NUM_MASK_REG;
				apbfilter_cfg[i][j].num_mode =
				  AXIFE_NUM_MODE;
				apbfilter_cfg[i][j].mask_reg_offset =
				  AXIFE_MASK_REG_OFFSET;
				apbfilter_cfg[i][j].mask_bits_per_reg =
				  AXIFE_MASK_BITS_PER_REG;
				apbfilter_cfg[i][j].page_sel_addr =
				  apbfilter_cfg[i][j].mask_reg_offset +
				  apbfilter_cfg[i][j].nbr_mask_regs * 4;
			}
		} else {
			hwid = ioread32((void __iomem *)
				   (hantrodec_data.hwregs[i][HW_VC8000D] +
				   HANTRODEC_HW_BUILD_ID_OFF));
			if (hwid == 0x1F58) {
				if (j == HW_VC8000D) {
					apbfilter_cfg[i][j].nbr_mask_regs =
					  VC8000D_NUM_MASK_REG;
					apbfilter_cfg[i][j].num_mode =
					  VC8000D_NUM_MODE;
					apbfilter_cfg[i][j].mask_reg_offset =
					  VC8000D_MASK_REG_OFFSET;
					apbfilter_cfg[i][j].mask_bits_per_reg =
					  VC8000D_MASK_BITS_PER_REG;
					apbfilter_cfg[i][j].page_sel_addr =
					  apbfilter_cfg[i][j].mask_reg_offset +
					  apbfilter_cfg[i][j].nbr_mask_regs * 4;
				}
				if (j == HW_AXIFE) {
					apbfilter_cfg[i][j].nbr_mask_regs =
					  AXIFE_NUM_MASK_REG;
					apbfilter_cfg[i][j].num_mode =
					  AXIFE_NUM_MODE;
					apbfilter_cfg[i][j].mask_reg_offset =
					  AXIFE_MASK_REG_OFFSET;
					apbfilter_cfg[i][j].mask_bits_per_reg =
					  AXIFE_MASK_BITS_PER_REG;
					apbfilter_cfg[i][j].page_sel_addr =
					  apbfilter_cfg[i][j].mask_reg_offset +
					  apbfilter_cfg[i][j].nbr_mask_regs * 4;
				}
			} else if (hwid == 0x1F59) {
				if (j == HW_VC8000DJ) {
					apbfilter_cfg[i][j].nbr_mask_regs =
					  VC8000DJ_NUM_MASK_REG;
					apbfilter_cfg[i][j].num_mode =
					  VC8000DJ_NUM_MODE;
					apbfilter_cfg[i][j].mask_reg_offset =
					  VC8000DJ_MASK_REG_OFFSET;
					apbfilter_cfg[i][j].mask_bits_per_reg =
					  VC8000DJ_MASK_BITS_PER_REG;
					apbfilter_cfg[i][j].page_sel_addr =
					  apbfilter_cfg[i][j].mask_reg_offset +
					  apbfilter_cfg[i][j].nbr_mask_regs * 4;
				}
				if (j == HW_AXIFE) {
					apbfilter_cfg[i][j].nbr_mask_regs =
					  AXIFE_NUM_MASK_REG;
					apbfilter_cfg[i][j].num_mode =
					  AXIFE_NUM_MODE;
					apbfilter_cfg[i][j].mask_reg_offset =
					  AXIFE_MASK_REG_OFFSET;
					apbfilter_cfg[i][j].mask_bits_per_reg =
					  AXIFE_MASK_BITS_PER_REG;
					apbfilter_cfg[i][j].page_sel_addr =
					  apbfilter_cfg[i][j].mask_reg_offset +
					  apbfilter_cfg[i][j].nbr_mask_regs * 4;
				}
			} else {
				LOG_INFO("furture APBFILTER canread those configure parameters from REG\n");
			}
		}
		hantrodec_data.apbfilter_hwregs[i][j] =
			hantrodec_data.hwregs[i][j] +
			apbfilter_cfg[i][j].mask_reg_offset;
	} else {
		apbfilter_cfg[i][j].has_apbfilter = 0;
	}
	if (j == HW_AXIFE) {
		int core_type = HW_VC8000D;
		if (vpu_subsys[i].subsys_type == 4)
			core_type = HW_VC8000DJ;

		hwid = ioread32((void __iomem *)
			(hantrodec_data.hwregs[i][core_type] +
			HANTRODEC_HW_BUILD_ID_OFF));
		axife_config = ioread32((void __iomem *)
			(hantrodec_data.hwregs[i][j]));
		axife_cfg[i].axi_rd_chn_num = axife_config & 0x7F;
		axife_cfg[i].axi_wr_chn_num = (axife_config >> 7) & 0x7F;
		axife_cfg[i].axi_rd_burst_length = (axife_config >> 14) & 0x1F;
		axife_cfg[i].axi_wr_burst_length = (axife_config >> 22) & 0x1F;
		axife_cfg[i].fe_mode = 0; /*need to read from reg in furture*/
		if (hwid == 0x1F66)
			axife_cfg[i].fe_mode = 1;
	}
	config.its_main_core_id[i] = -1;
	config.its_aux_core_id[i] = -1;

	LOG_DBG("HW %d reg[0]=0x%08x [ %s ]\n",
		j, readl((volatile void __iomem *)
			hantrodec_data.hwregs[i][j]),
			CoreTypeStr(j));

#ifdef SUPPORT_2ND_PIPELINES
	if (j != HW_VC8000D)
		continue;
	/* product version only */
	hwid = ((readl((volatile void __iomem *)
		 hantrodec_data.hwregs[i][HW_VC8000D])) >> 16) &
		 0xFFFF;

	if (IS_VC8000D(hwid)) {
		u32 reg;
		/* TODO(min): DO NOT
		 * support2nd pipeline.
		 */
		reg = readl(hantrodec_data.hwregs[i][HW_VC8000D] +
					HANTRODEC_SYNTH_CFG_2_OFF);
		if (((reg >> DWL_H264_PIPELINE_E) &
			0x01U) ||
			((reg >> DWL_JPEG_PIPELINE_E) &
			0x01U)) {
			i++;
			config.its_aux_core_id[i - 1] = i;

			config.its_main_core_id[i] = i - 1;

			config.its_aux_core_id[i] = -1;
			multicorebase_actual[i] =
				multicorebase_actual[i - 1] +
				0x800;
			hantrodec_data.iosize[i] =
				hantrodec_data.iosize[i - 1];
			memcpy(multicorebase_actual +
					i + 1,
					multicorebase + i,
					(HXDEC_MAX_CORES -
					i - 1) *
					sizeof(unsigned long));
			memcpy((unsigned int *)hantrodec_data.iosize +
					i + 1,
					iosize + i,
					(HXDEC_MAX_CORES -
					i - 1) * sizeof(unsigned int));
			if (!request_mem_region(multicorebase_actual[i],
						hantrodec_data.iosize[i],
						"hantrodec0")) {
				LOG_INFO("failed to reserve HW regs\n");
				return -EBUSY;
			}
#if (KERNEL_VERSION(4, 17, 0) > LINUX_VERSION_CODE)
			hantrodec_data.hwregs[i][HW_VC8000D] =
			  (volatile u8 *)
			  ioremap_nocache(multicorebase_actual[i],
					  hantrodec_data.iosize[i]);
#else
			hantrodec_data.hwregs[i][HW_VC8000D] =
			  (volatile u8 *)ioremap(multicorebase_actual[i],
			  hantrodec_data.iosize[i]);
#endif

			if (!hantrodec_data.hwregs[i][HW_VC8000D]) {
				LOG_INFO("failed to ioremap HW regs\n");
				ReleaseIO();
				return -EBUSY;
			}
			hantrodec_data.cores++;
		}
	}
#endif
			} else {
				hantrodec_data.hwregs[i][j] = NULL;
			}
		}
		hantrodec_data.cores++;
	}

	/* check for correct HW */
	if (!CheckHwId(&hantrodec_data)) {
		ReleaseIO();
		return -EBUSY;
	}

	return 0;
}

/*
 * Function name   : releaseIO
 * Description     : release
 * Return type     : void
 */

static void ReleaseIO(void)
{
	int i, j;

	for (i = 0; i < hantrodec_data.cores; i++) {
		for (j = 0; j < HW_CORE_MAX; j++) {
			if (hantrodec_data.hwregs[i][j]) {
				iounmap((void __iomem *)
				hantrodec_data.hwregs[i][j]);
				release_mem_region(vpu_subsys[i].base_addr +
				  vpu_subsys[i].submodule_offset[j],
				  vpu_subsys[i].submodule_iosize[j]);
				hantrodec_data.hwregs[i][j] = NULL;
			}
		}
	}
}

/*
 * Function name   : hantrodec_isr
 * Description     : interrupt handler

 * Return type     : irqreturn_t
 */
#if (KERNEL_VERSION(2, 6, 18) > LINUX_VERSION_CODE)
static irqreturn_t hantrodec_isr(int irq, void *dev_id, struct pt_regs *regs)
#else
static irqreturn_t hantrodec_isr(int irq, void *dev_id)
#endif
{
	unsigned long flags;
	unsigned int handled = 0;
	int i;
	volatile u8 *hwregs;

	hantrodec_t *dev = (hantrodec_t *)dev_id;
	u32 irq_status_dec;

	spin_lock_irqsave(&owner_lock, flags);

	for (i = 0; i < dev->cores; i++) {
		volatile u8 *hwregs = dev->hwregs[i][HW_VC8000D];

		/* interrupt status register read */
		irq_status_dec = ioread32((void __iomem *)
			(hwregs + HANTRODEC_IRQ_STAT_DEC_OFF));

		if (irq_status_dec & HANTRODEC_DEC_IRQ) {
			/* clear dec IRQ */
			irq_status_dec &= (~HANTRODEC_DEC_IRQ);
			iowrite32(irq_status_dec,
				  (void __iomem *)(hwregs +
				 HANTRODEC_IRQ_STAT_DEC_OFF));

			LOG_DBG("decoder IRQ received! core %d\n", i);

			atomic_inc(&irq_rx);

			dec_irq |= (1 << i);

			wake_up_interruptible_all(&dec_wait_queue);
			handled++;
		}
	}

	spin_unlock_irqrestore(&owner_lock, flags);

	if (!handled)
	{
		LOG_DBG("IRQ received, but not %s's!\n", DEC_DEV_NAME);
	}

	(void)hwregs;
	return IRQ_RETVAL(handled);
}

/*
 * Function name   : ResetAsic
 * Description     : reset asic (only VC8000D supports reset)

 * Return type     :
 */
static void ResetAsic(hantrodec_t *dev)
{
	int i, j;
	u32 status;

	for (j = 0; j < dev->cores; j++) {
		if (!dev->hwregs[j][HW_VC8000D])
			continue;

		status = ioread32((void __iomem *)(dev->hwregs[j][HW_VC8000D] +
						  HANTRODEC_IRQ_STAT_DEC_OFF));

		if (status & HANTRODEC_DEC_E) {
			/* abort with IRQ disabled */
			status = HANTRODEC_DEC_ABORT |
				     HANTRODEC_DEC_IRQ_DISABLE;
			iowrite32(status, (void __iomem *)
					  (dev->hwregs[j][HW_VC8000D] +
					  HANTRODEC_IRQ_STAT_DEC_OFF));
		}

		if (IS_G1(dev->hw_id[j][HW_VC8000D]))
			/* reset PP */
			iowrite32(0, (void __iomem *)
					  (dev->hwregs[j][HW_VC8000D] +
					  HANTRO_IRQ_STAT_PP_OFF));

		for (i = 4; i < dev->iosize[j]; i += 4)
			iowrite32(0, (void __iomem *)
					  (dev->hwregs[j][HW_VC8000D] +
					  i));
	}
}

/*
 * Function name   : dump_regs
 * Description     : Dump registers

 * Return type     :
 */
#ifdef HANTRODEC_DEBUG
void dump_regs(hantrodec_t *dev)
{
	int i, c;

	LOG_DBG("Reg Dump Start\n");
	for (c = 0; c < dev->cores; c++) {
		for (i = 0; i < dev->iosize[c]; i += 4 * 4) {
			LOG_DBG("\toffset %04X: %08X  %08X  %08X  %08X\n", i,
			       ioread32(dev->hwregs[c][HW_VC8000D] + i),
	ioread32(dev->hwregs[c][HW_VC8000D] + i + 4),
	ioread32(dev->hwregs[c][HW_VC8000D] + i + 16),
	ioread32(dev->hwregs[c][HW_VC8000D] + i + 24));
		}
	}
	LOG_DBG("Reg Dump End\n");
}
#endif

static int vdec_sys_reset_init(struct platform_device *pdev, vdec_clk_rst_t *vcrt)
{
	vcrt->rstc_cfg = devm_reset_control_get_shared(&pdev->dev, "cfg");
	if (IS_ERR_OR_NULL(vcrt->rstc_cfg)) {
		dev_err(&pdev->dev, "Failed to get cfg reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_axi = devm_reset_control_get_shared(&pdev->dev, "axi");
	if (IS_ERR_OR_NULL(vcrt->rstc_axi)) {
		dev_err(&pdev->dev, "Failed to get axi reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_moncfg = devm_reset_control_get_shared(&pdev->dev, "moncfg");
	if (IS_ERR_OR_NULL(vcrt->rstc_moncfg)) {
		dev_err(&pdev->dev, "Failed to get moncfg reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_jd_cfg = devm_reset_control_get_optional(&pdev->dev, "jd_cfg");
	if (IS_ERR_OR_NULL(vcrt->rstc_jd_cfg)) {
		dev_err(&pdev->dev, "Failed to get jd_cfg reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_jd_axi = devm_reset_control_get_optional(&pdev->dev, "jd_axi");
	if (IS_ERR_OR_NULL(vcrt->rstc_jd_axi)) {
		dev_err(&pdev->dev, "Failed to get jd_axi reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_vd_cfg = devm_reset_control_get_optional(&pdev->dev, "vd_cfg");
	if (IS_ERR_OR_NULL(vcrt->rstc_vd_cfg)) {
		dev_err(&pdev->dev, "Failed to get vd_cfg reset handle\n");
		return -EFAULT;
	}

	vcrt->rstc_vd_axi = devm_reset_control_get_optional(&pdev->dev, "vd_axi");
	if (IS_ERR_OR_NULL(vcrt->rstc_vd_axi)) {
		dev_err(&pdev->dev, "Failed to get vd_axi reset handle\n");
		return -EFAULT;
	}

	return 0;
}

static int vdec_sys_reset_release(vdec_clk_rst_t *vcrt)
{
	int ret;

	ret = reset_control_deassert(vcrt->rstc_cfg);
	WARN_ON(0 != ret);

	ret = reset_control_deassert(vcrt->rstc_axi);
	WARN_ON(0 != ret);

	ret = reset_control_deassert(vcrt->rstc_moncfg);
	WARN_ON(0 != ret);

	ret = reset_control_reset(vcrt->rstc_jd_cfg);
	WARN_ON(0 != ret);

	ret = reset_control_reset(vcrt->rstc_jd_axi);
	WARN_ON(0 != ret);

	ret = reset_control_reset(vcrt->rstc_vd_cfg);
	WARN_ON(0 != ret);

	ret = reset_control_reset(vcrt->rstc_vd_axi);
	WARN_ON(0 != ret);

	return 0;
}

static int vdec_sys_clk_init(struct platform_device *pdev, vdec_clk_rst_t *vcrt)
{
	int ret;

	vcrt->aclk = devm_clk_get(&pdev->dev, "aclk");
	if (IS_ERR(vcrt->aclk)) {
		ret = PTR_ERR(vcrt->aclk);
		dev_err(&pdev->dev, "failed to get aclk: %d\n", ret);
		return ret;
	}

	vcrt->cfg_clk = devm_clk_get(&pdev->dev, "cfg_clk");
	if (IS_ERR(vcrt->cfg_clk)) {
		ret = PTR_ERR(vcrt->cfg_clk);
		dev_err(&pdev->dev, "failed to get cfg_clk: %d\n", ret);
		return ret;
	}

	vcrt->jd_clk = devm_clk_get(&pdev->dev, "jd_clk");
	if (IS_ERR(vcrt->jd_clk)) {
		ret = PTR_ERR(vcrt->jd_clk);
		dev_err(&pdev->dev, "failed to get jd_clk: %d\n", ret);
		return ret;
	}

	vcrt->vd_clk = devm_clk_get(&pdev->dev, "vd_clk");
	if (IS_ERR(vcrt->vd_clk)) {
		ret = PTR_ERR(vcrt->vd_clk);
		dev_err(&pdev->dev, "failed to get vd_clk: %d\n", ret);
		return ret;
	}

	vcrt->vc_mux = devm_clk_get(&pdev->dev, "vc_mux");
	if (IS_ERR(vcrt->vc_mux)) {
		ret = PTR_ERR(vcrt->vc_mux);
		dev_err(&pdev->dev, "failed to get vc_mux: %d\n", ret);
		return ret;
	}

	vcrt->spll0_fout1 = devm_clk_get(&pdev->dev, "spll0_fout1");
	if (IS_ERR(vcrt->spll0_fout1)) {
		ret = PTR_ERR(vcrt->spll0_fout1);
		dev_err(&pdev->dev, "failed to get spll0_fout1: %d\n", ret);
		return ret;
	}

	vcrt->spll2_fout1 = devm_clk_get(&pdev->dev, "spll2_fout1");
	if (IS_ERR(vcrt->spll2_fout1)) {
		ret = PTR_ERR(vcrt->spll2_fout1);
		dev_err(&pdev->dev, "failed to get spll2_fout1: %d\n", ret);
		return ret;
	}

	vcrt->jd_pclk = devm_clk_get(&pdev->dev, "jd_pclk");
	if (IS_ERR(vcrt->jd_pclk)) {
		ret = PTR_ERR(vcrt->jd_pclk);
		dev_err(&pdev->dev, "failed to get jd_pclk: %d\n", ret);
		return ret;
	}

	vcrt->vd_pclk = devm_clk_get(&pdev->dev, "vd_pclk");
	if (IS_ERR(vcrt->vd_pclk)) {
		ret = PTR_ERR(vcrt->vd_pclk);
		dev_err(&pdev->dev, "failed to get vd_pclk: %d\n", ret);
		return ret;
	}

	vcrt->mon_pclk = devm_clk_get(&pdev->dev, "mon_pclk");
	if (IS_ERR(vcrt->mon_pclk)) {
		ret = PTR_ERR(vcrt->mon_pclk);
		dev_err(&pdev->dev, "failed to get mon_pclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static int vdec_sys_clk_enable(vdec_clk_rst_t *vcrt)
{
	int ret;
	long rate;

	ret = clk_set_parent(vcrt->vc_mux, vcrt->spll2_fout1);
	if (ret < 0) {
		LOG_ERR("Video decoder: failed to set vc_mux parent: %d\n", ret);
		return ret;
	}

	rate = clk_round_rate(vcrt->aclk, VC_ACLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(vcrt->aclk, rate);
		if (ret) {
			LOG_ERR("Video decoder: failed to set aclk: %d\n", ret);
			return ret;
		}
		LOG_INFO("VD set aclk to %ldHZ\n", rate);
	}

	rate = clk_round_rate(vcrt->jd_clk, VDEC_SYS_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(vcrt->jd_clk, rate);
		if (ret) {
			LOG_ERR("Video decoder: failed to set jd_clk: %d\n", ret);
			return ret;
		}
		LOG_INFO("VD set jd_clk to %ldHZ\n", rate);
	}

	rate = clk_round_rate(vcrt->vd_clk, VDEC_SYS_CLK_HIGHEST);
	if (rate > 0) {
		ret = clk_set_rate(vcrt->vd_clk, rate);
		if (ret) {
			LOG_ERR("Video decoder: failed to set vd_clk: %d\n", ret);
			return ret;
		}
		LOG_INFO("VD set vd_clk to %ldHZ\n", rate);
	}

	return vdec_clk_enable(vcrt);
}

static int vdec_clk_enable(vdec_clk_rst_t *vcrt) {
	int ret;

	ret = clk_prepare_enable(vcrt->aclk);
	if (ret) {
		LOG_ERR("Video Decoder: failed to enable aclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->jd_clk);
	if (ret) {
		LOG_ERR("Video Decoder: failed to enable jd_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->vd_clk);
	if (ret) {
		LOG_ERR("Video Decoder: failed to enable vd_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->cfg_clk);
	if (ret) {
		LOG_ERR("Video Decoder: failed to enable cfg_clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->jd_pclk);
	if (ret) {
		LOG_ERR("Video Decoder: failed to enable jd_pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->vd_pclk);
	if (ret) {
		LOG_ERR("Video Decoder: failed to enable vd_pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(vcrt->mon_pclk);
	if (ret) {
		LOG_ERR("Video Decoder: failed to enable mon_pclk: %d\n", ret);
		return ret;
	}

	return 0;
}

static int vdec_clk_disable(vdec_clk_rst_t *vcrt)
{
	clk_disable_unprepare(vcrt->vd_pclk);
	clk_disable_unprepare(vcrt->jd_pclk);
	clk_disable_unprepare(vcrt->jd_clk);
	clk_disable_unprepare(vcrt->vd_clk);
	clk_disable_unprepare(vcrt->mon_pclk);
	clk_disable_unprepare(vcrt->cfg_clk);
	clk_disable_unprepare(vcrt->aclk);

	return 0;
}

static int vdec_hardware_reset(vdec_clk_rst_t *vcrt)
{
	reset_control_assert(vcrt->rstc_jd_cfg);
	reset_control_assert(vcrt->rstc_vd_cfg);
	reset_control_assert(vcrt->rstc_jd_axi);
	reset_control_assert(vcrt->rstc_vd_axi);
	reset_control_assert(vcrt->rstc_moncfg);
	reset_control_assert(vcrt->rstc_axi);
	reset_control_assert(vcrt->rstc_cfg);

	return 0;
}


#ifdef SUPPORT_DMA_HEAP
static int vdec_smmu_dynm_sid_init(struct platform_device *pdev, int numa_id)
{
	int ret;
	unsigned int vccsr_addr[4] = {0};
	void __iomem *vdec_csr_reg = NULL;

	if (of_property_read_u32_array(pdev->dev.of_node, "vccsr-reg", vccsr_addr, 4)) {
		dev_err(&pdev->dev, "vc csr region not found\n");
		return -1;
	}

	vdec_csr_reg = ioremap(vccsr_addr[1], vccsr_addr[3]);
	if (!vdec_csr_reg) {
		LOG_ERR("vdec_csr_reg not initialized\n");
		return -1;
	}

	writel(WIN2030_SID_VDEC, (vdec_csr_reg + VDEC_MMU_AWSSID_OFF));
	writel(WIN2030_SID_VDEC, (vdec_csr_reg + VDEC_MMU_ARSSID_OFF));
	writel(WIN2030_SID_JDEC, (vdec_csr_reg + JDEC_MMU_AWSSID_OFF));
	writel(WIN2030_SID_JDEC, (vdec_csr_reg + JDEC_MMU_ARSSID_OFF));

/*
	ret = win2030_dynm_sid_enable(numa_id);
	if (ret) {
		dev_err(&pdev->dev, "Dynamic smmu stream id setting failed\n");
		return -1;
	}
*/
	{
		unsigned int reg_val;
		unsigned int dynm_csr_en_off, dynm_csr_gnt_off;
		struct regmap *regmap;

		regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "eswin,syscfg");
		if (IS_ERR(regmap)) {
			dev_err(&pdev->dev, "No syscfg phandle specified\n");
			return PTR_ERR(regmap);
		}

		ret = of_property_read_u32_index(pdev->dev.of_node, "eswin,syscfg", 1, &dynm_csr_en_off);
		if (ret) {
			dev_err(&pdev->dev, "No dynm csr enable offset found\n");
			return -1;
		}

		ret = of_property_read_u32_index(pdev->dev.of_node, "eswin,syscfg", 2, &dynm_csr_gnt_off);
		if (ret) {
			dev_err(&pdev->dev, "No dynm csr gnt offset found\n");
			return -1;
		}

		regmap_read(regmap, dynm_csr_en_off, &reg_val);
		reg_val |= (1 << MCPU_SP0_DYMN_CSR_EN_BIT);
		regmap_write(regmap, dynm_csr_en_off, reg_val);

		while(1) {
			regmap_read(regmap, dynm_csr_gnt_off, &reg_val);
			reg_val &= (1 << MCPU_SP0_DYMN_CSR_GNT_BIT);
			if (reg_val)
				break;

			msleep(10);
		}

		regmap_read(regmap, dynm_csr_en_off, &reg_val);
		reg_val &= (~(1U << MCPU_SP0_DYMN_CSR_EN_BIT));
		regmap_write(regmap, dynm_csr_en_off, reg_val);
	}

	return 0;
}
#endif

/* Temporary using this func to do crg init for d1 */
int d1_clk_reset_init(void)
{
	void __iomem *d1_crg_reg = NULL;

	d1_crg_reg = ioremap(0x71828000, 0x1000);
	writel(0x80000020, (d1_crg_reg + 0x1c4));
	writel(0x30003f, (d1_crg_reg + 0x1d0));
	writel(0x80000020, (d1_crg_reg + 0x1d8));
	writel(0x80000020, (d1_crg_reg + 0x1dc));
	writel(0x7, (d1_crg_reg + 0x458));
	writel(0x3, (d1_crg_reg + 0x45c));
	writel(0x3, (d1_crg_reg + 0x464));

	return 0;
}

static int hantro_vdec_probe(struct platform_device *pdev)
{
	int numa_id;
	int ret, vdec_dev_num = 0;
	static int pdev_count = 0;
	vdec_clk_rst_t *vcrt = devm_kzalloc(&pdev->dev, sizeof(vdec_clk_rst_t), GFP_KERNEL);
	if (!vcrt) {
		LOG_ERR("malloc drvdata failed\n");
		return -ENOMEM;
	}

	// pr_info("[%s]build version: %s\n", DEC_DEV_NAME, ES_VDEC_GIT_VER);
	vdec_dev_num = vdec_device_nodes_check();
	if (vdec_dev_num <= 0) {
		LOG_ERR("Invalid video decoder device number\n");
		return -1;
	}

	platform_set_drvdata(pdev, (void *)vcrt);

	if(of_property_read_u32(pdev->dev.of_node, "numa-node-id", &numa_id)) {
		numa_id = 0;
	}

	LOG_INFO("initializing vdec, numa id %d\n", numa_id);

	ret = vdec_sys_reset_init(pdev, vcrt);
	if (ret < 0) {
		LOG_ERR("vdec: reset initialization failed");
		return -1;
	}

	ret = vdec_sys_clk_init(pdev, vcrt);
	if (ret < 0) {
		LOG_ERR("vdec: clk init failed");
		return -1;
	}

	ret = vdec_sys_clk_enable(vcrt);
	if (ret < 0) {
		LOG_ERR("vdec: clk enable failed");
		return -1;
	}

	ret = vdec_sys_reset_release(vcrt);
	if (ret < 0) {
		LOG_ERR("vdec: reset release failed");
		return -1;
	}

	if (!numa_id)
		platformdev = pdev;
	else
	{
		platformdev_d1 = pdev;
		d1_clk_reset_init();
	}

	if (vdec_trans_device_nodes(pdev, numa_id)) {
		LOG_ERR("Translates video decoder dts to subsys failed");
		return -1;
	}

#ifdef SUPPORT_DMA_HEAP
	ret = win2030_tbu_power(&pdev->dev, true);
	if (ret != 0) {
		LOG_ERR("vdec: tbu power up failed\n");
		return -1;
	}

	ret = vdec_smmu_dynm_sid_init(pdev, numa_id);
	if (ret < 0) {
		LOG_ERR("vdec: dynamic smmu sid set failed");
		return -1;
	}
#endif

	pdev_count++;
	if (vdec_dev_num > pdev_count) {
		LOG_INFO("The first core loaded, waiting for another...");
		return 0;
	}

	ret = hantrodec_init();
	if (ret) {
		LOG_NOTICE("load driver %s failed\n", DEC_DEV_NAME);
	} else {
		LOG_NOTICE("module inserted. Major = %d\n", hantrodec_major);

		if (platformdev && vdec_pm_enable(platformdev) < 0) {
			LOG_WARN("enable pm for vdec-die0 failed\n");
		}
		if (platformdev_d1 && vdec_pm_enable(platformdev_d1) < 0) {
			LOG_WARN("enable pm for vdec-die1 failed\n");
		}
	}
	return ret;
}

static int vdec_pm_enable(struct platform_device *pdev) {
	/* The code below assumes runtime PM to be disabled. */
	WARN_ON(pm_runtime_enabled(&pdev->dev));
	pm_runtime_set_autosuspend_delay(&pdev->dev, 1000);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int hantro_vdec_remove(struct platform_device *pdev)
{
#ifdef SUPPORT_DMA_HEAP
	int ret;
#endif
	vdec_clk_rst_t *vcrt;

	pm_runtime_disable(&pdev->dev);
	hantrodec_cleanup();
#ifdef SUPPORT_DMA_HEAP
	ret = win2030_tbu_power(&pdev->dev, false);
	if (ret) {
		LOG_ERR("vdec tbu power down failed\n");
		return -1;
	}
#endif
	vcrt = platform_get_drvdata(pdev);
	vdec_hardware_reset(vcrt);
	vdec_clk_disable(vcrt);

	return 0;
}

static int eswin_vdec_runtime_suspend(struct device *dev) {
	vdec_clk_rst_t *vcrt = NULL;
	int ret = -1;

	vcrt = dev_get_drvdata(dev);
	if (vcrt) {
		ret = win2030_tbu_power(dev, false);
		if (ret != 0) {
			LOG_ERR("tbu power up failed, %d\n", __LINE__);
			return -1;
		}
		ret = vdec_clk_disable(vcrt);
	}
	return ret;
}

static int eswin_vdec_runtime_resume(struct device *dev) {
	vdec_clk_rst_t *vcrt = NULL;
	int ret = -1;

	vcrt = dev_get_drvdata(dev);
	if (vcrt) {
		ret = vdec_clk_enable(vcrt);
		if (ret) {
			LOG_ERR("enable sys clk failed, %d\n", __LINE__);
			return ret;
		}

		ret = win2030_tbu_power(dev, true);
		if (ret != 0) {
			LOG_ERR("tbu power down failed, %d\n", __LINE__);
			return -1;
		}
	}
	return ret;
}

/** <TODO> the jd & vd should be seperated as two devices*/
int vdec_wait_device_idle(struct platform_device *pdev) {
	int ret;

	if (pdev == platformdev) {
		ret = hantrovcmd_wait_core_idle(0, msecs_to_jiffies(500));
		if (ret <= 0) {
			return ret;
		}
		ret = hantrovcmd_wait_core_idle(1, msecs_to_jiffies(500));
		return ret;
	}
	else if (pdev == platformdev_d1) {
		ret = hantrovcmd_wait_core_idle(2, msecs_to_jiffies(500));
		if (ret <= 0) {
			return ret;
		}
		ret = hantrovcmd_wait_core_idle(3, msecs_to_jiffies(500));
		return ret;
	}

	LOG_ERR("Unknown platform device = %p\n", pdev);
	return 1;
}

static int eswin_vdec_suspend(struct device *dev) {
	int ret = 0;
	struct platform_device *pdev = NULL;

	if (!pm_runtime_status_suspended(dev)) {
		pdev = container_of(dev, struct platform_device, dev);
		ret = vdec_wait_device_idle(pdev);
		if (!ret) {
			LOG_ERR("Timeout for vdec_suspend\n");
			return -ETIMEDOUT;
		} else if (ret < 0) {
			LOG_ERR("Interrupt triggered while vdec_suspend\n");
			return -ERESTARTSYS;
		}

		ret = eswin_vdec_runtime_suspend(dev);
	}
	return ret;
}

static int eswin_vdec_resume(struct device *dev) {
	int ret = 0;

	if (!pm_runtime_status_suspended(dev)) {
		ret = eswin_vdec_runtime_resume(dev);
	}
	return ret;
}

static const struct dev_pm_ops eswin_vdec_dev_pm_ops = {
	LATE_SYSTEM_SLEEP_PM_OPS(eswin_vdec_suspend, eswin_vdec_resume)
	RUNTIME_PM_OPS(eswin_vdec_runtime_suspend, eswin_vdec_runtime_resume, NULL)
};

static const struct of_device_id eswin_vdec_match[] = {
	{ .compatible = "eswin,video-decoder0", },
	{ .compatible = "eswin,video-decoder1", },
};

static struct platform_driver eswin_vdec_driver = {
	.probe      = hantro_vdec_probe,
	.remove     = hantro_vdec_remove,
	.driver = {
		.name   = DEC_DEV_NAME,
		.of_match_table = eswin_vdec_match,
		.pm = &eswin_vdec_dev_pm_ops,
	},
};

static int __init hantro_vdec_init(void)
{
    return platform_driver_register(&eswin_vdec_driver);
}

static void __exit hantro_vdec_exit(void)
{
	platform_driver_unregister(&eswin_vdec_driver);
}

module_init(hantro_vdec_init);
module_exit(hantro_vdec_exit);

/* module description */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eswin");
MODULE_DESCRIPTION("driver module for eswin video decoder");
