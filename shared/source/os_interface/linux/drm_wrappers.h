/*
 * Copyright (C) 2022-2024 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace NEO {
class IoctlHelper;

struct RegisterRead {
    uint64_t offset;
    uint64_t value;
};

struct ExecObject {
    uint8_t data[56];
};

struct ExecBuffer {
    uint8_t data[64];
};

struct GemCreate {
    uint64_t size;
    uint32_t handle;
};

struct GemUserPtr {
    uint64_t userPtr;
    uint64_t userSize;
    uint32_t flags;
    uint32_t handle;
};

struct GemSetTiling {
    uint32_t handle;
    uint32_t tilingMode;
    uint32_t stride;
    uint32_t swizzleMode;
};

struct GemGetTiling {
    uint32_t handle;
    uint32_t tilingMode;
    uint32_t swizzleMode;
    uint32_t physSwizzleMode;
};

struct QueryItem {
    uint64_t queryId;
    int32_t length;
    uint32_t flags;
    uint64_t dataPtr;
};

struct EngineClassInstance {
    uint16_t engineClass;
    uint16_t engineInstance;
};

struct GemContextParamSseu {
    EngineClassInstance engine;
    uint32_t flags;
    uint64_t sliceMask;
    uint64_t subsliceMask;
    uint16_t minEusPerSubslice;
    uint16_t maxEusPerSubslice;
};

struct QueryTopologyInfo {
    uint16_t flags;
    uint16_t maxSlices;
    uint16_t maxSubslices;
    uint16_t maxEusPerSubslice;
    uint16_t subsliceOffset;
    uint16_t subsliceStride;
    uint16_t euOffset;
    uint16_t euStride;
    uint8_t data[];
};

struct DrmQueryTopologyData {
    int sliceCount = 0;
    int subSliceCount = 0;
    int euCount = 0;

    int maxSliceCount = 0;
    int maxSubSliceCount = 0;
    int maxEuPerSubSlice = 0;
};

struct MemoryClassInstance {
    uint16_t memoryClass;
    uint16_t memoryInstance;
};

struct GemMmapOffset {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
    uint64_t flags;
    uint64_t extensions;
};

struct GemSetDomain {
    uint32_t handle;
    uint32_t readDomains;
    uint32_t writeDomain;
};

struct GemContextParam {
    uint32_t contextId;
    uint32_t size;
    uint64_t param;
    uint64_t value;
};

struct DrmUserExtension {
    uint64_t nextExtension;
    uint32_t name;
    uint32_t flags;
    uint32_t reserved[4];
};

struct GemContextCreateExtSetParam {
    DrmUserExtension base;
    GemContextParam param;
};

struct GemContextCreateExt {
    uint32_t contextId;
    uint32_t flags;
    uint64_t extensions;
};

struct GemContextDestroy {
    uint32_t contextId;
    uint32_t reserved;
};

struct GemVmControl {
    uint64_t extensions;
    uint32_t flags;
    uint32_t vmId;
};

struct GemWait {
    uint32_t boHandle;
    uint32_t flags;
    int64_t timeoutNs;
};

struct ResetStats {
    uint32_t contextId;
    uint32_t flags;
    uint32_t resetCount;
    uint32_t batchActive;
    uint32_t batchPending;
    uint32_t reserved;
};

struct GetParam {
    int32_t param;
    int *value;
};

struct Query {
    uint32_t numItems;
    uint32_t flags;
    uint64_t itemsPtr;
};

struct GemClose {
    uint32_t handle;
    uint32_t reserved;
    uint64_t userptr;
};

struct PrimeHandle {
    uint32_t handle;
    uint32_t flags;
    int32_t fileDescriptor;
};

#pragma pack(1)
template <uint32_t numEngines = 10> // 1 + max engines
struct ContextParamEngines {
    uint64_t extensions;
    EngineClassInstance engines[numEngines];
};

template <uint32_t numEngines>
struct ContextEnginesLoadBalance {
    DrmUserExtension base;
    uint16_t engineIndex;
    uint16_t numSiblings;
    uint32_t flags;
    uint64_t reserved;
    EngineClassInstance engines[numEngines];
};
#pragma pack()

struct DrmVersion {
    int versionMajor;
    int versionMinor;
    int versionPatch;
    size_t nameLen;
    char *name;
    size_t dateLen;
    char *date;
    size_t descLen;
    char *desc;
};

struct DrmDebuggerOpen {
    uint64_t pid;
    uint32_t flags;
    uint32_t version;
    uint64_t events;
    uint64_t extensions;
};

enum class DrmIoctl {
    gemExecbuffer2,
    gemWait,
    gemUserptr,
    getparam,
    gemCreate,
    gemSetDomain,
    gemSetTiling,
    gemGetTiling,
    gemContextCreateExt,
    gemContextDestroy,
    regRead,
    getResetStats,
    getResetStatsPrelim,
    gemContextGetparam,
    gemContextSetparam,
    query,
    gemMmapOffset,
    gemVmCreate,
    gemVmDestroy,
    gemClose,
    primeFdToHandle,
    primeHandleToFd,
    gemVmBind,
    gemVmUnbind,
    gemWaitUserFence,
    dg1GemCreateExt,
    gemCreateExt,
    gemVmAdvise,
    gemVmPrefetch,
    uuidRegister,
    uuidUnregister,
    debuggerOpen,
    gemClosReserve,
    gemClosFree,
    gemCacheReserve,
    version,
    vmExport,
    metadataCreate,
    metadataDestroy,
    perfOpen,
    perfEnable,
    perfDisable
};

enum class DrmParam {
    contextCreateExtSetparam,
    contextCreateFlagsUseExtensions,
    contextEnginesExtLoadBalance,
    contextParamEngines,
    contextParamGttSize,
    contextParamPersistence,
    contextParamPriority,
    contextParamRecoverable,
    contextParamSseu,
    contextParamVm,
    engineClassRender,
    engineClassCompute,
    engineClassCopy,
    engineClassVideo,
    engineClassVideoEnhance,
    engineClassInvalid,
    engineClassInvalidNone,
    execBlt,
    execDefault,
    execNoReloc,
    execRender,
    memoryClassDevice,
    memoryClassSystem,
    mmapOffsetWb,
    mmapOffsetWc,
    paramChipsetId,
    paramRevision,
    paramHasExecSoftpin,
    paramHasPooledEu,
    paramHasScheduler,
    paramEuTotal,
    paramSubsliceTotal,
    paramMinEuInPool,
    paramCsTimestampFrequency,
    paramOATimestampFrequency,
    paramHasVmBind,
    paramHasPageFault,
    queryEngineInfo,
    queryHwconfigTable,
    queryComputeSlices,
    queryMemoryRegions,
    queryTopologyInfo,
    schedulerCapPreemption,
    tilingNone,
    tilingY,
};

unsigned int getIoctlRequestValue(DrmIoctl ioctlRequest, IoctlHelper *ioctlHelper);
int getDrmParamValue(DrmParam drmParam, IoctlHelper *ioctlHelper);
std::string getDrmParamString(DrmParam param, IoctlHelper *ioctlHelper);
std::string getIoctlString(DrmIoctl ioctlRequest, IoctlHelper *ioctlHelper);
bool checkIfIoctlReinvokeRequired(int error, DrmIoctl ioctlRequest, IoctlHelper *ioctlHelper);

} // namespace NEO
