/*
 * Copyright (C) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "runtime/utilities/idlist.h"
#include <cstdint>

namespace OCLRT {
class GraphicsAllocation;
class CommandStreamReceiver;

class AllocationsList : public IDList<GraphicsAllocation, true, true> {
  public:
    std::unique_ptr<GraphicsAllocation> detachAllocation(size_t requiredMinimalSize, CommandStreamReceiver &commandStreamReceiver, bool internalAllocationRequired);

  private:
    GraphicsAllocation *detachAllocationImpl(GraphicsAllocation *, void *);
};
} // namespace OCLRT
