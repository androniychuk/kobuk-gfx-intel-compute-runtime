/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "shared/source/helpers/non_copyable_or_moveable.h"

#include "level_zero/tools/source/sysman/ras/os_ras.h"
#include "level_zero/tools/source/sysman/ras/ras.h"

namespace L0 {

class RasImp : public Ras, NEO::NonCopyableOrMovableClass {
  public:
    ze_result_t rasGetProperties(zes_ras_properties_t *pProperties) override;
    ze_result_t rasGetConfig(zes_ras_config_t *pConfig) override;
    ze_result_t rasSetConfig(const zes_ras_config_t *pConfig) override;
    ze_result_t rasGetState(const zes_ras_state_t *pConfig) override;

    RasImp() = default;
    RasImp(OsSysman *pOsSysman, zes_ras_error_type_t type);
    ~RasImp() override;

    OsRas *pOsRas = nullptr;
    void init();

  private:
    zes_ras_properties_t rasProperties = {};
};

} // namespace L0
