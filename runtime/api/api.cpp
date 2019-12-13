/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "api.h"

#include "core/execution_environment/root_device_environment.h"
#include "core/helpers/aligned_memory.h"
#include "core/helpers/kernel_helpers.h"
#include "core/memory_manager/unified_memory_manager.h"
#include "core/utilities/stackvec.h"
#include "runtime/accelerators/intel_motion_estimation.h"
#include "runtime/api/additional_extensions.h"
#include "runtime/aub/aub_center.h"
#include "runtime/built_ins/built_ins.h"
#include "runtime/command_queue/command_queue.h"
#include "runtime/command_stream/command_stream_receiver.h"
#include "runtime/context/context.h"
#include "runtime/context/driver_diagnostics.h"
#include "runtime/device/device.h"
#include "runtime/device_queue/device_queue.h"
#include "runtime/event/user_event.h"
#include "runtime/gtpin/gtpin_notify.h"
#include "runtime/helpers/get_info.h"
#include "runtime/helpers/hw_info.h"
#include "runtime/helpers/mem_properties_parser_helper.h"
#include "runtime/helpers/memory_properties_flags_helpers.h"
#include "runtime/helpers/options.h"
#include "runtime/helpers/queue_helpers.h"
#include "runtime/helpers/validators.h"
#include "runtime/kernel/kernel.h"
#include "runtime/mem_obj/buffer.h"
#include "runtime/mem_obj/image.h"
#include "runtime/mem_obj/mem_obj_helper.h"
#include "runtime/mem_obj/pipe.h"
#include "runtime/os_interface/debug_settings_manager.h"
#include "runtime/platform/platform.h"
#include "runtime/program/program.h"
#include "runtime/sampler/sampler.h"
#include "runtime/sharings/sharing_factory.h"
#include "runtime/tracing/tracing_api.h"
#include "runtime/tracing/tracing_notify.h"
#include "runtime/utilities/api_intercept.h"

#include "CL/cl.h"
#include "config.h"

#include <algorithm>
#include <cstring>

using namespace NEO;

cl_int CL_API_CALL clGetPlatformIDs(cl_uint numEntries,
                                    cl_platform_id *platforms,
                                    cl_uint *numPlatforms) {
    TRACING_ENTER(clGetPlatformIDs, &numEntries, &platforms, &numPlatforms);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("numEntries", numEntries,
                   "platforms", platforms,
                   "numPlatforms", numPlatforms);

    do {
        // if platforms is nullptr, we must return the number of valid platforms we
        // support in the num_platforms variable (if it is non-nullptr)
        if ((platforms == nullptr) && (numPlatforms == nullptr)) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        // platform != nullptr and num_entries == 0 is defined by spec as invalid
        if (numEntries == 0 && platforms != nullptr) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        while (platforms != nullptr) {
            auto pPlatform = constructPlatform();
            bool ret = pPlatform->initialize();
            DEBUG_BREAK_IF(ret != true);
            if (!ret) {
                retVal = CL_OUT_OF_HOST_MEMORY;
                break;
            }

            // we only have one platform so we can program that directly
            platforms[0] = pPlatform;
            break;
        }

        // we only have a single platform at this time, so return 1 if num_platforms
        // is non-nullptr
        if (numPlatforms && retVal == CL_SUCCESS) {
            *numPlatforms = 1;
        }
    } while (false);
    TRACING_EXIT(clGetPlatformIDs, &retVal);
    return retVal;
}

CL_API_ENTRY cl_int CL_API_CALL clIcdGetPlatformIDsKHR(cl_uint numEntries,
                                                       cl_platform_id *platforms,
                                                       cl_uint *numPlatforms) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("numEntries", numEntries,
                   "platforms", platforms,
                   "numPlatforms", numPlatforms);
    retVal = clGetPlatformIDs(numEntries, platforms, numPlatforms);
    return retVal;
}

cl_int CL_API_CALL clGetPlatformInfo(cl_platform_id platform,
                                     cl_platform_info paramName,
                                     size_t paramValueSize,
                                     void *paramValue,
                                     size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetPlatformInfo, &platform, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_INVALID_PLATFORM;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("platform", platform,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);
    auto pPlatform = castToObject<Platform>(platform);
    if (pPlatform) {
        retVal = pPlatform->getInfo(paramName, paramValueSize,
                                    paramValue, paramValueSizeRet);
    }
    TRACING_EXIT(clGetPlatformInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetDeviceIDs(cl_platform_id platform,
                                  cl_device_type deviceType,
                                  cl_uint numEntries,
                                  cl_device_id *devices,
                                  cl_uint *numDevices) {
    TRACING_ENTER(clGetDeviceIDs, &platform, &deviceType, &numEntries, &devices, &numDevices);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("platform", platform,
                   "deviceType", deviceType,
                   "numEntries", numEntries,
                   "devices", devices,
                   "numDevices", numDevices);
    const cl_device_type validType = CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_CPU |
                                     CL_DEVICE_TYPE_ACCELERATOR | CL_DEVICE_TYPE_DEFAULT |
                                     CL_DEVICE_TYPE_CUSTOM;
    Platform *pPlatform = nullptr;

    do {
        /* Check parameter consistency */
        if (devices == nullptr && numDevices == nullptr) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        if (devices && numEntries == 0) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        if ((deviceType & validType) == 0) {
            retVal = CL_INVALID_DEVICE_TYPE;
            break;
        }

        if (platform != nullptr) {
            pPlatform = castToObject<Platform>(platform);
            if (pPlatform == nullptr) {
                retVal = CL_INVALID_PLATFORM;
                break;
            }
        } else {
            pPlatform = constructPlatform();
            bool ret = pPlatform->initialize();
            DEBUG_BREAK_IF(ret != true);
            UNUSED_VARIABLE(ret);
        }

        DEBUG_BREAK_IF(pPlatform->isInitialized() != true);
        cl_uint numDev = static_cast<cl_uint>(pPlatform->getNumDevices());
        if (numDev == 0) {
            retVal = CL_DEVICE_NOT_FOUND;
            break;
        }

        Device *device = pPlatform->getDevice(0);
        DEBUG_BREAK_IF(device == nullptr);

        if (deviceType == CL_DEVICE_TYPE_ALL) {
            /* According to Spec, set it to all except TYPE_CUSTOM. */
            deviceType = CL_DEVICE_TYPE_GPU | CL_DEVICE_TYPE_CPU |
                         CL_DEVICE_TYPE_ACCELERATOR | CL_DEVICE_TYPE_DEFAULT;
        } else if (deviceType == CL_DEVICE_TYPE_DEFAULT) {
            /* We just set it to GPU now. */
            deviceType = CL_DEVICE_TYPE_GPU;
        }

        cl_uint retNum = 0;

        if (deviceType & device->getDeviceInfo().deviceType) {
            if (devices) {
                devices[retNum] = device;
            }
            retNum++;
        }

        if (DebugManager.flags.LimitAmountOfReturnedDevices.get()) {
            retNum = std::min(static_cast<uint32_t>(DebugManager.flags.LimitAmountOfReturnedDevices.get()), retNum);
        }

        if (numDevices) {
            *numDevices = retNum;
        }

        /* If no suitable device, set a error. */
        if (retNum == 0)
            retVal = CL_DEVICE_NOT_FOUND;
    } while (false);
    TRACING_EXIT(clGetDeviceIDs, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetDeviceInfo(cl_device_id device,
                                   cl_device_info paramName,
                                   size_t paramValueSize,
                                   void *paramValue,
                                   size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetDeviceInfo, &device, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_INVALID_DEVICE;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("clDevice", device, "paramName", paramName, "paramValueSize", paramValueSize, "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize), "paramValueSizeRet", paramValueSizeRet);

    Device *pDevice = castToObject<Device>(device);
    if (pDevice != nullptr) {
        retVal = pDevice->getDeviceInfo(paramName, paramValueSize,
                                        paramValue, paramValueSizeRet);
    }
    TRACING_EXIT(clGetDeviceInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clCreateSubDevices(cl_device_id inDevice,
                                      const cl_device_partition_property *properties,
                                      cl_uint numDevices,
                                      cl_device_id *outDevices,
                                      cl_uint *numDevicesRet) {
    TRACING_ENTER(clCreateSubDevices, &inDevice, &properties, &numDevices, &outDevices, &numDevicesRet);
    cl_int retVal = CL_INVALID_DEVICE;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("inDevice", inDevice,
                   "properties", properties,
                   "numDevices", numDevices,
                   "outDevices:", outDevices,
                   "numDevicesRet", numDevicesRet);

    TRACING_EXIT(clCreateSubDevices, &retVal);
    return retVal;
}

cl_int CL_API_CALL clRetainDevice(cl_device_id device) {
    TRACING_ENTER(clRetainDevice, &device);
    cl_int retVal = CL_INVALID_DEVICE;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("device", device);
    auto pDevice = castToObject<Device>(device);
    if (pDevice) {
        pDevice->retain();
        retVal = CL_SUCCESS;
    }

    TRACING_EXIT(clRetainDevice, &retVal);
    return retVal;
}

cl_int CL_API_CALL clReleaseDevice(cl_device_id device) {
    TRACING_ENTER(clReleaseDevice, &device);
    cl_int retVal = CL_INVALID_DEVICE;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("device", device);
    auto pDevice = castToObject<Device>(device);
    if (pDevice) {
        pDevice->release();
        retVal = CL_SUCCESS;
    }

    TRACING_EXIT(clReleaseDevice, &retVal);
    return retVal;
}

cl_context CL_API_CALL clCreateContext(const cl_context_properties *properties,
                                       cl_uint numDevices,
                                       const cl_device_id *devices,
                                       void(CL_CALLBACK *funcNotify)(const char *, const void *,
                                                                     size_t, void *),
                                       void *userData,
                                       cl_int *errcodeRet) {
    TRACING_ENTER(clCreateContext, &properties, &numDevices, &devices, &funcNotify, &userData, &errcodeRet);

    cl_int retVal = CL_SUCCESS;
    cl_context context = nullptr;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("properties", properties, "numDevices", numDevices, "cl_device_id", devices, "funcNotify", funcNotify, "userData", userData);

    do {
        if (devices == nullptr) {
            /* Must have device. */
            retVal = CL_INVALID_VALUE;
            break;
        }

        /* validateObjects make sure numDevices != 0. */
        retVal = validateObjects(DeviceList(numDevices, devices));
        if (retVal != CL_SUCCESS)
            break;

        if (funcNotify == nullptr && userData != nullptr) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        DeviceVector allDevs(devices, numDevices);
        context = Context::create<Context>(properties, allDevs, funcNotify, userData, retVal);
        if (context != nullptr) {
            gtpinNotifyContextCreate(context);
        }
    } while (false);

    if (errcodeRet) {
        *errcodeRet = retVal;
    }
    TRACING_EXIT(clCreateContext, &context);
    return context;
}

cl_context CL_API_CALL clCreateContextFromType(const cl_context_properties *properties,
                                               cl_device_type deviceType,
                                               void(CL_CALLBACK *funcNotify)(const char *, const void *,
                                                                             size_t, void *),
                                               void *userData,
                                               cl_int *errcodeRet) {
    TRACING_ENTER(clCreateContextFromType, &properties, &deviceType, &funcNotify, &userData, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("properties", properties, "deviceType", deviceType, "funcNotify", funcNotify, "userData", userData);
    Context *pContext = nullptr;

    do {
        if (funcNotify == nullptr && userData != nullptr) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        cl_uint numDevices = 0;
        /* Query the number of device first. */
        retVal = clGetDeviceIDs(nullptr, deviceType, 0, nullptr, &numDevices);
        if (retVal != CL_SUCCESS) {
            break;
        }

        DEBUG_BREAK_IF(numDevices <= 0);
        StackVec<cl_device_id, 2> supportedDevs;
        supportedDevs.resize(numDevices);

        retVal = clGetDeviceIDs(nullptr, deviceType, numDevices, supportedDevs.begin(), nullptr);
        DEBUG_BREAK_IF(retVal != CL_SUCCESS);

        DeviceVector allDevs(supportedDevs.begin(), numDevices);
        pContext = Context::create<Context>(properties, allDevs, funcNotify, userData, retVal);
        if (pContext != nullptr) {
            gtpinNotifyContextCreate((cl_context)pContext);
        }
    } while (false);

    if (errcodeRet) {
        *errcodeRet = retVal;
    }
    TRACING_EXIT(clCreateContextFromType, (cl_context *)&pContext);
    return pContext;
}

cl_int CL_API_CALL clRetainContext(cl_context context) {
    TRACING_ENTER(clRetainContext, &context);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context);
    Context *pContext = castToObject<Context>(context);
    if (pContext) {
        pContext->retain();
        TRACING_EXIT(clRetainContext, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_CONTEXT;
    TRACING_EXIT(clRetainContext, &retVal);
    return retVal;
}

cl_int CL_API_CALL clReleaseContext(cl_context context) {
    TRACING_ENTER(clReleaseContext, &context);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context);
    Context *pContext = castToObject<Context>(context);
    if (pContext) {
        pContext->release();
        TRACING_EXIT(clReleaseContext, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_CONTEXT;
    TRACING_EXIT(clReleaseContext, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetContextInfo(cl_context context,
                                    cl_context_info paramName,
                                    size_t paramValueSize,
                                    void *paramValue,
                                    size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetContextInfo, &context, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    auto retVal = CL_INVALID_CONTEXT;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);
    auto pContext = castToObject<Context>(context);

    if (pContext) {
        retVal = pContext->getInfo(paramName, paramValueSize,
                                   paramValue, paramValueSizeRet);
    }

    TRACING_EXIT(clGetContextInfo, &retVal);
    return retVal;
}

cl_command_queue CL_API_CALL clCreateCommandQueue(cl_context context,
                                                  cl_device_id device,
                                                  const cl_command_queue_properties properties,
                                                  cl_int *errcodeRet) {
    TRACING_ENTER(clCreateCommandQueue, &context, &device, (cl_command_queue_properties *)&properties, &errcodeRet);
    cl_command_queue commandQueue = nullptr;
    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "device", device,
                   "properties", properties);

    do {
        if (properties &
            ~(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE | CL_QUEUE_PROFILING_ENABLE)) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        Context *pContext = nullptr;
        Device *pDevice = nullptr;

        retVal = validateObjects(
            WithCastToInternal(context, &pContext),
            WithCastToInternal(device, &pDevice));

        if (retVal != CL_SUCCESS) {
            break;
        }

        cl_queue_properties props[] = {
            CL_QUEUE_PROPERTIES, properties,
            0};

        commandQueue = CommandQueue::create(pContext,
                                            pDevice,
                                            props,
                                            retVal);

        if (pContext->isProvidingPerformanceHints()) {
            pContext->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, DRIVER_CALLS_INTERNAL_CL_FLUSH);
            if (castToObjectOrAbort<CommandQueue>(commandQueue)->isProfilingEnabled()) {
                pContext->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, PROFILING_ENABLED);
                if (pDevice->getDeviceInfo().preemptionSupported && pDevice->getHardwareInfo().platform.eProductFamily < IGFX_SKYLAKE) {
                    pContext->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, PROFILING_ENABLED_WITH_DISABLED_PREEMPTION);
                }
            }
        }
    } while (false);

    err.set(retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue);
    TRACING_EXIT(clCreateCommandQueue, &commandQueue);
    return commandQueue;
}

cl_int CL_API_CALL clRetainCommandQueue(cl_command_queue commandQueue) {
    TRACING_ENTER(clRetainCommandQueue, &commandQueue);
    cl_int retVal = CL_INVALID_COMMAND_QUEUE;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue);
    retainQueue<CommandQueue>(commandQueue, retVal);
    if (retVal == CL_SUCCESS) {
        TRACING_EXIT(clRetainCommandQueue, &retVal);
        return retVal;
    }
    // if host queue not found - try to query device queue
    retainQueue<DeviceQueue>(commandQueue, retVal);

    TRACING_EXIT(clRetainCommandQueue, &retVal);
    return retVal;
}

cl_int CL_API_CALL clReleaseCommandQueue(cl_command_queue commandQueue) {
    TRACING_ENTER(clReleaseCommandQueue, &commandQueue);
    cl_int retVal = CL_INVALID_COMMAND_QUEUE;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue);

    releaseQueue<CommandQueue>(commandQueue, retVal);
    if (retVal == CL_SUCCESS) {
        TRACING_EXIT(clReleaseCommandQueue, &retVal);
        return retVal;
    }
    // if host queue not found - try to query device queue
    releaseQueue<DeviceQueue>(commandQueue, retVal);

    TRACING_EXIT(clReleaseCommandQueue, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetCommandQueueInfo(cl_command_queue commandQueue,
                                         cl_command_queue_info paramName,
                                         size_t paramValueSize,
                                         void *paramValue,
                                         size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetCommandQueueInfo, &commandQueue, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_INVALID_COMMAND_QUEUE;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);

    getQueueInfo<CommandQueue>(commandQueue, paramName, paramValueSize, paramValue, paramValueSizeRet, retVal);
    // if host queue not found - try to query device queue
    if (retVal == CL_SUCCESS) {
        TRACING_EXIT(clGetCommandQueueInfo, &retVal);
        return retVal;
    }
    getQueueInfo<DeviceQueue>(commandQueue, paramName, paramValueSize, paramValue, paramValueSizeRet, retVal);

    TRACING_EXIT(clGetCommandQueueInfo, &retVal);
    return retVal;
}

// deprecated OpenCL 1.0
cl_int CL_API_CALL clSetCommandQueueProperty(cl_command_queue commandQueue,
                                             cl_command_queue_properties properties,
                                             cl_bool enable,
                                             cl_command_queue_properties *oldProperties) {
    TRACING_ENTER(clSetCommandQueueProperty, &commandQueue, &properties, &enable, &oldProperties);
    cl_int retVal = CL_INVALID_VALUE;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "properties", properties,
                   "enable", enable,
                   "oldProperties", oldProperties);
    TRACING_EXIT(clSetCommandQueueProperty, &retVal);
    return retVal;
}

cl_mem CL_API_CALL clCreateBuffer(cl_context context,
                                  cl_mem_flags flags,
                                  size_t size,
                                  void *hostPtr,
                                  cl_int *errcodeRet) {
    TRACING_ENTER(clCreateBuffer, &context, &flags, &size, &hostPtr, &errcodeRet);
    DBG_LOG_INPUTS("cl_context", context,
                   "cl_mem_flags", flags,
                   "size", size,
                   "hostPtr", DebugManager.infoPointerToString(hostPtr, size));

    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    cl_mem buffer = nullptr;
    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);

    MemoryPropertiesFlags memoryProperties = MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(flags, 0);
    if (isFieldValid(flags, MemObjHelper::validFlagsForBuffer)) {
        Buffer::validateInputAndCreateBuffer(context, memoryProperties, flags, 0, size, hostPtr, retVal, buffer);
    } else {
        retVal = CL_INVALID_VALUE;
    }

    err.set(retVal);
    DBG_LOG_INPUTS("buffer", buffer);
    TRACING_EXIT(clCreateBuffer, &buffer);
    return buffer;
}

cl_mem CL_API_CALL clCreateBufferWithPropertiesINTEL(cl_context context,
                                                     const cl_mem_properties_intel *properties,
                                                     size_t size,
                                                     void *hostPtr,
                                                     cl_int *errcodeRet) {

    DBG_LOG_INPUTS("cl_context", context,
                   "cl_mem_properties_intel", properties,
                   "size", size,
                   "hostPtr", DebugManager.infoPointerToString(hostPtr, size));

    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    cl_mem buffer = nullptr;
    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);

    MemoryPropertiesFlags memoryProperties;
    cl_mem_flags flags = 0;
    cl_mem_flags_intel flagsIntel = 0;
    if (MemoryPropertiesParser::parseMemoryProperties(properties, memoryProperties, flags, flagsIntel, MemoryPropertiesParser::MemoryPropertiesParser::ObjType::BUFFER)) {
        Buffer::validateInputAndCreateBuffer(context, memoryProperties, flags, flagsIntel, size, hostPtr, retVal, buffer);
    } else {
        retVal = CL_INVALID_VALUE;
    }

    err.set(retVal);
    DBG_LOG_INPUTS("buffer", buffer);
    return buffer;
}

cl_mem CL_API_CALL clCreateSubBuffer(cl_mem buffer,
                                     cl_mem_flags flags,
                                     cl_buffer_create_type bufferCreateType,
                                     const void *bufferCreateInfo,
                                     cl_int *errcodeRet) {
    TRACING_ENTER(clCreateSubBuffer, &buffer, &flags, &bufferCreateType, &bufferCreateInfo, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("buffer", buffer,
                   "flags", flags,
                   "bufferCreateType", bufferCreateType,
                   "bufferCreateInfo", bufferCreateInfo);
    cl_mem subBuffer = nullptr;
    Buffer *parentBuffer = castToObject<Buffer>(buffer);

    do {
        if (parentBuffer == nullptr) {
            retVal = CL_INVALID_MEM_OBJECT;
            break;
        }

        /* Are there some invalid flag bits? */
        if (!MemObjHelper::checkMemFlagsForSubBuffer(flags)) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        cl_mem_flags parentFlags = parentBuffer->getMemoryPropertiesFlags();
        cl_mem_flags_intel parentFlagsIntel = parentBuffer->getMemoryPropertiesFlagsIntel();

        if (parentBuffer->isSubBuffer() == true) {
            retVal = CL_INVALID_MEM_OBJECT;
            break;
        }

        /* Check whether flag is valid. */
        if (((flags & CL_MEM_HOST_READ_ONLY) && (flags & CL_MEM_HOST_NO_ACCESS)) ||
            ((flags & CL_MEM_HOST_READ_ONLY) && (flags & CL_MEM_HOST_WRITE_ONLY)) ||
            ((flags & CL_MEM_HOST_WRITE_ONLY) && (flags & CL_MEM_HOST_NO_ACCESS))) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        /* Check whether flag is valid and compatible with parent. */
        if (flags &&
            (((parentFlags & CL_MEM_WRITE_ONLY) && (flags & (CL_MEM_READ_WRITE | CL_MEM_READ_ONLY))) ||
             ((parentFlags & CL_MEM_READ_ONLY) && (flags & (CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY))) ||
             ((parentFlags & CL_MEM_HOST_WRITE_ONLY) && (flags & CL_MEM_HOST_READ_ONLY)) ||
             ((parentFlags & CL_MEM_HOST_READ_ONLY) && (flags & CL_MEM_HOST_WRITE_ONLY)) ||
             ((parentFlags & CL_MEM_HOST_NO_ACCESS) &&
              (flags & (CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_READ_ONLY))))) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        /* Inherit some flags if we do not set. */
        if ((flags & (CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY | CL_MEM_READ_WRITE)) == 0) {
            flags |= parentFlags & (CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY | CL_MEM_READ_WRITE);
        }
        if ((flags & (CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_READ_ONLY | CL_MEM_HOST_NO_ACCESS)) == 0) {
            flags |= parentFlags & (CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_READ_ONLY |
                                    CL_MEM_HOST_NO_ACCESS);
        }
        flags |= parentFlags & (CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR);

        if (bufferCreateType != CL_BUFFER_CREATE_TYPE_REGION) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        if (bufferCreateInfo == nullptr) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        /* Must have non-zero size. */
        const cl_buffer_region *region = reinterpret_cast<const cl_buffer_region *>(bufferCreateInfo);
        if (region->size == 0) {
            retVal = CL_INVALID_BUFFER_SIZE;
            break;
        }

        /* Out of range. */
        if (region->origin > parentBuffer->getSize() ||
            region->origin + region->size > parentBuffer->getSize()) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        if (!parentBuffer->isValidSubBufferOffset(region->origin)) {
            retVal = CL_MISALIGNED_SUB_BUFFER_OFFSET;
            break;
        }

        subBuffer = parentBuffer->createSubBuffer(flags, parentFlagsIntel, region, retVal);
    } while (false);

    if (errcodeRet) {
        *errcodeRet = retVal;
    }

    TRACING_EXIT(clCreateSubBuffer, &subBuffer);
    return subBuffer;
}

cl_mem CL_API_CALL clCreateImage(cl_context context,
                                 cl_mem_flags flags,
                                 const cl_image_format *imageFormat,
                                 const cl_image_desc *imageDesc,
                                 void *hostPtr,
                                 cl_int *errcodeRet) {
    TRACING_ENTER(clCreateImage, &context, &flags, &imageFormat, &imageDesc, &hostPtr, &errcodeRet);

    cl_int retVal = CL_SUCCESS;

    API_ENTER(&retVal);
    DBG_LOG_INPUTS("cl_context", context,
                   "cl_mem_flags", flags,
                   "cl_image_format.channel_data_type", imageFormat->image_channel_data_type,
                   "cl_image_format.channel_order", imageFormat->image_channel_order,
                   "cl_image_desc.width", imageDesc->image_width,
                   "cl_image_desc.heigth", imageDesc->image_height,
                   "cl_image_desc.depth", imageDesc->image_depth,
                   "cl_image_desc.type", imageDesc->image_type,
                   "cl_image_desc.array_size", imageDesc->image_array_size,
                   "hostPtr", hostPtr);

    cl_mem image = nullptr;
    Context *pContext = nullptr;
    retVal = validateObjects(WithCastToInternal(context, &pContext));

    if (retVal == CL_SUCCESS) {
        MemoryPropertiesFlags memoryProperties = MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(flags, 0);
        if (isFieldValid(flags, MemObjHelper::validFlagsForImage)) {
            image = Image::validateAndCreateImage(pContext, memoryProperties, flags, 0, imageFormat, imageDesc, hostPtr, retVal);
        } else {
            retVal = CL_INVALID_VALUE;
        }
    }

    ErrorCodeHelper err(errcodeRet, retVal);
    DBG_LOG_INPUTS("image", image);
    TRACING_EXIT(clCreateImage, &image);
    return image;
}

cl_mem CL_API_CALL clCreateImageWithPropertiesINTEL(cl_context context,
                                                    cl_mem_properties_intel *properties,
                                                    const cl_image_format *imageFormat,
                                                    const cl_image_desc *imageDesc,
                                                    void *hostPtr,
                                                    cl_int *errcodeRet) {
    cl_int retVal = CL_SUCCESS;

    API_ENTER(&retVal);
    DBG_LOG_INPUTS("cl_context", context,
                   "cl_mem_properties_intel", properties,
                   "cl_image_format.channel_data_type", imageFormat->image_channel_data_type,
                   "cl_image_format.channel_order", imageFormat->image_channel_order,
                   "cl_image_desc.width", imageDesc->image_width,
                   "cl_image_desc.heigth", imageDesc->image_height,
                   "cl_image_desc.depth", imageDesc->image_depth,
                   "cl_image_desc.type", imageDesc->image_type,
                   "cl_image_desc.array_size", imageDesc->image_array_size,
                   "hostPtr", hostPtr);

    cl_mem image = nullptr;
    Context *pContext = nullptr;
    MemoryPropertiesFlags memoryProperties;
    cl_mem_flags flags = 0;
    cl_mem_flags_intel flagsIntel = 0;
    retVal = validateObjects(WithCastToInternal(context, &pContext));

    if (retVal == CL_SUCCESS) {
        if (MemoryPropertiesParser::parseMemoryProperties(properties, memoryProperties, flags, flagsIntel, MemoryPropertiesParser::MemoryPropertiesParser::ObjType::IMAGE)) {
            image = Image::validateAndCreateImage(pContext, memoryProperties, flags, flagsIntel, imageFormat, imageDesc, hostPtr, retVal);
        } else {
            retVal = CL_INVALID_VALUE;
        }
    }

    ErrorCodeHelper err(errcodeRet, retVal);
    DBG_LOG_INPUTS("image", image);
    return image;
}

// deprecated OpenCL 1.1
cl_mem CL_API_CALL clCreateImage2D(cl_context context,
                                   cl_mem_flags flags,
                                   const cl_image_format *imageFormat,
                                   size_t imageWidth,
                                   size_t imageHeight,
                                   size_t imageRowPitch,
                                   void *hostPtr,
                                   cl_int *errcodeRet) {
    TRACING_ENTER(clCreateImage2D, &context, &flags, &imageFormat, &imageWidth, &imageHeight, &imageRowPitch, &hostPtr, &errcodeRet);

    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("context", context,
                   "flags", flags,
                   "imageFormat", imageFormat,
                   "imageWidth", imageWidth,
                   "imageHeight", imageHeight,
                   "imageRowPitch", imageRowPitch,
                   "hostPtr", hostPtr);

    cl_mem image2D = nullptr;
    cl_image_desc imageDesc;
    memset(&imageDesc, 0, sizeof(cl_image_desc));

    imageDesc.image_height = imageHeight;
    imageDesc.image_width = imageWidth;
    imageDesc.image_row_pitch = imageRowPitch;
    imageDesc.image_type = CL_MEM_OBJECT_IMAGE2D;

    Context *pContext = nullptr;
    retVal = validateObjects(WithCastToInternal(context, &pContext));

    if (retVal == CL_SUCCESS) {
        MemoryPropertiesFlags memoryProperties = MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(flags, 0);
        image2D = Image::validateAndCreateImage(pContext, memoryProperties, flags, 0, imageFormat, &imageDesc, hostPtr, retVal);
    }

    ErrorCodeHelper err(errcodeRet, retVal);
    DBG_LOG_INPUTS("image 2D", image2D);
    TRACING_EXIT(clCreateImage2D, &image2D);
    return image2D;
}

// deprecated OpenCL 1.1
cl_mem CL_API_CALL clCreateImage3D(cl_context context,
                                   cl_mem_flags flags,
                                   const cl_image_format *imageFormat,
                                   size_t imageWidth,
                                   size_t imageHeight,
                                   size_t imageDepth,
                                   size_t imageRowPitch,
                                   size_t imageSlicePitch,
                                   void *hostPtr,
                                   cl_int *errcodeRet) {
    TRACING_ENTER(clCreateImage3D, &context, &flags, &imageFormat, &imageWidth, &imageHeight, &imageDepth, &imageRowPitch, &imageSlicePitch, &hostPtr, &errcodeRet);

    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("context", context,
                   "flags", flags,
                   "imageFormat", imageFormat,
                   "imageWidth", imageWidth,
                   "imageHeight", imageHeight,
                   "imageDepth", imageDepth,
                   "imageRowPitch", imageRowPitch,
                   "imageSlicePitch", imageSlicePitch,
                   "hostPtr", hostPtr);

    cl_mem image3D = nullptr;
    cl_image_desc imageDesc;
    memset(&imageDesc, 0, sizeof(cl_image_desc));

    imageDesc.image_depth = imageDepth;
    imageDesc.image_height = imageHeight;
    imageDesc.image_width = imageWidth;
    imageDesc.image_row_pitch = imageRowPitch;
    imageDesc.image_slice_pitch = imageSlicePitch;
    imageDesc.image_type = CL_MEM_OBJECT_IMAGE3D;

    Context *pContext = nullptr;
    retVal = validateObjects(WithCastToInternal(context, &pContext));

    if (retVal == CL_SUCCESS) {
        MemoryPropertiesFlags memoryProperties = MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(flags, 0);
        image3D = Image::validateAndCreateImage(pContext, memoryProperties, flags, 0, imageFormat, &imageDesc, hostPtr, retVal);
    }

    ErrorCodeHelper err(errcodeRet, retVal);
    DBG_LOG_INPUTS("image 3D", image3D);
    TRACING_EXIT(clCreateImage3D, &image3D);
    return image3D;
}

cl_int CL_API_CALL clRetainMemObject(cl_mem memobj) {
    TRACING_ENTER(clRetainMemObject, &memobj);
    cl_int retVal = CL_INVALID_MEM_OBJECT;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("memobj", memobj);

    auto pMemObj = castToObject<MemObj>(memobj);

    if (pMemObj) {
        pMemObj->retain();
        retVal = CL_SUCCESS;
        TRACING_EXIT(clRetainMemObject, &retVal);
        return retVal;
    }

    TRACING_EXIT(clRetainMemObject, &retVal);
    return retVal;
}

cl_int CL_API_CALL clReleaseMemObject(cl_mem memobj) {
    TRACING_ENTER(clReleaseMemObject, &memobj);
    cl_int retVal = CL_INVALID_MEM_OBJECT;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("memobj", memobj);

    auto pMemObj = castToObject<MemObj>(memobj);
    if (pMemObj) {
        pMemObj->release();
        retVal = CL_SUCCESS;
        TRACING_EXIT(clReleaseMemObject, &retVal);
        return retVal;
    }

    TRACING_EXIT(clReleaseMemObject, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetSupportedImageFormats(cl_context context,
                                              cl_mem_flags flags,
                                              cl_mem_object_type imageType,
                                              cl_uint numEntries,
                                              cl_image_format *imageFormats,
                                              cl_uint *numImageFormats) {
    TRACING_ENTER(clGetSupportedImageFormats, &context, &flags, &imageType, &numEntries, &imageFormats, &numImageFormats);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "flags", flags,
                   "imageType", imageType,
                   "numEntries", numEntries,
                   "imageFormats", imageFormats,
                   "numImageFormats", numImageFormats);
    auto pContext = castToObject<Context>(context);
    auto pPlatform = platform();
    auto pDevice = pPlatform->getDevice(0);

    if (pContext) {
        retVal = pContext->getSupportedImageFormats(pDevice, flags, imageType, numEntries,
                                                    imageFormats, numImageFormats);
    } else {
        retVal = CL_INVALID_CONTEXT;
    }

    TRACING_EXIT(clGetSupportedImageFormats, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetMemObjectInfo(cl_mem memobj,
                                      cl_mem_info paramName,
                                      size_t paramValueSize,
                                      void *paramValue,
                                      size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetMemObjectInfo, &memobj, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("memobj", memobj,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);
    MemObj *pMemObj = nullptr;
    retVal = validateObjects(WithCastToInternal(memobj, &pMemObj));
    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clGetMemObjectInfo, &retVal);
        return retVal;
    }

    retVal = pMemObj->getMemObjectInfo(paramName, paramValueSize,
                                       paramValue, paramValueSizeRet);
    TRACING_EXIT(clGetMemObjectInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetImageInfo(cl_mem image,
                                  cl_image_info paramName,
                                  size_t paramValueSize,
                                  void *paramValue,
                                  size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetImageInfo, &image, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("image", image,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);
    retVal = validateObjects(image);
    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clGetImageInfo, &retVal);
        return retVal;
    }

    auto pImgObj = castToObject<Image>(image);
    if (pImgObj == nullptr) {
        retVal = CL_INVALID_MEM_OBJECT;
        TRACING_EXIT(clGetImageInfo, &retVal);
        return retVal;
    }

    retVal = pImgObj->getImageInfo(paramName, paramValueSize, paramValue, paramValueSizeRet);
    TRACING_EXIT(clGetImageInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetImageParamsINTEL(cl_context context,
                                         const cl_image_format *imageFormat,
                                         const cl_image_desc *imageDesc,
                                         size_t *imageRowPitch,
                                         size_t *imageSlicePitch) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "imageFormat", imageFormat,
                   "imageDesc", imageDesc,
                   "imageRowPitch", imageRowPitch,
                   "imageSlicePitch", imageSlicePitch);
    SurfaceFormatInfo *surfaceFormat = nullptr;
    cl_mem_flags memFlags = CL_MEM_READ_ONLY;
    retVal = validateObjects(context);
    auto pContext = castToObject<Context>(context);

    if (CL_SUCCESS == retVal) {
        if ((imageFormat == nullptr) || (imageDesc == nullptr) || (imageRowPitch == nullptr) || (imageSlicePitch == nullptr)) {
            retVal = CL_INVALID_VALUE;
        }
    }
    if (CL_SUCCESS == retVal) {
        retVal = Image::validateImageFormat(imageFormat);
    }
    if (CL_SUCCESS == retVal) {
        surfaceFormat = (SurfaceFormatInfo *)Image::getSurfaceFormatFromTable(memFlags, imageFormat);
        retVal = Image::validate(pContext, MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(memFlags, 0), surfaceFormat, imageDesc, nullptr);
    }
    if (CL_SUCCESS == retVal) {
        retVal = Image::getImageParams(pContext, memFlags, surfaceFormat, imageDesc, imageRowPitch, imageSlicePitch);
    }
    return retVal;
}

cl_int CL_API_CALL clSetMemObjectDestructorCallback(cl_mem memobj,
                                                    void(CL_CALLBACK *funcNotify)(cl_mem, void *),
                                                    void *userData) {
    TRACING_ENTER(clSetMemObjectDestructorCallback, &memobj, &funcNotify, &userData);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("memobj", memobj, "funcNotify", funcNotify, "userData", userData);
    retVal = validateObjects(memobj, (void *)funcNotify);

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clSetMemObjectDestructorCallback, &retVal);
        return retVal;
    }

    auto pMemObj = castToObject<MemObj>(memobj);
    retVal = pMemObj->setDestructorCallback(funcNotify, userData);
    TRACING_EXIT(clSetMemObjectDestructorCallback, &retVal);
    return retVal;
}

cl_sampler CL_API_CALL clCreateSampler(cl_context context,
                                       cl_bool normalizedCoords,
                                       cl_addressing_mode addressingMode,
                                       cl_filter_mode filterMode,
                                       cl_int *errcodeRet) {
    TRACING_ENTER(clCreateSampler, &context, &normalizedCoords, &addressingMode, &filterMode, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "normalizedCoords", normalizedCoords,
                   "addressingMode", addressingMode,
                   "filterMode", filterMode);
    retVal = validateObjects(context);
    cl_sampler sampler = nullptr;

    if (retVal == CL_SUCCESS) {
        auto pContext = castToObject<Context>(context);
        sampler = Sampler::create(
            pContext,
            normalizedCoords,
            addressingMode,
            filterMode,
            CL_FILTER_NEAREST,
            0.0f,
            std::numeric_limits<float>::max(),
            retVal);
    }

    if (errcodeRet) {
        *errcodeRet = retVal;
    }

    TRACING_EXIT(clCreateSampler, &sampler);
    return sampler;
}

cl_int CL_API_CALL clRetainSampler(cl_sampler sampler) {
    TRACING_ENTER(clRetainSampler, &sampler);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("sampler", sampler);
    auto pSampler = castToObject<Sampler>(sampler);
    if (pSampler) {
        pSampler->retain();
        TRACING_EXIT(clRetainSampler, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_SAMPLER;
    TRACING_EXIT(clRetainSampler, &retVal);
    return retVal;
}

cl_int CL_API_CALL clReleaseSampler(cl_sampler sampler) {
    TRACING_ENTER(clReleaseSampler, &sampler);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("sampler", sampler);
    auto pSampler = castToObject<Sampler>(sampler);
    if (pSampler) {
        pSampler->release();
        TRACING_EXIT(clReleaseSampler, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_SAMPLER;
    TRACING_EXIT(clReleaseSampler, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetSamplerInfo(cl_sampler sampler,
                                    cl_sampler_info paramName,
                                    size_t paramValueSize,
                                    void *paramValue,
                                    size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetSamplerInfo, &sampler, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_INVALID_SAMPLER;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("sampler", sampler,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);

    auto pSampler = castToObject<Sampler>(sampler);

    if (pSampler) {
        retVal = pSampler->getInfo(paramName, paramValueSize,
                                   paramValue, paramValueSizeRet);
    }

    TRACING_EXIT(clGetSamplerInfo, &retVal);
    return retVal;
}

cl_program CL_API_CALL clCreateProgramWithSource(cl_context context,
                                                 cl_uint count,
                                                 const char **strings,
                                                 const size_t *lengths,
                                                 cl_int *errcodeRet) {
    TRACING_ENTER(clCreateProgramWithSource, &context, &count, &strings, &lengths, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "count", count,
                   "strings", strings,
                   "lengths", lengths);
    retVal = validateObjects(context, count, strings);
    cl_program program = nullptr;

    if (CL_SUCCESS == retVal) {
        program = Program::create(
            context,
            count,
            strings,
            lengths,
            retVal);
    }

    if (errcodeRet) {
        *errcodeRet = retVal;
    }

    TRACING_EXIT(clCreateProgramWithSource, &program);
    return program;
}

cl_program CL_API_CALL clCreateProgramWithBinary(cl_context context,
                                                 cl_uint numDevices,
                                                 const cl_device_id *deviceList,
                                                 const size_t *lengths,
                                                 const unsigned char **binaries,
                                                 cl_int *binaryStatus,
                                                 cl_int *errcodeRet) {
    TRACING_ENTER(clCreateProgramWithBinary, &context, &numDevices, &deviceList, &lengths, &binaries, &binaryStatus, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "numDevices", numDevices,
                   "deviceList", deviceList,
                   "lengths", lengths,
                   "binaries", binaries,
                   "binaryStatus", binaryStatus);
    retVal = validateObjects(context, deviceList, *deviceList, binaries, *binaries, lengths, *lengths);
    cl_program program = nullptr;

    DebugManager.dumpBinaryProgram(numDevices, lengths, binaries);

    if (CL_SUCCESS == retVal) {
        program = Program::create(
            context,
            numDevices,
            deviceList,
            lengths,
            binaries,
            binaryStatus,
            retVal);
    }

    if (errcodeRet) {
        *errcodeRet = retVal;
    }

    TRACING_EXIT(clCreateProgramWithBinary, &program);
    return program;
}

cl_program CL_API_CALL clCreateProgramWithIL(cl_context context,
                                             const void *il,
                                             size_t length,
                                             cl_int *errcodeRet) {
    TRACING_ENTER(clCreateProgramWithIL, &context, &il, &length, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "il", il,
                   "length", length);

    cl_program program = nullptr;
    retVal = validateObjects(context, il);
    if (retVal == CL_SUCCESS) {
        program = Program::createFromIL(
            castToObjectOrAbort<Context>(context),
            il,
            length,
            retVal);
    }

    if (errcodeRet != nullptr) {
        *errcodeRet = retVal;
    }

    TRACING_EXIT(clCreateProgramWithIL, &program);
    return program;
}

cl_program CL_API_CALL clCreateProgramWithBuiltInKernels(cl_context context,
                                                         cl_uint numDevices,
                                                         const cl_device_id *deviceList,
                                                         const char *kernelNames,
                                                         cl_int *errcodeRet) {
    TRACING_ENTER(clCreateProgramWithBuiltInKernels, &context, &numDevices, &deviceList, &kernelNames, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "numDevices", numDevices,
                   "deviceList", deviceList,
                   "kernelNames", kernelNames);
    cl_program program = nullptr;

    retVal = validateObjects(
        context, deviceList, kernelNames, errcodeRet);

    if (numDevices == 0) {
        retVal = CL_INVALID_VALUE;
    }

    if (retVal == CL_SUCCESS) {

        for (cl_uint i = 0; i < numDevices; i++) {
            auto pContext = castToObject<Context>(context);
            validateObject(pContext);
            auto pDev = castToObject<Device>(*deviceList);
            validateObject(pDev);

            program = pDev->getExecutionEnvironment()->getBuiltIns()->createBuiltInProgram(
                *pContext,
                *pDev,
                kernelNames,
                retVal);
            if (program && retVal == CL_SUCCESS) {
                break;
            }
        }
    }

    if (errcodeRet) {
        *errcodeRet = retVal;
    }

    TRACING_EXIT(clCreateProgramWithBuiltInKernels, &program);
    return program;
}

cl_int CL_API_CALL clRetainProgram(cl_program program) {
    TRACING_ENTER(clRetainProgram, &program);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("program", program);
    auto pProgram = castToObject<Program>(program);
    if (pProgram) {
        pProgram->retain();
        TRACING_EXIT(clRetainProgram, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_PROGRAM;
    TRACING_EXIT(clRetainProgram, &retVal);
    return retVal;
}

cl_int CL_API_CALL clReleaseProgram(cl_program program) {
    TRACING_ENTER(clReleaseProgram, &program);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("program", program);
    auto pProgram = castToObject<Program>(program);
    if (pProgram) {
        pProgram->release();
        TRACING_EXIT(clReleaseProgram, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_PROGRAM;
    TRACING_EXIT(clReleaseProgram, &retVal);
    return retVal;
}

cl_int CL_API_CALL clBuildProgram(cl_program program,
                                  cl_uint numDevices,
                                  const cl_device_id *deviceList,
                                  const char *options,
                                  void(CL_CALLBACK *funcNotify)(cl_program program, void *userData),
                                  void *userData) {
    TRACING_ENTER(clBuildProgram, &program, &numDevices, &deviceList, &options, &funcNotify, &userData);
    cl_int retVal = CL_INVALID_PROGRAM;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("clProgram", program, "numDevices", numDevices, "cl_device_id", deviceList, "options", (options != nullptr) ? options : "", "funcNotify", funcNotify, "userData", userData);
    auto pProgram = castToObject<Program>(program);

    if (pProgram) {
        retVal = pProgram->build(numDevices, deviceList, options, funcNotify, userData, clCacheEnabled);
    }

    TRACING_EXIT(clBuildProgram, &retVal);
    return retVal;
}

cl_int CL_API_CALL clCompileProgram(cl_program program,
                                    cl_uint numDevices,
                                    const cl_device_id *deviceList,
                                    const char *options,
                                    cl_uint numInputHeaders,
                                    const cl_program *inputHeaders,
                                    const char **headerIncludeNames,
                                    void(CL_CALLBACK *funcNotify)(cl_program program, void *userData),
                                    void *userData) {
    TRACING_ENTER(clCompileProgram, &program, &numDevices, &deviceList, &options, &numInputHeaders, &inputHeaders, &headerIncludeNames, &funcNotify, &userData);
    cl_int retVal = CL_INVALID_PROGRAM;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("clProgram", program, "numDevices", numDevices, "cl_device_id", deviceList, "options", (options != nullptr) ? options : "", "numInputHeaders", numInputHeaders);
    auto pProgram = castToObject<Program>(program);

    if (pProgram != nullptr) {
        retVal = pProgram->compile(numDevices, deviceList, options,
                                   numInputHeaders, inputHeaders, headerIncludeNames,
                                   funcNotify, userData);
    }

    TRACING_EXIT(clCompileProgram, &retVal);
    return retVal;
}

cl_program CL_API_CALL clLinkProgram(cl_context context,
                                     cl_uint numDevices,
                                     const cl_device_id *deviceList,
                                     const char *options,
                                     cl_uint numInputPrograms,
                                     const cl_program *inputPrograms,
                                     void(CL_CALLBACK *funcNotify)(cl_program program, void *userData),
                                     void *userData,
                                     cl_int *errcodeRet) {
    TRACING_ENTER(clLinkProgram, &context, &numDevices, &deviceList, &options, &numInputPrograms, &inputPrograms, &funcNotify, &userData, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("cl_context", context, "numDevices", numDevices, "cl_device_id", deviceList, "options", (options != nullptr) ? options : "", "numInputPrograms", numInputPrograms);

    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);
    Context *pContext = nullptr;
    Program *program = nullptr;

    retVal = validateObject(context);
    if (CL_SUCCESS == retVal) {
        pContext = castToObject<Context>(context);
    }
    if (pContext != nullptr) {
        program = new Program(*pContext->getDevice(0)->getExecutionEnvironment(), pContext, false);
        retVal = program->link(numDevices, deviceList, options,
                               numInputPrograms, inputPrograms,
                               funcNotify, userData);
    }

    err.set(retVal);

    TRACING_EXIT(clLinkProgram, (cl_program *)&program);
    return program;
}

cl_int CL_API_CALL clUnloadPlatformCompiler(cl_platform_id platform) {
    TRACING_ENTER(clUnloadPlatformCompiler, &platform);
    cl_int retVal = CL_OUT_OF_HOST_MEMORY;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("platform", platform);
    TRACING_EXIT(clUnloadPlatformCompiler, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetProgramInfo(cl_program program,
                                    cl_program_info paramName,
                                    size_t paramValueSize,
                                    void *paramValue,
                                    size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetProgramInfo, &program, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("clProgram", program, "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);
    retVal = validateObjects(program);

    if (CL_SUCCESS == retVal) {
        Program *pProgram = (Program *)(program);

        retVal = pProgram->getInfo(
            paramName,
            paramValueSize,
            paramValue,
            paramValueSizeRet);
    }
    TRACING_EXIT(clGetProgramInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetProgramBuildInfo(cl_program program,
                                         cl_device_id device,
                                         cl_program_build_info paramName,
                                         size_t paramValueSize,
                                         void *paramValue,
                                         size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetProgramBuildInfo, &program, &device, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("clProgram", program, "cl_device_id", device,
                   "paramName", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSize", paramValueSize, "paramValue", paramValue,
                   "paramValueSizeRet", paramValueSizeRet);
    retVal = validateObjects(program);

    if (CL_SUCCESS == retVal) {
        Program *pProgram = (Program *)(program);

        retVal = pProgram->getBuildInfo(
            device,
            paramName,
            paramValueSize,
            paramValue,
            paramValueSizeRet);
    }
    TRACING_EXIT(clGetProgramBuildInfo, &retVal);
    return retVal;
}

cl_kernel CL_API_CALL clCreateKernel(cl_program clProgram,
                                     const char *kernelName,
                                     cl_int *errcodeRet) {
    TRACING_ENTER(clCreateKernel, &clProgram, &kernelName, &errcodeRet);
    API_ENTER(errcodeRet);
    Program *pProgram = nullptr;
    cl_kernel kernel = nullptr;
    cl_int retVal = CL_SUCCESS;
    DBG_LOG_INPUTS("clProgram", clProgram, "kernelName", kernelName);

    do {
        if (!isValidObject(clProgram) ||
            !(pProgram = castToObject<Program>(clProgram))) {
            retVal = CL_INVALID_PROGRAM;
            break;
        }

        if (kernelName == nullptr) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        if (pProgram->getBuildStatus() != CL_SUCCESS) {
            retVal = CL_INVALID_PROGRAM_EXECUTABLE;
            break;
        }

        const KernelInfo *pKernelInfo = pProgram->getKernelInfo(kernelName);
        if (!pKernelInfo) {
            retVal = CL_INVALID_KERNEL_NAME;
            break;
        }

        if (pKernelInfo->isValid == false) {
            retVal = CL_INVALID_PROGRAM_EXECUTABLE;
            break;
        }

        kernel = Kernel::create(
            pProgram,
            *pKernelInfo,
            &retVal);

        DBG_LOG_INPUTS("kernel", kernel);
    } while (false);

    if (errcodeRet) {
        *errcodeRet = retVal;
    }
    if (kernel != nullptr) {
        gtpinNotifyKernelCreate(kernel);
    }
    TRACING_EXIT(clCreateKernel, &kernel);
    return kernel;
}

cl_int CL_API_CALL clCreateKernelsInProgram(cl_program clProgram,
                                            cl_uint numKernels,
                                            cl_kernel *kernels,
                                            cl_uint *numKernelsRet) {
    TRACING_ENTER(clCreateKernelsInProgram, &clProgram, &numKernels, &kernels, &numKernelsRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("clProgram", clProgram,
                   "numKernels", numKernels,
                   "kernels", kernels,
                   "numKernelsRet", numKernelsRet);
    auto program = castToObject<Program>(clProgram);
    if (program) {
        auto numKernelsInProgram = program->getNumKernels();

        if (kernels) {
            if (numKernels < numKernelsInProgram) {
                retVal = CL_INVALID_VALUE;
                TRACING_EXIT(clCreateKernelsInProgram, &retVal);
                return retVal;
            }

            for (unsigned int ordinal = 0; ordinal < numKernelsInProgram; ++ordinal) {
                const auto kernelInfo = program->getKernelInfo(ordinal);
                DEBUG_BREAK_IF(kernelInfo == nullptr);
                DEBUG_BREAK_IF(!kernelInfo->isValid);
                kernels[ordinal] = Kernel::create(
                    program,
                    *kernelInfo,
                    nullptr);
                if (kernels[ordinal] != nullptr) {
                    gtpinNotifyKernelCreate(kernels[ordinal]);
                }
            }
        }

        if (numKernelsRet) {
            *numKernelsRet = static_cast<cl_uint>(numKernelsInProgram);
        }
        TRACING_EXIT(clCreateKernelsInProgram, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_PROGRAM;
    TRACING_EXIT(clCreateKernelsInProgram, &retVal);
    return retVal;
}

cl_int CL_API_CALL clRetainKernel(cl_kernel kernel) {
    TRACING_ENTER(clRetainKernel, &kernel);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("kernel", kernel);
    auto pKernel = castToObject<Kernel>(kernel);
    if (pKernel) {
        pKernel->retain();
        TRACING_EXIT(clRetainKernel, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_KERNEL;
    TRACING_EXIT(clRetainKernel, &retVal);
    return retVal;
}

cl_int CL_API_CALL clReleaseKernel(cl_kernel kernel) {
    TRACING_ENTER(clReleaseKernel, &kernel);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("kernel", kernel);
    auto pKernel = castToObject<Kernel>(kernel);
    if (pKernel) {
        pKernel->release();
        TRACING_EXIT(clReleaseKernel, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_KERNEL;
    TRACING_EXIT(clReleaseKernel, &retVal);
    return retVal;
}

cl_int CL_API_CALL clSetKernelArg(cl_kernel kernel,
                                  cl_uint argIndex,
                                  size_t argSize,
                                  const void *argValue) {
    TRACING_ENTER(clSetKernelArg, &kernel, &argIndex, &argSize, &argValue);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    auto pKernel = castToObject<Kernel>(kernel);
    DBG_LOG_INPUTS("kernel", kernel, "argIndex", argIndex,
                   "argSize", argSize, "argValue", DebugManager.infoPointerToString(argValue, argSize));
    do {
        if (!pKernel) {
            retVal = CL_INVALID_KERNEL;
            break;
        }
        if (pKernel->getKernelInfo().kernelArgInfo.size() <= argIndex) {
            retVal = CL_INVALID_ARG_INDEX;
            break;
        }
        retVal = pKernel->checkCorrectImageAccessQualifier(argIndex, argSize, argValue);
        if (retVal != CL_SUCCESS) {
            pKernel->unsetArg(argIndex);
            break;
        }
        retVal = pKernel->setArg(
            argIndex,
            argSize,
            argValue);
        break;

    } while (false);
    TRACING_EXIT(clSetKernelArg, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetKernelInfo(cl_kernel kernel,
                                   cl_kernel_info paramName,
                                   size_t paramValueSize,
                                   void *paramValue,
                                   size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetKernelInfo, &kernel, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("kernel", kernel, "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);
    auto pKernel = castToObject<Kernel>(kernel);
    retVal = pKernel
                 ? pKernel->getInfo(
                       paramName,
                       paramValueSize,
                       paramValue,
                       paramValueSizeRet)
                 : CL_INVALID_KERNEL;
    TRACING_EXIT(clGetKernelInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetKernelArgInfo(cl_kernel kernel,
                                      cl_uint argIndx,
                                      cl_kernel_arg_info paramName,
                                      size_t paramValueSize,
                                      void *paramValue,
                                      size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetKernelArgInfo, &kernel, &argIndx, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("kernel", kernel,
                   "argIndx", argIndx,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);

    auto pKernel = castToObject<Kernel>(kernel);
    retVal = pKernel
                 ? pKernel->getArgInfo(
                       argIndx,
                       paramName,
                       paramValueSize,
                       paramValue,
                       paramValueSizeRet)
                 : CL_INVALID_KERNEL;
    TRACING_EXIT(clGetKernelArgInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetKernelWorkGroupInfo(cl_kernel kernel,
                                            cl_device_id device,
                                            cl_kernel_work_group_info paramName,
                                            size_t paramValueSize,
                                            void *paramValue,
                                            size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetKernelWorkGroupInfo, &kernel, &device, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("kernel", kernel,
                   "device", device,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);

    auto pKernel = castToObject<Kernel>(kernel);
    retVal = pKernel
                 ? pKernel->getWorkGroupInfo(
                       device,
                       paramName,
                       paramValueSize,
                       paramValue,
                       paramValueSizeRet)
                 : CL_INVALID_KERNEL;
    TRACING_EXIT(clGetKernelWorkGroupInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clWaitForEvents(cl_uint numEvents,
                                   const cl_event *eventList) {
    TRACING_ENTER(clWaitForEvents, &numEvents, &eventList);

    auto retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("eventList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventList), numEvents));

    for (unsigned int i = 0; i < numEvents && retVal == CL_SUCCESS; i++)
        retVal = validateObjects(eventList[i]);

    if (retVal != CL_SUCCESS) {
        TRACING_EXIT(clWaitForEvents, &retVal);
        return retVal;
    }

    retVal = Event::waitForEvents(numEvents, eventList);
    TRACING_EXIT(clWaitForEvents, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetEventInfo(cl_event event,
                                  cl_event_info paramName,
                                  size_t paramValueSize,
                                  void *paramValue,
                                  size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetEventInfo, &event, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    auto retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("event", event,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);

    Event *neoEvent = castToObject<Event>(event);
    if (neoEvent == nullptr) {
        retVal = CL_INVALID_EVENT;
        TRACING_EXIT(clGetEventInfo, &retVal);
        return retVal;
    }

    GetInfoHelper info(paramValue, paramValueSize, paramValueSizeRet);

    switch (paramName) {
    default: {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clGetEventInfo, &retVal);
        return retVal;
    }
    // From OCL spec :
    // "Return the command-queue associated with event. For user event objects,"
    //  a nullptr value is returned."
    case CL_EVENT_COMMAND_QUEUE: {
        if (neoEvent->isUserEvent()) {
            retVal = info.set<cl_command_queue>(nullptr);
            TRACING_EXIT(clGetEventInfo, &retVal);
            return retVal;
        }
        retVal = info.set<cl_command_queue>(neoEvent->getCommandQueue());
        TRACING_EXIT(clGetEventInfo, &retVal);
        return retVal;
    }
    case CL_EVENT_CONTEXT:
        retVal = info.set<cl_context>(neoEvent->getContext());
        TRACING_EXIT(clGetEventInfo, &retVal);
        return retVal;
    case CL_EVENT_COMMAND_TYPE:
        retVal = info.set<cl_command_type>(neoEvent->getCommandType());
        TRACING_EXIT(clGetEventInfo, &retVal);
        return retVal;
    case CL_EVENT_COMMAND_EXECUTION_STATUS:
        neoEvent->tryFlushEvent();
        if (neoEvent->isUserEvent()) {
            auto executionStatus = neoEvent->peekExecutionStatus();
            //Spec requires initial state to be queued
            //our current design relies heavily on SUBMITTED status which directly corresponds
            //to command being able to be submitted, to overcome this we set initial status to queued
            //and we override the value stored with the value required by the spec.
            if (executionStatus == CL_QUEUED) {
                executionStatus = CL_SUBMITTED;
            }
            retVal = info.set<cl_int>(executionStatus);
            TRACING_EXIT(clGetEventInfo, &retVal);
            return retVal;
        }

        retVal = info.set<cl_int>(neoEvent->updateEventAndReturnCurrentStatus());
        TRACING_EXIT(clGetEventInfo, &retVal);
        return retVal;
    case CL_EVENT_REFERENCE_COUNT:
        retVal = info.set<cl_uint>(neoEvent->getReference());
        TRACING_EXIT(clGetEventInfo, &retVal);
        return retVal;
    }
}

cl_event CL_API_CALL clCreateUserEvent(cl_context context,
                                       cl_int *errcodeRet) {
    TRACING_ENTER(clCreateUserEvent, &context, &errcodeRet);
    API_ENTER(errcodeRet);

    DBG_LOG_INPUTS("context", context);

    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);

    Context *ctx = castToObject<Context>(context);
    if (ctx == nullptr) {
        err.set(CL_INVALID_CONTEXT);
        cl_event retVal = nullptr;
        TRACING_EXIT(clCreateUserEvent, &retVal);
        return retVal;
    }

    Event *userEvent = new UserEvent(ctx);
    cl_event userClEvent = userEvent;
    DebugManager.logInputs("cl_event", userClEvent, "UserEvent", userEvent);

    TRACING_EXIT(clCreateUserEvent, &userClEvent);
    return userClEvent;
}

cl_int CL_API_CALL clRetainEvent(cl_event event) {
    TRACING_ENTER(clRetainEvent, &event);
    auto retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("event", event);

    auto pEvent = castToObject<Event>(event);
    DebugManager.logInputs("cl_event", event, "Event", pEvent);

    if (pEvent) {
        pEvent->retain();
        TRACING_EXIT(clRetainEvent, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_EVENT;
    TRACING_EXIT(clRetainEvent, &retVal);
    return retVal;
}

cl_int CL_API_CALL clReleaseEvent(cl_event event) {
    TRACING_ENTER(clReleaseEvent, &event);
    auto retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("cl_event", event);
    auto pEvent = castToObject<Event>(event);
    DebugManager.logInputs("cl_event", event, "Event", pEvent);

    if (pEvent) {
        pEvent->release();
        TRACING_EXIT(clReleaseEvent, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_EVENT;
    TRACING_EXIT(clReleaseEvent, &retVal);
    return retVal;
}

cl_int CL_API_CALL clSetUserEventStatus(cl_event event,
                                        cl_int executionStatus) {
    TRACING_ENTER(clSetUserEventStatus, &event, &executionStatus);
    auto retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    auto userEvent = castToObject<UserEvent>(event);
    DBG_LOG_INPUTS("cl_event", event, "executionStatus", executionStatus, "UserEvent", userEvent);

    if (userEvent == nullptr) {
        retVal = CL_INVALID_EVENT;
        TRACING_EXIT(clSetUserEventStatus, &retVal);
        return retVal;
    }

    if (executionStatus > CL_COMPLETE) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clSetUserEventStatus, &retVal);
        return retVal;
    }

    if (!userEvent->isInitialEventStatus()) {
        retVal = CL_INVALID_OPERATION;
        TRACING_EXIT(clSetUserEventStatus, &retVal);
        return retVal;
    }

    userEvent->setStatus(executionStatus);
    TRACING_EXIT(clSetUserEventStatus, &retVal);
    return retVal;
}

cl_int CL_API_CALL clSetEventCallback(cl_event event,
                                      cl_int commandExecCallbackType,
                                      void(CL_CALLBACK *funcNotify)(cl_event, cl_int, void *),
                                      void *userData) {
    TRACING_ENTER(clSetEventCallback, &event, &commandExecCallbackType, &funcNotify, &userData);
    auto retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    auto eventObject = castToObject<Event>(event);
    DBG_LOG_INPUTS("cl_event", event, "commandExecCallbackType", commandExecCallbackType, "Event", eventObject);

    if (eventObject == nullptr) {
        retVal = CL_INVALID_EVENT;
        TRACING_EXIT(clSetEventCallback, &retVal);
        return retVal;
    }
    switch (commandExecCallbackType) {
    case CL_COMPLETE:
    case CL_SUBMITTED:
    case CL_RUNNING:
        break;
    default: {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clSetEventCallback, &retVal);
        return retVal;
    }
    }
    if (funcNotify == nullptr) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clSetEventCallback, &retVal);
        return retVal;
    }

    eventObject->tryFlushEvent();
    eventObject->addCallback(funcNotify, commandExecCallbackType, userData);
    TRACING_EXIT(clSetEventCallback, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetEventProfilingInfo(cl_event event,
                                           cl_profiling_info paramName,
                                           size_t paramValueSize,
                                           void *paramValue,
                                           size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetEventProfilingInfo, &event, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);
    auto retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("event", event,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);
    auto eventObject = castToObject<Event>(event);

    if (eventObject == nullptr) {
        retVal = CL_INVALID_EVENT;
        TRACING_EXIT(clGetEventProfilingInfo, &retVal);
        return retVal;
    }

    retVal = eventObject->getEventProfilingInfo(paramName,
                                                paramValueSize,
                                                paramValue,
                                                paramValueSizeRet);
    TRACING_EXIT(clGetEventProfilingInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clFlush(cl_command_queue commandQueue) {
    TRACING_ENTER(clFlush, &commandQueue);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue);
    auto pCommandQueue = castToObject<CommandQueue>(commandQueue);

    retVal = pCommandQueue
                 ? pCommandQueue->flush()
                 : CL_INVALID_COMMAND_QUEUE;
    TRACING_EXIT(clFlush, &retVal);
    return retVal;
}

cl_int CL_API_CALL clFinish(cl_command_queue commandQueue) {
    TRACING_ENTER(clFinish, &commandQueue);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue);
    auto pCommandQueue = castToObject<CommandQueue>(commandQueue);

    retVal = pCommandQueue
                 ? pCommandQueue->finish()
                 : CL_INVALID_COMMAND_QUEUE;
    TRACING_EXIT(clFinish, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueReadBuffer(cl_command_queue commandQueue,
                                       cl_mem buffer,
                                       cl_bool blockingRead,
                                       size_t offset,
                                       size_t cb,
                                       void *ptr,
                                       cl_uint numEventsInWaitList,
                                       const cl_event *eventWaitList,
                                       cl_event *event) {
    TRACING_ENTER(clEnqueueReadBuffer, &commandQueue, &buffer, &blockingRead, &offset, &cb, &ptr, &numEventsInWaitList, &eventWaitList, &event);
    CommandQueue *pCommandQueue = nullptr;
    Buffer *pBuffer = nullptr;

    auto retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(buffer, &pBuffer),
        ptr);

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "buffer", buffer, "blockingRead", blockingRead,
                   "offset", offset, "cb", cb, "ptr", ptr,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (CL_SUCCESS == retVal) {

        if (pBuffer->readMemObjFlagsInvalid()) {
            retVal = CL_INVALID_OPERATION;
            TRACING_EXIT(clEnqueueReadBuffer, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueReadBuffer(
            pBuffer,
            blockingRead,
            offset,
            cb,
            ptr,
            nullptr,
            numEventsInWaitList,
            eventWaitList,
            event);
    }

    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueReadBuffer, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueReadBufferRect(cl_command_queue commandQueue,
                                           cl_mem buffer,
                                           cl_bool blockingRead,
                                           const size_t *bufferOrigin,
                                           const size_t *hostOrigin,
                                           const size_t *region,
                                           size_t bufferRowPitch,
                                           size_t bufferSlicePitch,
                                           size_t hostRowPitch,
                                           size_t hostSlicePitch,
                                           void *ptr,
                                           cl_uint numEventsInWaitList,
                                           const cl_event *eventWaitList,
                                           cl_event *event) {
    TRACING_ENTER(clEnqueueReadBufferRect, &commandQueue, &buffer, &blockingRead, &bufferOrigin, &hostOrigin, &region, &bufferRowPitch, &bufferSlicePitch, &hostRowPitch, &hostSlicePitch, &ptr, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "buffer", buffer,
                   "blockingRead", blockingRead,
                   "bufferOrigin[0]", DebugManager.getInput(bufferOrigin, 0),
                   "bufferOrigin[1]", DebugManager.getInput(bufferOrigin, 1),
                   "bufferOrigin[2]", DebugManager.getInput(bufferOrigin, 2),
                   "hostOrigin[0]", DebugManager.getInput(hostOrigin, 0),
                   "hostOrigin[1]", DebugManager.getInput(hostOrigin, 1),
                   "hostOrigin[2]", DebugManager.getInput(hostOrigin, 2),
                   "region[0]", DebugManager.getInput(region, 0),
                   "region[1]", DebugManager.getInput(region, 1),
                   "region[2]", DebugManager.getInput(region, 2),
                   "bufferRowPitch", bufferRowPitch,
                   "bufferSlicePitch", bufferSlicePitch,
                   "hostRowPitch", hostRowPitch,
                   "hostSlicePitch", hostSlicePitch,
                   "ptr", ptr,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    Buffer *pBuffer = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(buffer, &pBuffer),
        ptr);

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clEnqueueReadBufferRect, &retVal);
        return retVal;
    }

    if (pBuffer->readMemObjFlagsInvalid()) {
        retVal = CL_INVALID_OPERATION;
        TRACING_EXIT(clEnqueueReadBufferRect, &retVal);
        return retVal;
    }

    if (pBuffer->bufferRectPitchSet(bufferOrigin,
                                    region,
                                    bufferRowPitch,
                                    bufferSlicePitch,
                                    hostRowPitch,
                                    hostSlicePitch) == false) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueReadBufferRect, &retVal);
        return retVal;
    }

    retVal = pCommandQueue->enqueueReadBufferRect(
        pBuffer,
        blockingRead,
        bufferOrigin,
        hostOrigin,
        region,
        bufferRowPitch,
        bufferSlicePitch,
        hostRowPitch,
        hostSlicePitch,
        ptr,
        numEventsInWaitList,
        eventWaitList,
        event);
    TRACING_EXIT(clEnqueueReadBufferRect, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueWriteBuffer(cl_command_queue commandQueue,
                                        cl_mem buffer,
                                        cl_bool blockingWrite,
                                        size_t offset,
                                        size_t cb,
                                        const void *ptr,
                                        cl_uint numEventsInWaitList,
                                        const cl_event *eventWaitList,
                                        cl_event *event) {
    TRACING_ENTER(clEnqueueWriteBuffer, &commandQueue, &buffer, &blockingWrite, &offset, &cb, &ptr, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "buffer", buffer, "blockingWrite", blockingWrite,
                   "offset", offset, "cb", cb, "ptr", ptr,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    Buffer *pBuffer = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(buffer, &pBuffer),
        ptr);

    if (CL_SUCCESS == retVal) {

        if (pBuffer->writeMemObjFlagsInvalid()) {
            retVal = CL_INVALID_OPERATION;
            TRACING_EXIT(clEnqueueWriteBuffer, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueWriteBuffer(
            pBuffer,
            blockingWrite,
            offset,
            cb,
            ptr,
            nullptr,
            numEventsInWaitList,
            eventWaitList,
            event);
    }

    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueWriteBuffer, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueWriteBufferRect(cl_command_queue commandQueue,
                                            cl_mem buffer,
                                            cl_bool blockingWrite,
                                            const size_t *bufferOrigin,
                                            const size_t *hostOrigin,
                                            const size_t *region,
                                            size_t bufferRowPitch,
                                            size_t bufferSlicePitch,
                                            size_t hostRowPitch,
                                            size_t hostSlicePitch,
                                            const void *ptr,
                                            cl_uint numEventsInWaitList,
                                            const cl_event *eventWaitList,
                                            cl_event *event) {
    TRACING_ENTER(clEnqueueWriteBufferRect, &commandQueue, &buffer, &blockingWrite, &bufferOrigin, &hostOrigin, &region, &bufferRowPitch, &bufferSlicePitch, &hostRowPitch, &hostSlicePitch, &ptr, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "buffer", buffer, "blockingWrite", blockingWrite,
                   "bufferOrigin[0]", DebugManager.getInput(bufferOrigin, 0), "bufferOrigin[1]", DebugManager.getInput(bufferOrigin, 1), "bufferOrigin[2]", DebugManager.getInput(bufferOrigin, 2),
                   "hostOrigin[0]", DebugManager.getInput(hostOrigin, 0), "hostOrigin[1]", DebugManager.getInput(hostOrigin, 1), "hostOrigin[2]", DebugManager.getInput(hostOrigin, 2),
                   "region[0]", DebugManager.getInput(region, 0), "region[1]", DebugManager.getInput(region, 1), "region[2]", DebugManager.getInput(region, 2),
                   "bufferRowPitch", bufferRowPitch, "bufferSlicePitch", bufferSlicePitch,
                   "hostRowPitch", hostRowPitch, "hostSlicePitch", hostSlicePitch, "ptr", ptr,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    Buffer *pBuffer = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(buffer, &pBuffer),
        ptr);

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clEnqueueWriteBufferRect, &retVal);
        return retVal;
    }

    if (pBuffer->writeMemObjFlagsInvalid()) {
        retVal = CL_INVALID_OPERATION;
        TRACING_EXIT(clEnqueueWriteBufferRect, &retVal);
        return retVal;
    }

    if (pBuffer->bufferRectPitchSet(bufferOrigin,
                                    region,
                                    bufferRowPitch,
                                    bufferSlicePitch,
                                    hostRowPitch,
                                    hostSlicePitch) == false) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueWriteBufferRect, &retVal);
        return retVal;
    }

    retVal = pCommandQueue->enqueueWriteBufferRect(
        pBuffer,
        blockingWrite,
        bufferOrigin,
        hostOrigin,
        region,
        bufferRowPitch,
        bufferSlicePitch,
        hostRowPitch,
        hostSlicePitch,
        ptr,
        numEventsInWaitList,
        eventWaitList,
        event);

    TRACING_EXIT(clEnqueueWriteBufferRect, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueFillBuffer(cl_command_queue commandQueue,
                                       cl_mem buffer,
                                       const void *pattern,
                                       size_t patternSize,
                                       size_t offset,
                                       size_t size,
                                       cl_uint numEventsInWaitList,
                                       const cl_event *eventWaitList,
                                       cl_event *event) {
    TRACING_ENTER(clEnqueueFillBuffer, &commandQueue, &buffer, &pattern, &patternSize, &offset, &size, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "buffer", buffer,
                   "pattern", DebugManager.infoPointerToString(pattern, patternSize), "patternSize", patternSize,
                   "offset", offset, "size", size,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    Buffer *pBuffer = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(buffer, &pBuffer),
        pattern,
        (PatternSize)patternSize,
        EventWaitList(numEventsInWaitList, eventWaitList));

    if (CL_SUCCESS == retVal) {
        retVal = pCommandQueue->enqueueFillBuffer(
            pBuffer,
            pattern,
            patternSize,
            offset,
            size,
            numEventsInWaitList,
            eventWaitList,
            event);
    }
    TRACING_EXIT(clEnqueueFillBuffer, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueCopyBuffer(cl_command_queue commandQueue,
                                       cl_mem srcBuffer,
                                       cl_mem dstBuffer,
                                       size_t srcOffset,
                                       size_t dstOffset,
                                       size_t cb,
                                       cl_uint numEventsInWaitList,
                                       const cl_event *eventWaitList,
                                       cl_event *event) {
    TRACING_ENTER(clEnqueueCopyBuffer, &commandQueue, &srcBuffer, &dstBuffer, &srcOffset, &dstOffset, &cb, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "srcBuffer", srcBuffer, "dstBuffer", dstBuffer,
                   "srcOffset", srcOffset, "dstOffset", dstOffset, "cb", cb,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    Buffer *pSrcBuffer = nullptr;
    Buffer *pDstBuffer = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(srcBuffer, &pSrcBuffer),
        WithCastToInternal(dstBuffer, &pDstBuffer));

    if (CL_SUCCESS == retVal) {
        size_t srcSize = pSrcBuffer->getSize();
        size_t dstSize = pDstBuffer->getSize();
        if (srcOffset + cb > srcSize || dstOffset + cb > dstSize) {
            retVal = CL_INVALID_VALUE;
            TRACING_EXIT(clEnqueueCopyBuffer, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueCopyBuffer(
            pSrcBuffer,
            pDstBuffer,
            srcOffset,
            dstOffset,
            cb,
            numEventsInWaitList,
            eventWaitList,
            event);
    }
    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueCopyBuffer, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueCopyBufferRect(cl_command_queue commandQueue,
                                           cl_mem srcBuffer,
                                           cl_mem dstBuffer,
                                           const size_t *srcOrigin,
                                           const size_t *dstOrigin,
                                           const size_t *region,
                                           size_t srcRowPitch,
                                           size_t srcSlicePitch,
                                           size_t dstRowPitch,
                                           size_t dstSlicePitch,
                                           cl_uint numEventsInWaitList,
                                           const cl_event *eventWaitList,
                                           cl_event *event) {
    TRACING_ENTER(clEnqueueCopyBufferRect, &commandQueue, &srcBuffer, &dstBuffer, &srcOrigin, &dstOrigin, &region, &srcRowPitch, &srcSlicePitch, &dstRowPitch, &dstSlicePitch, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "srcBuffer", srcBuffer, "dstBuffer", dstBuffer,
                   "srcOrigin[0]", DebugManager.getInput(srcOrigin, 0), "srcOrigin[1]", DebugManager.getInput(srcOrigin, 1), "srcOrigin[2]", DebugManager.getInput(srcOrigin, 2),
                   "dstOrigin[0]", DebugManager.getInput(dstOrigin, 0), "dstOrigin[1]", DebugManager.getInput(dstOrigin, 1), "dstOrigin[2]", DebugManager.getInput(dstOrigin, 2),
                   "region[0]", DebugManager.getInput(region, 0), "region[1]", DebugManager.getInput(region, 1), "region[2]", DebugManager.getInput(region, 2),
                   "srcRowPitch", srcRowPitch, "srcSlicePitch", srcSlicePitch,
                   "dstRowPitch", dstRowPitch, "dstSlicePitch", dstSlicePitch,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    Buffer *pSrcBuffer = nullptr;
    Buffer *pDstBuffer = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(srcBuffer, &pSrcBuffer),
        WithCastToInternal(dstBuffer, &pDstBuffer));

    if (CL_SUCCESS == retVal) {
        retVal = pCommandQueue->enqueueCopyBufferRect(
            pSrcBuffer,
            pDstBuffer,
            srcOrigin,
            dstOrigin,
            region,
            srcRowPitch,
            srcSlicePitch,
            dstRowPitch,
            dstSlicePitch,
            numEventsInWaitList,
            eventWaitList,
            event);
    }
    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueCopyBufferRect, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueReadImage(cl_command_queue commandQueue,
                                      cl_mem image,
                                      cl_bool blockingRead,
                                      const size_t *origin,
                                      const size_t *region,
                                      size_t rowPitch,
                                      size_t slicePitch,
                                      void *ptr,
                                      cl_uint numEventsInWaitList,
                                      const cl_event *eventWaitList,
                                      cl_event *event) {
    TRACING_ENTER(clEnqueueReadImage, &commandQueue, &image, &blockingRead, &origin, &region, &rowPitch, &slicePitch, &ptr, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;
    Image *pImage = nullptr;

    auto retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(image, &pImage));

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "image", image, "blockingRead", blockingRead,
                   "origin[0]", DebugManager.getInput(origin, 0), "origin[1]", DebugManager.getInput(origin, 1), "origin[2]", DebugManager.getInput(origin, 2),
                   "region[0]", DebugManager.getInput(region, 0), "region[1]", DebugManager.getInput(region, 1), "region[2]", DebugManager.getInput(region, 2),
                   "rowPitch", rowPitch, "slicePitch", slicePitch, "ptr", ptr,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (CL_SUCCESS == retVal) {

        if (pImage->readMemObjFlagsInvalid()) {
            retVal = CL_INVALID_OPERATION;
            TRACING_EXIT(clEnqueueReadImage, &retVal);
            return retVal;
        }
        if (IsPackedYuvImage(&pImage->getImageFormat())) {
            retVal = validateYuvOperation(origin, region);
            if (retVal != CL_SUCCESS) {
                TRACING_EXIT(clEnqueueReadImage, &retVal);
                return retVal;
            }
        }
        retVal = Image::validateRegionAndOrigin(origin, region, pImage->getImageDesc());
        if (retVal != CL_SUCCESS) {
            TRACING_EXIT(clEnqueueReadImage, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueReadImage(
            pImage,
            blockingRead,
            origin,
            region,
            rowPitch,
            slicePitch,
            ptr,
            nullptr,
            numEventsInWaitList,
            eventWaitList,
            event);
    }
    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueReadImage, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueWriteImage(cl_command_queue commandQueue,
                                       cl_mem image,
                                       cl_bool blockingWrite,
                                       const size_t *origin,
                                       const size_t *region,
                                       size_t inputRowPitch,
                                       size_t inputSlicePitch,
                                       const void *ptr,
                                       cl_uint numEventsInWaitList,
                                       const cl_event *eventWaitList,
                                       cl_event *event) {
    TRACING_ENTER(clEnqueueWriteImage, &commandQueue, &image, &blockingWrite, &origin, &region, &inputRowPitch, &inputSlicePitch, &ptr, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;
    Image *pImage = nullptr;

    auto retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(image, &pImage));

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "image", image, "blockingWrite", blockingWrite,
                   "origin[0]", DebugManager.getInput(origin, 0), "origin[1]", DebugManager.getInput(origin, 1), "origin[2]", DebugManager.getInput(origin, 2),
                   "region[0]", DebugManager.getInput(region, 0), "region[1]", DebugManager.getInput(region, 1), "region[2]", DebugManager.getInput(region, 2),
                   "inputRowPitch", inputRowPitch, "inputSlicePitch", inputSlicePitch, "ptr", ptr,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (CL_SUCCESS == retVal) {
        if (pImage->writeMemObjFlagsInvalid()) {
            retVal = CL_INVALID_OPERATION;
            TRACING_EXIT(clEnqueueWriteImage, &retVal);
            return retVal;
        }
        if (IsPackedYuvImage(&pImage->getImageFormat())) {
            retVal = validateYuvOperation(origin, region);
            if (retVal != CL_SUCCESS) {
                TRACING_EXIT(clEnqueueWriteImage, &retVal);
                return retVal;
            }
        }
        retVal = Image::validateRegionAndOrigin(origin, region, pImage->getImageDesc());
        if (retVal != CL_SUCCESS) {
            TRACING_EXIT(clEnqueueWriteImage, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueWriteImage(
            pImage,
            blockingWrite,
            origin,
            region,
            inputRowPitch,
            inputSlicePitch,
            ptr,
            nullptr,
            numEventsInWaitList,
            eventWaitList,
            event);
    }
    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueWriteImage, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueFillImage(cl_command_queue commandQueue,
                                      cl_mem image,
                                      const void *fillColor,
                                      const size_t *origin,
                                      const size_t *region,
                                      cl_uint numEventsInWaitList,
                                      const cl_event *eventWaitList,
                                      cl_event *event) {
    TRACING_ENTER(clEnqueueFillImage, &commandQueue, &image, &fillColor, &origin, &region, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;
    Image *dstImage = nullptr;

    auto retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(image, &dstImage),
        fillColor,
        EventWaitList(numEventsInWaitList, eventWaitList));

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "image", image, "fillColor", fillColor,
                   "origin[0]", DebugManager.getInput(origin, 0), "origin[1]", DebugManager.getInput(origin, 1), "origin[2]", DebugManager.getInput(origin, 2),
                   "region[0]", DebugManager.getInput(region, 0), "region[1]", DebugManager.getInput(region, 1), "region[2]", DebugManager.getInput(region, 2),
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (CL_SUCCESS == retVal) {
        retVal = Image::validateRegionAndOrigin(origin, region, dstImage->getImageDesc());
        if (retVal != CL_SUCCESS) {
            TRACING_EXIT(clEnqueueFillImage, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueFillImage(
            dstImage,
            fillColor,
            origin,
            region,
            numEventsInWaitList,
            eventWaitList,
            event);
    }
    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueFillImage, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueCopyImage(cl_command_queue commandQueue,
                                      cl_mem srcImage,
                                      cl_mem dstImage,
                                      const size_t *srcOrigin,
                                      const size_t *dstOrigin,
                                      const size_t *region,
                                      cl_uint numEventsInWaitList,
                                      const cl_event *eventWaitList,
                                      cl_event *event) {
    TRACING_ENTER(clEnqueueCopyImage, &commandQueue, &srcImage, &dstImage, &srcOrigin, &dstOrigin, &region, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;
    Image *pSrcImage = nullptr;
    Image *pDstImage = nullptr;

    auto retVal = validateObjects(WithCastToInternal(commandQueue, &pCommandQueue),
                                  WithCastToInternal(srcImage, &pSrcImage),
                                  WithCastToInternal(dstImage, &pDstImage));

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "srcImage", srcImage, "dstImage", dstImage,
                   "srcOrigin[0]", DebugManager.getInput(srcOrigin, 0), "srcOrigin[1]", DebugManager.getInput(srcOrigin, 1), "srcOrigin[2]", DebugManager.getInput(srcOrigin, 2),
                   "dstOrigin[0]", DebugManager.getInput(dstOrigin, 0), "dstOrigin[1]", DebugManager.getInput(dstOrigin, 1), "dstOrigin[2]", DebugManager.getInput(dstOrigin, 2),
                   "region[0]", region ? region[0] : 0, "region[1]", region ? region[1] : 0, "region[2]", region ? region[2] : 0,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (CL_SUCCESS == retVal) {
        if (memcmp(&pSrcImage->getImageFormat(), &pDstImage->getImageFormat(), sizeof(cl_image_format))) {
            retVal = CL_IMAGE_FORMAT_MISMATCH;
            TRACING_EXIT(clEnqueueCopyImage, &retVal);
            return retVal;
        }
        if (IsPackedYuvImage(&pSrcImage->getImageFormat())) {
            retVal = validateYuvOperation(srcOrigin, region);
            if (retVal != CL_SUCCESS) {
                TRACING_EXIT(clEnqueueCopyImage, &retVal);
                return retVal;
            }
        }
        if (IsPackedYuvImage(&pDstImage->getImageFormat())) {
            retVal = validateYuvOperation(dstOrigin, region);

            if (retVal != CL_SUCCESS) {
                TRACING_EXIT(clEnqueueCopyImage, &retVal);
                return retVal;
            }
            if (pDstImage->getImageDesc().image_type == CL_MEM_OBJECT_IMAGE2D && dstOrigin[2] != 0) {
                retVal = CL_INVALID_VALUE;
                TRACING_EXIT(clEnqueueCopyImage, &retVal);
                return retVal;
            }
        }
        retVal = Image::validateRegionAndOrigin(srcOrigin, region, pSrcImage->getImageDesc());
        if (retVal != CL_SUCCESS) {
            TRACING_EXIT(clEnqueueCopyImage, &retVal);
            return retVal;
        }
        retVal = Image::validateRegionAndOrigin(dstOrigin, region, pDstImage->getImageDesc());
        if (retVal != CL_SUCCESS) {
            TRACING_EXIT(clEnqueueCopyImage, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueCopyImage(
            pSrcImage,
            pDstImage,
            srcOrigin,
            dstOrigin,
            region,
            numEventsInWaitList,
            eventWaitList,
            event);
    }
    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueCopyImage, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueCopyImageToBuffer(cl_command_queue commandQueue,
                                              cl_mem srcImage,
                                              cl_mem dstBuffer,
                                              const size_t *srcOrigin,
                                              const size_t *region,
                                              const size_t dstOffset,
                                              cl_uint numEventsInWaitList,
                                              const cl_event *eventWaitList,
                                              cl_event *event) {
    TRACING_ENTER(clEnqueueCopyImageToBuffer, &commandQueue, &srcImage, &dstBuffer, &srcOrigin, &region, (size_t *)&dstOffset, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "srcImage", srcImage, "dstBuffer", dstBuffer,
                   "srcOrigin[0]", DebugManager.getInput(srcOrigin, 0), "srcOrigin[1]", DebugManager.getInput(srcOrigin, 1), "srcOrigin[2]", DebugManager.getInput(srcOrigin, 2),
                   "region[0]", DebugManager.getInput(region, 0), "region[1]", DebugManager.getInput(region, 1), "region[2]", DebugManager.getInput(region, 2),
                   "dstOffset", dstOffset,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    Image *pSrcImage = nullptr;
    Buffer *pDstBuffer = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(srcImage, &pSrcImage),
        WithCastToInternal(dstBuffer, &pDstBuffer));

    if (CL_SUCCESS == retVal) {
        if (IsPackedYuvImage(&pSrcImage->getImageFormat())) {
            retVal = validateYuvOperation(srcOrigin, region);
            if (retVal != CL_SUCCESS) {
                TRACING_EXIT(clEnqueueCopyImageToBuffer, &retVal);
                return retVal;
            }
        }
        retVal = Image::validateRegionAndOrigin(srcOrigin, region, pSrcImage->getImageDesc());
        if (retVal != CL_SUCCESS) {
            TRACING_EXIT(clEnqueueCopyImageToBuffer, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueCopyImageToBuffer(
            pSrcImage,
            pDstBuffer,
            srcOrigin,
            region,
            dstOffset,
            numEventsInWaitList,
            eventWaitList,
            event);
    }

    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueCopyImageToBuffer, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueCopyBufferToImage(cl_command_queue commandQueue,
                                              cl_mem srcBuffer,
                                              cl_mem dstImage,
                                              size_t srcOffset,
                                              const size_t *dstOrigin,
                                              const size_t *region,
                                              cl_uint numEventsInWaitList,
                                              const cl_event *eventWaitList,
                                              cl_event *event) {
    TRACING_ENTER(clEnqueueCopyBufferToImage, &commandQueue, &srcBuffer, &dstImage, &srcOffset, &dstOrigin, &region, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "srcBuffer", srcBuffer, "dstImage", dstImage, "srcOffset", srcOffset,
                   "dstOrigin[0]", DebugManager.getInput(dstOrigin, 0), "dstOrigin[1]", DebugManager.getInput(dstOrigin, 1), "dstOrigin[2]", DebugManager.getInput(dstOrigin, 2),
                   "region[0]", DebugManager.getInput(region, 0), "region[1]", DebugManager.getInput(region, 1), "region[2]", DebugManager.getInput(region, 2),
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    Buffer *pSrcBuffer = nullptr;
    Image *pDstImage = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(srcBuffer, &pSrcBuffer),
        WithCastToInternal(dstImage, &pDstImage));

    if (CL_SUCCESS == retVal) {
        if (IsPackedYuvImage(&pDstImage->getImageFormat())) {
            retVal = validateYuvOperation(dstOrigin, region);
            if (retVal != CL_SUCCESS) {
                TRACING_EXIT(clEnqueueCopyBufferToImage, &retVal);
                return retVal;
            }
        }
        retVal = Image::validateRegionAndOrigin(dstOrigin, region, pDstImage->getImageDesc());
        if (retVal != CL_SUCCESS) {
            TRACING_EXIT(clEnqueueCopyBufferToImage, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueCopyBufferToImage(
            pSrcBuffer,
            pDstImage,
            srcOffset,
            dstOrigin,
            region,
            numEventsInWaitList,
            eventWaitList,
            event);
    }

    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueCopyBufferToImage, &retVal);
    return retVal;
}

void *CL_API_CALL clEnqueueMapBuffer(cl_command_queue commandQueue,
                                     cl_mem buffer,
                                     cl_bool blockingMap,
                                     cl_map_flags mapFlags,
                                     size_t offset,
                                     size_t cb,
                                     cl_uint numEventsInWaitList,
                                     const cl_event *eventWaitList,
                                     cl_event *event,
                                     cl_int *errcodeRet) {
    TRACING_ENTER(clEnqueueMapBuffer, &commandQueue, &buffer, &blockingMap, &mapFlags, &offset, &cb, &numEventsInWaitList, &eventWaitList, &event, &errcodeRet);
    void *retPtr = nullptr;
    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);
    cl_int retVal;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue, "buffer", buffer, "blockingMap", blockingMap,
                   "mapFlags", mapFlags, "offset", offset, "cb", cb,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    do {
        auto pCommandQueue = castToObject<CommandQueue>(commandQueue);
        if (!pCommandQueue) {
            retVal = CL_INVALID_COMMAND_QUEUE;
            break;
        }

        auto pBuffer = castToObject<Buffer>(buffer);
        if (!pBuffer) {
            retVal = CL_INVALID_MEM_OBJECT;
            break;
        }

        if (pBuffer->mapMemObjFlagsInvalid(mapFlags)) {
            retVal = CL_INVALID_OPERATION;
            break;
        }

        retPtr = pCommandQueue->enqueueMapBuffer(
            pBuffer,
            blockingMap,
            mapFlags,
            offset,
            cb,
            numEventsInWaitList,
            eventWaitList,
            event,
            retVal);

    } while (false);

    err.set(retVal);
    DBG_LOG_INPUTS("retPtr", retPtr, "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));

    TRACING_EXIT(clEnqueueMapBuffer, &retPtr);
    return retPtr;
}

void *CL_API_CALL clEnqueueMapImage(cl_command_queue commandQueue,
                                    cl_mem image,
                                    cl_bool blockingMap,
                                    cl_map_flags mapFlags,
                                    const size_t *origin,
                                    const size_t *region,
                                    size_t *imageRowPitch,
                                    size_t *imageSlicePitch,
                                    cl_uint numEventsInWaitList,
                                    const cl_event *eventWaitList,
                                    cl_event *event,
                                    cl_int *errcodeRet) {
    TRACING_ENTER(clEnqueueMapImage, &commandQueue, &image, &blockingMap, &mapFlags, &origin, &region, &imageRowPitch, &imageSlicePitch, &numEventsInWaitList, &eventWaitList, &event, &errcodeRet);

    void *retPtr = nullptr;
    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);
    cl_int retVal;

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue, "image", image,
                   "blockingMap", blockingMap, "mapFlags", mapFlags,
                   "origin[0]", DebugManager.getInput(origin, 0), "origin[1]", DebugManager.getInput(origin, 1),
                   "origin[2]", DebugManager.getInput(origin, 2), "region[0]", DebugManager.getInput(region, 0),
                   "region[1]", DebugManager.getInput(region, 1), "region[2]", DebugManager.getInput(region, 2),
                   "imageRowPitch", DebugManager.getInput(imageRowPitch, 0),
                   "imageSlicePitch", DebugManager.getInput(imageSlicePitch, 0),
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    do {
        Image *pImage = nullptr;
        CommandQueue *pCommandQueue = nullptr;
        retVal = validateObjects(
            WithCastToInternal(commandQueue, &pCommandQueue),
            WithCastToInternal(image, &pImage));

        if (retVal != CL_SUCCESS) {
            break;
        }

        if (pImage->mapMemObjFlagsInvalid(mapFlags)) {
            retVal = CL_INVALID_OPERATION;
            break;
        }
        if (IsPackedYuvImage(&pImage->getImageFormat())) {
            retVal = validateYuvOperation(origin, region);
            if (retVal != CL_SUCCESS) {
                break;
            }
        }

        retVal = Image::validateRegionAndOrigin(origin, region, pImage->getImageDesc());
        if (retVal != CL_SUCCESS) {
            break;
        }

        retPtr = pCommandQueue->enqueueMapImage(
            pImage,
            blockingMap,
            mapFlags,
            origin,
            region,
            imageRowPitch,
            imageSlicePitch,
            numEventsInWaitList,
            eventWaitList,
            event,
            retVal);

    } while (false);

    err.set(retVal);
    DBG_LOG_INPUTS("retPtr", retPtr, "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));

    TRACING_EXIT(clEnqueueMapImage, &retPtr);
    return retPtr;
}

cl_int CL_API_CALL clEnqueueUnmapMemObject(cl_command_queue commandQueue,
                                           cl_mem memObj,
                                           void *mappedPtr,
                                           cl_uint numEventsInWaitList,
                                           const cl_event *eventWaitList,
                                           cl_event *event) {
    TRACING_ENTER(clEnqueueUnmapMemObject, &commandQueue, &memObj, &mappedPtr, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;
    MemObj *pMemObj = nullptr;

    cl_int retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        WithCastToInternal(memObj, &pMemObj));

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "memObj", memObj,
                   "mappedPtr", mappedPtr,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (retVal == CL_SUCCESS) {
        if (pMemObj->peekClMemObjType() == CL_MEM_OBJECT_PIPE) {
            retVal = CL_INVALID_MEM_OBJECT;
            TRACING_EXIT(clEnqueueUnmapMemObject, &retVal);
            return retVal;
        }

        retVal = pCommandQueue->enqueueUnmapMemObject(pMemObj, mappedPtr, numEventsInWaitList, eventWaitList, event);
    }

    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueUnmapMemObject, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueMigrateMemObjects(cl_command_queue commandQueue,
                                              cl_uint numMemObjects,
                                              const cl_mem *memObjects,
                                              cl_mem_migration_flags flags,
                                              cl_uint numEventsInWaitList,
                                              const cl_event *eventWaitList,
                                              cl_event *event) {
    TRACING_ENTER(clEnqueueMigrateMemObjects, &commandQueue, &numMemObjects, &memObjects, &flags, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "numMemObjects", numMemObjects,
                   "memObjects", memObjects,
                   "flags", flags,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        EventWaitList(numEventsInWaitList, eventWaitList));

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clEnqueueMigrateMemObjects, &retVal);
        return retVal;
    }

    if (numMemObjects == 0 || memObjects == nullptr) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueMigrateMemObjects, &retVal);
        return retVal;
    }

    const cl_mem_migration_flags allValidFlags = CL_MIGRATE_MEM_OBJECT_CONTENT_UNDEFINED | CL_MIGRATE_MEM_OBJECT_HOST;

    if ((flags & (~allValidFlags)) != 0) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueMigrateMemObjects, &retVal);
        return retVal;
    }

    retVal = pCommandQueue->enqueueMigrateMemObjects(numMemObjects,
                                                     memObjects,
                                                     flags,
                                                     numEventsInWaitList,
                                                     eventWaitList,
                                                     event);
    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueMigrateMemObjects, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueNDRangeKernel(cl_command_queue commandQueue,
                                          cl_kernel kernel,
                                          cl_uint workDim,
                                          const size_t *globalWorkOffset,
                                          const size_t *globalWorkSize,
                                          const size_t *localWorkSize,
                                          cl_uint numEventsInWaitList,
                                          const cl_event *eventWaitList,
                                          cl_event *event) {
    TRACING_ENTER(clEnqueueNDRangeKernel, &commandQueue, &kernel, &workDim, &globalWorkOffset, &globalWorkSize, &localWorkSize, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue, "cl_kernel", kernel,
                   "globalWorkOffset[0]", DebugManager.getInput(globalWorkOffset, 0),
                   "globalWorkOffset[1]", DebugManager.getInput(globalWorkOffset, 1),
                   "globalWorkOffset[2]", DebugManager.getInput(globalWorkOffset, 2),
                   "globalWorkSize", DebugManager.getSizes(globalWorkSize, workDim, false),
                   "localWorkSize", DebugManager.getSizes(localWorkSize, workDim, true),
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        kernel,
        EventWaitList(numEventsInWaitList, eventWaitList));

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clEnqueueNDRangeKernel, &retVal);
        return retVal;
    }

    auto pKernel = castToObjectOrAbort<Kernel>(kernel);
    TakeOwnershipWrapper<Kernel> kernelOwnership(*pKernel, gtpinIsGTPinInitialized());
    if (gtpinIsGTPinInitialized()) {
        gtpinNotifyKernelSubmit(kernel, pCommandQueue);
    }

    retVal = pCommandQueue->enqueueKernel(
        kernel,
        workDim,
        globalWorkOffset,
        globalWorkSize,
        localWorkSize,
        numEventsInWaitList,
        eventWaitList,
        event);

    DBG_LOG_INPUTS("event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1u));
    TRACING_EXIT(clEnqueueNDRangeKernel, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueTask(cl_command_queue commandQueue,
                                 cl_kernel kernel,
                                 cl_uint numEventsInWaitList,
                                 const cl_event *eventWaitList,
                                 cl_event *event) {
    TRACING_ENTER(clEnqueueTask, &commandQueue, &kernel, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue, "kernel", kernel,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));
    cl_uint workDim = 3;
    size_t *globalWorkOffset = nullptr;
    size_t globalWorkSize[3] = {1, 1, 1};
    size_t localWorkSize[3] = {1, 1, 1};
    retVal = (clEnqueueNDRangeKernel(
        commandQueue,
        kernel,
        workDim,
        globalWorkOffset,
        globalWorkSize,
        localWorkSize,
        numEventsInWaitList,
        eventWaitList,
        event));
    TRACING_EXIT(clEnqueueTask, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueNativeKernel(cl_command_queue commandQueue,
                                         void(CL_CALLBACK *userFunc)(void *),
                                         void *args,
                                         size_t cbArgs,
                                         cl_uint numMemObjects,
                                         const cl_mem *memList,
                                         const void **argsMemLoc,
                                         cl_uint numEventsInWaitList,
                                         const cl_event *eventWaitList,
                                         cl_event *event) {
    TRACING_ENTER(clEnqueueNativeKernel, &commandQueue, &userFunc, &args, &cbArgs, &numMemObjects, &memList, &argsMemLoc, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_OUT_OF_HOST_MEMORY;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue, "userFunc", userFunc, "args", args,
                   "cbArgs", cbArgs, "numMemObjects", numMemObjects, "memList", memList, "argsMemLoc", argsMemLoc,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    TRACING_EXIT(clEnqueueNativeKernel, &retVal);
    return retVal;
}

// deprecated OpenCL 1.1
cl_int CL_API_CALL clEnqueueMarker(cl_command_queue commandQueue,
                                   cl_event *event) {
    TRACING_ENTER(clEnqueueMarker, &commandQueue, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue, "cl_event", event);

    auto pCommandQueue = castToObject<CommandQueue>(commandQueue);
    if (pCommandQueue) {
        retVal = pCommandQueue->enqueueMarkerWithWaitList(
            0,
            nullptr,
            event);
        TRACING_EXIT(clEnqueueMarker, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_COMMAND_QUEUE;
    TRACING_EXIT(clEnqueueMarker, &retVal);
    return retVal;
}

// deprecated OpenCL 1.1
cl_int CL_API_CALL clEnqueueWaitForEvents(cl_command_queue commandQueue,
                                          cl_uint numEvents,
                                          const cl_event *eventList) {
    TRACING_ENTER(clEnqueueWaitForEvents, &commandQueue, &numEvents, &eventList);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue, "eventList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventList), numEvents));

    auto pCommandQueue = castToObject<CommandQueue>(commandQueue);
    if (!pCommandQueue) {
        retVal = CL_INVALID_COMMAND_QUEUE;
        TRACING_EXIT(clEnqueueWaitForEvents, &retVal);
        return retVal;
    }
    for (unsigned int i = 0; i < numEvents && retVal == CL_SUCCESS; i++) {
        retVal = validateObjects(eventList[i]);
    }

    if (retVal != CL_SUCCESS) {
        TRACING_EXIT(clEnqueueWaitForEvents, &retVal);
        return retVal;
    }

    retVal = Event::waitForEvents(numEvents, eventList);

    TRACING_EXIT(clEnqueueWaitForEvents, &retVal);
    return retVal;
}

// deprecated OpenCL 1.1
cl_int CL_API_CALL clEnqueueBarrier(cl_command_queue commandQueue) {
    TRACING_ENTER(clEnqueueBarrier, &commandQueue);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue);
    auto pCommandQueue = castToObject<CommandQueue>(commandQueue);
    if (pCommandQueue) {
        retVal = pCommandQueue->enqueueBarrierWithWaitList(
            0,
            nullptr,
            nullptr);
        TRACING_EXIT(clEnqueueBarrier, &retVal);
        return retVal;
    }
    retVal = CL_INVALID_COMMAND_QUEUE;
    TRACING_EXIT(clEnqueueBarrier, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueMarkerWithWaitList(cl_command_queue commandQueue,
                                               cl_uint numEventsInWaitList,
                                               const cl_event *eventWaitList,
                                               cl_event *event) {
    TRACING_ENTER(clEnqueueMarkerWithWaitList, &commandQueue, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DebugManager.logInputs("cl_command_queue", commandQueue,
                           "numEventsInWaitList", numEventsInWaitList,
                           "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                           "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        EventWaitList(numEventsInWaitList, eventWaitList));

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clEnqueueMarkerWithWaitList, &retVal);
        return retVal;
    }

    retVal = pCommandQueue->enqueueMarkerWithWaitList(
        numEventsInWaitList,
        eventWaitList,
        event);
    TRACING_EXIT(clEnqueueMarkerWithWaitList, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueBarrierWithWaitList(cl_command_queue commandQueue,
                                                cl_uint numEventsInWaitList,
                                                const cl_event *eventWaitList,
                                                cl_event *event) {
    TRACING_ENTER(clEnqueueBarrierWithWaitList, &commandQueue, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("cl_command_queue", commandQueue,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;

    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        EventWaitList(numEventsInWaitList, eventWaitList));

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clEnqueueBarrierWithWaitList, &retVal);
        return retVal;
    }
    retVal = pCommandQueue->enqueueBarrierWithWaitList(
        numEventsInWaitList,
        eventWaitList,
        event);
    TRACING_EXIT(clEnqueueBarrierWithWaitList, &retVal);
    return retVal;
}

CL_API_ENTRY cl_command_queue CL_API_CALL
clCreatePerfCountersCommandQueueINTEL(
    cl_context context,
    cl_device_id device,
    cl_command_queue_properties properties,
    cl_uint configuration,
    cl_int *errcodeRet) {
    API_ENTER(nullptr);

    DBG_LOG_INPUTS("context", context,
                   "device", device,
                   "properties", properties,
                   "configuration", configuration);

    cl_command_queue commandQueue = nullptr;
    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);

    Device *pDevice = nullptr;
    WithCastToInternal(device, &pDevice);
    if (pDevice == nullptr) {
        err.set(CL_INVALID_DEVICE);
        return commandQueue;
    }
    if (!pDevice->getHardwareInfo().capabilityTable.instrumentationEnabled) {
        err.set(CL_INVALID_DEVICE);
        return commandQueue;
    }

    if ((properties & CL_QUEUE_PROFILING_ENABLE) == 0) {
        err.set(CL_INVALID_QUEUE_PROPERTIES);
        return commandQueue;
    }
    if ((properties & CL_QUEUE_ON_DEVICE) != 0) {
        err.set(CL_INVALID_QUEUE_PROPERTIES);
        return commandQueue;
    }
    if ((properties & CL_QUEUE_ON_DEVICE_DEFAULT) != 0) {
        err.set(CL_INVALID_QUEUE_PROPERTIES);
        return commandQueue;
    }
    commandQueue = clCreateCommandQueue(context, device, properties, errcodeRet);
    if (commandQueue != nullptr) {
        auto commandQueueObject = castToObjectOrAbort<CommandQueue>(commandQueue);
        bool ret = commandQueueObject->setPerfCountersEnabled(true, configuration);
        if (!ret) {
            clReleaseCommandQueue(commandQueue);
            commandQueue = nullptr;
            err.set(CL_OUT_OF_RESOURCES);
        }
    }
    return commandQueue;
}

CL_API_ENTRY cl_int CL_API_CALL
clSetPerformanceConfigurationINTEL(
    cl_device_id device,
    cl_uint count,
    cl_uint *offsets,
    cl_uint *values) {
    // Not supported, covered by Metric Library DLL.
    return CL_INVALID_OPERATION;
}

void *clHostMemAllocINTEL(
    cl_context context,
    cl_mem_properties_intel *properties,
    size_t size,
    cl_uint alignment,
    cl_int *errcodeRet) {

    Context *neoContext = nullptr;

    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);

    auto retVal = validateObjects(WithCastToInternal(context, &neoContext));

    if (retVal != CL_SUCCESS) {
        err.set(retVal);
        return nullptr;
    }

    if (size > neoContext->getDevice(0u)->getDeviceInfo().maxMemAllocSize) {
        err.set(CL_INVALID_BUFFER_SIZE);
        return nullptr;
    }

    SVMAllocsManager::UnifiedMemoryProperties unifiedMemoryProperties(InternalMemoryType::HOST_UNIFIED_MEMORY);
    if (!MemObjHelper::parseUnifiedMemoryProperties(properties, unifiedMemoryProperties)) {
        err.set(CL_INVALID_VALUE);
        return nullptr;
    }

    return neoContext->getSVMAllocsManager()->createUnifiedMemoryAllocation(neoContext->getDevice(0)->getRootDeviceIndex(), size, unifiedMemoryProperties);
}

void *clDeviceMemAllocINTEL(
    cl_context context,
    cl_device_id device,
    cl_mem_properties_intel *properties,
    size_t size,
    cl_uint alignment,
    cl_int *errcodeRet) {
    Context *neoContext = nullptr;
    Device *neoDevice = nullptr;

    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);

    auto retVal = validateObjects(WithCastToInternal(context, &neoContext), WithCastToInternal(device, &neoDevice));

    if (retVal != CL_SUCCESS) {
        err.set(retVal);
        return nullptr;
    }

    if (size > neoDevice->getDeviceInfo().maxMemAllocSize) {
        err.set(CL_INVALID_BUFFER_SIZE);
        return nullptr;
    }

    SVMAllocsManager::UnifiedMemoryProperties unifiedMemoryProperties(InternalMemoryType::DEVICE_UNIFIED_MEMORY);
    if (!MemObjHelper::parseUnifiedMemoryProperties(properties, unifiedMemoryProperties)) {
        err.set(CL_INVALID_VALUE);
        return nullptr;
    }

    return neoContext->getSVMAllocsManager()->createUnifiedMemoryAllocation(neoDevice->getRootDeviceIndex(), size, unifiedMemoryProperties);
}

void *clSharedMemAllocINTEL(
    cl_context context,
    cl_device_id device,
    cl_mem_properties_intel *properties,
    size_t size,
    cl_uint alignment,
    cl_int *errcodeRet) {
    Context *neoContext = nullptr;
    Device *neoDevice = nullptr;

    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);

    auto retVal = validateObjects(WithCastToInternal(context, &neoContext), WithCastToInternal(device, &neoDevice));

    if (retVal != CL_SUCCESS) {
        err.set(retVal);
        return nullptr;
    }

    if (size > neoDevice->getDeviceInfo().maxMemAllocSize) {
        err.set(CL_INVALID_BUFFER_SIZE);
        return nullptr;
    }

    SVMAllocsManager::UnifiedMemoryProperties unifiedMemoryProperties(InternalMemoryType::SHARED_UNIFIED_MEMORY);
    if (!MemObjHelper::parseUnifiedMemoryProperties(properties, unifiedMemoryProperties)) {
        err.set(CL_INVALID_VALUE);
        return nullptr;
    }

    return neoContext->getSVMAllocsManager()->createSharedUnifiedMemoryAllocation(neoContext->getDevice(0)->getRootDeviceIndex(), size, unifiedMemoryProperties, neoContext->getSpecialQueue());
}

cl_int clMemFreeINTEL(
    cl_context context,
    const void *ptr) {

    Context *neoContext = nullptr;
    auto retVal = validateObjects(WithCastToInternal(context, &neoContext));

    if (retVal != CL_SUCCESS) {
        return retVal;
    }

    if (ptr && !neoContext->getSVMAllocsManager()->freeSVMAlloc(const_cast<void *>(ptr))) {
        return CL_INVALID_VALUE;
    }

    if (neoContext->getSVMAllocsManager()->getSvmMapOperation(ptr)) {
        neoContext->getSVMAllocsManager()->removeSvmMapOperation(ptr);
    }

    return CL_SUCCESS;
}

cl_int clGetMemAllocInfoINTEL(
    cl_context context,
    const void *ptr,
    cl_mem_info_intel paramName,
    size_t paramValueSize,
    void *paramValue,
    size_t *paramValueSizeRet) {
    Context *pContext = nullptr;
    cl_int retVal = CL_SUCCESS;
    retVal = validateObject(WithCastToInternal(context, &pContext));

    if (!pContext) {
        return retVal;
    }

    auto allocationsManager = pContext->getSVMAllocsManager();
    if (!allocationsManager) {
        return CL_INVALID_VALUE;
    }

    auto unifiedMemoryAllocation = allocationsManager->getSVMAlloc(ptr);
    if (!unifiedMemoryAllocation && paramName != CL_MEM_ALLOC_TYPE_INTEL) {
        return CL_INVALID_VALUE;
    }

    GetInfoHelper info(paramValue, paramValueSize, paramValueSizeRet);
    switch (paramName) {
    case CL_MEM_ALLOC_TYPE_INTEL: {
        if (!unifiedMemoryAllocation) {
            retVal = info.set<cl_int>(CL_MEM_TYPE_UNKNOWN_INTEL);
            return retVal;
        } else if (unifiedMemoryAllocation->memoryType == InternalMemoryType::HOST_UNIFIED_MEMORY) {
            retVal = info.set<cl_int>(CL_MEM_TYPE_HOST_INTEL);
            return retVal;
        } else if (unifiedMemoryAllocation->memoryType == InternalMemoryType::DEVICE_UNIFIED_MEMORY) {
            retVal = info.set<cl_int>(CL_MEM_TYPE_DEVICE_INTEL);
            return retVal;
        } else {
            retVal = info.set<cl_int>(CL_MEM_TYPE_SHARED_INTEL);
            return retVal;
        }
        break;
    }
    case CL_MEM_ALLOC_BASE_PTR_INTEL: {
        retVal = info.set<uint64_t>(unifiedMemoryAllocation->gpuAllocation->getGpuAddress());
        return retVal;
    }
    case CL_MEM_ALLOC_SIZE_INTEL: {
        retVal = info.set<size_t>(unifiedMemoryAllocation->size);
        return retVal;
    }
    case CL_MEM_ALLOC_FLAGS_INTEL: {
        retVal = info.set<uint64_t>(unifiedMemoryAllocation->allocationFlagsProperty);
        return retVal;
    }
    default: {
    }
    }

    return CL_INVALID_VALUE;
}

cl_int clSetKernelArgMemPointerINTEL(
    cl_kernel kernel,
    cl_uint argIndex,
    const void *argValue) {
    return clSetKernelArgSVMPointer(kernel, argIndex, argValue);
}

cl_int clEnqueueMemsetINTEL(
    cl_command_queue commandQueue,
    void *dstPtr,
    cl_int value,
    size_t size,
    cl_uint numEventsInWaitList,
    const cl_event *eventWaitList,
    cl_event *event) {
    auto retVal = clEnqueueSVMMemFill(commandQueue,
                                      dstPtr,
                                      &value,
                                      1u,
                                      size,
                                      numEventsInWaitList,
                                      eventWaitList,
                                      event);
    if (retVal == CL_SUCCESS && event) {
        auto pEvent = castToObjectOrAbort<Event>(*event);
        pEvent->setCmdType(CL_COMMAND_MEMSET_INTEL);
    }

    return retVal;
}

cl_int clEnqueueMemFillINTEL(
    cl_command_queue commandQueue,
    void *dstPtr,
    const void *pattern,
    size_t patternSize,
    size_t size,
    cl_uint numEventsInWaitList,
    const cl_event *eventWaitList,
    cl_event *event) {

    auto retVal = clEnqueueSVMMemFill(commandQueue,
                                      dstPtr,
                                      pattern,
                                      patternSize,
                                      size,
                                      numEventsInWaitList,
                                      eventWaitList,
                                      event);
    if (retVal == CL_SUCCESS && event) {
        auto pEvent = castToObjectOrAbort<Event>(*event);
        pEvent->setCmdType(CL_COMMAND_MEMFILL_INTEL);
    }

    return retVal;
}

cl_int clEnqueueMemcpyINTEL(
    cl_command_queue commandQueue,
    cl_bool blocking,
    void *dstPtr,
    const void *srcPtr,
    size_t size,
    cl_uint numEventsInWaitList,
    const cl_event *eventWaitList,
    cl_event *event) {
    auto retVal = clEnqueueSVMMemcpy(commandQueue,
                                     blocking,
                                     dstPtr,
                                     srcPtr,
                                     size,
                                     numEventsInWaitList,
                                     eventWaitList,
                                     event);
    if (retVal == CL_SUCCESS && event) {
        auto pEvent = castToObjectOrAbort<Event>(*event);
        pEvent->setCmdType(CL_COMMAND_MEMCPY_INTEL);
    }

    return retVal;
}

cl_int clEnqueueMigrateMemINTEL(
    cl_command_queue commandQueue,
    const void *ptr,
    size_t size,
    cl_mem_migration_flags flags,
    cl_uint numEventsInWaitList,
    const cl_event *eventWaitList,
    cl_event *event) {
    cl_int retVal = CL_SUCCESS;

    CommandQueue *pCommandQueue = nullptr;
    retVal = validateObjects(WithCastToInternal(commandQueue, &pCommandQueue), ptr, EventWaitList(numEventsInWaitList, eventWaitList));

    if (retVal == CL_SUCCESS) {
        pCommandQueue->enqueueMarkerWithWaitList(numEventsInWaitList, eventWaitList, event);

        if (event) {
            auto pEvent = castToObjectOrAbort<Event>(*event);
            pEvent->setCmdType(CL_COMMAND_MIGRATEMEM_INTEL);
        }
    }

    return retVal;
}

cl_int clEnqueueMemAdviseINTEL(
    cl_command_queue commandQueue,
    const void *ptr,
    size_t size,
    cl_mem_advice_intel advice,
    cl_uint numEventsInWaitList,
    const cl_event *eventWaitList,
    cl_event *event) {
    cl_int retVal = CL_SUCCESS;

    CommandQueue *pCommandQueue = nullptr;
    retVal = validateObjects(WithCastToInternal(commandQueue, &pCommandQueue), ptr, EventWaitList(numEventsInWaitList, eventWaitList));

    if (retVal == CL_SUCCESS) {
        pCommandQueue->enqueueMarkerWithWaitList(numEventsInWaitList, eventWaitList, event);

        if (event) {
            auto pEvent = castToObjectOrAbort<Event>(*event);
            pEvent->setCmdType(CL_COMMAND_MEMADVISE_INTEL);
        }
    }

    return retVal;
}

cl_command_queue CL_API_CALL clCreateCommandQueueWithPropertiesKHR(cl_context context,
                                                                   cl_device_id device,
                                                                   const cl_queue_properties_khr *properties,
                                                                   cl_int *errcodeRet) {

    API_ENTER(errcodeRet);
    DBG_LOG_INPUTS("context", context,
                   "device", device,
                   "properties", properties);

    return clCreateCommandQueueWithProperties(context, device, properties, errcodeRet);
}

cl_accelerator_intel CL_API_CALL clCreateAcceleratorINTEL(
    cl_context context,
    cl_accelerator_type_intel acceleratorType,
    size_t descriptorSize,
    const void *descriptor,
    cl_int *errcodeRet) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "acceleratorType", acceleratorType,
                   "descriptorSize", descriptorSize,
                   "descriptor", DebugManager.infoPointerToString(descriptor, descriptorSize));
    cl_accelerator_intel accelerator = nullptr;

    do {
        retVal = validateObjects(context);

        if (retVal != CL_SUCCESS) {
            retVal = CL_INVALID_CONTEXT;
            break;
        }

        Context *pContext = castToObject<Context>(context);

        DEBUG_BREAK_IF(!pContext);

        switch (acceleratorType) {
        case CL_ACCELERATOR_TYPE_MOTION_ESTIMATION_INTEL:
            accelerator = VmeAccelerator::create(
                pContext,
                acceleratorType,
                descriptorSize,
                descriptor,
                retVal);
            break;
        default:
            retVal = CL_INVALID_ACCELERATOR_TYPE_INTEL;
        }

    } while (false);

    if (errcodeRet) {
        *errcodeRet = retVal;
    }

    return accelerator;
}

cl_int CL_API_CALL clRetainAcceleratorINTEL(
    cl_accelerator_intel accelerator) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("accelerator", accelerator);

    IntelAccelerator *pAccelerator = nullptr;

    do {
        pAccelerator = castToObject<IntelAccelerator>(accelerator);

        if (!pAccelerator) {
            retVal = CL_INVALID_ACCELERATOR_INTEL;
            break;
        }

        pAccelerator->retain();
    } while (false);

    return retVal;
}

cl_int CL_API_CALL clGetAcceleratorInfoINTEL(
    cl_accelerator_intel accelerator,
    cl_accelerator_info_intel paramName,
    size_t paramValueSize,
    void *paramValue,
    size_t *paramValueSizeRet) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("accelerator", accelerator,
                   "paramName", paramName,
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);
    IntelAccelerator *pAccelerator = nullptr;

    do {
        pAccelerator = castToObject<IntelAccelerator>(accelerator);

        if (!pAccelerator) {
            retVal = CL_INVALID_ACCELERATOR_INTEL;
            break;
        }

        retVal = pAccelerator->getInfo(
            paramName, paramValueSize, paramValue, paramValueSizeRet);

    } while (false);

    return retVal;
}

cl_int CL_API_CALL clReleaseAcceleratorINTEL(
    cl_accelerator_intel accelerator) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("accelerator", accelerator);

    IntelAccelerator *pAccelerator = nullptr;

    do {
        pAccelerator = castToObject<IntelAccelerator>(accelerator);

        if (!pAccelerator) {
            retVal = CL_INVALID_ACCELERATOR_INTEL;
            break;
        }

        pAccelerator->release();
    } while (false);

    return retVal;
}

cl_program CL_API_CALL clCreateProgramWithILKHR(cl_context context,
                                                const void *il,
                                                size_t length,
                                                cl_int *errcodeRet) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "il", DebugManager.infoPointerToString(il, length),
                   "length", length);

    cl_program program = nullptr;
    retVal = validateObjects(context, il);
    if (retVal == CL_SUCCESS) {
        program = Program::createFromIL(
            castToObjectOrAbort<Context>(context),
            il,
            length,
            retVal);
    }

    if (errcodeRet != nullptr) {
        *errcodeRet = retVal;
    }

    return program;
}

#define RETURN_FUNC_PTR_IF_EXIST(name)                                   \
    {                                                                    \
        if (!strcmp(funcName, #name)) {                                  \
            TRACING_EXIT(clGetExtensionFunctionAddress, (void **)&name); \
            return ((void *)(name));                                     \
        }                                                                \
    }
void *CL_API_CALL clGetExtensionFunctionAddress(const char *funcName) {
    TRACING_ENTER(clGetExtensionFunctionAddress, &funcName);

    DBG_LOG_INPUTS("funcName", funcName);
    // Support an internal call by the ICD
    RETURN_FUNC_PTR_IF_EXIST(clIcdGetPlatformIDsKHR);

    //perf counters
    RETURN_FUNC_PTR_IF_EXIST(clCreatePerfCountersCommandQueueINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clSetPerformanceConfigurationINTEL);
    // Support device extensions
    RETURN_FUNC_PTR_IF_EXIST(clCreateAcceleratorINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clGetAcceleratorInfoINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clRetainAcceleratorINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clReleaseAcceleratorINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clCreateBufferWithPropertiesINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clCreateImageWithPropertiesINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clAddCommentINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clEnqueueVerifyMemoryINTEL);

    RETURN_FUNC_PTR_IF_EXIST(clCreateTracingHandleINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clSetTracingPointINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clDestroyTracingHandleINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clEnableTracingINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clDisableTracingINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clGetTracingStateINTEL);

    RETURN_FUNC_PTR_IF_EXIST(clHostMemAllocINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clDeviceMemAllocINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clSharedMemAllocINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clMemFreeINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clGetMemAllocInfoINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clSetKernelArgMemPointerINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clEnqueueMemsetINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clEnqueueMemFillINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clEnqueueMemcpyINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clEnqueueMigrateMemINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clEnqueueMemAdviseINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clGetDeviceFunctionPointerINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clGetDeviceGlobalVariablePointerINTEL);
    RETURN_FUNC_PTR_IF_EXIST(clGetExecutionInfoINTEL);

    void *ret = sharingFactory.getExtensionFunctionAddress(funcName);
    if (ret != nullptr) {
        TRACING_EXIT(clGetExtensionFunctionAddress, &ret);
        return ret;
    }

    // SPIR-V support through the cl_khr_il_program extension
    RETURN_FUNC_PTR_IF_EXIST(clCreateProgramWithILKHR);
    RETURN_FUNC_PTR_IF_EXIST(clCreateCommandQueueWithPropertiesKHR);

    ret = getAdditionalExtensionFunctionAddress(funcName);
    TRACING_EXIT(clGetExtensionFunctionAddress, &ret);
    return ret;
}

// OpenCL 1.2
void *CL_API_CALL clGetExtensionFunctionAddressForPlatform(cl_platform_id platform,
                                                           const char *funcName) {
    TRACING_ENTER(clGetExtensionFunctionAddressForPlatform, &platform, &funcName);
    DBG_LOG_INPUTS("platform", platform, "funcName", funcName);
    auto pPlatform = castToObject<Platform>(platform);

    if (pPlatform == nullptr) {
        void *ret = nullptr;
        TRACING_EXIT(clGetExtensionFunctionAddressForPlatform, &ret);
        return ret;
    }

    void *ret = clGetExtensionFunctionAddress(funcName);
    TRACING_EXIT(clGetExtensionFunctionAddressForPlatform, &ret);
    return ret;
}

void *CL_API_CALL clSVMAlloc(cl_context context,
                             cl_svm_mem_flags flags,
                             size_t size,
                             cl_uint alignment) {
    TRACING_ENTER(clSVMAlloc, &context, &flags, &size, &alignment);
    DBG_LOG_INPUTS("context", context,
                   "flags", flags,
                   "size", size,
                   "alignment", alignment);
    void *pAlloc = nullptr;
    auto pPlatform = platform();
    auto pDevice = pPlatform->getDevice(0);

    Context *pContext = nullptr;

    if (validateObjects(WithCastToInternal(context, &pContext), pDevice) != CL_SUCCESS) {
        TRACING_EXIT(clSVMAlloc, &pAlloc);
        return pAlloc;
    }

    if (flags == 0) {
        flags = CL_MEM_READ_WRITE;
    }

    if (!((flags == CL_MEM_READ_WRITE) ||
          (flags == CL_MEM_WRITE_ONLY) ||
          (flags == CL_MEM_READ_ONLY) ||
          (flags == CL_MEM_SVM_FINE_GRAIN_BUFFER) ||
          (flags == (CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS)) ||
          (flags == (CL_MEM_READ_WRITE | CL_MEM_SVM_FINE_GRAIN_BUFFER)) ||
          (flags == (CL_MEM_READ_WRITE | CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS)) ||
          (flags == (CL_MEM_WRITE_ONLY | CL_MEM_SVM_FINE_GRAIN_BUFFER)) ||
          (flags == (CL_MEM_WRITE_ONLY | CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS)) ||
          (flags == (CL_MEM_READ_ONLY | CL_MEM_SVM_FINE_GRAIN_BUFFER)) ||
          (flags == (CL_MEM_READ_ONLY | CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS)))) {
        TRACING_EXIT(clSVMAlloc, &pAlloc);
        return pAlloc;
    }

    if ((size == 0) || (size > pDevice->getDeviceInfo().maxMemAllocSize)) {
        TRACING_EXIT(clSVMAlloc, &pAlloc);
        return pAlloc;
    }

    if ((alignment && (alignment & (alignment - 1))) || (alignment > sizeof(cl_ulong16))) {
        TRACING_EXIT(clSVMAlloc, &pAlloc);
        return pAlloc;
    }

    const HardwareInfo &hwInfo = pDevice->getHardwareInfo();
    if (!hwInfo.capabilityTable.ftrSvm) {
        TRACING_EXIT(clSVMAlloc, &pAlloc);
        return pAlloc;
    }
    if (!hwInfo.capabilityTable.ftrSupportsCoherency &&
        (flags & (CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS))) {
        TRACING_EXIT(clSVMAlloc, &pAlloc);
        return pAlloc;
    }

    pAlloc = pContext->getSVMAllocsManager()->createSVMAlloc(pDevice->getRootDeviceIndex(), size, MemObjHelper::getSvmAllocationProperties(flags));

    if (pContext->isProvidingPerformanceHints()) {
        pContext->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_GOOD_INTEL, CL_SVM_ALLOC_MEETS_ALIGNMENT_RESTRICTIONS, pAlloc, size);
    }
    TRACING_EXIT(clSVMAlloc, &pAlloc);
    return pAlloc;
}

void CL_API_CALL clSVMFree(cl_context context,
                           void *svmPointer) {
    TRACING_ENTER(clSVMFree, &context, &svmPointer);
    DBG_LOG_INPUTS("context", context,
                   "svmPointer", svmPointer);
    Context *pContext = nullptr;
    if (validateObject(WithCastToInternal(context, &pContext)) == CL_SUCCESS) {
        pContext->getSVMAllocsManager()->freeSVMAlloc(svmPointer);
    }
    TRACING_EXIT(clSVMFree, nullptr);
}

cl_int CL_API_CALL clEnqueueSVMFree(cl_command_queue commandQueue,
                                    cl_uint numSvmPointers,
                                    void *svmPointers[],
                                    void(CL_CALLBACK *pfnFreeFunc)(cl_command_queue queue,
                                                                   cl_uint numSvmPointers,
                                                                   void *svmPointers[],
                                                                   void *userData),
                                    void *userData,
                                    cl_uint numEventsInWaitList,
                                    const cl_event *eventWaitList,
                                    cl_event *event) {
    TRACING_ENTER(clEnqueueSVMFree, &commandQueue, &numSvmPointers, &svmPointers, &pfnFreeFunc, &userData, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;

    cl_int retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        EventWaitList(numEventsInWaitList, eventWaitList));

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "numSvmPointers", numSvmPointers,
                   "svmPointers", svmPointers,
                   "pfnFreeFunc", pfnFreeFunc,
                   "userData", userData,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (retVal != CL_SUCCESS) {
        TRACING_EXIT(clEnqueueSVMFree, &retVal);
        return retVal;
    }

    if (((svmPointers != nullptr) && (numSvmPointers == 0)) ||
        ((svmPointers == nullptr) && (numSvmPointers != 0))) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueSVMFree, &retVal);
        return retVal;
    }

    retVal = pCommandQueue->enqueueSVMFree(
        numSvmPointers,
        svmPointers,
        pfnFreeFunc,
        userData,
        numEventsInWaitList,
        eventWaitList,
        event);

    TRACING_EXIT(clEnqueueSVMFree, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueSVMMemcpy(cl_command_queue commandQueue,
                                      cl_bool blockingCopy,
                                      void *dstPtr,
                                      const void *srcPtr,
                                      size_t size,
                                      cl_uint numEventsInWaitList,
                                      const cl_event *eventWaitList,
                                      cl_event *event) {
    TRACING_ENTER(clEnqueueSVMMemcpy, &commandQueue, &blockingCopy, &dstPtr, &srcPtr, &size, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;

    cl_int retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        EventWaitList(numEventsInWaitList, eventWaitList));

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "blockingCopy", blockingCopy,
                   "dstPtr", dstPtr,
                   "srcPtr", srcPtr,
                   "size", size,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (retVal != CL_SUCCESS) {
        TRACING_EXIT(clEnqueueSVMMemcpy, &retVal);
        return retVal;
    }

    if ((dstPtr == nullptr) || (srcPtr == nullptr)) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueSVMMemcpy, &retVal);
        return retVal;
    }

    retVal = pCommandQueue->enqueueSVMMemcpy(
        blockingCopy,
        dstPtr,
        srcPtr,
        size,
        numEventsInWaitList,
        eventWaitList,
        event);

    TRACING_EXIT(clEnqueueSVMMemcpy, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueSVMMemFill(cl_command_queue commandQueue,
                                       void *svmPtr,
                                       const void *pattern,
                                       size_t patternSize,
                                       size_t size,
                                       cl_uint numEventsInWaitList,
                                       const cl_event *eventWaitList,
                                       cl_event *event) {
    TRACING_ENTER(clEnqueueSVMMemFill, &commandQueue, &svmPtr, &pattern, &patternSize, &size, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;

    cl_int retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        EventWaitList(numEventsInWaitList, eventWaitList));

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "svmPtr", DebugManager.infoPointerToString(svmPtr, size),
                   "pattern", DebugManager.infoPointerToString(pattern, patternSize),
                   "patternSize", patternSize,
                   "size", size,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (retVal != CL_SUCCESS) {
        TRACING_EXIT(clEnqueueSVMMemFill, &retVal);
        return retVal;
    }

    if ((svmPtr == nullptr) || (size == 0)) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueSVMMemFill, &retVal);
        return retVal;
    }

    retVal = pCommandQueue->enqueueSVMMemFill(
        svmPtr,
        pattern,
        patternSize,
        size,
        numEventsInWaitList,
        eventWaitList,
        event);

    TRACING_EXIT(clEnqueueSVMMemFill, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueSVMMap(cl_command_queue commandQueue,
                                   cl_bool blockingMap,
                                   cl_map_flags mapFlags,
                                   void *svmPtr,
                                   size_t size,
                                   cl_uint numEventsInWaitList,
                                   const cl_event *eventWaitList,
                                   cl_event *event) {
    TRACING_ENTER(clEnqueueSVMMap, &commandQueue, &blockingMap, &mapFlags, &svmPtr, &size, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;

    cl_int retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        EventWaitList(numEventsInWaitList, eventWaitList));
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "blockingMap", blockingMap,
                   "mapFlags", mapFlags,
                   "svmPtr", DebugManager.infoPointerToString(svmPtr, size),
                   "size", size,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clEnqueueSVMMap, &retVal);
        return retVal;
    }

    if ((svmPtr == nullptr) || (size == 0)) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueSVMMap, &retVal);
        return retVal;
    }

    retVal = pCommandQueue->enqueueSVMMap(
        blockingMap,
        mapFlags,
        svmPtr,
        size,
        numEventsInWaitList,
        eventWaitList,
        event,
        true);

    TRACING_EXIT(clEnqueueSVMMap, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueSVMUnmap(cl_command_queue commandQueue,
                                     void *svmPtr,
                                     cl_uint numEventsInWaitList,
                                     const cl_event *eventWaitList,
                                     cl_event *event) {
    TRACING_ENTER(clEnqueueSVMUnmap, &commandQueue, &svmPtr, &numEventsInWaitList, &eventWaitList, &event);

    CommandQueue *pCommandQueue = nullptr;

    cl_int retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        EventWaitList(numEventsInWaitList, eventWaitList),
        svmPtr);

    API_ENTER(&retVal);

    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "svmPtr", svmPtr,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    if (retVal != CL_SUCCESS) {
        TRACING_EXIT(clEnqueueSVMUnmap, &retVal);
        return retVal;
    }

    retVal = pCommandQueue->enqueueSVMUnmap(
        svmPtr,
        numEventsInWaitList,
        eventWaitList,
        event,
        true);

    TRACING_EXIT(clEnqueueSVMUnmap, &retVal);
    return retVal;
}

cl_int CL_API_CALL clSetKernelArgSVMPointer(cl_kernel kernel,
                                            cl_uint argIndex,
                                            const void *argValue) {
    TRACING_ENTER(clSetKernelArgSVMPointer, &kernel, &argIndex, &argValue);

    Kernel *pKernel = nullptr;

    auto retVal = validateObjects(WithCastToInternal(kernel, &pKernel));
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("kernel", kernel, "argIndex", argIndex, "argValue", argValue);

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clSetKernelArgSVMPointer, &retVal);
        return retVal;
    }

    if (argIndex >= pKernel->getKernelArgsNumber()) {
        retVal = CL_INVALID_ARG_INDEX;
        TRACING_EXIT(clSetKernelArgSVMPointer, &retVal);
        return retVal;
    }

    auto svmManager = pKernel->getContext().getSVMAllocsManager();
    if (!svmManager) {
        retVal = CL_INVALID_ARG_VALUE;
        return retVal;
    }

    cl_int kernelArgAddressQualifier = pKernel->getKernelArgAddressQualifier(argIndex);
    if ((kernelArgAddressQualifier != CL_KERNEL_ARG_ADDRESS_GLOBAL) &&
        (kernelArgAddressQualifier != CL_KERNEL_ARG_ADDRESS_CONSTANT)) {
        retVal = CL_INVALID_ARG_VALUE;
        TRACING_EXIT(clSetKernelArgSVMPointer, &retVal);
        return retVal;
    }

    GraphicsAllocation *pSvmAlloc = nullptr;
    if (argValue != nullptr) {
        auto svmData = svmManager->getSVMAlloc(argValue);
        if (svmData == nullptr) {
            if (!pKernel->getDevice().areSharedSystemAllocationsAllowed()) {
                retVal = CL_INVALID_ARG_VALUE;
                TRACING_EXIT(clSetKernelArgSVMPointer, &retVal);
                return retVal;
            }
        } else {
            pSvmAlloc = svmData->gpuAllocation;
        }
    }

    retVal = pKernel->setArgSvmAlloc(argIndex, const_cast<void *>(argValue), pSvmAlloc);
    TRACING_EXIT(clSetKernelArgSVMPointer, &retVal);
    return retVal;
}

cl_int CL_API_CALL clSetKernelExecInfo(cl_kernel kernel,
                                       cl_kernel_exec_info paramName,
                                       size_t paramValueSize,
                                       const void *paramValue) {
    TRACING_ENTER(clSetKernelExecInfo, &kernel, &paramName, &paramValueSize, &paramValue);

    Kernel *pKernel = nullptr;
    auto retVal = validateObjects(WithCastToInternal(kernel, &pKernel));
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("kernel", kernel, "paramName", paramName,
                   "paramValueSize", paramValueSize, "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize));

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clSetKernelExecInfo, &retVal);
        return retVal;
    }

    switch (paramName) {
    case CL_KERNEL_EXEC_INFO_INDIRECT_DEVICE_ACCESS_INTEL:
    case CL_KERNEL_EXEC_INFO_INDIRECT_HOST_ACCESS_INTEL:
    case CL_KERNEL_EXEC_INFO_INDIRECT_SHARED_ACCESS_INTEL: {
        auto propertyValue = *reinterpret_cast<const cl_bool *>(paramValue);
        pKernel->setUnifiedMemoryProperty(paramName, propertyValue);
    } break;

    case CL_KERNEL_EXEC_INFO_SVM_PTRS:
    case CL_KERNEL_EXEC_INFO_USM_PTRS_INTEL: {
        if ((paramValueSize == 0) ||
            (paramValueSize % sizeof(void *)) ||
            (paramValue == nullptr)) {
            retVal = CL_INVALID_VALUE;
            TRACING_EXIT(clSetKernelExecInfo, &retVal);
            return retVal;
        }
        size_t numPointers = paramValueSize / sizeof(void *);
        size_t *pSvmPtrList = (size_t *)paramValue;

        if (pKernel->getContext().getSVMAllocsManager() == nullptr) {
            return CL_INVALID_VALUE;
        }

        if (paramName == CL_KERNEL_EXEC_INFO_SVM_PTRS) {
            pKernel->clearSvmKernelExecInfo();
        } else {
            pKernel->clearUnifiedMemoryExecInfo();
        }

        for (uint32_t i = 0; i < numPointers; i++) {
            auto svmData = pKernel->getContext().getSVMAllocsManager()->getSVMAlloc((const void *)pSvmPtrList[i]);
            if (svmData == nullptr) {
                retVal = CL_INVALID_VALUE;
                TRACING_EXIT(clSetKernelExecInfo, &retVal);
                return retVal;
            }
            GraphicsAllocation *svmAlloc = svmData->gpuAllocation;

            if (paramName == CL_KERNEL_EXEC_INFO_SVM_PTRS) {
                pKernel->setSvmKernelExecInfo(svmAlloc);
            } else {
                pKernel->setUnifiedMemoryExecInfo(svmAlloc);
            }
        }
        break;
    }
    case CL_KERNEL_EXEC_INFO_SVM_FINE_GRAIN_SYSTEM: {
        retVal = CL_INVALID_OPERATION;
        TRACING_EXIT(clSetKernelExecInfo, &retVal);
        return retVal;
    }
    default: {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clSetKernelExecInfo, &retVal);
        return retVal;
    }
    }

    TRACING_EXIT(clSetKernelExecInfo, &retVal);
    return retVal;
};

cl_mem CL_API_CALL clCreatePipe(cl_context context,
                                cl_mem_flags flags,
                                cl_uint pipePacketSize,
                                cl_uint pipeMaxPackets,
                                const cl_pipe_properties *properties,
                                cl_int *errcodeRet) {
    TRACING_ENTER(clCreatePipe, &context, &flags, &pipePacketSize, &pipeMaxPackets, &properties, &errcodeRet);
    cl_mem pipe = nullptr;
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("cl_context", context,
                   "cl_mem_flags", flags,
                   "cl_uint", pipePacketSize,
                   "cl_uint", pipeMaxPackets,
                   "const cl_pipe_properties", properties,
                   "cl_int", errcodeRet);

    auto pPlatform = platform();
    auto pDevice = pPlatform->getDevice(0);
    Context *pContext = nullptr;

    const cl_mem_flags allValidFlags =
        CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS;

    do {
        if ((pipePacketSize == 0) || (pipeMaxPackets == 0)) {
            retVal = CL_INVALID_PIPE_SIZE;
            break;
        }

        /* Are there some invalid flag bits? */
        if ((flags & (~allValidFlags)) != 0) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        if (properties != nullptr) {
            retVal = CL_INVALID_VALUE;
            break;
        }

        retVal = validateObjects(WithCastToInternal(context, &pContext));
        if (retVal != CL_SUCCESS) {
            break;
        }

        if (pipePacketSize > pDevice->getDeviceInfo().pipeMaxPacketSize) {
            retVal = CL_INVALID_PIPE_SIZE;
            break;
        }

        // create the pipe
        pipe = Pipe::create(pContext, flags, pipePacketSize, pipeMaxPackets, properties, retVal);
    } while (false);

    if (errcodeRet) {
        *errcodeRet = retVal;
    }
    DBG_LOG_INPUTS("pipe", pipe);
    TRACING_EXIT(clCreatePipe, &pipe);
    return pipe;
}

cl_int CL_API_CALL clGetPipeInfo(cl_mem pipe,
                                 cl_pipe_info paramName,
                                 size_t paramValueSize,
                                 void *paramValue,
                                 size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetPipeInfo, &pipe, &paramName, &paramValueSize, &paramValue, &paramValueSizeRet);

    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);

    DBG_LOG_INPUTS("cl_mem", pipe,
                   "cl_pipe_info", paramName,
                   "size_t", paramValueSize,
                   "void *", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "size_t*", paramValueSizeRet);

    retVal = validateObjects(pipe);
    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clGetPipeInfo, &retVal);
        return retVal;
    }

    auto pPipeObj = castToObject<Pipe>(pipe);

    if (pPipeObj == nullptr) {
        retVal = CL_INVALID_MEM_OBJECT;
        TRACING_EXIT(clGetPipeInfo, &retVal);
        return retVal;
    }

    retVal = pPipeObj->getPipeInfo(paramName, paramValueSize, paramValue, paramValueSizeRet);
    TRACING_EXIT(clGetPipeInfo, &retVal);
    return retVal;
}

cl_command_queue CL_API_CALL clCreateCommandQueueWithProperties(cl_context context,
                                                                cl_device_id device,
                                                                const cl_queue_properties *properties,
                                                                cl_int *errcodeRet) {
    TRACING_ENTER(clCreateCommandQueueWithProperties, &context, &device, &properties, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "device", device,
                   "properties", properties);

    cl_command_queue commandQueue = nullptr;
    ErrorCodeHelper err(errcodeRet, CL_SUCCESS);

    Context *pContext = nullptr;
    Device *pDevice = nullptr;

    retVal = validateObjects(
        WithCastToInternal(context, &pContext),
        WithCastToInternal(device, &pDevice));

    if (CL_SUCCESS != retVal) {
        err.set(retVal);
        TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
        return commandQueue;
    }

    auto minimumCreateDeviceQueueFlags = static_cast<cl_command_queue_properties>(CL_QUEUE_ON_DEVICE |
                                                                                  CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE);
    auto tokenValue = properties ? *properties : 0;
    auto propertiesAddress = properties;

    while (tokenValue != 0) {
        if (tokenValue != CL_QUEUE_PROPERTIES &&
            tokenValue != CL_QUEUE_SIZE &&
            tokenValue != CL_QUEUE_PRIORITY_KHR &&
            tokenValue != CL_QUEUE_THROTTLE_KHR &&
            tokenValue != CL_QUEUE_SLICE_COUNT_INTEL &&
            !isExtraToken(propertiesAddress)) {
            err.set(CL_INVALID_VALUE);
            TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
            return commandQueue;
        }

        propertiesAddress += 2;
        tokenValue = *propertiesAddress;
    }

    if (!verifyExtraTokens(pDevice, *pContext, properties)) {
        err.set(CL_INVALID_VALUE);
        TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
        return commandQueue;
    }

    auto commandQueueProperties = getCmdQueueProperties<cl_command_queue_properties>(properties);
    uint32_t maxOnDeviceQueueSize = pDevice->getDeviceInfo().queueOnDeviceMaxSize;
    uint32_t maxOnDeviceQueues = pDevice->getDeviceInfo().maxOnDeviceQueues;

    if (commandQueueProperties & static_cast<cl_command_queue_properties>(CL_QUEUE_ON_DEVICE)) {
        if (!(commandQueueProperties & static_cast<cl_command_queue_properties>(CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE))) {
            err.set(CL_INVALID_VALUE);
            TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
            return commandQueue;
        }
        if (!pDevice->getHardwareInfo().capabilityTable.supportsDeviceEnqueue) {
            err.set(CL_INVALID_QUEUE_PROPERTIES);
            TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
            return commandQueue;
        }
    }

    if (commandQueueProperties & static_cast<cl_command_queue_properties>(CL_QUEUE_ON_DEVICE_DEFAULT)) {
        if (!(commandQueueProperties & static_cast<cl_command_queue_properties>(CL_QUEUE_ON_DEVICE))) {
            err.set(CL_INVALID_VALUE);
            TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
            return commandQueue;
        }
    } else if (commandQueueProperties & static_cast<cl_command_queue_properties>(CL_QUEUE_ON_DEVICE)) {
        if ((maxOnDeviceQueues == 0) || ((maxOnDeviceQueues == 1) && pContext->getDefaultDeviceQueue())) {
            err.set(CL_OUT_OF_RESOURCES);
            TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
            return commandQueue;
        }
    }

    if (getCmdQueueProperties<cl_command_queue_properties>(properties, CL_QUEUE_SIZE) > maxOnDeviceQueueSize) {
        err.set(CL_INVALID_QUEUE_PROPERTIES);
        TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
        return commandQueue;
    }

    if (commandQueueProperties & static_cast<cl_command_queue_properties>(CL_QUEUE_ON_DEVICE)) {
        if (getCmdQueueProperties<cl_queue_priority_khr>(properties, CL_QUEUE_PRIORITY_KHR)) {
            err.set(CL_INVALID_QUEUE_PROPERTIES);
            TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
            return commandQueue;
        }
    }

    if (commandQueueProperties & static_cast<cl_command_queue_properties>(CL_QUEUE_ON_DEVICE)) {
        if (getCmdQueueProperties<cl_queue_throttle_khr>(properties, CL_QUEUE_THROTTLE_KHR)) {
            err.set(CL_INVALID_QUEUE_PROPERTIES);
            TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
            return commandQueue;
        }
    }

    if (getCmdQueueProperties<cl_command_queue_properties>(properties, CL_QUEUE_SLICE_COUNT_INTEL) > pDevice->getDeviceInfo().maxSliceCount) {
        err.set(CL_INVALID_QUEUE_PROPERTIES);
        TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
        return commandQueue;
    }

    auto maskedFlags = commandQueueProperties & minimumCreateDeviceQueueFlags;

    if (maskedFlags == minimumCreateDeviceQueueFlags) {
        commandQueue = DeviceQueue::create(
            pContext,
            pDevice,
            *properties,
            retVal);

    } else {
        commandQueue = CommandQueue::create(
            pContext,
            pDevice,
            properties,
            retVal);
        if (pContext->isProvidingPerformanceHints()) {
            pContext->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, DRIVER_CALLS_INTERNAL_CL_FLUSH);
            if (castToObjectOrAbort<CommandQueue>(commandQueue)->isProfilingEnabled()) {
                pContext->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, PROFILING_ENABLED);
                if (pDevice->getDeviceInfo().preemptionSupported && pDevice->getHardwareInfo().platform.eProductFamily < IGFX_SKYLAKE) {
                    pContext->providePerformanceHint(CL_CONTEXT_DIAGNOSTICS_LEVEL_NEUTRAL_INTEL, PROFILING_ENABLED_WITH_DISABLED_PREEMPTION);
                }
            }
        }
    }

    if (!commandQueue)
        retVal = CL_OUT_OF_HOST_MEMORY;

    DBG_LOG_INPUTS("commandQueue", commandQueue, "properties", static_cast<int>(getCmdQueueProperties<cl_command_queue_properties>(properties)));

    err.set(retVal);

    TRACING_EXIT(clCreateCommandQueueWithProperties, &commandQueue);
    return commandQueue;
}

cl_sampler CL_API_CALL clCreateSamplerWithProperties(cl_context context,
                                                     const cl_sampler_properties *samplerProperties,
                                                     cl_int *errcodeRet) {
    TRACING_ENTER(clCreateSamplerWithProperties, &context, &samplerProperties, &errcodeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "samplerProperties", samplerProperties);
    cl_sampler sampler = nullptr;
    retVal = validateObjects(context);

    if (CL_SUCCESS == retVal) {
        sampler = Sampler::create(
            castToObject<Context>(context),
            samplerProperties,
            retVal);
    }

    if (errcodeRet) {
        *errcodeRet = retVal;
    }

    TRACING_EXIT(clCreateSamplerWithProperties, &sampler);
    return sampler;
}

cl_int CL_API_CALL clUnloadCompiler() {
    TRACING_ENTER(clUnloadCompiler);
    cl_int retVal = CL_OUT_OF_HOST_MEMORY;
    API_ENTER(&retVal);
    TRACING_EXIT(clUnloadCompiler, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetKernelSubGroupInfoKHR(cl_kernel kernel,
                                              cl_device_id device,
                                              cl_kernel_sub_group_info paramName,
                                              size_t inputValueSize,
                                              const void *inputValue,
                                              size_t paramValueSize,
                                              void *paramValue,
                                              size_t *paramValueSizeRet) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("kernel", kernel,
                   "device", device,
                   "paramName", paramName,
                   "inputValueSize", inputValueSize,
                   "inputValue", DebugManager.infoPointerToString(inputValue, inputValueSize),
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);

    Kernel *pKernel = nullptr;
    retVal = validateObjects(device,
                             WithCastToInternal(kernel, &pKernel));

    if (CL_SUCCESS != retVal) {
        return retVal;
    }

    switch (paramName) {
    case CL_KERNEL_MAX_SUB_GROUP_SIZE_FOR_NDRANGE:
    case CL_KERNEL_SUB_GROUP_COUNT_FOR_NDRANGE:
    case CL_KERNEL_COMPILE_SUB_GROUP_SIZE_INTEL:
        return pKernel->getSubGroupInfo(paramName,
                                        inputValueSize, inputValue,
                                        paramValueSize, paramValue,
                                        paramValueSizeRet);
    default: {
        retVal = CL_INVALID_VALUE;
        return retVal;
    }
    }
}

cl_int CL_API_CALL clGetDeviceAndHostTimer(cl_device_id device,
                                           cl_ulong *deviceTimestamp,
                                           cl_ulong *hostTimestamp) {
    TRACING_ENTER(clGetDeviceAndHostTimer, &device, &deviceTimestamp, &hostTimestamp);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("device", device,
                   "deviceTimestamp", deviceTimestamp,
                   "hostTimestamp", hostTimestamp);
    do {
        Device *pDevice = castToObject<Device>(device);
        if (pDevice == nullptr) {
            retVal = CL_INVALID_DEVICE;
            break;
        }
        if (deviceTimestamp == nullptr || hostTimestamp == nullptr) {
            retVal = CL_INVALID_VALUE;
            break;
        }
        if (!pDevice->getDeviceAndHostTimer(static_cast<uint64_t *>(deviceTimestamp), static_cast<uint64_t *>(hostTimestamp))) {
            retVal = CL_OUT_OF_RESOURCES;
            break;
        }
    } while (false);

    TRACING_EXIT(clGetDeviceAndHostTimer, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetHostTimer(cl_device_id device,
                                  cl_ulong *hostTimestamp) {
    TRACING_ENTER(clGetHostTimer, &device, &hostTimestamp);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("device", device,
                   "hostTimestamp", hostTimestamp);

    do {
        Device *pDevice = castToObject<Device>(device);
        if (pDevice == nullptr) {
            retVal = CL_INVALID_DEVICE;
            break;
        }
        if (hostTimestamp == nullptr) {
            retVal = CL_INVALID_VALUE;
            break;
        }
        if (!pDevice->getHostTimer(static_cast<uint64_t *>(hostTimestamp))) {
            retVal = CL_OUT_OF_RESOURCES;
            break;
        }
    } while (false);

    TRACING_EXIT(clGetHostTimer, &retVal);
    return retVal;
}

cl_int CL_API_CALL clGetKernelSubGroupInfo(cl_kernel kernel,
                                           cl_device_id device,
                                           cl_kernel_sub_group_info paramName,
                                           size_t inputValueSize,
                                           const void *inputValue,
                                           size_t paramValueSize,
                                           void *paramValue,
                                           size_t *paramValueSizeRet) {
    TRACING_ENTER(clGetKernelSubGroupInfo, &kernel, &device, &paramName, &inputValueSize, &inputValue, &paramValueSize, &paramValue, &paramValueSizeRet);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("kernel", kernel,
                   "device", device,
                   "paramName", paramName,
                   "inputValueSize", inputValueSize,
                   "inputValue", DebugManager.infoPointerToString(inputValue, inputValueSize),
                   "paramValueSize", paramValueSize,
                   "paramValue", DebugManager.infoPointerToString(paramValue, paramValueSize),
                   "paramValueSizeRet", paramValueSizeRet);

    Kernel *pKernel = nullptr;
    retVal = validateObjects(device,
                             WithCastToInternal(kernel, &pKernel));

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clGetKernelSubGroupInfo, &retVal);
        return retVal;
    }

    retVal = pKernel->getSubGroupInfo(paramName,
                                      inputValueSize, inputValue,
                                      paramValueSize, paramValue,
                                      paramValueSizeRet);
    TRACING_EXIT(clGetKernelSubGroupInfo, &retVal);
    return retVal;
}

cl_int CL_API_CALL clSetDefaultDeviceCommandQueue(cl_context context,
                                                  cl_device_id device,
                                                  cl_command_queue commandQueue) {
    TRACING_ENTER(clSetDefaultDeviceCommandQueue, &context, &device, &commandQueue);

    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("context", context,
                   "device", device,
                   "commandQueue", commandQueue);

    Context *pContext = nullptr;

    retVal = validateObjects(WithCastToInternal(context, &pContext), device);

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clSetDefaultDeviceCommandQueue, &retVal);
        return retVal;
    }

    auto pDeviceQueue = castToObject<DeviceQueue>(static_cast<_device_queue *>(commandQueue));

    if (!pDeviceQueue) {
        retVal = CL_INVALID_COMMAND_QUEUE;
        TRACING_EXIT(clSetDefaultDeviceCommandQueue, &retVal);
        return retVal;
    }

    if (&pDeviceQueue->getContext() != pContext) {
        retVal = CL_INVALID_COMMAND_QUEUE;
        TRACING_EXIT(clSetDefaultDeviceCommandQueue, &retVal);
        return retVal;
    }

    pContext->setDefaultDeviceQueue(pDeviceQueue);

    retVal = CL_SUCCESS;
    TRACING_EXIT(clSetDefaultDeviceCommandQueue, &retVal);
    return retVal;
}

cl_int CL_API_CALL clEnqueueSVMMigrateMem(cl_command_queue commandQueue,
                                          cl_uint numSvmPointers,
                                          const void **svmPointers,
                                          const size_t *sizes,
                                          const cl_mem_migration_flags flags,
                                          cl_uint numEventsInWaitList,
                                          const cl_event *eventWaitList,
                                          cl_event *event) {
    TRACING_ENTER(clEnqueueSVMMigrateMem, &commandQueue, &numSvmPointers, &svmPointers, &sizes, &flags, &numEventsInWaitList, &eventWaitList, &event);
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "numSvmPointers", numSvmPointers,
                   "svmPointers", DebugManager.infoPointerToString(svmPointers ? svmPointers[0] : 0, DebugManager.getInput(sizes, 0)),
                   "sizes", DebugManager.getInput(sizes, 0),
                   "flags", flags,
                   "numEventsInWaitList", numEventsInWaitList,
                   "eventWaitList", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(eventWaitList), numEventsInWaitList),
                   "event", DebugManager.getEvents(reinterpret_cast<const uintptr_t *>(event), 1));

    CommandQueue *pCommandQueue = nullptr;
    retVal = validateObjects(
        WithCastToInternal(commandQueue, &pCommandQueue),
        EventWaitList(numEventsInWaitList, eventWaitList));

    if (CL_SUCCESS != retVal) {
        TRACING_EXIT(clEnqueueSVMMigrateMem, &retVal);
        return retVal;
    }

    if (numSvmPointers == 0 || svmPointers == nullptr) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueSVMMigrateMem, &retVal);
        return retVal;
    }

    const cl_mem_migration_flags allValidFlags =
        CL_MIGRATE_MEM_OBJECT_HOST | CL_MIGRATE_MEM_OBJECT_CONTENT_UNDEFINED;

    if ((flags & (~allValidFlags)) != 0) {
        retVal = CL_INVALID_VALUE;
        TRACING_EXIT(clEnqueueSVMMigrateMem, &retVal);
        return retVal;
    }
    auto pSvmAllocMgr = pCommandQueue->getContext().getSVMAllocsManager();

    if (pSvmAllocMgr == nullptr) {
        retVal = CL_INVALID_VALUE;
        return retVal;
    }

    for (uint32_t i = 0; i < numSvmPointers; i++) {
        auto svmData = pSvmAllocMgr->getSVMAlloc(svmPointers[i]);
        if (svmData == nullptr) {
            retVal = CL_INVALID_VALUE;
            TRACING_EXIT(clEnqueueSVMMigrateMem, &retVal);
            return retVal;
        }
        if (sizes != nullptr && sizes[i] != 0) {
            svmData = pSvmAllocMgr->getSVMAlloc(reinterpret_cast<void *>((size_t)svmPointers[i] + sizes[i] - 1));
            if (svmData == nullptr) {
                retVal = CL_INVALID_VALUE;
                TRACING_EXIT(clEnqueueSVMMigrateMem, &retVal);
                return retVal;
            }
        }
    }

    for (uint32_t i = 0; i < numEventsInWaitList; i++) {
        auto pEvent = castToObject<Event>(eventWaitList[i]);
        if (pEvent->getContext() != &pCommandQueue->getContext()) {
            retVal = CL_INVALID_CONTEXT;
            TRACING_EXIT(clEnqueueSVMMigrateMem, &retVal);
            return retVal;
        }
    }
    retVal = pCommandQueue->enqueueSVMMigrateMem(numSvmPointers,
                                                 svmPointers,
                                                 sizes,
                                                 flags,
                                                 numEventsInWaitList,
                                                 eventWaitList,
                                                 event);
    TRACING_EXIT(clEnqueueSVMMigrateMem, &retVal);
    return retVal;
}

cl_kernel CL_API_CALL clCloneKernel(cl_kernel sourceKernel,
                                    cl_int *errcodeRet) {
    TRACING_ENTER(clCloneKernel, &sourceKernel, &errcodeRet);
    Kernel *pSourceKernel = nullptr;
    Kernel *pClonedKernel = nullptr;

    auto retVal = validateObjects(WithCastToInternal(sourceKernel, &pSourceKernel));
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("sourceKernel", sourceKernel);

    if (CL_SUCCESS == retVal) {
        pClonedKernel = Kernel::create(pSourceKernel->getProgram(),
                                       pSourceKernel->getKernelInfo(),
                                       &retVal);
        UNRECOVERABLE_IF((pClonedKernel == nullptr) || (retVal != CL_SUCCESS));

        retVal = pClonedKernel->cloneKernel(pSourceKernel);
    }

    if (errcodeRet) {
        *errcodeRet = retVal;
    }
    if (pClonedKernel != nullptr) {
        gtpinNotifyKernelCreate(pClonedKernel);
    }

    TRACING_EXIT(clCloneKernel, (cl_kernel *)&pClonedKernel);
    return pClonedKernel;
}

CL_API_ENTRY cl_int CL_API_CALL clEnqueueVerifyMemoryINTEL(cl_command_queue commandQueue,
                                                           const void *allocationPtr,
                                                           const void *expectedData,
                                                           size_t sizeOfComparison,
                                                           cl_uint comparisonMode) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue,
                   "allocationPtr", allocationPtr,
                   "expectedData", expectedData,
                   "sizeOfComparison", sizeOfComparison,
                   "comparisonMode", comparisonMode);

    if (sizeOfComparison == 0 || expectedData == nullptr || allocationPtr == nullptr) {
        retVal = CL_INVALID_VALUE;
        return retVal;
    }

    CommandQueue *pCommandQueue = nullptr;
    retVal = validateObjects(WithCastToInternal(commandQueue, &pCommandQueue));
    if (retVal != CL_SUCCESS) {
        return retVal;
    }

    auto &csr = pCommandQueue->getGpgpuCommandStreamReceiver();
    retVal = csr.expectMemory(allocationPtr, expectedData, sizeOfComparison, comparisonMode);
    return retVal;
}

cl_int CL_API_CALL clAddCommentINTEL(cl_platform_id platform, const char *comment) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("platform", platform, "comment", comment);

    auto executionEnvironment = ::platform()->peekExecutionEnvironment();
    auto aubCenter = executionEnvironment->rootDeviceEnvironments[0]->aubCenter.get();

    if (!comment || (aubCenter && !aubCenter->getAubManager())) {
        retVal = CL_INVALID_VALUE;
    }

    if (retVal == CL_SUCCESS && aubCenter) {
        aubCenter->getAubManager()->addComment(comment);
    }

    return retVal;
}

cl_int CL_API_CALL clGetDeviceGlobalVariablePointerINTEL(
    cl_device_id device,
    cl_program program,
    const char *globalVariableName,
    size_t *globalVariableSizeRet,
    void **globalVariablePointerRet) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("device", device, "program", program,
                   "globalVariableName", globalVariableName,
                   "globalVariablePointerRet", globalVariablePointerRet);
    retVal = validateObjects(device, program);
    if (globalVariablePointerRet == nullptr) {
        retVal = CL_INVALID_ARG_VALUE;
    }

    if (CL_SUCCESS == retVal) {
        Program *pProgram = (Program *)(program);
        const auto &symbols = pProgram->getSymbols();
        auto symbolIt = symbols.find(globalVariableName);
        if ((symbolIt == symbols.end()) || (symbolIt->second.symbol.type == NEO::SymbolInfo::Function)) {
            retVal = CL_INVALID_ARG_VALUE;
        } else {
            if (globalVariableSizeRet != nullptr) {
                *globalVariableSizeRet = symbolIt->second.symbol.size;
            }
            *globalVariablePointerRet = reinterpret_cast<void *>(symbolIt->second.gpuAddress);
        }
    }

    return retVal;
}

cl_int CL_API_CALL clGetDeviceFunctionPointerINTEL(
    cl_device_id device,
    cl_program program,
    const char *functionName,
    cl_ulong *functionPointerRet) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("device", device, "program", program,
                   "functionName", functionName,
                   "functionPointerRet", functionPointerRet);
    retVal = validateObjects(device, program);
    if ((CL_SUCCESS == retVal) && (functionPointerRet == nullptr)) {
        retVal = CL_INVALID_ARG_VALUE;
    }

    if (CL_SUCCESS == retVal) {
        Program *pProgram = (Program *)(program);
        const auto &symbols = pProgram->getSymbols();
        auto symbolIt = symbols.find(functionName);
        if ((symbolIt == symbols.end()) || (symbolIt->second.symbol.type != NEO::SymbolInfo::Function)) {
            retVal = CL_INVALID_ARG_VALUE;
        } else {
            *functionPointerRet = static_cast<cl_ulong>(symbolIt->second.gpuAddress);
        }
    }

    return retVal;
}

cl_int CL_API_CALL clSetProgramSpecializationConstant(cl_program program, cl_uint specId, size_t specSize, const void *specValue) {
    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("program", program,
                   "specId", specId,
                   "specSize", specSize,
                   "specValue", specValue);

    Program *pProgram = nullptr;
    retVal = validateObjects(WithCastToInternal(program, &pProgram), specValue);

    if (retVal == CL_SUCCESS) {
        retVal = pProgram->setProgramSpecializationConstant(specId, specSize, specValue);
    }

    return retVal;
}

cl_int CL_API_CALL clGetExecutionInfoINTEL(cl_command_queue commandQueue,
                                           cl_kernel kernel,
                                           cl_uint workDim,
                                           const size_t *globalWorkOffset,
                                           const size_t *localWorkSize,
                                           cl_execution_info_intel paramName,
                                           size_t paramValueSize,
                                           void *paramValue,
                                           size_t *paramValueSizeRet) {

    cl_int retVal = CL_SUCCESS;
    API_ENTER(&retVal);
    DBG_LOG_INPUTS("commandQueue", commandQueue, "cl_kernel", kernel,
                   "globalWorkOffset[0]", DebugManager.getInput(globalWorkOffset, 0),
                   "globalWorkOffset[1]", DebugManager.getInput(globalWorkOffset, 1),
                   "globalWorkOffset[2]", DebugManager.getInput(globalWorkOffset, 2),
                   "localWorkSize", DebugManager.getSizes(localWorkSize, workDim, true),
                   "paramName", paramName, "paramValueSize", paramValueSize,
                   "paramValue", paramValue, "paramValueSizeRet", paramValueSizeRet);

    retVal = validateObjects(commandQueue, kernel);

    if (CL_SUCCESS != retVal) {
        return retVal;
    }

    auto pKernel = castToObjectOrAbort<Kernel>(kernel);
    if (!pKernel->isPatched()) {
        retVal = CL_INVALID_KERNEL;
        return retVal;
    }

    TakeOwnershipWrapper<Kernel> kernelOwnership(*pKernel, gtpinIsGTPinInitialized());
    switch (paramName) {
    case CL_EXECUTION_INFO_MAX_WORKGROUP_COUNT_INTEL:
        if ((paramValueSize < sizeof(uint32_t)) || (paramValue == nullptr)) {
            retVal = CL_INVALID_VALUE;
            return retVal;
        }
        *reinterpret_cast<uint32_t *>(paramValue) = pKernel->getMaxWorkGroupCount(workDim, localWorkSize);
        if (paramValueSizeRet != nullptr) {
            *paramValueSizeRet = sizeof(uint32_t);
        }
        break;
    default:
        retVal = CL_INVALID_VALUE;
    }

    return retVal;
}
