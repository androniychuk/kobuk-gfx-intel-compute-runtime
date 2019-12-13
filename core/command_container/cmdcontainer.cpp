/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/command_container/cmdcontainer.h"

#include "core/command_stream/linear_stream.h"
#include "core/helpers/debug_helpers.h"
#include "core/helpers/heap_helper.h"
#include "runtime/command_stream/command_stream_receiver.h"
#include "runtime/device/device.h"
#include "runtime/indirect_heap/indirect_heap.h"
#include "runtime/memory_manager/memory_manager.h"

#include <cassert>

namespace NEO {

CommandContainer::~CommandContainer() {
    if (!device) {
        DEBUG_BREAK_IF(device);
        return;
    }

    auto memoryManager = device->getMemoryManager();

    memoryManager->freeGraphicsMemory(cmdBufferAllocation);

    for (auto allocationIndirectHeap : allocationIndirectHeaps) {
        heapHelper->storeHeapAllocation(allocationIndirectHeap);
    }

    residencyContainer.clear();
    deallocationContainer.clear();
}

bool CommandContainer::initialize(Device *device) {
    if (!device) {
        DEBUG_BREAK_IF(device);
        return false;
    }
    this->device = device;

    heapHelper = std::unique_ptr<HeapHelper>(new HeapHelper(device->getMemoryManager(), device->getDefaultEngine().commandStreamReceiver->getInternalAllocationStorage(), device->getNumAvailableDevices() > 1u));

    size_t alignedSize = alignUp<size_t>(totalCmdBufferSize, MemoryConstants::pageSize64k);
    NEO::AllocationProperties properties{0u, true /* allocateMemory*/, alignedSize,
                                         GraphicsAllocation::AllocationType::INTERNAL_HOST_MEMORY,
                                         (device->getNumAvailableDevices() > 1u) /* multiOsContextCapable */,
                                         false,
                                         NEO::SubDevice::unspecifiedSubDeviceIndex};

    cmdBufferAllocation = device->getMemoryManager()->allocateGraphicsMemoryWithProperties(properties);

    UNRECOVERABLE_IF(!cmdBufferAllocation);

    commandStream = std::unique_ptr<LinearStream>(new LinearStream(cmdBufferAllocation->getUnderlyingBuffer(),
                                                                   defaultListCmdBufferSize));
    commandStream->replaceGraphicsAllocation(cmdBufferAllocation);

    addToResidencyContainer(cmdBufferAllocation);
    size_t heapSize = 65536u;

    for (auto &allocationIndirectHeap : allocationIndirectHeaps) {
        allocationIndirectHeap = heapHelper->getHeapAllocation(heapSize, alignedSize, 0u);
        UNRECOVERABLE_IF(!allocationIndirectHeap);
        residencyContainer.push_back(allocationIndirectHeap);
    }

    uint32_t index = 0;
    for (auto &indirectHeap : indirectHeaps) {
        auto alloc = allocationIndirectHeaps[index++];
        indirectHeap = std::make_unique<IndirectHeap>(alloc);
    }

    instructionHeapBaseAddress = device->getMemoryManager()->getInternalHeapBaseAddress(0);

    return true;
}

void CommandContainer::addToResidencyContainer(NEO::GraphicsAllocation *alloc) {
    if (alloc == nullptr) {
        return;
    }
    auto end = this->residencyContainer.end();
    bool isUnique = (end == std::find(this->residencyContainer.begin(), end, alloc));
    if (isUnique == false) {
        return;
    }

    this->residencyContainer.push_back(alloc);
}

void CommandContainer::reset() {
    dirtyHeaps = std::numeric_limits<uint32_t>::max();
    slmSize = std::numeric_limits<uint32_t>::max();
    getResidencyContainer().clear();
    getDeallocationContainer().clear();

    commandStream->replaceBuffer(this->getCommandStream()->getCpuBase(),
                                 defaultListCmdBufferSize);
    addToResidencyContainer(commandStream->getGraphicsAllocation());

    for (auto &indirectHeap : indirectHeaps) {
        indirectHeap->replaceBuffer(indirectHeap->getCpuBase(),
                                    indirectHeap->getMaxAvailableSpace());
        addToResidencyContainer(indirectHeap->getGraphicsAllocation());
    }
}

} // namespace NEO
