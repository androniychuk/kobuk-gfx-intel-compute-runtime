/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/compiler_interface/compiler_interface.h"
#include "shared/source/device_binary_format/elf/elf_decoder.h"
#include "shared/source/helpers/file_io.h"
#include "shared/test/unit_test/helpers/test_files.h"

#include "opencl/source/context/context.h"
#include "opencl/test/unit_test/global_environment.h"

#include "cl_api_tests.h"
#include "compiler_options.h"

using namespace NEO;

namespace ULT {

typedef api_tests clLinkProgramTests;

TEST_F(clLinkProgramTests, GivenValidParamsWhenLinkingProgramThenSuccessIsReturned) {
    cl_program pProgram = nullptr;
    size_t sourceSize = 0;
    std::string testFile;

    testFile.append(clFiles);
    testFile.append("copybuffer.cl");
    auto pSource = loadDataFromFile(
        testFile.c_str(),
        sourceSize);

    ASSERT_NE(0u, sourceSize);
    ASSERT_NE(nullptr, pSource);

    const char *sources[1] = {pSource.get()};
    pProgram = clCreateProgramWithSource(
        pContext,
        1,
        sources,
        &sourceSize,
        &retVal);

    EXPECT_NE(nullptr, pProgram);
    ASSERT_EQ(CL_SUCCESS, retVal);

    retVal = clCompileProgram(
        pProgram,
        1,
        &testedClDevice,
        nullptr,
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    ASSERT_EQ(CL_SUCCESS, retVal);

    cl_program program = pProgram;
    cl_program oprog;
    oprog = clLinkProgram(
        pContext,
        1,
        &testedClDevice,
        nullptr,
        1,
        &program,
        nullptr,
        nullptr,
        &retVal);

    ASSERT_EQ(CL_SUCCESS, retVal);

    retVal = clReleaseProgram(pProgram);
    EXPECT_EQ(CL_SUCCESS, retVal);

    retVal = clReleaseProgram(oprog);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

TEST_F(clLinkProgramTests, GivenCreateLibraryOptionWhenLinkingProgramThenSuccessIsReturned) {
    cl_program pProgram = nullptr;
    size_t sourceSize = 0;
    std::string testFile;

    testFile.append(clFiles);
    testFile.append("copybuffer.cl");
    auto pSource = loadDataFromFile(
        testFile.c_str(),
        sourceSize);

    ASSERT_NE(0u, sourceSize);
    ASSERT_NE(nullptr, pSource);

    const char *sources[1] = {pSource.get()};
    pProgram = clCreateProgramWithSource(
        pContext,
        1,
        sources,
        &sourceSize,
        &retVal);

    EXPECT_NE(nullptr, pProgram);
    ASSERT_EQ(CL_SUCCESS, retVal);

    retVal = clCompileProgram(
        pProgram,
        1,
        &testedClDevice,
        nullptr,
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    ASSERT_EQ(CL_SUCCESS, retVal);

    cl_program program = pProgram;
    cl_program oprog;
    oprog = clLinkProgram(
        pContext,
        1,
        &testedClDevice,
        CompilerOptions::createLibrary.data(),
        1,
        &program,
        nullptr,
        nullptr,
        &retVal);

    ASSERT_EQ(CL_SUCCESS, retVal);

    retVal = clReleaseProgram(pProgram);
    EXPECT_EQ(CL_SUCCESS, retVal);

    retVal = clReleaseProgram(oprog);
    EXPECT_EQ(CL_SUCCESS, retVal);
}

TEST_F(clLinkProgramTests, GivenNullContextWhenLinkingProgramThenClInvalidContextErrorIsReturned) {
    cl_program program = {0};
    cl_program oprog;
    oprog = clLinkProgram(
        nullptr,
        1,
        &testedClDevice,
        nullptr,
        1,
        &program,
        nullptr,
        nullptr,
        &retVal);
    EXPECT_EQ(CL_INVALID_CONTEXT, retVal);
    EXPECT_EQ(nullptr, oprog);
}

template <typename T>
std::vector<T> asVec(const uint8_t *src, size_t size) {
    auto beg = reinterpret_cast<const T *>(src);
    auto end = beg + size / sizeof(T);
    return std::vector<T>(beg, end);
}

TEST_F(clLinkProgramTests, GivenProgramsWithSpecConstantsThenSpecConstantsAreEmbeddedIntoElf) {
    uint8_t ir1[] = {15, 17, 19, 23};
    uint8_t ir2[] = {29, 31, 37, 41};
    uint8_t ir3[] = {43, 47, 53, 59};
    uint32_t prog1Keys[2] = {2, 3};
    uint64_t prog1Values[2] = {5, 7};
    uint32_t prog2Keys[1] = {11};
    uint64_t prog2Values[1] = {13};

    auto progSrc1 = clUniquePtr(new MockProgram(*pProgram->getDevice().getExecutionEnvironment(), pContext, false, pProgram->pDevice));
    progSrc1->pDevice = pProgram->pDevice;
    progSrc1->specConstantsValues[prog1Keys[0]] = prog1Values[0];
    progSrc1->specConstantsValues[prog1Keys[1]] = prog1Values[1];
    progSrc1->areSpecializationConstantsInitialized = true;
    progSrc1->irBinary = makeCopy(ir1, sizeof(ir1));
    progSrc1->irBinarySize = sizeof(ir1);
    progSrc1->isSpirV = true;

    auto progSrc2 = clUniquePtr(new MockProgram(*pProgram->getDevice().getExecutionEnvironment(), pContext, false, pProgram->pDevice));
    progSrc2->pDevice = pProgram->pDevice;
    progSrc2->specConstantsValues[prog2Keys[0]] = prog2Values[0];
    progSrc2->areSpecializationConstantsInitialized = true;
    progSrc2->irBinary = makeCopy(ir2, sizeof(ir2));
    progSrc2->irBinarySize = sizeof(ir2);
    progSrc2->isSpirV = true;

    auto progSrc3 = clUniquePtr(new MockProgram(*pProgram->getDevice().getExecutionEnvironment(), pContext, false, pProgram->pDevice));
    progSrc3->pDevice = pProgram->pDevice;
    progSrc3->irBinary = makeCopy(ir3, sizeof(ir3));
    progSrc3->irBinarySize = sizeof(ir3);
    progSrc3->isSpirV = true;

    auto progDst = clUniquePtr(new MockProgram(*pProgram->getDevice().getExecutionEnvironment(), pContext, false, pProgram->pDevice));
    progDst->pDevice = pProgram->pDevice;
    cl_program inputPrograms[3] = {progSrc1.get(), progSrc2.get(), progSrc3.get()};

    std::string receivedInput;
    MockCompilerDebugVars igcDebugVars;
    igcDebugVars.receivedInput = &receivedInput;
    gEnvironment->igcPushDebugVars(igcDebugVars);
    progDst->link(0U, nullptr, "", 3, inputPrograms, nullptr, nullptr);
    gEnvironment->igcPopDebugVars();

    std::string elfDecodeError;
    std::string elfDecoceWarnings;
    auto elf = NEO::Elf::decodeElf<NEO::Elf::EI_CLASS_64>(ArrayRef<const uint8_t>::fromAny(receivedInput.data(), receivedInput.size()),
                                                          elfDecodeError, elfDecoceWarnings);
    ASSERT_NE(nullptr, elf.elfFileHeader) << elfDecodeError;

    ASSERT_EQ(8U, elf.sectionHeaders.size());
    EXPECT_EQ(NEO::Elf::SHT_OPENCL_SPIRV_SC_IDS, elf.sectionHeaders[1].header->type);
    EXPECT_EQ(NEO::Elf::SHT_OPENCL_SPIRV_SC_VALUES, elf.sectionHeaders[2].header->type);
    EXPECT_EQ(NEO::Elf::SHT_OPENCL_SPIRV, elf.sectionHeaders[3].header->type);
    EXPECT_EQ(NEO::Elf::SHT_OPENCL_SPIRV_SC_IDS, elf.sectionHeaders[4].header->type);
    EXPECT_EQ(NEO::Elf::SHT_OPENCL_SPIRV_SC_VALUES, elf.sectionHeaders[5].header->type);
    EXPECT_EQ(NEO::Elf::SHT_OPENCL_SPIRV, elf.sectionHeaders[6].header->type);
    EXPECT_EQ(NEO::Elf::SHT_OPENCL_SPIRV, elf.sectionHeaders[7].header->type);

    ASSERT_EQ(sizeof(uint32_t) * progSrc1->specConstantsValues.size(), elf.sectionHeaders[1].data.size());
    ASSERT_EQ(sizeof(uint64_t) * progSrc1->specConstantsValues.size(), elf.sectionHeaders[2].data.size());
    ASSERT_EQ(sizeof(ir1), elf.sectionHeaders[3].data.size());
    ASSERT_EQ(sizeof(uint32_t) * progSrc2->specConstantsValues.size(), elf.sectionHeaders[4].data.size());
    ASSERT_EQ(sizeof(uint64_t) * progSrc2->specConstantsValues.size(), elf.sectionHeaders[5].data.size());
    ASSERT_EQ(sizeof(ir2), elf.sectionHeaders[6].data.size());
    ASSERT_EQ(sizeof(ir3), elf.sectionHeaders[7].data.size());

    auto readSpecConstId = [](NEO::Elf::Elf<NEO::Elf::EI_CLASS_64>::SectionHeaderAndData &section, uint32_t offset) {
        return *(reinterpret_cast<const uint32_t *>(section.data.begin()) + offset);
    };

    auto readSpecConstValue = [](NEO::Elf::Elf<NEO::Elf::EI_CLASS_64>::SectionHeaderAndData &section, uint32_t offset) {
        return *(reinterpret_cast<const uint64_t *>(section.data.begin()) + offset);
    };

    ASSERT_EQ(1U, progSrc1->specConstantsValues.count(readSpecConstId(elf.sectionHeaders[1], 0)));
    EXPECT_EQ(progSrc1->specConstantsValues[readSpecConstId(elf.sectionHeaders[1], 0)], readSpecConstValue(elf.sectionHeaders[2], 0));
    ASSERT_EQ(1U, progSrc1->specConstantsValues.count(readSpecConstId(elf.sectionHeaders[1], 1)));
    EXPECT_EQ(progSrc1->specConstantsValues[readSpecConstId(elf.sectionHeaders[1], 1)], readSpecConstValue(elf.sectionHeaders[2], 1));
    EXPECT_EQ(0, memcmp(ir1, elf.sectionHeaders[3].data.begin(), sizeof(ir1)));

    ASSERT_EQ(1U, progSrc2->specConstantsValues.count(readSpecConstId(elf.sectionHeaders[4], 0)));
    EXPECT_EQ(progSrc2->specConstantsValues[readSpecConstId(elf.sectionHeaders[4], 0)], readSpecConstValue(elf.sectionHeaders[5], 0));
    EXPECT_EQ(0, memcmp(ir2, elf.sectionHeaders[6].data.begin(), sizeof(ir2)));

    EXPECT_EQ(0, memcmp(ir3, elf.sectionHeaders[7].data.begin(), sizeof(ir3)));
}

} // namespace ULT
