/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/unit_tests/helpers/debug_manager_state_restore.h"
#include "runtime/device/device.h"
#include "runtime/helpers/device_helpers.h"
#include "runtime/helpers/hw_helper.h"
#include "runtime/helpers/options.h"
#include "runtime/indirect_heap/indirect_heap.h"
#include "runtime/os_interface/os_context.h"
#include "runtime/platform/platform.h"
#include "test.h"
#include "unit_tests/fixtures/device_fixture.h"
#include "unit_tests/helpers/variable_backup.h"
#include "unit_tests/libult/create_command_stream.h"
#include "unit_tests/libult/ult_command_stream_receiver.h"
#include "unit_tests/mocks/mock_context.h"
#include "unit_tests/mocks/mock_csr.h"

#include <memory>

using namespace NEO;

typedef Test<DeviceFixture> DeviceTest;

TEST_F(DeviceTest, Create) {
    EXPECT_NE(nullptr, pDevice);
}

TEST_F(DeviceTest, givenDeviceWhenGetProductAbbrevThenReturnsHardwarePrefix) {
    const auto productAbbrev = pDevice->getProductAbbrev();
    const auto hwPrefix = hardwarePrefix[pDevice->getHardwareInfo().platform.eProductFamily];
    EXPECT_EQ(hwPrefix, productAbbrev);
}

TEST_F(DeviceTest, getCommandStreamReceiver) {
    EXPECT_NE(nullptr, &pDevice->getGpgpuCommandStreamReceiver());
}

TEST_F(DeviceTest, getSupportedClVersion) {
    auto version = pDevice->getSupportedClVersion();
    auto version2 = pDevice->getHardwareInfo().capabilityTable.clVersionSupport;

    EXPECT_EQ(version, version2);
}

TEST_F(DeviceTest, givenDeviceWhenEngineIsCreatedThenSetInitialValueForTag) {
    for (auto &engine : pDevice->engines) {
        auto tagAddress = engine.commandStreamReceiver->getTagAddress();
        ASSERT_NE(nullptr, const_cast<uint32_t *>(tagAddress));
        EXPECT_EQ(initialHardwareTag, *tagAddress);
    }
}

TEST_F(DeviceTest, givenDeviceWhenAskedForSpecificEngineThenRetrunIt) {
    auto &engines = HwHelper::get(platformDevices[0]->platform.eRenderCoreFamily).getGpgpuEngineInstances();
    for (uint32_t i = 0; i < engines.size(); i++) {
        bool lowPriority = (HwHelper::lowPriorityGpgpuEngineIndex == i);
        auto &deviceEngine = pDevice->getEngine(engines[i], lowPriority);
        EXPECT_EQ(deviceEngine.osContext->getEngineType(), engines[i]);
        EXPECT_EQ(deviceEngine.osContext->isLowPriority(), lowPriority);
    }

    EXPECT_THROW(pDevice->getEngine(aub_stream::ENGINE_VCS, false), std::exception);
}

TEST_F(DeviceTest, givenDebugVariableToAlwaysChooseEngineZeroWhenNotExistingEngineSelectedThenIndexZeroEngineIsReturned) {
    DebugManagerStateRestore restore;
    DebugManager.flags.OverrideInvalidEngineWithDefault.set(true);
    auto &engines = HwHelper::get(platformDevices[0]->platform.eRenderCoreFamily).getGpgpuEngineInstances();
    auto &deviceEngine = pDevice->getEngine(engines[0], false);
    auto &notExistingEngine = pDevice->getEngine(aub_stream::ENGINE_VCS, false);
    EXPECT_EQ(&notExistingEngine, &deviceEngine);
}

TEST_F(DeviceTest, WhenGetOSTimeThenNotNull) {
    auto pDevice = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));

    OSTime *osTime = pDevice->getOSTime();
    ASSERT_NE(nullptr, osTime);
}

TEST_F(DeviceTest, GivenDebugVariableForcing32BitAllocationsWhenDeviceIsCreatedThenMemoryManagerHasForce32BitFlagSet) {
    DebugManager.flags.Force32bitAddressing.set(true);
    auto pDevice = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    if (is64bit) {
        EXPECT_TRUE(pDevice->getDeviceInfo().force32BitAddressess);
        EXPECT_TRUE(pDevice->getMemoryManager()->peekForce32BitAllocations());
    } else {
        EXPECT_FALSE(pDevice->getDeviceInfo().force32BitAddressess);
        EXPECT_FALSE(pDevice->getMemoryManager()->peekForce32BitAllocations());
    }
    DebugManager.flags.Force32bitAddressing.set(false);
}

TEST_F(DeviceTest, retainAndRelease) {
    ASSERT_NE(nullptr, pDevice);

    pDevice->retain();
    pDevice->retain();
    pDevice->retain();
    ASSERT_EQ(1, pDevice->getReference());

    ASSERT_FALSE(pDevice->release().isUnused());
    ASSERT_EQ(1, pDevice->getReference());
}

TEST_F(DeviceTest, getEngineTypeDefault) {
    auto pTestDevice = std::unique_ptr<Device>(createWithUsDeviceId(0));

    auto actualEngineType = pDevice->getDefaultEngine().osContext->getEngineType();
    auto defaultEngineType = pDevice->getHardwareInfo().capabilityTable.defaultEngineType;

    EXPECT_EQ(&pDevice->getDefaultEngine().commandStreamReceiver->getOsContext(), pDevice->getDefaultEngine().osContext);
    EXPECT_EQ(defaultEngineType, actualEngineType);
}

TEST(DeviceCleanup, givenDeviceWhenItIsDestroyedThenFlushBatchedSubmissionsIsCalled) {
    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    MockCommandStreamReceiver *csr = new MockCommandStreamReceiver(*mockDevice->getExecutionEnvironment());
    mockDevice->resetCommandStreamReceiver(csr);
    int flushedBatchedSubmissionsCalledCount = 0;
    csr->flushBatchedSubmissionsCallCounter = &flushedBatchedSubmissionsCalledCount;
    mockDevice.reset(nullptr);

    EXPECT_EQ(1, flushedBatchedSubmissionsCalledCount);
}

TEST(DeviceCreation, givenSelectedAubCsrInDebugVarsWhenDeviceIsCreatedThenIsSimulationReturnsTrue) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.SetCommandStreamReceiver.set(CommandStreamReceiverType::CSR_AUB);

    VariableBackup<bool> backup(&overrideCommandStreamReceiverCreation, true);
    auto mockDevice = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<Device>(nullptr));
    EXPECT_TRUE(mockDevice->isSimulation());
}

TEST(DeviceCreation, givenSelectedTbxCsrInDebugVarsWhenDeviceIsCreatedThenIsSimulationReturnsTrue) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.SetCommandStreamReceiver.set(CommandStreamReceiverType::CSR_TBX);

    VariableBackup<bool> backup(&overrideCommandStreamReceiverCreation, true);
    auto device = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<Device>(nullptr));
    EXPECT_TRUE(device->isSimulation());
}

TEST(DeviceCreation, givenSelectedTbxWithAubCsrInDebugVarsWhenDeviceIsCreatedThenIsSimulationReturnsTrue) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.SetCommandStreamReceiver.set(CommandStreamReceiverType::CSR_TBX_WITH_AUB);

    VariableBackup<bool> backup(&overrideCommandStreamReceiverCreation, true);
    auto device = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<Device>(nullptr));
    EXPECT_TRUE(device->isSimulation());
}

TEST(DeviceCreation, givenHwWithAubCsrInDebugVarsWhenDeviceIsCreatedThenIsSimulationReturnsFalse) {
    DebugManagerStateRestore dbgRestorer;
    DebugManager.flags.SetCommandStreamReceiver.set(CommandStreamReceiverType::CSR_HW_WITH_AUB);

    auto device = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<Device>(nullptr));
    EXPECT_FALSE(device->isSimulation());
}

TEST(DeviceCreation, givenDefaultHwCsrInDebugVarsWhenDeviceIsCreatedThenIsSimulationReturnsFalse) {
    auto device = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<Device>(nullptr));
    EXPECT_FALSE(device->isSimulation());
}

TEST(DeviceCreation, givenDeviceWhenItIsCreatedThenOsContextIsRegistredInMemoryManager) {
    auto device = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<Device>(nullptr));
    auto memoryManager = device->getMemoryManager();
    EXPECT_EQ(HwHelper::get(platformDevices[0]->platform.eRenderCoreFamily).getGpgpuEngineInstances().size(), memoryManager->getRegisteredEnginesCount());
}

TEST(DeviceCreation, givenMultiDeviceWhenTheyAreCreatedThenEachOsContextHasUniqueId) {
    ExecutionEnvironment *executionEnvironment = platformImpl->peekExecutionEnvironment();
    const size_t numDevices = 2;
    const auto &numGpgpuEngines = static_cast<uint32_t>(HwHelper::get(platformDevices[0]->platform.eRenderCoreFamily).getGpgpuEngineInstances().size());

    auto device1 = std::unique_ptr<MockDevice>(Device::create<MockDevice>(executionEnvironment, 0u));
    auto device2 = std::unique_ptr<MockDevice>(Device::create<MockDevice>(executionEnvironment, 1u));

    auto &registeredEngines = executionEnvironment->memoryManager->getRegisteredEngines();
    EXPECT_EQ(numGpgpuEngines * numDevices, registeredEngines.size());

    for (uint32_t i = 0; i < numGpgpuEngines; i++) {
        EXPECT_EQ(i, device1->engines[i].osContext->getContextId());
        EXPECT_EQ(1u, device1->engines[i].osContext->getDeviceBitfield().to_ulong());
        EXPECT_EQ(i + numGpgpuEngines, device2->engines[i].osContext->getContextId());
        EXPECT_EQ(2u, device2->engines[i].osContext->getDeviceBitfield().to_ulong());

        EXPECT_EQ(registeredEngines[i].commandStreamReceiver,
                  device1->engines[i].commandStreamReceiver);
        EXPECT_EQ(registeredEngines[i + numGpgpuEngines].commandStreamReceiver,
                  device2->engines[i].commandStreamReceiver);
    }
    EXPECT_EQ(numGpgpuEngines * numDevices, executionEnvironment->memoryManager->getRegisteredEnginesCount());
}

TEST(DeviceCreation, givenMultiDeviceWhenTheyAreCreatedThenEachDeviceHasSeperateDeviceIndex) {
    ExecutionEnvironment *executionEnvironment = platformImpl->peekExecutionEnvironment();
    auto device = std::unique_ptr<Device>(Device::create<MockDevice>(executionEnvironment, 0u));
    auto device2 = std::unique_ptr<Device>(Device::create<MockDevice>(executionEnvironment, 1u));

    EXPECT_EQ(0u, device->getDeviceIndex());
    EXPECT_EQ(1u, device2->getDeviceIndex());
}

TEST(DeviceCreation, givenMultiDeviceWhenTheyAreCreatedThenEachDeviceHasSeperateCommandStreamReceiver) {
    ExecutionEnvironment *executionEnvironment = platformImpl->peekExecutionEnvironment();
    const size_t numDevices = 2;
    const auto &numGpgpuEngines = HwHelper::get(platformDevices[0]->platform.eRenderCoreFamily).getGpgpuEngineInstances().size();
    auto device1 = std::unique_ptr<MockDevice>(Device::create<MockDevice>(executionEnvironment, 0u));
    auto device2 = std::unique_ptr<MockDevice>(Device::create<MockDevice>(executionEnvironment, 1u));

    EXPECT_EQ(numDevices, executionEnvironment->commandStreamReceivers.size());
    EXPECT_EQ(numGpgpuEngines, executionEnvironment->commandStreamReceivers[0].size());
    EXPECT_EQ(numGpgpuEngines, executionEnvironment->commandStreamReceivers[1].size());

    for (uint32_t i = 0; i < static_cast<uint32_t>(numGpgpuEngines); i++) {
        EXPECT_NE(nullptr, executionEnvironment->commandStreamReceivers[0][i]);
        EXPECT_NE(nullptr, executionEnvironment->commandStreamReceivers[1][i]);
        EXPECT_EQ(executionEnvironment->commandStreamReceivers[0][i].get(), device1->engines[i].commandStreamReceiver);
        EXPECT_EQ(executionEnvironment->commandStreamReceivers[1][i].get(), device2->engines[i].commandStreamReceiver);
    }
}

TEST(DeviceCreation, givenDeviceWhenAskingForDefaultEngineThenReturnValidValue) {
    ExecutionEnvironment *executionEnvironment = platformImpl->peekExecutionEnvironment();
    auto device = std::unique_ptr<MockDevice>(Device::create<MockDevice>(executionEnvironment, 0));
    auto osContext = device->getDefaultEngine().osContext;

    EXPECT_EQ(platformDevices[0]->capabilityTable.defaultEngineType, osContext->getEngineType());
    EXPECT_FALSE(osContext->isLowPriority());
}

TEST(DeviceCreation, givenFtrSimulationModeFlagTrueWhenNoOtherSimulationFlagsArePresentThenIsSimulationReturnsTrue) {
    HardwareInfo hwInfo = *platformDevices[0];
    hwInfo.featureTable.ftrSimulationMode = true;

    bool simulationFromDeviceId = hwInfo.capabilityTable.isSimulation(hwInfo.platform.usDeviceID);
    EXPECT_FALSE(simulationFromDeviceId);

    auto device = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<Device>(&hwInfo));
    EXPECT_TRUE(device->isSimulation());
}

TEST(DeviceCreation, givenDeviceWhenCheckingEnginesCountThenNumberGreaterThanZeroIsReturned) {
    auto device = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<Device>(nullptr));
    EXPECT_GT(DeviceHelper::getEnginesCount(device->getHardwareInfo()), 0u);
}
