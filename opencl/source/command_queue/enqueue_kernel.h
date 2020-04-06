/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_stream/command_stream_receiver.h"

#include "opencl/source/built_ins/builtins_dispatch_builder.h"
#include "opencl/source/command_queue/command_queue_hw.h"
#include "opencl/source/command_queue/gpgpu_walker.h"
#include "opencl/source/helpers/hardware_commands_helper.h"
#include "opencl/source/helpers/task_information.h"
#include "opencl/source/mem_obj/buffer.h"
#include "opencl/source/memory_manager/mem_obj_surface.h"

#include <new>

namespace NEO {

template <typename GfxFamily>
cl_int CommandQueueHw<GfxFamily>::enqueueKernel(
    cl_kernel clKernel,
    cl_uint workDim,
    const size_t *globalWorkOffsetIn,
    const size_t *globalWorkSizeIn,
    const size_t *localWorkSizeIn,
    cl_uint numEventsInWaitList,
    const cl_event *eventWaitList,
    cl_event *event) {

    size_t region[3] = {1, 1, 1};
    size_t globalWorkOffset[3] = {0, 0, 0};
    size_t workGroupSize[3] = {1, 1, 1};
    size_t enqueuedLocalWorkSize[3] = {0, 0, 0};

    auto &kernel = *castToObjectOrAbort<Kernel>(clKernel);
    const auto &kernelInfo = kernel.getKernelInfo();

    if (kernel.isParentKernel && !this->context->getDefaultDeviceQueue()) {
        return CL_INVALID_OPERATION;
    }

    if (!kernel.isPatched()) {
        if (event) {
            *event = nullptr;
        }

        return CL_INVALID_KERNEL_ARGS;
    }

    if (kernel.isUsingSharedObjArgs()) {
        kernel.resetSharedObjectsPatchAddresses();
    }

    bool haveRequiredWorkGroupSize = false;

    if (kernelInfo.reqdWorkGroupSize[0] != WorkloadInfo::undefinedOffset) {
        haveRequiredWorkGroupSize = true;
    }

    size_t remainder = 0;
    size_t totalWorkItems = 1u;
    const size_t *localWkgSizeToPass = localWorkSizeIn ? workGroupSize : nullptr;

    for (auto i = 0u; i < workDim; i++) {
        region[i] = globalWorkSizeIn ? globalWorkSizeIn[i] : 0;
        if (region[i] == 0 && (kernel.getDevice().getEnabledClVersion() < 21)) {
            return CL_INVALID_GLOBAL_WORK_SIZE;
        }
        globalWorkOffset[i] = globalWorkOffsetIn
                                  ? globalWorkOffsetIn[i]
                                  : 0;

        if (localWorkSizeIn) {
            if (haveRequiredWorkGroupSize) {
                if (kernelInfo.reqdWorkGroupSize[i] != localWorkSizeIn[i]) {
                    return CL_INVALID_WORK_GROUP_SIZE;
                }
            }
            if (localWorkSizeIn[i] == 0) {
                return CL_INVALID_WORK_GROUP_SIZE;
            }
            if (kernel.getAllowNonUniform()) {
                workGroupSize[i] = std::min(localWorkSizeIn[i], std::max(static_cast<size_t>(1), globalWorkSizeIn[i]));
            } else {
                workGroupSize[i] = localWorkSizeIn[i];
            }
            enqueuedLocalWorkSize[i] = localWorkSizeIn[i];
            totalWorkItems *= localWorkSizeIn[i];
        }

        remainder += region[i] % workGroupSize[i];
    }

    if (remainder != 0 && !kernel.getAllowNonUniform()) {
        return CL_INVALID_WORK_GROUP_SIZE;
    }

    if (haveRequiredWorkGroupSize) {
        localWkgSizeToPass = kernelInfo.reqdWorkGroupSize;
    }

    NullSurface s;
    Surface *surfaces[] = {&s};

    if (context->isProvidingPerformanceHints()) {
        if (kernel.hasPrintfOutput()) {
            context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, PRINTF_DETECTED_IN_KERNEL, kernel.getKernelInfo().name.c_str());
        }
        if (kernel.requiresCoherency()) {
            context->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, KERNEL_REQUIRES_COHERENCY, kernel.getKernelInfo().name.c_str());
        }
    }

    if (kernel.getKernelInfo().builtinDispatchBuilder != nullptr) {
        cl_int err = kernel.getKernelInfo().builtinDispatchBuilder->validateDispatch(&kernel, workDim, Vec3<size_t>(region), Vec3<size_t>(workGroupSize), Vec3<size_t>(globalWorkOffset));
        if (err != CL_SUCCESS)
            return err;
    }

    DBG_LOG(PrintDispatchParameters, "Kernel: ", kernel.getKernelInfo().name,
            ",LWS:, ", localWorkSizeIn ? localWorkSizeIn[0] : 0,
            ",", localWorkSizeIn ? localWorkSizeIn[1] : 0,
            ",", localWorkSizeIn ? localWorkSizeIn[2] : 0,
            ",GWS:,", globalWorkSizeIn[0],
            ",", globalWorkSizeIn[1],
            ",", globalWorkSizeIn[2],
            ",SIMD:, ", kernel.getKernelInfo().getMaxSimdSize());

    if (totalWorkItems > kernel.maxKernelWorkGroupSize) {
        return CL_INVALID_WORK_GROUP_SIZE;
    }

    enqueueHandler<CL_COMMAND_NDRANGE_KERNEL>(
        surfaces,
        false,
        &kernel,
        workDim,
        globalWorkOffset,
        region,
        localWkgSizeToPass,
        enqueuedLocalWorkSize,
        numEventsInWaitList,
        eventWaitList,
        event);

    return CL_SUCCESS;
}
} // namespace NEO
