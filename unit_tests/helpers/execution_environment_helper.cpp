/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "unit_tests/helpers/execution_environment_helper.h"

#include "runtime/os_interface/device_factory.h"
#include "runtime/platform/platform.h"

namespace NEO {

ExecutionEnvironment *getExecutionEnvironmentImpl(HardwareInfo *&hwInfo) {
    ExecutionEnvironment *executionEnvironment = platformImpl->peekExecutionEnvironment();
    size_t numDevicesReturned = 0;
    hwInfo = nullptr;
    DeviceFactory::getDevices(numDevicesReturned, *executionEnvironment);
    hwInfo = executionEnvironment->getMutableHardwareInfo();
    executionEnvironment->initializeMemoryManager();

    return executionEnvironment;
}
} // namespace NEO
