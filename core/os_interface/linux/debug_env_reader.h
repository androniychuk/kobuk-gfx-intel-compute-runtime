/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "core/utilities/debug_settings_reader.h"
#include "runtime/os_interface/debug_settings_manager.h"

namespace NEO {

class EnvironmentVariableReader : public SettingsReader {
  public:
    int32_t getSetting(const char *settingName, int32_t defaultValue) override;
    bool getSetting(const char *settingName, bool defaultValue) override;
    std::string getSetting(const char *settingName, const std::string &value) override;
    const char *appSpecificLocation(const std::string &name) override;
};
} // namespace NEO
