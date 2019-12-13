/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/command_stream/command_stream_receiver.h"

#include "core/command_stream/preemption.h"
#include "core/helpers/string.h"
#include "runtime/aub_mem_dump/aub_services.h"
#include "runtime/built_ins/built_ins.h"
#include "runtime/command_stream/experimental_command_buffer.h"
#include "runtime/command_stream/scratch_space_controller.h"
#include "runtime/context/context.h"
#include "runtime/device/device.h"
#include "runtime/event/event.h"
#include "runtime/gtpin/gtpin_notify.h"
#include "runtime/helpers/array_count.h"
#include "runtime/helpers/cache_policy.h"
#include "runtime/helpers/flush_stamp.h"
#include "runtime/helpers/timestamp_packet.h"
#include "runtime/mem_obj/buffer.h"
#include "runtime/memory_manager/internal_allocation_storage.h"
#include "runtime/memory_manager/memory_manager.h"
#include "runtime/memory_manager/surface.h"
#include "runtime/os_interface/os_context.h"
#include "runtime/os_interface/os_interface.h"
#include "runtime/platform/platform.h"
#include "runtime/utilities/tag_allocator.h"

namespace NEO {
// Global table of CommandStreamReceiver factories for HW and tests
CommandStreamReceiverCreateFunc commandStreamReceiverFactory[2 * IGFX_MAX_CORE] = {};

CommandStreamReceiver::CommandStreamReceiver(ExecutionEnvironment &executionEnvironment, uint32_t rootDeviceIndex)
    : executionEnvironment(executionEnvironment), rootDeviceIndex(rootDeviceIndex) {
    residencyAllocations.reserve(20);

    latestSentStatelessMocsConfig = CacheSettings::unknownMocs;
    submissionAggregator.reset(new SubmissionAggregator());
    if (DebugManager.flags.CsrDispatchMode.get()) {
        this->dispatchMode = (DispatchMode)DebugManager.flags.CsrDispatchMode.get();
    }
    flushStamp.reset(new FlushStampTracker(true));
    for (int i = 0; i < IndirectHeap::NUM_TYPES; ++i) {
        indirectHeap[i] = nullptr;
    }
    internalAllocationStorage = std::make_unique<InternalAllocationStorage>(*this);
}

CommandStreamReceiver::~CommandStreamReceiver() {
    for (int i = 0; i < IndirectHeap::NUM_TYPES; ++i) {
        if (indirectHeap[i] != nullptr) {
            auto allocation = indirectHeap[i]->getGraphicsAllocation();
            if (allocation != nullptr) {
                internalAllocationStorage->storeAllocation(std::unique_ptr<GraphicsAllocation>(allocation), REUSABLE_ALLOCATION);
            }
            delete indirectHeap[i];
        }
    }
    cleanupResources();

    internalAllocationStorage->cleanAllocationList(-1, REUSABLE_ALLOCATION);
    internalAllocationStorage->cleanAllocationList(-1, TEMPORARY_ALLOCATION);
    getMemoryManager()->unregisterEngineForCsr(this);
}

void CommandStreamReceiver::makeResident(GraphicsAllocation &gfxAllocation) {
    auto submissionTaskCount = this->taskCount + 1;
    if (gfxAllocation.isResidencyTaskCountBelow(submissionTaskCount, osContext->getContextId())) {
        this->getResidencyAllocations().push_back(&gfxAllocation);
        gfxAllocation.updateTaskCount(submissionTaskCount, osContext->getContextId());
        if (!gfxAllocation.isResident(osContext->getContextId())) {
            this->totalMemoryUsed += gfxAllocation.getUnderlyingBufferSize();
        }
    }
    gfxAllocation.updateResidencyTaskCount(submissionTaskCount, osContext->getContextId());
}

void CommandStreamReceiver::processEviction() {
    this->getEvictionAllocations().clear();
}

void CommandStreamReceiver::makeNonResident(GraphicsAllocation &gfxAllocation) {
    if (gfxAllocation.isResident(osContext->getContextId())) {
        if (gfxAllocation.peekEvictable()) {
            this->getEvictionAllocations().push_back(&gfxAllocation);
        } else {
            gfxAllocation.setEvictable(true);
        }
    }

    gfxAllocation.releaseResidencyInOsContext(this->osContext->getContextId());
}

void CommandStreamReceiver::makeSurfacePackNonResident(ResidencyContainer &allocationsForResidency) {
    for (auto &surface : allocationsForResidency) {
        this->makeNonResident(*surface);
    }
    allocationsForResidency.clear();
    this->processEviction();
}

void CommandStreamReceiver::makeResidentHostPtrAllocation(GraphicsAllocation *gfxAllocation) {
    makeResident(*gfxAllocation);
}

void CommandStreamReceiver::waitForTaskCountAndCleanAllocationList(uint32_t requiredTaskCount, uint32_t allocationUsage) {
    auto address = getTagAddress();
    if (address) {
        while (*address < requiredTaskCount)
            ;
    }
    internalAllocationStorage->cleanAllocationList(requiredTaskCount, allocationUsage);
}

void CommandStreamReceiver::ensureCommandBufferAllocation(LinearStream &commandStream, size_t minimumRequiredSize, size_t additionalAllocationSize) {
    if (commandStream.getAvailableSpace() >= minimumRequiredSize) {
        return;
    }

    const auto allocationSize = alignUp(minimumRequiredSize + additionalAllocationSize, MemoryConstants::pageSize64k);
    constexpr static auto allocationType = GraphicsAllocation::AllocationType::COMMAND_BUFFER;
    auto allocation = this->getInternalAllocationStorage()->obtainReusableAllocation(allocationSize, allocationType).release();
    if (allocation == nullptr) {
        const AllocationProperties commandStreamAllocationProperties{rootDeviceIndex, true, allocationSize, allocationType,
                                                                     isMultiOsContextCapable(), false, getDeviceIndex()};
        allocation = this->getMemoryManager()->allocateGraphicsMemoryWithProperties(commandStreamAllocationProperties);
    }
    DEBUG_BREAK_IF(allocation == nullptr);

    if (commandStream.getGraphicsAllocation() != nullptr) {
        getInternalAllocationStorage()->storeAllocation(std::unique_ptr<GraphicsAllocation>(commandStream.getGraphicsAllocation()), REUSABLE_ALLOCATION);
    }

    commandStream.replaceBuffer(allocation->getUnderlyingBuffer(), allocationSize - additionalAllocationSize);
    commandStream.replaceGraphicsAllocation(allocation);
}

MemoryManager *CommandStreamReceiver::getMemoryManager() const {
    DEBUG_BREAK_IF(!executionEnvironment.memoryManager);
    return executionEnvironment.memoryManager.get();
}

LinearStream &CommandStreamReceiver::getCS(size_t minRequiredSize) {
    constexpr static auto additionalAllocationSize = MemoryConstants::cacheLineSize + CSRequirements::csOverfetchSize;
    ensureCommandBufferAllocation(this->commandStream, minRequiredSize, additionalAllocationSize);
    return commandStream;
}

void CommandStreamReceiver::cleanupResources() {
    waitForTaskCountAndCleanAllocationList(this->latestFlushedTaskCount, TEMPORARY_ALLOCATION);
    waitForTaskCountAndCleanAllocationList(this->latestFlushedTaskCount, REUSABLE_ALLOCATION);

    if (debugSurface) {
        getMemoryManager()->freeGraphicsMemory(debugSurface);
        debugSurface = nullptr;
    }

    if (commandStream.getCpuBase()) {
        getMemoryManager()->freeGraphicsMemory(commandStream.getGraphicsAllocation());
        commandStream.replaceGraphicsAllocation(nullptr);
        commandStream.replaceBuffer(nullptr, 0);
    }

    if (tagAllocation) {
        getMemoryManager()->freeGraphicsMemory(tagAllocation);
        tagAllocation = nullptr;
        tagAddress = nullptr;
    }

    if (preemptionAllocation) {
        getMemoryManager()->freeGraphicsMemory(preemptionAllocation);
        preemptionAllocation = nullptr;
    }

    if (perDssBackedBuffer) {
        getMemoryManager()->freeGraphicsMemory(perDssBackedBuffer);
        perDssBackedBuffer = nullptr;
    }
}

bool CommandStreamReceiver::waitForCompletionWithTimeout(bool enableTimeout, int64_t timeoutMicroseconds, uint32_t taskCountToWait) {
    std::chrono::high_resolution_clock::time_point time1, time2;
    int64_t timeDiff = 0;

    uint32_t latestSentTaskCount = this->latestFlushedTaskCount;
    if (latestSentTaskCount < taskCountToWait) {
        if (!this->flushBatchedSubmissions()) {
            return false;
        }
    }

    time1 = std::chrono::high_resolution_clock::now();
    while (*getTagAddress() < taskCountToWait && timeDiff <= timeoutMicroseconds) {
        std::this_thread::yield();
        _mm_pause();

        if (enableTimeout) {
            time2 = std::chrono::high_resolution_clock::now();
            timeDiff = std::chrono::duration_cast<std::chrono::microseconds>(time2 - time1).count();
        }
    }
    if (*getTagAddress() >= taskCountToWait) {
        if (gtpinIsGTPinInitialized()) {
            gtpinNotifyTaskCompletion(taskCountToWait);
        }
        return true;
    }
    return false;
}

void CommandStreamReceiver::setTagAllocation(GraphicsAllocation *allocation) {
    this->tagAllocation = allocation;
    UNRECOVERABLE_IF(allocation == nullptr);
    this->tagAddress = reinterpret_cast<uint32_t *>(allocation->getUnderlyingBuffer());
}

FlushStamp CommandStreamReceiver::obtainCurrentFlushStamp() const {
    return flushStamp->peekStamp();
}

void CommandStreamReceiver::setRequiredScratchSizes(uint32_t newRequiredScratchSize, uint32_t newRequiredPrivateScratchSize) {
    if (newRequiredScratchSize > requiredScratchSize) {
        requiredScratchSize = newRequiredScratchSize;
    }
    if (newRequiredPrivateScratchSize > requiredPrivateScratchSize) {
        requiredPrivateScratchSize = newRequiredPrivateScratchSize;
    }
}

GraphicsAllocation *CommandStreamReceiver::getScratchAllocation() {
    return scratchSpaceController->getScratchSpaceAllocation();
}

void CommandStreamReceiver::initProgrammingFlags() {
    isPreambleSent = false;
    GSBAFor32BitProgrammed = false;
    bindingTableBaseAddressRequired = true;
    mediaVfeStateDirty = true;
    lastVmeSubslicesConfig = false;

    lastSentL3Config = 0;
    lastSentCoherencyRequest = -1;
    lastMediaSamplerConfig = -1;
    lastPreemptionMode = PreemptionMode::Initial;
    latestSentStatelessMocsConfig = 0;
}

void CommandStreamReceiver::programForAubSubCapture(bool wasActiveInPreviousEnqueue, bool isActive) {
    if (!wasActiveInPreviousEnqueue && isActive) {
        // force CSR reprogramming upon subcapture activation
        this->initProgrammingFlags();
    }
    if (wasActiveInPreviousEnqueue && !isActive) {
        // flush BB upon subcapture deactivation
        this->flushBatchedSubmissions();
    }
}

ResidencyContainer &CommandStreamReceiver::getResidencyAllocations() {
    return this->residencyAllocations;
}

ResidencyContainer &CommandStreamReceiver::getEvictionAllocations() {
    return this->evictionAllocations;
}

AubSubCaptureStatus CommandStreamReceiver::checkAndActivateAubSubCapture(const MultiDispatchInfo &dispatchInfo) { return {false, false}; }

void CommandStreamReceiver::addAubComment(const char *comment) {}

GraphicsAllocation *CommandStreamReceiver::allocateDebugSurface(size_t size) {
    UNRECOVERABLE_IF(debugSurface != nullptr);
    debugSurface = getMemoryManager()->allocateGraphicsMemoryWithProperties({rootDeviceIndex, size, GraphicsAllocation::AllocationType::INTERNAL_HOST_MEMORY});
    return debugSurface;
}

IndirectHeap &CommandStreamReceiver::getIndirectHeap(IndirectHeap::Type heapType,
                                                     size_t minRequiredSize) {
    DEBUG_BREAK_IF(static_cast<uint32_t>(heapType) >= arrayCount(indirectHeap));
    auto &heap = indirectHeap[heapType];
    GraphicsAllocation *heapMemory = nullptr;

    if (heap)
        heapMemory = heap->getGraphicsAllocation();

    if (heap && heap->getAvailableSpace() < minRequiredSize && heapMemory) {
        internalAllocationStorage->storeAllocation(std::unique_ptr<GraphicsAllocation>(heapMemory), REUSABLE_ALLOCATION);
        heapMemory = nullptr;
    }

    if (!heapMemory) {
        allocateHeapMemory(heapType, minRequiredSize, heap);
    }

    return *heap;
}

uint32_t CommandStreamReceiver::getDeviceIndex() const {
    return osContext->getDeviceBitfield().any() ? static_cast<uint32_t>(Math::log2(static_cast<uint32_t>(osContext->getDeviceBitfield().to_ulong()))) : 0u;
}

void CommandStreamReceiver::allocateHeapMemory(IndirectHeap::Type heapType,
                                               size_t minRequiredSize, IndirectHeap *&indirectHeap) {
    size_t reservedSize = 0;
    auto finalHeapSize = defaultHeapSize;
    if (IndirectHeap::SURFACE_STATE == heapType) {
        finalHeapSize = defaultSshSize;
    }
    bool requireInternalHeap = IndirectHeap::INDIRECT_OBJECT == heapType ? true : false;

    if (DebugManager.flags.AddPatchInfoCommentsForAUBDump.get()) {
        requireInternalHeap = false;
    }

    minRequiredSize += reservedSize;

    finalHeapSize = alignUp(std::max(finalHeapSize, minRequiredSize), MemoryConstants::pageSize);
    auto allocationType = GraphicsAllocation::AllocationType::LINEAR_STREAM;
    if (requireInternalHeap) {
        allocationType = GraphicsAllocation::AllocationType::INTERNAL_HEAP;
    }
    auto heapMemory = internalAllocationStorage->obtainReusableAllocation(finalHeapSize, allocationType).release();

    if (!heapMemory) {
        heapMemory = getMemoryManager()->allocateGraphicsMemoryWithProperties({rootDeviceIndex, true, finalHeapSize, allocationType,
                                                                               isMultiOsContextCapable(), false, getDeviceIndex()});
    } else {
        finalHeapSize = std::max(heapMemory->getUnderlyingBufferSize(), finalHeapSize);
    }

    if (IndirectHeap::SURFACE_STATE == heapType) {
        DEBUG_BREAK_IF(minRequiredSize > defaultSshSize - MemoryConstants::pageSize);
        finalHeapSize = defaultSshSize - MemoryConstants::pageSize;
    }

    if (indirectHeap) {
        indirectHeap->replaceBuffer(heapMemory->getUnderlyingBuffer(), finalHeapSize);
        indirectHeap->replaceGraphicsAllocation(heapMemory);
    } else {
        indirectHeap = new IndirectHeap(heapMemory, requireInternalHeap);
        indirectHeap->overrideMaxSize(finalHeapSize);
    }
    scratchSpaceController->reserveHeap(heapType, indirectHeap);
}

void CommandStreamReceiver::releaseIndirectHeap(IndirectHeap::Type heapType) {
    DEBUG_BREAK_IF(static_cast<uint32_t>(heapType) >= arrayCount(indirectHeap));
    auto &heap = indirectHeap[heapType];

    if (heap) {
        auto heapMemory = heap->getGraphicsAllocation();
        if (heapMemory != nullptr)
            internalAllocationStorage->storeAllocation(std::unique_ptr<GraphicsAllocation>(heapMemory), REUSABLE_ALLOCATION);
        heap->replaceBuffer(nullptr, 0);
        heap->replaceGraphicsAllocation(nullptr);
    }
}

void CommandStreamReceiver::setExperimentalCmdBuffer(std::unique_ptr<ExperimentalCommandBuffer> &&cmdBuffer) {
    experimentalCmdBuffer = std::move(cmdBuffer);
}

bool CommandStreamReceiver::initializeTagAllocation() {
    auto tagAllocation = getMemoryManager()->allocateGraphicsMemoryWithProperties({rootDeviceIndex, MemoryConstants::pageSize, GraphicsAllocation::AllocationType::TAG_BUFFER});
    if (!tagAllocation) {
        return false;
    }

    this->setTagAllocation(tagAllocation);
    *this->tagAddress = DebugManager.flags.EnableNullHardware.get() ? -1 : initialHardwareTag;

    return true;
}

bool CommandStreamReceiver::createPreemptionAllocation() {
    auto hwInfo = executionEnvironment.getHardwareInfo();
    AllocationProperties properties{rootDeviceIndex, true, hwInfo->capabilityTable.requiredPreemptionSurfaceSize, GraphicsAllocation::AllocationType::PREEMPTION, false};
    properties.flags.uncacheable = hwInfo->workaroundTable.waCSRUncachable;
    properties.alignment = 256 * MemoryConstants::kiloByte;
    this->preemptionAllocation = getMemoryManager()->allocateGraphicsMemoryWithProperties(properties);
    return this->preemptionAllocation != nullptr;
}

std::unique_lock<CommandStreamReceiver::MutexType> CommandStreamReceiver::obtainUniqueOwnership() {
    return std::unique_lock<CommandStreamReceiver::MutexType>(this->ownershipMutex);
}
AllocationsList &CommandStreamReceiver::getTemporaryAllocations() { return internalAllocationStorage->getTemporaryAllocations(); }
AllocationsList &CommandStreamReceiver::getAllocationsForReuse() { return internalAllocationStorage->getAllocationsForReuse(); }

bool CommandStreamReceiver::createAllocationForHostSurface(HostPtrSurface &surface, bool requiresL3Flush) {
    auto memoryManager = getMemoryManager();
    AllocationProperties properties{rootDeviceIndex, false, surface.getSurfaceSize(), GraphicsAllocation::AllocationType::EXTERNAL_HOST_PTR, false};
    properties.flags.flushL3RequiredForRead = properties.flags.flushL3RequiredForWrite = requiresL3Flush;
    auto allocation = memoryManager->allocateGraphicsMemoryWithProperties(properties, surface.getMemoryPointer());
    if (allocation == nullptr && surface.peekIsPtrCopyAllowed()) {
        // Try with no host pointer allocation and copy
        AllocationProperties copyProperties{rootDeviceIndex, surface.getSurfaceSize(), GraphicsAllocation::AllocationType::INTERNAL_HOST_MEMORY};
        copyProperties.alignment = MemoryConstants::pageSize;
        allocation = memoryManager->allocateGraphicsMemoryWithProperties(copyProperties);
        if (allocation) {
            memcpy_s(allocation->getUnderlyingBuffer(), allocation->getUnderlyingBufferSize(), surface.getMemoryPointer(), surface.getSurfaceSize());
        }
    }
    if (allocation == nullptr) {
        return false;
    }
    allocation->updateTaskCount(Event::eventNotReady, osContext->getContextId());
    surface.setAllocation(allocation);
    internalAllocationStorage->storeAllocation(std::unique_ptr<GraphicsAllocation>(allocation), TEMPORARY_ALLOCATION);
    return true;
}

TagAllocator<HwTimeStamps> *CommandStreamReceiver::getEventTsAllocator() {
    if (profilingTimeStampAllocator.get() == nullptr) {
        profilingTimeStampAllocator = std::make_unique<TagAllocator<HwTimeStamps>>(rootDeviceIndex, getMemoryManager(), getPreferredTagPoolSize(), MemoryConstants::cacheLineSize);
    }
    return profilingTimeStampAllocator.get();
}

TagAllocator<HwPerfCounter> *CommandStreamReceiver::getEventPerfCountAllocator(const uint32_t tagSize) {
    if (perfCounterAllocator.get() == nullptr) {
        perfCounterAllocator = std::make_unique<TagAllocator<HwPerfCounter>>(rootDeviceIndex, getMemoryManager(), getPreferredTagPoolSize(), MemoryConstants::cacheLineSize, tagSize);
    }
    return perfCounterAllocator.get();
}

TagAllocator<TimestampPacketStorage> *CommandStreamReceiver::getTimestampPacketAllocator() {
    if (timestampPacketAllocator.get() == nullptr) {
        timestampPacketAllocator = std::make_unique<TagAllocator<TimestampPacketStorage>>(rootDeviceIndex, getMemoryManager(), getPreferredTagPoolSize(), MemoryConstants::cacheLineSize);
    }
    return timestampPacketAllocator.get();
}

cl_int CommandStreamReceiver::expectMemory(const void *gfxAddress, const void *srcAddress,
                                           size_t length, uint32_t compareOperation) {
    auto isMemoryEqual = (memcmp(gfxAddress, srcAddress, length) == 0);
    auto isEqualMemoryExpected = (compareOperation == CmdServicesMemTraceMemoryCompare::CompareOperationValues::CompareEqual);

    return (isMemoryEqual == isEqualMemoryExpected) ? CL_SUCCESS : CL_INVALID_VALUE;
}

} // namespace NEO
