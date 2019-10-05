/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "core/unit_tests/helpers/debug_manager_state_restore.h"
#include "runtime/device_queue/device_queue.h"
#include "unit_tests/command_queue/command_queue_fixture.h"
#include "unit_tests/fixtures/execution_model_kernel_fixture.h"
#include "unit_tests/mocks/mock_kernel.h"

class DeviceQueueFixture {
  public:
    void SetUp(Context *context, Device *device) {
        cl_int errcodeRet = 0;
        cl_queue_properties properties[3];

        properties[0] = CL_QUEUE_PROPERTIES;
        properties[1] = CL_QUEUE_ON_DEVICE | CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE;
        properties[2] = 0;

        ASSERT_NE(nullptr, context);
        ASSERT_NE(nullptr, device);

        pDevQueue = DeviceQueue::create(context, device,
                                        properties[0],
                                        errcodeRet);

        ASSERT_NE(nullptr, pDevQueue);

        auto devQueue = context->getDefaultDeviceQueue();

        ASSERT_NE(nullptr, devQueue);
        EXPECT_EQ(pDevQueue, devQueue);
    }

    void TearDown() {
        delete pDevQueue;
    }

    DeviceQueue *pDevQueue;
};

class ExecutionModelKernelTest : public ExecutionModelKernelFixture,
                                 public CommandQueueHwFixture,
                                 public DeviceQueueFixture {
  public:
    ExecutionModelKernelTest(){};

    void SetUp() override {
        DebugManager.flags.EnableTimestampPacket.set(0);
        ExecutionModelKernelFixture::SetUp();
        CommandQueueHwFixture::SetUp(pDevice, 0);
        DeviceQueueFixture::SetUp(context, pDevice);
    }

    void TearDown() override {

        DeviceQueueFixture::TearDown();
        CommandQueueHwFixture::TearDown();
        ExecutionModelKernelFixture::TearDown();
    }

    std::unique_ptr<KernelOperation> createBlockedCommandsData(CommandQueue &commandQueue) {
        auto commandStream = new LinearStream();

        auto &gpgpuCsr = commandQueue.getGpgpuCommandStreamReceiver();
        gpgpuCsr.ensureCommandBufferAllocation(*commandStream, 1, 1);

        return std::make_unique<KernelOperation>(commandStream, *gpgpuCsr.getInternalAllocationStorage());
    }

    DebugManagerStateRestore dbgRestore;
};

class ExecutionModelSchedulerTest : public DeviceFixture,
                                    public CommandQueueHwFixture,
                                    public DeviceQueueFixture {
  public:
    ExecutionModelSchedulerTest(){};

    void SetUp() override {
        DeviceFixture::SetUp();
        CommandQueueHwFixture::SetUp(pDevice, 0);
        DeviceQueueFixture::SetUp(context, pDevice);

        parentKernel = MockParentKernel::create(*context);
        ASSERT_NE(nullptr, parentKernel);
    }

    void TearDown() override {
        parentKernel->release();

        DeviceQueueFixture::TearDown();
        CommandQueueHwFixture::TearDown();
        DeviceFixture::TearDown();
    }

    MockParentKernel *parentKernel;
};

struct ParentKernelCommandQueueFixture : public CommandQueueHwFixture,
                                         testing::Test {

    void SetUp() override {
        device = MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr);
        CommandQueueHwFixture::SetUp(device, 0);
    }
    void TearDown() override {
        CommandQueueHwFixture::TearDown();
        delete device;
    }

    std::unique_ptr<KernelOperation> createBlockedCommandsData(CommandQueue &commandQueue) {
        auto commandStream = new LinearStream();

        auto &gpgpuCsr = commandQueue.getGpgpuCommandStreamReceiver();
        gpgpuCsr.ensureCommandBufferAllocation(*commandStream, 1, 1);

        return std::make_unique<KernelOperation>(commandStream, *gpgpuCsr.getInternalAllocationStorage());
    }

    MockDevice *device;
};
