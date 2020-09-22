/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/heap_assigner.h"

#include "shared/source/helpers/heap_assigner_config.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/memory_manager/memory_manager.h"

namespace NEO {

HeapAssigner::HeapAssigner() {
    apiAllowExternalHeapForSshAndDsh = HeapAssignerConfig::getConfiguration();
}
bool HeapAssigner::useInternal32BitHeap(GraphicsAllocation::AllocationType allocType) {
    return allocType == GraphicsAllocation::AllocationType::KERNEL_ISA ||
           allocType == GraphicsAllocation::AllocationType::INTERNAL_HEAP;
}
bool HeapAssigner::use32BitHeap(GraphicsAllocation::AllocationType allocType) {
    return useExternal32BitHeap(allocType) || useInternal32BitHeap(allocType);
}
HeapIndex HeapAssigner::get32BitHeapIndex(GraphicsAllocation::AllocationType allocType, bool useLocalMem, const HardwareInfo &hwInfo) {
    if (useInternal32BitHeap(allocType)) {
        return MemoryManager::selectInternalHeap(useLocalMem);
    }
    return MemoryManager::selectExternalHeap(useLocalMem);
}
bool HeapAssigner::useExternal32BitHeap(GraphicsAllocation::AllocationType allocType) {
    if (apiAllowExternalHeapForSshAndDsh) {
        return allocType == GraphicsAllocation::AllocationType::LINEAR_STREAM;
    }
    return false;
}
} // namespace NEO