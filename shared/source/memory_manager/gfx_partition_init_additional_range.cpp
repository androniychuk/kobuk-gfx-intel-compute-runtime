/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/memory_manager/gfx_partition.h"

namespace NEO {

void GfxPartition::initAdditionalRange(uint64_t gpuAddressSpace, uint64_t &gfxBase, uint64_t &gfxTop, uint32_t rootDeviceIndex, size_t numRootDevices) {
    UNRECOVERABLE_IF("Invalid GPU Address Range!");
}

} // namespace NEO
