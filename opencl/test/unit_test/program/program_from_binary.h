/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/built_ins/built_ins.h"

#include "opencl/test/unit_test/fixtures/cl_device_fixture.h"
#include "opencl/test/unit_test/fixtures/context_fixture.h"
#include "opencl/test/unit_test/fixtures/program_fixture.h"
#include "opencl/test/unit_test/mocks/mock_context.h"

#include <string>

namespace NEO {

////////////////////////////////////////////////////////////////////////////////
// ProgramFromBinaryTest Test Fixture
//      Used to test the Program class
////////////////////////////////////////////////////////////////////////////////
class ProgramFromBinaryTest : public ClDeviceFixture,
                              public ContextFixture,
                              public ProgramFixture,
                              public testing::TestWithParam<std::tuple<const char *, const char *>> {

    using ContextFixture::SetUp;

  protected:
    void SetUp() override {
        std::tie(BinaryFileName, KernelName) = GetParam();

        ClDeviceFixture::SetUp();

        cl_device_id device = pClDevice;
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
        ClDeviceFixture::TearDown();
    }

    void setOptions(std::string &optionsIn) {
        options = optionsIn;
    }

    const char *BinaryFileName = nullptr;
    const char *KernelName = nullptr;
    cl_int retVal = CL_SUCCESS;
    std::string options;
};

////////////////////////////////////////////////////////////////////////////////
// ProgramSimpleFixture Test Fixture
//      Used to test the Program class, but not using parameters
////////////////////////////////////////////////////////////////////////////////
class ProgramSimpleFixture : public ClDeviceFixture,
                             public ContextFixture,
                             public ProgramFixture {
    using ContextFixture::SetUp;

  public:
    void SetUp() override {
        ClDeviceFixture::SetUp();

        cl_device_id device = pClDevice;
        ContextFixture::SetUp(1, &device);
        ProgramFixture::SetUp();
    }

    void TearDown() override {
        knownSource.reset();
        ProgramFixture::TearDown();
        ContextFixture::TearDown();
        ClDeviceFixture::TearDown();
    }

  protected:
    cl_int retVal = CL_SUCCESS;
};
} // namespace NEO
