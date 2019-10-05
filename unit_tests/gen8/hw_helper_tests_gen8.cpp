/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/memory_manager/memory_constants.h"
#include "unit_tests/helpers/get_gpgpu_engines_tests.inl"
#include "unit_tests/helpers/hw_helper_tests.h"

using HwHelperTestGen8 = HwHelperTest;

GEN8TEST_F(HwHelperTestGen8, getMaxBarriersPerSliceReturnsCorrectSize) {
    auto &helper = HwHelper::get(renderCoreFamily);

    EXPECT_EQ(16u, helper.getMaxBarrierRegisterPerSlice());
}

GEN8TEST_F(HwHelperTestGen8, setCapabilityCoherencyFlag) {
    auto &helper = HwHelper::get(renderCoreFamily);

    bool coherency = false;
    helper.setCapabilityCoherencyFlag(&hardwareInfo, coherency);
    EXPECT_TRUE(coherency);
}

GEN8TEST_F(HwHelperTestGen8, getPitchAlignmentForImage) {
    auto &helper = HwHelper::get(renderCoreFamily);
    EXPECT_EQ(4u, helper.getPitchAlignmentForImage(&hardwareInfo));
}

GEN8TEST_F(HwHelperTestGen8, adjustDefaultEngineType) {
    auto engineType = hardwareInfo.capabilityTable.defaultEngineType;
    auto &helper = HwHelper::get(renderCoreFamily);
    helper.adjustDefaultEngineType(&hardwareInfo);
    EXPECT_EQ(engineType, hardwareInfo.capabilityTable.defaultEngineType);
}

GEN8TEST_F(HwHelperTestGen8, givenGen8PlatformWhenSetupHardwareCapabilitiesIsCalledThenSpecificImplementationIsUsed) {
    auto &helper = HwHelper::get(renderCoreFamily);
    HardwareCapabilities hwCaps = {0};
    helper.setupHardwareCapabilities(&hwCaps, hardwareInfo);

    EXPECT_EQ(2048u, hwCaps.image3DMaxHeight);
    EXPECT_EQ(2048u, hwCaps.image3DMaxWidth);
    EXPECT_EQ(2 * MemoryConstants::gigaByte - 8 * MemoryConstants::megaByte, hwCaps.maxMemAllocSize);
    EXPECT_FALSE(hwCaps.isStatelesToStatefullWithOffsetSupported);
}

GEN8TEST_F(HwHelperTestGen8, whenGetConfigureAddressSpaceModeThenReturnZero) {
    auto &helper = HwHelper::get(renderCoreFamily);
    EXPECT_EQ(0u, helper.getConfigureAddressSpaceMode());
}

GEN8TEST_F(HwHelperTestGen8, whenGetGpgpuEnginesThenReturnTwoRcsEngines) {
    whenGetGpgpuEnginesThenReturnTwoRcsEngines<FamilyType>();
    EXPECT_EQ(2u, pDevice->getExecutionEnvironment()->commandStreamReceivers[0].size());
}
