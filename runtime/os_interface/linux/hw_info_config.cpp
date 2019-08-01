/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/os_interface/hw_info_config.h"

#include "runtime/command_stream/preemption.h"
#include "runtime/gen_common/hw_cmds.h"
#include "runtime/helpers/hw_helper.h"
#include "runtime/helpers/hw_info.h"
#include "runtime/memory_manager/memory_constants.h"
#include "runtime/os_interface/debug_settings_manager.h"
#include "runtime/os_interface/linux/drm_neo.h"
#include "runtime/os_interface/linux/os_interface.h"
#include "runtime/utilities/cpu_info.h"

#include <cstring>

namespace NEO {

HwInfoConfig *hwInfoConfigFactory[IGFX_MAX_PRODUCT] = {};

uint32_t bitExact(uint32_t value, uint32_t highBit, uint32_t lowBit) {
    uint32_t bitVal = ((value >> lowBit) & ((1 << (highBit - lowBit + 1)) - 1));
    return bitVal;
}

int configureCacheInfo(HardwareInfo *hwInfo) {
    GT_SYSTEM_INFO *gtSystemInfo = &hwInfo->gtSystemInfo;

    uint32_t type = 0;
    uint32_t subleaf = 0;
    uint32_t eax, ebx, ecx;
    uint32_t cachelevel, linesize, partitions, ways;
    uint64_t sets, size;

    const CpuInfo &cpuInfo = CpuInfo::getInstance();

    do {
        uint32_t cpuRegsInfo[4];

        cpuInfo.cpuidex(cpuRegsInfo, 4, subleaf);
        eax = cpuRegsInfo[0];
        ebx = cpuRegsInfo[1];
        ecx = cpuRegsInfo[2];

        type = bitExact(eax, 4, 0);
        if (type != 0) {
            cachelevel = bitExact(eax, 7, 5);
            linesize = bitExact(ebx, 11, 0) + 1;
            partitions = bitExact(ebx, 21, 12) + 1;
            ways = bitExact(ebx, 31, 22) + 1;
            sets = (uint64_t)ecx + 1;

            size = sets * ways * partitions * linesize / 1024;
            if (cachelevel == 3) {
                gtSystemInfo->LLCCacheSizeInKb = size;
            }
            subleaf++;
        }
    } while (type);

    return 0;
}

int HwInfoConfig::configureHwInfo(const HardwareInfo *inHwInfo, HardwareInfo *outHwInfo, OSInterface *osIface) {
    int ret = 0;
    Drm *drm = osIface->get()->getDrm();

    *outHwInfo = *inHwInfo;
    auto platform = &outHwInfo->platform;
    auto gtSystemInfo = &outHwInfo->gtSystemInfo;
    auto featureTable = &outHwInfo->featureTable;

    int val = 0;
    ret = drm->getDeviceID(val);
    if (ret != 0 || val == 0) {
        *outHwInfo = {};
        return (ret == 0) ? -1 : ret;
    }
    platform->usDeviceID = static_cast<unsigned short>(val);
    ret = drm->getDeviceRevID(val);
    if (ret != 0) {
        *outHwInfo = {};
        return ret;
    }
    platform->usRevId = static_cast<unsigned short>(val);

    int euCount;
    ret = drm->getEuTotal(euCount);
    if (ret != 0) {
        *outHwInfo = {};
        return ret;
    }
    gtSystemInfo->EUCount = static_cast<uint32_t>(euCount);

    gtSystemInfo->ThreadCount = this->threadsPerEu * gtSystemInfo->EUCount;

    int subSliceCount;
    ret = drm->getSubsliceTotal(subSliceCount);
    if (ret != 0) {
        *outHwInfo = {};
        return ret;
    }
    gtSystemInfo->SubSliceCount = static_cast<uint32_t>(subSliceCount);

    featureTable->ftrSVM = drm->is48BitAddressRangeSupported();

    int maxGpuFreq = 0;
    drm->getMaxGpuFrequency(maxGpuFreq);

    GTTYPE gtType = drm->getGtType();
    if (gtType == GTTYPE_UNDEFINED) {
        *outHwInfo = {};
        return -1;
    }
    platform->eGTType = gtType;
    featureTable->ftrGTA = (gtType == GTTYPE_GTA) ? 1 : 0;
    featureTable->ftrGTC = (gtType == GTTYPE_GTC) ? 1 : 0;
    featureTable->ftrGTX = (gtType == GTTYPE_GTX) ? 1 : 0;
    featureTable->ftrGT1 = (gtType == GTTYPE_GT1) ? 1 : 0;
    featureTable->ftrGT1_5 = (gtType == GTTYPE_GT1_5) ? 1 : 0;
    featureTable->ftrGT2 = (gtType == GTTYPE_GT2) ? 1 : 0;
    featureTable->ftrGT2_5 = (gtType == GTTYPE_GT2_5) ? 1 : 0;
    featureTable->ftrGT3 = (gtType == GTTYPE_GT3) ? 1 : 0;
    featureTable->ftrGT4 = (gtType == GTTYPE_GT4) ? 1 : 0;

    ret = configureHardwareCustom(outHwInfo, osIface);
    if (ret != 0) {
        *outHwInfo = {};
        return ret;
    }
    configureCacheInfo(outHwInfo);
    featureTable->ftrEDram = (gtSystemInfo->EdramSizeInKb != 0) ? 1 : 0;

    outHwInfo->capabilityTable.maxRenderFrequency = maxGpuFreq;
    outHwInfo->capabilityTable.ftrSvm = featureTable->ftrSVM;

    HwHelper &hwHelper = HwHelper::get(platform->eRenderCoreFamily);
    outHwInfo->capabilityTable.ftrSupportsCoherency = false;

    hwHelper.adjustDefaultEngineType(outHwInfo);
    outHwInfo->capabilityTable.defaultEngineType = getChosenEngineType(*outHwInfo);

    outHwInfo->capabilityTable.instrumentationEnabled = false;
    outHwInfo->capabilityTable.ftrRenderCompressedBuffers = false;
    outHwInfo->capabilityTable.ftrRenderCompressedImages = false;

    drm->checkPreemptionSupport();
    bool preemption = drm->isPreemptionSupported();
    preemption = hwHelper.setupPreemptionRegisters(outHwInfo, preemption);
    PreemptionHelper::adjustDefaultPreemptionMode(outHwInfo->capabilityTable,
                                                  static_cast<bool>(outHwInfo->featureTable.ftrGpGpuMidThreadLevelPreempt) && preemption,
                                                  static_cast<bool>(outHwInfo->featureTable.ftrGpGpuThreadGroupLevelPreempt) && preemption,
                                                  static_cast<bool>(outHwInfo->featureTable.ftrGpGpuMidBatchPreempt) && preemption);
    outHwInfo->capabilityTable.requiredPreemptionSurfaceSize = outHwInfo->gtSystemInfo.CsrSizeInMb * MemoryConstants::megaByte;

    auto &kmdNotifyProperties = outHwInfo->capabilityTable.kmdNotifyProperties;
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideEnableKmdNotify.get(), kmdNotifyProperties.enableKmdNotify);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideKmdNotifyDelayMicroseconds.get(), kmdNotifyProperties.delayKmdNotifyMicroseconds);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideEnableQuickKmdSleep.get(), kmdNotifyProperties.enableQuickKmdSleep);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideQuickKmdSleepDelayMicroseconds.get(), kmdNotifyProperties.delayQuickKmdSleepMicroseconds);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideEnableQuickKmdSleepForSporadicWaits.get(), kmdNotifyProperties.enableQuickKmdSleepForSporadicWaits);
    KmdNotifyHelper::overrideFromDebugVariable(DebugManager.flags.OverrideDelayQuickKmdSleepForSporadicWaitsMicroseconds.get(), kmdNotifyProperties.delayQuickKmdSleepForSporadicWaitsMicroseconds);

    return 0;
}

} // namespace NEO
