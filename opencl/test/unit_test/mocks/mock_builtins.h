/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/built_ins/built_ins.h"
#include "shared/source/built_ins/sip.h"

#include "opencl/source/built_ins/builtins_dispatch_builder.h"
#include "opencl/source/program/program.h"

#include <memory>

namespace NEO {
class MockBuiltins : public BuiltIns {
  public:
    const SipKernel &getSipKernel(SipKernelType type, Device &device) override {
        if (sipKernelsOverride.find(type) != sipKernelsOverride.end()) {
            return *sipKernelsOverride[type];
        }
        getSipKernelCalled = true;
        getSipKernelType = type;
        return BuiltIns::getSipKernel(type, device);
    }

    void overrideSipKernel(std::unique_ptr<SipKernel> kernel) {
        sipKernelsOverride[kernel->getType()] = std::move(kernel);
    }
    std::unique_ptr<BuiltinDispatchInfoBuilder> setBuiltinDispatchInfoBuilder(EBuiltInOps::Type operation, Context &context, Device &device, std::unique_ptr<BuiltinDispatchInfoBuilder> builder);
    BuiltIns *originalGlobalBuiltins = nullptr;
    std::map<SipKernelType, std::unique_ptr<SipKernel>> sipKernelsOverride;
    bool getSipKernelCalled = false;
    SipKernelType getSipKernelType = SipKernelType::COUNT;
};
} // namespace NEO