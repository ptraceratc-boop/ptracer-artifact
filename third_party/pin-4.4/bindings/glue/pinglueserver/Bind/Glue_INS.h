/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_INS_H_
#define _GLUE_INS_H_

#include "../gluepy.h"

namespace glue
{
enum class E_INS_Function
{
    Address,
    AddressSizePrefix,
    BranchNotTakenPrefix,
    BranchTakenPrefix,
    CATEGORY_StringShort,
    Category,
    ChangeReg,
    Delete,
    DirectControlFlowTargetAddress,
    Disassemble,
    EffectiveAddressWidth,
    EXTENSION_StringShort,
    Extension,
    FullRegRContain,
    FullRegWContain,
    GetFarPointer,
    GetNumberAndSizeOfMemAccesses,
    GetPredicate,
    HasExplicitMemoryReference,
    HasFallThrough,
    HasMemoryRead2,
    HasMemoryVector,
    HasRealRep,
    HasScatteredMemoryAccess,
    InsertDirectJump,
    InsertIndirectJump,
    Invalid,
    IsAddedForFunctionReplacement,
    IsAmx,
    IsAtomicUpdate,
    IsBranch,
    IsCacheLineFlush,
    IsCall,
    IsControlFlow,
    IsDirectBranch,
    IsDirectCall,
    IsDirectControlFlow,
    IsDirectFarJump,
    IsFarCall,
    IsFarJump,
    IsFarRet,
    IsHalt,
    IsIndirectControlFlow,
    IsInterrupt,
    IsIpRelRead,
    IsIpRelWrite,
    IsIRet,
    IsLea,
    IsMaskMov,
    IsMemoryRead,
    IsMemoryWrite,
    IsMov,
    IsMovdir64b,
    IsMovFullRegRegSame,
    IsNop,
    IsOriginal,
    IsPcMaterialization,
    IsPredicated,
    IsPrefetch,
    IsProcedureCall,
    IsRDTSC,
    IsRet,
    IsStackRead,
    IsStackWrite,
    IsStandardMemop,
    IsStringop,
    IsSub,
    IsSyscall,
    IsSysenter,
    IsSysret,
    IsValidForIarg,
    IsValidForIpointAfter,
    IsValidForIpointTakenBranch,
    IsVgather,
    IsVscatter,
    IsXbegin,
    IsXchg,
    IsXend,
    LockPrefix,
    MaskRegister,
    MaxNumRRegs,
    MaxNumWRegs,
    MemoryBaseReg,
    MemoryDisplacement,
    MemoryIndexReg,
    MemoryOperandCount,
    MemoryOperandElementCount,
    MemoryOperandElementSize,
    MemoryOperandIndexToOperandIndex,
    MemoryOperandIsRead,
    MemoryOperandIsWritten,
    MemoryOperandSize,
    MemoryScale,
    Mnemonic,
    Next,
    NextAddress,
    OPCODE_StringShort,
    Opcode,
    OperandCount,
    OperandElementCount,
    OperandElementSize,
    OperandHasElements,
    OperandImmediate,
    OperandIsAddressGenerator,
    OperandIsBranchDisplacement,
    OperandIsFixedMemop,
    OperandIsImmediate,
    OperandIsImplicit,
    OperandIsMemory,
    OperandIsReg,
    OperandIsSegmentReg,
    OperandMemoryBaseReg,
    OperandMemoryDisplacement,
    OperandMemoryIndexReg,
    OperandMemoryScale,
    OperandMemorySegmentReg,
    OperandNameId,
    OperandRead,
    OperandReadAndWritten,
    OperandReadOnly,
    OperandReg,
    OperandSize,
    OperandSizePrefix,
    OperandWidth,
    OperandWritten,
    OperandWrittenOnly,
    PIN_SetSyntaxATT,
    PIN_SetSyntaxIntel,
    PIN_SetSyntaxXED,
    Prev,
    RegIsImplicit,
    RegR,
    RegRContain,
    RegW,
    RegWContain,
    RepCountRegister,
    RepnePrefix,
    RepPrefix,
    RewriteMemoryOperand,
    RewriteScatteredMemoryOperand,
    Rtn,
    SegmentPrefix,
    SegmentRegPrefix,
    SegPrefixIsMemoryRead,
    SegPrefixIsMemoryWrite,
    Size,
    Stutters,
    SyscallStd,
    Valid,
    XedDec,
    XedExactMapFromPinReg,
    XedExactMapToPinReg
};

template< E_INS_Function Function, BOOL IsInsFirstArg = TRUE > inline PyObject* ins_function(VM* vm, ArgsView args)
{
    INS ins;
    if constexpr (IsInsFirstArg)
    {
        ins.index = py_cast< INT32 >(vm, args[0]);
    }

    switch (Function)
    {
        case E_INS_Function::Address:
            return py_var(vm, INS_Address(ins));
        case E_INS_Function::AddressSizePrefix:
            return py_var(vm, INS_AddressSizePrefix(ins));
        case E_INS_Function::BranchNotTakenPrefix:
            return py_var(vm, INS_BranchNotTakenPrefix(ins));
        case E_INS_Function::BranchTakenPrefix:
            return py_var(vm, INS_BranchTakenPrefix(ins));
        case E_INS_Function::CATEGORY_StringShort:
            return py_var(vm, CATEGORY_StringShort(py_cast< UINT32 >(vm, args[0])));
        case E_INS_Function::Category:
            return py_var(vm, INS_Category(ins));
        case E_INS_Function::ChangeReg:
            return py_var(
                vm, INS_ChangeReg(ins, py_cast< REG >(vm, args[1]), py_cast< REG >(vm, args[2]), py_cast< BOOL >(vm, args[3])));
        case E_INS_Function::Delete:
            INS_Delete(ins);
            return vm->None;
        case E_INS_Function::DirectControlFlowTargetAddress:
            return py_var(vm, INS_DirectControlFlowTargetAddress(ins));
        case E_INS_Function::Disassemble:
            return py_var(vm, INS_Disassemble(ins));
        case E_INS_Function::EffectiveAddressWidth:
            return py_var(vm, INS_EffectiveAddressWidth(ins));
        case E_INS_Function::EXTENSION_StringShort:
            return py_var(vm, EXTENSION_StringShort(py_cast< UINT32 >(vm, args[0])));
        case E_INS_Function::Extension:
            return py_var(vm, INS_Extension(ins));
        case E_INS_Function::FullRegRContain:
            return py_var(vm, INS_FullRegRContain(ins, py_cast< REG >(vm, args[1])));
        case E_INS_Function::FullRegWContain:
            return py_var(vm, INS_FullRegWContain(ins, py_cast< REG >(vm, args[1])));
        case E_INS_Function::GetFarPointer:
        {
            UINT16 segment_selector;
            UINT32 displacement;
            Tuple res(2);

            INS_GetFarPointer(ins, segment_selector, displacement);

            res[0] = py_var(vm, segment_selector);
            res[1] = py_var(vm, displacement);

            return py_var(vm, res);
        }
        case E_INS_Function::GetNumberAndSizeOfMemAccesses:
        {
            int numAccesses, accessSize, indexSize;
            Tuple res(3);

            GetNumberAndSizeOfMemAccesses(ins, &numAccesses, &accessSize, &indexSize);

            res[0] = py_var(vm, numAccesses);
            res[1] = py_var(vm, accessSize);
            res[1] = py_var(vm, indexSize);

            return py_var(vm, res);
        }
        case E_INS_Function::GetPredicate:
            return py_var(vm, INS_GetPredicate(ins));
        case E_INS_Function::HasExplicitMemoryReference:
            return py_var(vm, INS_HasExplicitMemoryReference(ins));
        case E_INS_Function::HasFallThrough:
            return py_var(vm, INS_HasFallThrough(ins));
        case E_INS_Function::HasMemoryRead2:
            return py_var(vm, INS_HasMemoryRead2(ins));
        case E_INS_Function::HasMemoryVector:
            return py_var(vm, INS_HasMemoryVector(ins));
        case E_INS_Function::HasRealRep:
            return py_var(vm, INS_HasRealRep(ins));
        case E_INS_Function::HasScatteredMemoryAccess:
            return py_var(vm, INS_HasScatteredMemoryAccess(ins));
        case E_INS_Function::InsertDirectJump:
            INS_InsertDirectJump(ins, py_cast< IPOINT >(vm, args[1]), py_cast< ADDRINT >(vm, args[2]));
            return vm->None;
        case E_INS_Function::InsertIndirectJump:
            INS_InsertIndirectJump(ins, py_cast< IPOINT >(vm, args[1]), py_cast< REG >(vm, args[2]));
            return vm->None;
        case E_INS_Function::Invalid:
            return py_var(vm, INS_Invalid().index);
        case E_INS_Function::IsAddedForFunctionReplacement:
            return py_var(vm, INS_IsAddedForFunctionReplacement(ins));
        case E_INS_Function::IsAmx:
            return py_var(vm, INS_IsAmx(ins));
        case E_INS_Function::IsAtomicUpdate:
            return py_var(vm, INS_IsAtomicUpdate(ins));
        case E_INS_Function::IsBranch:
            return py_var(vm, INS_IsBranch(ins));
        case E_INS_Function::IsCacheLineFlush:
            return py_var(vm, INS_IsCacheLineFlush(ins));
        case E_INS_Function::IsCall:
            return py_var(vm, INS_IsCall(ins));
        case E_INS_Function::IsControlFlow:
            return py_var(vm, INS_IsControlFlow(ins));
        case E_INS_Function::IsDirectBranch:
            return py_var(vm, INS_IsDirectBranch(ins));
        case E_INS_Function::IsDirectCall:
            return py_var(vm, INS_IsDirectCall(ins));
        case E_INS_Function::IsDirectControlFlow:
            return py_var(vm, INS_IsDirectControlFlow(ins));
        case E_INS_Function::IsDirectFarJump:
            return py_var(vm, INS_IsDirectFarJump(ins));
        case E_INS_Function::IsFarCall:
            return py_var(vm, INS_IsFarCall(ins));
        case E_INS_Function::IsFarJump:
            return py_var(vm, INS_IsFarJump(ins));
        case E_INS_Function::IsFarRet:
            return py_var(vm, INS_IsFarRet(ins));
        case E_INS_Function::IsHalt:
            return py_var(vm, INS_IsHalt(ins));
        case E_INS_Function::IsIndirectControlFlow:
            return py_var(vm, INS_IsIndirectControlFlow(ins));
        case E_INS_Function::IsInterrupt:
            return py_var(vm, INS_IsInterrupt(ins));
        case E_INS_Function::IsIpRelRead:
            return py_var(vm, INS_IsIpRelRead(ins));
        case E_INS_Function::IsIpRelWrite:
            return py_var(vm, INS_IsIpRelWrite(ins));
        case E_INS_Function::IsIRet:
            return py_var(vm, INS_IsIRet(ins));
        case E_INS_Function::IsLea:
            return py_var(vm, INS_IsLea(ins));
        case E_INS_Function::IsMaskMov:
            return py_var(vm, INS_IsMaskMov(ins));
        case E_INS_Function::IsMemoryRead:
            return py_var(vm, INS_IsMemoryRead(ins));
        case E_INS_Function::IsMemoryWrite:
            return py_var(vm, INS_IsMemoryWrite(ins));
        case E_INS_Function::IsMov:
            return py_var(vm, INS_IsMov(ins));
        case E_INS_Function::IsMovdir64b:
            return py_var(vm, INS_IsMovdir64b(ins));
        case E_INS_Function::IsMovFullRegRegSame:
            return py_var(vm, INS_IsMovFullRegRegSame(ins));
        case E_INS_Function::IsNop:
            return py_var(vm, INS_IsNop(ins));
        case E_INS_Function::IsOriginal:
            return py_var(vm, INS_IsOriginal(ins));
        case E_INS_Function::IsPcMaterialization:
            return py_var(vm, INS_IsPcMaterialization(ins));
        case E_INS_Function::IsPredicated:
            return py_var(vm, INS_IsPredicated(ins));
        case E_INS_Function::IsPrefetch:
            return py_var(vm, INS_IsPrefetch(ins));
        case E_INS_Function::IsProcedureCall:
            return py_var(vm, INS_IsProcedureCall(ins));
        case E_INS_Function::IsRDTSC:
            return py_var(vm, INS_IsRDTSC(ins));
        case E_INS_Function::IsRet:
            return py_var(vm, INS_IsRet(ins));
        case E_INS_Function::IsStackRead:
            return py_var(vm, INS_IsStackRead(ins));
        case E_INS_Function::IsStackWrite:
            return py_var(vm, INS_IsStackWrite(ins));
        case E_INS_Function::IsStandardMemop:
            return py_var(vm, INS_IsStandardMemop(ins));
        case E_INS_Function::IsStringop:
            return py_var(vm, INS_IsStringop(ins));
        case E_INS_Function::IsSub:
            return py_var(vm, INS_IsSub(ins));
        case E_INS_Function::IsSyscall:
            return py_var(vm, INS_IsSyscall(ins));
        case E_INS_Function::IsSysenter:
            return py_var(vm, INS_IsSysenter(ins));
        case E_INS_Function::IsSysret:
            return py_var(vm, INS_IsSysret(ins));
        case E_INS_Function::IsValidForIarg:
            return py_var(vm, INS_IsValidForIarg(ins, py_cast< IARG_TYPE >(vm, args[1])));
        case E_INS_Function::IsValidForIpointAfter:
            return py_var(vm, INS_IsValidForIpointAfter(ins));
        case E_INS_Function::IsValidForIpointTakenBranch:
            return py_var(vm, INS_IsValidForIpointTakenBranch(ins));
        case E_INS_Function::IsVgather:
            return py_var(vm, INS_IsVgather(ins));
        case E_INS_Function::IsVscatter:
            return py_var(vm, INS_IsVscatter(ins));
        case E_INS_Function::IsXbegin:
            return py_var(vm, INS_IsXbegin(ins));
        case E_INS_Function::IsXchg:
            return py_var(vm, INS_IsXchg(ins));
        case E_INS_Function::IsXend:
            return py_var(vm, INS_IsXend(ins));
        case E_INS_Function::LockPrefix:
            return py_var(vm, INS_LockPrefix(ins));
        case E_INS_Function::MaskRegister:
            return py_var(vm, INS_MaskRegister(ins));
        case E_INS_Function::MaxNumRRegs:
            return py_var(vm, INS_MaxNumRRegs(ins));
        case E_INS_Function::MaxNumWRegs:
            return py_var(vm, INS_MaxNumWRegs(ins));
        case E_INS_Function::MemoryBaseReg:
            return py_var(vm, INS_MemoryBaseReg(ins));
        case E_INS_Function::MemoryDisplacement:
            return py_var(vm, INS_MemoryDisplacement(ins));
        case E_INS_Function::MemoryIndexReg:
            return py_var(vm, INS_MemoryIndexReg(ins));
        case E_INS_Function::MemoryOperandCount:
            return py_var(vm, INS_MemoryOperandCount(ins));
        case E_INS_Function::MemoryOperandElementCount:
            return py_var(vm, INS_MemoryOperandElementCount(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::MemoryOperandElementSize:
            return py_var(vm, INS_MemoryOperandElementSize(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::MemoryOperandIndexToOperandIndex:
            return py_var(vm, INS_MemoryOperandIndexToOperandIndex(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::MemoryOperandIsRead:
            return py_var(vm, INS_MemoryOperandIsRead(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::MemoryOperandIsWritten:
            return py_var(vm, INS_MemoryOperandIsWritten(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::MemoryOperandSize:
            return py_var(vm, INS_MemoryOperandSize(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::MemoryScale:
            return py_var(vm, INS_MemoryScale(ins));
        case E_INS_Function::Mnemonic:
            return py_var(vm, INS_Mnemonic(ins));
        case E_INS_Function::Next:
            return py_var(vm, INS_Next(ins).index);
        case E_INS_Function::NextAddress:
            return py_var(vm, INS_NextAddress(ins));
        case E_INS_Function::OPCODE_StringShort:
            return py_var(vm, OPCODE_StringShort(py_cast< UINT32 >(vm, args[0])));
        case E_INS_Function::Opcode:
            return py_var(vm, INS_Opcode(ins));
        case E_INS_Function::OperandCount:
            return py_var(vm, INS_OperandCount(ins));
        case E_INS_Function::OperandElementCount:
            return py_var(vm, INS_OperandElementCount(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandElementSize:
            return py_var(vm, INS_OperandElementSize(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandHasElements:
            return py_var(vm, INS_OperandHasElements(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandImmediate:
            return py_var(vm, INS_OperandImmediate(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandIsAddressGenerator:
            return py_var(vm, INS_OperandIsAddressGenerator(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandIsBranchDisplacement:
            return py_var(vm, INS_OperandIsBranchDisplacement(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandIsFixedMemop:
            return py_var(vm, INS_OperandIsFixedMemop(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandIsImmediate:
            return py_var(vm, INS_OperandIsImmediate(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandIsImplicit:
            return py_var(vm, INS_OperandIsImplicit(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandIsMemory:
            return py_var(vm, INS_OperandIsMemory(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandIsReg:
            return py_var(vm, INS_OperandIsReg(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandIsSegmentReg:
            return py_var(vm, INS_OperandIsSegmentReg(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandMemoryBaseReg:
            return py_var(vm, INS_OperandMemoryBaseReg(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandMemoryDisplacement:
            return py_var(vm, INS_OperandMemoryDisplacement(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandMemoryIndexReg:
            return py_var(vm, INS_OperandMemoryIndexReg(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandMemoryScale:
            return py_var(vm, INS_OperandMemoryScale(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandMemorySegmentReg:
            return py_var(vm, INS_OperandMemorySegmentReg(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandNameId:
            return py_var(vm, INS_OperandNameId(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandRead:
            return py_var(vm, INS_OperandRead(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandReadAndWritten:
            return py_var(vm, INS_OperandReadAndWritten(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandReadOnly:
            return py_var(vm, INS_OperandReadOnly(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandReg:
            return py_var(vm, INS_OperandReg(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandSize:
            return py_var(vm, INS_OperandSize(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandSizePrefix:
            return py_var(vm, INS_OperandSizePrefix(ins));
        case E_INS_Function::OperandWidth:
            return py_var(vm, INS_OperandWidth(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandWritten:
            return py_var(vm, INS_OperandWritten(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::OperandWrittenOnly:
            return py_var(vm, INS_OperandWrittenOnly(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::PIN_SetSyntaxATT:
            PIN_SetSyntaxATT();
            return vm->None;
        case E_INS_Function::PIN_SetSyntaxIntel:
            PIN_SetSyntaxIntel();
            return vm->None;
        case E_INS_Function::PIN_SetSyntaxXED:
            PIN_SetSyntaxXED();
            return vm->None;
        case E_INS_Function::Prev:
            return py_var(vm, INS_Prev(ins).index);
        case E_INS_Function::RegIsImplicit:
            return py_var(vm, INS_RegIsImplicit(ins, py_cast< REG >(vm, args[1])));
        case E_INS_Function::RegR:
            return py_var(vm, INS_RegR(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::RegRContain:
            return py_var(vm, INS_RegRContain(ins, py_cast< REG >(vm, args[1])));
        case E_INS_Function::RegW:
            return py_var(vm, INS_RegW(ins, py_cast< UINT32 >(vm, args[1])));
        case E_INS_Function::RegWContain:
            return py_var(vm, INS_RegWContain(ins, py_cast< REG >(vm, args[1])));
        case E_INS_Function::RepCountRegister:
            return py_var(vm, INS_RepCountRegister(ins));
        case E_INS_Function::RepnePrefix:
            return py_var(vm, INS_RepnePrefix(ins));
        case E_INS_Function::RepPrefix:
            return py_var(vm, INS_RepPrefix(ins));
        case E_INS_Function::RewriteMemoryOperand:
            INS_RewriteMemoryOperand(ins, py_cast< UINT32 >(vm, args[1]), py_cast< REG >(vm, args[2]));
            return vm->None;
        case E_INS_Function::RewriteScatteredMemoryOperand:
            INS_RewriteScatteredMemoryOperand(ins, py_cast< UINT32 >(vm, args[1]));
            return vm->None;
        case E_INS_Function::Rtn:
            return py_var(vm, INS_Rtn(ins).index);
        case E_INS_Function::SegmentPrefix:
            return py_var(vm, INS_SegmentPrefix(ins));
        case E_INS_Function::SegmentRegPrefix:
            return py_var(vm, INS_SegmentRegPrefix(ins));
        case E_INS_Function::SegPrefixIsMemoryRead:
            return py_var(vm, INS_SegPrefixIsMemoryRead(ins));
        case E_INS_Function::SegPrefixIsMemoryWrite:
            return py_var(vm, INS_SegPrefixIsMemoryWrite(ins));
        case E_INS_Function::Size:
            return py_var(vm, INS_Size(ins));
        case E_INS_Function::Stutters:
            return py_var(vm, INS_Stutters(ins));
        case E_INS_Function::SyscallStd:
            return py_var(vm, INS_SyscallStd(ins));
        case E_INS_Function::Valid:
            return py_var(vm, INS_Valid(ins));
        case E_INS_Function::XedDec:
            return py_var(vm, INS_XedDec(ins));
        case E_INS_Function::XedExactMapFromPinReg:
            return py_var(vm, INS_XedExactMapFromPinReg(py_cast< REG >(vm, args[0])));
        case E_INS_Function::XedExactMapToPinReg:
            return py_var(vm, INS_XedExactMapToPinReg(py_cast< xed_reg_enum_t >(vm, args[0])));

        default:
            return nullptr;
    }
    }

    inline void ins_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "INS_Address(ins)", ins_function< E_INS_Function::Address >);
        vm->bind(module, "INS_AddressSizePrefix(ins)", ins_function< E_INS_Function::AddressSizePrefix >);
        vm->bind(module, "INS_BranchNotTakenPrefix(ins)", ins_function< E_INS_Function::BranchNotTakenPrefix >);
        vm->bind(module, "INS_BranchTakenPrefix(ins)", ins_function< E_INS_Function::BranchTakenPrefix >);
        vm->bind(module, "CATEGORY_StringShort(num)", ins_function< E_INS_Function::CATEGORY_StringShort, FALSE >);
        vm->bind(module, "INS_Category(ins)", ins_function< E_INS_Function::Category >);
        vm->bind(module, "INS_ChangeReg(ins, old_reg, new_reg, as_read)", ins_function< E_INS_Function::ChangeReg >);
        vm->bind(module, "INS_Delete(ins)", ins_function< E_INS_Function::Delete >);
        vm->bind(module, "INS_DirectControlFlowTargetAddress(ins)",
                 ins_function< E_INS_Function::DirectControlFlowTargetAddress >);
        vm->bind(module, "INS_Disassemble(ins)", ins_function< E_INS_Function::Disassemble >);
        vm->bind(module, "INS_EffectiveAddressWidth(ins)", ins_function< E_INS_Function::EffectiveAddressWidth >);
        vm->bind(module, "EXTENSION_StringShort(num)", ins_function< E_INS_Function::EXTENSION_StringShort, FALSE >);
        vm->bind(module, "INS_Extension(ins)", ins_function< E_INS_Function::Extension >);
        vm->bind(module, "INS_FullRegRContain(ins, reg)", ins_function< E_INS_Function::FullRegRContain >);
        vm->bind(module, "INS_FullRegWContain(ins, reg)", ins_function< E_INS_Function::FullRegWContain >);

        // INS_GetFarPointer gets ins and returns Tuple: (<segment_selector>, <displacement>)
        vm->bind(module, "INS_GetFarPointer(ins)", ins_function< E_INS_Function::GetFarPointer >);

        // GetNumberAndSizeOfMemAccesses gets ins and returns Tuple: (<numAccesses>, <accessSize>, <indexSize>)
        vm->bind(module, "GetNumberAndSizeOfMemAccesses(ins)", ins_function< E_INS_Function::GetNumberAndSizeOfMemAccesses >);
        vm->bind(module, "INS_GetPredicate(ins)", ins_function< E_INS_Function::GetPredicate >);
        vm->bind(module, "INS_HasExplicitMemoryReference(ins)", ins_function< E_INS_Function::HasExplicitMemoryReference >);
        vm->bind(module, "INS_HasFallThrough(ins)", ins_function< E_INS_Function::HasFallThrough >);
        vm->bind(module, "INS_HasMemoryRead2(ins)", ins_function< E_INS_Function::HasMemoryRead2 >);
        vm->bind(module, "INS_HasMemoryVector(ins)", ins_function< E_INS_Function::HasMemoryVector >);
        vm->bind(module, "INS_HasRealRep(ins)", ins_function< E_INS_Function::HasRealRep >);
        vm->bind(module, "INS_HasScatteredMemoryAccess(ins)", ins_function< E_INS_Function::HasScatteredMemoryAccess >);
        vm->bind(module, "INS_InsertDirectJump(ins, ipoint, tgt)", ins_function< E_INS_Function::InsertDirectJump >);
        vm->bind(module, "INS_InsertIndirectJump(ins, ipoint, reg)", ins_function< E_INS_Function::InsertIndirectJump >);
        vm->bind(module, "INS_Invalid()", ins_function< E_INS_Function::Invalid, FALSE >);
        vm->bind(module, "INS_IsAddedForFunctionReplacement(ins)", ins_function< E_INS_Function::IsAddedForFunctionReplacement >);
        vm->bind(module, "INS_IsAmx(ins)", ins_function< E_INS_Function::IsAmx >);
        vm->bind(module, "INS_IsAtomicUpdate(ins)", ins_function< E_INS_Function::IsAtomicUpdate >);
        vm->bind(module, "INS_IsBranch(ins)", ins_function< E_INS_Function::IsBranch >);
        vm->bind(module, "INS_IsCacheLineFlush(ins)", ins_function< E_INS_Function::IsCacheLineFlush >);
        vm->bind(module, "INS_IsCall(ins)", ins_function< E_INS_Function::IsCall >);
        vm->bind(module, "INS_IsControlFlow(ins)", ins_function< E_INS_Function::IsControlFlow >);
        vm->bind(module, "INS_IsDirectBranch(ins)", ins_function< E_INS_Function::IsDirectBranch >);
        vm->bind(module, "INS_IsDirectCall(ins)", ins_function< E_INS_Function::IsDirectCall >);
        vm->bind(module, "INS_IsDirectControlFlow(ins)", ins_function< E_INS_Function::IsDirectControlFlow >);
        vm->bind(module, "INS_IsDirectFarJump(ins)", ins_function< E_INS_Function::IsDirectFarJump >);
        vm->bind(module, "INS_IsFarCall(ins)", ins_function< E_INS_Function::IsFarCall >);
        vm->bind(module, "INS_IsFarJump(ins)", ins_function< E_INS_Function::IsFarJump >);
        vm->bind(module, "INS_IsFarRet(ins)", ins_function< E_INS_Function::IsFarRet >);
        vm->bind(module, "INS_IsHalt(ins)", ins_function< E_INS_Function::IsHalt >);
        vm->bind(module, "INS_IsIndirectControlFlow(ins)", ins_function< E_INS_Function::IsIndirectControlFlow >);
        vm->bind(module, "INS_IsInterrupt(ins)", ins_function< E_INS_Function::IsInterrupt >);
        vm->bind(module, "INS_IsIpRelRead(ins)", ins_function< E_INS_Function::IsIpRelRead >);
        vm->bind(module, "INS_IsIpRelWrite(ins)", ins_function< E_INS_Function::IsIpRelWrite >);
        vm->bind(module, "INS_IsIRet(ins)", ins_function< E_INS_Function::IsIRet >);
        vm->bind(module, "INS_IsLea(ins)", ins_function< E_INS_Function::IsLea >);
        vm->bind(module, "INS_IsMaskMov(ins)", ins_function< E_INS_Function::IsMaskMov >);
        vm->bind(module, "INS_IsMemoryRead(ins)", ins_function< E_INS_Function::IsMemoryRead >);
        vm->bind(module, "INS_IsMemoryWrite(ins)", ins_function< E_INS_Function::IsMemoryWrite >);
        vm->bind(module, "INS_IsMov(ins)", ins_function< E_INS_Function::IsMov >);
        vm->bind(module, "INS_IsMovdir64b(ins)", ins_function< E_INS_Function::IsMovdir64b >);
        vm->bind(module, "INS_IsMovFullRegRegSame(ins)", ins_function< E_INS_Function::IsMovFullRegRegSame >);
        vm->bind(module, "INS_IsNop(ins)", ins_function< E_INS_Function::IsNop >);
        vm->bind(module, "INS_IsOriginal(ins)", ins_function< E_INS_Function::IsOriginal >);
        vm->bind(module, "INS_IsPcMaterialization(ins)", ins_function< E_INS_Function::IsPcMaterialization >);
        vm->bind(module, "INS_IsPredicated(ins)", ins_function< E_INS_Function::IsPredicated >);
        vm->bind(module, "INS_IsPrefetch(ins)", ins_function< E_INS_Function::IsPrefetch >);
        vm->bind(module, "INS_IsProcedureCall(ins)", ins_function< E_INS_Function::IsProcedureCall >);
        vm->bind(module, "INS_IsRDTSC(ins)", ins_function< E_INS_Function::IsRDTSC >);
        vm->bind(module, "INS_IsRet(ins)", ins_function< E_INS_Function::IsRet >);
        vm->bind(module, "INS_IsStackRead(ins)", ins_function< E_INS_Function::IsStackRead >);
        vm->bind(module, "INS_IsStackWrite(ins)", ins_function< E_INS_Function::IsStackWrite >);
        vm->bind(module, "INS_IsStandardMemop(ins)", ins_function< E_INS_Function::IsStandardMemop >);
        vm->bind(module, "INS_IsStringop(ins)", ins_function< E_INS_Function::IsStringop >);
        vm->bind(module, "INS_IsSub(ins)", ins_function< E_INS_Function::IsSub >);
        vm->bind(module, "INS_IsSyscall(ins)", ins_function< E_INS_Function::IsSyscall >);
        vm->bind(module, "INS_IsSysenter(ins)", ins_function< E_INS_Function::IsSysenter >);
        vm->bind(module, "INS_IsSysret(ins)", ins_function< E_INS_Function::IsSysret >);
        vm->bind(module, "INS_IsValidForIarg(ins, argType)", ins_function< E_INS_Function::IsValidForIarg >);
        vm->bind(module, "INS_IsValidForIpointAfter(ins)", ins_function< E_INS_Function::IsValidForIpointAfter >);
        vm->bind(module, "INS_IsValidForIpointTakenBranch(ins)", ins_function< E_INS_Function::IsValidForIpointTakenBranch >);
        vm->bind(module, "INS_IsVgather(ins)", ins_function< E_INS_Function::IsVgather >);
        vm->bind(module, "INS_IsVscatter(ins)", ins_function< E_INS_Function::IsVscatter >);
        vm->bind(module, "INS_IsXbegin(ins)", ins_function< E_INS_Function::IsXbegin >);
        vm->bind(module, "INS_IsXchg(ins)", ins_function< E_INS_Function::IsXchg >);
        vm->bind(module, "INS_IsXend(ins)", ins_function< E_INS_Function::IsXend >);
        vm->bind(module, "INS_LockPrefix(ins)", ins_function< E_INS_Function::LockPrefix >);
        vm->bind(module, "INS_MaskRegister(ins)", ins_function< E_INS_Function::MaskRegister >);
        vm->bind(module, "INS_MaxNumRRegs(ins)", ins_function< E_INS_Function::MaxNumRRegs >);
        vm->bind(module, "INS_MaxNumWRegs(ins)", ins_function< E_INS_Function::MaxNumWRegs >);
        vm->bind(module, "INS_MemoryBaseReg(ins)", ins_function< E_INS_Function::MemoryBaseReg >);
        vm->bind(module, "INS_MemoryDisplacement(ins)", ins_function< E_INS_Function::MemoryDisplacement >);
        vm->bind(module, "INS_MemoryIndexReg(ins)", ins_function< E_INS_Function::MemoryIndexReg >);
        vm->bind(module, "INS_MemoryOperandCount(ins)", ins_function< E_INS_Function::MemoryOperandCount >);
        vm->bind(module, "INS_MemoryOperandElementCount(ins, memoryOp)",
                 ins_function< E_INS_Function::MemoryOperandElementCount >);
        vm->bind(module, "INS_MemoryOperandElementSize(ins, memoryOp)", ins_function< E_INS_Function::MemoryOperandElementSize >);
        vm->bind(module, "INS_MemoryOperandIndexToOperandIndex(ins, memoryOp)",
                 ins_function< E_INS_Function::MemoryOperandIndexToOperandIndex >);
        vm->bind(module, "INS_MemoryOperandIsRead(ins, memOp)", ins_function< E_INS_Function::MemoryOperandIsRead >);
        vm->bind(module, "INS_MemoryOperandIsWritten(ins, memOp)", ins_function< E_INS_Function::MemoryOperandIsWritten >);
        vm->bind(module, "INS_MemoryOperandSize(ins, memOp)", ins_function< E_INS_Function::MemoryOperandSize >);
        vm->bind(module, "INS_MemoryScale(ins)", ins_function< E_INS_Function::MemoryScale >);
        vm->bind(module, "INS_Mnemonic(ins)", ins_function< E_INS_Function::Mnemonic >);
        vm->bind(module, "INS_Next(ins)", ins_function< E_INS_Function::Next >);
        vm->bind(module, "INS_NextAddress(ins)", ins_function< E_INS_Function::NextAddress >);
        vm->bind(module, "OPCODE_StringShort(opcode)", ins_function< E_INS_Function::OPCODE_StringShort, FALSE >);
        vm->bind(module, "INS_Opcode(ins)", ins_function< E_INS_Function::Opcode >);
        vm->bind(module, "INS_OperandCount(ins)", ins_function< E_INS_Function::OperandCount >);
        vm->bind(module, "INS_OperandElementCount(ins, opIdx)", ins_function< E_INS_Function::OperandElementCount >);
        vm->bind(module, "INS_OperandElementSize(ins, opIdx)", ins_function< E_INS_Function::OperandElementSize >);
        vm->bind(module, "INS_OperandHasElements(ins, opIdx)", ins_function< E_INS_Function::OperandHasElements >);
        vm->bind(module, "INS_OperandImmediate(ins, n)", ins_function< E_INS_Function::OperandImmediate >);
        vm->bind(module, "INS_OperandIsAddressGenerator(ins, n)", ins_function< E_INS_Function::OperandIsAddressGenerator >);
        vm->bind(module, "INS_OperandIsBranchDisplacement(ins, n)", ins_function< E_INS_Function::OperandIsBranchDisplacement >);
        vm->bind(module, "INS_OperandIsFixedMemop(ins, n)", ins_function< E_INS_Function::OperandIsFixedMemop >);
        vm->bind(module, "INS_OperandIsImmediate(ins, n)", ins_function< E_INS_Function::OperandIsImmediate >);
        vm->bind(module, "INS_OperandIsImplicit(ins, n)", ins_function< E_INS_Function::OperandIsImplicit >);
        vm->bind(module, "INS_OperandIsMemory(ins, n)", ins_function< E_INS_Function::OperandIsMemory >);
        vm->bind(module, "INS_OperandIsReg(ins, n)", ins_function< E_INS_Function::OperandIsReg >);
        vm->bind(module, "INS_OperandIsSegmentReg(ins, n)", ins_function< E_INS_Function::OperandIsSegmentReg >);
        vm->bind(module, "INS_OperandMemoryBaseReg(ins, n)", ins_function< E_INS_Function::OperandMemoryBaseReg >);
        vm->bind(module, "INS_OperandMemoryDisplacement(ins, n)", ins_function< E_INS_Function::OperandMemoryDisplacement >);
        vm->bind(module, "INS_OperandMemoryIndexReg(ins, n)", ins_function< E_INS_Function::OperandMemoryIndexReg >);
        vm->bind(module, "INS_OperandMemoryScale(ins, n)", ins_function< E_INS_Function::OperandMemoryScale >);
        vm->bind(module, "INS_OperandMemorySegmentReg(ins, n)", ins_function< E_INS_Function::OperandMemorySegmentReg >);
        vm->bind(module, "INS_OperandNameId(ins, n)", ins_function< E_INS_Function::OperandNameId >);
        vm->bind(module, "INS_OperandRead(ins, n)", ins_function< E_INS_Function::OperandRead >);
        vm->bind(module, "INS_OperandReadAndWritten(ins, n)", ins_function< E_INS_Function::OperandReadAndWritten >);
        vm->bind(module, "INS_OperandReadOnly(ins, n)", ins_function< E_INS_Function::OperandReadOnly >);
        vm->bind(module, "INS_OperandReg(ins, n)", ins_function< E_INS_Function::OperandReg >);
        vm->bind(module, "INS_OperandSize(ins, n)", ins_function< E_INS_Function::OperandSize >);
        vm->bind(module, "INS_OperandSizePrefix(ins)", ins_function< E_INS_Function::OperandSizePrefix >);
        vm->bind(module, "INS_OperandWidth(ins, n)", ins_function< E_INS_Function::OperandWidth >);
        vm->bind(module, "INS_OperandWritten(ins, n)", ins_function< E_INS_Function::OperandWritten >);
        vm->bind(module, "INS_OperandWrittenOnly(ins, n)", ins_function< E_INS_Function::OperandWrittenOnly >);
        vm->bind(module, "PIN_SetSyntaxATT()", ins_function< E_INS_Function::PIN_SetSyntaxATT, FALSE >);
        vm->bind(module, "PIN_SetSyntaxIntel()", ins_function< E_INS_Function::PIN_SetSyntaxIntel, FALSE >);
        vm->bind(module, "PIN_SetSyntaxXED()", ins_function< E_INS_Function::PIN_SetSyntaxXED, FALSE >);
        vm->bind(module, "INS_Prev(ins)", ins_function< E_INS_Function::Prev >);
        vm->bind(module, "INS_RegIsImplicit(ins, reg)", ins_function< E_INS_Function::RegIsImplicit >);
        vm->bind(module, "INS_RegR(ins, k)", ins_function< E_INS_Function::RegR >);
        vm->bind(module, "INS_RegRContain(ins, reg)", ins_function< E_INS_Function::RegRContain >);
        vm->bind(module, "INS_RegW(ins, k)", ins_function< E_INS_Function::RegW >);
        vm->bind(module, "INS_RegWContain(ins, reg)", ins_function< E_INS_Function::RegWContain >);
        vm->bind(module, "INS_RepCountRegister(ins)", ins_function< E_INS_Function::RepCountRegister >);
        vm->bind(module, "INS_RepnePrefix(ins)", ins_function< E_INS_Function::RepnePrefix >);
        vm->bind(module, "INS_RepPrefix(ins)", ins_function< E_INS_Function::RepPrefix >);
        vm->bind(module, "INS_RewriteMemoryOperand(ins, memindex, reg)", ins_function< E_INS_Function::RewriteMemoryOperand >);
        vm->bind(module, "INS_RewriteScatteredMemoryOperand(ins, memindex)",
                 ins_function< E_INS_Function::RewriteScatteredMemoryOperand >);
        vm->bind(module, "INS_Rtn(ins)", ins_function< E_INS_Function::Rtn >);
        vm->bind(module, "INS_SegmentPrefix(ins)", ins_function< E_INS_Function::SegmentPrefix >);
        vm->bind(module, "INS_SegmentRegPrefix(ins)", ins_function< E_INS_Function::SegmentRegPrefix >);
        vm->bind(module, "INS_SegPrefixIsMemoryRead(ins)", ins_function< E_INS_Function::SegPrefixIsMemoryRead >);
        vm->bind(module, "INS_SegPrefixIsMemoryWrite(ins)", ins_function< E_INS_Function::SegPrefixIsMemoryWrite >);
        vm->bind(module, "INS_Size(ins)", ins_function< E_INS_Function::Size >);
        vm->bind(module, "INS_Stutters(ins)", ins_function< E_INS_Function::Stutters >);
        vm->bind(module, "INS_SyscallStd(ins)", ins_function< E_INS_Function::SyscallStd >);
        vm->bind(module, "INS_Valid(ins)", ins_function< E_INS_Function::Valid >);
        vm->bind(module, "INS_XedDec(ins)", ins_function< E_INS_Function::XedDec >);
        vm->bind(module, "INS_XedExactMapFromPinReg(pin_reg)", ins_function< E_INS_Function::XedExactMapFromPinReg, FALSE >);
        vm->bind(module, "INS_XedExactMapToPinReg(xed_reg)", ins_function< E_INS_Function::XedExactMapToPinReg, FALSE >);

        //PREDICATE
        module->attr().set("PREDICATE_ALWAYS_TRUE", py_var(vm, PREDICATE_ALWAYS_TRUE));
        module->attr().set("PREDICATE_INVALID", py_var(vm, PREDICATE_INVALID));
        module->attr().set("PREDICATE_BELOW", py_var(vm, PREDICATE_BELOW));
        module->attr().set("PREDICATE_BELOW_OR_EQUAL", py_var(vm, PREDICATE_BELOW_OR_EQUAL));
        module->attr().set("PREDICATE_LESS", py_var(vm, PREDICATE_LESS));
        module->attr().set("PREDICATE_LESS_OR_EQUAL", py_var(vm, PREDICATE_LESS_OR_EQUAL));
        module->attr().set("PREDICATE_NOT_BELOW", py_var(vm, PREDICATE_NOT_BELOW));
        module->attr().set("PREDICATE_NOT_BELOW_OR_EQUAL", py_var(vm, PREDICATE_NOT_BELOW_OR_EQUAL));
        module->attr().set("PREDICATE_NOT_LESS", py_var(vm, PREDICATE_NOT_LESS));
        module->attr().set("PREDICATE_NOT_LESS_OR_EQUAL", py_var(vm, PREDICATE_NOT_LESS_OR_EQUAL));
        module->attr().set("PREDICATE_NOT_OVERFLOW", py_var(vm, PREDICATE_NOT_OVERFLOW));
        module->attr().set("PREDICATE_NOT_PARITY", py_var(vm, PREDICATE_NOT_PARITY));
        module->attr().set("PREDICATE_NOT_SIGN", py_var(vm, PREDICATE_NOT_SIGN));
        module->attr().set("PREDICATE_NOT_ZERO", py_var(vm, PREDICATE_NOT_ZERO));
        module->attr().set("PREDICATE_OVERFLOW", py_var(vm, PREDICATE_OVERFLOW));
        module->attr().set("PREDICATE_PARITY", py_var(vm, PREDICATE_PARITY));
        module->attr().set("PREDICATE_SIGN", py_var(vm, PREDICATE_SIGN));
        module->attr().set("PREDICATE_ZERO", py_var(vm, PREDICATE_ZERO));
        module->attr().set("PREDICATE_CX_NON_ZERO", py_var(vm, PREDICATE_CX_NON_ZERO));
        module->attr().set("PREDICATE_ECX_NON_ZERO", py_var(vm, PREDICATE_ECX_NON_ZERO));
        module->attr().set("PREDICATE_RCX_NON_ZERO", py_var(vm, PREDICATE_RCX_NON_ZERO));
        module->attr().set("PREDICATE_SAVED_GCX_NON_ZERO", py_var(vm, PREDICATE_SAVED_GCX_NON_ZERO));

        //MEMORY_TYPE
        module->attr().set("MEMORY_TYPE_READ", py_var(vm, MEMORY_TYPE_READ));
        module->attr().set("MEMORY_TYPE_WRITE", py_var(vm, MEMORY_TYPE_WRITE));
        module->attr().set("MEMORY_TYPE_READ2", py_var(vm, MEMORY_TYPE_READ2));

        //SYSCALL_STANDARD
        module->attr().set("SYSCALL_STANDARD_INVALID", py_var(vm, SYSCALL_STANDARD_INVALID));
        module->attr().set("SYSCALL_STANDARD_IA32_LINUX", py_var(vm, SYSCALL_STANDARD_IA32_LINUX));
        module->attr().set("SYSCALL_STANDARD_IA32_LINUX_SYSENTER", py_var(vm, SYSCALL_STANDARD_IA32_LINUX_SYSENTER));
        module->attr().set("SYSCALL_STANDARD_IA32E_LINUX", py_var(vm, SYSCALL_STANDARD_IA32E_LINUX));
        module->attr().set("SYSCALL_STANDARD_IA32E_LINUX_VSYSCALL", py_var(vm, SYSCALL_STANDARD_IA32E_LINUX_VSYSCALL));
        module->attr().set("SYSCALL_STANDARD_IA32_MAC", py_var(vm, SYSCALL_STANDARD_IA32_MAC));
        module->attr().set("SYSCALL_STANDARD_IA32E_MAC", py_var(vm, SYSCALL_STANDARD_IA32E_MAC));
        module->attr().set("SYSCALL_STANDARD_IA32_WINDOWS_FAST", py_var(vm, SYSCALL_STANDARD_IA32_WINDOWS_FAST));
        module->attr().set("SYSCALL_STANDARD_IA32E_WINDOWS_FAST", py_var(vm, SYSCALL_STANDARD_IA32E_WINDOWS_FAST));
        module->attr().set("SYSCALL_STANDARD_IA32_WINDOWS_ALT", py_var(vm, SYSCALL_STANDARD_IA32_WINDOWS_ALT));
        module->attr().set("SYSCALL_STANDARD_WOW64", py_var(vm, SYSCALL_STANDARD_WOW64));
        module->attr().set("SYSCALL_STANDARD_WINDOWS_INT", py_var(vm, SYSCALL_STANDARD_WINDOWS_INT));
    }

} // namespace glue

#endif // _GLUE_INS_H_
