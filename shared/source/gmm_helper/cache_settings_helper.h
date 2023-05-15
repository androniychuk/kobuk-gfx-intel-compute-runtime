/*
 * Copyright (C) 2022-2023 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/gmm_helper/gmm_lib.h"

namespace CacheSettings {
inline constexpr uint32_t unknownMocs = GMM_RESOURCE_USAGE_UNKNOWN;
} // namespace CacheSettings

namespace NEO {
enum class AllocationType;
struct HardwareInfo;
class ProductHelper;

struct CacheSettingsHelper {
    static GMM_RESOURCE_USAGE_TYPE_ENUM getGmmUsageType(AllocationType allocationType, bool forceUncached, const ProductHelper &productHelper);

    static constexpr bool isUncachedType(GMM_RESOURCE_USAGE_TYPE_ENUM gmmResourceUsageType) {
        return ((gmmResourceUsageType == GMM_RESOURCE_USAGE_OCL_BUFFER_CSR_UC) ||
                (gmmResourceUsageType == GMM_RESOURCE_USAGE_OCL_SYSTEM_MEMORY_BUFFER_CACHELINE_MISALIGNED) ||
                (gmmResourceUsageType == GMM_RESOURCE_USAGE_OCL_BUFFER_CACHELINE_MISALIGNED));
    }

    static bool isResourceCacheableOnCpu(GMM_RESOURCE_USAGE_TYPE_ENUM gmmResourceUsageType);

  protected:
    static GMM_RESOURCE_USAGE_TYPE_ENUM getDefaultUsageTypeWithCachingEnabled(AllocationType allocationType, const ProductHelper &productHelper);
    static GMM_RESOURCE_USAGE_TYPE_ENUM getDefaultUsageTypeWithCachingDisabled(AllocationType allocationType);
};
} // namespace NEO