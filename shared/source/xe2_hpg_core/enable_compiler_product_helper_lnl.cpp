/*
 * Copyright (C) 2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/helpers/compiler_product_helper.h"
#include "shared/source/helpers/compiler_product_helper_base.inl"
#include "shared/source/helpers/compiler_product_helper_enable_subgroup_local_block_io.inl"
#include "shared/source/helpers/compiler_product_helper_mtl_and_later.inl"
#include "shared/source/helpers/compiler_product_helper_tgllp_and_later.inl"
#include "shared/source/helpers/compiler_product_helper_xe2_hpg_and_later.inl"
#include "shared/source/helpers/compiler_product_helper_xe_hp_and_later.inl"
#include "shared/source/helpers/compiler_product_helper_xe_hpc_and_later.inl"

#include "platforms.h"
#include "wmtp_setup_lnl.inl"

namespace NEO {
template <>
uint32_t CompilerProductHelperHw<IGFX_LUNARLAKE>::getDefaultHwIpVersion() const {
    return AOT::LNL_B0;
}

template <>
bool CompilerProductHelperHw<IGFX_LUNARLAKE>::isMidThreadPreemptionSupported(const HardwareInfo &hwInfo) const {
    return hwInfo.featureTable.flags.ftrWalkerMTP && wmtpSupported;
}

static EnableCompilerProductHelper<IGFX_LUNARLAKE> enableCompilerProductHelperLNL;

} // namespace NEO
