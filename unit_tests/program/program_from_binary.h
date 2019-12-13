/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "runtime/built_ins/built_ins.h"
#include "runtime/helpers/options.h"
#include "unit_tests/fixtures/context_fixture.h"
#include "unit_tests/fixtures/device_fixture.h"
#include "unit_tests/fixtures/program_fixture.h"
#include "unit_tests/mocks/mock_context.h"

#include <string>

namespace NEO {

////////////////////////////////////////////////////////////////////////////////
// ProgramFromBinaryTest Test Fixture
//      Used to test the Program class
////////////////////////////////////////////////////////////////////////////////
class ProgramFromBinaryTest : public DeviceFixture,
                              public ContextFixture,
                              public ProgramFixture,
                              public testing::TestWithParam<std::tuple<const char *, const char *>> {

    using ContextFixture::SetUp;

  protected:
    ProgramFromBinaryTest() : BinaryFileName(nullptr),
                              KernelName(nullptr),
                              retVal(CL_SUCCESS) {
    }

    void SetUp() override {
        std::tie(BinaryFileName, KernelName) = GetParam();

        DeviceFixture::SetUp();

        cl_device_id device = pDevice;
        ContextFixture::SetUp(1, &device);
        ProgramFixture::SetUp();

        if (options.size())
            CreateProgramFromBinary(pContext, &device, BinaryFileName, options);
        else
            CreateProgramFromBinary(pContext, &device, BinaryFileName);
    }

    void TearDown() override {
        knownSource.reset();
        ProgramFixture::TearDown();
        ContextFixture::TearDown();
        DeviceFixture::TearDown();
    }

    void setOptions(std::string &optionsIn) {
        options = optionsIn;
    }

    const char *BinaryFileName;
    const char *KernelName;
    cl_int retVal;
    std::string options;
};

////////////////////////////////////////////////////////////////////////////////
// ProgramSimpleFixture Test Fixture
//      Used to test the Program class, but not using parameters
////////////////////////////////////////////////////////////////////////////////
class ProgramSimpleFixture : public DeviceFixture,
                             public ContextFixture,
                             public ProgramFixture {
    using ContextFixture::SetUp;

  public:
    ProgramSimpleFixture() : retVal(CL_SUCCESS) {
    }

    void SetUp() override {
        DeviceFixture::SetUp();

        cl_device_id device = pDevice;
        ContextFixture::SetUp(1, &device);
        ProgramFixture::SetUp();
    }

    void TearDown() override {
        knownSource.reset();
        ProgramFixture::TearDown();
        ContextFixture::TearDown();
        DeviceFixture::TearDown();
    }

  protected:
    cl_int retVal;
};
} // namespace NEO
