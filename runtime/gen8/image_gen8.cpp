/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/gen8/hw_cmds.h"
#include "runtime/mem_obj/image.h"
#include "runtime/mem_obj/image.inl"

#include <map>

namespace NEO {

typedef BDWFamily Family;
static auto gfxCore = IGFX_GEN8_CORE;

template <typename GfxFamily>
void ImageHw<GfxFamily>::setMediaSurfaceRotation(void *) {}

template <typename GfxFamily>
void ImageHw<GfxFamily>::setSurfaceMemoryObjectControlStateIndexToMocsTable(void *, uint32_t) {}

#include "runtime/mem_obj/image_factory_init.inl"
} // namespace NEO
