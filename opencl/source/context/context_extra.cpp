/*
 * Copyright (C) 2018-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/command_stream/command_stream_receiver.h"

#include "opencl/source/context/context.h"
#include "opencl/source/mem_obj/mem_obj.h"

namespace NEO {

cl_int Context::processExtraProperties(cl_context_properties propertyType, cl_context_properties propertyValue) {
    return CL_INVALID_PROPERTY;
}

} // namespace NEO
