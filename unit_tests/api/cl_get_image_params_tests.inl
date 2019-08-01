/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/context/context.h"
#include "runtime/helpers/hw_info.h"
#include "unit_tests/mocks/mock_device.h"

#include "cl_api_tests.h"

using namespace NEO;

namespace ULT {

template <typename T>
struct clGetImageParams : public api_fixture,
                          public T {

    void SetUp() override {
        api_fixture::SetUp();

        // clang-format off
        imageFormat.image_channel_order     = CL_RGBA;
        imageFormat.image_channel_data_type = CL_UNORM_INT8;

        imageDesc.image_type        = CL_MEM_OBJECT_IMAGE2D;
        imageDesc.image_width       = 32;
        imageDesc.image_height      = 32;
        imageDesc.image_depth       = 1;
        imageDesc.image_array_size  = 1;
        imageDesc.image_row_pitch   = 0;
        imageDesc.image_slice_pitch = 0;
        imageDesc.num_mip_levels    = 0;
        imageDesc.num_samples       = 0;
        imageDesc.mem_object        = nullptr;
        // clang-format on
    }

    void TearDown() override {
        api_fixture::TearDown();
    }

    cl_image_format imageFormat;
    cl_image_desc imageDesc;
};

typedef clGetImageParams<::testing::Test> clGetImageParamsTest;

TEST_F(clGetImageParamsTest, GivenValidParamsWhenGettingImageParamsThenSuccessIsReturned) {
    size_t imageRowPitch = 0;
    size_t imageSlicePitch = 0;
    cl_int retVal = CL_INVALID_VALUE;

    retVal = clGetImageParamsINTEL(pContext, &imageFormat, &imageDesc, &imageRowPitch, &imageSlicePitch);
    ASSERT_EQ(CL_SUCCESS, retVal);
    EXPECT_NE(imageRowPitch, 0u);
    EXPECT_NE(imageSlicePitch, 0u);
}

TEST_F(clGetImageParamsTest, GivenNullContextWhenGettingImageParamsThenInvalidContextErrorIsReturned) {
    size_t imageRowPitch = 0;
    size_t imageSlicePitch = 0;
    cl_int retVal = CL_SUCCESS;

    retVal = clGetImageParamsINTEL(nullptr, &imageFormat, &imageDesc, &imageRowPitch, &imageSlicePitch);
    ASSERT_EQ(CL_INVALID_CONTEXT, retVal);
    EXPECT_EQ(imageRowPitch, 0u);
    EXPECT_EQ(imageSlicePitch, 0u);
}

TEST_F(clGetImageParamsTest, GivenNullParamsWhenGettingImageParamsThenInvalidValueErrorIsReturned) {
    size_t imageRowPitch = 0;
    size_t imageSlicePitch = 0;
    cl_int retVal = CL_SUCCESS;

    retVal = clGetImageParamsINTEL(pContext, nullptr, &imageDesc, &imageRowPitch, &imageSlicePitch);
    ASSERT_EQ(CL_INVALID_VALUE, retVal);
    EXPECT_EQ(imageRowPitch, 0u);
    EXPECT_EQ(imageSlicePitch, 0u);

    retVal = clGetImageParamsINTEL(pContext, &imageFormat, nullptr, &imageRowPitch, &imageSlicePitch);
    ASSERT_EQ(CL_INVALID_VALUE, retVal);
    EXPECT_EQ(imageRowPitch, 0u);
    EXPECT_EQ(imageSlicePitch, 0u);

    retVal = clGetImageParamsINTEL(pContext, &imageFormat, &imageDesc, nullptr, &imageSlicePitch);
    ASSERT_EQ(CL_INVALID_VALUE, retVal);
    EXPECT_EQ(imageRowPitch, 0u);
    EXPECT_EQ(imageSlicePitch, 0u);

    retVal = clGetImageParamsINTEL(pContext, &imageFormat, &imageDesc, &imageRowPitch, nullptr);
    ASSERT_EQ(CL_INVALID_VALUE, retVal);
    EXPECT_EQ(imageRowPitch, 0u);
    EXPECT_EQ(imageSlicePitch, 0u);
}

TEST_F(clGetImageParamsTest, GivenInvalidFormatWhenGettingImageParamsThenImageFormatNotSupportedErrorIsReturned) {
    size_t imageRowPitch = 0;
    size_t imageSlicePitch = 0;

    imageFormat.image_channel_order = CL_A;
    imageFormat.image_channel_data_type = CL_SIGNED_INT32;
    auto retVal = clGetImageParamsINTEL(pContext, &imageFormat, &imageDesc, &imageRowPitch, &imageSlicePitch);
    ASSERT_EQ(CL_IMAGE_FORMAT_NOT_SUPPORTED, retVal);
    EXPECT_EQ(imageRowPitch, 0u);
    EXPECT_EQ(imageSlicePitch, 0u);
}
} // namespace ULT
