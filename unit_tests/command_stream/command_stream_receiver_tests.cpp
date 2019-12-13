/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/command_stream/linear_stream.h"
#include "core/command_stream/preemption.h"
#include "core/memory_manager/graphics_allocation.h"
#include "core/unit_tests/helpers/debug_manager_state_restore.h"
#include "runtime/aub_mem_dump/aub_services.h"
#include "runtime/command_stream/command_stream_receiver.h"
#include "runtime/helpers/cache_policy.h"
#include "runtime/helpers/timestamp_packet.h"
#include "runtime/mem_obj/buffer.h"
#include "runtime/memory_manager/internal_allocation_storage.h"
#include "runtime/memory_manager/memory_manager.h"
#include "runtime/memory_manager/surface.h"
#include "runtime/platform/platform.h"
#include "runtime/utilities/tag_allocator.h"
#include "test.h"
#include "unit_tests/fixtures/device_fixture.h"
#include "unit_tests/gen_common/matchers.h"
#include "unit_tests/helpers/unit_test_helper.h"
#include "unit_tests/mocks/mock_buffer.h"
#include "unit_tests/mocks/mock_builtins.h"
#include "unit_tests/mocks/mock_context.h"
#include "unit_tests/mocks/mock_csr.h"
#include "unit_tests/mocks/mock_execution_environment.h"
#include "unit_tests/mocks/mock_graphics_allocation.h"
#include "unit_tests/mocks/mock_memory_manager.h"
#include "unit_tests/mocks/mock_program.h"

#include "gmock/gmock.h"

using namespace NEO;

struct CommandStreamReceiverTest : public DeviceFixture,
                                   public ::testing::Test {
    void SetUp() override {
        DeviceFixture::SetUp();

        commandStreamReceiver = &pDevice->getGpgpuCommandStreamReceiver();
        ASSERT_NE(nullptr, commandStreamReceiver);
        memoryManager = commandStreamReceiver->getMemoryManager();
        internalAllocationStorage = commandStreamReceiver->getInternalAllocationStorage();
    }

    void TearDown() override {
        DeviceFixture::TearDown();
    }

    CommandStreamReceiver *commandStreamReceiver;
    MemoryManager *memoryManager;
    InternalAllocationStorage *internalAllocationStorage;
};

HWTEST_F(CommandStreamReceiverTest, testCtor) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    EXPECT_EQ(0u, csr.peekTaskLevel());
    EXPECT_EQ(0u, csr.peekTaskCount());
    EXPECT_FALSE(csr.isPreambleSent);
}

HWTEST_F(CommandStreamReceiverTest, testInitProgrammingFlags) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    csr.initProgrammingFlags();
    EXPECT_FALSE(csr.isPreambleSent);
    EXPECT_FALSE(csr.GSBAFor32BitProgrammed);
    EXPECT_TRUE(csr.mediaVfeStateDirty);
    EXPECT_FALSE(csr.lastVmeSubslicesConfig);
    EXPECT_EQ(0u, csr.lastSentL3Config);
    EXPECT_EQ(-1, csr.lastSentCoherencyRequest);
    EXPECT_EQ(-1, csr.lastMediaSamplerConfig);
    EXPECT_EQ(PreemptionMode::Initial, csr.lastPreemptionMode);
    EXPECT_EQ(0u, csr.latestSentStatelessMocsConfig);
}

TEST_F(CommandStreamReceiverTest, makeResident_setsBufferResidencyFlag) {
    MockContext context;
    float srcMemory[] = {1.0f};

    auto retVal = CL_INVALID_VALUE;
    auto buffer = Buffer::create(
        &context,
        CL_MEM_USE_HOST_PTR,
        sizeof(srcMemory),
        srcMemory,
        retVal);
    ASSERT_NE(nullptr, buffer);
    EXPECT_FALSE(buffer->getGraphicsAllocation()->isResident(commandStreamReceiver->getOsContext().getContextId()));

    commandStreamReceiver->makeResident(*buffer->getGraphicsAllocation());

    EXPECT_TRUE(buffer->getGraphicsAllocation()->isResident(commandStreamReceiver->getOsContext().getContextId()));

    delete buffer;
}

TEST_F(CommandStreamReceiverTest, commandStreamReceiverFromDeviceHasATagValue) {
    EXPECT_NE(nullptr, const_cast<uint32_t *>(commandStreamReceiver->getTagAddress()));
}

TEST_F(CommandStreamReceiverTest, GetCommandStreamReturnsValidObject) {
    auto &cs = commandStreamReceiver->getCS();
    EXPECT_NE(nullptr, &cs);
}

TEST_F(CommandStreamReceiverTest, getCommandStreamContainsMemoryForRequest) {
    size_t requiredSize = 16384;
    const auto &commandStream = commandStreamReceiver->getCS(requiredSize);
    ASSERT_NE(nullptr, &commandStream);
    EXPECT_GE(commandStream.getAvailableSpace(), requiredSize);
}

TEST_F(CommandStreamReceiverTest, getCsReturnsCsWithCsOverfetchSizeIncludedInGraphicsAllocation) {
    size_t sizeRequested = 560;
    const auto &commandStream = commandStreamReceiver->getCS(sizeRequested);
    ASSERT_NE(nullptr, &commandStream);
    auto *allocation = commandStream.getGraphicsAllocation();
    ASSERT_NE(nullptr, allocation);

    size_t expectedTotalSize = alignUp(sizeRequested + MemoryConstants::cacheLineSize + CSRequirements::csOverfetchSize, MemoryConstants::pageSize64k);

    EXPECT_LT(commandStream.getAvailableSpace(), expectedTotalSize);
    EXPECT_LE(commandStream.getAvailableSpace(), expectedTotalSize - CSRequirements::csOverfetchSize);
    EXPECT_EQ(expectedTotalSize, allocation->getUnderlyingBufferSize());
}

TEST_F(CommandStreamReceiverTest, getCommandStreamCanRecycle) {
    auto &commandStreamInitial = commandStreamReceiver->getCS();
    size_t requiredSize = commandStreamInitial.getMaxAvailableSpace() + 42;

    const auto &commandStream = commandStreamReceiver->getCS(requiredSize);
    ASSERT_NE(nullptr, &commandStream);
    EXPECT_GE(commandStream.getMaxAvailableSpace(), requiredSize);
}

TEST_F(CommandStreamReceiverTest, givenCommandStreamReceiverWhenGetCSIsCalledThenCommandStreamAllocationTypeShouldBeSetToLinearStream) {
    const auto &commandStream = commandStreamReceiver->getCS();
    auto commandStreamAllocation = commandStream.getGraphicsAllocation();
    ASSERT_NE(nullptr, commandStreamAllocation);

    EXPECT_EQ(GraphicsAllocation::AllocationType::COMMAND_BUFFER, commandStreamAllocation->getAllocationType());
}

HWTEST_F(CommandStreamReceiverTest, whenStoreAllocationThenStoredAllocationHasTaskCountFromCsr) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    auto *memoryManager = csr.getMemoryManager();
    auto allocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{MemoryConstants::pageSize});

    EXPECT_FALSE(allocation->isUsed());

    csr.taskCount = 2u;

    csr.getInternalAllocationStorage()->storeAllocation(std::unique_ptr<GraphicsAllocation>(allocation), REUSABLE_ALLOCATION);

    EXPECT_EQ(csr.peekTaskCount(), allocation->getTaskCount(csr.getOsContext().getContextId()));
}

HWTEST_F(CommandStreamReceiverTest, givenCommandStreamReceiverWhenCheckedForInitialStatusOfStatelessMocsIndexThenUnknownMocsIsReturend) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    EXPECT_EQ(CacheSettings::unknownMocs, csr.latestSentStatelessMocsConfig);
}

TEST_F(CommandStreamReceiverTest, makeResidentPushesAllocationToMemoryManagerResidencyList) {
    auto *memoryManager = commandStreamReceiver->getMemoryManager();

    GraphicsAllocation *graphicsAllocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{MemoryConstants::pageSize});

    ASSERT_NE(nullptr, graphicsAllocation);

    commandStreamReceiver->makeResident(*graphicsAllocation);

    auto &residencyAllocations = commandStreamReceiver->getResidencyAllocations();

    ASSERT_EQ(1u, residencyAllocations.size());
    EXPECT_EQ(graphicsAllocation, residencyAllocations[0]);

    memoryManager->freeGraphicsMemory(graphicsAllocation);
}

TEST_F(CommandStreamReceiverTest, makeResidentWithoutParametersDoesNothing) {
    commandStreamReceiver->processResidency(commandStreamReceiver->getResidencyAllocations());
    auto &residencyAllocations = commandStreamReceiver->getResidencyAllocations();
    EXPECT_EQ(0u, residencyAllocations.size());
}

TEST_F(CommandStreamReceiverTest, givenForced32BitAddressingWhenDebugSurfaceIsAllocatedThenRegularAllocationIsReturned) {
    auto *memoryManager = commandStreamReceiver->getMemoryManager();
    memoryManager->setForce32BitAllocations(true);
    auto allocation = commandStreamReceiver->allocateDebugSurface(1024);
    EXPECT_FALSE(allocation->is32BitAllocation());
}

HWTEST_F(CommandStreamReceiverTest, givenDefaultCommandStreamReceiverThenDefaultDispatchingPolicyIsImmediateSubmission) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    EXPECT_EQ(DispatchMode::ImmediateDispatch, csr.dispatchMode);
}

HWTEST_F(CommandStreamReceiverTest, givenCsrWhenGetIndirectHeapIsCalledThenHeapIsReturned) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    auto &heap = csr.getIndirectHeap(IndirectHeap::DYNAMIC_STATE, 10u);
    EXPECT_NE(nullptr, heap.getGraphicsAllocation());
    EXPECT_NE(nullptr, csr.indirectHeap[IndirectHeap::DYNAMIC_STATE]);
    EXPECT_EQ(&heap, csr.indirectHeap[IndirectHeap::DYNAMIC_STATE]);
}

HWTEST_F(CommandStreamReceiverTest, givenCsrWhenReleaseIndirectHeapIsCalledThenHeapAllocationIsNull) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    auto &heap = csr.getIndirectHeap(IndirectHeap::DYNAMIC_STATE, 10u);
    csr.releaseIndirectHeap(IndirectHeap::DYNAMIC_STATE);
    EXPECT_EQ(nullptr, heap.getGraphicsAllocation());
    EXPECT_EQ(0u, heap.getMaxAvailableSpace());
}

HWTEST_F(CommandStreamReceiverTest, givenCsrWhenAllocateHeapMemoryIsCalledThenHeapMemoryIsAllocated) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    IndirectHeap *dsh = nullptr;
    csr.allocateHeapMemory(IndirectHeap::DYNAMIC_STATE, 4096u, dsh);
    EXPECT_NE(nullptr, dsh);
    ASSERT_NE(nullptr, dsh->getGraphicsAllocation());
    csr.getMemoryManager()->freeGraphicsMemory(dsh->getGraphicsAllocation());
    delete dsh;
}

TEST(CommandStreamReceiverSimpleTest, givenCSRWithoutTagAllocationWhenGetTagAllocationIsCalledThenNullptrIsReturned) {
    ExecutionEnvironment executionEnvironment;
    executionEnvironment.prepareRootDeviceEnvironments(1);
    executionEnvironment.initializeMemoryManager();
    MockCommandStreamReceiver csr(executionEnvironment, 0);
    EXPECT_EQ(nullptr, csr.getTagAllocation());
}

HWTEST_F(CommandStreamReceiverTest, givenDebugVariableEnabledWhenCreatingCsrThenEnableTimestampPacketWriteMode) {
    DebugManagerStateRestore restore;

    DebugManager.flags.EnableTimestampPacket.set(true);
    CommandStreamReceiverHw<FamilyType> csr1(*pDevice->executionEnvironment, pDevice->getRootDeviceIndex());
    EXPECT_TRUE(csr1.peekTimestampPacketWriteEnabled());

    DebugManager.flags.EnableTimestampPacket.set(false);
    CommandStreamReceiverHw<FamilyType> csr2(*pDevice->executionEnvironment, pDevice->getRootDeviceIndex());
    EXPECT_FALSE(csr2.peekTimestampPacketWriteEnabled());
}

HWTEST_F(CommandStreamReceiverTest, whenCsrIsCreatedThenUseTimestampPacketWriteIfPossible) {
    CommandStreamReceiverHw<FamilyType> csr(*pDevice->executionEnvironment, pDevice->getRootDeviceIndex());
    EXPECT_EQ(UnitTestHelper<FamilyType>::isTimestampPacketWriteSupported(), csr.peekTimestampPacketWriteEnabled());
}

TEST_F(CommandStreamReceiverTest, whenGetEventTsAllocatorIsCalledItReturnsSameTagAllocator) {
    TagAllocator<HwTimeStamps> *allocator = commandStreamReceiver->getEventTsAllocator();
    EXPECT_NE(nullptr, allocator);
    TagAllocator<HwTimeStamps> *allocator2 = commandStreamReceiver->getEventTsAllocator();
    EXPECT_EQ(allocator2, allocator);
}

TEST_F(CommandStreamReceiverTest, whenGetEventPerfCountAllocatorIsCalledItReturnsSameTagAllocator) {
    const uint32_t gpuReportSize = 100;
    TagAllocator<HwPerfCounter> *allocator = commandStreamReceiver->getEventPerfCountAllocator(gpuReportSize);
    EXPECT_NE(nullptr, allocator);
    TagAllocator<HwPerfCounter> *allocator2 = commandStreamReceiver->getEventPerfCountAllocator(gpuReportSize);
    EXPECT_EQ(allocator2, allocator);
}

HWTEST_F(CommandStreamReceiverTest, givenTimestampPacketAllocatorWhenAskingForTagThenReturnValidObject) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    EXPECT_EQ(nullptr, csr.timestampPacketAllocator.get());

    TagAllocator<TimestampPacketStorage> *allocator = csr.getTimestampPacketAllocator();
    EXPECT_NE(nullptr, csr.timestampPacketAllocator.get());
    EXPECT_EQ(allocator, csr.timestampPacketAllocator.get());

    TagAllocator<TimestampPacketStorage> *allocator2 = csr.getTimestampPacketAllocator();
    EXPECT_EQ(allocator, allocator2);

    auto node1 = allocator->getTag();
    auto node2 = allocator->getTag();
    EXPECT_NE(nullptr, node1);
    EXPECT_NE(nullptr, node2);
    EXPECT_NE(node1, node2);
}

HWTEST_F(CommandStreamReceiverTest, givenUltCommandStreamReceiverWhenAddAubCommentIsCalledThenCallAddAubCommentOnCsr) {
    auto &csr = pDevice->getUltCommandStreamReceiver<FamilyType>();
    csr.addAubComment("message");
    EXPECT_TRUE(csr.addAubCommentCalled);
}

TEST(CommandStreamReceiverSimpleTest, givenCommandStreamReceiverWhenItIsDestroyedThenItDestroysTagAllocation) {
    struct MockGraphicsAllocationWithDestructorTracing : public MockGraphicsAllocation {
        using MockGraphicsAllocation::MockGraphicsAllocation;
        ~MockGraphicsAllocationWithDestructorTracing() override { *destructorCalled = true; }
        bool *destructorCalled = nullptr;
    };

    bool destructorCalled = false;

    auto mockGraphicsAllocation = new MockGraphicsAllocationWithDestructorTracing(0, GraphicsAllocation::AllocationType::UNKNOWN, nullptr, 0llu, 0llu, 1u, MemoryPool::MemoryNull);
    mockGraphicsAllocation->destructorCalled = &destructorCalled;
    MockExecutionEnvironment executionEnvironment(*platformDevices);
    auto csr = std::make_unique<MockCommandStreamReceiver>(executionEnvironment, 0);
    executionEnvironment.memoryManager.reset(new OsAgnosticMemoryManager(executionEnvironment));
    csr->setTagAllocation(mockGraphicsAllocation);
    EXPECT_FALSE(destructorCalled);
    csr.reset(nullptr);
    EXPECT_TRUE(destructorCalled);
}

TEST(CommandStreamReceiverSimpleTest, givenCommandStreamReceiverWhenInitializeTagAllocationIsCalledThenTagAllocationIsBeingAllocated) {
    MockExecutionEnvironment executionEnvironment(*platformDevices);
    auto csr = std::make_unique<MockCommandStreamReceiver>(executionEnvironment, 0);
    executionEnvironment.memoryManager.reset(new OsAgnosticMemoryManager(executionEnvironment));
    EXPECT_EQ(nullptr, csr->getTagAllocation());
    EXPECT_TRUE(csr->getTagAddress() == nullptr);
    csr->initializeTagAllocation();
    EXPECT_NE(nullptr, csr->getTagAllocation());
    EXPECT_EQ(GraphicsAllocation::AllocationType::TAG_BUFFER, csr->getTagAllocation()->getAllocationType());
    EXPECT_TRUE(csr->getTagAddress() != nullptr);
    EXPECT_EQ(*csr->getTagAddress(), initialHardwareTag);
}

TEST(CommandStreamReceiverSimpleTest, givenNullHardwareDebugModeWhenInitializeTagAllocationIsCalledThenTagAllocationIsBeingAllocatedAndinitialValueIsMinusOne) {
    DebugManagerStateRestore dbgRestore;
    DebugManager.flags.EnableNullHardware.set(true);
    MockExecutionEnvironment executionEnvironment(*platformDevices);
    auto csr = std::make_unique<MockCommandStreamReceiver>(executionEnvironment, 0);
    executionEnvironment.memoryManager.reset(new OsAgnosticMemoryManager(executionEnvironment));
    EXPECT_EQ(nullptr, csr->getTagAllocation());
    EXPECT_TRUE(csr->getTagAddress() == nullptr);
    csr->initializeTagAllocation();
    EXPECT_NE(nullptr, csr->getTagAllocation());
    EXPECT_TRUE(csr->getTagAddress() != nullptr);
    EXPECT_EQ(*csr->getTagAddress(), static_cast<uint32_t>(-1));
}

TEST(CommandStreamReceiverSimpleTest, givenVariousDataSetsWhenVerifyingMemoryThenCorrectValueIsReturned) {
    ExecutionEnvironment executionEnvironment;
    executionEnvironment.prepareRootDeviceEnvironments(1);
    executionEnvironment.initializeMemoryManager();
    MockCommandStreamReceiver csr(executionEnvironment, 0);

    constexpr size_t setSize = 6;
    uint8_t setA1[setSize] = {4, 3, 2, 1, 2, 10};
    uint8_t setA2[setSize] = {4, 3, 2, 1, 2, 10};
    uint8_t setB1[setSize] = {40, 15, 3, 11, 17, 4};
    uint8_t setB2[setSize] = {40, 15, 3, 11, 17, 4};

    constexpr auto compareEqual = CmdServicesMemTraceMemoryCompare::CompareOperationValues::CompareEqual;
    constexpr auto compareNotEqual = CmdServicesMemTraceMemoryCompare::CompareOperationValues::CompareNotEqual;

    EXPECT_EQ(CL_SUCCESS, csr.expectMemory(setA1, setA2, setSize, compareEqual));
    EXPECT_EQ(CL_SUCCESS, csr.expectMemory(setB1, setB2, setSize, compareEqual));
    EXPECT_EQ(CL_INVALID_VALUE, csr.expectMemory(setA1, setA2, setSize, compareNotEqual));
    EXPECT_EQ(CL_INVALID_VALUE, csr.expectMemory(setB1, setB2, setSize, compareNotEqual));

    EXPECT_EQ(CL_INVALID_VALUE, csr.expectMemory(setA1, setB1, setSize, compareEqual));
    EXPECT_EQ(CL_INVALID_VALUE, csr.expectMemory(setA2, setB2, setSize, compareEqual));
    EXPECT_EQ(CL_SUCCESS, csr.expectMemory(setA1, setB1, setSize, compareNotEqual));
    EXPECT_EQ(CL_SUCCESS, csr.expectMemory(setA2, setB2, setSize, compareNotEqual));
}

TEST(CommandStreamReceiverMultiContextTests, givenMultipleCsrsWhenSameResourcesAreUsedThenResidencyIsProperlyHandled) {
    auto executionEnvironment = platformImpl->peekExecutionEnvironment();

    std::unique_ptr<MockDevice> device(Device::create<MockDevice>(executionEnvironment, 0u));

    auto &commandStreamReceiver0 = *device->commandStreamReceivers[0];
    auto &commandStreamReceiver1 = *device->commandStreamReceivers[1];

    auto csr0ContextId = commandStreamReceiver0.getOsContext().getContextId();
    auto csr1ContextId = commandStreamReceiver1.getOsContext().getContextId();

    MockGraphicsAllocation graphicsAllocation;

    commandStreamReceiver0.makeResident(graphicsAllocation);
    EXPECT_EQ(1u, commandStreamReceiver0.getResidencyAllocations().size());
    EXPECT_EQ(0u, commandStreamReceiver1.getResidencyAllocations().size());

    commandStreamReceiver1.makeResident(graphicsAllocation);
    EXPECT_EQ(1u, commandStreamReceiver0.getResidencyAllocations().size());
    EXPECT_EQ(1u, commandStreamReceiver1.getResidencyAllocations().size());

    EXPECT_EQ(1u, graphicsAllocation.getResidencyTaskCount(csr0ContextId));
    EXPECT_EQ(1u, graphicsAllocation.getResidencyTaskCount(csr1ContextId));

    commandStreamReceiver0.makeNonResident(graphicsAllocation);
    EXPECT_FALSE(graphicsAllocation.isResident(csr0ContextId));
    EXPECT_TRUE(graphicsAllocation.isResident(csr1ContextId));

    commandStreamReceiver1.makeNonResident(graphicsAllocation);
    EXPECT_FALSE(graphicsAllocation.isResident(csr0ContextId));
    EXPECT_FALSE(graphicsAllocation.isResident(csr1ContextId));

    EXPECT_EQ(1u, commandStreamReceiver0.getEvictionAllocations().size());
    EXPECT_EQ(1u, commandStreamReceiver1.getEvictionAllocations().size());
}

struct CreateAllocationForHostSurfaceTest : public ::testing::Test {
    void SetUp() override {
        executionEnvironment = platformImpl->peekExecutionEnvironment();
        executionEnvironment->setHwInfo(&hwInfo);
        gmockMemoryManager = new ::testing::NiceMock<GMockMemoryManager>(*executionEnvironment);
        executionEnvironment->memoryManager.reset(gmockMemoryManager);
        device.reset(MockDevice::create<MockDevice>(executionEnvironment, 0u));
        commandStreamReceiver = &device->getGpgpuCommandStreamReceiver();
    }
    HardwareInfo hwInfo = *platformDevices[0];
    ExecutionEnvironment *executionEnvironment = nullptr;
    GMockMemoryManager *gmockMemoryManager = nullptr;
    std::unique_ptr<MockDevice> device;
    CommandStreamReceiver *commandStreamReceiver = nullptr;
};

TEST_F(CreateAllocationForHostSurfaceTest, givenReadOnlyHostPointerWhenAllocationForHostSurfaceWithPtrCopyAllowedIsCreatedThenCopyAllocationIsCreatedAndMemoryCopied) {
    const char memory[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t size = sizeof(memory);
    HostPtrSurface surface(const_cast<char *>(memory), size, true);

    if (device->isFullRangeSvm()) {
        EXPECT_CALL(*gmockMemoryManager, populateOsHandles(::testing::_))
            .Times(1)
            .WillOnce(::testing::Return(MemoryManager::AllocationStatus::InvalidHostPointer));
    } else {
        EXPECT_CALL(*gmockMemoryManager, allocateGraphicsMemoryForNonSvmHostPtr(::testing::_))
            .Times(1)
            .WillOnce(::testing::Return(nullptr));
    }

    bool result = commandStreamReceiver->createAllocationForHostSurface(surface, false);
    EXPECT_TRUE(result);

    auto allocation = surface.getAllocation();
    ASSERT_NE(nullptr, allocation);

    EXPECT_NE(memory, allocation->getUnderlyingBuffer());
    EXPECT_THAT(allocation->getUnderlyingBuffer(), MemCompare(memory, size));

    allocation->updateTaskCount(commandStreamReceiver->peekLatestFlushedTaskCount(), commandStreamReceiver->getOsContext().getContextId());
}

TEST_F(CreateAllocationForHostSurfaceTest, givenReadOnlyHostPointerWhenAllocationForHostSurfaceWithPtrCopyNotAllowedIsCreatedThenCopyAllocationIsNotCreated) {
    const char memory[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    size_t size = sizeof(memory);
    HostPtrSurface surface(const_cast<char *>(memory), size, false);

    if (device->isFullRangeSvm()) {
        EXPECT_CALL(*gmockMemoryManager, populateOsHandles(::testing::_))
            .Times(1)
            .WillOnce(::testing::Return(MemoryManager::AllocationStatus::InvalidHostPointer));
    } else {
        EXPECT_CALL(*gmockMemoryManager, allocateGraphicsMemoryForNonSvmHostPtr(::testing::_))
            .Times(1)
            .WillOnce(::testing::Return(nullptr));
    }

    bool result = commandStreamReceiver->createAllocationForHostSurface(surface, false);
    EXPECT_FALSE(result);

    auto allocation = surface.getAllocation();
    EXPECT_EQ(nullptr, allocation);
}

struct ReducedAddrSpaceCommandStreamReceiverTest : public CreateAllocationForHostSurfaceTest {
    void SetUp() override {
        hwInfo.capabilityTable.gpuAddressSpace = MemoryConstants::max32BitAddress;
        CreateAllocationForHostSurfaceTest::SetUp();
    }
};

TEST_F(ReducedAddrSpaceCommandStreamReceiverTest,
       givenReducedGpuAddressSpaceWhenAllocationForHostSurfaceIsCreatedThenAllocateGraphicsMemoryForNonSvmHostPtrIsCalled) {

    char memory[8] = {};
    HostPtrSurface surface(memory, sizeof(memory), false);

    EXPECT_CALL(*gmockMemoryManager, allocateGraphicsMemoryForNonSvmHostPtr(::testing::_))
        .Times(1)
        .WillOnce(::testing::Return(nullptr));

    bool result = commandStreamReceiver->createAllocationForHostSurface(surface, false);
    EXPECT_FALSE(result);
}

TEST_F(CommandStreamReceiverTest, givenMinimumSizeDoesNotExceedCurrentWhenCallingEnsureCommandBufferAllocationThenDoNotReallocate) {
    GraphicsAllocation *allocation = memoryManager->allocateGraphicsMemoryWithProperties({commandStreamReceiver->getRootDeviceIndex(), 128u, GraphicsAllocation::AllocationType::COMMAND_BUFFER});
    LinearStream commandStream{allocation};

    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, 100u, 0u);
    EXPECT_EQ(allocation, commandStream.getGraphicsAllocation());
    EXPECT_EQ(128u, commandStream.getMaxAvailableSpace());

    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, 128u, 0u);
    EXPECT_EQ(allocation, commandStream.getGraphicsAllocation());
    EXPECT_EQ(128u, commandStream.getMaxAvailableSpace());

    memoryManager->freeGraphicsMemory(commandStream.getGraphicsAllocation());
}

TEST_F(CommandStreamReceiverTest, givenMinimumSizeExceedsCurrentWhenCallingEnsureCommandBufferAllocationThenReallocate) {
    GraphicsAllocation *allocation = memoryManager->allocateGraphicsMemoryWithProperties({commandStreamReceiver->getRootDeviceIndex(), 128u, GraphicsAllocation::AllocationType::COMMAND_BUFFER});
    LinearStream commandStream{allocation};

    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, 129u, 0u);
    EXPECT_NE(allocation, commandStream.getGraphicsAllocation());
    memoryManager->freeGraphicsMemory(commandStream.getGraphicsAllocation());
}

TEST_F(CommandStreamReceiverTest, givenMinimumSizeExceedsCurrentWhenCallingEnsureCommandBufferAllocationThenReallocateAndAlignSizeTo64kb) {
    GraphicsAllocation *allocation = memoryManager->allocateGraphicsMemoryWithProperties({commandStreamReceiver->getRootDeviceIndex(), 128u, GraphicsAllocation::AllocationType::COMMAND_BUFFER});
    LinearStream commandStream{allocation};

    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, 129u, 0u);
    EXPECT_EQ(MemoryConstants::pageSize64k, commandStream.getGraphicsAllocation()->getUnderlyingBufferSize());
    EXPECT_EQ(MemoryConstants::pageSize64k, commandStream.getMaxAvailableSpace());

    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, MemoryConstants::pageSize64k + 1u, 0u);
    EXPECT_EQ(2 * MemoryConstants::pageSize64k, commandStream.getGraphicsAllocation()->getUnderlyingBufferSize());
    EXPECT_EQ(2 * MemoryConstants::pageSize64k, commandStream.getMaxAvailableSpace());

    memoryManager->freeGraphicsMemory(commandStream.getGraphicsAllocation());
}

TEST_F(CommandStreamReceiverTest, givenAdditionalAllocationSizeWhenCallingEnsureCommandBufferAllocationThenSizesOfAllocationAndCommandBufferAreCorrect) {
    GraphicsAllocation *allocation = memoryManager->allocateGraphicsMemoryWithProperties({commandStreamReceiver->getRootDeviceIndex(), 128u, GraphicsAllocation::AllocationType::COMMAND_BUFFER});
    LinearStream commandStream{allocation};

    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, 129u, 350u);
    EXPECT_NE(allocation, commandStream.getGraphicsAllocation());
    EXPECT_EQ(MemoryConstants::pageSize64k, commandStream.getGraphicsAllocation()->getUnderlyingBufferSize());
    EXPECT_EQ(MemoryConstants::pageSize64k - 350u, commandStream.getMaxAvailableSpace());

    memoryManager->freeGraphicsMemory(commandStream.getGraphicsAllocation());
}

TEST_F(CommandStreamReceiverTest, givenMinimumSizeExceedsCurrentAndNoAllocationsForReuseWhenCallingEnsureCommandBufferAllocationThenAllocateMemoryFromMemoryManager) {
    LinearStream commandStream;

    EXPECT_TRUE(internalAllocationStorage->getAllocationsForReuse().peekIsEmpty());
    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, 1u, 0u);
    EXPECT_NE(nullptr, commandStream.getGraphicsAllocation());

    memoryManager->freeGraphicsMemory(commandStream.getGraphicsAllocation());
}

TEST_F(CommandStreamReceiverTest, givenMinimumSizeExceedsCurrentAndAllocationsForReuseWhenCallingEnsureCommandBufferAllocationThenObtainAllocationFromInternalAllocationStorage) {
    auto allocation = memoryManager->allocateGraphicsMemoryWithProperties({commandStreamReceiver->getRootDeviceIndex(), MemoryConstants::pageSize64k, GraphicsAllocation::AllocationType::COMMAND_BUFFER});
    internalAllocationStorage->storeAllocation(std::unique_ptr<GraphicsAllocation>{allocation}, REUSABLE_ALLOCATION);
    LinearStream commandStream;

    EXPECT_FALSE(internalAllocationStorage->getAllocationsForReuse().peekIsEmpty());
    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, 1u, 0u);
    EXPECT_EQ(allocation, commandStream.getGraphicsAllocation());
    EXPECT_TRUE(internalAllocationStorage->getAllocationsForReuse().peekIsEmpty());

    memoryManager->freeGraphicsMemory(commandStream.getGraphicsAllocation());
}

TEST_F(CommandStreamReceiverTest, givenMinimumSizeExceedsCurrentAndNoSuitableReusableAllocationWhenCallingEnsureCommandBufferAllocationThenObtainAllocationMemoryManager) {
    auto allocation = memoryManager->allocateGraphicsMemoryWithProperties({commandStreamReceiver->getRootDeviceIndex(), MemoryConstants::pageSize64k, GraphicsAllocation::AllocationType::COMMAND_BUFFER});
    internalAllocationStorage->storeAllocation(std::unique_ptr<GraphicsAllocation>{allocation}, REUSABLE_ALLOCATION);
    LinearStream commandStream;

    EXPECT_FALSE(internalAllocationStorage->getAllocationsForReuse().peekIsEmpty());
    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, MemoryConstants::pageSize64k + 1, 0u);
    EXPECT_NE(allocation, commandStream.getGraphicsAllocation());
    EXPECT_NE(nullptr, commandStream.getGraphicsAllocation());
    EXPECT_FALSE(internalAllocationStorage->getAllocationsForReuse().peekIsEmpty());

    memoryManager->freeGraphicsMemory(commandStream.getGraphicsAllocation());
}

class CommandStreamReceiverWithAubSubCaptureTest : public CommandStreamReceiverTest,
                                                   public ::testing::WithParamInterface<std::pair<bool, bool>> {};

HWTEST_P(CommandStreamReceiverWithAubSubCaptureTest, givenCommandStreamReceiverWhenProgramForAubSubCaptureIsCalledThenProgramCsrDependsOnAubSubCaptureStatus) {
    class MyMockCsr : public MockCommandStreamReceiver {
      public:
        using MockCommandStreamReceiver::MockCommandStreamReceiver;

        void initProgrammingFlags() override {
            initProgrammingFlagsCalled = true;
        }
        bool flushBatchedSubmissions() override {
            flushBatchedSubmissionsCalled = true;
            return true;
        }
        bool initProgrammingFlagsCalled = false;
        bool flushBatchedSubmissionsCalled = false;
    };

    auto status = GetParam();
    bool wasActiveInPreviousEnqueue = status.first;
    bool isActive = status.second;

    ExecutionEnvironment executionEnvironment;
    executionEnvironment.prepareRootDeviceEnvironments(1);
    executionEnvironment.initializeMemoryManager();
    MyMockCsr mockCsr(executionEnvironment, 0);

    mockCsr.programForAubSubCapture(wasActiveInPreviousEnqueue, isActive);

    EXPECT_EQ(!wasActiveInPreviousEnqueue && isActive, mockCsr.initProgrammingFlagsCalled);
    EXPECT_EQ(wasActiveInPreviousEnqueue && !isActive, mockCsr.flushBatchedSubmissionsCalled);
}

std::pair<bool, bool> aubSubCaptureStatus[] = {
    {false, false},
    {false, true},
    {true, false},
    {true, true},
};

INSTANTIATE_TEST_CASE_P(
    CommandStreamReceiverWithAubSubCaptureTest_program,
    CommandStreamReceiverWithAubSubCaptureTest,
    testing::ValuesIn(aubSubCaptureStatus));

TEST(CommandStreamReceiverDeviceIndexTest, givenCsrWithOsContextWhenGetDeviceIndexThenGetHighestEnabledBitInDeviceBitfield) {
    ExecutionEnvironment executionEnvironment;
    executionEnvironment.prepareRootDeviceEnvironments(1);
    executionEnvironment.initializeMemoryManager();
    MockCommandStreamReceiver csr(executionEnvironment, 0);
    auto osContext = executionEnvironment.memoryManager->createAndRegisterOsContext(&csr, aub_stream::EngineType::ENGINE_RCS, 0b10, PreemptionMode::Disabled, false);

    csr.setupContext(*osContext);
    EXPECT_EQ(1u, csr.getDeviceIndex());
}

TEST(CommandStreamReceiverDeviceIndexTest, givenOsContextWithNoDeviceBitfieldWhenGettingDeviceIndexThenZeroIsReturned) {
    ExecutionEnvironment executionEnvironment;
    executionEnvironment.prepareRootDeviceEnvironments(1);
    executionEnvironment.initializeMemoryManager();
    MockCommandStreamReceiver csr(executionEnvironment, 0);
    auto osContext = executionEnvironment.memoryManager->createAndRegisterOsContext(&csr, aub_stream::EngineType::ENGINE_RCS, 0b00, PreemptionMode::Disabled, false);

    csr.setupContext(*osContext);
    EXPECT_EQ(0u, csr.getDeviceIndex());
}

TEST(CommandStreamReceiverRootDeviceIndexTest, commandStreamGraphicsAllocationsHaveCorrectRootDeviceIndex) {
    const uint32_t expectedRootDeviceIndex = 101;

    // Setup
    auto executionEnvironment = platformImpl->peekExecutionEnvironment();
    executionEnvironment->prepareRootDeviceEnvironments(2 * expectedRootDeviceIndex);
    auto memoryManager = new MockMemoryManager(false, false, *executionEnvironment);
    executionEnvironment->memoryManager.reset(memoryManager);
    std::unique_ptr<MockDevice> device(Device::create<MockDevice>(executionEnvironment, expectedRootDeviceIndex));
    auto commandStreamReceiver = &device->getGpgpuCommandStreamReceiver();

    ASSERT_NE(nullptr, commandStreamReceiver);
    EXPECT_EQ(expectedRootDeviceIndex, commandStreamReceiver->getRootDeviceIndex());

    // Linear stream / Command buffer
    GraphicsAllocation *allocation = memoryManager->allocateGraphicsMemoryWithProperties({expectedRootDeviceIndex, 128u, GraphicsAllocation::AllocationType::COMMAND_BUFFER});
    LinearStream commandStream{allocation};

    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, 100u, 0u);
    EXPECT_EQ(allocation, commandStream.getGraphicsAllocation());
    EXPECT_EQ(128u, commandStream.getMaxAvailableSpace());
    EXPECT_EQ(expectedRootDeviceIndex, commandStream.getGraphicsAllocation()->getRootDeviceIndex());

    commandStreamReceiver->ensureCommandBufferAllocation(commandStream, 1024u, 0u);
    EXPECT_NE(allocation, commandStream.getGraphicsAllocation());
    EXPECT_EQ(0u, commandStream.getMaxAvailableSpace() % MemoryConstants::pageSize64k);
    EXPECT_EQ(expectedRootDeviceIndex, commandStream.getGraphicsAllocation()->getRootDeviceIndex());
    memoryManager->freeGraphicsMemory(commandStream.getGraphicsAllocation());

    // Debug surface
    auto debugSurface = commandStreamReceiver->allocateDebugSurface(MemoryConstants::pageSize);
    ASSERT_NE(nullptr, debugSurface);
    EXPECT_EQ(expectedRootDeviceIndex, debugSurface->getRootDeviceIndex());

    // Indirect heaps
    IndirectHeap::Type heapTypes[]{IndirectHeap::DYNAMIC_STATE, IndirectHeap::GENERAL_STATE, IndirectHeap::INDIRECT_OBJECT, IndirectHeap::SURFACE_STATE};
    for (auto heapType : heapTypes) {
        IndirectHeap *heap = nullptr;
        commandStreamReceiver->allocateHeapMemory(heapType, MemoryConstants::pageSize, heap);
        ASSERT_NE(nullptr, heap);
        ASSERT_NE(nullptr, heap->getGraphicsAllocation());
        EXPECT_EQ(expectedRootDeviceIndex, heap->getGraphicsAllocation()->getRootDeviceIndex());
        memoryManager->freeGraphicsMemory(heap->getGraphicsAllocation());
        delete heap;
    }

    // Tag allocation
    ASSERT_NE(nullptr, commandStreamReceiver->getTagAllocation());
    EXPECT_EQ(expectedRootDeviceIndex, commandStreamReceiver->getTagAllocation()->getRootDeviceIndex());

    // Preemption allocation
    if (nullptr == commandStreamReceiver->getPreemptionAllocation()) {
        commandStreamReceiver->createPreemptionAllocation();
    }
    EXPECT_EQ(expectedRootDeviceIndex, commandStreamReceiver->getPreemptionAllocation()->getRootDeviceIndex());

    // HostPtr surface
    char memory[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    HostPtrSurface surface(memory, sizeof(memory), true);
    EXPECT_TRUE(commandStreamReceiver->createAllocationForHostSurface(surface, false));
    ASSERT_NE(nullptr, surface.getAllocation());
    EXPECT_EQ(expectedRootDeviceIndex, surface.getAllocation()->getRootDeviceIndex());
}
