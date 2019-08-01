/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/memory_manager/memory_manager.h"

namespace NEO {
StorageInfo MemoryManager::createStorageInfoFromProperties(const AllocationProperties &properties) {
    return {};
}
uint32_t StorageInfo::getNumHandles() const { return 1u; }
} // namespace NEO
