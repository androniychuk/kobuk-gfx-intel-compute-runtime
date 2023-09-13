/*
 * Copyright (C) 2022-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/test/common/helpers/default_hw_info.h"
#include "shared/test/common/test_macros/header/per_product_test_definitions.h"
#include "shared/test/common/test_macros/test.h"

#include "level_zero/core/source/gfx_core_helpers/l0_gfx_core_helper.h"
#include "level_zero/core/test/unit_tests/fixtures/device_fixture.h"

#include "hw_cmds_xe_hpc_core_base.h"

namespace L0 {
namespace ult {

using L0GfxCoreHelperTestXeHpc = Test<DeviceFixture>;

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenCheckingL0HelperForMultiTileCapablePlatformThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.multiTileCapablePlatform());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenHpcPlatformsWhenAlwaysAllocateEventInLocalMemCalledThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.alwaysAllocateEventInLocalMem());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenCheckingL0HelperForCmdListHeapSharingSupportThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.platformSupportsCmdListHeapSharing());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenCheckingL0HelperForStateComputeModeTrackingSupportThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.platformSupportsStateComputeModeTracking());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenCheckingL0HelperForFrontEndTrackingSupportThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.platformSupportsFrontEndTracking());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenCheckingL0HelperForPipelineSelectTrackingSupportThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.platformSupportsPipelineSelectTracking());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenCheckingL0HelperForStateBaseAddressTrackingSupportThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.platformSupportsStateBaseAddressTracking());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenCheckingL0HelperForRayTracingSupportThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.platformSupportsRayTracing());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenGettingPlatformDefaultHeapAddressModelThenReturnPrivateHeaps) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_EQ(NEO::HeapAddressModel::PrivateHeaps, l0GfxCoreHelper.getPlatformHeapAddressModel());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenCheckingL0HelperForCmdlistPrimaryBufferSupportThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.platformSupportsPrimaryBatchBufferCmdList());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenCheckingL0HelperForPlatformSupportsImmediateFlushTaskThenReturnTrue) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_TRUE(l0GfxCoreHelper.platformSupportsImmediateComputeFlushTask());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenGetRegsetTypeForLargeGrfDetectionIsCalledThenCrRegsetTypeIsRetuned) {
    auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_EQ(ZET_DEBUG_REGSET_TYPE_CR_INTEL_GPU, l0GfxCoreHelper.getRegsetTypeForLargeGrfDetection());
}

XE_HPC_CORETEST_F(L0GfxCoreHelperTestXeHpc, GivenXeHpcWhenGettingSupportedRTASFormatThenExpectedFormatIsReturned) {
    const auto &l0GfxCoreHelper = getHelper<L0GfxCoreHelper>();
    EXPECT_EQ(ZE_RTAS_DEVICE_FORMAT_EXP_VERSION_1, static_cast<ze_rtas_device_format_internal_t>(l0GfxCoreHelper.getSupportedRTASFormat()));
}

} // namespace ult
} // namespace L0
