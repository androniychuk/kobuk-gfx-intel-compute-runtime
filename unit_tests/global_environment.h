/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "core/helpers/hw_info.h"
#include "core/os_interface/os_library.h"
#include "unit_tests/mocks/mock_compilers.h"

#include "gtest/gtest.h"

using namespace NEO;

OsLibrary *setAdapterInfo(const PLATFORM *platform, const GT_SYSTEM_INFO *gtSystemInfo, uint64_t gpuAddressSpace);

class TestEnvironment : public ::testing::Environment {
  public:
    TestEnvironment();

    virtual void SetUp() override;

    virtual void TearDown() override;

    virtual void fclPushDebugVars(
        MockCompilerDebugVars &newDebugVars);

    virtual void fclPopDebugVars();

    virtual void igcPushDebugVars(
        MockCompilerDebugVars &newDebugVars);

    virtual void igcPopDebugVars();

    virtual void setDefaultDebugVars(
        MockCompilerDebugVars &fclDefaults,
        MockCompilerDebugVars &igcDefaults,
        HardwareInfo &hwInfo);

    virtual void setMockFileNames(
        std::string &fclMockFile,
        std::string &igcMockFile);

    virtual std::string &fclGetMockFile();
    virtual std::string &igcGetMockFile();

  protected:
    OsLibrary *libraryFrontEnd;
    OsLibrary *libraryIGC;
    OsLibrary *libraryOS;

    std::vector<MockCompilerDebugVars> igcDebugVarStack;
    std::vector<MockCompilerDebugVars> fclDebugVarStack;

    void (*igcSetDebugVarsFPtr)(MockCompilerDebugVars &debugVars);
    void (*fclSetDebugVarsFptr)(MockCompilerDebugVars &debugVars);

    MockCompilerDebugVars fclDefaultDebugVars;
    MockCompilerDebugVars igcDefaultDebugVars;
    HardwareInfo hwInfoDefaultDebugVars;

    std::string fclMockFile;
    std::string igcMockFile;

    MockCompilerEnableGuard mockCompilerGuard;
};

extern TestEnvironment *gEnvironment;
