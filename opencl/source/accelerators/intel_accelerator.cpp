/*
 * Copyright (C) 2017-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "opencl/source/accelerators/intel_accelerator.h"

#include "shared/source/helpers/get_info.h"
#include "shared/source/helpers/string.h"

#include "opencl/source/context/context.h"
#include "opencl/source/helpers/get_info_status_mapper.h"

namespace NEO {

cl_int IntelAccelerator::getInfo(cl_accelerator_info_intel paramName,
                                 size_t paramValueSize,
                                 void *paramValue,
                                 size_t *paramValueSizeRet) const {
    cl_int result = CL_SUCCESS;
    size_t ret = 0;

    switch (paramName) {
    case CL_ACCELERATOR_DESCRIPTOR_INTEL: {
        ret = getDescriptorSize();
        result = changeGetInfoStatusToCLResultType(::getInfo(paramValue, paramValueSize, getDescriptor(), ret));
    }

    break;

    case CL_ACCELERATOR_REFERENCE_COUNT_INTEL: {
        auto v = getReference();

        ret = sizeof(cl_uint);
        result = changeGetInfoStatusToCLResultType(::getInfo(paramValue, paramValueSize, &v, ret));
    }

    break;

    case CL_ACCELERATOR_CONTEXT_INTEL: {
        ret = sizeof(cl_context);
        cl_context ctx = static_cast<cl_context>(pContext);
        result = changeGetInfoStatusToCLResultType(::getInfo(paramValue, paramValueSize, &ctx, ret));
    }

    break;

    case CL_ACCELERATOR_TYPE_INTEL: {
        auto v = getTypeId();
        ret = sizeof(cl_accelerator_type_intel);
        result = changeGetInfoStatusToCLResultType(::getInfo(paramValue, paramValueSize, &v, ret));
    }

    break;

    default:
        result = CL_INVALID_VALUE;
        break;
    }

    if (paramValueSizeRet) {
        *paramValueSizeRet = ret;
    }

    return result;
}
} // namespace NEO
