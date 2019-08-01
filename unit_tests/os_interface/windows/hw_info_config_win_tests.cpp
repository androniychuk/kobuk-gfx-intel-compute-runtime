/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "unit_tests/os_interface/windows/hw_info_config_win_tests.h"

#include "core/unit_tests/helpers/debug_manager_state_restore.h"
#include "runtime/os_interface/windows/os_interface.h"
#include "runtime/os_interface/windows/wddm/wddm.h"

#include "instrumentation.h"

namespace NEO {

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getHostMemCapabilities() {
    return 0;
}

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getDeviceMemCapabilities() {
    return 0;
}

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getSingleDeviceSharedMemCapabilities() {
    return 0;
}

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getCrossDeviceSharedMemCapabilities() {
    return 0;
}

template <>
cl_unified_shared_memory_capabilities_intel HwInfoConfigHw<IGFX_UNKNOWN>::getSharedSystemMemCapabilities() {
    return 0;
}

template <>
int HwInfoConfigHw<IGFX_UNKNOWN>::configureHardwareCustom(HardwareInfo *hwInfo, OSInterface *osIface) {
    return 0;
}

template <>
void HwInfoConfigHw<IGFX_UNKNOWN>::adjustPlatformForProductFamily(HardwareInfo *hwInfo) {
}

void HwInfoConfigTestWindows::SetUp() {
    HwInfoConfigTest::SetUp();

    osInterface.reset(new OSInterface());

    std::unique_ptr<Wddm> wddm(Wddm::createWddm());
    wddm->init(outHwInfo);
}

void HwInfoConfigTestWindows::TearDown() {
    HwInfoConfigTest::TearDown();
}

TEST_F(HwInfoConfigTestWindows, givenCorrectParametersWhenConfiguringHwInfoThenReturnSuccess) {
    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface.get());
    EXPECT_EQ(0, ret);
}

TEST_F(HwInfoConfigTestWindows, givenCorrectParametersWhenConfiguringHwInfoThenSetFtrSvmCorrectly) {
    auto ftrSvm = outHwInfo.featureTable.ftrSVM;

    int ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface.get());
    ASSERT_EQ(0, ret);

    EXPECT_EQ(outHwInfo.capabilityTable.ftrSvm, ftrSvm);
}

TEST_F(HwInfoConfigTestWindows, givenInstrumentationForHardwareIsEnabledOrDisabledWhenConfiguringHwInfoThenOverrideItUsingHaveInstrumentation) {
    int ret;

    outHwInfo.capabilityTable.instrumentationEnabled = false;
    ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface.get());
    ASSERT_EQ(0, ret);
    EXPECT_FALSE(outHwInfo.capabilityTable.instrumentationEnabled);

    outHwInfo.capabilityTable.instrumentationEnabled = true;
    ret = hwConfig.configureHwInfo(&pInHwInfo, &outHwInfo, osInterface.get());
    ASSERT_EQ(0, ret);
    EXPECT_TRUE(outHwInfo.capabilityTable.instrumentationEnabled == haveInstrumentation);
}

} // namespace NEO
