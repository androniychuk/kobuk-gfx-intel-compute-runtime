/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/os_interface/windows/os_interface.h"
#include "unit_tests/os_interface/windows/hw_info_config_win_tests.h"

using namespace NEO;
using namespace std;

using HwInfoConfigTestWindowsIcllp = HwInfoConfigTestWindows;

ICLLPTEST_F(HwInfoConfigTestWindowsIcllp, whenCallAdjustPlatformThenDoNothing) {
    EXPECT_EQ(IGFX_ICELAKE_LP, productFamily);
    auto hwInfoConfig = HwInfoConfig::get(productFamily);
    outHwInfo = pInHwInfo;
    hwInfoConfig->adjustPlatformForProductFamily(&outHwInfo);

    int ret = memcmp(&outHwInfo.platform, &pInHwInfo.platform, sizeof(PLATFORM));
    EXPECT_EQ(0, ret);
}
