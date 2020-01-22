/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/debug_settings/debug_settings_manager.h"
#include "core/helpers/hw_info.h"
#include "runtime/os_interface/hw_info_config.h"
#include "runtime/os_interface/hw_info_config.inl"
#include "runtime/os_interface/hw_info_config_bdw_plus.inl"

namespace NEO {

#ifdef SUPPORT_TGLLP
template <>
int HwInfoConfigHw<IGFX_TIGERLAKE_LP>::configureHardwareCustom(HardwareInfo *hwInfo, OSInterface *osIface) {
    hwInfo->capabilityTable.ftrRenderCompressedImages = hwInfo->featureTable.ftrE2ECompression;
    hwInfo->capabilityTable.ftrRenderCompressedBuffers = hwInfo->featureTable.ftrE2ECompression;

    return 0;
}

template <>
void HwInfoConfigHw<IGFX_TIGERLAKE_LP>::adjustPlatformForProductFamily(HardwareInfo *hwInfo) {
    PLATFORM *platform = &hwInfo->platform;
    platform->eRenderCoreFamily = IGFX_GEN12LP_CORE;
    platform->eDisplayCoreFamily = IGFX_GEN12LP_CORE;
}

template class HwInfoConfigHw<IGFX_TIGERLAKE_LP>;
#endif

} // namespace NEO
