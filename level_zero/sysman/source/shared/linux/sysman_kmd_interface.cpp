/*
 * Copyright (C) 2023-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "level_zero/sysman/source/shared/linux/sysman_kmd_interface.h"

#include "shared/source/debug_settings/debug_settings_manager.h"
#include "shared/source/execution_environment/root_device_environment.h"
#include "shared/source/helpers/hw_info.h"
#include "shared/source/os_interface/linux/drm_neo.h"
#include "shared/source/os_interface/linux/engine_info.h"

#include "level_zero/sysman/source/shared/linux/pmu/sysman_pmu_imp.h"
#include "level_zero/sysman/source/shared/linux/sysman_fs_access_interface.h"

#include "drm/i915_drm.h"
namespace L0 {
namespace Sysman {

const std::string deviceDir("device");
const std::string sysDevicesDir("/sys/devices/");

const std::map<uint16_t, std::string> SysmanKmdInterfaceI915::i915EngineClassToSysfsEngineMap = {
    {drm_i915_gem_engine_class::I915_ENGINE_CLASS_RENDER, "rcs"},
    {static_cast<uint16_t>(drm_i915_gem_engine_class::I915_ENGINE_CLASS_COMPUTE), "ccs"},
    {drm_i915_gem_engine_class::I915_ENGINE_CLASS_COPY, "bcs"},
    {drm_i915_gem_engine_class::I915_ENGINE_CLASS_VIDEO, "vcs"},
    {drm_i915_gem_engine_class::I915_ENGINE_CLASS_VIDEO_ENHANCE, "vecs"}};

static const std::multimap<zes_engine_type_flag_t, std::string> level0EngineTypeToSysfsEngineMap = {
    {ZES_ENGINE_TYPE_FLAG_RENDER, "rcs"},
    {ZES_ENGINE_TYPE_FLAG_COMPUTE, "ccs"},
    {ZES_ENGINE_TYPE_FLAG_DMA, "bcs"},
    {ZES_ENGINE_TYPE_FLAG_MEDIA, "vcs"},
    {ZES_ENGINE_TYPE_FLAG_OTHER, "vecs"}};

SysmanKmdInterface::SysmanKmdInterface() = default;
SysmanKmdInterface::~SysmanKmdInterface() = default;

std::unique_ptr<SysmanKmdInterface> SysmanKmdInterface::create(NEO::Drm &drm) {
    std::unique_ptr<SysmanKmdInterface> pSysmanKmdInterface;
    auto drmVersion = drm.getDrmVersion(drm.getFileDescriptor());
    auto pHwInfo = drm.getRootDeviceEnvironment().getHardwareInfo();
    const auto productFamily = pHwInfo->platform.eProductFamily;
    if ("xe" == drmVersion) {
        pSysmanKmdInterface = std::make_unique<SysmanKmdInterfaceXe>(productFamily);
    } else {
        std::string prelimVersion;
        drm.getPrelimVersion(prelimVersion);
        if (prelimVersion == "") {
            pSysmanKmdInterface = std::make_unique<SysmanKmdInterfaceI915Upstream>(productFamily);
        } else {
            pSysmanKmdInterface = std::make_unique<SysmanKmdInterfaceI915Prelim>(productFamily);
        }
    }

    return pSysmanKmdInterface;
}

ze_result_t SysmanKmdInterface::initFsAccessInterface(const NEO::Drm &drm) {
    pFsAccess = FsAccessInterface::create();
    pProcfsAccess = ProcFsAccessInterface::create();
    std::string deviceName;
    auto result = pProcfsAccess->getFileName(pProcfsAccess->myProcessId(), drm.getFileDescriptor(), deviceName);
    if (result != ZE_RESULT_SUCCESS) {
        NEO::printDebugString(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, "Error@ %s(): Failed to device name and returning error:0x%x \n", __FUNCTION__, result);
        return result;
    }
    pSysfsAccess = SysFsAccessInterface::create(deviceName);
    return result;
}

FsAccessInterface *SysmanKmdInterface::getFsAccess() {
    UNRECOVERABLE_IF(nullptr == pFsAccess.get());
    return pFsAccess.get();
}

ProcFsAccessInterface *SysmanKmdInterface::getProcFsAccess() {
    UNRECOVERABLE_IF(nullptr == pProcfsAccess.get());
    return pProcfsAccess.get();
}

SysFsAccessInterface *SysmanKmdInterface::getSysFsAccess() {
    UNRECOVERABLE_IF(nullptr == pSysfsAccess.get());
    return pSysfsAccess.get();
}

ze_result_t SysmanKmdInterface::getNumEngineTypeAndInstancesForDevice(std::string engineDir, std::map<zes_engine_type_flag_t, std::vector<std::string>> &mapOfEngines,
                                                                      SysFsAccessInterface *pSysfsAccess) {
    std::vector<std::string> localListOfAllEngines = {};
    auto result = pSysfsAccess->scanDirEntries(engineDir, localListOfAllEngines);
    if (ZE_RESULT_SUCCESS != result) {
        if (result == ZE_RESULT_ERROR_NOT_AVAILABLE) {
            result = ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
        }
        NEO::printDebugString(NEO::debugManager.flags.PrintDebugMessages.get(), stderr, "Error@ %s(): Failed to scan directory entries to list all engines and returning error:0x%x \n", __FUNCTION__, result);
        return result;
    }
    for_each(localListOfAllEngines.begin(), localListOfAllEngines.end(),
             [&](std::string &mappedEngine) {
                 for (auto itr = level0EngineTypeToSysfsEngineMap.begin(); itr != level0EngineTypeToSysfsEngineMap.end(); itr++) {
                     char digits[] = "0123456789";
                     auto mappedEngineName = mappedEngine.substr(0, mappedEngine.find_first_of(digits, 0));
                     if (0 == mappedEngineName.compare(itr->second.c_str())) {
                         auto ret = mapOfEngines.find(itr->first);
                         if (ret != mapOfEngines.end()) {
                             ret->second.push_back(mappedEngine);
                         } else {
                             std::vector<std::string> engineVec = {};
                             engineVec.push_back(mappedEngine);
                             mapOfEngines.emplace(itr->first, engineVec);
                         }
                     }
                 }
             });
    return result;
}

SysmanKmdInterface::SysfsValueUnit SysmanKmdInterface::getNativeUnit(const SysfsName sysfsName) {
    auto sysfsNameToNativeUnitMap = getSysfsNameToNativeUnitMap();
    if (sysfsNameToNativeUnitMap.find(sysfsName) != sysfsNameToNativeUnitMap.end()) {
        return sysfsNameToNativeUnitMap[sysfsName];
    }
    // Entries are expected to be available at sysfsNameToNativeUnitMap
    DEBUG_BREAK_IF(true);
    return unAvailable;
}

void SysmanKmdInterface::convertSysfsValueUnit(const SysfsValueUnit dstUnit, const SysfsValueUnit srcUnit, const uint64_t srcValue, uint64_t &dstValue) const {
    dstValue = srcValue;

    if (dstUnit != srcUnit) {
        if (dstUnit == SysfsValueUnit::milli && srcUnit == SysfsValueUnit::micro) {
            dstValue = srcValue / 1000u;
        } else if (dstUnit == SysfsValueUnit::micro && srcUnit == SysfsValueUnit::milli) {
            dstValue = srcValue * 1000u;
        }
    }
}

uint32_t SysmanKmdInterface::getEventTypeImpl(std::string &dirName, const bool isIntegratedDevice) {
    auto pSysFsAccess = getSysFsAccess();
    auto pFsAccess = getFsAccess();

    if (!isIntegratedDevice) {
        std::string bdfDir;
        ze_result_t result = pSysFsAccess->readSymLink(deviceDir, bdfDir);
        if (ZE_RESULT_SUCCESS != result) {
            return 0;
        }
        const auto loc = bdfDir.find_last_of('/');
        auto bdf = bdfDir.substr(loc + 1);
        std::replace(bdf.begin(), bdf.end(), ':', '_');
        dirName = dirName + "_" + bdf;
    }

    const std::string eventTypeSysfsNode = sysDevicesDir + dirName + "/" + "type";
    auto eventTypeVal = 0u;
    if (ZE_RESULT_SUCCESS != pFsAccess->read(eventTypeSysfsNode, eventTypeVal)) {
        return 0;
    }
    return eventTypeVal;
}

std::string SysmanKmdInterfaceI915::getBasePathI915(uint32_t subDeviceId) {
    return "gt/gt" + std::to_string(subDeviceId) + "/";
}

std::string SysmanKmdInterfaceI915::getHwmonNameI915(uint32_t subDeviceId, bool isSubdevice) {
    std::string filePath = isSubdevice ? "i915_gt" + std::to_string(subDeviceId) : "i915";
    return filePath;
}

std::string SysmanKmdInterfaceI915::getEngineBasePathI915(uint32_t subDeviceId) {
    return "engine";
}

std::optional<std::string> SysmanKmdInterfaceI915::getEngineClassStringI915(uint16_t engineClass) {
    auto sysfEngineString = i915EngineClassToSysfsEngineMap.find(static_cast<drm_i915_gem_engine_class>(engineClass));
    if (sysfEngineString == i915EngineClassToSysfsEngineMap.end()) {
        DEBUG_BREAK_IF(true);
        return {};
    }
    return sysfEngineString->second;
}

} // namespace Sysman
} // namespace L0
