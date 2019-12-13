/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "runtime/os_interface/windows/os_interface.h"
#include "runtime/os_interface/windows/wddm_memory_operations_handler.h"
#include "test.h"
#include "unit_tests/helpers/execution_environment_helper.h"
#include "unit_tests/mocks/mock_context.h"
#include "unit_tests/mocks/mock_gmm.h"
#include "unit_tests/mocks/mock_gmm_page_table_mngr.h"
#include "unit_tests/mocks/mock_wddm_residency_allocations_container.h"
#include "unit_tests/os_interface/windows/mock_gdi_interface.h"
#include "unit_tests/os_interface/windows/mock_wddm_memory_manager.h"
#include "unit_tests/os_interface/windows/wddm_fixture.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <type_traits>

using namespace NEO;
using namespace ::testing;

class WddmMemoryManagerFixture : public GdiDllFixture {
  public:
    void SetUp() override;

    void TearDown() override {
        GdiDllFixture::TearDown();
    }

    ExecutionEnvironment *executionEnvironment;
    std::unique_ptr<MockWddmMemoryManager> memoryManager;
    WddmMock *wddm;
};

typedef ::Test<WddmMemoryManagerFixture> WddmMemoryManagerTest;

class MockWddmMemoryManagerFixture {
  public:
    void SetUp() {
        executionEnvironment = platformImpl->peekExecutionEnvironment();
        gdi = new MockGdi();

        wddm = static_cast<WddmMock *>(Wddm::createWddm());
        wddm->gdi.reset(gdi);
        constexpr uint64_t heap32Base = (is32bit) ? 0x1000 : 0x800000000000;
        wddm->setHeap32(heap32Base, 1000 * MemoryConstants::pageSize - 1);
        auto hwInfo = *platformDevices[0];
        wddm->init(hwInfo);

        executionEnvironment->osInterface.reset(new OSInterface());
        executionEnvironment->osInterface->get()->setWddm(wddm);
        executionEnvironment->memoryOperationsInterface = std::make_unique<WddmMemoryOperationsHandler>(wddm);

        memoryManager = std::make_unique<MockWddmMemoryManager>(*executionEnvironment);
        osContext = memoryManager->createAndRegisterOsContext(nullptr, HwHelper::get(platformDevices[0]->platform.eRenderCoreFamily).getGpgpuEngineInstances()[0],
                                                              1, PreemptionHelper::getDefaultPreemptionMode(*platformDevices[0]), false);

        osContext->incRefInternal();
        mockTemporaryResources = reinterpret_cast<MockWddmResidentAllocationsContainer *>(wddm->getTemporaryResourcesContainer());
    }

    void TearDown() {
        osContext->decRefInternal();
    }

    ExecutionEnvironment *executionEnvironment;
    std::unique_ptr<MockWddmMemoryManager> memoryManager;
    WddmMock *wddm = nullptr;
    MockWddmResidentAllocationsContainer *mockTemporaryResources;
    OsContext *osContext = nullptr;
    MockGdi *gdi = nullptr;
};

typedef ::Test<MockWddmMemoryManagerFixture> WddmMemoryManagerResidencyTest;

class ExecutionEnvironmentFixture : public ::testing::Test {
  public:
    ExecutionEnvironmentFixture() {
        executionEnvironment = platformImpl->peekExecutionEnvironment();
    }

    ExecutionEnvironment *executionEnvironment;
};

class WddmMemoryManagerFixtureWithGmockWddm : public ExecutionEnvironmentFixture {
  public:
    MockWddmMemoryManager *memoryManager = nullptr;

    void SetUp() override {
        // wddm is deleted by memory manager
        wddm = new NiceMock<GmockWddm>;
        executionEnvironment->osInterface = std::make_unique<OSInterface>();
        ASSERT_NE(nullptr, wddm);
        auto preemptionMode = PreemptionHelper::getDefaultPreemptionMode(*platformDevices[0]);
        auto hwInfo = *platformDevices[0];
        wddm->init(hwInfo);
        executionEnvironment->osInterface->get()->setWddm(wddm);
        executionEnvironment->memoryOperationsInterface = std::make_unique<WddmMemoryOperationsHandler>(wddm);
        osInterface = executionEnvironment->osInterface.get();
        memoryManager = new (std::nothrow) MockWddmMemoryManager(*executionEnvironment);
        //assert we have memory manager
        ASSERT_NE(nullptr, memoryManager);
        osContext = memoryManager->createAndRegisterOsContext(nullptr, HwHelper::get(platformDevices[0]->platform.eRenderCoreFamily).getGpgpuEngineInstances()[0], 1, preemptionMode, false);

        osContext->incRefInternal();

        ON_CALL(*wddm, createAllocationsAndMapGpuVa(::testing::_)).WillByDefault(::testing::Invoke(wddm, &GmockWddm::baseCreateAllocationAndMapGpuVa));
    }

    void TearDown() override {
        osContext->decRefInternal();
        delete memoryManager;
    }

    NiceMock<GmockWddm> *wddm = nullptr;
    OSInterface *osInterface;
    OsContext *osContext;
};

using WddmMemoryManagerTest2 = WddmMemoryManagerFixtureWithGmockWddm;

class BufferWithWddmMemory : public ::testing::Test,
                             public WddmMemoryManagerFixture {
  public:
  protected:
    void SetUp() {
        WddmMemoryManagerFixture::SetUp();
        tmp = context.getMemoryManager();
        context.memoryManager = memoryManager.get();
        flags = 0;
    }

    void TearDown() {
        context.memoryManager = tmp;
        WddmMemoryManagerFixture::TearDown();
    }

    MemoryManager *tmp;
    MockContext context;
    cl_mem_flags flags;
    cl_int retVal;
};

class WddmMemoryManagerSimpleTest : public MockWddmMemoryManagerFixture, public ::testing::Test {
  public:
    void SetUp() override {
        MockWddmMemoryManagerFixture::SetUp();
    }
    void TearDown() override {
        MockWddmMemoryManagerFixture::TearDown();
    }
};

class MockWddmMemoryManagerTest : public ::testing::Test {
  public:
    void SetUp() override {
        executionEnvironment = getExecutionEnvironmentImpl(hwInfo);
        wddm = new WddmMock();
        executionEnvironment->osInterface->get()->setWddm(wddm);
        executionEnvironment->memoryOperationsInterface = std::make_unique<WddmMemoryOperationsHandler>(wddm);
    }

    HardwareInfo *hwInfo;
    WddmMock *wddm;
    ExecutionEnvironment *executionEnvironment;
};

using OsAgnosticMemoryManagerUsingWddmTest = MockWddmMemoryManagerTest;
