/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "core/utilities/range.h"
#include "runtime/built_ins/built_ins.h"
#include "runtime/built_ins/builtins_dispatch_builder.h"
#include "runtime/builtin_kernels_simulation/scheduler_simulation.h"
#include "runtime/command_queue/command_queue_hw.h"
#include "runtime/command_queue/gpgpu_walker.h"
#include "runtime/command_queue/hardware_interface.h"
#include "runtime/command_stream/command_stream_receiver.h"
#include "runtime/event/event_builder.h"
#include "runtime/event/user_event.h"
#include "runtime/gtpin/gtpin_notify.h"
#include "runtime/helpers/array_count.h"
#include "runtime/helpers/dispatch_info_builder.h"
#include "runtime/helpers/hardware_commands_helper.h"
#include "runtime/helpers/options.h"
#include "runtime/helpers/task_information.h"
#include "runtime/mem_obj/buffer.h"
#include "runtime/mem_obj/image.h"
#include "runtime/memory_manager/internal_allocation_storage.h"
#include "runtime/memory_manager/memory_manager.h"
#include "runtime/memory_manager/surface.h"
#include "runtime/os_interface/os_context.h"
#include "runtime/program/block_kernel_manager.h"
#include "runtime/program/printf_handler.h"
#include "runtime/utilities/tag_allocator.h"

#include "hw_cmds.h"

#include <algorithm>
#include <new>

namespace NEO {

template <typename GfxFamily>
template <uint32_t commandType, size_t surfaceCount>
void CommandQueueHw<GfxFamily>::enqueueHandler(Surface *(&surfaces)[surfaceCount],
                                               bool blocking,
                                               Kernel *kernel,
                                               cl_uint workDim,
                                               const size_t globalOffsets[3],
                                               const size_t workItems[3],
                                               const size_t *localWorkSizesIn,
                                               const size_t *enqueuedWorkSizes,
                                               cl_uint numEventsInWaitList,
                                               const cl_event *eventWaitList,
                                               cl_event *event) {
    BuiltInOwnershipWrapper builtInLock;
    MultiDispatchInfo multiDispatchInfo(kernel);

    if (DebugManager.flags.ForceDispatchScheduler.get()) {
        forceDispatchScheduler(multiDispatchInfo);
    } else {
        MemObjsForAuxTranslation memObjsForAuxTranslation;
        if (kernel->isAuxTranslationRequired()) {
            auto &builder = getDevice().getExecutionEnvironment()->getBuiltIns()->getBuiltinDispatchInfoBuilder(EBuiltInOps::AuxTranslation, getContext(), getDevice());
            builtInLock.takeOwnership(builder, this->context);
            kernel->fillWithBuffersForAuxTranslation(memObjsForAuxTranslation);
            if (!memObjsForAuxTranslation.empty()) {
                dispatchAuxTranslation(multiDispatchInfo, memObjsForAuxTranslation, AuxTranslationDirection::AuxToNonAux);
            }
        }

        if (kernel->getKernelInfo().builtinDispatchBuilder == nullptr) {
            DispatchInfoBuilder<SplitDispatch::Dim::d3D, SplitDispatch::SplitMode::WalkerSplit> builder;
            builder.setDispatchGeometry(workDim, workItems, enqueuedWorkSizes, globalOffsets, Vec3<size_t>{0, 0, 0}, localWorkSizesIn);
            builder.setKernel(kernel);
            builder.bake(multiDispatchInfo);
        } else {
            auto builder = kernel->getKernelInfo().builtinDispatchBuilder;
            builder->buildDispatchInfos(multiDispatchInfo, kernel, workDim, workItems, enqueuedWorkSizes, globalOffsets);

            if (multiDispatchInfo.size() == 0) {
                return;
            }
        }
        if (kernel->isAuxTranslationRequired()) {
            if (!memObjsForAuxTranslation.empty()) {
                UNRECOVERABLE_IF(kernel->isParentKernel);
                dispatchAuxTranslation(multiDispatchInfo, memObjsForAuxTranslation, AuxTranslationDirection::NonAuxToAux);
            }
        }
    }

    enqueueHandler<commandType>(surfaces, blocking, multiDispatchInfo, numEventsInWaitList, eventWaitList, event);
}

template <typename GfxFamily>
void CommandQueueHw<GfxFamily>::forceDispatchScheduler(NEO::MultiDispatchInfo &multiDispatchInfo) {
    BuiltIns &builtIns = *getDevice().getExecutionEnvironment()->getBuiltIns();
    SchedulerKernel &scheduler = builtIns.getSchedulerKernel(this->getContext());
    DispatchInfo dispatchInfo(&scheduler, 1, Vec3<size_t>(scheduler.getGws(), 1, 1), Vec3<size_t>(scheduler.getLws(), 1, 1), Vec3<size_t>(0, 0, 0));

    auto devQueue = this->getContext().getDefaultDeviceQueue();
    DeviceQueueHw<GfxFamily> *devQueueHw = castToObject<DeviceQueueHw<GfxFamily>>(devQueue);

    scheduler.createReflectionSurface();
    GraphicsAllocation *reflectionSurface = scheduler.getKernelReflectionSurface();

    devQueueHw->resetDeviceQueue();

    scheduler.setArgs(devQueueHw->getQueueBuffer(),
                      devQueueHw->getStackBuffer(),
                      devQueueHw->getEventPoolBuffer(),
                      devQueueHw->getSlbBuffer(),
                      devQueueHw->getDshBuffer(),
                      reflectionSurface,
                      devQueueHw->getQueueStorageBuffer(),
                      this->getIndirectHeap(IndirectHeap::SURFACE_STATE, 0u).getGraphicsAllocation());

    multiDispatchInfo.push(dispatchInfo);
}

template <typename GfxFamily>
template <uint32_t commandType>
void CommandQueueHw<GfxFamily>::enqueueHandler(Surface **surfacesForResidency,
                                               size_t numSurfaceForResidency,
                                               bool blocking,
                                               const MultiDispatchInfo &multiDispatchInfo,
                                               cl_uint numEventsInWaitList,
                                               const cl_event *eventWaitList,
                                               cl_event *event) {
    if (multiDispatchInfo.empty() && !isCommandWithoutKernel(commandType)) {
        enqueueHandler<CL_COMMAND_MARKER>(surfacesForResidency, numSurfaceForResidency, blocking, multiDispatchInfo,
                                          numEventsInWaitList, eventWaitList, event);
        if (event) {
            castToObjectOrAbort<Event>(*event)->setCmdType(commandType);
        }
        return;
    }

    Kernel *parentKernel = multiDispatchInfo.peekParentKernel();
    auto devQueue = this->getContext().getDefaultDeviceQueue();
    DeviceQueueHw<GfxFamily> *devQueueHw = castToObject<DeviceQueueHw<GfxFamily>>(devQueue);
    auto clearAllDependencies = queueDependenciesClearRequired();

    TagNode<HwTimeStamps> *hwTimeStamps = nullptr;

    auto commandStreamRecieverOwnership = getGpgpuCommandStreamReceiver().obtainUniqueOwnership();

    TimeStampData queueTimeStamp;
    if (isProfilingEnabled() && event) {
        this->getDevice().getOSTime()->getCpuGpuTime(&queueTimeStamp);
    }
    EventBuilder eventBuilder;
    if (event) {
        eventBuilder.create<Event>(this, commandType, Event::eventNotReady, 0);
        *event = eventBuilder.getEvent();
        if (eventBuilder.getEvent()->isProfilingEnabled()) {
            eventBuilder.getEvent()->setQueueTimeStamp(&queueTimeStamp);
            if (isCommandWithoutKernel(commandType)) {
                eventBuilder.getEvent()->setCPUProfilingPath(true);
                eventBuilder.getEvent()->setQueueTimeStamp();
            }
        }
        DBG_LOG(EventsDebugEnable, "enqueueHandler commandType", commandType, "output Event", eventBuilder.getEvent());
    }

    std::unique_ptr<KernelOperation> blockedCommandsData;
    std::unique_ptr<PrintfHandler> printfHandler;
    TakeOwnershipWrapper<CommandQueueHw<GfxFamily>> queueOwnership(*this);

    auto blockQueue = false;
    auto taskLevel = 0u;
    obtainTaskLevelAndBlockedStatus(taskLevel, numEventsInWaitList, eventWaitList, blockQueue, commandType);
    bool blitEnqueue = blitEnqueueAllowed(commandType);

    DBG_LOG(EventsDebugEnable, "blockQueue", blockQueue, "virtualEvent", virtualEvent, "taskLevel", taskLevel);

    if (parentKernel && !blockQueue) {
        while (!devQueueHw->isEMCriticalSectionFree())
            ;
    }

    enqueueHandlerHook(commandType, multiDispatchInfo);

    aubCaptureHook(blocking, clearAllDependencies, multiDispatchInfo);

    if (DebugManager.flags.MakeEachEnqueueBlocking.get()) {
        blocking = true;
    }

    TimestampPacketContainer previousTimestampPacketNodes;
    EventsRequest eventsRequest(numEventsInWaitList, eventWaitList, event);
    CsrDependencies csrDeps;
    BlitProperties blitProperties;

    if (getGpgpuCommandStreamReceiver().peekTimestampPacketWriteEnabled()) {
        csrDeps.fillFromEventsRequest(eventsRequest, getGpgpuCommandStreamReceiver(), CsrDependencies::DependenciesType::OnCsr);

        size_t nodesCount = 0u;
        if (blitEnqueue || isCacheFlushCommand(commandType)) {
            nodesCount = 1;
        } else if (!multiDispatchInfo.empty()) {
            nodesCount = estimateTimestampPacketNodesCount(multiDispatchInfo);
        }

        if (nodesCount > 0) {
            obtainNewTimestampPacketNodes(nodesCount, previousTimestampPacketNodes, clearAllDependencies);
            csrDeps.push_back(&previousTimestampPacketNodes);
        }
    }

    auto &commandStream = *obtainCommandStream<commandType>(csrDeps, blitEnqueue, blockQueue, multiDispatchInfo, eventsRequest,
                                                            blockedCommandsData, surfacesForResidency, numSurfaceForResidency);
    auto commandStreamStart = commandStream.getUsed();

    if (eventBuilder.getEvent() && getGpgpuCommandStreamReceiver().peekTimestampPacketWriteEnabled()) {
        eventBuilder.getEvent()->addTimestampPacketNodes(*timestampPacketContainer);
    }

    bool flushDependenciesForNonKernelCommand = false;

    if (blitEnqueue) {
        blitProperties = processDispatchForBlitEnqueue(multiDispatchInfo, previousTimestampPacketNodes, eventsRequest, commandStream,
                                                       commandType, blockQueue);
    } else if (multiDispatchInfo.empty() == false) {
        processDispatchForKernels<commandType>(multiDispatchInfo, printfHandler, eventBuilder.getEvent(),
                                               hwTimeStamps, blockQueue, devQueueHw, csrDeps, blockedCommandsData.get(),
                                               previousTimestampPacketNodes);
    } else if (isCacheFlushCommand(commandType)) {
        processDispatchForCacheFlush(surfacesForResidency, numSurfaceForResidency, &commandStream, csrDeps);
    } else if (getGpgpuCommandStreamReceiver().peekTimestampPacketWriteEnabled()) {
        if (CL_COMMAND_BARRIER == commandType) {
            getGpgpuCommandStreamReceiver().requestStallingPipeControlOnNextFlush();
        }

        for (size_t i = 0; i < eventsRequest.numEventsInWaitList; i++) {
            auto waitlistEvent = castToObjectOrAbort<Event>(eventsRequest.eventWaitList[i]);
            if (waitlistEvent->getTimestampPacketNodes()) {
                flushDependenciesForNonKernelCommand = true;
                if (eventBuilder.getEvent()) {
                    eventBuilder.getEvent()->addTimestampPacketNodes(*waitlistEvent->getTimestampPacketNodes());
                }
            }
        }
        if (flushDependenciesForNonKernelCommand) {
            TimestampPacketHelper::programCsrDependencies<GfxFamily>(commandStream, csrDeps);
        }
    }

    CompletionStamp completionStamp = {Event::eventNotReady, taskLevel, 0};

    const EnqueueProperties enqueueProperties(blitEnqueue, !multiDispatchInfo.empty(), isCacheFlushCommand(commandType),
                                              flushDependenciesForNonKernelCommand, &blitProperties);

    if (!blockQueue) {
        csrDeps.makeResident(getGpgpuCommandStreamReceiver());

        if (parentKernel) {
            processDeviceEnqueue(devQueueHw, multiDispatchInfo, hwTimeStamps, blocking);
        }

        if (enqueueProperties.operation == EnqueueProperties::Operation::GpuKernel) {
            completionStamp = enqueueNonBlocked<commandType>(
                surfacesForResidency,
                numSurfaceForResidency,
                commandStream,
                commandStreamStart,
                blocking,
                multiDispatchInfo,
                &previousTimestampPacketNodes,
                eventsRequest,
                eventBuilder,
                taskLevel,
                printfHandler.get());

            if (parentKernel) {
                getGpgpuCommandStreamReceiver().setMediaVFEStateDirty(true);

                if (devQueueHw->getSchedulerReturnInstance() > 0) {
                    waitUntilComplete(completionStamp.taskCount, completionStamp.flushStamp, false);

                    BuiltinKernelsSimulation::SchedulerSimulation<GfxFamily> simulation;
                    simulation.runSchedulerSimulation(devQueueHw->getQueueBuffer(),
                                                      devQueueHw->getStackBuffer(),
                                                      devQueueHw->getEventPoolBuffer(),
                                                      devQueueHw->getSlbBuffer(),
                                                      devQueueHw->getDshBuffer(),
                                                      parentKernel->getKernelReflectionSurface(),
                                                      devQueueHw->getQueueStorageBuffer(),
                                                      this->getIndirectHeap(IndirectHeap::SURFACE_STATE, 0u).getGraphicsAllocation(),
                                                      devQueueHw->getDebugQueue());
                }
            }
        } else if (enqueueProperties.isFlushWithoutKernelRequired()) {
            completionStamp = enqueueCommandWithoutKernel(
                surfacesForResidency,
                numSurfaceForResidency,
                commandStream,
                commandStreamStart,
                blocking,
                enqueueProperties,
                &previousTimestampPacketNodes,
                eventsRequest,
                eventBuilder,
                taskLevel);
        } else {
            UNRECOVERABLE_IF(enqueueProperties.operation != EnqueueProperties::Operation::EnqueueWithoutSubmission);
            auto maxTaskCount = this->taskCount;
            for (auto eventId = 0u; eventId < numEventsInWaitList; eventId++) {
                auto event = castToObject<Event>(eventWaitList[eventId]);
                if (!event->isUserEvent() && !event->isExternallySynchronized()) {
                    maxTaskCount = std::max(maxTaskCount, event->peekTaskCount());
                }
            }

            //inherit data from event_wait_list and previous packets
            completionStamp.flushStamp = this->flushStamp->peekStamp();
            completionStamp.taskCount = maxTaskCount;
            completionStamp.taskLevel = taskLevel;

            if (eventBuilder.getEvent() && isProfilingEnabled()) {
                TimeStampData submitTimeStamp;
                this->getDevice().getOSTime()->getCpuGpuTime(&submitTimeStamp);
                eventBuilder.getEvent()->setSubmitTimeStamp(&submitTimeStamp);
                eventBuilder.getEvent()->setSubmitTimeStamp();
                eventBuilder.getEvent()->setStartTimeStamp();
            }
        }
        if (eventBuilder.getEvent()) {
            eventBuilder.getEvent()->flushStamp->replaceStampObject(this->flushStamp->getStampReference());
        }
    }
    updateFromCompletionStamp(completionStamp);

    if (eventBuilder.getEvent()) {
        eventBuilder.getEvent()->updateCompletionStamp(completionStamp.taskCount, completionStamp.taskLevel, completionStamp.flushStamp);
        DebugManager.log(DebugManager.flags.EventsDebugEnable.get(), "updateCompletionStamp Event", eventBuilder.getEvent(), "taskLevel", eventBuilder.getEvent()->taskLevel.load());
    }

    if (blockQueue) {
        if (parentKernel) {
            size_t minSizeSSHForEM = HardwareCommandsHelper<GfxFamily>::getSizeRequiredForExecutionModel(IndirectHeap::SURFACE_STATE, *parentKernel);
            blockedCommandsData->surfaceStateHeapSizeEM = minSizeSSHForEM;
        }

        enqueueBlocked(commandType,
                       surfacesForResidency,
                       numSurfaceForResidency,
                       multiDispatchInfo,
                       &previousTimestampPacketNodes,
                       blockedCommandsData,
                       enqueueProperties,
                       eventsRequest,
                       eventBuilder,
                       std::move(printfHandler));
    }

    queueOwnership.unlock();
    commandStreamRecieverOwnership.unlock();

    if (blocking) {
        if (blockQueue) {
            while (isQueueBlocked())
                ;
            waitUntilComplete(taskCount, flushStamp->peekStamp(), false);
        } else {
            waitUntilComplete(taskCount, flushStamp->peekStamp(), false);
            if (printfHandler) {
                printfHandler->printEnqueueOutput();
            }
        }
    }
}

template <typename GfxFamily>
template <uint32_t commandType>
void CommandQueueHw<GfxFamily>::processDispatchForKernels(const MultiDispatchInfo &multiDispatchInfo,
                                                          std::unique_ptr<PrintfHandler> &printfHandler,
                                                          Event *event,
                                                          TagNode<HwTimeStamps> *&hwTimeStamps,
                                                          bool blockQueue,
                                                          DeviceQueueHw<GfxFamily> *devQueueHw,
                                                          CsrDependencies &csrDeps,
                                                          KernelOperation *blockedCommandsData,
                                                          TimestampPacketContainer &previousTimestampPacketNodes) {
    TagNode<HwPerfCounter> *hwPerfCounter = nullptr;
    DebugManager.dumpKernelArgs(&multiDispatchInfo);

    printfHandler.reset(PrintfHandler::create(multiDispatchInfo, *device));
    if (printfHandler) {
        printfHandler->prepareDispatch(multiDispatchInfo);
    }

    if (commandType == CL_COMMAND_NDRANGE_KERNEL) {
        if (multiDispatchInfo.peekMainKernel()->getProgram()->isKernelDebugEnabled()) {
            setupDebugSurface(multiDispatchInfo.peekMainKernel());
        }
    }

    if (event && this->isProfilingEnabled()) {
        // Get allocation for timestamps
        hwTimeStamps = event->getHwTimeStampNode();
    }

    if (auto parentKernel = multiDispatchInfo.peekParentKernel()) {
        parentKernel->createReflectionSurface();
        parentKernel->patchDefaultDeviceQueue(context->getDefaultDeviceQueue());
        parentKernel->patchEventPool(context->getDefaultDeviceQueue());
        parentKernel->patchReflectionSurface(context->getDefaultDeviceQueue(), printfHandler.get());
        if (!blockQueue) {
            devQueueHw->resetDeviceQueue();
            devQueueHw->acquireEMCriticalSection();
        }
    }

    if (event && this->isPerfCountersEnabled()) {
        hwPerfCounter = event->getHwPerfCounterNode();
    }

    HardwareInterface<GfxFamily>::dispatchWalker(
        *this,
        multiDispatchInfo,
        csrDeps,
        blockedCommandsData,
        hwTimeStamps,
        hwPerfCounter,
        &previousTimestampPacketNodes,
        timestampPacketContainer.get(),
        commandType);

    if (DebugManager.flags.AddPatchInfoCommentsForAUBDump.get()) {
        for (auto &dispatchInfo : multiDispatchInfo) {
            for (auto &patchInfoData : dispatchInfo.getKernel()->getPatchInfoDataList()) {
                getGpgpuCommandStreamReceiver().getFlatBatchBufferHelper().setPatchInfoData(patchInfoData);
            }
        }
    }

    getGpgpuCommandStreamReceiver().setRequiredScratchSizes(multiDispatchInfo.getRequiredScratchSize(), multiDispatchInfo.getRequiredPrivateScratchSize());
}

template <typename GfxFamily>
BlitProperties CommandQueueHw<GfxFamily>::processDispatchForBlitEnqueue(const MultiDispatchInfo &multiDispatchInfo,
                                                                        TimestampPacketContainer &previousTimestampPacketNodes,
                                                                        const EventsRequest &eventsRequest, LinearStream &commandStream,
                                                                        uint32_t commandType, bool queueBlocked) {
    auto blitDirection = BlitProperties::obtainBlitDirection(commandType);

    auto blitCommandStreamReceiver = getBcsCommandStreamReceiver();

    auto blitProperties = BlitProperties::constructPropertiesForReadWriteBuffer(blitDirection, *blitCommandStreamReceiver,
                                                                                multiDispatchInfo.peekBuiltinOpParams(), false);
    if (!queueBlocked) {
        blitProperties.csrDependencies.fillFromEventsRequest(eventsRequest, *blitCommandStreamReceiver,
                                                             CsrDependencies::DependenciesType::All);

        blitProperties.csrDependencies.push_back(&previousTimestampPacketNodes);
    }

    blitProperties.outputTimestampPacket = timestampPacketContainer.get();

    auto currentTimestampPacketNode = timestampPacketContainer->peekNodes().at(0);
    TimestampPacketHelper::programSemaphoreWithImplicitDependency<GfxFamily>(commandStream, *currentTimestampPacketNode);

    return blitProperties;
}

template <typename GfxFamily>
void CommandQueueHw<GfxFamily>::processDispatchForCacheFlush(Surface **surfaces,
                                                             size_t numSurfaces,
                                                             LinearStream *commandStream,
                                                             CsrDependencies &csrDeps) {

    TimestampPacketHelper::programCsrDependencies<GfxFamily>(*commandStream, csrDeps);

    uint64_t postSyncAddress = 0;
    if (getGpgpuCommandStreamReceiver().peekTimestampPacketWriteEnabled()) {
        auto timestampPacketNodeForPostSync = timestampPacketContainer->peekNodes().at(0);
        postSyncAddress = timestampPacketNodeForPostSync->getGpuAddress() + offsetof(TimestampPacketStorage, packets[0].contextEnd);
    }

    submitCacheFlush(surfaces, numSurfaces, commandStream, postSyncAddress);
}

template <typename GfxFamily>
void CommandQueueHw<GfxFamily>::processDeviceEnqueue(DeviceQueueHw<GfxFamily> *devQueueHw,
                                                     const MultiDispatchInfo &multiDispatchInfo,
                                                     TagNode<HwTimeStamps> *hwTimeStamps,
                                                     bool &blocking) {
    auto parentKernel = multiDispatchInfo.peekParentKernel();
    size_t minSizeSSHForEM = HardwareCommandsHelper<GfxFamily>::getSizeRequiredForExecutionModel(IndirectHeap::SURFACE_STATE, *parentKernel);

    uint32_t taskCount = getGpgpuCommandStreamReceiver().peekTaskCount() + 1;
    devQueueHw->setupExecutionModelDispatch(getIndirectHeap(IndirectHeap::SURFACE_STATE, minSizeSSHForEM),
                                            *devQueueHw->getIndirectHeap(IndirectHeap::DYNAMIC_STATE),
                                            parentKernel,
                                            (uint32_t)multiDispatchInfo.size(),
                                            getGpgpuCommandStreamReceiver().getTagAllocation()->getGpuAddress(),
                                            taskCount,
                                            hwTimeStamps);

    BuiltIns &builtIns = *getDevice().getExecutionEnvironment()->getBuiltIns();
    SchedulerKernel &scheduler = builtIns.getSchedulerKernel(this->getContext());

    scheduler.setArgs(devQueueHw->getQueueBuffer(),
                      devQueueHw->getStackBuffer(),
                      devQueueHw->getEventPoolBuffer(),
                      devQueueHw->getSlbBuffer(),
                      devQueueHw->getDshBuffer(),
                      parentKernel->getKernelReflectionSurface(),
                      devQueueHw->getQueueStorageBuffer(),
                      this->getIndirectHeap(IndirectHeap::SURFACE_STATE, 0u).getGraphicsAllocation(),
                      devQueueHw->getDebugQueue());

    auto preemptionMode = PreemptionHelper::taskPreemptionMode(*device, multiDispatchInfo);
    GpgpuWalkerHelper<GfxFamily>::dispatchScheduler(
        *this->commandStream,
        *devQueueHw,
        preemptionMode,
        scheduler,
        &getIndirectHeap(IndirectHeap::SURFACE_STATE, 0u),
        devQueueHw->getIndirectHeap(IndirectHeap::DYNAMIC_STATE));

    scheduler.makeResident(getGpgpuCommandStreamReceiver());

    parentKernel->getProgram()->getBlockKernelManager()->makeInternalAllocationsResident(getGpgpuCommandStreamReceiver());

    if (parentKernel->isAuxTranslationRequired()) {
        blocking = true;
    }
}

template <typename GfxFamily>
void CommandQueueHw<GfxFamily>::obtainTaskLevelAndBlockedStatus(unsigned int &taskLevel, cl_uint &numEventsInWaitList, const cl_event *&eventWaitList, bool &blockQueueStatus, unsigned int commandType) {
    auto isQueueBlockedStatus = isQueueBlocked();
    taskLevel = getTaskLevelFromWaitList(this->taskLevel, numEventsInWaitList, eventWaitList);
    blockQueueStatus = (taskLevel == Event::eventNotReady) || isQueueBlockedStatus;

    auto taskLevelUpdateRequired = isTaskLevelUpdateRequired(taskLevel, eventWaitList, numEventsInWaitList, commandType);
    if (taskLevelUpdateRequired) {
        taskLevel++;
        this->taskLevel = taskLevel;
    }
}

template <typename GfxFamily>
bool CommandQueueHw<GfxFamily>::isTaskLevelUpdateRequired(const uint32_t &taskLevel, const cl_event *eventWaitList, const cl_uint &numEventsInWaitList, unsigned int commandType) {
    bool updateTaskLevel = true;
    //if we are blocked by user event then no update
    if (taskLevel == Event::eventNotReady) {
        updateTaskLevel = false;
    }
    //if we are executing command without kernel then it will inherit state from
    //previous commands, barrier is exception
    if (isCommandWithoutKernel(commandType) && commandType != CL_COMMAND_BARRIER) {
        updateTaskLevel = false;
    }
    //ooq special cases starts here
    if (this->isOOQEnabled()) {
        //if no wait list and barrier , do not update task level
        if (eventWaitList == nullptr && commandType != CL_COMMAND_BARRIER) {
            updateTaskLevel = false;
        }
        //if we have waitlist then deduce task level from waitlist and check if it is higher then current task level of queue
        if (eventWaitList != nullptr) {
            auto taskLevelFromEvents = getTaskLevelFromWaitList(0, numEventsInWaitList, eventWaitList);
            taskLevelFromEvents++;
            if (taskLevelFromEvents <= this->taskLevel) {
                updateTaskLevel = false;
            }
        }
    }
    return updateTaskLevel;
}

template <typename GfxFamily>
template <uint32_t commandType>
CompletionStamp CommandQueueHw<GfxFamily>::enqueueNonBlocked(
    Surface **surfaces,
    size_t surfaceCount,
    LinearStream &commandStream,
    size_t commandStreamStart,
    bool &blocking,
    const MultiDispatchInfo &multiDispatchInfo,
    TimestampPacketContainer *previousTimestampPacketNodes,
    EventsRequest &eventsRequest,
    EventBuilder &eventBuilder,
    uint32_t taskLevel,
    PrintfHandler *printfHandler) {

    UNRECOVERABLE_IF(multiDispatchInfo.empty());

    auto implicitFlush = false;

    if (printfHandler) {
        blocking = true;
        printfHandler->makeResident(getGpgpuCommandStreamReceiver());
    }
    if (timestampPacketContainer) {
        timestampPacketContainer->makeResident(getGpgpuCommandStreamReceiver());
        previousTimestampPacketNodes->makeResident(getGpgpuCommandStreamReceiver());
    }

    bool anyUncacheableArgs = false;
    auto requiresCoherency = false;
    for (auto surface : CreateRange(surfaces, surfaceCount)) {
        surface->makeResident(getGpgpuCommandStreamReceiver());
        requiresCoherency |= surface->IsCoherent;
        if (!surface->allowsL3Caching()) {
            anyUncacheableArgs = true;
        }
    }

    auto mediaSamplerRequired = false;
    uint32_t numGrfRequired = GrfConfig::DefaultGrfNumber;
    auto specialPipelineSelectMode = false;
    Kernel *kernel = nullptr;

    for (auto &dispatchInfo : multiDispatchInfo) {
        if (kernel != dispatchInfo.getKernel()) {
            kernel = dispatchInfo.getKernel();
        } else {
            continue;
        }
        kernel->makeResident(getGpgpuCommandStreamReceiver());
        requiresCoherency |= kernel->requiresCoherency();
        mediaSamplerRequired |= kernel->isVmeKernel();
        auto numGrfRequiredByKernel = kernel->getKernelInfo().patchInfo.executionEnvironment->NumGRFRequired;
        numGrfRequired = std::max(numGrfRequired, numGrfRequiredByKernel);
        specialPipelineSelectMode |= kernel->requiresSpecialPipelineSelectMode();
        if (kernel->hasUncacheableStatelessArgs()) {
            anyUncacheableArgs = true;
        }
    }

    if (mediaSamplerRequired) {
        DEBUG_BREAK_IF(device->getDeviceInfo().preemptionSupported != false);
    }

    TimeStampData submitTimeStamp;
    if (isProfilingEnabled() && eventBuilder.getEvent()) {
        this->getDevice().getOSTime()->getCpuGpuTime(&submitTimeStamp);
        eventBuilder.getEvent()->setSubmitTimeStamp(&submitTimeStamp);
        getGpgpuCommandStreamReceiver().makeResident(*eventBuilder.getEvent()->getHwTimeStampNode()->getBaseGraphicsAllocation());
        if (isPerfCountersEnabled()) {
            getGpgpuCommandStreamReceiver().makeResident(*eventBuilder.getEvent()->getHwPerfCounterNode()->getBaseGraphicsAllocation());
        }
    }

    IndirectHeap *dsh = nullptr;
    IndirectHeap *ioh = nullptr;

    if (multiDispatchInfo.peekParentKernel()) {
        DeviceQueueHw<GfxFamily> *pDevQueue = castToObject<DeviceQueueHw<GfxFamily>>(this->getContext().getDefaultDeviceQueue());
        DEBUG_BREAK_IF(pDevQueue == nullptr);
        dsh = pDevQueue->getIndirectHeap(IndirectHeap::DYNAMIC_STATE);
        // In ExecutionModel IOH is the same as DSH to eliminate StateBaseAddress reprogramming for scheduler kernel and blocks.
        ioh = dsh;
        implicitFlush = true;
    } else {
        dsh = &getIndirectHeap(IndirectHeap::DYNAMIC_STATE, 0u);
        ioh = &getIndirectHeap(IndirectHeap::INDIRECT_OBJECT, 0u);
    }

    getGpgpuCommandStreamReceiver().requestThreadArbitrationPolicy(multiDispatchInfo.peekMainKernel()->getThreadArbitrationPolicy<GfxFamily>());

    auto allocNeedsFlushDC = false;
    if (!device->isFullRangeSvm()) {
        if (std::any_of(getGpgpuCommandStreamReceiver().getResidencyAllocations().begin(), getGpgpuCommandStreamReceiver().getResidencyAllocations().end(), [](const auto allocation) { return allocation->isFlushL3Required(); })) {
            allocNeedsFlushDC = true;
        }
    }

    DispatchFlags dispatchFlags;
    dispatchFlags.blocking = blocking;
    dispatchFlags.dcFlush = shouldFlushDC(commandType, printfHandler) || allocNeedsFlushDC;
    dispatchFlags.useSLM = multiDispatchInfo.usesSlm() || multiDispatchInfo.peekParentKernel();
    dispatchFlags.guardCommandBufferWithPipeControl = true;
    dispatchFlags.GSBA32BitRequired = commandType == CL_COMMAND_NDRANGE_KERNEL;
    dispatchFlags.pipelineSelectArgs.mediaSamplerRequired = mediaSamplerRequired;
    dispatchFlags.requiresCoherency = requiresCoherency;
    dispatchFlags.lowPriority = (QueuePriority::LOW == priority);
    dispatchFlags.throttle = getThrottle();
    dispatchFlags.implicitFlush = implicitFlush;
    dispatchFlags.flushStampReference = this->flushStamp->getStampReference();
    dispatchFlags.preemptionMode = PreemptionHelper::taskPreemptionMode(*device, multiDispatchInfo);
    dispatchFlags.outOfOrderExecutionAllowed = !eventBuilder.getEvent() || getGpgpuCommandStreamReceiver().isNTo1SubmissionModelEnabled();
    if (getGpgpuCommandStreamReceiver().peekTimestampPacketWriteEnabled()) {
        dispatchFlags.csrDependencies.fillFromEventsRequest(eventsRequest, getGpgpuCommandStreamReceiver(), CsrDependencies::DependenciesType::OutOfCsr);
        dispatchFlags.csrDependencies.makeResident(getGpgpuCommandStreamReceiver());
    }
    dispatchFlags.numGrfRequired = numGrfRequired;
    dispatchFlags.pipelineSelectArgs.specialPipelineSelectMode = specialPipelineSelectMode;
    dispatchFlags.multiEngineQueue = this->multiEngineQueue;
    DEBUG_BREAK_IF(taskLevel >= Event::eventNotReady);

    if (anyUncacheableArgs) {
        dispatchFlags.l3CacheSettings = L3CachingSettings::l3CacheOff;
    } else if (!kernel->areStatelessWritesUsed()) {
        dispatchFlags.l3CacheSettings = L3CachingSettings::l3AndL1On;
    }

    if (gtpinIsGTPinInitialized()) {
        gtpinNotifyPreFlushTask(this);
    }

    printDebugString(DebugManager.flags.PrintDebugMessages.get(), stdout, "preemption = %d.\n", static_cast<int>(dispatchFlags.preemptionMode));
    CompletionStamp completionStamp = getGpgpuCommandStreamReceiver().flushTask(
        commandStream,
        commandStreamStart,
        *dsh,
        *ioh,
        getIndirectHeap(IndirectHeap::SURFACE_STATE, 0u),
        taskLevel,
        dispatchFlags,
        *device);

    return completionStamp;
}

template <typename GfxFamily>
void CommandQueueHw<GfxFamily>::enqueueBlocked(
    uint32_t commandType,
    Surface **surfaces,
    size_t surfaceCount,
    const MultiDispatchInfo &multiDispatchInfo,
    TimestampPacketContainer *previousTimestampPacketNodes,
    std::unique_ptr<KernelOperation> &blockedCommandsData,
    const EnqueueProperties &enqueueProperties,
    EventsRequest &eventsRequest,
    EventBuilder &externalEventBuilder,
    std::unique_ptr<PrintfHandler> printfHandler) {

    TakeOwnershipWrapper<CommandQueueHw<GfxFamily>> queueOwnership(*this);

    //store previous virtual event as it will add dependecies to new virtual event
    if (this->virtualEvent) {
        DBG_LOG(EventsDebugEnable, "enqueueBlocked", "previousVirtualEvent", this->virtualEvent);
    }

    EventBuilder internalEventBuilder;
    EventBuilder *eventBuilder;
    // check if event will be exposed externally
    if (externalEventBuilder.getEvent()) {
        externalEventBuilder.getEvent()->incRefInternal();
        eventBuilder = &externalEventBuilder;
        DBG_LOG(EventsDebugEnable, "enqueueBlocked", "output event as virtualEvent", virtualEvent);
    } else {
        // it will be an internal event
        internalEventBuilder.create<VirtualEvent>(this, context);
        eventBuilder = &internalEventBuilder;
        DBG_LOG(EventsDebugEnable, "enqueueBlocked", "new virtualEvent", eventBuilder->getEvent());
    }
    auto outEvent = eventBuilder->getEvent();

    //update queue taskCount
    taskCount = outEvent->getCompletionStamp();

    std::unique_ptr<Command> command;
    bool storeTimestampPackets = false;

    if (blockedCommandsData) {
        if (enqueueProperties.operation == EnqueueProperties::Operation::Blit) {
            blockedCommandsData->blitProperties = *enqueueProperties.blitProperties;
            blockedCommandsData->blitEnqueue = true;
        }

        storeTimestampPackets = (timestampPacketContainer != nullptr);
    }

    if (enqueueProperties.operation != EnqueueProperties::Operation::GpuKernel) {
        command = std::make_unique<CommandWithoutKernel>(*this, blockedCommandsData);
    } else {
        //store task data in event
        std::vector<Surface *> allSurfaces;
        Kernel *kernel = nullptr;
        for (auto &dispatchInfo : multiDispatchInfo) {
            if (kernel != dispatchInfo.getKernel()) {
                kernel = dispatchInfo.getKernel();
            } else {
                continue;
            }
            kernel->getResidency(allSurfaces);
        }
        for (auto &surface : CreateRange(surfaces, surfaceCount)) {
            allSurfaces.push_back(surface->duplicate());
        }
        PreemptionMode preemptionMode = PreemptionHelper::taskPreemptionMode(*device, multiDispatchInfo);
        bool slmUsed = multiDispatchInfo.usesSlm() || multiDispatchInfo.peekParentKernel();
        command = std::make_unique<CommandComputeKernel>(*this,
                                                         blockedCommandsData,
                                                         allSurfaces,
                                                         shouldFlushDC(commandType, printfHandler.get()),
                                                         slmUsed,
                                                         commandType == CL_COMMAND_NDRANGE_KERNEL,
                                                         std::move(printfHandler),
                                                         preemptionMode,
                                                         multiDispatchInfo.peekMainKernel(),
                                                         (uint32_t)multiDispatchInfo.size());
    }
    if (storeTimestampPackets) {
        for (cl_uint i = 0; i < eventsRequest.numEventsInWaitList; i++) {
            auto event = castToObjectOrAbort<Event>(eventsRequest.eventWaitList[i]);
            event->incRefInternal();
        }
        command->setTimestampPacketNode(*timestampPacketContainer, *previousTimestampPacketNodes);
        command->setEventsRequest(eventsRequest);
    }
    outEvent->setCommand(std::move(command));

    eventBuilder->addParentEvents(ArrayRef<const cl_event>(eventsRequest.eventWaitList, eventsRequest.numEventsInWaitList));
    eventBuilder->addParentEvent(this->virtualEvent);
    eventBuilder->finalize();

    if (this->virtualEvent) {
        this->virtualEvent->decRefInternal();
    }

    this->virtualEvent = outEvent;
}

template <typename GfxFamily>
CompletionStamp CommandQueueHw<GfxFamily>::enqueueCommandWithoutKernel(
    Surface **surfaces,
    size_t surfaceCount,
    LinearStream &commandStream,
    size_t commandStreamStart,
    bool &blocking,
    const EnqueueProperties &enqueueProperties,
    TimestampPacketContainer *previousTimestampPacketNodes,
    EventsRequest &eventsRequest,
    EventBuilder &eventBuilder,
    uint32_t taskLevel) {

    if (timestampPacketContainer) {
        timestampPacketContainer->makeResident(getGpgpuCommandStreamReceiver());
        previousTimestampPacketNodes->makeResident(getGpgpuCommandStreamReceiver());
    }

    auto requiresCoherency = false;
    for (auto surface : CreateRange(surfaces, surfaceCount)) {
        surface->makeResident(getGpgpuCommandStreamReceiver());
        requiresCoherency |= surface->IsCoherent;
    }

    if (enqueueProperties.operation == EnqueueProperties::Operation::Blit) {
        UNRECOVERABLE_IF(!enqueueProperties.blitProperties);
        auto bcsCsr = getBcsCommandStreamReceiver();

        enqueueProperties.blitProperties->csrDependencies.makeResident(*bcsCsr);
        previousTimestampPacketNodes->makeResident(*bcsCsr);
        timestampPacketContainer->makeResident(*bcsCsr);
        bcsCsr->blitBuffer(*enqueueProperties.blitProperties);
    }

    DispatchFlags dispatchFlags = {};
    dispatchFlags.blocking = blocking;
    dispatchFlags.multiEngineQueue = multiEngineQueue;
    dispatchFlags.preemptionMode = device->getPreemptionMode();
    dispatchFlags.implicitFlush = (enqueueProperties.operation == EnqueueProperties::Operation::Blit);
    dispatchFlags.guardCommandBufferWithPipeControl = true;
    dispatchFlags.outOfOrderExecutionAllowed = getGpgpuCommandStreamReceiver().isNTo1SubmissionModelEnabled();

    if (getGpgpuCommandStreamReceiver().peekTimestampPacketWriteEnabled()) {
        dispatchFlags.csrDependencies.fillFromEventsRequest(eventsRequest, getGpgpuCommandStreamReceiver(), CsrDependencies::DependenciesType::OutOfCsr);
        dispatchFlags.csrDependencies.makeResident(getGpgpuCommandStreamReceiver());
    }
    CompletionStamp completionStamp = getGpgpuCommandStreamReceiver().flushTask(
        commandStream,
        commandStreamStart,
        getIndirectHeap(IndirectHeap::DYNAMIC_STATE, 0u),
        getIndirectHeap(IndirectHeap::INDIRECT_OBJECT, 0u),
        getIndirectHeap(IndirectHeap::SURFACE_STATE, 0u),
        taskLevel,
        dispatchFlags,
        *device);

    return completionStamp;
}

template <typename GfxFamily>
void CommandQueueHw<GfxFamily>::computeOffsetsValueForRectCommands(size_t *bufferOffset,
                                                                   size_t *hostOffset,
                                                                   const size_t *bufferOrigin,
                                                                   const size_t *hostOrigin,
                                                                   const size_t *region,
                                                                   size_t bufferRowPitch,
                                                                   size_t bufferSlicePitch,
                                                                   size_t hostRowPitch,
                                                                   size_t hostSlicePitch) {
    size_t computedBufferRowPitch = bufferRowPitch ? bufferRowPitch : region[0];
    size_t computedBufferSlicePitch = bufferSlicePitch ? bufferSlicePitch : region[1] * computedBufferRowPitch;
    size_t computedHostRowPitch = hostRowPitch ? hostRowPitch : region[0];
    size_t computedHostSlicePitch = hostSlicePitch ? hostSlicePitch : region[1] * computedHostRowPitch;
    *bufferOffset = bufferOrigin[2] * computedBufferSlicePitch + bufferOrigin[1] * computedBufferRowPitch + bufferOrigin[0];
    *hostOffset = hostOrigin[2] * computedHostSlicePitch + hostOrigin[1] * computedHostRowPitch + hostOrigin[0];
}

template <typename GfxFamily>
size_t CommandQueueHw<GfxFamily>::calculateHostPtrSizeForImage(const size_t *region, size_t rowPitch, size_t slicePitch, Image *image) {
    auto bytesPerPixel = image->getSurfaceFormatInfo().ImageElementSizeInBytes;
    auto dstRowPitch = rowPitch ? rowPitch : region[0] * bytesPerPixel;
    auto dstSlicePitch = slicePitch ? slicePitch : ((image->getImageDesc().image_type == CL_MEM_OBJECT_IMAGE1D_ARRAY ? 1 : region[1]) * dstRowPitch);

    return Image::calculateHostPtrSize(region, dstRowPitch, dstSlicePitch, bytesPerPixel, image->getImageDesc().image_type);
}
} // namespace NEO
