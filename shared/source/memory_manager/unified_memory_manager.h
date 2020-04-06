/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/common_types.h"
#include "shared/source/memory_manager/residency_container.h"
#include "shared/source/unified_memory/unified_memory.h"
#include "shared/source/utilities/spinlock.h"

#include "memory_properties_flags.h"

#include <cstdint>
#include <map>
#include <mutex>

namespace NEO {
class CommandStreamReceiver;
class GraphicsAllocation;
class MemoryManager;

struct SvmAllocationData {
    GraphicsAllocation *cpuAllocation = nullptr;
    GraphicsAllocation *gpuAllocation = nullptr;
    size_t size = 0;
    InternalMemoryType memoryType = InternalMemoryType::SVM;
    MemoryPropertiesFlags allocationFlagsProperty;
    void *device = nullptr;
};

struct SvmMapOperation {
    void *regionSvmPtr = nullptr;
    size_t regionSize = 0;
    void *baseSvmPtr = nullptr;
    size_t offset = 0;
    bool readOnlyMap = false;
};

class SVMAllocsManager {
  public:
    class MapBasedAllocationTracker {
        friend class SVMAllocsManager;

      public:
        using SvmAllocationContainer = std::map<const void *, SvmAllocationData>;
        void insert(SvmAllocationData);
        void remove(SvmAllocationData);
        SvmAllocationData *get(const void *);
        size_t getNumAllocs() const { return allocations.size(); };

      protected:
        SvmAllocationContainer allocations;
    };

    struct MapOperationsTracker {
        using SvmMapOperationsContainer = std::map<const void *, SvmMapOperation>;
        void insert(SvmMapOperation);
        void remove(const void *);
        SvmMapOperation *get(const void *);
        size_t getNumMapOperations() const { return operations.size(); };

      protected:
        SvmMapOperationsContainer operations;
    };

    struct SvmAllocationProperties {
        bool coherent = false;
        bool hostPtrReadOnly = false;
        bool readOnly = false;
    };

    struct UnifiedMemoryProperties {
        UnifiedMemoryProperties() = default;
        UnifiedMemoryProperties(InternalMemoryType memoryType) : memoryType(memoryType){};
        InternalMemoryType memoryType = InternalMemoryType::NOT_SPECIFIED;
        MemoryPropertiesFlags allocationFlags;
        void *device = nullptr;
        DeviceBitfield subdeviceBitfield;
    };

    SVMAllocsManager(MemoryManager *memoryManager);
    void *createSVMAlloc(uint32_t rootDeviceIndex, size_t size, const SvmAllocationProperties svmProperties);
    void *createUnifiedMemoryAllocation(uint32_t rootDeviceIndex, size_t size, const UnifiedMemoryProperties &svmProperties);
    void *createSharedUnifiedMemoryAllocation(uint32_t rootDeviceIndex, size_t size, const UnifiedMemoryProperties &svmProperties, void *cmdQ);
    SvmAllocationData *getSVMAlloc(const void *ptr);
    bool freeSVMAlloc(void *ptr, bool blocking);
    bool freeSVMAlloc(void *ptr) { return freeSVMAlloc(ptr, false); }
    size_t getNumAllocs() const { return SVMAllocs.getNumAllocs(); }
    MapBasedAllocationTracker *getSVMAllocs() { return &SVMAllocs; }

    void insertSvmMapOperation(void *regionSvmPtr, size_t regionSize, void *baseSvmPtr, size_t offset, bool readOnlyMap);
    void removeSvmMapOperation(const void *regionSvmPtr);
    SvmMapOperation *getSvmMapOperation(const void *regionPtr);
    void addInternalAllocationsToResidencyContainer(ResidencyContainer &residencyContainer, uint32_t requestedTypesMask);
    void makeInternalAllocationsResident(CommandStreamReceiver &commandStreamReceiver, uint32_t requestedTypesMask);
    void *createUnifiedAllocationWithDeviceStorage(uint32_t rootDeviceIndex, size_t size, const SvmAllocationProperties &svmProperties, const UnifiedMemoryProperties &unifiedMemoryProperties);
    void freeSvmAllocationWithDeviceStorage(SvmAllocationData *svmData);

  protected:
    void *createZeroCopySvmAllocation(uint32_t rootDeviceIndex, size_t size, const SvmAllocationProperties &svmProperties);

    void freeZeroCopySvmAllocation(SvmAllocationData *svmData);

    MapBasedAllocationTracker SVMAllocs;
    MapOperationsTracker svmMapOperations;
    MemoryManager *memoryManager;
    SpinLock mtx;
};
} // namespace NEO
