/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/release_helper/release_helper.h"
#include "shared/source/xe2_hpg_core/hw_cmds_base.h"
#include "shared/test/unit_test/release_helper/release_helper_tests_base.h"

#include "gtest/gtest.h"

struct ReleaseHelper2004Tests : public ReleaseHelperTests<20, 4> {

    std::vector<uint32_t> getRevisions() override {
        return {0, 1, 4};
    }
};

TEST_F(ReleaseHelper2004Tests, whenGettingCapabilitiesThenCorrectPropertiesAreReturned) {
    for (auto &revision : getRevisions()) {
        ipVersion.revision = revision;
        releaseHelper = ReleaseHelper::create(ipVersion);
        ASSERT_NE(nullptr, releaseHelper);

        EXPECT_FALSE(releaseHelper->isAdjustWalkOrderAvailable());
        EXPECT_TRUE(releaseHelper->isMatrixMultiplyAccumulateSupported());
        EXPECT_TRUE(releaseHelper->isDotProductAccumulateSystolicSupported());
        EXPECT_FALSE(releaseHelper->isPipeControlPriorToNonPipelinedStateCommandsWARequired());
        EXPECT_FALSE(releaseHelper->isPipeControlPriorToPipelineSelectWaRequired());
        EXPECT_FALSE(releaseHelper->isProgramAllStateComputeCommandFieldsWARequired());
        EXPECT_FALSE(releaseHelper->isPrefetchDisablingRequired());
        EXPECT_FALSE(releaseHelper->isSplitMatrixMultiplyAccumulateSupported());
        EXPECT_TRUE(releaseHelper->isBFloat16ConversionSupported());
        EXPECT_TRUE(releaseHelper->isResolvingSubDeviceIDNeeded());
        EXPECT_FALSE(releaseHelper->isDirectSubmissionSupported());
        EXPECT_TRUE(releaseHelper->isAuxSurfaceModeOverrideRequired());
        EXPECT_TRUE(releaseHelper->isRcsExposureDisabled());
        EXPECT_FALSE(releaseHelper->isBindlessAddressingDisabled());
        EXPECT_EQ(8u, releaseHelper->getNumThreadsPerEu());
        EXPECT_TRUE(releaseHelper->isRayTracingSupported());
        EXPECT_TRUE(releaseHelper->isGlobalBindlessAllocatorEnabled());
    }
}

TEST_F(ReleaseHelper2004Tests, whenGettingMaxPreferredSlmSizeThenSizeSizeIsLimitedBy128K) {
    for (auto &revision : getRevisions()) {
        ipVersion.revision = revision;
        releaseHelper = ReleaseHelper::create(ipVersion);
        ASSERT_NE(nullptr, releaseHelper);

        using PREFERRED_SLM_ALLOCATION_SIZE = typename Xe2HpgCoreFamily::INTERFACE_DESCRIPTOR_DATA::PREFERRED_SLM_ALLOCATION_SIZE;
        for (auto &preferredSlmSize : {PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_0K,
                                       PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_16K,
                                       PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_32K,
                                       PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_64K,
                                       PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_96K,
                                       PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_128K}) {

            auto maxPreferredSlmValue = releaseHelper->getProductMaxPreferredSlmSize(preferredSlmSize);
            EXPECT_EQ(maxPreferredSlmValue, preferredSlmSize);
        }
        auto preferredSlmSize128k = PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_128K;
        auto preferredSlmSize160k = PREFERRED_SLM_ALLOCATION_SIZE::PREFERRED_SLM_ALLOCATION_SIZE_160K;
        auto maxPreferredSlmValue = releaseHelper->getProductMaxPreferredSlmSize(preferredSlmSize160k);
        EXPECT_EQ(maxPreferredSlmValue, preferredSlmSize128k);
    }
}

TEST_F(ReleaseHelper2004Tests, whenShouldAdjustCalledThenTrueReturned) {
    whenShouldAdjustCalledThenTrueReturned();
}

TEST_F(ReleaseHelper2004Tests, whenGettingSupportedNumGrfsThenCorrectValuesAreReturned) {
    whenGettingSupportedNumGrfsThenValues128And256Returned();
}

TEST_F(ReleaseHelper2004Tests, whenGettingThreadsPerEuConfigsThen4And8AreReturned) {
    whenGettingThreadsPerEuConfigsThen4And8AreReturned();
}

TEST_F(ReleaseHelper2004Tests, whenGettingTotalMemBankSizeThenReturn32GB) {
    whenGettingTotalMemBankSizeThenReturn32GB();
}

TEST_F(ReleaseHelper2004Tests, whenGettingAdditionalFp16AtomicCapabilitiesThenReturnNoCapabilities) {
    whenGettingAdditionalFp16AtomicCapabilitiesThenReturnNoCapabilities();
}

TEST_F(ReleaseHelper2004Tests, whenGettingAdditionalExtraKernelCapabilitiesThenReturnNoCapabilities) {
    whenGettingAdditionalExtraKernelCapabilitiesThenReturnNoCapabilities();
}
TEST_F(ReleaseHelper2004Tests, whenIsLocalOnlyAllowedCalledThenFalseReturned) {
    whenIsLocalOnlyAllowedCalledThenFalseReturned();
}