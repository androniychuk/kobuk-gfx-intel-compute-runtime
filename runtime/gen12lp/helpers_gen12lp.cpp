/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/gen12lp/helpers_gen12lp.h"

#include "runtime/command_stream/command_stream_receiver.h"
#include "runtime/command_stream/command_stream_receiver_simulated_common_hw.h"

namespace NEO {
namespace Gen12LPHelpers {
bool hdcFlushForPipeControlBeforeStateBaseAddressRequired(PRODUCT_FAMILY productFamily) {
    return (productFamily == PRODUCT_FAMILY::IGFX_TIGERLAKE_LP);
}

bool pipeControlWaRequired(PRODUCT_FAMILY productFamily) {
    return (productFamily == PRODUCT_FAMILY::IGFX_TIGERLAKE_LP);
}

bool imagePitchAlignmentWaRequired(PRODUCT_FAMILY productFamily) {
    return (productFamily == PRODUCT_FAMILY::IGFX_TIGERLAKE_LP);
}

void adjustCoherencyFlag(PRODUCT_FAMILY productFamily, bool &coherencyFlag) {}

bool isLocalMemoryEnabled(const HardwareInfo &hwInfo) {
    return false;
}

void initAdditionalGlobalMMIO(const CommandStreamReceiver &commandStreamReceiver, AubMemDump::AubStream &stream) {}

uint64_t getPPGTTAdditionalBits(GraphicsAllocation *graphicsAllocation) {
    return 0;
}

void adjustAubGTTData(const CommandStreamReceiver &commandStreamReceiver, AubGTTData &data) {}

void setAdditionalPipelineSelectFields(void *pipelineSelectCmd,
                                       const PipelineSelectArgs &pipelineSelectArgs,
                                       const HardwareInfo &hwInfo) {}

bool isPageTableManagerSupported(const HardwareInfo &hwInfo) {
    return hwInfo.capabilityTable.ftrRenderCompressedBuffers || hwInfo.capabilityTable.ftrRenderCompressedImages;
}

bool obtainRenderBufferCompressionPreference(const HardwareInfo &hwInfo, const size_t size) {
    return false;
}

void setAdditionalSurfaceStateParamsForImageCompression(Image &image, TGLLPFamily::RENDER_SURFACE_STATE *surfaceState) {
}

} // namespace Gen12LPHelpers
} // namespace NEO
