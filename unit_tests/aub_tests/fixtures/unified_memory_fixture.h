/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include "core/memory_manager/memory_constants.h"
#include "runtime/api/api.h"
#include "unit_tests/aub_tests/fixtures/aub_fixture.h"

namespace NEO {
class UnifiedMemoryAubFixture : public AUBFixture {
  public:
    using AUBFixture::TearDown;

    cl_int retVal = CL_SUCCESS;
    const size_t dataSize = MemoryConstants::megaByte;
    bool skipped = false;

    void SetUp() override {
        AUBFixture::SetUp(nullptr);
        if (!platformImpl->peekExecutionEnvironment()->memoryManager->getPageFaultManager()) {
            skipped = true;
            GTEST_SKIP();
        }
    }

    void *allocateUSM(InternalMemoryType type) {
        void *ptr = nullptr;
        if (!this->skipped) {
            switch (type) {
            case DEVICE_UNIFIED_MEMORY:
                ptr = clDeviceMemAllocINTEL(this->context, this->device.get(), nullptr, dataSize, 0, &retVal);
                break;
            case HOST_UNIFIED_MEMORY:
                ptr = clHostMemAllocINTEL(this->context, nullptr, dataSize, 0, &retVal);
                break;
            case SHARED_UNIFIED_MEMORY:
                ptr = clSharedMemAllocINTEL(this->context, this->device.get(), nullptr, dataSize, 0, &retVal);
                break;
            default:
                ptr = new char[dataSize];
                break;
            }
            EXPECT_EQ(retVal, CL_SUCCESS);
            EXPECT_NE(ptr, nullptr);
        }
        return ptr;
    }

    void freeUSM(void *ptr, InternalMemoryType type) {
        if (!this->skipped) {
            switch (type) {
            case DEVICE_UNIFIED_MEMORY:
            case HOST_UNIFIED_MEMORY:
            case SHARED_UNIFIED_MEMORY:
                retVal = clMemFreeINTEL(this->context, ptr);
                break;
            default:
                delete[] static_cast<char *>(ptr);
                break;
            }
            EXPECT_EQ(retVal, CL_SUCCESS);
        }
    }

    void writeToUsmMemory(std::vector<char> data, void *ptr, InternalMemoryType type) {
        if (!this->skipped) {
            switch (type) {
            case DEVICE_UNIFIED_MEMORY:
                retVal = clEnqueueMemcpyINTEL(this->pCmdQ, true, ptr, data.data(), dataSize, 0, nullptr, nullptr);
                break;
            default:
                std::copy(data.begin(), data.end(), static_cast<char *>(ptr));
                break;
            }
            EXPECT_EQ(retVal, CL_SUCCESS);
        }
    }
};
} // namespace NEO
