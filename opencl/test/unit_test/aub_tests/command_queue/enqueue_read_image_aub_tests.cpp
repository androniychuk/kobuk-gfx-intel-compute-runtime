/*
 * Copyright (C) 2018-2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/command_stream_receiver.h"
#include "shared/source/device/device.h"
#include "shared/source/helpers/aligned_memory.h"
#include "shared/source/helpers/ptr_math.h"
#include "shared/source/memory_manager/os_agnostic_memory_manager.h"
#include "shared/test/common/test_macros/test.h"

#include "opencl/source/mem_obj/image.h"
#include "opencl/test/unit_test/aub_tests/command_queue/command_enqueue_fixture.h"
#include "opencl/test/unit_test/aub_tests/command_queue/enqueue_read_write_image_aub_fixture.h"
#include "opencl/test/unit_test/mocks/mock_context.h"

using namespace NEO;

struct ReadImageParams {
    cl_mem_object_type imageType;
    size_t offsets[3];
} readImageParams[] = {
    {CL_MEM_OBJECT_IMAGE1D, {0u, 0u, 0u}},
    {CL_MEM_OBJECT_IMAGE1D, {1u, 0u, 0u}},
    {CL_MEM_OBJECT_IMAGE2D, {0u, 0u, 0u}},
    {CL_MEM_OBJECT_IMAGE2D, {1u, 2u, 0u}},
    {CL_MEM_OBJECT_IMAGE3D, {0u, 0u, 0u}},
    {CL_MEM_OBJECT_IMAGE3D, {1u, 2u, 3u}},
};

struct AUBReadImage
    : public CommandDeviceFixture,
      public AUBCommandStreamFixture,
      public ::testing::WithParamInterface<std::tuple<uint32_t /*cl_channel_type*/, uint32_t /*cl_channel_order*/, ReadImageParams>>,
      public ::testing::Test {

    typedef AUBCommandStreamFixture CommandStreamFixture;

    using AUBCommandStreamFixture::SetUp;

    void SetUp() override {
        if (!(defaultHwInfo->capabilityTable.supportsImages)) {
            GTEST_SKIP();
        }
        CommandDeviceFixture::SetUp(cl_command_queue_properties(0));
        CommandStreamFixture::SetUp(pCmdQ);

        context = std::make_unique<MockContext>(pClDevice);
    }

    void TearDown() override {
        srcImage.reset();
        context.reset();

        CommandStreamFixture::TearDown();
        CommandDeviceFixture::TearDown();
    }

    std::unique_ptr<MockContext> context;
    std::unique_ptr<Image> srcImage;
};

HWTEST_P(AUBReadImage, GivenUnalignedMemoryWhenReadingImageThenExpectationsAreMet) {
    const auto testWidth = 5u;
    const auto testHeight = std::get<2>(GetParam()).imageType != CL_MEM_OBJECT_IMAGE1D ? 5u : 1u;
    const auto testDepth = std::get<2>(GetParam()).imageType == CL_MEM_OBJECT_IMAGE3D ? 5u : 1u;
    auto numPixels = testWidth * testHeight * testDepth;

    cl_image_format imageFormat;
    cl_image_desc imageDesc;
    // clang-format off
    imageFormat.image_channel_data_type = std::get<0>(GetParam());
    imageFormat.image_channel_order     = std::get<1>(GetParam());

    imageDesc.image_type        = std::get<2>(GetParam()).imageType;
    imageDesc.image_width       = testWidth;
    imageDesc.image_height      = testHeight;
    imageDesc.image_depth       = testDepth;
    imageDesc.image_array_size  = 1;
    imageDesc.image_row_pitch   = 0;
    imageDesc.image_slice_pitch = 0;
    imageDesc.num_mip_levels    = 0;
    imageDesc.num_samples       = 0;
    imageDesc.mem_object        = NULL;
    // clang-format on

    auto perChannelDataSize = 0u;
    switch (imageFormat.image_channel_data_type) {
    case CL_UNORM_INT8:
        perChannelDataSize = 1u;
        break;
    case CL_SIGNED_INT16:
    case CL_HALF_FLOAT:
        perChannelDataSize = 2u;
        break;
    case CL_UNSIGNED_INT32:
    case CL_FLOAT:
        perChannelDataSize = 4u;
        break;
    }

    auto numChannels = 0u;
    switch (imageFormat.image_channel_order) {
    case CL_R:
        numChannels = 1u;
        break;
    case CL_RG:
        numChannels = 2u;
        break;
    case CL_RGBA:
        numChannels = 4u;
        break;
    }
    size_t elementSize = perChannelDataSize * numChannels;
    size_t rowPitch = testWidth * elementSize;
    size_t slicePitch = rowPitch * testHeight;

    // Generate initial dst memory but make it unaligned to page size
    auto dstMemoryAligned = alignedMalloc(4 + elementSize * numPixels, MemoryConstants::pageSize);
    auto dstMemoryUnaligned = ptrOffset(reinterpret_cast<uint8_t *>(dstMemoryAligned), 4);

    auto sizeMemory = testWidth * alignUp(testHeight, 4) * testDepth * elementSize;
    auto srcMemory = new (std::nothrow) uint8_t[sizeMemory];
    ASSERT_NE(nullptr, srcMemory);

    for (auto i = 0u; i < sizeMemory; ++i) {
        srcMemory[i] = static_cast<uint8_t>(i);
    }

    memset(dstMemoryUnaligned, 0xFF, numPixels * elementSize);

    cl_mem_flags flags = CL_MEM_USE_HOST_PTR;
    auto surfaceFormat = Image::getSurfaceFormatFromTable(flags, &imageFormat, context->getDevice(0)->getHardwareInfo().capabilityTable.supportsOcl21Features);
    auto retVal = CL_INVALID_VALUE;
    srcImage.reset(Image::create(
        context.get(),
        ClMemoryPropertiesHelper::createMemoryProperties(flags, 0, 0, &context->getDevice(0)->getDevice()),
        flags,
        0,
        surfaceFormat,
        &imageDesc,
        srcMemory,
        retVal));
    ASSERT_NE(nullptr, srcImage.get());

    auto origin = std::get<2>(GetParam()).offsets;

    // Only draw 1/4 of the original image
    const size_t region[3] = {std::max(testWidth / 2, 1u),
                              std::max(testHeight / 2, 1u),
                              std::max(testDepth / 2, 1u)};

    retVal = pCmdQ->enqueueReadImage(
        srcImage.get(),
        CL_TRUE,
        origin,
        region,
        rowPitch,
        slicePitch,
        dstMemoryUnaligned,
        nullptr,
        0,
        nullptr,
        nullptr);
    EXPECT_EQ(CL_SUCCESS, retVal);

    retVal = pCmdQ->finish();
    EXPECT_EQ(CL_SUCCESS, retVal);

    auto imageMemory = srcMemory;
    auto memoryPool = srcImage->getGraphicsAllocation(context->getDevice(0)->getRootDeviceIndex())->getMemoryPool();
    bool isGpuCopy = srcImage->isTiledAllocation() || !MemoryPool::isSystemMemoryPool(memoryPool);
    if (!isGpuCopy) {
        imageMemory = reinterpret_cast<uint8_t *>(srcImage->getCpuAddress());
    }

    auto offset = (origin[2] * testWidth * testHeight + origin[1] * testWidth + origin[0]) * elementSize;
    auto pSrcMemory = ptrOffset(imageMemory, offset);
    auto pDstMemory = dstMemoryUnaligned;

    for (size_t z = 0; z < region[2]; ++z) {
        for (size_t y = 0; y < region[1]; ++y) {
            AUBCommandStreamFixture::expectMemory<FamilyType>(pDstMemory, pSrcMemory, elementSize * region[0]);

            pDstMemory = ptrOffset(pDstMemory, rowPitch);
            pSrcMemory = ptrOffset(pSrcMemory, rowPitch);
        }

        pDstMemory = ptrOffset(pDstMemory, slicePitch - (rowPitch * region[1]));
        pSrcMemory = ptrOffset(pSrcMemory, slicePitch - (rowPitch * region[1]));
    }

    alignedFree(dstMemoryAligned);
    delete[] srcMemory;
}

INSTANTIATE_TEST_CASE_P(
    AUBReadImage_simple, AUBReadImage,
    ::testing::Combine(::testing::Values( // formats
                           CL_UNORM_INT8, CL_SIGNED_INT16, CL_UNSIGNED_INT32,
                           CL_HALF_FLOAT, CL_FLOAT),
                       ::testing::Values( // channels
                           CL_R, CL_RG, CL_RGBA),
                       ::testing::ValuesIn(readImageParams)));

using AUBReadImageUnaligned = AUBImageUnaligned;

HWTEST_F(AUBReadImageUnaligned, GivenMisalignedHostPtrWhenReadingImageThenExpectationsAreMet) {
    const std::vector<size_t> pixelSizes = {1, 2, 4};
    const std::vector<size_t> offsets = {0, 1, 2, 3};
    const std::vector<size_t> sizes = {3, 2, 1};

    for (auto pixelSize : pixelSizes) {
        for (auto offset : offsets) {
            for (auto size : sizes) {
                testReadImageUnaligned<FamilyType>(offset, size, pixelSize);
            }
        }
    }
}
