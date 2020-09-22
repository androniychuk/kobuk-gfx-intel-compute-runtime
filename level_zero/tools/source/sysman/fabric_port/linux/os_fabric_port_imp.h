/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/non_copyable_or_moveable.h"

#include "sysman/fabric_port/fabric_port_imp.h"
#include "sysman/fabric_port/os_fabric_port.h"
#include "sysman/linux/fs_access.h"

namespace L0 {

class LinuxFabricDeviceImp : public OsFabricDevice, NEO::NonCopyableOrMovableClass {
  public:
    uint32_t getNumPorts() override;

    LinuxFabricDeviceImp() = delete;
    LinuxFabricDeviceImp(OsSysman *pOsSysman);
    ~LinuxFabricDeviceImp() override;

  private:
    uint32_t numPorts = 0;
};

class LinuxFabricPortImp : public OsFabricPort, NEO::NonCopyableOrMovableClass {
  public:
    ze_result_t getLinkType(zes_fabric_link_type_t *pLinkType) override;
    ze_result_t getConfig(zes_fabric_port_config_t *pConfig) override;
    ze_result_t setConfig(const zes_fabric_port_config_t *pConfig) override;
    ze_result_t getState(zes_fabric_port_state_t *pState) override;
    ze_result_t getThroughput(zes_fabric_port_throughput_t *pThroughput) override;
    void getModel(char *model) override;
    void getPortId(zes_fabric_port_id_t &portId) override;
    void getMaxRxSpeed(zes_fabric_port_speed_t &maxRxSpeed) override;
    void getMaxTxSpeed(zes_fabric_port_speed_t &maxTxSpeed) override;

    LinuxFabricPortImp() = delete;
    LinuxFabricPortImp(OsFabricDevice *pOsFabricDevice, uint32_t portNum);
    ~LinuxFabricPortImp() override;

  private:
    uint32_t portNum = 0;
    std::string model = "";
    zes_fabric_port_id_t portId = {};
    zes_fabric_port_speed_t maxRxSpeed = {};
    zes_fabric_port_speed_t maxTxSpeed = {};
    zes_fabric_port_config_t config = {};
};

} // namespace L0
