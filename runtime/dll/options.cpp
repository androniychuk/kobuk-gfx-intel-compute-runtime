/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/helpers/options.h"

#include "runtime/gen_common/hw_cmds.h"
#include "runtime/helpers/array_count.h"

namespace NEO {
// AUB file folder location
const char *folderAUB = ".";

// Initial value for HW tag
uint32_t initialHardwareTag = 0;

// Number of devices in the platform
static const HardwareInfo *DefaultPlatformDevices[] = {
    &DEFAULT_PLATFORM::hwInfo,
};

size_t numPlatformDevices = arrayCount(DefaultPlatformDevices);
const HardwareInfo **platformDevices = DefaultPlatformDevices;
} // namespace NEO
