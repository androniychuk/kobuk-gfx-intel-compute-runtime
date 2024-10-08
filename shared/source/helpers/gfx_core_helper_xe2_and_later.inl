/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/gmm_helper/gmm.h"
#include "shared/source/helpers/gfx_core_helper.h"

namespace NEO {

template <>
void GfxCoreHelperHw<Family>::applyAdditionalCompressionSettings(Gmm &gmm, bool isNotCompressed) const {
    gmm.resourceParams.Flags.Info.NotCompressed = isNotCompressed;
    if (!isNotCompressed) {
        gmm.resourceParams.Flags.Info.Cacheable = 0;
    }

    if (debugManager.flags.PrintGmmCompressionParams.get()) {
        printf("\n\tFlags.Info.NotCompressed: %u", gmm.resourceParams.Flags.Info.NotCompressed);
    }
}

template <>
void GfxCoreHelperHw<Family>::applyRenderCompressionFlag(Gmm &gmm, uint32_t isCompressed) const {}

template <>
bool GfxCoreHelperHw<Family>::isTimestampShiftRequired() const {
    return false;
}

template <>
void MemorySynchronizationCommands<Family>::encodeAdditionalTimestampOffsets(LinearStream &commandStream, uint64_t contextAddress, uint64_t globalAddress, bool isBcs) {
    EncodeStoreMMIO<Family>::encode(commandStream, RegisterOffsets::gpThreadTimeRegAddressOffsetHigh, contextAddress + sizeof(uint32_t), false, nullptr, isBcs);
    EncodeStoreMMIO<Family>::encode(commandStream, RegisterOffsets::globalTimestampUn, globalAddress + sizeof(uint32_t), false, nullptr, isBcs);
}

} // namespace NEO
