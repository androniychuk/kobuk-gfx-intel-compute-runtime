/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/fixtures/execution_model_kernel_fixture.h"
#include "opencl/test/unit_test/mocks/mock_device.h"
#include "opencl/test/unit_test/mocks/mock_kernel.h"
#include "opencl/test/unit_test/mocks/mock_program.h"
#include "test.h"

#include <memory>

using namespace NEO;

TEST(DebugKernelTest, givenKernelCompiledForDebuggingWhenGetDebugSurfaceBtiIsCalledThenCorrectValueIsReturned) {
    auto device = std::make_unique<MockClDevice>(new MockDevice);
    MockProgram program(*device->getExecutionEnvironment());
    program.enableKernelDebug();
    std::unique_ptr<MockKernel> kernel(MockKernel::create<MockDebugKernel>(device->getDevice(), &program));

    EXPECT_EQ(0, kernel->getDebugSurfaceBti());
}

TEST(DebugKernelTest, givenKernelCompiledForDebuggingWhenGetPerThreadSystemThreadSurfaceSizeIsCalledThenCorrectValueIsReturned) {
    auto device = std::make_unique<MockClDevice>(new MockDevice);
    MockProgram program(*device->getExecutionEnvironment());
    program.enableKernelDebug();
    std::unique_ptr<MockDebugKernel> kernel(MockKernel::create<MockDebugKernel>(device->getDevice(), &program));

    EXPECT_EQ(MockDebugKernel::perThreadSystemThreadSurfaceSize, kernel->getPerThreadSystemThreadSurfaceSize());
}

TEST(DebugKernelTest, givenKernelWithoutDebugFlagWhenGetDebugSurfaceBtiIsCalledThenInvalidIndexValueIsReturned) {
    auto device = std::make_unique<MockClDevice>(new MockDevice);
    MockProgram program(*device->getExecutionEnvironment());
    program.enableKernelDebug();
    std::unique_ptr<MockKernel> kernel(MockKernel::create<MockKernel>(device->getDevice(), &program));

    EXPECT_EQ(-1, kernel->getDebugSurfaceBti());
}

TEST(DebugKernelTest, givenKernelWithoutDebugFlagWhenGetPerThreadSystemThreadSurfaceSizeIsCalledThenZeroIsReturned) {
    auto device = std::make_unique<MockClDevice>(new MockDevice);
    MockProgram program(*device->getExecutionEnvironment());
    program.enableKernelDebug();
    std::unique_ptr<MockKernel> kernel(MockKernel::create<MockKernel>(device->getDevice(), &program));

    EXPECT_EQ(0u, kernel->getPerThreadSystemThreadSurfaceSize());
}
