/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/execution_environment/execution_environment.h"
#include "runtime/helpers/hw_info.h"
#include "test.h"

namespace NEO {
bool getDevices(size_t &numDevicesReturned, ExecutionEnvironment &executionEnvironment);
} // namespace NEO

using GetDevicesTests = ::testing::Test;

HWTEST_F(GetDevicesTests, WhenGetDevicesIsCalledThenSuccessIsReturned) {
    size_t numDevicesReturned = 0;
    NEO::ExecutionEnvironment executionEnviornment;

    auto returnValue = NEO::getDevices(numDevicesReturned, executionEnviornment);
    EXPECT_EQ(true, returnValue);
}
