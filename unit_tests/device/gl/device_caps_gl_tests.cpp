/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/helpers/basic_math.h"
#include "core/unit_tests/helpers/debug_manager_state_restore.h"
#include "runtime/helpers/hw_info.h"
#include "runtime/helpers/options.h"
#include "runtime/memory_manager/os_agnostic_memory_manager.h"
#include "runtime/os_interface/debug_settings_manager.h"
#include "test.h"
#include "unit_tests/fixtures/device_fixture.h"

#include "gmock/gmock.h"
#include "hw_cmds.h"

#include <memory>

using namespace NEO;

TEST(Device_GetCaps, givenForceClGlSharingWhenCapsAreCreatedThenDeviceReporstClGlSharingExtension) {
    DebugManagerStateRestore dbgRestorer;
    {
        DebugManager.flags.AddClGlSharing.set(true);
        auto device = std::unique_ptr<Device>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(platformDevices[0]));
        const auto &caps = device->getDeviceInfo();

        EXPECT_THAT(caps.deviceExtensions, testing::HasSubstr(std::string("cl_khr_gl_sharing ")));
        DebugManager.flags.AddClGlSharing.set(false);
    }
}
