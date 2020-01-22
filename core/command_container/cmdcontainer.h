/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "core/helpers/heap_helper.h"
#include "core/helpers/non_copyable_or_moveable.h"
#include "core/indirect_heap/indirect_heap.h"
#include "runtime/command_stream/csr_definitions.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace NEO {
class Device;
class GraphicsAllocation;
class LinearStream;
using ResidencyContainer = std::vector<GraphicsAllocation *>;
using HeapType = IndirectHeap::Type;

class CommandContainer : public NonCopyableOrMovableClass {
  public:
    static constexpr size_t defaultListCmdBufferSize = MemoryConstants::kiloByte * 256;
    static constexpr size_t totalCmdBufferSize =
        defaultListCmdBufferSize +
        MemoryConstants::cacheLineSize +
        NEO::CSRequirements::csOverfetchSize;

    CommandContainer() = default;

    GraphicsAllocation *getCmdBufferAllocation() { return cmdBufferAllocation; }

    ResidencyContainer &getResidencyContainer() { return residencyContainer; }

    std::vector<GraphicsAllocation *> &getDeallocationContainer() { return deallocationContainer; }

    void addToResidencyContainer(GraphicsAllocation *alloc);

    LinearStream *getCommandStream() { return commandStream.get(); }

    IndirectHeap *getIndirectHeap(HeapType heapType) { return indirectHeaps[heapType].get(); }

    HeapHelper *getHeapHelper() { return heapHelper.get(); }

    GraphicsAllocation *getIndirectHeapAllocation(HeapType heapType) { return allocationIndirectHeaps[heapType]; }

    void setIndirectHeapAllocation(HeapType heapType, GraphicsAllocation *allocation) { allocationIndirectHeaps[heapType] = allocation; }

    void setCmdBufferAllocation(GraphicsAllocation *allocation) { cmdBufferAllocation = allocation; }

    uint64_t getInstructionHeapBaseAddress() const { return instructionHeapBaseAddress; }

    bool initialize(Device *device);

    virtual ~CommandContainer();

    uint32_t slmSize = std::numeric_limits<uint32_t>::max();

    Device *getDevice() const { return device; }

    void reset();

    bool isHeapDirty(HeapType heapType) const { return (dirtyHeaps & (1u << heapType)); }
    bool isAnyHeapDirty() const { return dirtyHeaps != 0; }
    void setHeapDirty(HeapType heapType) { dirtyHeaps |= (1u << heapType); }
    void setDirtyStateForAllHeaps(bool dirty) { dirtyHeaps = dirty ? std::numeric_limits<uint32_t>::max() : 0; }

  protected:
    Device *device = nullptr;
    std::unique_ptr<HeapHelper> heapHelper;

    GraphicsAllocation *cmdBufferAllocation = nullptr;
    GraphicsAllocation *allocationIndirectHeaps[HeapType::NUM_TYPES] = {};

    uint64_t instructionHeapBaseAddress = 0u;
    uint32_t dirtyHeaps = std::numeric_limits<uint32_t>::max();

    std::unique_ptr<LinearStream> commandStream;
    std::unique_ptr<IndirectHeap> indirectHeaps[HeapType::NUM_TYPES] = {};
    ResidencyContainer residencyContainer;
    std::vector<GraphicsAllocation *> deallocationContainer;
};

} // namespace NEO
