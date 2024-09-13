#ifndef __ESWIN_IOMMU_RSV__
#define __ESWIN_IOMMU_RSV__
#include <linux/types.h>
#include <linux/device.h>

int iommu_unmap_rsv_iova(struct device *dev, void *cpu_addr, dma_addr_t iova, unsigned long size);
int iommu_map_rsv_iova_with_phys(struct device *dev, dma_addr_t iova, unsigned long size, phys_addr_t paddr, unsigned long attrs);
void *iommu_map_rsv_iova(struct device *dev, dma_addr_t iova, unsigned long size, gfp_t gfp, unsigned long attrs);
ssize_t iommu_rsv_iova_map_sgt(struct device *dev, unsigned long iova, struct sg_table *sgt, unsigned long attrs, size_t buf_size);

#endif
