/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "core/utilities/cpu_info.h"
#include "runtime/os_interface/hw_info_config.h"
#include "runtime/os_interface/os_interface.h"
#include "unit_tests/os_interface/hw_info_config_tests.h"
#include "unit_tests/os_interface/linux/drm_mock.h"

using namespace NEO;
using namespace std;

struct HwInfoConfigTestLinux : public HwInfoConfigTest {
    void SetUp() override;
    void TearDown() override;

    OSInterface *osInterface;

    DrmMock *drm;

    void (*rt_cpuidex_func)(int *, int, int);
};
