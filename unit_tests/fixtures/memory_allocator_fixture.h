/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "runtime/command_stream/preemption.h"
#include "runtime/execution_environment/execution_environment.h"
#include "runtime/helpers/hw_helper.h"
#include "runtime/memory_manager/os_agnostic_memory_manager.h"
#include "unit_tests/fixtures/memory_management_fixture.h"
#include "unit_tests/libult/create_command_stream.h"
#include "unit_tests/mocks/mock_memory_manager.h"

using namespace NEO;

class MemoryAllocatorFixture : public MemoryManagementFixture {
  public:
    void SetUp() override {
        MemoryManagementFixture::SetUp();
        executionEnvironment = std::make_unique<ExecutionEnvironment>();
        executionEnvironment->setHwInfo(*platformDevices);
        executionEnvironment->initializeCommandStreamReceiver(0, 0);
        memoryManager = new MockMemoryManager(false, false, *executionEnvironment);
        executionEnvironment->memoryManager.reset(memoryManager);
        csr = memoryManager->getDefaultCommandStreamReceiver(0);
        auto engineType = HwHelper::get(platformDevices[0]->platform.eRenderCoreFamily).getGpgpuEngineInstances()[0];
        auto osContext = memoryManager->createAndRegisterOsContext(csr, engineType, 1, PreemptionHelper::getDefaultPreemptionMode(*platformDevices[0]), false);
        csr->setupContext(*osContext);
    }

    void TearDown() override {
        executionEnvironment.reset();
        MemoryManagementFixture::TearDown();
    }

  protected:
    std::unique_ptr<ExecutionEnvironment> executionEnvironment;
    MockMemoryManager *memoryManager;
    CommandStreamReceiver *csr;
};
