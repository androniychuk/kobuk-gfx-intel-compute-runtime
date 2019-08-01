/*
 * Copyright (C) 2018-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "offline_compiler/decoder/binary_decoder.h"

#include "mock_iga_wrapper.h"

struct MockDecoder : public BinaryDecoder {
    MockDecoder() : MockDecoder("", "", "") {
    }
    MockDecoder(const std::string &file, const std::string &patch, const std::string &dump)
        : BinaryDecoder(file, patch, dump) {
        this->iga.reset(new MockIgaWrapper);
        setMessagePrinter(MessagePrinter{true});
    };
    using BinaryDecoder::binaryFile;
    using BinaryDecoder::decode;
    using BinaryDecoder::getSize;
    using BinaryDecoder::iga;
    using BinaryDecoder::kernelHeader;
    using BinaryDecoder::parseTokens;
    using BinaryDecoder::patchTokens;
    using BinaryDecoder::pathToDump;
    using BinaryDecoder::pathToPatch;
    using BinaryDecoder::processBinary;
    using BinaryDecoder::processKernel;
    using BinaryDecoder::programHeader;
    using BinaryDecoder::readPatchTokens;
    using BinaryDecoder::readStructFields;

    MockIgaWrapper *getMockIga() const {
        return static_cast<MockIgaWrapper *>(iga.get());
    }
};
