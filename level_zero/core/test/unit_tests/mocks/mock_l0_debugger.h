/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "level_zero/core/source/debugger/debugger_l0.h"

namespace L0 {
namespace ult {
extern DebugerL0CreateFn mockDebuggerL0HwFactory[];

template <typename GfxFamily>
class MockDebuggerL0Hw : public L0::DebuggerL0Hw<GfxFamily> {
  public:
    using L0::DebuggerL0::perContextSbaAllocations;
    using L0::DebuggerL0::sbaTrackingGpuVa;

    MockDebuggerL0Hw(NEO::Device *device) : L0::DebuggerL0Hw<GfxFamily>(device) {}
    ~MockDebuggerL0Hw() override = default;

    static DebuggerL0 *allocate(NEO::Device *device) {
        return new MockDebuggerL0Hw<GfxFamily>(device);
    }

    void captureStateBaseAddress(NEO::CommandContainer &container, NEO::Debugger::SbaAddresses sba) override {
        captureStateBaseAddressCount++;
        L0::DebuggerL0Hw<GfxFamily>::captureStateBaseAddress(container, sba);
    }
    uint32_t captureStateBaseAddressCount = 0;
};

template <uint32_t productFamily, typename GfxFamily>
struct MockDebuggerL0HwPopulateFactory {
    MockDebuggerL0HwPopulateFactory() {
        mockDebuggerL0HwFactory[productFamily] = MockDebuggerL0Hw<GfxFamily>::allocate;
    }
};

} // namespace ult
} // namespace L0