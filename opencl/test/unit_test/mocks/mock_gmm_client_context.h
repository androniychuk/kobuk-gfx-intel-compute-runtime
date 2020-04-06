/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "opencl/test/unit_test/mocks/mock_gmm_client_context_base.h"

namespace NEO {
class MockGmmClientContext : public MockGmmClientContextBase {
  public:
    MockGmmClientContext(OSInterface *osInterface, HardwareInfo *hwInfo);
};
} // namespace NEO
