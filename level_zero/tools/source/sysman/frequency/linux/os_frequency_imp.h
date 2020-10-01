/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/helpers/non_copyable_or_moveable.h"

#include "sysman/frequency/frequency_imp.h"
#include "sysman/frequency/os_frequency.h"
#include "sysman/linux/fs_access.h"

namespace L0 {

class LinuxFrequencyImp : public OsFrequency, NEO::NonCopyableOrMovableClass {
  public:
    ze_result_t osFrequencyGetProperties(zes_freq_properties_t &properties) override;
    ze_result_t osFrequencyGetRange(zes_freq_range_t *pLimits) override;
    ze_result_t osFrequencySetRange(const zes_freq_range_t *pLimits) override;
    ze_result_t osFrequencyGetState(zes_freq_state_t *pState) override;
    ze_result_t osFrequencyGetThrottleTime(zes_freq_throttle_time_t *pThrottleTime) override;
    LinuxFrequencyImp() = default;
    LinuxFrequencyImp(OsSysman *pOsSysman, ze_bool_t onSubdevice, uint32_t subdeviceId);
    ~LinuxFrequencyImp() override = default;

  protected:
    SysfsAccess *pSysfsAccess = nullptr;
    ze_result_t getMin(double &min);
    ze_result_t setMin(double min);
    ze_result_t getMax(double &max);
    ze_result_t setMax(double max);
    ze_result_t getRequest(double &request);
    ze_result_t getTdp(double &tdp);
    ze_result_t getActual(double &actual);
    ze_result_t getEfficient(double &efficient);
    ze_result_t getMaxVal(double &maxVal);
    ze_result_t getMinVal(double &minVal);

  private:
    std::string minFreqFile;
    std::string maxFreqFile;
    std::string requestFreqFile;
    std::string tdpFreqFile;
    std::string actualFreqFile;
    std::string efficientFreqFile;
    std::string maxValFreqFile;
    std::string minValFreqFile;
    static const bool canControl;
    bool isSubdevice = false;
    uint32_t subdeviceId = 0;
    void init();
};

} // namespace L0
