/*
 * Copyright (C) 2020-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/mocks/linux/mock_drm_memory_manager.h"

#include "shared/source/memory_manager/gfx_partition.h"
#include "shared/source/os_interface/linux/allocator_helper.h"
#include "shared/source/os_interface/linux/drm_allocation.h"
#include "shared/source/os_interface/linux/drm_buffer_object.h"
#include "shared/source/os_interface/linux/drm_memory_manager.h"
#include "shared/test/common/mocks/mock_allocation_properties.h"
#include "shared/test/common/mocks/mock_host_ptr_manager.h"
#include "shared/test/common/mocks/mock_memory_manager.h"

#include <atomic>

namespace NEO {
off_t lseekReturn = 4096u;
std::atomic<int> lseekCalledCount(0);
std::atomic<int> closeInputFd(0);
std::atomic<int> closeCalledCount(0);

TestedDrmMemoryManager::TestedDrmMemoryManager(ExecutionEnvironment &executionEnvironment) : MemoryManagerCreate(gemCloseWorkerMode::gemCloseWorkerInactive,
                                                                                                                 false,
                                                                                                                 false,
                                                                                                                 executionEnvironment) {
    this->mmapFunction = SysCalls::mmap;
    this->munmapFunction = SysCalls::munmap;
    this->lseekFunction = &lseekMock;
    this->closeFunction = &closeMock;
    lseekReturn = 4096;
    lseekCalledCount = 0;
    closeInputFd = 0;
    closeCalledCount = 0;
    hostPtrManager.reset(new MockHostPtrManager);
};

TestedDrmMemoryManager::TestedDrmMemoryManager(bool enableLocalMemory,
                                               bool allowForcePin,
                                               bool validateHostPtrMemory,
                                               ExecutionEnvironment &executionEnvironment) : MemoryManagerCreate(false, enableLocalMemory,
                                                                                                                 gemCloseWorkerMode::gemCloseWorkerInactive,
                                                                                                                 allowForcePin,
                                                                                                                 validateHostPtrMemory,
                                                                                                                 executionEnvironment) {
    this->mmapFunction = SysCalls::mmap;
    this->munmapFunction = SysCalls::munmap;
    this->lseekFunction = &lseekMock;
    this->closeFunction = &closeMock;
    lseekReturn = 4096;
    lseekCalledCount = 0;
    closeInputFd = 0;
    closeCalledCount = 0;
    this->executionEnvironment = &executionEnvironment;
}

BufferObject *TestedDrmMemoryManager::findAndReferenceSharedBufferObject(int boHandle, uint32_t rootDeviceIndex) {
    if (failOnfindAndReferenceSharedBufferObject) {
        DrmMockCustom drmMock(*executionEnvironment->rootDeviceEnvironments[rootDeviceIndex]);
        auto patIndex = drmMock.getPatIndex(nullptr, AllocationType::BUFFER, CacheRegion::Default, CachePolicy::WriteBack, false);
        return new (std::nothrow) BufferObject(rootDeviceIndex, &drmMock, patIndex, boHandle, 4096u, 2u);
    }
    return MemoryManagerCreate<DrmMemoryManager>::findAndReferenceSharedBufferObject(boHandle, rootDeviceIndex);
}

void TestedDrmMemoryManager::injectPinBB(BufferObject *newPinBB, uint32_t rootDeviceIndex) {
    BufferObject *currentPinBB = pinBBs[rootDeviceIndex];
    pinBBs[rootDeviceIndex] = nullptr;
    DrmMemoryManager::unreference(currentPinBB, true);
    pinBBs[rootDeviceIndex] = newPinBB;
}

DrmGemCloseWorker *TestedDrmMemoryManager::getgemCloseWorker() { return this->gemCloseWorker.get(); }
void TestedDrmMemoryManager::forceLimitedRangeAllocator(uint64_t range) {
    for (auto &gfxPartition : gfxPartitions) {
        gfxPartition->init(range, getSizeToReserve(), 0, 1);
    }
}
void TestedDrmMemoryManager::overrideGfxPartition(GfxPartition *newGfxPartition) { gfxPartitions[0].reset(newGfxPartition); }

DrmAllocation *TestedDrmMemoryManager::allocate32BitGraphicsMemory(uint32_t rootDeviceIndex, size_t size, const void *ptr, AllocationType allocationType) {
    bool allocateMemory = ptr == nullptr;
    AllocationData allocationData;
    MockAllocationProperties properties(rootDeviceIndex, allocateMemory, size, allocationType);
    getAllocationData(allocationData, properties, ptr, createStorageInfoFromProperties(properties));
    bool useLocalMemory = !allocationData.flags.useSystemMemory && this->localMemorySupported[rootDeviceIndex];
    return static_cast<DrmAllocation *>(allocate32BitGraphicsMemoryImpl(allocationData, useLocalMemory));
}

void TestedDrmMemoryManager::closeSharedHandle(GraphicsAllocation *gfxAllocation) {
    std::unique_lock<std::mutex> lock(callsToCloseSharedHandleMtx);
    DrmMemoryManager::closeSharedHandle(gfxAllocation);
    callsToCloseSharedHandle++;
}

TestedDrmMemoryManager::~TestedDrmMemoryManager() {
    DrmMemoryManager::commonCleanup();
}
}; // namespace NEO
