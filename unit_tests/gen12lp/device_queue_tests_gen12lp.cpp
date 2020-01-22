/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "unit_tests/fixtures/device_host_queue_fixture.h"
#include "unit_tests/mocks/mock_context.h"
#include "unit_tests/mocks/mock_device.h"
#include "unit_tests/mocks/mock_device_queue.h"
#include "unit_tests/mocks/mock_kernel.h"

using namespace NEO;
using namespace DeviceHostQueue;

GEN12LPTEST_F(DeviceQueueHwTest, givenDeviceQueueWhenRunningOnCCsThenFfidSkipOffsetIsAddedToBlockKernelStartPointer) {
    auto device = pContext->getDevice(0);
    std::unique_ptr<MockParentKernel> mockParentKernel(MockParentKernel::create(*pContext));
    KernelInfo *blockInfo = const_cast<KernelInfo *>(mockParentKernel->mockProgram->blockKernelManager->getBlockKernelInfo(0));
    blockInfo->createKernelAllocation(device->getRootDeviceIndex(), device->getMemoryManager());
    ASSERT_NE(nullptr, blockInfo->getGraphicsAllocation());
    const_cast<SPatchThreadPayload *>(blockInfo->patchInfo.threadPayload)->OffsetToSkipSetFFIDGP = 0x1234;

    const_cast<HardwareInfo &>(device->getHardwareInfo()).platform.usRevId = REVISION_A0;

    uint64_t expectedOffset = blockInfo->getGraphicsAllocation()->getGpuAddressToPatch() + blockInfo->patchInfo.threadPayload->OffsetToSkipSetFFIDGP;
    uint64_t offset = MockDeviceQueueHw<FamilyType>::getBlockKernelStartPointer(*device, blockInfo, true);
    EXPECT_EQ(expectedOffset, offset);

    expectedOffset = blockInfo->getGraphicsAllocation()->getGpuAddressToPatch();
    offset = MockDeviceQueueHw<FamilyType>::getBlockKernelStartPointer(*device, blockInfo, false);
    EXPECT_EQ(expectedOffset, offset);

    const_cast<HardwareInfo &>(device->getHardwareInfo()).platform.usRevId = REVISION_B;

    expectedOffset = blockInfo->getGraphicsAllocation()->getGpuAddressToPatch();
    offset = MockDeviceQueueHw<FamilyType>::getBlockKernelStartPointer(*device, blockInfo, true);
    EXPECT_EQ(expectedOffset, offset);

    offset = MockDeviceQueueHw<FamilyType>::getBlockKernelStartPointer(*device, blockInfo, false);
    EXPECT_EQ(expectedOffset, offset);
}
