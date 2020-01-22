/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "runtime/api/cl_types.h"
#include "runtime/device/device_vector.h"
#include "runtime/helpers/base_object.h"

#include "platform_info.h"

#include <condition_variable>
#include <vector>

namespace NEO {

class CompilerInterface;
class RootDevice;
class Device;
class AsyncEventsHandler;
class ExecutionEnvironment;
class GmmHelper;
class GmmClientContext;
struct HardwareInfo;

template <>
struct OpenCLObjectMapper<_cl_platform_id> {
    typedef class Platform DerivedType;
};

class Platform : public BaseObject<_cl_platform_id> {
  public:
    static const cl_ulong objectMagic = 0x8873ACDEF2342133LL;

    Platform();
    ~Platform() override;

    Platform(const Platform &) = delete;
    Platform &operator=(Platform const &) = delete;

    cl_int getInfo(cl_platform_info paramName,
                   size_t paramValueSize,
                   void *paramValue,
                   size_t *paramValueSizeRet);

    const std::string &peekCompilerExtensions() const;

    bool initialize();
    bool isInitialized();

    size_t getNumDevices() const;
    Device **getDevices();
    Device *getDevice(size_t deviceOrdinal);

    const PlatformInfo &getPlatformInfo() const;
    AsyncEventsHandler *getAsyncEventsHandler();
    std::unique_ptr<AsyncEventsHandler> setAsyncEventsHandler(std::unique_ptr<AsyncEventsHandler> handler);
    ExecutionEnvironment *peekExecutionEnvironment() const { return executionEnvironment; }
    GmmHelper *peekGmmHelper() const;
    GmmClientContext *peekGmmClientContext() const;

  protected:
    enum {
        StateNone,
        StateIniting,
        StateInited,
    };
    cl_uint state = StateNone;
    void fillGlobalDispatchTable();
    MOCKABLE_VIRTUAL void initializationLoopHelper(){};
    MOCKABLE_VIRTUAL RootDevice *createRootDevice(uint32_t rootDeviceIndex) const;
    std::unique_ptr<PlatformInfo> platformInfo;
    DeviceVector devices;
    std::string compilerExtensions;
    std::unique_ptr<AsyncEventsHandler> asyncEventsHandler;
    ExecutionEnvironment *executionEnvironment = nullptr;
};

extern std::unique_ptr<Platform> platformImpl;
Platform *platform();
Platform *constructPlatform();
} // namespace NEO
