/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/os_interface/os_memory.h"

#include <windows.h>

namespace NEO {

class OSMemoryWindows : public OSMemory {
  public:
    OSMemoryWindows() = default;

    void getMemoryMaps(MemoryMaps &memoryMaps) override {}

  protected:
    void *osReserveCpuAddressRange(void *baseAddress, size_t sizeToReserve) override;
    void osReleaseCpuAddressRange(void *reservedCpuAddressRange, size_t reservedSize) override;

    MOCKABLE_VIRTUAL LPVOID virtualAllocWrapper(LPVOID, SIZE_T, DWORD, DWORD);
    MOCKABLE_VIRTUAL BOOL virtualFreeWrapper(LPVOID, SIZE_T, DWORD);
};

}; // namespace NEO
