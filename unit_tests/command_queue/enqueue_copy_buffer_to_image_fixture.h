/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "core/helpers/ptr_math.h"
#include "unit_tests/command_queue/command_enqueue_fixture.h"
#include "unit_tests/command_queue/enqueue_fixture.h"
#include "unit_tests/fixtures/buffer_fixture.h"
#include "unit_tests/fixtures/image_fixture.h"

#include "gtest/gtest.h"

namespace NEO {

struct EnqueueCopyBufferToImageTest : public CommandEnqueueFixture,
                                      public ::testing::Test {

    EnqueueCopyBufferToImageTest() : srcBuffer(nullptr),
                                     dstImage(nullptr) {
    }

    virtual void SetUp(void) override {
        CommandEnqueueFixture::SetUp();

        BufferDefaults::context = new MockContext(pDevice);
        context = new MockContext(pDevice);
        srcBuffer = BufferHelper<>::create(context);
        dstImage = Image2dHelper<>::create(context);
    }

    virtual void TearDown(void) override {
        delete dstImage;
        delete srcBuffer;
        delete BufferDefaults::context;
        delete context;
        CommandEnqueueFixture::TearDown();
    }

  protected:
    template <typename FamilyType>
    void enqueueCopyBufferToImage() {
        auto retVal = EnqueueCopyBufferToImageHelper<>::enqueueCopyBufferToImage(
            pCmdQ,
            srcBuffer,
            dstImage);
        EXPECT_EQ(CL_SUCCESS, retVal);
        parseCommands<FamilyType>(*pCmdQ);
    }

    MockContext *context;
    Buffer *srcBuffer;
    Image *dstImage;
};

struct EnqueueCopyBufferToImageMipMapTest : public CommandEnqueueFixture,
                                            public ::testing::Test,
                                            public ::testing::WithParamInterface<uint32_t> {

    EnqueueCopyBufferToImageMipMapTest() : srcBuffer(nullptr) {
    }

    virtual void SetUp(void) override {
        CommandEnqueueFixture::SetUp();
        BufferDefaults::context = new MockContext(pDevice);
        context = new MockContext(pDevice);
        srcBuffer = BufferHelper<>::create(context);
    }

    virtual void TearDown(void) override {
        delete srcBuffer;
        delete BufferDefaults::context;
        delete context;
        CommandEnqueueFixture::TearDown();
    }

    MockContext *context;
    Buffer *srcBuffer;
};

} // namespace NEO
