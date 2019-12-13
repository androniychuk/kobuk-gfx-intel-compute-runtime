/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "unit_tests/mocks/mock_gmm_client_context_base.h"

namespace NEO {
class MockGmmClientContext : public MockGmmClientContextBase {
  public:
    MockGmmClientContext(HardwareInfo *hwInfo, decltype(&InitializeGmm) initFunc, decltype(&GmmDestroy) destroyFunc);
};
} // namespace NEO
