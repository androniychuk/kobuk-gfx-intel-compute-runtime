/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/helpers/ptr_math.h"
#include "core/memory_manager/unified_memory_manager.h"
#include "core/unit_tests/helpers/debug_manager_state_restore.h"
#include "runtime/gmm_helper/gmm.h"
#include "runtime/gmm_helper/gmm_helper.h"
#include "runtime/kernel/kernel.h"
#include "runtime/memory_manager/surface.h"
#include "test.h"
#include "unit_tests/fixtures/buffer_fixture.h"
#include "unit_tests/fixtures/context_fixture.h"
#include "unit_tests/fixtures/device_fixture.h"
#include "unit_tests/mocks/mock_kernel.h"
#include "unit_tests/mocks/mock_program.h"

#include "gtest/gtest.h"

using namespace NEO;

class BufferSetArgTest : public ContextFixture,
                         public DeviceFixture,
                         public testing::Test {

    using ContextFixture::SetUp;

  public:
    BufferSetArgTest()

    {
    }

  protected:
    void SetUp() override {
        DeviceFixture::SetUp();
        cl_device_id device = pDevice;
        ContextFixture::SetUp(1, &device);
        pKernelInfo = std::make_unique<KernelInfo>();
        ASSERT_NE(nullptr, pKernelInfo);

        // define kernel info
        // setup kernel arg offsets
        KernelArgPatchInfo kernelArgPatchInfo;

        pKernelInfo->kernelArgInfo.resize(3);
        pKernelInfo->kernelArgInfo[2].kernelArgPatchInfoVector.push_back(kernelArgPatchInfo);
        pKernelInfo->kernelArgInfo[1].kernelArgPatchInfoVector.push_back(kernelArgPatchInfo);
        pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector.push_back(kernelArgPatchInfo);

        uint32_t sizeOfPointer = sizeof(void *);

        pKernelInfo->kernelArgInfo[2].kernelArgPatchInfoVector[0].crossthreadOffset = 0x10;
        pKernelInfo->kernelArgInfo[1].kernelArgPatchInfoVector[0].crossthreadOffset = 0x20;
        pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset = 0x30;

        pKernelInfo->kernelArgInfo[2].kernelArgPatchInfoVector[0].size = sizeOfPointer;
        pKernelInfo->kernelArgInfo[1].kernelArgPatchInfoVector[0].size = sizeOfPointer;
        pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].size = sizeOfPointer;

        kernelHeader.SurfaceStateHeapSize = sizeof(surfaceStateHeap);
        pKernelInfo->heapInfo.pSsh = surfaceStateHeap;
        pKernelInfo->heapInfo.pKernelHeader = &kernelHeader;
        pKernelInfo->usesSsh = true;

        pProgram = new MockProgram(*pDevice->getExecutionEnvironment(), pContext, false);

        pKernel = new MockKernel(pProgram, *pKernelInfo, *pDevice);
        ASSERT_NE(nullptr, pKernel);
        ASSERT_EQ(CL_SUCCESS, pKernel->initialize());
        pKernel->setCrossThreadData(pCrossThreadData, sizeof(pCrossThreadData));

        pKernel->setKernelArgHandler(1, &Kernel::setArgBuffer);
        pKernel->setKernelArgHandler(2, &Kernel::setArgBuffer);
        pKernel->setKernelArgHandler(0, &Kernel::setArgBuffer);

        BufferDefaults::context = new MockContext(pDevice);
        buffer = BufferHelper<>::create(BufferDefaults::context);
    }

    void TearDown() override {
        delete buffer;
        delete BufferDefaults::context;
        delete pKernel;

        delete pProgram;
        ContextFixture::TearDown();
        DeviceFixture::TearDown();
    }

    cl_int retVal = CL_SUCCESS;
    MockProgram *pProgram;
    MockKernel *pKernel = nullptr;
    std::unique_ptr<KernelInfo> pKernelInfo;
    SKernelBinaryHeaderCommon kernelHeader;
    char surfaceStateHeap[0x80];
    char pCrossThreadData[64];
    Buffer *buffer = nullptr;
};

TEST_F(BufferSetArgTest, setKernelArgBuffer) {
    auto pKernelArg = (void **)(pKernel->getCrossThreadData() +
                                pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);

    auto tokenSize = pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].size;

    buffer->setArgStateless(pKernelArg, tokenSize);

    EXPECT_EQ((void *)((uintptr_t)buffer->getGraphicsAllocation()->getGpuAddress()), *pKernelArg);
}

TEST_F(BufferSetArgTest, givenInvalidSizeWhenSettingKernelArgBufferThenReturnClInvalidArgSize) {
    cl_mem arg = buffer;
    cl_int err = pKernel->setArgBuffer(0, sizeof(cl_mem) + 1, arg);
    EXPECT_EQ(CL_INVALID_ARG_SIZE, err);
}

HWTEST_F(BufferSetArgTest, givenSetArgBufferWhenNullArgStatefulThenProgramNullSurfaceState) {
    using RENDER_SURFACE_STATE = typename FamilyType::RENDER_SURFACE_STATE;
    using SURFACE_FORMAT = typename RENDER_SURFACE_STATE::SURFACE_FORMAT;

    auto surfaceState = reinterpret_cast<const RENDER_SURFACE_STATE *>(
        ptrOffset(pKernel->getSurfaceStateHeap(),
                  pKernelInfo->kernelArgInfo[0].offsetHeap));

    pKernelInfo->requiresSshForBuffers = true;

    cl_int ret = pKernel->setArgBuffer(0, sizeof(cl_mem), nullptr);

    EXPECT_EQ(CL_SUCCESS, ret);

    auto surfaceFormat = surfaceState->getSurfaceType();
    auto surfacetype = surfaceState->getSurfaceFormat();

    EXPECT_EQ(surfaceFormat, RENDER_SURFACE_STATE::SURFACE_TYPE_SURFTYPE_NULL);
    EXPECT_EQ(surfacetype, SURFACE_FORMAT::SURFACE_FORMAT_RAW);
}

HWTEST_F(BufferSetArgTest, givenSetKernelArgOnReadOnlyBufferThatIsMisalingedWhenSurfaceStateIsSetThenCachingIsOn) {
    using RENDER_SURFACE_STATE = typename FamilyType::RENDER_SURFACE_STATE;

    auto surfaceState = reinterpret_cast<const RENDER_SURFACE_STATE *>(
        ptrOffset(pKernel->getSurfaceStateHeap(),
                  pKernelInfo->kernelArgInfo[0].offsetHeap));

    pKernelInfo->requiresSshForBuffers = true;
    pKernelInfo->kernelArgInfo[0].isReadOnly = true;

    auto graphicsAllocation = castToObject<Buffer>(buffer)->getGraphicsAllocation();
    graphicsAllocation->setSize(graphicsAllocation->getUnderlyingBufferSize() - 1);

    cl_mem clMemBuffer = buffer;

    cl_int ret = pKernel->setArgBuffer(0, sizeof(cl_mem), &clMemBuffer);

    EXPECT_EQ(CL_SUCCESS, ret);

    auto mocs = surfaceState->getMemoryObjectControlState();
    auto gmmHelper = pDevice->getGmmHelper();
    auto expectedMocs = gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER);
    auto expectedMocs2 = gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER_CONST);
    EXPECT_TRUE(expectedMocs == mocs || expectedMocs2 == mocs);
}

HWTEST_F(BufferSetArgTest, givenSetArgBufferWithNullArgStatelessThenDontProgramNullSurfaceState) {
    using RENDER_SURFACE_STATE = typename FamilyType::RENDER_SURFACE_STATE;
    using SURFACE_FORMAT = typename RENDER_SURFACE_STATE::SURFACE_FORMAT;

    char sshOriginal[sizeof(surfaceStateHeap)];
    memcpy(sshOriginal, surfaceStateHeap, sizeof(surfaceStateHeap));

    pKernelInfo->requiresSshForBuffers = false;

    cl_int ret = pKernel->setArgBuffer(0, sizeof(cl_mem), nullptr);

    EXPECT_EQ(CL_SUCCESS, ret);

    EXPECT_EQ(memcmp(sshOriginal, surfaceStateHeap, sizeof(surfaceStateHeap)), 0);
}

HWTEST_F(BufferSetArgTest, givenNonPureStatefulArgWhenRenderCompressedBufferIsSetThenSetNonAuxMode) {
    using RENDER_SURFACE_STATE = typename FamilyType::RENDER_SURFACE_STATE;

    auto surfaceState = reinterpret_cast<RENDER_SURFACE_STATE *>(ptrOffset(pKernel->getSurfaceStateHeap(), pKernelInfo->kernelArgInfo[0].offsetHeap));
    buffer->getGraphicsAllocation()->setAllocationType(GraphicsAllocation::AllocationType::BUFFER_COMPRESSED);
    buffer->getGraphicsAllocation()->setDefaultGmm(new Gmm(buffer->getGraphicsAllocation()->getUnderlyingBuffer(), buffer->getSize(), false));
    buffer->getGraphicsAllocation()->getDefaultGmm()->isRenderCompressed = true;
    pKernelInfo->requiresSshForBuffers = true;
    cl_mem clMem = buffer;

    pKernelInfo->kernelArgInfo.at(0).pureStatefulBufferAccess = false;
    cl_int ret = pKernel->setArgBuffer(0, sizeof(cl_mem), &clMem);
    EXPECT_EQ(CL_SUCCESS, ret);
    EXPECT_TRUE(RENDER_SURFACE_STATE::AUXILIARY_SURFACE_MODE::AUXILIARY_SURFACE_MODE_AUX_NONE == surfaceState->getAuxiliarySurfaceMode());

    pKernelInfo->kernelArgInfo.at(0).pureStatefulBufferAccess = true;
    ret = pKernel->setArgBuffer(0, sizeof(cl_mem), &clMem);
    EXPECT_EQ(CL_SUCCESS, ret);
    EXPECT_TRUE(RENDER_SURFACE_STATE::AUXILIARY_SURFACE_MODE::AUXILIARY_SURFACE_MODE_AUX_CCS_E == surfaceState->getAuxiliarySurfaceMode());
}

TEST_F(BufferSetArgTest, setKernelArgBufferFor32BitAddressing) {
    auto pKernelArg = (void **)(pKernel->getCrossThreadData() +
                                pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);

    auto tokenSize = pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].size;

    uintptr_t gpuBase = (uintptr_t)buffer->getGraphicsAllocation()->getGpuAddress() >> 2;
    buffer->getGraphicsAllocation()->setGpuBaseAddress(gpuBase);
    buffer->setArgStateless(pKernelArg, tokenSize, true);

    EXPECT_EQ((uintptr_t)buffer->getGraphicsAllocation()->getGpuAddress() - gpuBase, (uintptr_t)*pKernelArg);
}

TEST_F(BufferSetArgTest, givenBufferWhenOffsetedSubbufferIsPassedToSetKernelArgThenCorrectGpuVAIsPatched) {
    cl_buffer_region region;
    region.origin = 0xc0;
    region.size = 32;
    cl_int error = 0;
    auto subBuffer = buffer->createSubBuffer(buffer->getMemoryPropertiesFlags(), buffer->getMemoryPropertiesFlagsIntel(), &region, error);

    ASSERT_NE(nullptr, subBuffer);

    EXPECT_EQ(ptrOffset(buffer->getCpuAddress(), region.origin), subBuffer->getCpuAddress());

    auto pKernelArg = (void **)(pKernel->getCrossThreadData() +
                                pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);

    auto tokenSize = pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].size;

    subBuffer->setArgStateless(pKernelArg, tokenSize);

    EXPECT_EQ((void *)((uintptr_t)subBuffer->getGraphicsAllocation()->getGpuAddress() + region.origin), *pKernelArg);
    delete subBuffer;
}

TEST_F(BufferSetArgTest, givenCurbeTokenThatSizeIs4BytesWhenStatelessArgIsPatchedThenOnly4BytesArePatchedInCurbe) {
    auto pKernelArg = (void **)(pKernel->getCrossThreadData() +
                                pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);

    //fill 8 bytes with 0xffffffffffffffff;
    uint64_t fillValue = -1;
    uint64_t *pointer64bytes = (uint64_t *)pKernelArg;
    *pointer64bytes = fillValue;

    uint32_t sizeOf4Bytes = sizeof(uint32_t);

    pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].size = sizeOf4Bytes;

    buffer->setArgStateless(pKernelArg, sizeOf4Bytes);

    //make sure only 4 bytes are patched
    uintptr_t bufferAddress = (uintptr_t)buffer->getGraphicsAllocation()->getGpuAddress();
    uint32_t address32bits = (uint32_t)bufferAddress;
    uint64_t curbeValue = *pointer64bytes;
    uint32_t higherPart = curbeValue >> 32;
    uint32_t lowerPart = (curbeValue & 0xffffffff);
    EXPECT_EQ(0xffffffff, higherPart);
    EXPECT_EQ(address32bits, lowerPart);
}

TEST_F(BufferSetArgTest, clSetKernelArgBuffer) {
    cl_mem memObj = buffer;

    retVal = clSetKernelArg(
        pKernel,
        0,
        sizeof(memObj),
        &memObj);
    ASSERT_EQ(CL_SUCCESS, retVal);

    auto pKernelArg = (void **)(pKernel->getCrossThreadData() +
                                pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);

    EXPECT_EQ((void *)buffer->getGraphicsAllocation()->getGpuAddressToPatch(), *pKernelArg);

    std::vector<Surface *> surfaces;
    pKernel->getResidency(surfaces);
    EXPECT_EQ(1u, surfaces.size());

    for (auto &surface : surfaces) {
        delete surface;
    }
}

TEST_F(BufferSetArgTest, clSetKernelArgSVMPointer) {
    if (!pDevice->getHardwareInfo().capabilityTable.ftrSvm) {
        GTEST_SKIP();
    }
    void *ptrSVM = pContext->getSVMAllocsManager()->createSVMAlloc(pDevice->getRootDeviceIndex(), 256, {});
    EXPECT_NE(nullptr, ptrSVM);

    auto svmData = pContext->getSVMAllocsManager()->getSVMAlloc(ptrSVM);
    ASSERT_NE(nullptr, svmData);
    GraphicsAllocation *pSvmAlloc = svmData->gpuAllocation;
    EXPECT_NE(nullptr, pSvmAlloc);

    retVal = pKernel->setArgSvmAlloc(
        0,
        ptrSVM,
        pSvmAlloc);
    ASSERT_EQ(CL_SUCCESS, retVal);

    auto pKernelArg = (void **)(pKernel->getCrossThreadData() +
                                pKernelInfo->kernelArgInfo[0].kernelArgPatchInfoVector[0].crossthreadOffset);

    EXPECT_EQ(ptrSVM, *pKernelArg);

    std::vector<Surface *> surfaces;
    pKernel->getResidency(surfaces);
    EXPECT_EQ(1u, surfaces.size());
    for (auto &surface : surfaces) {
        delete surface;
    }

    pContext->getSVMAllocsManager()->freeSVMAlloc(ptrSVM);
}

TEST_F(BufferSetArgTest, getKernelArgShouldReturnBuffer) {
    cl_mem memObj = buffer;

    retVal = pKernel->setArg(
        0,
        sizeof(memObj),
        &memObj);
    ASSERT_EQ(CL_SUCCESS, retVal);

    EXPECT_EQ(memObj, pKernel->getKernelArg(0));
}

TEST_F(BufferSetArgTest, givenKernelArgBufferWhenAddPathInfoDataIsSetThenPatchInfoDataIsCollected) {
    DebugManagerStateRestore dbgRestore;
    DebugManager.flags.AddPatchInfoCommentsForAUBDump.set(true);
    cl_mem memObj = buffer;

    retVal = pKernel->setArg(
        0,
        sizeof(memObj),
        &memObj);

    ASSERT_EQ(CL_SUCCESS, retVal);
    ASSERT_EQ(1u, pKernel->getPatchInfoDataList().size());

    EXPECT_EQ(PatchInfoAllocationType::KernelArg, pKernel->getPatchInfoDataList()[0].sourceType);
    EXPECT_EQ(PatchInfoAllocationType::IndirectObjectHeap, pKernel->getPatchInfoDataList()[0].targetType);
    EXPECT_EQ(buffer->getGraphicsAllocation()->getGpuAddressToPatch(), pKernel->getPatchInfoDataList()[0].sourceAllocation);
    EXPECT_EQ(reinterpret_cast<uint64_t>(pKernel->getCrossThreadData()), pKernel->getPatchInfoDataList()[0].targetAllocation);
    EXPECT_EQ(0u, pKernel->getPatchInfoDataList()[0].sourceAllocationOffset);
}

TEST_F(BufferSetArgTest, givenKernelArgBufferWhenAddPathInfoDataIsNotSetThenPatchInfoDataIsNotCollected) {
    cl_mem memObj = buffer;

    retVal = pKernel->setArg(
        0,
        sizeof(memObj),
        &memObj);

    ASSERT_EQ(CL_SUCCESS, retVal);
    EXPECT_EQ(0u, pKernel->getPatchInfoDataList().size());
}
