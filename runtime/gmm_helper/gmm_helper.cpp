/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/gmm_helper/gmm_helper.h"

#include "core/helpers/debug_helpers.h"
#include "core/memory_manager/graphics_allocation.h"
#include "core/os_interface/os_library.h"
#include "core/sku_info/operations/sku_info_transfer.h"
#include "runtime/execution_environment/execution_environment.h"
#include "runtime/helpers/get_info.h"
#include "runtime/helpers/hw_info.h"
#include "runtime/helpers/surface_formats.h"
#include "runtime/platform/platform.h"

#include "gmm_client_context.h"

namespace NEO {

GmmClientContext *GmmHelper::getClientContext() {
    return getInstance()->gmmClientContext.get();
}

const HardwareInfo *GmmHelper::getHardwareInfo() {
    return hwInfo;
}

GmmHelper *GmmHelper::getInstance() {
    return platform()->peekExecutionEnvironment()->getGmmHelper();
}

uint32_t GmmHelper::getMOCS(uint32_t type) {
    MEMORY_OBJECT_CONTROL_STATE mocs = gmmClientContext->cachePolicyGetMemoryObject(nullptr, static_cast<GMM_RESOURCE_USAGE_TYPE>(type));

    return static_cast<uint32_t>(mocs.DwordValue);
}

void GmmHelper::queryImgFromBufferParams(ImageInfo &imgInfo, GraphicsAllocation *gfxAlloc) {
    // 1D or 2D from buffer
    if (imgInfo.imgDesc->image_row_pitch > 0) {
        imgInfo.rowPitch = imgInfo.imgDesc->image_row_pitch;
    } else {
        imgInfo.rowPitch = getValidParam(imgInfo.imgDesc->image_width) * imgInfo.surfaceFormat->ImageElementSizeInBytes;
    }
    imgInfo.slicePitch = imgInfo.rowPitch * getValidParam(imgInfo.imgDesc->image_height);
    imgInfo.size = gfxAlloc->getUnderlyingBufferSize();
    imgInfo.qPitch = 0;
}

uint32_t GmmHelper::getRenderMultisamplesCount(uint32_t numSamples) {
    if (numSamples == 2) {
        return 1;
    } else if (numSamples == 4) {
        return 2;
    } else if (numSamples == 8) {
        return 3;
    } else if (numSamples == 16) {
        return 4;
    }
    return 0;
}

GMM_YUV_PLANE GmmHelper::convertPlane(OCLPlane oclPlane) {
    if (oclPlane == OCLPlane::PLANE_Y) {
        return GMM_PLANE_Y;
    } else if (oclPlane == OCLPlane::PLANE_U || oclPlane == OCLPlane::PLANE_UV) {
        return GMM_PLANE_U;
    } else if (oclPlane == OCLPlane::PLANE_V) {
        return GMM_PLANE_V;
    }

    return GMM_NO_PLANE;
}
GmmHelper::GmmHelper(const HardwareInfo *pHwInfo) : hwInfo(pHwInfo) {
    loadLib();
    gmmClientContext = GmmHelper::createGmmContextWrapperFunc(const_cast<HardwareInfo *>(pHwInfo), this->initGmmFunc, this->destroyGmmFunc);
    UNRECOVERABLE_IF(!gmmClientContext);
}

GmmHelper::~GmmHelper() = default;

decltype(GmmHelper::createGmmContextWrapperFunc) GmmHelper::createGmmContextWrapperFunc = GmmClientContextBase::create<GmmClientContext>;
} // namespace NEO
