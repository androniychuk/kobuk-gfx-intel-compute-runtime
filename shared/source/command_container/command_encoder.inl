/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once
#include "shared/source/command_container/command_encoder.h"
#include "shared/source/command_stream/linear_stream.h"
#include "shared/source/device/device.h"
#include "shared/source/execution_environment/execution_environment.h"
#include "shared/source/gmm_helper/gmm_helper.h"
#include "shared/source/helpers/hw_helper.h"
#include "shared/source/helpers/preamble.h"
#include "shared/source/helpers/register_offsets.h"
#include "shared/source/helpers/simd_helper.h"
#include "shared/source/helpers/string.h"
#include "shared/source/kernel/dispatch_kernel_encoder_interface.h"

#include "opencl/source/helpers/hardware_commands_helper.h"

#include <algorithm>

namespace NEO {

template <typename Family>
uint32_t EncodeStates<Family>::copySamplerState(IndirectHeap *dsh,
                                                uint32_t samplerStateOffset,
                                                uint32_t samplerCount,
                                                uint32_t borderColorOffset,
                                                const void *fnDynamicStateHeap) {
    auto sizeSamplerState = sizeof(SAMPLER_STATE) * samplerCount;
    auto borderColorSize = samplerStateOffset - borderColorOffset;

    dsh->align(alignIndirectStatePointer);
    auto borderColorOffsetInDsh = static_cast<uint32_t>(dsh->getUsed());

    auto borderColor = dsh->getSpace(borderColorSize);

    memcpy_s(borderColor, borderColorSize, ptrOffset(fnDynamicStateHeap, borderColorOffset),
             borderColorSize);

    dsh->align(INTERFACE_DESCRIPTOR_DATA::SAMPLERSTATEPOINTER_ALIGN_SIZE);
    auto samplerStateOffsetInDsh = static_cast<uint32_t>(dsh->getUsed());

    auto dstSamplerState = reinterpret_cast<SAMPLER_STATE *>(dsh->getSpace(sizeSamplerState));

    auto srcSamplerState = reinterpret_cast<const SAMPLER_STATE *>(ptrOffset(fnDynamicStateHeap, samplerStateOffset));
    SAMPLER_STATE state = {};
    for (uint32_t i = 0; i < samplerCount; i++) {
        state = srcSamplerState[i];
        state.setIndirectStatePointer(static_cast<uint32_t>(borderColorOffsetInDsh));
        dstSamplerState[i] = state;
    }

    return samplerStateOffsetInDsh;
}
template <typename Family>
void EncodeMathMMIO<Family>::encodeMulRegVal(CommandContainer &container, uint32_t offset, uint32_t val, uint64_t dstAddress) {
    int logLws = 0;
    int i = val;
    while (val >> logLws) {
        logLws++;
    }

    EncodeSetMMIO<Family>::encodeREG(container, CS_GPR_R0, offset);
    EncodeSetMMIO<Family>::encodeIMM(container, CS_GPR_R1, 0);

    i = 0;
    while (i < logLws) {
        if (val & (1 << i)) {
            EncodeMath<Family>::addition(container, AluRegisters::R_1,
                                         AluRegisters::R_0, AluRegisters::R_2);
            EncodeSetMMIO<Family>::encodeREG(container, CS_GPR_R1, CS_GPR_R2);
        }
        EncodeMath<Family>::addition(container, AluRegisters::R_0,
                                     AluRegisters::R_0, AluRegisters::R_2);
        EncodeSetMMIO<Family>::encodeREG(container, CS_GPR_R0, CS_GPR_R2);
        i++;
    }
    EncodeStoreMMIO<Family>::encode(*container.getCommandStream(), CS_GPR_R1, dstAddress);
}

/*
 * Compute *firstOperand > secondOperand and store the result in
 * MI_PREDICATE_RESULT where  firstOperand is an device memory address.
 *
 * To calculate the "greater than" operation in the device,
 * (secondOperand - *firstOperand) is used, and if the carry flag register is
 * set, then (*firstOperand) is greater than secondOperand.
 */
template <typename Family>
void EncodeMathMMIO<Family>::encodeGreaterThanPredicate(CommandContainer &container, uint64_t firstOperand, uint32_t secondOperand) {
    EncodeSetMMIO<Family>::encodeMEM(container, CS_GPR_R0, firstOperand);
    EncodeSetMMIO<Family>::encodeIMM(container, CS_GPR_R1, secondOperand);

    /* CS_GPR_R* registers map to AluRegisters::R_* registers */
    EncodeMath<Family>::greaterThan(container, AluRegisters::R_0,
                                    AluRegisters::R_1, AluRegisters::R_2);

    EncodeSetMMIO<Family>::encodeREG(container, CS_PREDICATE_RESULT, CS_GPR_R2);
}

/*
 * encodeAlu() performs operations that leave a state including the result of
 * an operation such as the carry flag, and the accu flag with subtraction and
 * addition result.
 *
 * Parameter "postOperationStateRegister" is the ALU register with the result
 * from the operation that the function caller is interested in obtaining.
 *
 * Parameter "finalResultRegister" is the final destination register where
 * data from "postOperationStateRegister" will be copied.
 */
template <typename Family>
void EncodeMathMMIO<Family>::encodeAlu(MI_MATH_ALU_INST_INLINE *pAluParam, AluRegisters srcA, AluRegisters srcB, AluRegisters op, AluRegisters finalResultRegister, AluRegisters postOperationStateRegister) {
    MI_MATH_ALU_INST_INLINE aluParam;

    aluParam.DW0.Value = 0x0;
    aluParam.DW0.BitField.ALUOpcode = static_cast<uint32_t>(AluRegisters::OPCODE_LOAD);
    aluParam.DW0.BitField.Operand1 = static_cast<uint32_t>(AluRegisters::R_SRCA);
    aluParam.DW0.BitField.Operand2 = static_cast<uint32_t>(srcA);
    *pAluParam = aluParam;
    pAluParam++;

    aluParam.DW0.Value = 0x0;
    aluParam.DW0.BitField.ALUOpcode = static_cast<uint32_t>(AluRegisters::OPCODE_LOAD);
    aluParam.DW0.BitField.Operand1 = static_cast<uint32_t>(AluRegisters::R_SRCB);
    aluParam.DW0.BitField.Operand2 = static_cast<uint32_t>(srcB);
    *pAluParam = aluParam;
    pAluParam++;

    /* Order of operation: Operand1 <ALUOpcode> Operand2 */
    aluParam.DW0.Value = 0x0;
    aluParam.DW0.BitField.ALUOpcode = static_cast<uint32_t>(op);
    aluParam.DW0.BitField.Operand1 = 0;
    aluParam.DW0.BitField.Operand2 = 0;
    *pAluParam = aluParam;
    pAluParam++;

    aluParam.DW0.Value = 0x0;
    aluParam.DW0.BitField.ALUOpcode = static_cast<uint32_t>(AluRegisters::OPCODE_STORE);
    aluParam.DW0.BitField.Operand1 = static_cast<uint32_t>(finalResultRegister);
    aluParam.DW0.BitField.Operand2 = static_cast<uint32_t>(postOperationStateRegister);
    *pAluParam = aluParam;
    pAluParam++;
}

template <typename Family>
uint32_t *EncodeMath<Family>::commandReserve(CommandContainer &container) {
    size_t size = sizeof(MI_MATH) + sizeof(MI_MATH_ALU_INST_INLINE) * NUM_ALU_INST_FOR_READ_MODIFY_WRITE;

    auto cmd = reinterpret_cast<uint32_t *>(container.getCommandStream()->getSpace(size));
    MI_MATH mathBuffer;
    mathBuffer.DW0.Value = 0x0;
    mathBuffer.DW0.BitField.InstructionType = MI_MATH::COMMAND_TYPE_MI_COMMAND;
    mathBuffer.DW0.BitField.InstructionOpcode = MI_MATH::MI_COMMAND_OPCODE_MI_MATH;
    mathBuffer.DW0.BitField.DwordLength = NUM_ALU_INST_FOR_READ_MODIFY_WRITE - 1;
    *reinterpret_cast<MI_MATH *>(cmd) = mathBuffer;
    cmd++;

    return cmd;
}

template <typename Family>
void EncodeMathMMIO<Family>::encodeAluAdd(MI_MATH_ALU_INST_INLINE *pAluParam,
                                          AluRegisters firstOperandRegister,
                                          AluRegisters secondOperandRegister,
                                          AluRegisters finalResultRegister) {
    encodeAlu(pAluParam, firstOperandRegister, secondOperandRegister, AluRegisters::OPCODE_ADD, finalResultRegister, AluRegisters::R_ACCU);
}

template <typename Family>
void EncodeMathMMIO<Family>::encodeAluSubStoreCarry(MI_MATH_ALU_INST_INLINE *pAluParam, AluRegisters regA, AluRegisters regB, AluRegisters finalResultRegister) {
    /* regB is subtracted from regA */
    encodeAlu(pAluParam, regA, regB, AluRegisters::OPCODE_SUB, finalResultRegister, AluRegisters::R_CF);
}

/*
 * greaterThan() tests if firstOperandRegister is greater than
 * secondOperandRegister.
 */
template <typename Family>
void EncodeMath<Family>::greaterThan(CommandContainer &container,
                                     AluRegisters firstOperandRegister,
                                     AluRegisters secondOperandRegister,
                                     AluRegisters finalResultRegister) {
    uint32_t *cmd = EncodeMath<Family>::commandReserve(container);

    /* firstOperandRegister will be subtracted from secondOperandRegister */
    EncodeMathMMIO<Family>::encodeAluSubStoreCarry(reinterpret_cast<MI_MATH_ALU_INST_INLINE *>(cmd),
                                                   secondOperandRegister,
                                                   firstOperandRegister,
                                                   finalResultRegister);
}

template <typename Family>
void EncodeMath<Family>::addition(CommandContainer &container,
                                  AluRegisters firstOperandRegister,
                                  AluRegisters secondOperandRegister,
                                  AluRegisters finalResultRegister) {
    uint32_t *cmd = EncodeMath<Family>::commandReserve(container);

    EncodeMathMMIO<Family>::encodeAluAdd(reinterpret_cast<MI_MATH_ALU_INST_INLINE *>(cmd),
                                         firstOperandRegister,
                                         secondOperandRegister,
                                         finalResultRegister);
}

template <typename Family>
void EncodeIndirectParams<Family>::setGroupCountIndirect(CommandContainer &container, const NEO::CrossThreadDataOffset offsets[3], void *crossThreadAddress) {
    for (int i = 0; i < 3; ++i) {
        if (NEO::isUndefinedOffset(offsets[i])) {
            continue;
        }
        EncodeStoreMMIO<Family>::encode(*container.getCommandStream(), GPUGPU_DISPATCHDIM[i], ptrOffset(reinterpret_cast<uint64_t>(crossThreadAddress), offsets[i]));
    }
}

template <typename Family>
void EncodeIndirectParams<Family>::setGlobalWorkSizeIndirect(CommandContainer &container, const NEO::CrossThreadDataOffset offsets[3], void *crossThreadAddress, const uint32_t *lws) {
    for (int i = 0; i < 3; ++i) {
        if (NEO::isUndefinedOffset(offsets[i])) {
            continue;
        }
        EncodeMathMMIO<Family>::encodeMulRegVal(container, GPUGPU_DISPATCHDIM[i], lws[i], ptrOffset(reinterpret_cast<uint64_t>(crossThreadAddress), offsets[i]));
    }
}

template <typename Family>
void EncodeSetMMIO<Family>::encodeIMM(CommandContainer &container, uint32_t offset, uint32_t data) {
    MI_LOAD_REGISTER_IMM cmd = Family::cmdInitLoadRegisterImm;
    cmd.setRegisterOffset(offset);
    cmd.setDataDword(data);
    auto buffer = container.getCommandStream()->getSpaceForCmd<MI_LOAD_REGISTER_IMM>();
    *buffer = cmd;
}

template <typename Family>
void EncodeSetMMIO<Family>::encodeMEM(CommandContainer &container, uint32_t offset, uint64_t address) {
    MI_LOAD_REGISTER_MEM cmd = Family::cmdInitLoadRegisterMem;
    cmd.setRegisterAddress(offset);
    cmd.setMemoryAddress(address);
    auto buffer = container.getCommandStream()->getSpaceForCmd<MI_LOAD_REGISTER_MEM>();
    *buffer = cmd;
}

template <typename Family>
void EncodeSetMMIO<Family>::encodeREG(CommandContainer &container, uint32_t dstOffset, uint32_t srcOffset) {
    MI_LOAD_REGISTER_REG cmd = Family::cmdInitLoadRegisterReg;
    cmd.setSourceRegisterAddress(srcOffset);
    cmd.setDestinationRegisterAddress(dstOffset);
    auto buffer = container.getCommandStream()->getSpaceForCmd<MI_LOAD_REGISTER_REG>();
    *buffer = cmd;
}

template <typename Family>
void EncodeStoreMMIO<Family>::encode(LinearStream &csr, uint32_t offset, uint64_t address) {
    MI_STORE_REGISTER_MEM cmd = Family::cmdInitStoreRegisterMem;
    cmd.setRegisterAddress(offset);
    cmd.setMemoryAddress(address);
    remapOffset(&cmd);
    auto buffer = csr.getSpaceForCmd<MI_STORE_REGISTER_MEM>();
    *buffer = cmd;
}

template <typename Family>
void EncodeSurfaceState<Family>::encodeBuffer(void *dst, uint64_t address, size_t size, uint32_t mocs,
                                              bool cpuCoherent) {
    auto ss = reinterpret_cast<R_SURFACE_STATE *>(dst);
    UNRECOVERABLE_IF(!isAligned<getSurfaceBaseAddressAlignment()>(size));

    SURFACE_STATE_BUFFER_LENGTH Length = {0};
    Length.Length = static_cast<uint32_t>(size - 1);

    ss->setWidth(Length.SurfaceState.Width + 1);
    ss->setHeight(Length.SurfaceState.Height + 1);
    ss->setDepth(Length.SurfaceState.Depth + 1);

    ss->setSurfaceType((address != 0) ? R_SURFACE_STATE::SURFACE_TYPE_SURFTYPE_BUFFER
                                      : R_SURFACE_STATE::SURFACE_TYPE_SURFTYPE_NULL);
    ss->setSurfaceFormat(SURFACE_FORMAT::SURFACE_FORMAT_RAW);
    ss->setSurfaceVerticalAlignment(R_SURFACE_STATE::SURFACE_VERTICAL_ALIGNMENT_VALIGN_4);
    ss->setSurfaceHorizontalAlignment(R_SURFACE_STATE::SURFACE_HORIZONTAL_ALIGNMENT_HALIGN_4);

    ss->setTileMode(R_SURFACE_STATE::TILE_MODE_LINEAR);
    ss->setVerticalLineStride(0);
    ss->setVerticalLineStrideOffset(0);
    ss->setMemoryObjectControlState(mocs);
    ss->setSurfaceBaseAddress(address);

    ss->setCoherencyType(cpuCoherent ? R_SURFACE_STATE::COHERENCY_TYPE_IA_COHERENT
                                     : R_SURFACE_STATE::COHERENCY_TYPE_GPU_COHERENT);
    ss->setAuxiliarySurfaceMode(AUXILIARY_SURFACE_MODE::AUXILIARY_SURFACE_MODE_AUX_NONE);
}
template <typename Family>
void EncodeSurfaceState<Family>::encodeExtraBufferParams(GraphicsAllocation *allocation, GmmHelper *gmmHelper, void *memory, bool forceNonAuxMode, bool isReadOnlyArgument) {
    using RENDER_SURFACE_STATE = typename Family::RENDER_SURFACE_STATE;
    using AUXILIARY_SURFACE_MODE = typename RENDER_SURFACE_STATE::AUXILIARY_SURFACE_MODE;

    auto surfaceState = reinterpret_cast<RENDER_SURFACE_STATE *>(memory);
    Gmm *gmm = allocation ? allocation->getDefaultGmm() : nullptr;

    if (gmm && gmm->isRenderCompressed && !forceNonAuxMode &&
        GraphicsAllocation::AllocationType::BUFFER_COMPRESSED == allocation->getAllocationType()) {
        // Its expected to not program pitch/qpitch/baseAddress for Aux surface in CCS scenarios
        surfaceState->setCoherencyType(RENDER_SURFACE_STATE::COHERENCY_TYPE_GPU_COHERENT);
        surfaceState->setAuxiliarySurfaceMode(AUXILIARY_SURFACE_MODE::AUXILIARY_SURFACE_MODE_AUX_CCS_E);
    }

    if (DebugManager.flags.DisableCachingForStatefulBufferAccess.get()) {
        surfaceState->setMemoryObjectControlState(gmmHelper->getMOCS(GMM_RESOURCE_USAGE_OCL_BUFFER_CACHELINE_MISALIGNED));
    }
}
template <typename Family>
void *EncodeDispatchKernel<Family>::getInterfaceDescriptor(CommandContainer &container, uint32_t &iddOffset) {

    if (container.nextIddInBlock == container.getNumIddPerBlock()) {
        container.getIndirectHeap(HeapType::DYNAMIC_STATE)->align(HardwareCommandsHelper<Family>::alignInterfaceDescriptorData);
        container.setIddBlock(container.getHeapSpaceAllowGrow(HeapType::DYNAMIC_STATE,
                                                              sizeof(INTERFACE_DESCRIPTOR_DATA) * container.getNumIddPerBlock()));
        container.nextIddInBlock = 0;

        EncodeMediaInterfaceDescriptorLoad<Family>::encode(container);
    }

    iddOffset = container.nextIddInBlock;
    auto interfaceDescriptorData = static_cast<INTERFACE_DESCRIPTOR_DATA *>(container.getIddBlock());
    return &interfaceDescriptorData[container.nextIddInBlock++];
}

template <typename Family>
void EncodeDispatchKernel<Family>::patchBindlessSurfaceStateOffsets(const size_t sshOffset, const KernelDescriptor &kernelDesc, uint8_t *crossThread) {
    auto &hwHelper = HwHelperHw<Family>::get();

    for (const auto &argT : kernelDesc.payloadMappings.explicitArgs) {
        CrossThreadDataOffset bindless = undefined<CrossThreadDataOffset>;
        SurfaceStateHeapOffset bindful = undefined<SurfaceStateHeapOffset>;

        switch (argT.type) {
        case ArgDescriptor::ArgTPointer: {
            auto &arg = argT.as<NEO::ArgDescPointer>();
            bindless = arg.bindless;
            bindful = arg.bindful;
        } break;

        case ArgDescriptor::ArgTImage: {
            auto &arg = argT.as<NEO::ArgDescImage>();
            bindless = arg.bindless;
            bindful = arg.bindful;
        } break;

        default:
            break;
        }

        if (NEO::isValidOffset(bindless)) {
            auto patchLocation = ptrOffset(crossThread, bindless);
            auto bindlessOffset = static_cast<uint32_t>(sshOffset) + bindful;
            auto patchValue = hwHelper.getBindlessSurfaceExtendedMessageDescriptorValue(bindlessOffset);
            patchWithRequiredSize(patchLocation, sizeof(patchValue), patchValue);
        }
    }
}

template <typename Family>
bool EncodeDispatchKernel<Family>::inlineDataProgrammingRequired(const KernelDescriptor &kernelDesc) {
    auto checkKernelForInlineData = true;
    if (DebugManager.flags.EnablePassInlineData.get() != -1) {
        checkKernelForInlineData = !!DebugManager.flags.EnablePassInlineData.get();
    }
    if (checkKernelForInlineData) {
        return kernelDesc.kernelAttributes.flags.passInlineData;
    }
    return false;
}

template <typename Family>
size_t EncodeStates<Family>::getAdjustStateComputeModeSize() {
    return 0;
}

template <typename Family>
size_t EncodeIndirectParams<Family>::getCmdsSizeForIndirectParams() {
    return 3 * sizeof(typename Family::MI_LOAD_REGISTER_MEM);
}

template <typename Family>
size_t EncodeIndirectParams<Family>::getCmdsSizeForSetGroupCountIndirect() {
    return 3 * (sizeof(MI_STORE_REGISTER_MEM));
}

template <typename Family>
size_t EncodeIndirectParams<Family>::getCmdsSizeForSetGroupSizeIndirect() {
    return 3 * (sizeof(MI_LOAD_REGISTER_REG) + sizeof(MI_LOAD_REGISTER_IMM) + sizeof(MI_MATH) + sizeof(MI_MATH_ALU_INST_INLINE) + sizeof(MI_STORE_REGISTER_MEM));
}

template <typename Family>
void EncodeSempahore<Family>::programMiSemaphoreWait(MI_SEMAPHORE_WAIT *cmd,
                                                     uint64_t compareAddress,
                                                     uint32_t compareData,
                                                     COMPARE_OPERATION compareMode) {
    MI_SEMAPHORE_WAIT localCmd = Family::cmdInitMiSemaphoreWait;
    localCmd.setCompareOperation(compareMode);
    localCmd.setSemaphoreDataDword(compareData);
    localCmd.setSemaphoreGraphicsAddress(compareAddress);
    localCmd.setWaitMode(MI_SEMAPHORE_WAIT::WAIT_MODE::WAIT_MODE_POLLING_MODE);

    *cmd = localCmd;
}

template <typename Family>
void EncodeSempahore<Family>::addMiSemaphoreWaitCommand(LinearStream &commandStream,
                                                        uint64_t compareAddress,
                                                        uint32_t compareData,
                                                        COMPARE_OPERATION compareMode) {
    auto semaphoreCommand = commandStream.getSpaceForCmd<MI_SEMAPHORE_WAIT>();
    programMiSemaphoreWait(semaphoreCommand,
                           compareAddress,
                           compareData,
                           compareMode);
}

template <typename Family>
size_t EncodeSempahore<Family>::getSizeMiSemaphoreWait() {
    return sizeof(MI_SEMAPHORE_WAIT);
}

template <typename Family>
void EncodeAtomic<Family>::programMiAtomic(MI_ATOMIC *atomic, uint64_t writeAddress,
                                           ATOMIC_OPCODES opcode,
                                           DATA_SIZE dataSize) {
    MI_ATOMIC cmd = Family::cmdInitAtomic;
    cmd.setAtomicOpcode(opcode);
    cmd.setDataSize(dataSize);
    cmd.setMemoryAddress(static_cast<uint32_t>(writeAddress & 0x0000FFFFFFFFULL));
    cmd.setMemoryAddressHigh(static_cast<uint32_t>(writeAddress >> 32));

    *atomic = cmd;
}

template <typename Family>
void EncodeBatchBufferStartOrEnd<Family>::programBatchBufferStart(LinearStream *commandStream,
                                                                  uint64_t address,
                                                                  bool secondLevel) {
    MI_BATCH_BUFFER_START cmd = Family::cmdInitBatchBufferStart;
    if (secondLevel) {
        cmd.setSecondLevelBatchBuffer(MI_BATCH_BUFFER_START::SECOND_LEVEL_BATCH_BUFFER_SECOND_LEVEL_BATCH);
    }
    cmd.setAddressSpaceIndicator(MI_BATCH_BUFFER_START::ADDRESS_SPACE_INDICATOR_PPGTT);
    cmd.setBatchBufferStartAddressGraphicsaddress472(address);
    auto buffer = commandStream->getSpaceForCmd<MI_BATCH_BUFFER_START>();
    *buffer = cmd;
}

template <typename Family>
void EncodeBatchBufferStartOrEnd<Family>::programBatchBufferEnd(CommandContainer &container) {
    MI_BATCH_BUFFER_END cmd = Family::cmdInitBatchBufferEnd;
    auto buffer = container.getCommandStream()->getSpaceForCmd<MI_BATCH_BUFFER_END>();
    *buffer = cmd;
}

template <typename Family>
void EncodeSurfaceState<Family>::getSshAlignedPointer(uintptr_t &ptr, size_t &offset) {
    auto sshAlignmentMask =
        getSurfaceBaseAddressAlignmentMask();
    uintptr_t alignedPtr = ptr & sshAlignmentMask;

    offset = 0;
    if (ptr != alignedPtr) {
        offset = ptrDiff(ptr, alignedPtr);
        ptr = alignedPtr;
    }
}

template <typename GfxFamily>
void EncodeMiFlushDW<GfxFamily>::programMiFlushDw(LinearStream &commandStream, uint64_t immediateDataGpuAddress, uint64_t immediateData, bool timeStampOperation, bool commandWithPostSync) {
    programMiFlushDwWA(commandStream);

    auto miFlushDwCmd = commandStream.getSpaceForCmd<MI_FLUSH_DW>();
    MI_FLUSH_DW miFlush = GfxFamily::cmdInitMiFlushDw;
    if (commandWithPostSync) {
        auto postSyncType = timeStampOperation ? MI_FLUSH_DW::POST_SYNC_OPERATION_WRITE_TIMESTAMP_REGISTER : MI_FLUSH_DW::POST_SYNC_OPERATION_WRITE_IMMEDIATE_DATA_QWORD;
        miFlush.setPostSyncOperation(postSyncType);
        miFlush.setDestinationAddress(immediateDataGpuAddress);
        miFlush.setImmediateData(immediateData);
    }
    appendMiFlushDw(&miFlush);
    *miFlushDwCmd = miFlush;
}

template <typename GfxFamily>
size_t EncodeMiFlushDW<GfxFamily>::getMiFlushDwCmdSizeForDataWrite() {
    return sizeof(typename GfxFamily::MI_FLUSH_DW) + EncodeMiFlushDW<GfxFamily>::getMiFlushDwWaSize();
}

template <typename GfxFamily>
void EncodeMemoryPrefetch<GfxFamily>::programMemoryPrefetch(LinearStream &commandStream, const GraphicsAllocation &graphicsAllocation, uint32_t size, const HardwareInfo &hwInfo) {}

template <typename GfxFamily>
size_t EncodeMemoryPrefetch<GfxFamily>::getSizeForMemoryPrefetch() { return 0; }

} // namespace NEO
