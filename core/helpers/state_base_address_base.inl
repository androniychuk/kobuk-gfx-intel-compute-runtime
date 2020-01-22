/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/gmm_helper/gmm_helper.h"
#include "core/helpers/cache_policy.h"
#include "core/helpers/hw_cmds.h"
#include "core/helpers/state_base_address.h"
#include "core/indirect_heap/indirect_heap.h"
#include "core/memory_manager/memory_constants.h"

namespace NEO {
template <typename GfxFamily>
void StateBaseAddressHelper<GfxFamily>::programStateBaseAddress(
    LinearStream &commandStream,
    const IndirectHeap *dsh,
    const IndirectHeap *ioh,
    const IndirectHeap *ssh,
    uint64_t generalStateBase,
    uint32_t statelessMocsIndex,
    uint64_t internalHeapBase,
    GmmHelper *gmmHelper,
    bool isMultiOsContextCapable) {

    auto pCmd = static_cast<STATE_BASE_ADDRESS *>(commandStream.getSpace(sizeof(STATE_BASE_ADDRESS)));
    *pCmd = GfxFamily::cmdInitStateBaseAddress;

    if (dsh) {
        pCmd->setDynamicStateBaseAddressModifyEnable(true);
        pCmd->setDynamicStateBufferSizeModifyEnable(true);
        pCmd->setDynamicStateBaseAddress(dsh->getHeapGpuBase());
        pCmd->setDynamicStateBufferSize(dsh->getHeapSizeInPages());
    }

    if (ioh) {
        pCmd->setIndirectObjectBaseAddressModifyEnable(true);
        pCmd->setIndirectObjectBufferSizeModifyEnable(true);
        pCmd->setIndirectObjectBaseAddress(ioh->getHeapGpuBase());
        pCmd->setIndirectObjectBufferSize(ioh->getHeapSizeInPages());
    }

    if (ssh) {
        pCmd->setSurfaceStateBaseAddressModifyEnable(true);
        pCmd->setSurfaceStateBaseAddress(ssh->getHeapGpuBase());
    }

    pCmd->setInstructionBaseAddressModifyEnable(true);
    pCmd->setInstructionBaseAddress(internalHeapBase);
    pCmd->setInstructionBufferSizeModifyEnable(true);
    pCmd->setInstructionBufferSize(MemoryConstants::sizeOf4GBinPageEntities);

    pCmd->setGeneralStateBaseAddressModifyEnable(true);
    pCmd->setGeneralStateBufferSizeModifyEnable(true);
    // GSH must be set to 0 for stateless
    pCmd->setGeneralStateBaseAddress(GmmHelper::decanonize(generalStateBase));
    pCmd->setGeneralStateBufferSize(0xfffff);

    if (DebugManager.flags.OverrideStatelessMocsIndex.get() != -1) {
        statelessMocsIndex = DebugManager.flags.OverrideStatelessMocsIndex.get();
    }

    statelessMocsIndex = statelessMocsIndex << 1;

    pCmd->setStatelessDataPortAccessMemoryObjectControlState(statelessMocsIndex);
    pCmd->setInstructionMemoryObjectControlState(gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_STATE_HEAP_BUFFER));

    appendStateBaseAddressParameters(pCmd, ssh, internalHeapBase, gmmHelper, isMultiOsContextCapable);
}

} // namespace NEO
