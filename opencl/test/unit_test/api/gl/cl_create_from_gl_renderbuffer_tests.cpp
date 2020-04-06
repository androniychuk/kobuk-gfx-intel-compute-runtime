/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/test/unit_test/api/cl_api_tests.h"

using namespace NEO;

typedef api_tests clCreateFromGLRenderbuffer_;

namespace ULT {

TEST_F(clCreateFromGLRenderbuffer_, givenNullContextWhenCreateIsCalledThenErrorIsReturned) {
    int errCode = CL_SUCCESS;
    auto retVal = clCreateFromGLRenderbuffer(nullptr,           // cl_context context
                                             CL_MEM_READ_WRITE, // cl_mem_flags flags
                                             0,                 // GLuint renderbuffer
                                             &errCode           // cl_int *errcode_ret
    );
    EXPECT_EQ(nullptr, retVal);
    EXPECT_EQ(errCode, CL_INVALID_CONTEXT);
}
} // namespace ULT
