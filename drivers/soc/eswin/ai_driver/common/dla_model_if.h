// SPDX-License-Identifier: GPL-2.0
/*
 * ESWIN AI driver
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
 * Authors: Lu XiangFeng <luxiangfeng@eswincomputing.com>
 */

#ifndef _DLA_MODEL_IF_H_
#define _DLA_MODEL_IF_H_
#include "dla_interface.h"

static inline uint32_t get_network_op_num(const struct dla_network_desc *network) {
    if(network->version.major_version == 0) {
        return network->v0.num_operations;
    }
    return network->v1.num_operations;
}

static inline int16_t get_network_opdesc_idx(const struct dla_network_desc *network) {
    if(network->version.major_version == 0) {
        return network->v0.operation_desc_index;
    }
    return network->v1.operation_desc_index;
}

static inline int16_t get_network_surf_idx(const struct dla_network_desc *network) {
    if(network->version.major_version == 0) {
        return network->v0.surface_desc_index;
    }
    return network->v1.surface_desc_index;
}

static inline int16_t get_network_dep_idx(const struct dla_network_desc *network) {
    if(network->version.major_version == 0) {
        return network->v0.dependency_graph_index;
    }
    return network->v1.dependency_graph_index;
}

static inline int16_t get_network_lut_idx(const struct dla_network_desc *network) {
    if(network->version.major_version == 0) {
        return network->v0.lut_data_index;
    }
    return network->v1.lut_data_index;
}

static inline int16_t get_network_opconfig_idx(const struct dla_network_desc *network) {
    if(network->version.major_version == 0) {
        return network->v0.op_config_index;
    }
    return network->v1.op_config_index;
}

static inline uint32_t get_consumer_index(const struct dla_network_desc *network, const struct dla_consumer *consumer) {
    if(network->version.major_version == 0) {
        return consumer->v0.index;
    }
    return consumer->v1.index;
}

static inline uint32_t get_consumer_event(const struct dla_network_desc *network, const struct dla_consumer *consumer) {
    if(network->version.major_version == 0) {
        return consumer->v0.event;
    }
    return consumer->v1.event;
}

static inline uint32_t get_op_index(const struct dla_network_desc *network, const struct dla_common_op_desc *op_desc) {
    if(network->version.major_version == 0) {
        return op_desc->v0.index;
    }
    return op_desc->v1.index;
}

static inline uint8_t get_op_depcnt(const struct dla_network_desc *network, const struct dla_common_op_desc *op_desc) {
    if(network->version.major_version == 0) {
        return op_desc->v0.dependency_count;
    }
    return op_desc->v1.dependency_count;
}

static inline uint8_t get_op_type(const struct dla_network_desc *network, const struct dla_common_op_desc *op_desc) {
    if(network->version.major_version == 0) {
        return op_desc->v0.op_type;
    }
    return op_desc->v1.op_type;
}
#endif