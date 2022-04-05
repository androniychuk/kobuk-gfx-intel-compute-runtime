/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/windows/gmm_callbacks_tgllp_and_later.inl"
#include "shared/source/xe_hpg_core/hw_cmds.h"

namespace NEO {

template struct DeviceCallbacks<XE_HPG_COREFamily>;
template struct TTCallbacks<XE_HPG_COREFamily>;

} // namespace NEO
