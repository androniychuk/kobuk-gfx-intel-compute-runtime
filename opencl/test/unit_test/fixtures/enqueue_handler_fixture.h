/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "opencl/test/unit_test/fixtures/device_fixture.h"
#include "opencl/test/unit_test/mocks/mock_context.h"

#include "gtest/gtest.h"

class EnqueueHandlerTest : public NEO::DeviceFixture,
                           public testing::Test {
  public:
    void SetUp() override {
        DeviceFixture::SetUp();
        context = new NEO::MockContext(pClDevice);
    }

    void TearDown() override {
        context->decRefInternal();
        DeviceFixture::TearDown();
    }
    NEO::MockContext *context;
};
