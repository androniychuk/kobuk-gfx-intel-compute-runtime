/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "offline_compiler/multi_command.h"
#include "offline_compiler/offline_compiler.h"

#include "gtest/gtest.h"
#include <CL/cl.h>

#include <cstdint>

namespace NEO {

class OfflineCompilerTests : public ::testing::Test {
  public:
    OfflineCompilerTests() : pOfflineCompiler(nullptr),
                             retVal(CL_SUCCESS) {
        // ctor
    }

    OfflineCompiler *pOfflineCompiler;
    int retVal;
};

class MultiCommandTests : public ::testing::Test {
  public:
    MultiCommandTests() : pMultiCommand(nullptr),
                          retVal(CL_SUCCESS) {
    }
    void createFileWithArgs(const std::vector<std::string> &, int numOfBuild);
    void deleteFileWithArgs();
    MultiCommand *pMultiCommand;
    std::string nameOfFileWithArgs;
    int retVal;
};

void MultiCommandTests::createFileWithArgs(const std::vector<std::string> &singleArgs, int numOfBuild) {
    std::ofstream myfile(nameOfFileWithArgs);
    if (myfile.is_open()) {
        for (int i = 0; i < numOfBuild; i++) {
            for (auto singleArg : singleArgs)
                myfile << singleArg + " ";
            myfile << std::endl;
        }
        myfile.close();
    } else
        printf("Unable to open file\n");
}

void MultiCommandTests::deleteFileWithArgs() {
    if (remove(nameOfFileWithArgs.c_str()) != 0)
        perror("Error deleting file");
}

} // namespace NEO
