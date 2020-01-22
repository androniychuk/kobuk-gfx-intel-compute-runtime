/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

// Need to suppress warining 4005 caused by hw_cmds.h and wddm.h order.
// Current order must be preserved due to two versions of igfxfmid.h
#pragma warning(push)
#pragma warning(disable : 4005)
#include "core/command_stream/linear_stream.h"
#include "core/command_stream/preemption.h"
#include "core/gmm_helper/page_table_mngr.h"
#include "core/helpers/hw_cmds.h"
#include "core/helpers/ptr_math.h"
#include "runtime/device/device.h"
#include "runtime/helpers/flush_stamp.h"
#include "runtime/helpers/windows/gmm_callbacks.h"
#include "runtime/mem_obj/mem_obj.h"
#include "runtime/os_interface/windows/wddm/wddm.h"
#include "runtime/os_interface/windows/wddm_device_command_stream.h"
#pragma warning(pop)

#include "runtime/os_interface/windows/gdi_interface.h"
#include "runtime/os_interface/windows/os_context_win.h"
#include "runtime/os_interface/windows/os_interface.h"
#include "runtime/os_interface/windows/wddm_memory_manager.h"

namespace NEO {

// Initialize COMMAND_BUFFER_HEADER         Type PatchList  Streamer Perf Tag
DECLARE_COMMAND_BUFFER(CommandBufferHeader, UMD_OCL, FALSE, FALSE, PERFTAG_OCL);

template <typename GfxFamily>
WddmCommandStreamReceiver<GfxFamily>::WddmCommandStreamReceiver(ExecutionEnvironment &executionEnvironment, uint32_t rootDeviceIndex)
    : BaseClass(executionEnvironment, rootDeviceIndex) {

    notifyAubCaptureImpl = DeviceCallbacks<GfxFamily>::notifyAubCapture;
    this->wddm = executionEnvironment.osInterface->get()->getWddm();
    this->osInterface = executionEnvironment.osInterface.get();

    PreemptionMode preemptionMode = PreemptionHelper::getDefaultPreemptionMode(peekHwInfo());

    commandBufferHeader = new COMMAND_BUFFER_HEADER;
    *commandBufferHeader = CommandBufferHeader;

    if (preemptionMode != PreemptionMode::Disabled) {
        commandBufferHeader->NeedsMidBatchPreEmptionSupport = true;
    }

    this->dispatchMode = DispatchMode::BatchedDispatch;

    if (DebugManager.flags.CsrDispatchMode.get()) {
        this->dispatchMode = (DispatchMode)DebugManager.flags.CsrDispatchMode.get();
    }
}

template <typename GfxFamily>
WddmCommandStreamReceiver<GfxFamily>::~WddmCommandStreamReceiver() {
    if (commandBufferHeader)
        delete commandBufferHeader;
}

template <typename GfxFamily>
bool WddmCommandStreamReceiver<GfxFamily>::flush(BatchBuffer &batchBuffer, ResidencyContainer &allocationsForResidency) {
    auto commandStreamAddress = ptrOffset(batchBuffer.commandBufferAllocation->getGpuAddress(), batchBuffer.startOffset);

    if (this->dispatchMode == DispatchMode::ImmediateDispatch) {
        makeResident(*batchBuffer.commandBufferAllocation);
    } else {
        allocationsForResidency.push_back(batchBuffer.commandBufferAllocation);
        batchBuffer.commandBufferAllocation->updateResidencyTaskCount(this->taskCount, this->osContext->getContextId());
    }

    this->processResidency(allocationsForResidency);

    COMMAND_BUFFER_HEADER *pHeader = reinterpret_cast<COMMAND_BUFFER_HEADER *>(commandBufferHeader);
    pHeader->RequiresCoherency = batchBuffer.requiresCoherency;

    pHeader->UmdRequestedSliceState = 0;
    pHeader->UmdRequestedEUCount = wddm->getGtSysInfo()->EUCount / wddm->getGtSysInfo()->SubSliceCount;

    const uint32_t maxRequestedSubsliceCount = 7;
    switch (batchBuffer.throttle) {
    case QueueThrottle::LOW:
    case QueueThrottle::MEDIUM:
        pHeader->UmdRequestedSubsliceCount = 0;
        break;
    case QueueThrottle::HIGH:
        pHeader->UmdRequestedSubsliceCount = (wddm->getGtSysInfo()->SubSliceCount <= maxRequestedSubsliceCount) ? wddm->getGtSysInfo()->SubSliceCount : 0;
        break;
    }

    if (wddm->isKmDafEnabled()) {
        this->kmDafLockAllocations(allocationsForResidency);
    }

    auto osContextWin = static_cast<OsContextWin *>(osContext);
    WddmSubmitArguments submitArgs = {};
    submitArgs.contextHandle = osContextWin->getWddmContextHandle();
    submitArgs.hwQueueHandle = osContextWin->getHwQueue().handle;
    submitArgs.monitorFence = &osContextWin->getResidencyController().getMonitoredFence();
    auto status = wddm->submit(commandStreamAddress, batchBuffer.usedSize - batchBuffer.startOffset, commandBufferHeader, submitArgs);

    flushStamp->setStamp(submitArgs.monitorFence->lastSubmittedFence);
    return status;
}

template <typename GfxFamily>
void WddmCommandStreamReceiver<GfxFamily>::processResidency(const ResidencyContainer &allocationsForResidency) {
    bool success = static_cast<OsContextWin *>(osContext)->getResidencyController().makeResidentResidencyAllocations(allocationsForResidency);
    DEBUG_BREAK_IF(!success);
}

template <typename GfxFamily>
void WddmCommandStreamReceiver<GfxFamily>::processEviction() {
    static_cast<OsContextWin *>(osContext)->getResidencyController().makeNonResidentEvictionAllocations(this->getEvictionAllocations());
    this->getEvictionAllocations().clear();
}

template <typename GfxFamily>
WddmMemoryManager *WddmCommandStreamReceiver<GfxFamily>::getMemoryManager() const {
    return static_cast<WddmMemoryManager *>(CommandStreamReceiver::getMemoryManager());
}

template <typename GfxFamily>
bool WddmCommandStreamReceiver<GfxFamily>::waitForFlushStamp(FlushStamp &flushStampToWait) {
    return wddm->waitFromCpu(flushStampToWait, static_cast<OsContextWin *>(osContext)->getResidencyController().getMonitoredFence());
}

template <typename GfxFamily>
GmmPageTableMngr *WddmCommandStreamReceiver<GfxFamily>::createPageTableManager() {
    GMM_TRANSLATIONTABLE_CALLBACKS ttCallbacks = {};
    ttCallbacks.pfWriteL3Adr = TTCallbacks<GfxFamily>::writeL3Address;

    GmmPageTableMngr *gmmPageTableMngr = GmmPageTableMngr::create(TT_TYPE::AUXTT, &ttCallbacks);
    gmmPageTableMngr->setCsrHandle(this);
    this->executionEnvironment.rootDeviceEnvironments[this->rootDeviceIndex]->pageTableManager.reset(gmmPageTableMngr);
    return gmmPageTableMngr;
}

template <typename GfxFamily>
void WddmCommandStreamReceiver<GfxFamily>::kmDafLockAllocations(ResidencyContainer &allocationsForResidency) {
    for (auto &graphicsAllocation : allocationsForResidency) {
        if ((GraphicsAllocation::AllocationType::LINEAR_STREAM == graphicsAllocation->getAllocationType()) ||
            (GraphicsAllocation::AllocationType::FILL_PATTERN == graphicsAllocation->getAllocationType()) ||
            (GraphicsAllocation::AllocationType::COMMAND_BUFFER == graphicsAllocation->getAllocationType())) {
            wddm->kmDafLock(static_cast<WddmAllocation *>(graphicsAllocation)->getDefaultHandle());
        }
    }
}
} // namespace NEO
