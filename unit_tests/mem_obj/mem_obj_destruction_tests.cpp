/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/helpers/memory_properties_flags_helpers.h"
#include "runtime/mem_obj/mem_obj.h"
#include "runtime/memory_manager/allocations_list.h"
#include "runtime/os_interface/os_context.h"
#include "runtime/platform/platform.h"
#include "test.h"
#include "unit_tests/libult/ult_command_stream_receiver.h"
#include "unit_tests/mocks/mock_context.h"
#include "unit_tests/mocks/mock_device.h"
#include "unit_tests/mocks/mock_memory_manager.h"

using namespace NEO;

template <typename Family>
class MyCsr : public UltCommandStreamReceiver<Family> {
  public:
    MyCsr(const ExecutionEnvironment &executionEnvironment) : UltCommandStreamReceiver<Family>(const_cast<ExecutionEnvironment &>(executionEnvironment), 0) {}
    MOCK_METHOD3(waitForCompletionWithTimeout, bool(bool enableTimeout, int64_t timeoutMs, uint32_t taskCountToWait));
};

void CL_CALLBACK emptyDestructorCallback(cl_mem memObj, void *userData) {
}

class MemObjDestructionTest : public ::testing::TestWithParam<bool> {
  public:
    void SetUp() override {
        executionEnvironment = platformImpl->peekExecutionEnvironment();
        memoryManager = new MockMemoryManager(*executionEnvironment);
        executionEnvironment->memoryManager.reset(memoryManager);
        device.reset(MockDevice::create<MockDevice>(executionEnvironment, 0));
        context.reset(new MockContext(device.get()));

        allocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{size});
        memObj = new MemObj(context.get(), CL_MEM_OBJECT_BUFFER,
                            MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(CL_MEM_READ_WRITE, 0), CL_MEM_READ_WRITE, 0,
                            size,
                            nullptr, nullptr, allocation, true, false, false);
        csr = device->getDefaultEngine().commandStreamReceiver;
        *csr->getTagAddress() = 0;
        contextId = device->getDefaultEngine().osContext->getContextId();
    }

    void TearDown() override {
        context.reset();
    }

    void makeMemObjUsed() {
        memObj->getGraphicsAllocation()->updateTaskCount(taskCountReady, contextId);
    }

    void makeMemObjNotReady() {
        makeMemObjUsed();
        *device->getDefaultEngine().commandStreamReceiver->getTagAddress() = taskCountReady - 1;
    }

    void makeMemObjReady() {
        makeMemObjUsed();
        *device->getDefaultEngine().commandStreamReceiver->getTagAddress() = taskCountReady;
    }

    constexpr static uint32_t taskCountReady = 3u;
    ExecutionEnvironment *executionEnvironment = nullptr;
    std::unique_ptr<MockDevice> device;
    uint32_t contextId = 0;
    MockMemoryManager *memoryManager = nullptr;
    std::unique_ptr<MockContext> context;
    GraphicsAllocation *allocation = nullptr;
    MemObj *memObj = nullptr;
    CommandStreamReceiver *csr = nullptr;
    size_t size = MemoryConstants::pageSize;
};

class MemObjAsyncDestructionTest : public MemObjDestructionTest {
  public:
    void SetUp() override {
        DebugManager.flags.EnableAsyncDestroyAllocations.set(true);
        MemObjDestructionTest::SetUp();
    }
    void TearDown() override {
        MemObjDestructionTest::TearDown();
        DebugManager.flags.EnableAsyncDestroyAllocations.set(defaultFlag);
    }
    bool defaultFlag = DebugManager.flags.EnableAsyncDestroyAllocations.get();
};

class MemObjSyncDestructionTest : public MemObjDestructionTest {
  public:
    void SetUp() override {
        DebugManager.flags.EnableAsyncDestroyAllocations.set(false);
        MemObjDestructionTest::SetUp();
    }
    void TearDown() override {
        MemObjDestructionTest::TearDown();
        DebugManager.flags.EnableAsyncDestroyAllocations.set(defaultFlag);
    }
    bool defaultFlag = DebugManager.flags.EnableAsyncDestroyAllocations.get();
};

TEST_P(MemObjAsyncDestructionTest, givenMemObjWithDestructableAllocationWhenAsyncDestructionsAreEnabledAndAllocationIsNotReadyAndMemObjectIsDestructedThenAllocationIsDeferred) {
    bool isMemObjReady;
    bool expectedDeferration;
    isMemObjReady = GetParam();
    expectedDeferration = !isMemObjReady;

    if (isMemObjReady) {
        makeMemObjReady();
    } else {
        makeMemObjNotReady();
    }
    auto &allocationList = csr->getTemporaryAllocations();
    EXPECT_TRUE(allocationList.peekIsEmpty());

    delete memObj;

    EXPECT_EQ(!expectedDeferration, allocationList.peekIsEmpty());
    if (expectedDeferration) {
        EXPECT_EQ(allocation, allocationList.peekHead());
    }
}

HWTEST_P(MemObjAsyncDestructionTest, givenUsedMemObjWithAsyncDestructionsEnabledThatHasDestructorCallbacksWhenItIsDestroyedThenDestructorWaitsOnTaskCount) {
    bool hasCallbacks = GetParam();

    if (hasCallbacks) {
        memObj->setDestructorCallback(emptyDestructorCallback, nullptr);
    }

    auto mockCsr0 = new ::testing::NiceMock<MyCsr<FamilyType>>(*device->executionEnvironment);
    auto mockCsr1 = new ::testing::NiceMock<MyCsr<FamilyType>>(*device->executionEnvironment);
    device->resetCommandStreamReceiver(mockCsr0, 0);
    device->resetCommandStreamReceiver(mockCsr1, 1);
    *mockCsr0->getTagAddress() = 0;
    *mockCsr1->getTagAddress() = 0;

    auto waitForCompletionWithTimeoutMock0 = [&mockCsr0](bool enableTimeout, int64_t timeoutMs, uint32_t taskCountToWait) -> bool {
        *mockCsr0->getTagAddress() = taskCountReady;
        return true;
    };
    auto waitForCompletionWithTimeoutMock1 = [&mockCsr1](bool enableTimeout, int64_t timeoutMs, uint32_t taskCountToWait) -> bool {
        *mockCsr1->getTagAddress() = taskCountReady;
        return true;
    };
    auto osContextId0 = mockCsr0->getOsContext().getContextId();
    auto osContextId1 = mockCsr1->getOsContext().getContextId();

    memObj->getGraphicsAllocation()->updateTaskCount(taskCountReady, osContextId0);
    memObj->getGraphicsAllocation()->updateTaskCount(taskCountReady, osContextId1);

    ON_CALL(*mockCsr0, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(waitForCompletionWithTimeoutMock0));
    ON_CALL(*mockCsr1, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(waitForCompletionWithTimeoutMock1));

    if (hasCallbacks) {
        EXPECT_CALL(*mockCsr0, waitForCompletionWithTimeout(::testing::_, TimeoutControls::maxTimeout, allocation->getTaskCount(osContextId0)))
            .Times(1);
        EXPECT_CALL(*mockCsr1, waitForCompletionWithTimeout(::testing::_, TimeoutControls::maxTimeout, allocation->getTaskCount(osContextId1)))
            .Times(1);
    } else {
        *mockCsr0->getTagAddress() = taskCountReady;
        *mockCsr1->getTagAddress() = taskCountReady;
        EXPECT_CALL(*mockCsr0, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
            .Times(0);
        EXPECT_CALL(*mockCsr1, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
            .Times(0);
    }
    delete memObj;
}

HWTEST_P(MemObjAsyncDestructionTest, givenUsedMemObjWithAsyncDestructionsEnabledThatHasAllocatedMappedPtrWhenItIsDestroyedThenDestructorWaitsOnTaskCount) {
    makeMemObjUsed();

    bool hasAllocatedMappedPtr = GetParam();

    if (hasAllocatedMappedPtr) {
        auto allocatedPtr = alignedMalloc(size, MemoryConstants::pageSize);
        memObj->setAllocatedMapPtr(allocatedPtr);
    }

    auto mockCsr = new ::testing::NiceMock<MyCsr<FamilyType>>(*device->executionEnvironment);
    device->resetCommandStreamReceiver(mockCsr);
    *mockCsr->getTagAddress() = 0;
    auto osContextId = mockCsr->getOsContext().getContextId();

    bool desired = true;

    auto waitForCompletionWithTimeoutMock = [=](bool enableTimeout, int64_t timeoutMs, uint32_t taskCountToWait) -> bool { return desired; };

    ON_CALL(*mockCsr, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(waitForCompletionWithTimeoutMock));

    if (hasAllocatedMappedPtr) {
        EXPECT_CALL(*mockCsr, waitForCompletionWithTimeout(::testing::_, TimeoutControls::maxTimeout, allocation->getTaskCount(osContextId)))
            .Times(1);
    } else {
        EXPECT_CALL(*mockCsr, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
            .Times(0);
    }
    delete memObj;
}

HWTEST_P(MemObjAsyncDestructionTest, givenUsedMemObjWithAsyncDestructionsEnabledThatHasDestructableMappedPtrWhenItIsDestroyedThenDestructorWaitsOnTaskCount) {
    auto storage = alignedMalloc(size, MemoryConstants::pageSize);

    bool hasAllocatedMappedPtr = GetParam();

    if (!hasAllocatedMappedPtr) {
        delete memObj;
        allocation = memoryManager->allocateGraphicsMemoryWithProperties(MockAllocationProperties{size});
        MemObjOffsetArray origin = {{0, 0, 0}};
        MemObjSizeArray region = {{1, 1, 1}};
        cl_map_flags mapFlags = CL_MAP_READ;
        memObj = new MemObj(context.get(), CL_MEM_OBJECT_BUFFER,
                            MemoryPropertiesFlagsParser::createMemoryPropertiesFlags(CL_MEM_READ_WRITE, 0), CL_MEM_READ_WRITE, 0,
                            size,
                            storage, nullptr, allocation, true, false, false);
        memObj->addMappedPtr(storage, 1, mapFlags, region, origin, 0);
    } else {
        memObj->setAllocatedMapPtr(storage);
    }

    makeMemObjUsed();
    auto mockCsr = new ::testing::NiceMock<MyCsr<FamilyType>>(*device->executionEnvironment);
    device->resetCommandStreamReceiver(mockCsr);
    *mockCsr->getTagAddress() = 0;

    bool desired = true;

    auto waitForCompletionWithTimeoutMock = [=](bool enableTimeout, int64_t timeoutMs, uint32_t taskCountToWait) -> bool { return desired; };
    auto osContextId = mockCsr->getOsContext().getContextId();

    ON_CALL(*mockCsr, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(waitForCompletionWithTimeoutMock));

    if (hasAllocatedMappedPtr) {
        EXPECT_CALL(*mockCsr, waitForCompletionWithTimeout(::testing::_, TimeoutControls::maxTimeout, allocation->getTaskCount(osContextId)))
            .Times(1);
    } else {
        EXPECT_CALL(*mockCsr, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
            .Times(0);
    }
    delete memObj;

    if (!hasAllocatedMappedPtr) {
        alignedFree(storage);
    }
}

HWTEST_P(MemObjSyncDestructionTest, givenMemObjWithDestructableAllocationWhenAsyncDestructionsAreDisabledThenDestructorWaitsOnTaskCount) {
    bool isMemObjReady;
    isMemObjReady = GetParam();

    if (isMemObjReady) {
        makeMemObjReady();
    } else {
        makeMemObjNotReady();
    }
    auto mockCsr = new ::testing::NiceMock<MyCsr<FamilyType>>(*device->executionEnvironment);
    device->resetCommandStreamReceiver(mockCsr);
    *mockCsr->getTagAddress() = 0;

    bool desired = true;

    auto waitForCompletionWithTimeoutMock = [=](bool enableTimeout, int64_t timeoutMs, uint32_t taskCountToWait) -> bool { return desired; };
    auto osContextId = mockCsr->getOsContext().getContextId();

    ON_CALL(*mockCsr, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(waitForCompletionWithTimeoutMock));

    EXPECT_CALL(*mockCsr, waitForCompletionWithTimeout(::testing::_, TimeoutControls::maxTimeout, allocation->getTaskCount(osContextId)))
        .Times(1);

    delete memObj;
}

HWTEST_P(MemObjSyncDestructionTest, givenMemObjWithDestructableAllocationWhenAsyncDestructionsAreDisabledThenAllocationIsNotDeferred) {
    bool isMemObjReady;
    isMemObjReady = GetParam();

    if (isMemObjReady) {
        makeMemObjReady();
    } else {
        makeMemObjNotReady();
    }
    auto mockCsr = new ::testing::NiceMock<MyCsr<FamilyType>>(*device->executionEnvironment);
    device->resetCommandStreamReceiver(mockCsr);
    *mockCsr->getTagAddress() = 0;

    bool desired = true;

    auto waitForCompletionWithTimeoutMock = [=](bool enableTimeout, int64_t timeoutMs, uint32_t taskCountToWait) -> bool { return desired; };

    ON_CALL(*mockCsr, waitForCompletionWithTimeout(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(::testing::Invoke(waitForCompletionWithTimeoutMock));

    delete memObj;
    auto &allocationList = mockCsr->getTemporaryAllocations();
    EXPECT_TRUE(allocationList.peekIsEmpty());
}

INSTANTIATE_TEST_CASE_P(
    MemObjTests,
    MemObjAsyncDestructionTest,
    testing::Bool());

INSTANTIATE_TEST_CASE_P(
    MemObjTests,
    MemObjSyncDestructionTest,
    testing::Bool());
