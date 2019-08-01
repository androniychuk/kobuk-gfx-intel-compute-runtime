/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/context/context.h"
#include "runtime/platform/platform.h"
#include "unit_tests/helpers/variable_backup.h"

#include "cl_api_tests.h"

using namespace NEO;

namespace NEO {
extern bool getDevicesResult;
}; // namespace NEO

typedef api_tests clGetPlatformIDsTests;

namespace ULT {
TEST_F(clGetPlatformIDsTests, GivenNullPlatformWhenGettingPlatformIdsThenNumberofPlatformsIsReturned) {
    cl_int retVal = CL_SUCCESS;
    cl_uint numPlatforms = 0;

    retVal = clGetPlatformIDs(0, nullptr, &numPlatforms);

    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_GT(numPlatforms, 0u);
}

TEST_F(clGetPlatformIDsTests, GivenPlatformsWhenGettingPlatformIdsThenPlatformsIdIsReturned) {
    cl_int retVal = CL_SUCCESS;
    cl_platform_id platform = nullptr;

    retVal = clGetPlatformIDs(1, &platform, nullptr);

    EXPECT_EQ(CL_SUCCESS, retVal);
    EXPECT_NE(nullptr, platform);
}

TEST_F(clGetPlatformIDsTests, GivenNumEntriesZeroAndPlatformNotNullWhenGettingPlatformIdsThenClInvalidValueErrorIsReturned) {
    cl_int retVal = CL_SUCCESS;
    cl_platform_id platform = nullptr;

    retVal = clGetPlatformIDs(0, &platform, nullptr);

    EXPECT_EQ(CL_INVALID_VALUE, retVal);
}

TEST(clGetPlatformIDsNegativeTests, GivenFailedInitializationWhenGettingPlatformIdsThenClOutOfHostMemoryErrorIsReturned) {
    VariableBackup<decltype(getDevicesResult)> bkp(&getDevicesResult, false);

    cl_int retVal = CL_SUCCESS;
    cl_platform_id platformRet = nullptr;
    cl_uint numPlatforms = 0;

    retVal = clGetPlatformIDs(1, &platformRet, &numPlatforms);

    EXPECT_EQ(CL_OUT_OF_HOST_MEMORY, retVal);
    EXPECT_EQ(0u, numPlatforms);
    EXPECT_EQ(nullptr, platformRet);

    platformImpl.reset(nullptr);
}
} // namespace ULT
