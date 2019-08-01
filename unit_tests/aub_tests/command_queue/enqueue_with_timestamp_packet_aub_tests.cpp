/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/unit_tests/helpers/debug_manager_state_restore.h"
#include "runtime/event/event.h"
#include "runtime/helpers/hardware_commands_helper.h"
#include "runtime/helpers/timestamp_packet.h"
#include "runtime/mem_obj/buffer.h"
#include "runtime/utilities/tag_allocator.h"
#include "test.h"
#include "unit_tests/aub_tests/command_queue/command_enqueue_fixture.h"

using namespace NEO;

struct TimestampPacketAubTests : public CommandEnqueueAUBFixture, public ::testing::Test {
    void SetUp() override {
        DebugManagerStateRestore restore;
        DebugManager.flags.EnableTimestampPacket.set(true);
        CommandEnqueueAUBFixture::SetUp();
    }

    void TearDown() override {
        CommandEnqueueAUBFixture::TearDown();
    }
};

HWTEST_F(TimestampPacketAubTests, givenTwoBatchedEnqueuesWhenDependencyIsResolvedThenDecrementCounterOnGpu) {
    MockContext context(&pCmdQ->getDevice());
    pCommandStreamReceiver->overrideDispatchPolicy(DispatchMode::BatchedDispatch);

    const size_t bufferSize = 1024;
    auto retVal = CL_SUCCESS;
    uint8_t initialMemory[bufferSize] = {};
    uint8_t writePattern1[bufferSize];
    uint8_t writePattern2[bufferSize];
    std::fill(writePattern1, writePattern1 + sizeof(writePattern1), 1);
    std::fill(writePattern2, writePattern2 + sizeof(writePattern2), 1);

    auto buffer = std::unique_ptr<Buffer>(Buffer::create(&context, CL_MEM_COPY_HOST_PTR, bufferSize, initialMemory, retVal));
    buffer->forceDisallowCPUCopy = true;
    cl_event outEvent1, outEvent2;

    pCmdQ->enqueueWriteBuffer(buffer.get(), CL_FALSE, 0, bufferSize, writePattern1, nullptr, 0, nullptr, &outEvent1);
    auto node1 = castToObject<Event>(outEvent1)->getTimestampPacketNodes()->peekNodes().at(0);
    node1->getBaseGraphicsAllocation()->setAubWritable(true); // allow to write again after Buffer::create

    pCmdQ->enqueueWriteBuffer(buffer.get(), CL_TRUE, 0, bufferSize, writePattern2, nullptr, 0, nullptr, &outEvent2);
    auto node2 = castToObject<Event>(outEvent2)->getTimestampPacketNodes()->peekNodes().at(0);

    expectMemory<FamilyType>(reinterpret_cast<void *>(buffer->getGraphicsAllocation()->getGpuAddress()), writePattern2, bufferSize);

    uint32_t expectedDepsCount = 0;
    auto dependenciesGpuAddress = node1->getGpuAddress() + offsetof(TimestampPacketStorage, implicitDependenciesCount);
    expectMemory<FamilyType>(reinterpret_cast<void *>(dependenciesGpuAddress),
                             &expectedDepsCount, sizeof(uint32_t));

    uint32_t expectedEndTimestamp[2] = {0, 0};
    auto endTimestampAddress1 = node1->getGpuAddress() + offsetof(TimestampPacketStorage, packets[0].contextEnd);
    auto endTimestampAddress2 = node2->getGpuAddress() + offsetof(TimestampPacketStorage, packets[0].contextEnd);
    expectMemory<FamilyType>(reinterpret_cast<void *>(endTimestampAddress1), expectedEndTimestamp, 2 * sizeof(uint32_t));
    expectMemory<FamilyType>(reinterpret_cast<void *>(endTimestampAddress2), expectedEndTimestamp, 2 * sizeof(uint32_t));

    clReleaseEvent(outEvent1);
    clReleaseEvent(outEvent2);
}

HWTEST_F(TimestampPacketAubTests, givenMultipleWalkersWhenEnqueueingThenWriteAllTimestamps) {
    MockContext context(&pCmdQ->getDevice());
    const size_t bufferSize = 70;
    const size_t writeSize = bufferSize - 2;
    uint8_t writeData[writeSize] = {};
    cl_int retVal = CL_SUCCESS;
    cl_event outEvent;

    auto buffer = std::unique_ptr<Buffer>(Buffer::create(&context, CL_MEM_READ_WRITE, bufferSize, nullptr, retVal));
    buffer->forceDisallowCPUCopy = true;

    pCmdQ->enqueueWriteBuffer(buffer.get(), CL_TRUE, 1, writeSize, writeData, nullptr, 0, nullptr, &outEvent);

    auto &timestampNodes = castToObject<Event>(outEvent)->getTimestampPacketNodes()->peekNodes();

    EXPECT_EQ(2u, timestampNodes.size());

    uint32_t expectedEndTimestamp[2] = {0, 0};
    auto endTimestampAddress1 = timestampNodes.at(0)->getGpuAddress() + offsetof(TimestampPacketStorage, packets[0].contextEnd);
    auto endTimestampAddress2 = timestampNodes.at(1)->getGpuAddress() + offsetof(TimestampPacketStorage, packets[0].contextEnd);
    expectMemory<FamilyType>(reinterpret_cast<void *>(endTimestampAddress1), expectedEndTimestamp, 2 * sizeof(uint32_t));
    expectMemory<FamilyType>(reinterpret_cast<void *>(endTimestampAddress2), expectedEndTimestamp, 2 * sizeof(uint32_t));

    clReleaseEvent(outEvent);
}
