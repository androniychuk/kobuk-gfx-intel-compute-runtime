/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "core/elf/reader.h"
#include "core/elf/writer.h"
#include "unit_tests/fixtures/memory_management_fixture.h"

#include "gtest/gtest.h"

using namespace CLElfLib;

struct ElfTests : public MemoryManagementFixture,
                  public ::testing::Test {
  public:
    void SetUp() override {
        MemoryManagementFixture::SetUp();
    }

    void TearDown() override {
        MemoryManagementFixture::TearDown();
    }
};

TEST_F(ElfTests, givenSectionDataWhenWriteToBinaryThenSectionIsAdded) {
    class MockElfWriter : public CElfWriter {
      public:
        MockElfWriter() : CElfWriter(E_EH_TYPE::EH_TYPE_EXECUTABLE, E_EH_MACHINE::EH_MACHINE_NONE, 0) {}
        using CElfWriter::nodeQueue;
    };

    MockElfWriter writer;
    std::string data{"data pattern"};

    writer.addSection(SSectionNode(E_SH_TYPE::SH_TYPE_OPENCL_SOURCE, E_SH_FLAG::SH_FLAG_WRITE, "Steve", data, static_cast<uint32_t>(data.size())));

    ASSERT_EQ(2u, writer.nodeQueue.size());
    // remove first (default) section
    writer.nodeQueue.pop();
    EXPECT_EQ(E_SH_TYPE::SH_TYPE_OPENCL_SOURCE, writer.nodeQueue.front().type);
    EXPECT_EQ(E_SH_FLAG::SH_FLAG_WRITE, writer.nodeQueue.front().flag);
    EXPECT_EQ("Steve", writer.nodeQueue.front().name);
    EXPECT_EQ(data, writer.nodeQueue.front().data);
    EXPECT_EQ(static_cast<uint32_t>(data.size()), writer.nodeQueue.front().dataSize);
}

TEST_F(ElfTests, givenCElfWriterWhenPatchElfHeaderThenDefaultAreSet) {
    class MockElfWriter : public CElfWriter {
      public:
        MockElfWriter() : CElfWriter(E_EH_TYPE::EH_TYPE_EXECUTABLE, E_EH_MACHINE::EH_MACHINE_NONE, 0) {}
        using CElfWriter::patchElfHeader;
    };

    MockElfWriter writer;

    SElf64Header elfHeader;

    writer.patchElfHeader(elfHeader);

    EXPECT_TRUE(elfHeader.Identity[ELFConstants::idIdxMagic0] == ELFConstants::elfMag0);
    EXPECT_TRUE(elfHeader.Identity[ELFConstants::idIdxMagic1] == ELFConstants::elfMag1);
    EXPECT_TRUE(elfHeader.Identity[ELFConstants::idIdxMagic2] == ELFConstants::elfMag2);
    EXPECT_TRUE(elfHeader.Identity[ELFConstants::idIdxMagic3] == ELFConstants::elfMag3);
    EXPECT_TRUE(elfHeader.Identity[ELFConstants::idIdxClass] == static_cast<uint32_t>(E_EH_CLASS::EH_CLASS_64));
    EXPECT_TRUE(elfHeader.Identity[ELFConstants::idIdxVersion] == static_cast<uint32_t>(E_EHT_VERSION::EH_VERSION_CURRENT));

    EXPECT_TRUE(elfHeader.Type == E_EH_TYPE::EH_TYPE_EXECUTABLE);
    EXPECT_TRUE(elfHeader.Machine == E_EH_MACHINE::EH_MACHINE_NONE);
    EXPECT_TRUE(elfHeader.Flags == static_cast<uint32_t>(0));
    EXPECT_TRUE(elfHeader.ElfHeaderSize == static_cast<uint32_t>(sizeof(SElf64Header)));
    EXPECT_TRUE(elfHeader.SectionHeaderEntrySize == static_cast<uint32_t>(sizeof(SElf64SectionHeader)));
    EXPECT_TRUE(elfHeader.NumSectionHeaderEntries == 1);
    EXPECT_TRUE(elfHeader.SectionHeadersOffset == static_cast<uint32_t>(sizeof(SElf64Header)));
    EXPECT_TRUE(elfHeader.SectionNameTableIndex == 0);
}

TEST_F(ElfTests, givenSectionDataWhenWriteToBinaryThenSectionCanBeReadByID) {
    CElfWriter writer(E_EH_TYPE::EH_TYPE_EXECUTABLE, E_EH_MACHINE::EH_MACHINE_NONE, 0);

    char sectionData[16];
    memset(sectionData, 0xdeadbeef, 4);

    writer.addSection(SSectionNode(E_SH_TYPE::SH_TYPE_OPENCL_SOURCE, E_SH_FLAG::SH_FLAG_WRITE, "", std::string(sectionData, 16u), 16u));

    size_t binarySize = writer.getTotalBinarySize();

    ElfBinaryStorage binary(binarySize);
    writer.resolveBinary(binary);

    CElfReader elfReader(binary);

    char *pData = elfReader.getSectionData(elfReader.getSectionHeaders()[1].DataOffset);

    EXPECT_EQ(16u, elfReader.getSectionHeaders()[1].DataSize);
    for (unsigned int i = 0; i < elfReader.getSectionHeaders()[1].DataSize; i++) {
        EXPECT_EQ(sectionData[i], pData[i]);
    }
}

TEST_F(ElfTests, givenInvalidBinaryStorageThenExceptionIsThrown) {
    ElfBinaryStorage binary;

    EXPECT_THROW(CElfReader elfReader(binary), ElfException);
}
