/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/gen12lp/hw_cmds.h"
#include "runtime/helpers/gmm_callbacks_tgllp_plus.inl"

using namespace NEO;

template struct DeviceCallbacks<TGLLPFamily>;
template struct TTCallbacks<TGLLPFamily>;
