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
#define gcdSUPPORT_DEVICE_TREE_SOURCE 1

#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include "gc_hal_kernel_linux.h"
#include "gc_hal_kernel_platform.h"
#include "gc_hal_kernel_platform_win2030.h"

/* Disable MSI for internal FPGA build except PPC */
#if gcdFPGA_BUILD
#define USE_MSI     0
#else
#define USE_MSI     1
#endif

gceSTATUS
_AdjustParam(
    IN gcsPLATFORM *Platform,
    OUT gcsMODULE_PARAMETERS *Args
    );

gceSTATUS
_GetGPUPhysical(
    IN gcsPLATFORM * Platform,
    IN gctPHYS_ADDR_T CPUPhysical,
    OUT gctPHYS_ADDR_T *GPUPhysical
    );

gceSTATUS _DmaExit(gcsPLATFORM *Platform);

#if gcdENABLE_MP_SWITCH
gceSTATUS
_SwitchCoreCount(
    IN gcsPLATFORM *Platform,
    OUT gctUINT32 *Count
    );
#endif

#if gcdENABLE_VIDEO_MEMORY_MIRROR
gceSTATUS
_dmaCopy(
    IN gctPOINTER Object,
    IN gctPOINTER DstNode,
    IN gctPOINTER SrcNode
    );
#endif

static struct _gcsPLATFORM_OPERATIONS default_ops =
{
    .adjustParam        = _AdjustParam,
    .getGPUPhysical     = _GetGPUPhysical,
#if gcdENABLE_MP_SWITCH
    .switchCoreCount    = _SwitchCoreCount,
#endif
#if gcdENABLE_VIDEO_MEMORY_MIRROR
    .dmaCopy            = _dmaCopy,
#endif
    .dmaExit            = _DmaExit,
};


#if gcdSUPPORT_DEVICE_TREE_SOURCE

#define gcvCLKS_COUNT 7
static char *clk_names[] = { "vc_aclk", "vc_cfg", "g2d_cfg", "g2d_st2", "g2d_clk", "g2d_aclk", "mon_pclk"};
static const int nc_of_clks = gcmCOUNTOF(clk_names);

#define gcvRST_COUNT 6
static char *rst_names[] = { "axi", "cfg", "moncfg", "g2d_core", "g2d_cfg", "g2d_axi" };
static const int nc_of_rsts = gcmCOUNTOF(rst_names);

static struct _gcsEsw2DParams {
    struct clk *clks[gcdDEVICE_COUNT][gcvCLKS_COUNT];
    struct reset_control *rsts[gcdDEVICE_COUNT][gcvRST_COUNT];
}es2DParas;

static int gpu_parse_dt(struct _gcsPLATFORM *g2d_platform, gcsMODULE_PARAMETERS *params);
static void gpu_add_power_domains(void);

static struct _gcsPLATFORM default_platform =
{
    .name = __FILE__,
    .ops = &default_ops,
    .priv = &es2DParas,
};

static gcsPOWER_DOMAIN domains[] =
{
    [gcvCORE_MAJOR] =
    {
        .base =
        {
            .name = "pd-major",
        },
    },
    [gcvCORE_3D1] =
    {
        .base =
        {
            .name = "pd-3d1",
        },
    },
    [gcvCORE_3D2] =
    {
        .base =
        {
            .name = "pd-3d2",
        },
    },
    [gcvCORE_3D3] =
    {
        .base =
        {
            .name = "pd-3d3",
        },
    },
    [gcvCORE_3D4] =
    {
        .base =
        {
            .name = "pd-3d4",
        },
    },
    [gcvCORE_3D5] =
    {
        .base =
        {
            .name = "pd-3d5",
        },
    },
    [gcvCORE_3D6] =
    {
        .base =
        {
            .name = "pd-3d6",
        },
    },
    [gcvCORE_3D7] =
    {
        .base =
        {
            .name = "pd-3d7",
        },
    },
    [gcvCORE_2D] =
    {
        .base =
        {
            .name = "pd-2d",
        },
    },
    [gcvCORE_VG] =
    {
        .base =
        {
            .name = "pd-vg",
        },
    },
#if gcdDEC_ENABLE_AHB
    [gcvCORE_DEC] =
    {
        .base =
        {
            .name = "pd-dec",
        },
    },
#endif
    [gcvCORE_2D1] =
    {
        .base =
        {
            .name = "pd-2d1",
        },
    },
};

static inline gcsPOWER_DOMAIN *to_gc_power_domain(struct generic_pm_domain *gpd)
{
    return gcmCONTAINEROF(gpd, gcsPOWER_DOMAIN, base);
}

static int gc_power_domain_power_on(    struct generic_pm_domain *gpd)
{
    return 0;
}

static int gc_power_domain_power_off(    struct generic_pm_domain *gpd)
{
    return 0;
}

static int gc_power_domain_probe(struct platform_device *pdev)
{
    gceSTATUS status;
    int ret = 0;
    struct device_node *np = pdev->dev.of_node;
    int core_id;

    ret = of_property_read_u32(np, "core-id", &core_id);
    if (ret)
    {
        gcmONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }
    if (core_id >= gcvCORE_COUNT)
    {
        gcmONERROR(gcvSTATUS_INVALID_ARGUMENT);
    }

    ret = platform_device_add_data(pdev, (gctPOINTER)&domains[core_id], sizeof(gcsPOWER_DOMAIN));
    if (ret)
    {
        gcmONERROR(gcvSTATUS_NOT_SUPPORTED);
    }

    return 0;
OnError:
    return ret;
}

static int gc_power_domain_remove(struct platform_device *pdev)
{
    gcsPOWER_DOMAIN *domain = pdev->dev.platform_data;

    if (IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS))
    {
        of_genpd_del_provider(domain->pdev->dev.of_node);
    }

    return 0;
}

static const struct of_device_id gc_power_domain_dt_ids[] =
{
    {.compatible = "verisilicon,pd-vip",},
    {.compatible = "verisilicon,pd-gpu2d",},
    {.compatible = "verisilicon,pd-gpu3d",},

    {/* sentinel */}
};

static struct platform_driver gc_power_domain_driver =
{
    .driver = {
        .owner = THIS_MODULE,
        .name = "gc-pm-domains",
        .of_match_table = gc_power_domain_dt_ids,
    },
    .probe = gc_power_domain_probe,
    .remove = gc_power_domain_remove,
};

static int gpu_power_domain_init(void)
{
    return platform_driver_register(&gc_power_domain_driver);
}


static void gpu_power_domain_exit(void)
{
    platform_driver_unregister(&gc_power_domain_driver);
}

static void gpu_add_power_domains(void)
{
    struct device_node *np = gcvNULL;
    int ret;
    gceSTATUS status;

    for_each_matching_node(np, gc_power_domain_dt_ids)
    {
        struct platform_device *pdev;
        gcsPOWER_DOMAIN *domain = domains;
        int core_id;

        if (!of_device_is_available(np))
        {
            continue;
        }

        pdev = of_find_device_by_node(np);

        if (!pdev)
            break;

        ret = of_property_read_u32(np, "core-id", &core_id);
        if (ret)
        {
            gcmONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }
        if (core_id >= gcvCORE_COUNT)
        {
            gcmONERROR(gcvSTATUS_INVALID_ARGUMENT);
        }

        domain[core_id].pdev = pdev;
        domain[core_id].core_id = core_id;

        domain[core_id].base.power_on = gc_power_domain_power_on;
        domain[core_id].base.power_off = gc_power_domain_power_off;

        if (IS_ENABLED(CONFIG_PM_GENERIC_DOMAINS))
        {
            ret = pm_genpd_init(&domain[core_id].base, gcvNULL, true);
            if (ret)
            {
                continue;
            }

            ret = of_genpd_add_provider_simple(np, &domain[core_id].base);
            if (ret)
            {
                continue;
            }
        }
    }
OnError:
    return;
}

static const struct of_device_id gpu_dt_ids[] = {
    { .compatible = "verisilicon,galcore",},
    { .compatible = "eswin,galcore_d0",},
    { .compatible = "eswin,galcore_d1",},

    {/* sentinel */}
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

static int g2d_set_clock(struct device *dev, int dieIndex, int enable) {
    int i, ret;

    if(!dev) return 0;

    for (i = 0; i < nc_of_clks; i++) {
        es2DParas.clks[dieIndex][i] = devm_clk_get(dev, clk_names[i]);
        if (IS_ERR(es2DParas.clks[dieIndex][i])) {
            ret = PTR_ERR(es2DParas.clks[dieIndex][i]);
            dev_err(dev, "failed to get %s clock: %d\n", clk_names[i], ret);
            return ret;
        }

        if (enable) {
            ret = clk_prepare_enable(es2DParas.clks[dieIndex][i]);
            if (ret) {
                dev_err(dev, "failed to enable device %s: %d\n", clk_names[i], ret);
                return ret;
            }
        } else {
            clk_disable_unprepare(es2DParas.clks[dieIndex][i]);
        }
    }
    return 0;
}

static int g2d_reset(struct device *dev, int dieIndex, int enable) {
    int i, ret;

    if(!dev) return 0;

    if (enable) {
        /*1. get reset handle*/
        for (i = 0; i < nc_of_rsts; i++) {
            if (!strncmp(rst_names[i], "g2d", 3)) {
                es2DParas.rsts[dieIndex][i] = devm_reset_control_get_optional(dev, rst_names[i]);
            } else {
                /*shared resets*/
                es2DParas.rsts[dieIndex][i] = devm_reset_control_get_shared(dev, rst_names[i]);
            }
            if (IS_ERR_OR_NULL(es2DParas.rsts[dieIndex][i])) {
                dev_err(dev, "Failed to get %s reset handle\n", rst_names[i]);
                return -1;
            }
        }
        /*2. deassert the shared reset handle*/
        for (i = 0; i < nc_of_rsts; i++) {
            if (strncmp(rst_names[i], "g2d", 3)) {
                ret = reset_control_deassert(es2DParas.rsts[dieIndex][i]);
                if (ret) {
                    dev_err(dev, "failed to deassert '%s': %d\n", rst_names[i], ret);
                    return ret;
                }
            }
        }
        /*3. reset the g2d reset handle*/
        for (i = 0; i < nc_of_rsts; i++) {
            if (!strncmp(rst_names[i], "g2d", 3)) {
                ret = reset_control_reset(es2DParas.rsts[dieIndex][i]);
                if (ret) {
                    dev_err(dev, "failed to reset '%s': %d\n", rst_names[i], ret);
                    return ret;
                }
            }
        }
    } else {
        for (i = 0; i < nc_of_rsts; i++) {
            ret = reset_control_assert(es2DParas.rsts[dieIndex][i]);
            if (ret) {
                dev_err(dev, "failed to assert '%s': %d\n", rst_names[i], ret);
                return ret;
            }
        }
    }
    return 0;
}


static int gpu_parse_dt(struct _gcsPLATFORM *g2d_platform, gcsMODULE_PARAMETERS *para)
{
    struct device_node *root = gcvNULL;
    struct resource* res;
    gctUINT32 i;
    const gctUINT32 *value;
    gctUINT32 out_value;
    int ret;
    const char *core_names[] =
    {
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

    gcmSTATIC_ASSERT(gcvCORE_COUNT == gcmCOUNTOF(core_names),
                     "core_names array does not match core types");
    gcmSTATIC_ASSERT(gcvCLKS_COUNT == nc_of_clks,
                         "clk_names array does not match clock counts");
    gcmSTATIC_ASSERT(gcvRST_COUNT == nc_of_rsts,
                             "rst_names array does not match reset counts");
    /*force the 3D core count as zero*/
    para->devCoreCounts[0] = 0;
    for_each_matching_node(root, gpu_dt_ids) {
        struct platform_device *pdev;
        const char *str;
        int dieIndex = -1;

        if (!of_device_is_available(root)) {
            continue;
        }
        pdev = of_find_device_by_node(root);
        if (!pdev) break;

        ret = of_property_read_string(root, "compatible", &str);
        if (!ret) {
            if (!strcmp("eswin,galcore_d0", str) || !strcmp("verisilicon,galcore", str)) {
                dieIndex = 0;
            } else if (!strcmp("eswin,galcore_d1", str)) {
                dieIndex = 1;
            }
            printk("compatible='%s',dieIndex=%d,nc_of_clks=%d\n", str, dieIndex, nc_of_clks);
        }

        show_clk_status(dieIndex);

        if ((ret = g2d_set_clock(&pdev->dev, dieIndex, 1))) {
            return ret;
        }

        if ((ret = g2d_reset(&pdev->dev, dieIndex, 1))) {
            return ret;
        }

        /* parse the irqs config */
        for (i = gcvCORE_2D; i <= gcvCORE_2D1; i++) {
            res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, core_names[i]);
            if (res) {
                para->irq2Ds[dieIndex * 2 + (i - gcvCORE_2D)] = res->start;
            } else {
                para->irq2Ds[dieIndex * 2 + (i - gcvCORE_2D)] = platform_get_irq(pdev, i - gcvCORE_2D);
            }
        }

        /* parse the registers config */
        for (i = 0; i < gcvCORE_COUNT; i++)
        {
            res = platform_get_resource_byname(pdev, IORESOURCE_MEM, core_names[i]);
            if (res)
            {
                if (i >= gcvCORE_2D && i <= gcvCORE_2D_MAX) {
                    para->register2DBases[dieIndex * 2 + (i - gcvCORE_2D)] = res->start;
                    para->register2DSizes[dieIndex * 2 + (i - gcvCORE_2D)] = res->end - res->start + 1;
                }
            }
        }

        /* parse the contiguous mem */
        value = of_get_property(root, "contiguous-size", gcvNULL);
        if (value && be32_to_cpup(value) != 0)
        {
            gctUINT64 addr;

            para->contiguousSize = be32_to_cpup(value);
            if (!of_property_read_u64(root, "contiguous-base", &addr))
                para->contiguousBase = addr;
        }

        value = of_get_property(root, "contiguous-requested", gcvNULL);
        if (value)
        {
            para->contiguousRequested = *value ? gcvTRUE : gcvFALSE;
        }

        /* parse the external mem */
        value = of_get_property(root, "external-size", gcvNULL);
        if (value && be32_to_cpup(value) != 0)
        {
            gctUINT64 addr;

            para->externalSize[dieIndex] = be32_to_cpup(value);
            if (!of_property_read_u64(root, "external-base", &addr))
                para->externalBase[dieIndex] = addr;
        }

        value = of_get_property(root, "phys-size", gcvNULL);
        if (value && be32_to_cpup(value) != 0)
        {
            gctUINT64 addr;

            para->physSize = be32_to_cpup(value);
            if (!of_property_read_u64(root, "base-address", &addr))
                para->baseAddress = addr;
        }

        value = of_get_property(root, "phys-size", gcvNULL);
        if (value)
        {
            para->bankSize = be32_to_cpup(value);
        }
#if 0
        value = of_get_property(root, "recovery", gcvNULL);
        if (value)
        {
            para->recovery = be32_to_cpup(value);
        }
#endif

        value = of_get_property(root, "power-management", gcvNULL);
        if (value)
        {
            para->powerManagement = be32_to_cpup(value);
        }

        value = of_get_property(root, "enable-mmu", gcvNULL);
        if (value)
        {
            para->enableMmu = be32_to_cpup(value);
        }

        value = of_get_property(root, "stuck-dump", gcvNULL);
        if (value)
        {
            para->stuckDump = be32_to_cpup(value);
        }

        value = of_get_property(root, "show-args", gcvNULL);
        if (value)
        {
            para->showArgs = be32_to_cpup(value);
        }

        value = of_get_property(root, "mmu-page-table-pool", gcvNULL);
        if (value)
        {
            para->mmuPageTablePool = be32_to_cpup(value);
        }

        value = of_get_property(root, "mmu-dynamic-map", gcvNULL);
        if (value)
        {
            para->mmuDynamicMap = be32_to_cpup(value);
        }

        value = of_get_property(root, "all-map-in-one", gcvNULL);
        if (value)
        {
            para->allMapInOne = be32_to_cpup(value);
        }

        value = of_get_property(root, "isr-poll-mask", gcvNULL);
        if (value)
        {
            para->isrPoll = be32_to_cpup(value);
        }

        if (!of_property_read_u32(root, "fe-apb-offset", &out_value)) {
            para->registerAPB = out_value;
        }

        para->devices[dieIndex] = &pdev->dev;
        pr_warn("para->devices[%d] = %px\n", dieIndex, para->devices[dieIndex]);
        show_clk_status(dieIndex);

        /*each device have 2 core*/
        para->dev2DCoreCounts[dieIndex] = 2;
        /*set device count*/
        para->devCount++;
    }

    return 0;
}

#elif USE_LINUX_PCIE

typedef struct _gcsBARINFO
{
    gctPHYS_ADDR_T base;
    gctPOINTER logical;
    gctUINT64 size;
    gctBOOL available;
    gctUINT64 reg_max_offset;
    gctUINT32 reg_size;
}
gcsBARINFO, *gckBARINFO;

struct _gcsPCIEInfo
{
    gcsBARINFO bar[gcdMAX_PCIE_BAR];
    struct pci_dev *pdev;
    gctPHYS_ADDR_T sram_bases[gcvSRAM_EXT_COUNT];
    gctPHYS_ADDR_T sram_gpu_bases[gcvSRAM_EXT_COUNT];
    uint32_t sram_sizes[gcvSRAM_EXT_COUNT];
    int sram_bars[gcvSRAM_EXT_COUNT];
    int sram_offsets[gcvSRAM_EXT_COUNT];
};

struct _gcsPLATFORM_PCIE
{
    struct _gcsPLATFORM base;
    struct _gcsPCIEInfo pcie_info[gcdPLATFORM_DEVICE_COUNT];
    unsigned int device_number;
};


struct _gcsPLATFORM_PCIE default_platform =
{
    .base =
    {
        .name = __FILE__,
        .ops  = &default_ops,
    },
};

gctINT
_QueryBarInfo(
    struct pci_dev *Pdev,
    gctPHYS_ADDR_T *BarAddr,
    gctUINT64 *BarSize,
    gctUINT BarNum
    )
{
    gctUINT addr, size;
    gctINT is_64_bit = 0;
    gctUINT64 addr64, size64;

    /* Read the bar address */
    if (pci_read_config_dword(Pdev, PCI_BASE_ADDRESS_0 + BarNum * 0x4, &addr) < 0)
    {
        return -1;
    }

    /* Read the bar size */
    if (pci_write_config_dword(Pdev, PCI_BASE_ADDRESS_0 + BarNum * 0x4, 0xffffffff) < 0)
    {
        return -1;
    }

    if (pci_read_config_dword(Pdev, PCI_BASE_ADDRESS_0 + BarNum * 0x4, &size) < 0)
    {
        return -1;
    }

    /* Write back the bar address */
    if (pci_write_config_dword(Pdev, PCI_BASE_ADDRESS_0 + BarNum * 0x4, addr) < 0)
    {
        return -1;
    }

    /* The bar is not working properly */
    if (size == 0xffffffff)
    {
        return -1;
    }

    if ((size & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64)
    {
        is_64_bit = 1;
    }

    addr64 = addr;
    size64 = size;

    if (is_64_bit)
    {
        /* Read the bar address */
        if (pci_read_config_dword(Pdev, PCI_BASE_ADDRESS_0 + (BarNum + 1) * 0x4, &addr) < 0)
        {
            return -1;
        }

        /* Read the bar size */
        if (pci_write_config_dword(Pdev, PCI_BASE_ADDRESS_0 + (BarNum + 1) * 0x4, 0xffffffff) < 0)
        {
            return -1;
        }

        if (pci_read_config_dword(Pdev, PCI_BASE_ADDRESS_0 + (BarNum + 1) * 0x4, &size) < 0)
        {
            return -1;
        }

        /* Write back the bar address */
        if (pci_write_config_dword(Pdev, PCI_BASE_ADDRESS_0 + (BarNum + 1) * 0x4, addr) < 0)
        {
            return -1;
        }

        addr64 |= ((gctUINT64)addr << 32);
        size64 |= ((gctUINT64)size << 32);
    }

    size64 &= ~0xfULL;
    size64  = ~size64;
    size64 += 1;
    addr64 &= ~0xfULL;

    gcmkPRINT("Bar%d addr=0x%08lx size=0x%08lx", BarNum, addr64, size64);

    *BarAddr = addr64;
    *BarSize = size64;

    return is_64_bit;
}

static const struct pci_device_id vivpci_ids[] = {
  {
    .class = 0x000000,
    .class_mask = 0x000000,
    .vendor = 0x10ee,
    .device = 0x7012,
    .subvendor = PCI_ANY_ID,
    .subdevice = PCI_ANY_ID,
    .driver_data = 0
  },
  {
    .class = 0x000000,
    .class_mask = 0x000000,
    .vendor = 0x10ee,
    .device = 0x7014,
    .subvendor = PCI_ANY_ID,
    .subdevice = PCI_ANY_ID,
    .driver_data = 0
  }, { /* End: all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, vivpci_ids);


static int gpu_sub_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
    static u64 dma_mask = DMA_BIT_MASK(40);
#else
    static u64 dma_mask = DMA_40BIT_MASK;
#endif

    gcmkPRINT("PCIE DRIVER PROBED");
    if (pci_enable_device(pdev)) {
        printk(KERN_ERR "galcore: pci_enable_device() failed.\n");
    }

    if (pci_set_dma_mask(pdev, dma_mask)) {
        printk(KERN_ERR "galcore: Failed to set DMA mask.\n");
    }

    pci_set_master(pdev);

    if (pci_request_regions(pdev, "galcore")) {
        printk(KERN_ERR "galcore: Failed to get ownership of BAR region.\n");
    }

#if USE_MSI
    if (pci_enable_msi(pdev)) {
        printk(KERN_ERR "galcore: Failed to enable MSI.\n");
    }
#endif

#if defined(CONFIG_PPC)
    /* On PPC platform, enable bus master, enable irq. */
    if (pci_write_config_word(pdev, 0x4, 0x0006) < 0) {
        printk(KERN_ERR "galcore: Failed to enable bus master on PPC.\n");
    }
#endif

    default_platform.pcie_info[default_platform.device_number++].pdev = pdev;
    return 0;
}

static void gpu_sub_remove(struct pci_dev *pdev)
{
    pci_set_drvdata(pdev, NULL);
#if USE_MSI
    pci_disable_msi(pdev);
#endif
    pci_clear_master(pdev);
    pci_release_regions(pdev);
    pci_disable_device(pdev);
    return;
}

static struct pci_driver gpu_pci_subdriver = {
    .name = DEVICE_NAME,
    .id_table = vivpci_ids,
    .probe = gpu_sub_probe,
    .remove = gpu_sub_remove
};

static int sRAMCount = 0;

#else
static struct _gcsPLATFORM default_platform =
{
    .name = __FILE__,
    .ops  = &default_ops,
};
#endif



gceSTATUS
_AdjustParam(
    IN gcsPLATFORM *Platform,
    OUT gcsMODULE_PARAMETERS *Args
    )
{
#if gcdSUPPORT_DEVICE_TREE_SOURCE
    gpu_parse_dt(Platform, Args);
    gpu_add_power_domains();
#elif USE_LINUX_PCIE
    struct _gcsPLATFORM_PCIE *pcie_platform = (struct _gcsPLATFORM_PCIE *)Platform;
    struct pci_dev *pdev = pcie_platform->pcie_info[0].pdev;
    int irqline = pdev->irq;
    unsigned int i, core = 0;
    unsigned int coreCount = 0;
    gctPOINTER ptr = gcvNULL;
    unsigned int dev_index, bar_index = 0;
    int sram_bar, sram_offset;
    unsigned int core_index = 0;
    unsigned long reg_max_offset = 0;
    unsigned int reg_size = 0;
    int ret;

    if (Args->irqs[gcvCORE_2D] != -1)
    {
        Args->irqs[gcvCORE_2D] = irqline;
    }
    if (Args->irqs[gcvCORE_MAJOR] != -1)
    {
        Args->irqs[gcvCORE_MAJOR] = irqline;
    }

    for (dev_index = 0; dev_index < pcie_platform->device_number; dev_index++)
    {
        struct pci_dev * pcieDev = pcie_platform->pcie_info[dev_index].pdev;

        memset(&pcie_platform->pcie_info[dev_index].bar[0], 0, sizeof(gcsBARINFO) * gcdMAX_PCIE_BAR);

        for (i = 0; i < gcdMAX_PCIE_BAR; i++)
        {
            ret = _QueryBarInfo(
                        pcieDev,
                        &pcie_platform->pcie_info[dev_index].bar[i].base,
                        &pcie_platform->pcie_info[dev_index].bar[i].size,
                        i
                        );
            if (ret < 0)
            {
                continue;
            }

            pcie_platform->pcie_info[dev_index].bar[i].available = gcvTRUE;
            i += ret;
        }

        coreCount += Args->pdevCoreCount[dev_index];

        for (; core_index < coreCount; core_index++)
        {
            if (Args->bars[core_index] != -1)
            {
                bar_index = Args->bars[core_index];
                if (Args->regOffsets[core_index] > pcie_platform->pcie_info[dev_index].bar[bar_index].reg_max_offset)
                {
                    pcie_platform->pcie_info[dev_index].bar[bar_index].reg_max_offset = Args->regOffsets[core_index];
                    pcie_platform->pcie_info[dev_index].bar[bar_index].reg_size = Args->registerSizes[core_index];
                }
            }
        }

        for (; core < coreCount; core++)
        {
            if (Args->bars[core] != -1)
            {
                bar_index = Args->bars[core];
                Args->irqs[core] = pcieDev->irq;

                gcmkASSERT(pcie_platform->pcie_info[dev_index].bar[bar_index].available);

                if (Args->regOffsets[core])
                {
                    gcmkASSERT(Args->regOffsets[core] + Args->registerSizes[core]
                               < pcie_platform->pcie_info[dev_index].bar[bar_index].size);
                }

                ptr =  pcie_platform->pcie_info[dev_index].bar[bar_index].logical;
                if (!ptr)
                {
                    reg_max_offset = pcie_platform->pcie_info[dev_index].bar[bar_index].reg_max_offset;
                    reg_size = reg_max_offset == 0 ? Args->registerSizes[core] : pcie_platform->pcie_info[dev_index].bar[bar_index].reg_size;
                    ptr = pcie_platform->pcie_info[dev_index].bar[bar_index].logical = (gctPOINTER)pci_iomap(pcieDev, bar_index, reg_max_offset + reg_size);
                }

                if (ptr)
                {
                    Args->registerBasesMapped[core] = (gctPOINTER)((gctCHAR*)ptr + Args->regOffsets[core]);
                }
            }
        }

        /* All the PCIE devices AXI-SRAM should have same base address. */
        for (i = 0; i < gcvSRAM_EXT_COUNT; i++)
        {
            pcie_platform->pcie_info[dev_index].sram_bases[i] =
            pcie_platform->pcie_info[dev_index].sram_gpu_bases[i] = Args->extSRAMBases[i];

            pcie_platform->pcie_info[dev_index].sram_sizes[i] = Args->extSRAMSizes[i];

            pcie_platform->pcie_info[dev_index].sram_bars[i] = sram_bar = Args->sRAMBars[i];
            pcie_platform->pcie_info[dev_index].sram_offsets[i] = sram_offset = Args->sRAMOffsets[i];

            /* Get CPU view SRAM base address from bar address and bar inside offset. */
            if (sram_bar != -1 && sram_offset != -1)
            {
                gcmkASSERT(pcie_platform->pcie_info[dev_index].bar[sram_bar].available);
                pcie_platform->pcie_info[dev_index].sram_bases[i] = Args->extSRAMBases[i]
                                                                  = pcie_platform->pcie_info[dev_index].bar[sram_bar].base
                                                                  + sram_offset;
                sRAMCount += 1;
            }
        }
    }

    Args->contiguousRequested = gcvTRUE;
#endif
    return gcvSTATUS_OK;
}

gceSTATUS
_GetGPUPhysical(
    IN gcsPLATFORM * Platform,
    IN gctPHYS_ADDR_T CPUPhysical,
    OUT gctPHYS_ADDR_T *GPUPhysical
    )
{
#if gcdSUPPORT_DEVICE_TREE_SOURCE
#elif USE_LINUX_PCIE
    struct _gcsPLATFORM_PCIE *pcie_platform = (struct _gcsPLATFORM_PCIE *)Platform;
    /* Only support 1 external shared SRAM currently. */
    gctPHYS_ADDR_T sram_base;
    gctPHYS_ADDR_T sram_gpu_base;
    uint32_t sram_size;
    int i;

    for (i = 0; i < sRAMCount; i++)
    {
        sram_base = pcie_platform->pcie_info[0].sram_bases[i];
        sram_gpu_base = pcie_platform->pcie_info[0].sram_gpu_bases[i];
        sram_size = pcie_platform->pcie_info[0].sram_sizes[i];

        if (!sram_size && Platform->dev && Platform->dev->extSRAMSizes[0])
        {
            sram_size = Platform->dev->extSRAMSizes[0];
        }

        if (sram_base != gcvINVALID_PHYSICAL_ADDRESS && sram_gpu_base != gcvINVALID_PHYSICAL_ADDRESS && sram_size)
        {
            if ((CPUPhysical >= sram_base) && (CPUPhysical < (sram_base + sram_size)))
            {
                *GPUPhysical = CPUPhysical - sram_base + sram_gpu_base;

                return gcvSTATUS_OK;
            }
        }
    }
#endif

    *GPUPhysical = CPUPhysical;

    return gcvSTATUS_OK;
}

gceSTATUS _DmaExit(gcsPLATFORM *Platform)
{
    int i;
    for (i = 0; i < gcdDEVICE_COUNT; i++) {
        struct device *dev = Platform->params.devices[i];
        if (!dev) continue;
        g2d_reset(dev, i, 0);
        g2d_set_clock(dev, i, 0);
        show_clk_status(i);
    }
    return gcvSTATUS_OK;
}

#if gcdENABLE_MP_SWITCH
gceSTATUS
_SwitchCoreCount(
    IN gcsPLATFORM *Platform,
    OUT gctUINT32 *Count
    )
{
    *Count = Platform->coreCount;

    return gcvSTATUS_OK;
}
#endif

#if gcdENABLE_VIDEO_MEMORY_MIRROR
gceSTATUS
_dmaCopy(
    IN gctPOINTER Object,
    IN gctPOINTER DstNode,
    IN gctPOINTER SrcNode
    )
{
    gceSTATUS status = gcvSTATUS_OK;
    gckKERNEL kernel = (gckKERNEL)Object;
    gckVIDMEM_NODE dst_node_obj = (gckVIDMEM_NODE)DstNode;
    gckVIDMEM_NODE src_node_obj = (gckVIDMEM_NODE)SrcNode;
    gctPOINTER src_ptr = gcvNULL, dst_ptr = gcvNULL;
    gctSIZE_T size0, size1;
    gctPOINTER src_mem_handle = gcvNULL, dst_mem_handle = gcvNULL;
    gctBOOL src_need_unmap = gcvFALSE, dst_need_unmap = gcvFALSE;

    status = gckVIDMEM_NODE_GetSize(kernel, src_node_obj, &size0);
    if (status)
    {
        return status;
    }

    status = gckVIDMEM_NODE_GetSize(kernel, dst_node_obj, &size1);
    if (status)
    {
        return status;
    }

    status = gckVIDMEM_NODE_GetMemoryHandle(kernel, src_node_obj, &src_mem_handle);
    if (status)
    {
        return status;
    }

    status = gckVIDMEM_NODE_GetMemoryHandle(kernel, dst_node_obj, &dst_mem_handle);
    if (status)
    {
        return status;
    }

    status = gckVIDMEM_NODE_GetMapKernel(kernel, src_node_obj, &src_ptr);
    if (status)
    {
        return status;
    }

    if (!src_ptr)
    {
        gctSIZE_T offset;

        status = gckVIDMEM_NODE_GetOffset(kernel, src_node_obj, &offset);
        if (status)
        {
            return status;
        }

        status = gckOS_CreateKernelMapping(
                    kernel->os,
                    src_mem_handle,
                    offset,
                    size0,
                    &src_ptr);

        if (status)
        {
            goto error;
        }

        src_need_unmap = gcvTRUE;
    }

    status = gckVIDMEM_NODE_GetMapKernel(kernel, dst_node_obj, &dst_ptr);
    if (status)
    {
        return status;
    }

    if (!dst_ptr)
    {
        gctSIZE_T offset;

        status = gckVIDMEM_NODE_GetOffset(kernel, dst_node_obj, &offset);
        if (status)
        {
            return status;
        }

        status = gckOS_CreateKernelMapping(
            kernel->os,
            dst_mem_handle,
            offset,
            size1,
            &dst_ptr);

        if (status)
        {
            goto error;
        }

        dst_need_unmap = gcvTRUE;
    }

    gckOS_MemCopy(dst_ptr, src_ptr, gcmMIN(size0, size1));

error:
    if (src_need_unmap && src_ptr)
    {
        gckOS_DestroyKernelMapping(
            kernel->os,
            src_mem_handle,
            src_ptr);
    }

    if (dst_need_unmap && dst_ptr)
    {
        gckOS_DestroyKernelMapping(
            kernel->os,
            dst_mem_handle,
            dst_ptr);
    }

    return status;
}
#endif

int gckPLATFORM_Init(struct platform_driver *pdrv,
            struct _gcsPLATFORM **platform)
{
    int ret = 0;
#if !gcdSUPPORT_DEVICE_TREE_SOURCE
    struct platform_device *default_dev = platform_device_alloc(pdrv->driver.name, -1);

    printk(KERN_ERR "lsheng Called gckPLATFORM_Init.\n");
    if (!default_dev) {
        printk(KERN_ERR "galcore: platform_device_alloc failed.\n");
        return -ENOMEM;
    }

    /* Add device */
    ret = platform_device_add(default_dev);
    if (ret) {
        printk(KERN_ERR "galcore: platform_device_add failed.\n");
        platform_device_put(default_dev);
        return ret;
    }

#if USE_LINUX_PCIE
    ret = pci_register_driver(&gpu_pci_subdriver);
    if (ret)
    {
        platform_device_unregister(default_dev);
        return ret;
    }
#endif
#else
    pdrv->driver.of_match_table = gpu_dt_ids;
    ret = gpu_power_domain_init();
    if (ret) {
        printk(KERN_ERR "galcore: gpu_gpc_init failed.\n");
    }
#endif

    *platform = (gcsPLATFORM *)&default_platform;
    return ret;
}

int gckPLATFORM_Terminate(struct _gcsPLATFORM *platform)
{
#if !gcdSUPPORT_DEVICE_TREE_SOURCE
    if (platform->device) {
        platform_device_unregister(platform->device);
        platform->device = NULL;
    }

#if USE_LINUX_PCIE
    {
        unsigned int dev_index;
        struct _gcsPLATFORM_PCIE *pcie_platform = (struct _gcsPLATFORM_PCIE *)platform;
        for (dev_index = 0; dev_index < pcie_platform->device_number; dev_index++)
        {
            unsigned int i;
            for (i = 0; i < gcdMAX_PCIE_BAR; i++)
            {
                if (pcie_platform->pcie_info[dev_index].bar[i].logical != 0)
                {
                    pci_iounmap(pcie_platform->pcie_info[dev_index].pdev, pcie_platform->pcie_info[dev_index].bar[i].logical);
                }
            }
        }

        pci_unregister_driver(&gpu_pci_subdriver);
    }
#endif
#else
    gpu_power_domain_exit();
#endif
    return 0;
}
