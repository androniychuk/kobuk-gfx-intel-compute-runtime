/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/os_interface/windows/wddm_memory_operations_handler.h"

#include "runtime/os_interface/windows/wddm/wddm.h"
#include "runtime/os_interface/windows/wddm_allocation.h"
#include "runtime/os_interface/windows/wddm_residency_allocations_container.h"

#include "engine_limits.h"

namespace NEO {

WddmMemoryOperationsHandler::WddmMemoryOperationsHandler(Wddm *wddm) : wddm(wddm) {
    residentAllocations = std::make_unique<WddmResidentAllocationsContainer>(wddm);
}

MemoryOperationsStatus WddmMemoryOperationsHandler::makeResident(GraphicsAllocation &gfxAllocation) {
    WddmAllocation &wddmAllocation = reinterpret_cast<WddmAllocation &>(gfxAllocation);
    return residentAllocations->makeResidentResources(wddmAllocation.getHandles().data(), wddmAllocation.getNumHandles());
}

MemoryOperationsStatus WddmMemoryOperationsHandler::evict(GraphicsAllocation &gfxAllocation) {
    WddmAllocation &wddmAllocation = reinterpret_cast<WddmAllocation &>(gfxAllocation);
    return residentAllocations->evictResources(wddmAllocation.getHandles().data(), wddmAllocation.getNumHandles());
}

MemoryOperationsStatus WddmMemoryOperationsHandler::isResident(GraphicsAllocation &gfxAllocation) {
    WddmAllocation &wddmAllocation = reinterpret_cast<WddmAllocation &>(gfxAllocation);
    return residentAllocations->isAllocationResident(wddmAllocation.getDefaultHandle());
}

} // namespace NEO
