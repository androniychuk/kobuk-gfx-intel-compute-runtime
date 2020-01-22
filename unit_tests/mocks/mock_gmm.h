/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "core/helpers/options.h"
#include "runtime/gmm_helper/gmm.h"
#include "runtime/helpers/surface_formats.h"
#include "unit_tests/mocks/mock_device.h"
#include "unit_tests/mocks/mock_gmm_resource_info.h"

namespace NEO {
namespace MockGmmParams {
static SurfaceFormatInfo mockSurfaceFormat;
}

class MockGmm : public Gmm {
  public:
    using Gmm::Gmm;
    using Gmm::setupImageResourceParams;

    MockGmm() : Gmm(nullptr, nullptr, 1, false){};

    static std::unique_ptr<Gmm> queryImgParams(GmmClientContext *clientContext, ImageInfo &imgInfo) {
        return std::unique_ptr<Gmm>(new Gmm(clientContext, imgInfo, {}));
    }

    static ImageInfo initImgInfo(cl_image_desc &imgDesc, int baseMipLevel, const SurfaceFormatInfo *surfaceFormat) {
        ImageInfo imgInfo = {0};
        imgInfo.baseMipLevel = baseMipLevel;
        imgInfo.imgDesc = &imgDesc;
        if (!surfaceFormat) {
            ArrayRef<const SurfaceFormatInfo> readWriteSurfaceFormats = SurfaceFormats::readWrite();
            MockGmmParams::mockSurfaceFormat = readWriteSurfaceFormats[0]; // any valid format
            imgInfo.surfaceFormat = &MockGmmParams::mockSurfaceFormat;
        } else {
            imgInfo.surfaceFormat = surfaceFormat;
        }
        return imgInfo;
    }

    static GraphicsAllocation *allocateImage2d(MemoryManager &memoryManager) {
        cl_image_desc imgDesc{};
        imgDesc.image_type = CL_MEM_OBJECT_IMAGE2D;
        imgDesc.image_width = 5;
        imgDesc.image_height = 5;
        auto imgInfo = MockGmm::initImgInfo(imgDesc, 0, nullptr);
        return memoryManager.allocateGraphicsMemoryWithProperties({0, true, imgInfo, GraphicsAllocation::AllocationType::IMAGE});
    }
};
} // namespace NEO
