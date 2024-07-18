/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2023 Vivante Corporation
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
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2023 Vivante Corporation
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
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


/*
 *   dts node example:
 *   gpu_3d: gpu@53100000 {
 *       compatible = "verisilicon,galcore";
 *       reg = <0 0x53100000 0 0x40000>,
 *               <0 0x54100000 0 0x40000>;
 *       reg-names = "core_major", "core_3d1";
 *       interrupts = <GIC_SPI 64 IRQ_TYPE_LEVEL_HIGH>,
 *                   <GIC_SPI 65 IRQ_TYPE_LEVEL_HIGH>;
 *       interrupt-names = "core_major", "core_3d1";
 *       clocks = <&clk IMX_SC_R_GPU_0_PID0 IMX_SC_PM_CLK_PER>,
 *               <&clk IMX_SC_R_GPU_0_PID0 IMX_SC_PM_CLK_MISC>,
 *               <&clk IMX_SC_R_GPU_1_PID0 IMX_SC_PM_CLK_PER>,
 *               <&clk IMX_SC_R_GPU_1_PID0 IMX_SC_PM_CLK_MISC>;
 *       clock-names = "core_major", "core_major_sh", "core_3d1", "core_3d1_sh";
 *       assigned-clocks = <&clk IMX_SC_R_GPU_0_PID0 IMX_SC_PM_CLK_PER>,
 *                       <&clk IMX_SC_R_GPU_0_PID0 IMX_SC_PM_CLK_MISC>,
 *                       <&clk IMX_SC_R_GPU_1_PID0 IMX_SC_PM_CLK_PER>,
 *                       <&clk IMX_SC_R_GPU_1_PID0 IMX_SC_PM_CLK_MISC>;
 *       assigned-clock-rates = <700000000>, <850000000>, <800000000>, <1000000000>;
 *       power-domains = <&pd IMX_SC_R_GPU_0_PID0>, <&pd IMX_SC_R_GPU_1_PID0>;
 *       power-domain-names = "core_major", "core_3d1";
 *       contiguous-base = <0x0>;
 *       contiguous-size = <0x1000000>;
 *       status = "okay";
 *   };
 */

#include "gc_hal_kernel_linux.h"
#include "gc_hal_kernel_platform.h"
#include "gc_hal_kernel_platform_win2030.h"
#if gcdSUPPORT_DEVICE_TREE_SOURCE
# include <linux/pm_runtime.h>
# include <linux/pm_domain.h>
# include <linux/clk.h>
# include <linux/reset.h>
#endif

/* Disable MSI for internal FPGA build except PPC */
#if gcdFPGA_BUILD
# define USE_MSI            0
#else
# define USE_MSI            1
#endif

#define gcdMIXED_PLATFORM   0

#define gcdDISABLE_NODE_OFFSET 1

gceSTATUS
_AdjustParam(gcsPLATFORM *Platform, gcsMODULE_PARAMETERS *Args);

gceSTATUS
_GetGPUPhysical(gcsPLATFORM *Platform, gctPHYS_ADDR_T CPUPhysical, gctPHYS_ADDR_T *GPUPhysical);

#if gcdENABLE_MP_SWITCH
gceSTATUS
_SwitchCoreCount(gcsPLATFORM *Platform, gctUINT32 *Count);
#endif

#if gcdENABLE_VIDEO_MEMORY_MIRROR
gceSTATUS
_dmaCopy(gctPOINTER Object, gcsDMA_TRANS_INFO *Info);
#endif

#if gcdSUPPORT_DEVICE_TREE_SOURCE
static int gpu_parse_dt(struct platform_device *pdev, gcsMODULE_PARAMETERS *params);

gceSTATUS
_set_power(gcsPLATFORM *Platform, gctUINT32 DevIndex, gceCORE GPU, gctBOOL Enable);

gceSTATUS
_set_clock(gcsPLATFORM *Platform, gctUINT32 DevIndex, gceCORE GPU, gctBOOL Enable);

gceSTATUS _DmaExit(gcsPLATFORM *Platform);
#endif

static struct _gcsPLATFORM_OPERATIONS default_ops = {
    .adjustParam        = _AdjustParam,
    .getGPUPhysical     = _GetGPUPhysical,
#if gcdENABLE_MP_SWITCH
    .switchCoreCount    = _SwitchCoreCount,
#endif
#if gcdENABLE_VIDEO_MEMORY_MIRROR
    .dmaCopy            = _dmaCopy,
#endif
#if gcdSUPPORT_DEVICE_TREE_SOURCE
    .setPower           = _set_power,
    .setClock           = _set_clock,
    .dmaExit            = _DmaExit,
#endif
};

#if gcdSUPPORT_DEVICE_TREE_SOURCE
#define gcvCLKS_COUNT 7
static char *clk_names[] = { "vc_aclk", "vc_cfg", "g2d_cfg", "g2d_st2", "g2d_clk", "g2d_aclk", "mon_pclk"};
static const int nc_of_clks = gcmCOUNTOF(clk_names);

#define gcvRST_COUNT 6
static char *rst_names[] = { "axi", "cfg", "moncfg", "g2d_core", "g2d_cfg", "g2d_axi" };
static const int nc_of_rsts = gcmCOUNTOF(rst_names);

struct gpu_power_domain {
    int num_domains;
    struct device **power_dev;
    struct clk *clks[gcdDEVICE_COUNT][gcvCLKS_COUNT];
};

static struct _gpu_reset {
    struct reset_control *rsts[gcdDEVICE_COUNT][gcvRST_COUNT];
}gpu_reset;

static struct _gcsPLATFORM default_platform = {
    .name = __FILE__,
    .ops = &default_ops,
};

const char *core_names[] = {
    "core_major",
    "core_3d1",
    "core_3d2",
    "core_3d3",
    "core_3d4",
    "core_3d5",
    "core_3d6",
    "core_3d7",
    "core_3d8",
    "core_3d9",
    "core_3d10",
    "core_3d11",
    "core_3d12",
    "core_3d13",
    "core_3d14",
    "core_3d15",
    "core_2d",
    "core_2d1",
    "core_2d2",
    "core_2d3",
    "core_vg",
#if gcdDEC_ENABLE_AHB
    "core_dec",
#endif
};

static void show_clk_status(int dieIndex)
{
    unsigned long base_addr = (dieIndex == 1) ? 0x71828000 : 0x51828000;
    void __iomem *g2d_top_ptr = ioremap(base_addr, 0x1000);
    printk("base_addr:%lx,vc_aclk=0x%08x,vc_clken=0x%08x,g2d=0x%08x,"
            "vc rst=0x%08x,g2d rst=0x%08x\n",
            base_addr, ioread32(g2d_top_ptr + 0x1c4),
            ioread32(g2d_top_ptr + 0x1d0),
            ioread32(g2d_top_ptr + 0x1cc),
            ioread32(g2d_top_ptr + 0x458),
            ioread32(g2d_top_ptr + 0x46c));
    iounmap(g2d_top_ptr);
}

struct gpu_power_domain gpd;

gceSTATUS
_set_clock(gcsPLATFORM *Platform, gctUINT32 DevIndex, gceCORE GPU, gctBOOL Enable)
{
    int j;
    if (Enable) {
        for (j = 0; j < nc_of_clks; j++) {
            if (gpd.clks[DevIndex][j]) {
                clk_prepare_enable(gpd.clks[DevIndex][j]);
            }
        }
    } else {
        for (j = 0; j < nc_of_clks; j++) {
            if (gpd.clks[DevIndex][j]) {
                clk_disable_unprepare(gpd.clks[DevIndex][j]);
            }
        }
    }

    return gcvSTATUS_OK;
}

gceSTATUS
_set_power(gcsPLATFORM *Platform, gctUINT32 DevIndex, gceCORE GPU, gctBOOL Enable)
{
    int num_domains = gpd.num_domains;
    if (num_domains > 1) {
        struct device *sub_dev = gpd.power_dev[DevIndex];

        if (Enable)
            pm_runtime_get_sync(sub_dev);
        else
            pm_runtime_put(sub_dev);
    }

    if (num_domains == 1) {
        if (Enable)
            pm_runtime_get_sync(&Platform->device->dev);
        else
            pm_runtime_put(&Platform->device->dev);
    }
    return gcvSTATUS_OK;
}

static int gpu_remove_power_domains(struct platform_device *pdev)
{
    int i = 0, j = 0;

    for (i = 0; i < gpd.num_domains; i++) {
        for (j = 0; j < nc_of_clks; j++) {
            if (gpd.clks[i][j]) {
                gpd.clks[i][j] = NULL;
            }
        }

        if (gpd.power_dev) {
            pm_runtime_disable(gpd.power_dev[i]);
            dev_pm_domain_detach(gpd.power_dev[i], true);
        }
    }

    if (gpd.num_domains == 1) {
        pm_runtime_disable(&pdev->dev);
    }

    return 0;
}

static int gpu_add_power_domains(struct platform_device *pdev, gcsMODULE_PARAMETERS *params)
{
    struct device *dev = &pdev->dev;
    int i, j = 0;
    int num_domains = 0;
    int ret = 0;

    memset(&gpd, 0, sizeof(struct gpu_power_domain));
    num_domains = params->devCount;
    gpd.num_domains = num_domains;

    /* If the num of domains is less than 2, the domain will be attached automatically */
    if (num_domains > 1) {
        gpd.power_dev = devm_kcalloc(dev, num_domains, sizeof(struct device *), GFP_KERNEL);
        if (!gpd.power_dev)
            return -ENOMEM;
    }

    for (i = 0; i < num_domains; i++) {
        if (gpd.power_dev) {
            gpd.power_dev[i] = dev_pm_domain_attach_by_id(dev, i);
            if (IS_ERR(gpd.power_dev[i]))
                goto error;
        }

        for (j = 0; j < nc_of_clks; j++) {
            gpd.clks[i][j] = devm_clk_get(dev, clk_names[j]);
            if (IS_ERR(gpd.clks[i][j])) {
                ret = PTR_ERR(gpd.clks[i][j]);
                dev_err(dev, "failed to get die-%d:%s clock: %d\n", i, clk_names[j], ret);
                goto error;
            }
        }
    }

    if (num_domains == 1)
        pm_runtime_enable(&pdev->dev);

    return 0;

error:
    for (i = 0; i < num_domains; i++) {
        if (gpd.power_dev[i])
            dev_pm_domain_detach(gpd.power_dev[i], true);
    }
    return ret;
}

static int g2d_device_node_scan(unsigned char *compatible) {
    struct device_node *np;

    np = of_find_compatible_node(NULL, NULL, compatible);
    if (!np) {
        return -1;
    }
    of_node_put(np);

    return 0;
}

static int g2d_reset(struct device *dev, int dieIndex, int enable) {
    int i, ret;

    if(!dev) return 0;

    if (enable) {
        /*1. get reset handle*/
        for (i = 0; i < nc_of_rsts; i++) {
            if (!strncmp(rst_names[i], "g2d", 3)) {
                gpu_reset.rsts[dieIndex][i] = devm_reset_control_get_optional(dev, rst_names[i]);
            } else {
                /*shared resets*/
                gpu_reset.rsts[dieIndex][i] = devm_reset_control_get_shared(dev, rst_names[i]);
            }
            if (IS_ERR_OR_NULL(gpu_reset.rsts[dieIndex][i])) {
                dev_err(dev, "Failed to get %s reset handle\n", rst_names[i]);
                return -1;
            }
        }
        /*2. deassert the shared reset handle*/
        for (i = 0; i < nc_of_rsts; i++) {
            if (strncmp(rst_names[i], "g2d", 3)) {
                ret = reset_control_deassert(gpu_reset.rsts[dieIndex][i]);
                if (ret) {
                    dev_err(dev, "failed to deassert '%s': %d\n", rst_names[i], ret);
                    return ret;
                }
            }
        }
        /*3. reset the g2d reset handle*/
        for (i = 0; i < nc_of_rsts; i++) {
            if (!strncmp(rst_names[i], "g2d", 3)) {
                ret = reset_control_reset(gpu_reset.rsts[dieIndex][i]);
                if (ret) {
                    dev_err(dev, "failed to reset '%s': %d\n", rst_names[i], ret);
                    return ret;
                }
            }
        }
    } else {
        for (i = 0; i < nc_of_rsts; i++) {
            ret = reset_control_assert(gpu_reset.rsts[dieIndex][i]);
            if (ret) {
                dev_err(dev, "failed to assert '%s': %d\n", rst_names[i], ret);
                return ret;
            }
        }
    }
    return 0;
}

static int gpu_parse_dt(struct platform_device *pdev, gcsMODULE_PARAMETERS *params)
{
    struct device_node *root = pdev->dev.of_node;
    struct resource *res;
    gctUINT32 i, data;
    const gctUINT32 *value;
    const char *str;
    int dieIndex = 0;

    gcmSTATIC_ASSERT(gcvCORE_COUNT == gcmCOUNTOF(core_names),
                     "core_names array does not match core types");

    of_property_read_string(pdev->dev.of_node, "compatible", &str);

    if (!strcmp("eswin,galcore_d0", str)) {
        dieIndex = 0;
    } else if (!strcmp("eswin,galcore_d1", str)) {
        dieIndex = 1;
    }

    /* parse the irqs config */
    for (i = gcvCORE_2D; i <= gcvCORE_2D1; i++) {
        res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, core_names[i]);
        if (res) {
            params->irq2Ds[dieIndex * 2 + (i - gcvCORE_2D)] = res->start;
        } else {
            params->irq2Ds[dieIndex * 2 + (i - gcvCORE_2D)] = platform_get_irq(pdev, i - gcvCORE_2D);
        }
        params->dev2DCoreCounts[dieIndex]++;
    }

    /* parse the registers config */
    for (i = gcvCORE_2D; i <= gcvCORE_2D1; i++) {
        res = platform_get_resource_byname(pdev, IORESOURCE_MEM, core_names[i]);
        if (res) {
            params->register2DBases[dieIndex * 2 + (i - gcvCORE_2D)] = res->start;
            params->register2DSizes[dieIndex * 2 + (i - gcvCORE_2D)] = res->end - res->start + 1;
        }
    }

    /* parse the contiguous mem */
    value = of_get_property(root, "contiguous-size", gcvNULL);
    if (value && be32_to_cpup(value) != 0)
    {
        gctUINT64 addr;

        params->contiguousSize = be32_to_cpup(value);
        if (!of_property_read_u64(root, "contiguous-base", &addr))
            params->contiguousBase = addr;
    }

    value = of_get_property(root, "contiguous-requested", gcvNULL);
    if (value)
    {
        params->contiguousRequested = *value ? gcvTRUE : gcvFALSE;
    }

    /* parse the external mem */
    value = of_get_property(root, "external-size", gcvNULL);
    if (value && be32_to_cpup(value) != 0)
    {
        gctUINT64 addr;

        params->externalSize[dieIndex] = be32_to_cpup(value);
        if (!of_property_read_u64(root, "external-base", &addr))
            params->externalBase[dieIndex] = addr;
    }

    value = of_get_property(root, "phys-size", gcvNULL);
    if (value && be32_to_cpup(value) != 0)
    {
        gctUINT64 addr;

        params->physSize = be32_to_cpup(value);
        if (!of_property_read_u64(root, "base-address", &addr))
            params->baseAddress = addr;
    }

    value = of_get_property(root, "phys-size", gcvNULL);
    if (value)
    {
        params->bankSize = be32_to_cpup(value);
    }
#if 0
    value = of_get_property(root, "recovery", gcvNULL);
    if (value)
    {
        params->recovery = be32_to_cpup(value);
    }
#endif

    value = of_get_property(root, "power-management", gcvNULL);
    if (value)
    {
        params->powerManagement = be32_to_cpup(value);
    }

    value = of_get_property(root, "enable-mmu", gcvNULL);
    if (value)
    {
        params->enableMmu = be32_to_cpup(value);
    }

    value = of_get_property(root, "stuck-dump", gcvNULL);
    if (value)
    {
        params->stuckDump = be32_to_cpup(value);
    }

    value = of_get_property(root, "show-args", gcvNULL);
    if (value)
    {
        params->showArgs = be32_to_cpup(value);
    }

    value = of_get_property(root, "mmu-page-table-pool", gcvNULL);
    if (value)
    {
        params->mmuPageTablePool = be32_to_cpup(value);
    }

    value = of_get_property(root, "mmu-dynamic-map", gcvNULL);
    if (value)
    {
        params->mmuDynamicMap = be32_to_cpup(value);
    }

    value = of_get_property(root, "all-map-in-one", gcvNULL);
    if (value)
    {
        params->allMapInOne = be32_to_cpup(value);
    }

    value = of_get_property(root, "isr-poll-mask", gcvNULL);
    if (value)
    {
        params->isrPoll = be32_to_cpup(value);
    }

    if (!of_property_read_u32(root, "fe-apb-offset", &data)) {
        params->registerAPB = data;
    }

    g2d_reset(&pdev->dev, dieIndex, 1);

    show_clk_status(dieIndex);

    params->devCount++;

    if (params->devCount == 1) {
        unsigned char compatible[32] = { 0 };
        sprintf(compatible, "eswin,galcore_d%d", (dieIndex + 1) % 2);
        if(!g2d_device_node_scan(compatible)){
            return 1;
        }
    }
    return 0;
}

static const struct of_device_id gpu_dt_ids[] = {
    { .compatible = "eswin,galcore_d0",},
    { .compatible = "eswin,galcore_d1",},

    { /* sentinel */ }
};

#elif USE_LINUX_PCIE

typedef struct _gcsBARINFO {
    gctPHYS_ADDR_T  base;
    gctPOINTER      logical;
    gctUINT64       size;
    gctBOOL         available;
    gctUINT64       reg_max_offset;
    gctUINT32       reg_size;
} gcsBARINFO, *gckBARINFO;

struct _gcsPCIEInfo {
    gcsBARINFO      bar[gcdMAX_PCIE_BAR];
    struct pci_dev *pdev;
    gctPHYS_ADDR_T  sram_base;
    gctPHYS_ADDR_T  sram_gpu_base;
    uint32_t        sram_size;
    int             sram_bar;
    int             sram_offset;
    struct completion probed;
};

struct _gcsPLATFORM_PCIE {
    struct _gcsPLATFORM base;
    struct _gcsPCIEInfo pcie_info[gcdPLATFORM_COUNT];
    unsigned int        device_number;
};

struct _gcsPLATFORM_PCIE default_platform = {
    .base = {
        .name = __FILE__,
        .ops  = &default_ops,
    },
};

gctINT
_QueryBarInfo(struct pci_dev *Pdev, gctPHYS_ADDR_T *BarAddr, gctUINT64 *BarSize, gctUINT BarNum)
{
    gctUINT addr, size;
    gctINT is_64_bit = 0;
    gctUINT64 addr64, size64;

    /* Read the bar address */
    if (pci_read_config_dword(Pdev, PCI_BASE_ADDRESS_0 + BarNum * 0x4, &addr) < 0)
        return -1;

    /* Read the bar size */
    if (pci_write_config_dword(Pdev, PCI_BASE_ADDRESS_0 + BarNum * 0x4, 0xffffffff) < 0)
        return -1;

    if (pci_read_config_dword(Pdev, PCI_BASE_ADDRESS_0 + BarNum * 0x4, &size) < 0)
        return -1;

    /* Write back the bar address */
    if (pci_write_config_dword(Pdev, PCI_BASE_ADDRESS_0 + BarNum * 0x4, addr) < 0)
        return -1;

    /* The bar is not working properly */
    if (size == 0xffffffff)
        return -1;

    if ((size & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64)
        is_64_bit = 1;

    addr64 = addr;
    size64 = size;

    if (is_64_bit) {
        /* Read the bar address */
        if (pci_read_config_dword(Pdev, PCI_BASE_ADDRESS_0 + (BarNum + 1) * 0x4, &addr) < 0)
            return -1;

        /* Read the bar size */
        if (pci_write_config_dword(Pdev, PCI_BASE_ADDRESS_0 + (BarNum + 1) * 0x4, 0xffffffff) < 0)
            return -1;

        if (pci_read_config_dword(Pdev, PCI_BASE_ADDRESS_0 + (BarNum + 1) * 0x4, &size) < 0)
            return -1;

        /* Write back the bar address */
        if (pci_write_config_dword(Pdev, PCI_BASE_ADDRESS_0 + (BarNum + 1) * 0x4, addr) < 0)
            return -1;

        addr64 |= ((gctUINT64)addr << 32);
        size64 |= ((gctUINT64)size << 32);
    }

    size64 &= ~0xfULL;
    size64 = ~size64;
    size64 += 1;
    addr64 &= ~0xfULL;

    gcmkPRINT("Bar%d addr=0x%08lx size=0x%08lx", BarNum, addr64, size64);

    *BarAddr = addr64;
    *BarSize = size64;

    return is_64_bit;
}

static const struct pci_device_id vivpci_ids[] = {
  {
    .class       = 0x000000,
    .class_mask  = 0x000000,
    .vendor      = 0x10ee,
    .device      = 0x7012,
    .subvendor   = PCI_ANY_ID,
    .subdevice   = PCI_ANY_ID,
    .driver_data = 0
  },
  {
    .class       = 0x000000,
    .class_mask  = 0x000000,
    .vendor      = 0x10ee,
    .device      = 0x7014,
    .subvendor   = PCI_ANY_ID,
    .subdevice   = PCI_ANY_ID,
    .driver_data = 0
  },
  { /* End: all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, vivpci_ids);

static int
gpu_sub_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
    static u64 dma_mask = DMA_BIT_MASK(40);
# else
    static u64 dma_mask = DMA_40BIT_MASK;
# endif

    struct _gcsPCIEInfo *pcie_info;

    gcmkPRINT("PCIE DRIVER PROBED");
    if (pci_enable_device(pdev))
        pr_err("galcore: pci_enable_device() failed.\n");

    if (pci_set_dma_mask(pdev, dma_mask))
        pr_err("galcore: Failed to set DMA mask.\n");

    pci_set_master(pdev);

    if (pci_request_regions(pdev, "galcore"))
        pr_err("galcore: Failed to get ownership of BAR region.\n");

#if USE_MSI
    if (pci_enable_msi(pdev))
        pr_err("galcore: Failed to enable MSI.\n");
# endif

#if defined(CONFIG_PPC)
    /* On PPC platform, enable bus master, enable irq. */
    if (pci_write_config_word(pdev, 0x4, 0x0006) < 0)
        pr_err("galcore: Failed to enable bus master on PPC.\n");
# endif


    pcie_info = &default_platform.pcie_info[default_platform.device_number++];
    pcie_info->pdev = pdev;

    complete(&pcie_info->probed);

    return 0;
}

static void
gpu_sub_remove(struct pci_dev *pdev)
{
    pci_set_drvdata(pdev, NULL);
#if USE_MSI
    pci_disable_msi(pdev);
# endif
    pci_clear_master(pdev);
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    return;
}

static struct pci_driver gpu_pci_subdriver = {
    .name     = DEVICE_NAME,
    .id_table = vivpci_ids,
    .probe    = gpu_sub_probe,
    .remove   = gpu_sub_remove
};

#else
static struct _gcsPLATFORM default_platform = {
    .name = __FILE__,
    .ops = &default_ops,
};
#endif

gceSTATUS
_AdjustParam(gcsPLATFORM *Platform, gcsMODULE_PARAMETERS *Args)
{
    int ret;
#if gcdSUPPORT_DEVICE_TREE_SOURCE
    ret = gpu_parse_dt(Platform->device, Args);
    if(!ret){
        gpu_add_power_domains(Platform->device, Args);
    }
#elif USE_LINUX_PCIE
    struct _gcsPLATFORM_PCIE *pcie_platform = (struct _gcsPLATFORM_PCIE *)Platform;
    struct pci_dev *pdev = pcie_platform->pcie_info[0].pdev;
    int irqline = pdev->irq;
    unsigned int i, core = 0;
    unsigned int core_count = 0;
    unsigned int core_2d_count = 0;
    unsigned int core_2d = 0;
    gctPOINTER   ptr = gcvNULL;
    int sram_bar = 0;
    int sram_offset = 0;
    unsigned int dev_index = 0;
    unsigned int pdev_index, bar_index = 0;
    unsigned int index = 0;
    unsigned int hw_dev_index = 0;
    unsigned long reg_max_offset = 0;
    unsigned int reg_size = 0;
    int ret;

    if (Args->irqs[gcvCORE_2D] != -1)
        Args->irqs[gcvCORE_2D] = irqline;
    if (Args->irqs[gcvCORE_MAJOR] != -1)
        Args->irqs[gcvCORE_MAJOR] = irqline;

#if gcdMIXED_PLATFORM
    /* Fill the SOC platform paramters first. */
    {
        hw_dev_index += Args->hwDevCounts[dev_index];
        for (; index < hw_dev_index; index++) {
            core_count += Args->devCoreCounts[index];

            for (; core < core_count; core++) {
                /* Fill the SOC platform parameters here. */
                Args->irqs[core] = -1;
                Args->registerBasesMapped[core] = 0;
                Args->registerSizes[core] = 0;
            }
        }

        dev_index++;
    }
#endif

    for (pdev_index = 0; pdev_index < pcie_platform->device_number; pdev_index++) {
        struct pci_dev *pcie_dev = pcie_platform->pcie_info[pdev_index].pdev;

        memset(&pcie_platform->pcie_info[pdev_index].bar[0], 0, sizeof(gcsBARINFO) * gcdMAX_PCIE_BAR);

        Args->devices[dev_index] = &pcie_dev->dev;

        for (i = 0; i < gcdMAX_PCIE_BAR; i++) {
            ret = _QueryBarInfo(pcie_dev,
                                &pcie_platform->pcie_info[pdev_index].bar[i].base,
                                &pcie_platform->pcie_info[pdev_index].bar[i].size,
                                i);

            if (ret < 0)
                continue;

            pcie_platform->pcie_info[pdev_index].bar[i].available = gcvTRUE;
            i += ret;
        }

        hw_dev_index += Args->hwDevCounts[dev_index];

        for (; index < hw_dev_index; index++) {
            core_count += Args->devCoreCounts[index];

            for (i = 0; i < core_count; i++) {
                if (Args->bars[i] != -1 &&
                    Args->regOffsets[i] > pcie_platform->pcie_info[pdev_index].bar[bar_index].reg_max_offset) {
                    bar_index = Args->bars[i];
                    pcie_platform->pcie_info[pdev_index].bar[bar_index].reg_max_offset = Args->regOffsets[i];
                    pcie_platform->pcie_info[pdev_index].bar[bar_index].reg_size = Args->registerSizes[i];
                }
            }

            for (; core < core_count; core++) {
                if (Args->bars[core] != -1) {
                    bar_index = Args->bars[core];
                    Args->irqs[core] = pcie_dev->irq;

                    gcmkASSERT(pcie_platform->pcie_info[pdev_index].bar[bar_index].available);

                    if (Args->regOffsets[core]) {
                        gcmkASSERT(Args->regOffsets[core] + Args->registerSizes[core] <
                                   pcie_platform->pcie_info[pdev_index].bar[bar_index].size);
                    }

                    ptr =  pcie_platform->pcie_info[pdev_index].bar[bar_index].logical;
                    if (!ptr) {
                        reg_max_offset =
                            pcie_platform->pcie_info[pdev_index].bar[bar_index].reg_max_offset;
                        reg_size = reg_max_offset == 0 ?
                                   Args->registerSizes[core] :
                                   pcie_platform->pcie_info[pdev_index].bar[bar_index].reg_size;
                        ptr = pcie_platform->pcie_info[pdev_index].bar[bar_index].logical =
                            (gctPOINTER)pci_iomap(pcie_dev, bar_index, reg_max_offset + reg_size);
                    }

                    if (ptr) {
                        Args->registerBasesMapped[core] =
                            (gctPOINTER)((gctCHAR*)ptr + Args->regOffsets[core]);
                    }
                }
            }

            core_2d_count += Args->dev2DCoreCounts[index];

            for (; core_2d < core_2d_count; core_2d++) {
                if (Args->bar2Ds[core_2d] != -1) {
                    bar_index = Args->bar2Ds[core_2d];
                    Args->irq2Ds[core_2d] = pcie_dev->irq;

                    if (Args->reg2DOffsets[core_2d]) {
                        gcmkASSERT(Args->reg2DOffsets[core_2d] + Args->register2DSizes[core_2d] <
                                   pcie_platform->pcie_info[pdev_index].bar[bar_index].size);
                    }

                    ptr = pcie_platform->pcie_info[pdev_index].bar[bar_index].logical;
                    if (!ptr) {
                        ptr = pcie_platform->pcie_info[pdev_index].bar[bar_index].logical =
                            (gctPOINTER)pci_iomap(pcie_dev, bar_index, Args->register2DSizes[core_2d]);
                    }

                    if (ptr) {
                        Args->register2DBasesMapped[core_2d] =
                            (gctPOINTER)((gctCHAR*)ptr + Args->reg2DOffsets[core_2d]);
                    }
                }
            }

            if (Args->barVG != -1) {
                bar_index = Args->barVG;
                Args->irqVG = pcie_dev->irq;

                if (Args->regVGOffset) {
                    gcmkASSERT(Args->regVGOffset + Args->registerVGSize <
                               pcie_platform->pcie_info[pdev_index].bar[bar_index].size);
                }

                ptr = pcie_platform->pcie_info[pdev_index].bar[bar_index].logical;
                if (!ptr) {
                    ptr = pcie_platform->pcie_info[pdev_index].bar[bar_index].logical =
                        (gctPOINTER)pci_iomap(pcie_dev, bar_index, Args->registerVGSize);
                }

                if (ptr) {
                    Args->registerVGBaseMapped =
                        (gctPOINTER)((gctCHAR*)ptr + Args->regVGOffset);
                }
            }

            /* All the PCIE devices AXI-SRAM should have same base address. */
            pcie_platform->pcie_info[pdev_index].sram_base = Args->extSRAMBases[pdev_index];
            pcie_platform->pcie_info[pdev_index].sram_gpu_base = Args->extSRAMBases[pdev_index];

            pcie_platform->pcie_info[pdev_index].sram_size = Args->extSRAMSizes[pdev_index];

            pcie_platform->pcie_info[pdev_index].sram_bar = Args->sRAMBars[pdev_index];
            sram_bar = Args->sRAMBars[pdev_index];

            pcie_platform->pcie_info[pdev_index].sram_offset = Args->sRAMOffsets[pdev_index];
            sram_offset = Args->sRAMOffsets[pdev_index];
        }

        /* Get CPU view SRAM base address from bar address and bar inside offset. */
        if (sram_bar != -1 && sram_offset != -1) {
            gcmkASSERT(pcie_platform->pcie_info[pdev_index].bar[sram_bar].available);
            pcie_platform->pcie_info[pdev_index].sram_base = pcie_platform->pcie_info[pdev_index].bar[sram_bar].base
                                                           + sram_offset;
            Args->extSRAMBases[pdev_index] = pcie_platform->pcie_info[pdev_index].bar[sram_bar].base
                                           + sram_offset;
        }

        dev_index++;
    }

    Args->contiguousRequested = gcvTRUE;
#endif
    return (ret == 0) ? gcvSTATUS_OK : gcvSTATUS_MORE_DATA;
}

gceSTATUS
_GetGPUPhysical(gcsPLATFORM *Platform, gctPHYS_ADDR_T CPUPhysical, gctPHYS_ADDR_T *GPUPhysical)
{
#if gcdSUPPORT_DEVICE_TREE_SOURCE
#elif USE_LINUX_PCIE
    struct _gcsPLATFORM_PCIE *pcie_platform = (struct _gcsPLATFORM_PCIE *)Platform;
    unsigned int pdev_index;
    /* Only support 1 external shared SRAM currently. */
    gctPHYS_ADDR_T sram_base;
    gctPHYS_ADDR_T sram_gpu_base;
    uint32_t sram_size;

    for (pdev_index = 0; pdev_index < pcie_platform->device_number; pdev_index++) {
        sram_base = pcie_platform->pcie_info[pdev_index].sram_base;
        sram_gpu_base = pcie_platform->pcie_info[pdev_index].sram_gpu_base;
        sram_size = pcie_platform->pcie_info[pdev_index].sram_size;

        if (!sram_size && Platform->dev && Platform->dev->extSRAMSizes[0])
            sram_size = Platform->dev->extSRAMSizes[0];

        if (sram_base != gcvINVALID_PHYSICAL_ADDRESS &&
            sram_gpu_base != gcvINVALID_PHYSICAL_ADDRESS &&
            sram_size) {
            if (CPUPhysical >= sram_base && (CPUPhysical < (sram_base + sram_size))) {
                *GPUPhysical = CPUPhysical - sram_base + sram_gpu_base;

                return gcvSTATUS_OK;
            }
        }
    }
#endif

    *GPUPhysical = CPUPhysical;

    return gcvSTATUS_OK;
}

#if gcdENABLE_MP_SWITCH
gceSTATUS
_SwitchCoreCount(gcsPLATFORM *Platform, gctUINT32 *Count)
{
    *Count = Platform->coreCount;

    return gcvSTATUS_OK;
}
#endif

#if gcdENABLE_VIDEO_MEMORY_MIRROR
gceSTATUS
_dmaCopy(gctPOINTER Object, gcsDMA_TRANS_INFO *Info)
{
    gceSTATUS status = gcvSTATUS_OK;
    gckKERNEL kernel = (gckKERNEL)Object;
    gckVIDMEM_NODE dst_node_obj = (gckVIDMEM_NODE)Info->dst_node;
    gckVIDMEM_NODE src_node_obj = (gckVIDMEM_NODE)Info->src_node;
    gctPOINTER src_ptr = gcvNULL, dst_ptr = gcvNULL;
    gctSIZE_T size0, size1;
    gctPOINTER src_mem_handle = gcvNULL, dst_mem_handle = gcvNULL;
    gctBOOL src_need_unmap = gcvFALSE, dst_need_unmap = gcvFALSE;

    gcsPLATFORM *platform = kernel->os->device->platform;
#if USE_LINUX_PCIE
    struct _gcsPLATFORM_PCIE *pcie_platform = (struct _gcsPLATFORM_PCIE *)platform;
    struct pci_dev *pdev = pcie_platform->pcie_info[kernel->device->platformIndex].pdev;

    /* Make compiler happy. */
    pdev = pdev;
#else
    /* Make compiler happy. */
    platform = platform;
#endif

#if gcdDISABLE_NODE_OFFSET
    Info->offset = 0;
#endif

    status = gckVIDMEM_NODE_GetSize(kernel, src_node_obj, &size0);
    if (status)
        return status;

    status = gckVIDMEM_NODE_GetSize(kernel, dst_node_obj, &size1);
    if (status)
        return status;

    status = gckVIDMEM_NODE_GetMemoryHandle(kernel, src_node_obj, &src_mem_handle);
    if (status)
        return status;

    status = gckVIDMEM_NODE_GetMemoryHandle(kernel, dst_node_obj, &dst_mem_handle);
    if (status)
        return status;

    status = gckVIDMEM_NODE_GetMapKernel(kernel, src_node_obj, &src_ptr);
    if (status)
        return status;

    if (!src_ptr) {
        gctSIZE_T offset;

        status = gckVIDMEM_NODE_GetOffset(kernel, src_node_obj, &offset);
        if (status)
            return status;

        offset += Info->offset;
        status = gckOS_CreateKernelMapping(kernel->os, src_mem_handle, offset, size0, &src_ptr);

        if (status)
            goto error;

        src_need_unmap = gcvTRUE;
    } else
        src_ptr += Info->offset;

    status = gckVIDMEM_NODE_GetMapKernel(kernel, dst_node_obj, &dst_ptr);
    if (status)
        return status;

    if (!dst_ptr) {
        gctSIZE_T offset;

        status = gckVIDMEM_NODE_GetOffset(kernel, dst_node_obj, &offset);
        if (status)
            return status;

        offset += Info->offset;
        status = gckOS_CreateKernelMapping(kernel->os, dst_mem_handle, offset, size1, &dst_ptr);

        if (status)
            goto error;

        dst_need_unmap = gcvTRUE;
    } else
        dst_ptr += Info->offset;


#if gcdDISABLE_NODE_OFFSET
    gckOS_MemCopy(dst_ptr, src_ptr, gcmMIN(size0, size1));
#else
    gckOS_MemCopy(dst_ptr, src_ptr, gcmMIN(Info->bytes, gcmMIN(size0, size1)));
#endif

error:
    if (src_need_unmap && src_ptr)
        gckOS_DestroyKernelMapping(kernel->os, src_mem_handle, src_ptr);

    if (dst_need_unmap && dst_ptr)
        gckOS_DestroyKernelMapping(kernel->os, dst_mem_handle, dst_ptr);

    return status;
}
#endif

int gckPLATFORM_Init(struct platform_driver *pdrv, struct _gcsPLATFORM **platform)
{
    int ret = 0;
#if !gcdSUPPORT_DEVICE_TREE_SOURCE

#if USE_LINUX_PCIE
    u32 timeout = msecs_to_jiffies(5000);
    struct _gcsPCIEInfo *pcie_info;
    struct pci_dev *pdev = NULL;
    int info_count, dev_count = 0;
    int idx = 0;
# endif

    struct platform_device *default_dev = platform_device_alloc(pdrv->driver.name, -1);

    if (!default_dev) {
        pr_err("galcore: platform_device_alloc failed.\n");
        return -ENOMEM;
    }

    /* Add device */
    ret = platform_device_add(default_dev);
    if (ret) {
        pr_err("galcore: platform_device_add failed.\n");
        goto put_dev;
    }

#if USE_LINUX_PCIE
    info_count = gcmCOUNTOF(vivpci_ids);

    do {
        pdev = pci_get_device(vivpci_ids[idx].vendor, vivpci_ids[idx].device, pdev);
        if (pdev)
            dev_count++;
        else
            idx++;
    } while (idx < info_count);

    gcmkASSERT(dev_count <= gcdPLATFORM_COUNT);

    for (idx = 0; idx < dev_count; idx++) {
        pcie_info = &default_platform.pcie_info[idx];
        init_completion(&pcie_info->probed);
    }

    ret = pci_register_driver(&gpu_pci_subdriver);
    if (ret) {
        goto del_dev;
    }

    for (idx = 0; idx < dev_count; idx++) {
        pcie_info = &default_platform.pcie_info[idx];

        timeout = wait_for_completion_timeout(&pcie_info->probed, timeout);
        if (timeout == 0) {
            gcmkTRACE(gcvLEVEL_ERROR, "[galcore] failed to probe pcie device");
            ret = -ENODEV;
            goto pci_unregister;
        }
    }

# endif
#else
    pdrv->driver.of_match_table = gpu_dt_ids;
#endif

    *platform = (gcsPLATFORM *)&default_platform;
    return ret;

#if !gcdSUPPORT_DEVICE_TREE_SOURCE
#if USE_LINUX_PCIE
pci_unregister:
    pci_unregister_driver(&gpu_pci_subdriver);
del_dev:
    platform_device_del(default_dev);
# endif
put_dev:
    platform_device_put(default_dev);
    return ret;
#endif
}

gceSTATUS _DmaExit(gcsPLATFORM *Platform)
{
    int i;
    for (i = 0; i < gcdDEVICE_COUNT; i++) {
        struct device *dev = Platform->params.devices[i];
        if (!dev) continue;
        g2d_reset(dev, i, 0);
        show_clk_status(i);
    }
    return gcvSTATUS_OK;
}

int gckPLATFORM_Terminate(struct _gcsPLATFORM *platform)
{
#if !gcdSUPPORT_DEVICE_TREE_SOURCE
#if USE_LINUX_PCIE
    unsigned int dev_index;
    struct _gcsPLATFORM_PCIE *pcie_platform = (struct _gcsPLATFORM_PCIE *)platform;

    for (dev_index = 0; dev_index < pcie_platform->device_number; dev_index++) {
        unsigned int i;

        for (i = 0; i < gcdMAX_PCIE_BAR; i++) {
            if (pcie_platform->pcie_info[dev_index].bar[i].logical != 0)
                pci_iounmap(pcie_platform->pcie_info[dev_index].pdev,
                            pcie_platform->pcie_info[dev_index].bar[i].logical);
        }
    }

    pci_unregister_driver(&gpu_pci_subdriver);
# endif
    if (platform->device) {
        platform_device_unregister(platform->device);
        platform->device = NULL;
    }
#else
    gpu_remove_power_domains(platform->device);
#endif
    return 0;
}
