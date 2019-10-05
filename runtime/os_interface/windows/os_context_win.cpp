/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/os_interface/windows/os_context_win.h"

#include "runtime/os_interface/windows/os_interface.h"
#include "runtime/os_interface/windows/wddm/wddm.h"
#include "runtime/os_interface/windows/wddm/wddm_interface.h"

namespace NEO {

OsContext *OsContext::create(OSInterface *osInterface, uint32_t contextId, DeviceBitfield deviceBitfield,
                             aub_stream::EngineType engineType, PreemptionMode preemptionMode, bool lowPriority) {
    if (osInterface) {
        return new OsContextWin(*osInterface->get()->getWddm(), contextId, deviceBitfield, engineType, preemptionMode, lowPriority);
    }
    return new OsContext(contextId, deviceBitfield, engineType, preemptionMode, lowPriority);
}

OsContextWin::OsContextWin(Wddm &wddm, uint32_t contextId, DeviceBitfield deviceBitfield,
                           aub_stream::EngineType engineType, PreemptionMode preemptionMode, bool lowPriority)
    : OsContext(contextId, deviceBitfield, engineType, preemptionMode, lowPriority), wddm(wddm), residencyController(wddm, contextId) {

    auto wddmInterface = wddm.getWddmInterface();
    if (!wddm.createContext(*this)) {
        return;
    }
    if (wddmInterface->hwQueuesSupported()) {
        if (!wddmInterface->createHwQueue(*this)) {
            return;
        }
    }
    initialized = wddmInterface->createMonitoredFence(*this);
    residencyController.registerCallback();
};

OsContextWin::~OsContextWin() {
    wddm.getWddmInterface()->destroyHwQueue(hardwareQueue.handle);
    wddm.destroyContext(wddmContextHandle);
}

} // namespace NEO
