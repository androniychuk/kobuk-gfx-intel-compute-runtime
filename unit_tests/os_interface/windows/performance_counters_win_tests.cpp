/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/helpers/options.h"
#include "unit_tests/os_interface/windows/mock_performance_counters_win.h"

#include "gtest/gtest.h"

using namespace NEO;

struct PerformanceCountersWinTest : public PerformanceCountersFixture,
                                    public ::testing::Test {
  public:
    void SetUp() override {
        PerformanceCountersFixture::SetUp();
    }

    void TearDown() override {
        PerformanceCountersFixture::TearDown();
    }
};
