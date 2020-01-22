/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "runtime/os_interface/os_interface.h"

namespace NEO {
class Drm;

class OSInterface::OSInterfaceImpl {
  public:
    OSInterfaceImpl() {
        drm = nullptr;
    }
    Drm *getDrm() const {
        return drm;
    }
    void setDrm(Drm *drm) {
        this->drm = drm;
    }

  protected:
    Drm *drm;
};
} // namespace NEO
