/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "core/helpers/hw_info.h"
#include "core/helpers/non_copyable_or_moveable.h"
#include "core/memory_manager/graphics_allocation.h"
#include "runtime/gen_common/aub_mapper_base.h"

namespace NEO {

class AubHelper : public NonCopyableOrMovableClass {
  public:
    static bool isOneTimeAubWritableAllocationType(const GraphicsAllocation::AllocationType &type) {
        switch (type) {
        case GraphicsAllocation::AllocationType::PIPE:
        case GraphicsAllocation::AllocationType::CONSTANT_SURFACE:
        case GraphicsAllocation::AllocationType::GLOBAL_SURFACE:
        case GraphicsAllocation::AllocationType::KERNEL_ISA:
        case GraphicsAllocation::AllocationType::PRIVATE_SURFACE:
        case GraphicsAllocation::AllocationType::SCRATCH_SURFACE:
        case GraphicsAllocation::AllocationType::BUFFER:
        case GraphicsAllocation::AllocationType::BUFFER_HOST_MEMORY:
        case GraphicsAllocation::AllocationType::BUFFER_COMPRESSED:
        case GraphicsAllocation::AllocationType::IMAGE:
        case GraphicsAllocation::AllocationType::TIMESTAMP_PACKET_TAG_BUFFER:
        case GraphicsAllocation::AllocationType::EXTERNAL_HOST_PTR:
            return true;
        default:
            return false;
        }
    }

    static uint64_t getTotalMemBankSize();
    static int getMemTrace(uint64_t pdEntryBits);
    static uint64_t getPTEntryBits(uint64_t pdEntryBits);
    static uint32_t getMemType(uint32_t addressSpace);
    static uint64_t getMemBankSize(const HardwareInfo *pHwInfo);
    static MMIOList getAdditionalMmioList();

    virtual int getDataHintForPml4Entry() const = 0;
    virtual int getDataHintForPdpEntry() const = 0;
    virtual int getDataHintForPdEntry() const = 0;
    virtual int getDataHintForPtEntry() const = 0;

    virtual int getMemTraceForPml4Entry() const = 0;
    virtual int getMemTraceForPdpEntry() const = 0;
    virtual int getMemTraceForPdEntry() const = 0;
    virtual int getMemTraceForPtEntry() const = 0;

  protected:
    static MMIOList splitMMIORegisters(const std::string &registers, char delimiter);
};

template <typename GfxFamily>
class AubHelperHw : public AubHelper {
  public:
    AubHelperHw(bool localMemoryEnabled) : localMemoryEnabled(localMemoryEnabled){};

    int getDataHintForPml4Entry() const override;
    int getDataHintForPdpEntry() const override;
    int getDataHintForPdEntry() const override;
    int getDataHintForPtEntry() const override;

    int getMemTraceForPml4Entry() const override;
    int getMemTraceForPdpEntry() const override;
    int getMemTraceForPdEntry() const override;
    int getMemTraceForPtEntry() const override;

  protected:
    bool localMemoryEnabled;
};

} // namespace NEO
