/*
 * Copyright (C) 2017-2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "core/helpers/preamble.h"
#include "core/unified_memory/unified_memory.h"
#include "core/utilities/stackvec.h"
#include "runtime/api/cl_types.h"
#include "runtime/command_stream/thread_arbitration_policy.h"
#include "runtime/device_queue/device_queue.h"
#include "runtime/helpers/address_patch.h"
#include "runtime/helpers/base_object.h"
#include "runtime/helpers/properties_helper.h"
#include "runtime/os_interface/debug_settings_manager.h"
#include "runtime/program/kernel_info.h"
#include "runtime/program/program.h"

#include <vector>

namespace NEO {
struct CompletionStamp;
class Buffer;
class CommandStreamReceiver;
class GraphicsAllocation;
class ImageTransformer;
class Surface;
class PrintfHandler;

template <>
struct OpenCLObjectMapper<_cl_kernel> {
    typedef class Kernel DerivedType;
};

class Kernel : public BaseObject<_cl_kernel> {
  public:
    static const cl_ulong objectMagic = 0x3284ADC8EA0AFE25LL;
    static const uint32_t kernelBinaryAlignement = 64;

    enum kernelArgType {
        NONE_OBJ,
        IMAGE_OBJ,
        BUFFER_OBJ,
        PIPE_OBJ,
        SVM_OBJ,
        SVM_ALLOC_OBJ,
        SAMPLER_OBJ,
        ACCELERATOR_OBJ,
        DEVICE_QUEUE_OBJ,
        SLM_OBJ
    };

    struct SimpleKernelArgInfo {
        kernelArgType type;
        void *object;
        const void *value;
        size_t size;
        GraphicsAllocation *pSvmAlloc;
        cl_mem_flags svmFlags;
        bool isPatched = false;
        bool isStatelessUncacheable = false;
    };

    typedef int32_t (Kernel::*KernelArgHandler)(uint32_t argIndex,
                                                size_t argSize,
                                                const void *argVal);

    template <typename kernel_t = Kernel, typename program_t = Program>
    static kernel_t *create(Program *program, const KernelInfo &kernelInfo, cl_int *errcodeRet) {
        cl_int retVal;
        kernel_t *pKernel = nullptr;

        do {
            // copy the kernel data into our new allocation
            pKernel = new kernel_t(program, kernelInfo, program->getDevice(0));
            retVal = pKernel->initialize();
        } while (false);

        if (retVal != CL_SUCCESS) {
            delete pKernel;
            pKernel = nullptr;
        }

        if (errcodeRet) {
            *errcodeRet = retVal;
        }

        if (DebugManager.debugKernelDumpingAvailable()) {
            std::string source;
            program->getSource(source);
            DebugManager.dumpKernel(kernelInfo.name, source);
        }

        return pKernel;
    }

    Kernel &operator=(const Kernel &) = delete;
    Kernel(const Kernel &) = delete;

    ~Kernel() override;

    static bool isMemObj(kernelArgType kernelArg) {
        return kernelArg == BUFFER_OBJ || kernelArg == IMAGE_OBJ || kernelArg == PIPE_OBJ;
    }

    bool isAuxTranslationRequired() const { return auxTranslationRequired; }

    char *getCrossThreadData() const {
        return crossThreadData;
    }

    uint32_t getCrossThreadDataSize() const {
        return crossThreadDataSize;
    }

    cl_int initialize();

    MOCKABLE_VIRTUAL cl_int cloneKernel(Kernel *pSourceKernel);

    MOCKABLE_VIRTUAL bool canTransformImages() const;
    MOCKABLE_VIRTUAL bool isPatched() const;

    // API entry points
    cl_int setArg(uint32_t argIndex, size_t argSize, const void *argVal);
    cl_int setArgSvm(uint32_t argIndex, size_t svmAllocSize, void *svmPtr, GraphicsAllocation *svmAlloc, cl_mem_flags svmFlags);
    cl_int setArgSvmAlloc(uint32_t argIndex, void *svmPtr, GraphicsAllocation *svmAlloc);

    void setSvmKernelExecInfo(GraphicsAllocation *argValue);
    void clearSvmKernelExecInfo();

    cl_int getInfo(cl_kernel_info paramName, size_t paramValueSize,
                   void *paramValue, size_t *paramValueSizeRet) const;
    void getAdditionalInfo(cl_kernel_info paramName, const void *&paramValue, size_t &paramValueSizeRet) const;

    cl_int getArgInfo(cl_uint argIndx, cl_kernel_arg_info paramName,
                      size_t paramValueSize, void *paramValue, size_t *paramValueSizeRet) const;

    cl_int getWorkGroupInfo(cl_device_id device, cl_kernel_work_group_info paramName,
                            size_t paramValueSize, void *paramValue, size_t *paramValueSizeRet) const;

    cl_int getSubGroupInfo(cl_kernel_sub_group_info paramName,
                           size_t inputValueSize, const void *inputValue,
                           size_t paramValueSize, void *paramValue,
                           size_t *paramValueSizeRet) const;

    const void *getKernelHeap() const;
    const void *getSurfaceStateHeap() const;
    void *getSurfaceStateHeap();
    const void *getDynamicStateHeap() const;

    size_t getKernelHeapSize() const;
    size_t getSurfaceStateHeapSize() const;
    size_t getDynamicStateHeapSize() const;
    size_t getNumberOfBindingTableStates() const;
    size_t getBindingTableOffset() const {
        return localBindingTableOffset;
    }

    void resizeSurfaceStateHeap(void *pNewSsh, size_t newSshSize, size_t newBindingTableCount, size_t newBindingTableOffset);

    void substituteKernelHeap(void *newKernelHeap, size_t newKernelHeapSize);
    bool isKernelHeapSubstituted() const;
    uint64_t getKernelId() const;
    void setKernelId(uint64_t newKernelId);
    uint32_t getStartOffset() const;
    void setStartOffset(uint32_t offset);

    const std::vector<SimpleKernelArgInfo> &getKernelArguments() const {
        return kernelArguments;
    }

    size_t getKernelArgsNumber() const {
        return kernelInfo.kernelArgInfo.size();
    }

    uint32_t getKernelArgAddressQualifier(uint32_t argIndex) const {
        return kernelInfo.kernelArgInfo[argIndex].addressQualifier;
    }

    bool requiresSshForBuffers() const {
        return kernelInfo.requiresSshForBuffers;
    }

    const KernelInfo &getKernelInfo() const {
        return kernelInfo;
    }

    const Device &getDevice() const {
        return device;
    }

    Context &getContext() const {
        return context ? *context : program->getContext();
    }

    void setContext(Context *context) {
        this->context = context;
    }

    Program *getProgram() const { return program; }

    static uint32_t getScratchSizeValueToProgramMediaVfeState(int scratchSize);
    uint32_t getScratchSize() {
        return kernelInfo.patchInfo.mediavfestate ? kernelInfo.patchInfo.mediavfestate->PerThreadScratchSpace : 0;
    }

    uint32_t getPrivateScratchSize() {
        return kernelInfo.patchInfo.mediaVfeStateSlot1 ? kernelInfo.patchInfo.mediaVfeStateSlot1->PerThreadScratchSpace : 0;
    }

    void createReflectionSurface();
    template <bool mockable = false>
    void patchReflectionSurface(DeviceQueue *devQueue, PrintfHandler *printfHandler);

    void patchDefaultDeviceQueue(DeviceQueue *devQueue);
    void patchEventPool(DeviceQueue *devQueue);
    void patchBlocksSimdSize();

    GraphicsAllocation *getKernelReflectionSurface() const {
        return kernelReflectionSurface;
    }

    size_t getInstructionHeapSizeForExecutionModel() const;

    // Helpers
    cl_int setArg(uint32_t argIndex, uint32_t argValue);
    cl_int setArg(uint32_t argIndex, uint64_t argValue);
    cl_int setArg(uint32_t argIndex, cl_mem argValue);
    cl_int setArg(uint32_t argIndex, cl_mem argValue, uint32_t mipLevel);

    // Handlers
    void setKernelArgHandler(uint32_t argIndex, KernelArgHandler handler);

    void unsetArg(uint32_t argIndex);

    cl_int setArgImmediate(uint32_t argIndex,
                           size_t argSize,
                           const void *argVal);

    cl_int setArgBuffer(uint32_t argIndex,
                        size_t argSize,
                        const void *argVal);

    cl_int setArgPipe(uint32_t argIndex,
                      size_t argSize,
                      const void *argVal);

    cl_int setArgImage(uint32_t argIndex,
                       size_t argSize,
                       const void *argVal);

    cl_int setArgImageWithMipLevel(uint32_t argIndex,
                                   size_t argSize,
                                   const void *argVal, uint32_t mipLevel);

    cl_int setArgLocal(uint32_t argIndex,
                       size_t argSize,
                       const void *argVal);

    cl_int setArgSampler(uint32_t argIndex,
                         size_t argSize,
                         const void *argVal);

    cl_int setArgAccelerator(uint32_t argIndex,
                             size_t argSize,
                             const void *argVal);

    cl_int setArgDevQueue(uint32_t argIndex,
                          size_t argSize,
                          const void *argVal);

    void storeKernelArg(uint32_t argIndex,
                        kernelArgType argType,
                        void *argObject,
                        const void *argValue,
                        size_t argSize,
                        GraphicsAllocation *argSvmAlloc = nullptr,
                        cl_mem_flags argSvmFlags = 0);
    const void *getKernelArg(uint32_t argIndex) const;
    const SimpleKernelArgInfo &getKernelArgInfo(uint32_t argIndex) const;

    bool getAllowNonUniform() const { return program->getAllowNonUniform(); }
    bool isVmeKernel() const { return kernelInfo.isVmeWorkload; }
    bool requiresSpecialPipelineSelectMode() const { return specialPipelineSelectMode; }

    //residency for kernel surfaces
    MOCKABLE_VIRTUAL void makeResident(CommandStreamReceiver &commandStreamReceiver);
    MOCKABLE_VIRTUAL void getResidency(std::vector<Surface *> &dst);
    bool requiresCoherency();
    void resetSharedObjectsPatchAddresses();
    bool isUsingSharedObjArgs() const { return usingSharedObjArgs; }
    bool hasUncacheableStatelessArgs() const { return statelessUncacheableArgsCount > 0; }

    bool hasPrintfOutput() const;

    void setReflectionSurfaceBlockBtOffset(uint32_t blockID, uint32_t offset);

    cl_int checkCorrectImageAccessQualifier(cl_uint argIndex,
                                            size_t argSize,
                                            const void *argValue) const;

    uint32_t *globalWorkOffsetX;
    uint32_t *globalWorkOffsetY;
    uint32_t *globalWorkOffsetZ;

    uint32_t *localWorkSizeX;
    uint32_t *localWorkSizeY;
    uint32_t *localWorkSizeZ;

    uint32_t *localWorkSizeX2;
    uint32_t *localWorkSizeY2;
    uint32_t *localWorkSizeZ2;

    uint32_t *globalWorkSizeX;
    uint32_t *globalWorkSizeY;
    uint32_t *globalWorkSizeZ;

    uint32_t *enqueuedLocalWorkSizeX;
    uint32_t *enqueuedLocalWorkSizeY;
    uint32_t *enqueuedLocalWorkSizeZ;

    uint32_t *numWorkGroupsX;
    uint32_t *numWorkGroupsY;
    uint32_t *numWorkGroupsZ;

    uint32_t *maxWorkGroupSizeForCrossThreadData;
    uint32_t maxKernelWorkGroupSize = 0;
    uint32_t *workDim;
    uint32_t *dataParameterSimdSize;
    uint32_t *parentEventOffset;
    uint32_t *preferredWkgMultipleOffset;

    static uint32_t dummyPatchLocation;

    std::vector<size_t> slmSizes;

    uint32_t allBufferArgsStateful = CL_TRUE;

    uint32_t slmTotalSize;
    bool isBuiltIn;
    const bool isParentKernel;
    const bool isSchedulerKernel;

    template <typename GfxFamily>
    uint32_t getThreadArbitrationPolicy() {
        if (kernelInfo.patchInfo.executionEnvironment->SubgroupIndependentForwardProgressRequired) {
            return PreambleHelper<GfxFamily>::getDefaultThreadArbitrationPolicy();
        } else {
            return ThreadArbitrationPolicy::AgeBased;
        }
    }
    bool checkIfIsParentKernelAndBlocksUsesPrintf();

    bool is32Bit() const {
        return kernelInfo.gpuPointerSize == 4;
    }

    int32_t getDebugSurfaceBti() const {
        if (kernelInfo.patchInfo.pAllocateSystemThreadSurface) {
            return kernelInfo.patchInfo.pAllocateSystemThreadSurface->BTI;
        }
        return -1;
    }

    size_t getPerThreadSystemThreadSurfaceSize() const {
        if (kernelInfo.patchInfo.pAllocateSystemThreadSurface) {
            return kernelInfo.patchInfo.pAllocateSystemThreadSurface->PerThreadSystemThreadSurfaceSize;
        }
        return 0;
    }

    std::vector<PatchInfoData> &getPatchInfoDataList() { return patchInfoDataList; };
    bool usesOnlyImages() const {
        return usingImagesOnly;
    }

    void fillWithBuffersForAuxTranslation(MemObjsForAuxTranslation &memObjsForAuxTranslation);

    MOCKABLE_VIRTUAL bool requiresCacheFlushCommand(const CommandQueue &commandQueue) const;

    using CacheFlushAllocationsVec = StackVec<GraphicsAllocation *, 32>;
    void getAllocationsForCacheFlush(CacheFlushAllocationsVec &out) const;

    void setAuxTranslationDirection(AuxTranslationDirection auxTranslationDirection) {
        this->auxTranslationDirection = auxTranslationDirection;
    }
    void setUnifiedMemorySyncRequirement(bool isUnifiedMemorySyncRequired) {
        this->isUnifiedMemorySyncRequired = isUnifiedMemorySyncRequired;
    }
    void setUnifiedMemoryProperty(cl_kernel_exec_info infoType, bool infoValue);
    void setUnifiedMemoryExecInfo(GraphicsAllocation *argValue);
    void clearUnifiedMemoryExecInfo();

    bool areStatelessWritesUsed() { return containsStatelessWrites; }

    uint32_t getMaxWorkGroupCount(const cl_uint workDim, const size_t *localWorkSize) const;

  protected:
    struct ObjectCounts {
        uint32_t imageCount;
        uint32_t samplerCount;
    };

    class ReflectionSurfaceHelper {
      public:
        static const uint64_t undefinedOffset = (uint64_t)-1;

        static void setKernelDataHeader(void *reflectionSurface, uint32_t numberOfBlocks,
                                        uint32_t parentImages, uint32_t parentSamplers,
                                        uint32_t imageOffset, uint32_t samplerOffset) {
            IGIL_KernelDataHeader *kernelDataHeader = reinterpret_cast<IGIL_KernelDataHeader *>(reflectionSurface);
            kernelDataHeader->m_numberOfKernels = numberOfBlocks;
            kernelDataHeader->m_ParentKernelImageCount = parentImages;
            kernelDataHeader->m_ParentSamplerCount = parentSamplers;
            kernelDataHeader->m_ParentImageDataOffset = imageOffset;
            kernelDataHeader->m_ParentSamplerParamsOffset = samplerOffset;
        }

        static uint32_t setKernelData(void *reflectionSurface, uint32_t offset,
                                      std::vector<IGIL_KernelCurbeParams> &curbeParamsIn,
                                      uint64_t tokenMaskIn, size_t maxConstantBufferSize,
                                      size_t samplerCount, const KernelInfo &kernelInfo,
                                      const HardwareInfo &hwInfo);

        static void setKernelAddressData(void *reflectionSurface, uint32_t offset,
                                         uint32_t kernelDataOffset, uint32_t samplerHeapOffset,
                                         uint32_t constantBufferOffset, uint32_t samplerParamsOffset,
                                         uint32_t sshTokensOffset, uint32_t btOffset,
                                         const KernelInfo &kernelInfo, const HardwareInfo &hwInfo);

        static void getCurbeParams(std::vector<IGIL_KernelCurbeParams> &curbeParamsOut,
                                   uint64_t &tokenMaskOut, uint32_t &firstSSHTokenIndex,
                                   const KernelInfo &kernelInfo, const HardwareInfo &hwInfo);

        static bool compareFunction(IGIL_KernelCurbeParams argFirst, IGIL_KernelCurbeParams argSecond) {
            if (argFirst.m_parameterType == argSecond.m_parameterType) {
                if (argFirst.m_parameterType == iOpenCL::DATA_PARAMETER_LOCAL_WORK_SIZE) {
                    return argFirst.m_patchOffset < argSecond.m_patchOffset;
                } else {
                    return argFirst.m_sourceOffset < argSecond.m_sourceOffset;
                }
            } else {
                return argFirst.m_parameterType < argSecond.m_parameterType;
            }
        }

        static void setKernelAddressDataBtOffset(void *reflectionSurface, uint32_t blockID, uint32_t btOffset);

        static void setParentImageParams(void *reflectionSurface, std::vector<Kernel::SimpleKernelArgInfo> &parentArguments, const KernelInfo &parentKernelInfo);
        static void setParentSamplerParams(void *reflectionSurface, std::vector<Kernel::SimpleKernelArgInfo> &parentArguments, const KernelInfo &parentKernelInfo);

        template <bool mockable = false>
        static void patchBlocksCurbe(void *reflectionSurface, uint32_t blockID,
                                     uint64_t defaultDeviceQueueCurbeOffset, uint32_t patchSizeDefaultQueue, uint64_t defaultDeviceQueueGpuAddress,
                                     uint64_t eventPoolCurbeOffset, uint32_t patchSizeEventPool, uint64_t eventPoolGpuAddress,
                                     uint64_t deviceQueueCurbeOffset, uint32_t patchSizeDeviceQueue, uint64_t deviceQueueGpuAddress,
                                     uint64_t printfBufferOffset, uint32_t printfBufferSize, uint64_t printfBufferGpuAddress,
                                     uint64_t privateSurfaceOffset, uint32_t privateSurfaceSize, uint64_t privateSurfaceGpuAddress);

        static void patchBlocksCurbeWithConstantValues(void *reflectionSurface, uint32_t blockID,
                                                       uint64_t globalMemoryCurbeOffset, uint32_t globalMemoryPatchSize, uint64_t globalMemoryGpuAddress,
                                                       uint64_t constantMemoryCurbeOffset, uint32_t constantMemoryPatchSize, uint64_t constantMemoryGpuAddress,
                                                       uint64_t privateMemoryCurbeOffset, uint32_t privateMemoryPatchSize, uint64_t privateMemoryGpuAddress);
    };

    void
    makeArgsResident(CommandStreamReceiver &commandStreamReceiver);

    void *patchBufferOffset(const KernelArgInfo &argInfo, void *svmPtr, GraphicsAllocation *svmAlloc);

    // Sets-up both crossThreadData and ssh for given implicit (private/constant, etc.) allocation
    template <typename PatchTokenT>
    void patchWithImplicitSurface(void *ptrToPatchInCrossThreadData, GraphicsAllocation &allocation, const PatchTokenT &patch);

    void getParentObjectCounts(ObjectCounts &objectCount);
    Kernel(Program *programArg, const KernelInfo &kernelInfoArg, const Device &deviceArg, bool schedulerKernel = false);
    void provideInitializationHints();

    void patchBlocksCurbeWithConstantValues();

    void resolveArgs();

    void reconfigureKernel();

    void addAllocationToCacheFlushVector(uint32_t argIndex, GraphicsAllocation *argAllocation);
    bool allocationForCacheFlush(GraphicsAllocation *argAllocation) const;
    Program *program;
    Context *context;
    const Device &device;
    const KernelInfo &kernelInfo;

    std::vector<SimpleKernelArgInfo> kernelArguments;
    std::vector<KernelArgHandler> kernelArgHandlers;
    std::vector<GraphicsAllocation *> kernelSvmGfxAllocations;
    std::vector<GraphicsAllocation *> kernelUnifiedMemoryGfxAllocations;

    AuxTranslationDirection auxTranslationDirection = AuxTranslationDirection::None;

    size_t numberOfBindingTableStates;
    size_t localBindingTableOffset;
    std::unique_ptr<char[]> pSshLocal;
    uint32_t sshLocalSize;

    char *crossThreadData;
    uint32_t crossThreadDataSize;

    GraphicsAllocation *privateSurface;
    uint64_t privateSurfaceSize;

    GraphicsAllocation *kernelReflectionSurface;

    bool usingSharedObjArgs;
    bool usingImagesOnly = false;
    bool auxTranslationRequired = false;
    bool containsStatelessWrites = true;
    uint32_t patchedArgumentsNum = 0;
    uint32_t startOffset = 0;
    uint32_t statelessUncacheableArgsCount = 0;

    std::vector<PatchInfoData> patchInfoDataList;
    std::unique_ptr<ImageTransformer> imageTransformer;

    bool specialPipelineSelectMode = false;
    bool svmAllocationsRequireCacheFlush = false;
    std::vector<GraphicsAllocation *> kernelArgRequiresCacheFlush;
    UnifiedMemoryControls unifiedMemoryControls;
    bool isUnifiedMemorySyncRequired = true;
};
} // namespace NEO
