/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "test.h"
#include "unit_tests/fixtures/device_fixture.h"

using namespace NEO;

typedef Test<DeviceFixture> KblDeviceCaps;

KBLTEST_F(KblDeviceCaps, reportsOcl21) {
    const auto &caps = pDevice->getDeviceInfo();
    EXPECT_STREQ("OpenCL 2.1 NEO ", caps.clVersion);
    EXPECT_STREQ("OpenCL C 2.0 ", caps.clCVersion);
}

KBLTEST_F(KblDeviceCaps, GivenKBLWhenCheckftr64KBpagesThenTrue) {
    EXPECT_TRUE(pDevice->getHardwareInfo().capabilityTable.ftr64KBpages);
}

KBLTEST_F(KblDeviceCaps, givenKblWhenCheckFtrSupportsInteger64BitAtomicsThenReturnTrue) {
    EXPECT_TRUE(pDevice->getHardwareInfo().capabilityTable.ftrSupportsInteger64BitAtomics);
}