/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/mem_obj/buffer_bdw_plus.inl"

#include "hw_cmds.h"

namespace NEO {

typedef ICLFamily Family;
static auto gfxCore = IGFX_GEN11_CORE;

#include "runtime/mem_obj/buffer_factory_init.inl"
} // namespace NEO
