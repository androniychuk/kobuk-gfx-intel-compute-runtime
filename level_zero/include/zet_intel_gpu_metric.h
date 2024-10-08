/*
 * Copyright (C) 2023-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef _ZET_INTEL_GPU_METRIC_H
#define _ZET_INTEL_GPU_METRIC_H

#include <level_zero/zet_api.h>

#if defined(__cplusplus)
#pragma once
extern "C" {
#endif

#include <stdint.h>

#define ZET_INTEL_GPU_METRIC_VERSION_MAJOR 0
#define ZET_INTEL_GPU_METRIC_VERSION_MINOR 1

#define ZET_INTEL_STRUCTURE_TYPE_METRIC_GROUP_TYPE_EXP (static_cast<zet_structure_type_t>(0x00010006))
#define ZET_INTEL_STRUCTURE_TYPE_EXPORT_DMA_PROPERTIES_EXP (static_cast<zet_structure_type_t>(0x00010007))
#define ZET_INTEL_METRIC_TYPE_EXPORT_DMA_BUF (0x7ffffffd)

typedef struct _zet_intel_export_dma_buf_exp_properties_t {
    zet_structure_type_t stype;
    void *pNext;
    int fd;      // [out] the file descriptor handle that could be used to import the memory by the host process.
    size_t size; // [out] size in bytes of the dma_buf
} zet_intel_export_dma_buf_exp_properties_t;

typedef enum _zet_intel_metric_group_type_exp_flags_t {
    ZET_INTEL_METRIC_GROUP_TYPE_EXP_OTHER = ZE_BIT(0),
    ZET_INTEL_METRIC_GROUP_TYPE_EXP_EXPORT_DMA_BUF = ZE_BIT(1)
} zet_intel_metric_group_type_exp_flags_t;

typedef struct _zet_intel_metric_group_type_exp_t {
    zet_structure_type_t stype;
    void *pNext;
    zet_intel_metric_group_type_exp_flags_t type; //[out] metric group type
} zet_intel_metric_group_type_exp_t;

///////////////////////////////////////////////////////////////////////////////
/// @brief Get sets of metric groups which could be collected concurrently.
///
/// @details
///     - Re-arrange the input metric groups to provide sets of concurrent metric groups.
///     - The application may call this function from simultaneous threads.
///     - The implementation of this function must be thread-safe.
///
/// @returns
///     - ::ZE_RESULT_SUCCESS
///     - ::ZE_RESULT_ERROR_INVALID_ARGUMENT
///         + `pConcurrentGroupCount` is not same as was returned by L0 using zetIntelDeviceGetConcurrentMetricGroupsExp
ze_result_t ZE_APICALL
zetIntelDeviceGetConcurrentMetricGroupsExp(
    zet_device_handle_t hDevice,               // [in] handle of the device
    uint32_t metricGroupCount,                 // [in] metric group count
    zet_metric_group_handle_t *phMetricGroups, // [in, out] metrics groups to be re-arranged to be sets of concurrent groups
    uint32_t *pConcurrentGroupCount,           // [out] number of concurrent groups.
    uint32_t *pCountPerConcurrentGroup);       // [in,out][optional][*pConcurrentGroupCount] count of metric groups per concurrent group.

#if defined(__cplusplus)
} // extern "C"
#endif

#endif
