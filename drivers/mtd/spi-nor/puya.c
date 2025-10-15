// SPDX-License-Identifier: GPL-2.0
/*
 * Device Tree Include file for Die0 System peripherals of Eswin EIC770x family SoC.
 *
 * Copyright 2024, Beijing ESWIN Computing Technology Co., Ltd.. All rights reserved.
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
 * Authors: Xiang Xu <xuxiang@eswincomputing.com>
 * 
 */

#include <linux/mtd/spi-nor.h>

#include "core.h"

static const struct flash_info puya_nor_parts[] = {
	{ "py25q128la", INFO(0x856518, 0, 64 * 1024, 256)
		 FLAGS(SPI_NOR_HAS_LOCK | SPI_NOR_HAS_TB)
		NO_SFDP_FLAGS(SECT_4K | SPI_NOR_DUAL_READ |
			      SPI_NOR_QUAD_READ)  },
};

const struct spi_nor_manufacturer spi_nor_puya = {
	.name = "puya",
	.parts = puya_nor_parts,
	.nparts = ARRAY_SIZE(puya_nor_parts),
};
