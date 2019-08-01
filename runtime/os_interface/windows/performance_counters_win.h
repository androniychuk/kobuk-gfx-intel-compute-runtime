/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "runtime/os_interface/performance_counters.h"

namespace NEO {

class PerformanceCountersWin : virtual public PerformanceCounters {
  public:
    PerformanceCountersWin() = default;
    ~PerformanceCountersWin() override = default;

    /////////////////////////////////////////////////////
    // Gpu oa/mmio configuration.
    /////////////////////////////////////////////////////
    bool enableCountersConfiguration() override;
    void releaseCountersConfiguration() override;
};
} // namespace NEO
