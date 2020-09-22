/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/core/source/debugger/debugger_l0.inl"

namespace NEO {

struct BDWFamily;
using GfxFamily = BDWFamily;

} // namespace NEO

namespace L0 {
template class DebuggerL0Hw<NEO::GfxFamily>;
} // namespace L0