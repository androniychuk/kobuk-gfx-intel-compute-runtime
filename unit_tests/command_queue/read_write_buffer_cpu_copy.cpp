/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/helpers/basic_math.h"
#include "runtime/gmm_helper/gmm.h"
#include "test.h"
#include "unit_tests/command_queue/enqueue_read_buffer_fixture.h"

using namespace NEO;

typedef EnqueueReadBufferTypeTest ReadWriteBufferCpuCopyTest;

HWTEST_F(ReadWriteBufferCpuCopyTest, givenRenderCompressedGmmWhenAskingForCpuOperationThenDisallow) {
    cl_int retVal;
    std::unique_ptr<Buffer> buffer(Buffer::create(context, CL_MEM_READ_WRITE, 1, nullptr, retVal));
    auto gmm = new Gmm(pDevice->getExecutionEnvironment()->getGmmClientContext(), nullptr, 1, false);
    gmm->isRenderCompressed = false;
    buffer->getGraphicsAllocation()->setDefaultGmm(gmm);

    auto alignedPtr = alignedMalloc(2, MemoryConstants::cacheLineSize);
    auto unalignedPtr = ptrOffset(alignedPtr, 1);
    EXPECT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, unalignedPtr, 1));

    gmm->isRenderCompressed = true;
    EXPECT_FALSE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, unalignedPtr, 1));

    alignedFree(alignedPtr);
}

HWTEST_F(ReadWriteBufferCpuCopyTest, simpleRead) {
    cl_int retVal;
    size_t offset = 1;
    size_t size = 4;

    auto alignedReadPtr = alignedMalloc(size + 1, MemoryConstants::cacheLineSize);
    memset(alignedReadPtr, 0x00, size + 1);
    auto unalignedReadPtr = ptrOffset(alignedReadPtr, 1);

    std::unique_ptr<uint8_t[]> bufferPtr(new uint8_t[size]);
    for (uint8_t i = 0; i < size; i++) {
        bufferPtr[i] = i + 1;
    }
    std::unique_ptr<Buffer> buffer(Buffer::create(context, CL_MEM_USE_HOST_PTR, size, bufferPtr.get(), retVal));
    EXPECT_EQ(retVal, CL_SUCCESS);

    bool aligned = (reinterpret_cast<uintptr_t>(unalignedReadPtr) & (MemoryConstants::cacheLineSize - 1)) == 0;
    EXPECT_TRUE(!aligned || buffer->isMemObjZeroCopy());
    ASSERT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, unalignedReadPtr, size));

    retVal = EnqueueReadBufferHelper<>::enqueueReadBuffer(pCmdQ,
                                                          buffer.get(),
                                                          CL_TRUE,
                                                          offset,
                                                          size - offset,
                                                          unalignedReadPtr,
                                                          nullptr,
                                                          0,
                                                          nullptr,
                                                          nullptr);
    EXPECT_EQ(retVal, CL_SUCCESS);

    auto pBufferPtr = ptrOffset(bufferPtr.get(), offset);
    EXPECT_EQ(memcmp(unalignedReadPtr, pBufferPtr, size - offset), 0);

    alignedFree(alignedReadPtr);
}

HWTEST_F(ReadWriteBufferCpuCopyTest, simpleWrite) {
    cl_int retVal;
    size_t offset = 1;
    size_t size = 4;

    auto alignedWritePtr = alignedMalloc(size + 1, MemoryConstants::cacheLineSize);
    auto unalignedWritePtr = static_cast<uint8_t *>(ptrOffset(alignedWritePtr, 1));
    auto bufferPtrBase = new uint8_t[size];
    auto bufferPtr = new uint8_t[size];
    for (uint8_t i = 0; i < size; i++) {
        unalignedWritePtr[i] = i + 5;
        bufferPtrBase[i] = i + 1;
        bufferPtr[i] = i + 1;
    }

    std::unique_ptr<Buffer> buffer(Buffer::create(context, CL_MEM_USE_HOST_PTR, size, bufferPtr, retVal));
    EXPECT_EQ(retVal, CL_SUCCESS);

    bool aligned = (reinterpret_cast<uintptr_t>(unalignedWritePtr) & (MemoryConstants::cacheLineSize - 1)) == 0;
    EXPECT_TRUE(!aligned || buffer->isMemObjZeroCopy());
    ASSERT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, unalignedWritePtr, size));

    retVal = EnqueueWriteBufferHelper<>::enqueueWriteBuffer(pCmdQ,
                                                            buffer.get(),
                                                            CL_TRUE,
                                                            offset,
                                                            size - offset,
                                                            unalignedWritePtr,
                                                            nullptr,
                                                            0,
                                                            nullptr,
                                                            nullptr);
    EXPECT_EQ(retVal, CL_SUCCESS);

    auto pBufferPtr = buffer->getCpuAddress();

    EXPECT_EQ(memcmp(pBufferPtr, bufferPtrBase, offset), 0); // untouched
    pBufferPtr = ptrOffset(pBufferPtr, offset);
    EXPECT_EQ(memcmp(pBufferPtr, unalignedWritePtr, size - offset), 0); // updated

    alignedFree(alignedWritePtr);
    delete[] bufferPtr;
    delete[] bufferPtrBase;
}

HWTEST_F(ReadWriteBufferCpuCopyTest, cpuCopyCriteriaMet) {
    cl_int retVal;
    size_t size = MemoryConstants::cacheLineSize;
    auto alignedBufferPtr = alignedMalloc(MemoryConstants::cacheLineSize + 1, MemoryConstants::cacheLineSize);
    auto unalignedBufferPtr = ptrOffset(alignedBufferPtr, 1);
    auto alignedHostPtr = alignedMalloc(MemoryConstants::cacheLineSize + 1, MemoryConstants::cacheLineSize);
    auto unalignedHostPtr = ptrOffset(alignedHostPtr, 1);
    auto smallBufferPtr = alignedMalloc(1 * MB, MemoryConstants::cacheLineSize);
    size_t largeBufferSize = 11u * MemoryConstants::megaByte;

    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    auto mockContext = std::unique_ptr<MockContext>(new MockContext(mockDevice.get()));
    auto memoryManager = static_cast<OsAgnosticMemoryManager *>(mockDevice->getMemoryManager());
    memoryManager->turnOnFakingBigAllocations();

    std::unique_ptr<Buffer> buffer(Buffer::create(context, CL_MEM_USE_HOST_PTR, size, alignedBufferPtr, retVal));
    EXPECT_EQ(retVal, CL_SUCCESS);
    EXPECT_TRUE(buffer->isMemObjZeroCopy());

    // zeroCopy == true && aligned/unaligned hostPtr
    EXPECT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, alignedHostPtr, MemoryConstants::cacheLineSize + 1));
    EXPECT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, unalignedHostPtr, MemoryConstants::cacheLineSize));

    buffer.reset(Buffer::create(context, CL_MEM_USE_HOST_PTR, size, unalignedBufferPtr, retVal));

    EXPECT_EQ(retVal, CL_SUCCESS);

    // zeroCopy == false && unaligned hostPtr
    EXPECT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, unalignedHostPtr, MemoryConstants::cacheLineSize));

    buffer.reset(Buffer::create(mockContext.get(), CL_MEM_USE_HOST_PTR, 1 * MB, smallBufferPtr, retVal));

    // platform LP == true && size <= 10 MB
    mockDevice->deviceInfo.platformLP = true;
    EXPECT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, smallBufferPtr, 1 * MB));

    // platform LP == false && size <= 10 MB
    mockDevice->deviceInfo.platformLP = false;
    EXPECT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, smallBufferPtr, 1 * MB));

    buffer.reset(Buffer::create(mockContext.get(), CL_MEM_ALLOC_HOST_PTR, largeBufferSize, nullptr, retVal));

    // platform LP == false && size > 10 MB
    mockDevice->deviceInfo.platformLP = false;
    EXPECT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, buffer->getCpuAddress(), largeBufferSize));

    alignedFree(smallBufferPtr);
    alignedFree(alignedHostPtr);
    alignedFree(alignedBufferPtr);
}

HWTEST_F(ReadWriteBufferCpuCopyTest, cpuCopyCriteriaNotMet) {
    cl_int retVal;
    size_t size = MemoryConstants::cacheLineSize;
    auto alignedBufferPtr = alignedMalloc(MemoryConstants::cacheLineSize + 1, MemoryConstants::cacheLineSize);
    auto unalignedBufferPtr = ptrOffset(alignedBufferPtr, 1);
    auto alignedHostPtr = alignedMalloc(MemoryConstants::cacheLineSize + 1, MemoryConstants::cacheLineSize);
    auto unalignedHostPtr = ptrOffset(alignedHostPtr, 1);
    size_t largeBufferSize = 11u * MemoryConstants::megaByte;

    auto mockDevice = std::unique_ptr<MockDevice>(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    auto mockContext = std::unique_ptr<MockContext>(new MockContext(mockDevice.get()));
    auto memoryManager = static_cast<OsAgnosticMemoryManager *>(mockDevice->getMemoryManager());
    memoryManager->turnOnFakingBigAllocations();

    std::unique_ptr<Buffer> buffer(Buffer::create(context, CL_MEM_USE_HOST_PTR, size, alignedBufferPtr, retVal));
    EXPECT_EQ(retVal, CL_SUCCESS);
    EXPECT_TRUE(buffer->isMemObjZeroCopy());

    // non blocking
    EXPECT_FALSE(buffer->isReadWriteOnCpuAllowed(CL_FALSE, 0, unalignedHostPtr, MemoryConstants::cacheLineSize));
    // numEventWaitlist > 0
    EXPECT_FALSE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 1, unalignedHostPtr, MemoryConstants::cacheLineSize));

    buffer.reset(Buffer::create(context, CL_MEM_USE_HOST_PTR, size, unalignedBufferPtr, retVal));

    EXPECT_EQ(retVal, CL_SUCCESS);

    // zeroCopy == false && aligned hostPtr
    EXPECT_FALSE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, alignedHostPtr, MemoryConstants::cacheLineSize + 1));

    buffer.reset(Buffer::create(mockContext.get(), CL_MEM_ALLOC_HOST_PTR, largeBufferSize, nullptr, retVal));

    // platform LP == true && size > 10 MB
    mockDevice->deviceInfo.platformLP = true;
    EXPECT_FALSE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, buffer->getCpuAddress(), largeBufferSize));

    alignedFree(alignedHostPtr);
    alignedFree(alignedBufferPtr);
}

TEST(ReadWriteBufferOnCpu, givenNoHostPtrAndAlignedSizeWhenMemoryAllocationIsInNonSystemMemoryPoolThenIsReadWriteOnCpuAllowedReturnsFalse) {
    std::unique_ptr<MockDevice> device(MockDevice::createWithNewExecutionEnvironment<MockDevice>(nullptr));
    auto memoryManager = new MockMemoryManager(*device->getExecutionEnvironment());

    device->injectMemoryManager(memoryManager);
    MockContext ctx(device.get());

    cl_int retVal = 0;
    cl_mem_flags flags = CL_MEM_READ_WRITE;

    std::unique_ptr<Buffer> buffer(Buffer::create(&ctx, flags, MemoryConstants::pageSize, nullptr, retVal));
    ASSERT_NE(nullptr, buffer.get());

    EXPECT_TRUE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, reinterpret_cast<void *>(0x1000), MemoryConstants::pageSize));
    reinterpret_cast<MemoryAllocation *>(buffer->getGraphicsAllocation())->overrideMemoryPool(MemoryPool::SystemCpuInaccessible);
    EXPECT_FALSE(buffer->isReadWriteOnCpuAllowed(CL_TRUE, 0, reinterpret_cast<void *>(0x1000), MemoryConstants::pageSize));
}
