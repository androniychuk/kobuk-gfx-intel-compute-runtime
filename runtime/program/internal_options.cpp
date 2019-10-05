/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "runtime/program/program.h"

#include <vector>

namespace NEO {
const std::vector<std::string> Program::internalOptionsToExtract = {"-cl-intel-gtpin-rera", "-cl-intel-greater-than-4GB-buffer-required"};
};
