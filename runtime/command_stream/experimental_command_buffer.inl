/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/command_stream/command_stream_receiver_hw.h"
#include "runtime/command_stream/experimental_command_buffer.h"
#include "runtime/command_stream/linear_stream.h"
#include "runtime/helpers/hw_helper.h"
#include "runtime/memory_manager/graphics_allocation.h"

namespace NEO {

template <typename GfxFamily>
void ExperimentalCommandBuffer::injectBufferStart(LinearStream &parentStream, size_t cmdBufferOffset) {
    using MI_BATCH_BUFFER_START = typename GfxFamily::MI_BATCH_BUFFER_START;
    auto pCmd = parentStream.getSpaceForCmd<MI_BATCH_BUFFER_START>();
    auto commandStreamReceiverHw = static_cast<CommandStreamReceiverHw<GfxFamily> *>(commandStreamReceiver);
    commandStreamReceiverHw->addBatchBufferStart(pCmd, currentStream->getGraphicsAllocation()->getGpuAddress() + cmdBufferOffset, true);
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::getRequiredInjectionSize() noexcept {
    using MI_BATCH_BUFFER_START = typename GfxFamily::MI_BATCH_BUFFER_START;
    return sizeof(MI_BATCH_BUFFER_START);
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::programExperimentalCommandBuffer() {
    using MI_BATCH_BUFFER_END = typename GfxFamily::MI_BATCH_BUFFER_END;

    getCS(getTotalExperimentalSize<GfxFamily>());

    size_t returnOffset = currentStream->getUsed();

    //begin timestamp
    addTimeStampPipeControl<GfxFamily>();

    addExperimentalCommands<GfxFamily>();

    //end timestamp
    addTimeStampPipeControl<GfxFamily>();

    //end
    auto pCmd = currentStream->getSpaceForCmd<MI_BATCH_BUFFER_END>();
    *pCmd = GfxFamily::cmdInitBatchBufferEnd;

    return returnOffset;
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::getTotalExperimentalSize() noexcept {
    using MI_BATCH_BUFFER_END = typename GfxFamily::MI_BATCH_BUFFER_END;

    size_t size = sizeof(MI_BATCH_BUFFER_END) + getTimeStampPipeControlSize<GfxFamily>() + getExperimentalCommandsSize<GfxFamily>();
    return size;
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::getTimeStampPipeControlSize() noexcept {
    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;

    // Two P_C for timestamps
    return 2 * PipeControlHelper<GfxFamily>::getSizeForPipeControlWithPostSyncOperation();
}

template <typename GfxFamily>
void ExperimentalCommandBuffer::addTimeStampPipeControl() {
    using PIPE_CONTROL = typename GfxFamily::PIPE_CONTROL;

    uint64_t timeStampAddress = timestamps->getGpuAddress() + timestampsOffset;

    PipeControlHelper<GfxFamily>::obtainPipeControlAndProgramPostSyncOperation(*currentStream,
                                                                               PIPE_CONTROL::POST_SYNC_OPERATION_WRITE_TIMESTAMP, timeStampAddress, 0llu, false);

    //moving to next chunk
    timestampsOffset += sizeof(uint64_t);

    DEBUG_BREAK_IF(timestamps->getUnderlyingBufferSize() < timestampsOffset);
}

template <typename GfxFamily>
void ExperimentalCommandBuffer::addExperimentalCommands() {
    using MI_SEMAPHORE_WAIT = typename GfxFamily::MI_SEMAPHORE_WAIT;

    uint32_t *semaphoreData = reinterpret_cast<uint32_t *>(ptrOffset(experimentalAllocation->getUnderlyingBuffer(), experimentalAllocationOffset));
    *semaphoreData = 1;
    uint64_t gpuAddr = experimentalAllocation->getGpuAddress() + experimentalAllocationOffset;

    auto semaphoreCmd = currentStream->getSpaceForCmd<MI_SEMAPHORE_WAIT>();
    *semaphoreCmd = GfxFamily::cmdInitMiSemaphoreWait;
    semaphoreCmd->setCompareOperation(MI_SEMAPHORE_WAIT::COMPARE_OPERATION_SAD_EQUAL_SDD);
    semaphoreCmd->setSemaphoreDataDword(*semaphoreData);
    semaphoreCmd->setSemaphoreGraphicsAddress(gpuAddr);
}

template <typename GfxFamily>
size_t ExperimentalCommandBuffer::getExperimentalCommandsSize() noexcept {
    using MI_SEMAPHORE_WAIT = typename GfxFamily::MI_SEMAPHORE_WAIT;
    return sizeof(MI_SEMAPHORE_WAIT);
}

} // namespace NEO
