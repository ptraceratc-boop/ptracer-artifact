/*
 * Copyright (C) 2024-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_PIN_H_
#define _GLUE_PIN_H_

#include "../gluepy.h"
#include "Glue_Wrappers.h"
#include "Glue_XED_ICLASS.h"
#include "Glue_GLUE.h"
#include "Glue_REG.h"
#include "Glue_BBL.h"
#include "Glue_IMG.h"
#include "Glue_SEC.h"
#include "Glue_RTN.h"
#include "Glue_INS.h"
#include "Glue_TRACE.h"
#include "Glue_SYM.h"
#include "Glue_THREADS.h"
#include "Glue_CONTEXT.h"
#include "Glue_EXCEPTION.h"

namespace glue
{
// PIN_PARG macro expansion
const PARG_T PIN_PARG_POINTER   = PIN_PARG(void*);
const PARG_T PIN_PARG_BOOL      = PIN_PARG(bool);
const PARG_T PIN_PARG_CHAR      = PIN_PARG(char);
const PARG_T PIN_PARG_UCHAR     = PIN_PARG(unsigned char);
const PARG_T PIN_PARG_SCHAR     = PIN_PARG(signed char);
const PARG_T PIN_PARG_SHORT     = PIN_PARG(short);
const PARG_T PIN_PARG_USHORT    = PIN_PARG(unsigned short);
const PARG_T PIN_PARG_SIZE_T    = PIN_PARG(size_t);
const PARG_T PIN_PARG_INT       = PIN_PARG(int);
const PARG_T PIN_PARG_UINT      = PIN_PARG(unsigned int);
const PARG_T PIN_PARG_LONG      = PIN_PARG(long);
const PARG_T PIN_PARG_ULONG     = PIN_PARG(unsigned long);
const PARG_T PIN_PARG_LONGLONG  = PIN_PARG(long long);
const PARG_T PIN_PARG_ULONGLONG = PIN_PARG(unsigned long long);
const PARG_T PIN_PARG_FLOAT     = PIN_PARG(float);
const PARG_T PIN_PARG_DOUBLE    = PIN_PARG(double);
const PARG_T PIN_PARG_VOID      = PIN_PARG(void);

PyObject* glue_proto_allocate(VM* vm, ArgsView args)
{
    // args[0] - PARG_T* retPargPtr, args[1] - CALLINGSTD_TYPE cstype, args[2] - PyObject* name (str), args[3] - *args (python variadic args)
    auto retPargPtr        = py_cast< PARG_T* >(vm, args[0]);
    PARG_T retParg         = *retPargPtr;
    CALLINGSTD_TYPE cstype = py_cast< CALLINGSTD_TYPE >(vm, args[1]);
    const char* name       = py_cast< const char* >(vm, args[2]);

    Tuple pyVariadicArgs = py_cast< Tuple >(vm, args[3]);
    auto varSize         = pyVariadicArgs.size();

    Glue_Proto* protoObj = new Glue_Proto();
    protoObj->retParg    = retPargPtr;
    for (auto i = 0; i < varSize; ++i)
    {
        protoObj->pargsTypeList.emplace_back(py_cast< PARG_T* >(vm, pyVariadicArgs[i]));
    }

    if (0 == varSize)
    {
        protoObj->proto = PROTO_Allocate(retParg, cstype, name, PIN_PARG_END());
    }
    else if (1 == varSize)
    {
        protoObj->proto = PROTO_Allocate(retParg, cstype, name, *(protoObj->pargsTypeList[0]), PIN_PARG_END());
    }
    else if (2 == varSize)
    {
        protoObj->proto =
            PROTO_Allocate(retParg, cstype, name, *(protoObj->pargsTypeList[0]), *(protoObj->pargsTypeList[1]), PIN_PARG_END());
    }
    else if (3 == varSize)
    {
        protoObj->proto = PROTO_Allocate(retParg, cstype, name, *(protoObj->pargsTypeList[0]), *(protoObj->pargsTypeList[1]),
                                         *(protoObj->pargsTypeList[2]), PIN_PARG_END());
    }
    else if (4 == varSize)
    {
        protoObj->proto = PROTO_Allocate(retParg, cstype, name, *(protoObj->pargsTypeList[0]), *(protoObj->pargsTypeList[1]),
                                         *(protoObj->pargsTypeList[2]), *(protoObj->pargsTypeList[3]), PIN_PARG_END());
    }
    else if (5 == varSize)
    {
        protoObj->proto = PROTO_Allocate(retParg, cstype, name, *(protoObj->pargsTypeList[0]), *(protoObj->pargsTypeList[1]),
                                         *(protoObj->pargsTypeList[2]), *(protoObj->pargsTypeList[3]),
                                         *(protoObj->pargsTypeList[4]), PIN_PARG_END());
    }
    else if (6 == varSize)
    {
        protoObj->proto = PROTO_Allocate(retParg, cstype, name, *(protoObj->pargsTypeList[0]), *(protoObj->pargsTypeList[1]),
                                         *(protoObj->pargsTypeList[2]), *(protoObj->pargsTypeList[3]),
                                         *(protoObj->pargsTypeList[4]), *(protoObj->pargsTypeList[5]), PIN_PARG_END());
    }
    else if (7 == varSize)
    {
        protoObj->proto =
            PROTO_Allocate(retParg, cstype, name, *(protoObj->pargsTypeList[0]), *(protoObj->pargsTypeList[1]),
                           *(protoObj->pargsTypeList[2]), *(protoObj->pargsTypeList[3]), *(protoObj->pargsTypeList[4]),
                           *(protoObj->pargsTypeList[5]), *(protoObj->pargsTypeList[6]), PIN_PARG_END());
    }
    else if (8 == varSize)
    {
        protoObj->proto = PROTO_Allocate(retParg, cstype, name, *(protoObj->pargsTypeList[0]), *(protoObj->pargsTypeList[1]),
                                         *(protoObj->pargsTypeList[2]), *(protoObj->pargsTypeList[3]),
                                         *(protoObj->pargsTypeList[4]), *(protoObj->pargsTypeList[5]),
                                         *(protoObj->pargsTypeList[6]), *(protoObj->pargsTypeList[7]), PIN_PARG_END());
    }
    else
    {
        ASSERTQ("pinglueserver: currently PROTO_Allocate doesn't support variadic size more than 8");
    }

    return py_var(vm, protoObj);
}

enum class E_PIN_Function
{
    CheckReadAccess,
    CheckWriteAccess,
    CreateThreadDataKey,
    Detach,
    DetachProbed,
    ExitApplication,
    ExitProcess,
    GetAuxVectorValue,
    GetInitialThreadCount,
    GetPid,
    GetSourceLocation,
    GetSyscallArgument,
    GetSyscallErrno,
    GetSyscallNumber,
    GetSyscallReturn,
    GetThreadData,
    IsActionPending,
    IsAttaching,
    IsProbeMode,
    IsSafeForProbedInsertion,
    PROTO_Allocate,
    REG_StringShort,
    RemoveFiniFunctions,
    RemoveInstrumentation,
    RemoveInstrumentationInRange,
    SafeCopy,
    SafeCopyEx,
    SetReplayMode,
    SetSmcSupport,
    SetSyscallArgument,
    SetSyscallErrno,
    SetSyscallNumber,
    SetSyscallReturn,
    SetThreadData,
    ToolFullPath,
    TryEnd,
    UnblockSignal,
    Version,
    VmFullPath
};

template< E_PIN_Function Function > inline PyObject* pin_function(VM* vm, ArgsView args)
{
    switch (Function)
    {
        case E_PIN_Function::CheckReadAccess:
            return py_var(vm, PIN_CheckReadAccess(py_cast< VOID* >(vm, args[0])));
        case E_PIN_Function::CheckWriteAccess:
            return py_var(vm, PIN_CheckWriteAccess(py_cast< VOID* >(vm, args[0])));
        case E_PIN_Function::CreateThreadDataKey:
            return py_var(vm, PIN_CreateThreadDataKey(NULL));
        case E_PIN_Function::Detach:
        {
            // PIN_Detach acquires VM and Client lock - prevent deadlock
            Scoped_GIL_temp_release tempUnlock;
            PIN_Detach();
            return vm->None;
        }
        case E_PIN_Function::DetachProbed:
        {
            // PIN_DetachProbed acquires VM and Client lock - prevent deadlock
            Scoped_GIL_temp_release tempUnlock;
            PIN_DetachProbed();
            return vm->None;
        }
        case E_PIN_Function::ExitApplication:
        {
            // PIN_ExitApplication does not return and we don't want to
            // block other threads
            Pinglue_GIL::unlock();
            PIN_ExitApplication(py_cast< INT32 >(vm, args[0]));
            return vm->None;
        }
        case E_PIN_Function::ExitProcess:
        {
            // PIN_ExitProcess does not return and we don't want to
            // block other threads
            Pinglue_GIL::unlock();
            PIN_ExitProcess(py_cast< INT32 >(vm, args[0]));
            return vm->None; // We should not get here
        }
        case E_PIN_Function::GetAuxVectorValue:
            return py_var(vm, PIN_GetAuxVectorValue(py_cast< ADDRINT >(vm, args[0]), py_cast< bool* >(vm, args[1])));
        case E_PIN_Function::GetInitialThreadCount:
            return py_var(vm, PIN_GetInitialThreadCount());
        case E_PIN_Function::GetPid:
            return py_var(vm, PIN_GetPid());
        case E_PIN_Function::GetSourceLocation:
        {
            int column, lineno;
            std::string srcFile = "";

            // We are going to take the client lock, we release the GIL first
            // so we don't deadlock with instrumentation routines or other PIN API
            // that takes the Client Lock
            Scoped_GIL_temp_release tempUnlock;

            // The client lock should be taken before calling PIN_GetSourceLocation
            // from an analysis routine. Here we don't check that we are called from
            // an analysis routine but rather just take the lock. This is OK because
            // the client lock is recursive
            PIN_LockClient();
            PIN_GetSourceLocation(py_cast< ADDRINT >(vm, args[0]), &column, &lineno, &srcFile);
            PIN_UnlockClient(); // We are done - release the client lock

            Tuple res(3);
            res[0] = py_var(vm, column);
            res[1] = py_var(vm, lineno);
            res[2] = py_var(vm, srcFile);

            return py_var(vm, res);
        }
        case E_PIN_Function::GetSyscallArgument:
            return py_var(vm, PIN_GetSyscallArgument(py_cast< const CONTEXT* >(vm, args[0]),
                                                     py_cast< SYSCALL_STANDARD >(vm, args[1]), py_cast< UINT32 >(vm, args[2])));
        case E_PIN_Function::GetSyscallErrno:
            return py_var(vm,
                          PIN_GetSyscallErrno(py_cast< const CONTEXT* >(vm, args[0]), py_cast< SYSCALL_STANDARD >(vm, args[1])));
        case E_PIN_Function::GetThreadData:
        {
            auto data = (PyObject*)PIN_GetThreadData(py_cast< ADDRINT >(vm, args[0]), py_cast< ADDRINT >(vm, args[1]));
            PK_OBJ_MARK(data);
            return data;
        }
        case E_PIN_Function::IsActionPending:
            return py_var(vm, PIN_IsActionPending(py_cast< THREADID >(vm, args[0])));
        case E_PIN_Function::IsAttaching:
            return py_var(vm, PIN_IsAttaching());
        case E_PIN_Function::IsProbeMode:
            return py_var(vm, PIN_IsProbeMode());
        case E_PIN_Function::IsSafeForProbedInsertion:
            return py_var(vm, PIN_IsSafeForProbedInsertion(py_cast< ADDRINT >(vm, args[0])));
        case E_PIN_Function::REG_StringShort:
            return py_var(vm, REG_StringShort(py_cast< REG >(vm, args[0])));
        case E_PIN_Function::RemoveFiniFunctions:
            PIN_RemoveFiniFunctions();
            return vm->None;
        case E_PIN_Function::RemoveInstrumentation:
            PIN_RemoveInstrumentation();
            return vm->None;
        case E_PIN_Function::RemoveInstrumentationInRange:
            PIN_RemoveInstrumentationInRange(py_cast< ADDRINT >(vm, args[0]), py_cast< ADDRINT >(vm, args[1]));
            return vm->None;
        case E_PIN_Function::SafeCopy:
            return py_var(
                vm, PIN_SafeCopy(py_cast< VOID* >(vm, args[0]), py_cast< VOID* >(vm, args[1]), py_cast< size_t >(vm, args[2])));
        case E_PIN_Function::SafeCopyEx:
            return py_var(vm, PIN_SafeCopyEx(py_cast< VOID* >(vm, args[0]), py_cast< VOID* >(vm, args[1]),
                                             py_cast< size_t >(vm, args[2]), py_cast< EXCEPTION_INFO* >(vm, args[3])));
        case E_PIN_Function::SetReplayMode:
            PIN_SetReplayMode(py_cast< UINT32 >(vm, args[0]));
            return vm->None;
        case E_PIN_Function::SetSmcSupport:
            PIN_SetSmcSupport(py_cast< SMC_ENABLE_DISABLE_TYPE >(vm, args[0]));
            return vm->None;
        case E_PIN_Function::SetSyscallArgument:
            PIN_SetSyscallArgument(py_cast< CONTEXT* >(vm, args[0]), py_cast< SYSCALL_STANDARD >(vm, args[1]),
                                   py_cast< UINT32 >(vm, args[2]), py_cast< ADDRINT >(vm, args[3]));
            return vm->None;
        case E_PIN_Function::SetSyscallErrno:
            PIN_SetSyscallErrno(py_cast< CONTEXT* >(vm, args[0]), py_cast< SYSCALL_STANDARD >(vm, args[1]),
                                py_cast< ADDRINT >(vm, args[2]));
            return vm->None;
        case E_PIN_Function::SetSyscallNumber:
            PIN_SetSyscallNumber(py_cast< CONTEXT* >(vm, args[0]), py_cast< SYSCALL_STANDARD >(vm, args[1]),
                                 py_cast< ADDRINT >(vm, args[2]));
            return vm->None;
        case E_PIN_Function::SetSyscallReturn:
            PIN_SetSyscallReturn(py_cast< CONTEXT* >(vm, args[0]), py_cast< SYSCALL_STANDARD >(vm, args[1]),
                                 py_cast< ADDRINT >(vm, args[2]));
            return vm->None;
        case E_PIN_Function::GetSyscallNumber:
            return py_var(vm,
                          PIN_GetSyscallNumber(py_cast< const CONTEXT* >(vm, args[0]), py_cast< SYSCALL_STANDARD >(vm, args[1])));
        case E_PIN_Function::GetSyscallReturn:
            return py_var(vm,
                          PIN_GetSyscallReturn(py_cast< const CONTEXT* >(vm, args[0]), py_cast< SYSCALL_STANDARD >(vm, args[1])));
        case E_PIN_Function::SetThreadData:
            PK_OBJ_MARK(args[1]);
            return py_var(vm,
                          PIN_SetThreadData(py_cast< TLS_KEY >(vm, args[0]), (VOID*)args[1], py_cast< THREADID >(vm, args[2])));
        case E_PIN_Function::ToolFullPath:
            return py_var(vm, PIN_ToolFullPath());
        case E_PIN_Function::TryEnd:
            PIN_TryEnd(py_cast< THREADID >(vm, args[0]));
            return vm->None;
        case E_PIN_Function::UnblockSignal:
            return py_var(vm, PIN_UnblockSignal(py_cast< INT32 >(vm, args[0]), py_cast< BOOL >(vm, args[1])));
        case E_PIN_Function::Version:
            return py_var(vm, PIN_Version());
        case E_PIN_Function::VmFullPath:
            return py_var(vm, PIN_VmFullPath());
        default:
            return nullptr;
    }
}

/**
     * @brief Bind pin apis and attributes to module.
     * 
     * @param[in] vm       pocketpy vm ptr.
     * @param[in] module   pocketpy module ptr.
     * 
     */
inline void bind_pin_apis_and_attributes(VM* vm, PyObject* module)
{
    Wrapped_ADDRINT::register_class(vm, vm->builtins);
    Wrapped_BUFFER::register_class(vm, vm->builtins);
    Wrapped_EXCEPTION_INFO::register_class(vm, vm->builtins);
    Wrapped_FXSAVE::register_class(vm, vm->builtins);
    Wrapped_FPSTATE::register_class(vm, vm->builtins);

    vm->bind(module, "PROTO_Allocate(returnArg, cstype, name, *args)", glue_proto_allocate);

    vm->bind(module, "PIN_CheckReadAccess(addr)", pin_function< E_PIN_Function::CheckReadAccess >);
    vm->bind(module, "PIN_CheckWriteAccess(addr)", pin_function< E_PIN_Function::CheckWriteAccess >);
    vm->bind(module, "PIN_CreateThreadDataKey()", pin_function< E_PIN_Function::CreateThreadDataKey >);
    vm->bind(module, "PIN_Detach()", pin_function< E_PIN_Function::Detach >);
    vm->bind(module, "PIN_DetachProbed()", pin_function< E_PIN_Function::DetachProbed >);
    vm->bind(module, "PIN_ExitApplication(status)", pin_function< E_PIN_Function::ExitApplication >);
    vm->bind(module, "PIN_ExitProcess(exitCode)", pin_function< E_PIN_Function::ExitProcess >);
    vm->bind(module, "PIN_GetAuxVectorValue(type, found)", pin_function< E_PIN_Function::GetAuxVectorValue >);
    vm->bind(module, "PIN_GetInitialThreadCount()", pin_function< E_PIN_Function::GetInitialThreadCount >);
    vm->bind(module, "PIN_GetPid()", pin_function< E_PIN_Function::GetPid >);

    // PIN_GetSourceLocation returns Tuple: (<column>, <line>, <fileName>)
    // When:
    // column number 0 indicates that there is no valid column information.
    // line number 0 indicates that there is no valid line information.
    // Empty fileName ("") indicates that there is no valid file name information
    vm->bind(module, "PIN_GetSourceLocation(address)", pin_function< E_PIN_Function::GetSourceLocation >);

    vm->bind(module, "PIN_GetSyscallArgument(ctxt, std, argNum)", pin_function< E_PIN_Function::GetSyscallArgument >);
    vm->bind(module, "PIN_GetSyscallErrno(ctxt, std)", pin_function< E_PIN_Function::GetSyscallErrno >);
    vm->bind(module, "PIN_GetSyscallNumber(ctxt, std)", pin_function< E_PIN_Function::GetSyscallNumber >);
    vm->bind(module, "PIN_GetSyscallReturn(ctxt, std)", pin_function< E_PIN_Function::GetSyscallReturn >);
    vm->bind(module, "PIN_GetThreadData(key, thread_id)", pin_function< E_PIN_Function::GetThreadData >);
    vm->bind(module, "PIN_IsActionPending(tid)", pin_function< E_PIN_Function::IsActionPending >);
    vm->bind(module, "PIN_IsAttaching()", pin_function< E_PIN_Function::IsAttaching >);
    vm->bind(module, "PIN_IsProbeMode()", pin_function< E_PIN_Function::IsProbeMode >);
    vm->bind(module, "PIN_IsSafeForProbedInsertion(addr)", pin_function< E_PIN_Function::IsSafeForProbedInsertion >);
    vm->bind(module, "REG_StringShort(reg)", pin_function< E_PIN_Function::REG_StringShort >);
    vm->bind(module, "PIN_RemoveFiniFunctions()", pin_function< E_PIN_Function::RemoveFiniFunctions >);
    vm->bind(module, "PIN_RemoveInstrumentation()", pin_function< E_PIN_Function::RemoveInstrumentation >);
    vm->bind(module, "PIN_RemoveInstrumentationInRange(start, end)",
             pin_function< E_PIN_Function::RemoveInstrumentationInRange >);
    vm->bind(module, "PIN_SafeCopy(dst, src, size)", pin_function< E_PIN_Function::SafeCopy >);
    vm->bind(module, "PIN_SafeCopyEx(dst, src, size, pExceptInfo)", pin_function< E_PIN_Function::SafeCopyEx >);
    vm->bind(module, "PIN_SetReplayMode(flags)", pin_function< E_PIN_Function::SetReplayMode >);
    vm->bind(module, "PIN_SetSmcSupport(enable_disable)", pin_function< E_PIN_Function::SetSmcSupport >);
    vm->bind(module, "PIN_SetSyscallArgument(ctxt, std, argNum, val)", pin_function< E_PIN_Function::SetSyscallArgument >);
    vm->bind(module, "PIN_SetSyscallErrno(ctxt, std, val)", pin_function< E_PIN_Function::SetSyscallErrno >);
    vm->bind(module, "PIN_SetSyscallNumber(ctxt, std, val)", pin_function< E_PIN_Function::SetSyscallNumber >);
    vm->bind(module, "PIN_SetSyscallReturn(ctxt, std, val)", pin_function< E_PIN_Function::SetSyscallReturn >);
    vm->bind(module, "PIN_SetThreadData(tls_key, tdata, thread_id)", pin_function< E_PIN_Function::SetThreadData >);
    vm->bind(module, "PIN_ToolFullPath()", pin_function< E_PIN_Function::ToolFullPath >);
    vm->bind(module, "PIN_TryEnd(tid)", pin_function< E_PIN_Function::TryEnd >);
    vm->bind(module, "PIN_UnblockSignal(sig, enable)", pin_function< E_PIN_Function::UnblockSignal >);
    vm->bind(module, "PIN_Version()", pin_function< E_PIN_Function::Version >);
    vm->bind(module, "PIN_VmFullPath()", pin_function< E_PIN_Function::VmFullPath >);

    // ATTACH_STATUS
    module->attr().set("ATTACH_INITIATED", py_var(vm, ATTACH_INITIATED));
    module->attr().set("ATTACH_FAILED_DETACH", py_var(vm, ATTACH_FAILED_DETACH));

    // IPOINT
    module->attr().set("IPOINT_BEFORE", py_var(vm, IPOINT_BEFORE));
    module->attr().set("IPOINT_AFTER", py_var(vm, IPOINT_AFTER));
    module->attr().set("IPOINT_ANYWHERE", py_var(vm, IPOINT_ANYWHERE));
    module->attr().set("IPOINT_TAKEN_BRANCH", py_var(vm, IPOINT_TAKEN_BRANCH));

    // IARG_TYPE
    module->attr().set("IARG_ADDRINT", py_var(vm, IARG_ADDRINT));
    module->attr().set("IARG_PTR", py_var(vm, IARG_PTR));
    module->attr().set("IARG_BOOL", py_var(vm, IARG_BOOL));
    module->attr().set("IARG_UINT32", py_var(vm, IARG_UINT32));
    module->attr().set("IARG_UINT64", py_var(vm, IARG_UINT64));
    module->attr().set("IARG_INST_PTR", py_var(vm, IARG_INST_PTR));
    module->attr().set("IARG_REG_VALUE", py_var(vm, IARG_REG_VALUE));
    module->attr().set("IARG_REG_REFERENCE", py_var(vm, IARG_REG_REFERENCE));
    module->attr().set("IARG_REG_CONST_REFERENCE", py_var(vm, IARG_REG_CONST_REFERENCE));
    module->attr().set("IARG_MEMORYREAD_EA", py_var(vm, IARG_MEMORYREAD_EA));
    module->attr().set("IARG_MEMORYREAD2_EA", py_var(vm, IARG_MEMORYREAD2_EA));
    module->attr().set("IARG_MEMORYWRITE_EA", py_var(vm, IARG_MEMORYWRITE_EA));
    module->attr().set("IARG_MEMORYREAD_SIZE", py_var(vm, IARG_MEMORYREAD_SIZE));
    module->attr().set("IARG_MEMORYWRITE_SIZE", py_var(vm, IARG_MEMORYWRITE_SIZE));
    module->attr().set("IARG_MEMORYREAD_PTR", py_var(vm, IARG_MEMORYREAD_PTR));
    module->attr().set("IARG_MEMORYREAD2_PTR", py_var(vm, IARG_MEMORYREAD2_PTR));
    module->attr().set("IARG_MEMORYWRITE_PTR", py_var(vm, IARG_MEMORYWRITE_PTR));
    module->attr().set("IARG_MEMORYOP_PTR", py_var(vm, IARG_MEMORYOP_PTR));
    module->attr().set("IARG_MULTI_MEMORYACCESS_EA", py_var(vm, IARG_MULTI_MEMORYACCESS_EA));
    module->attr().set("IARG_MULTI_ELEMENT_OPERAND", py_var(vm, IARG_MULTI_ELEMENT_OPERAND));
    module->attr().set("IARG_REWRITE_SCATTERED_MEMOP", py_var(vm, IARG_REWRITE_SCATTERED_MEMOP));
    module->attr().set("IARG_EXPLICIT_MEMORY_EA", py_var(vm, IARG_EXPLICIT_MEMORY_EA));
    module->attr().set("IARG_BRANCH_TAKEN", py_var(vm, IARG_BRANCH_TAKEN));
    module->attr().set("IARG_BRANCH_TARGET_ADDR", py_var(vm, IARG_BRANCH_TARGET_ADDR));
    module->attr().set("IARG_FALLTHROUGH_ADDR", py_var(vm, IARG_FALLTHROUGH_ADDR));
    module->attr().set("IARG_EXECUTING", py_var(vm, IARG_EXECUTING));
    module->attr().set("IARG_FIRST_REP_ITERATION", py_var(vm, IARG_FIRST_REP_ITERATION));
    module->attr().set("IARG_SYSCALL_NUMBER", py_var(vm, IARG_SYSCALL_NUMBER));
    module->attr().set("IARG_SYSARG_REFERENCE", py_var(vm, IARG_SYSARG_REFERENCE));
    module->attr().set("IARG_SYSARG_VALUE", py_var(vm, IARG_SYSARG_VALUE));
    module->attr().set("IARG_SYSRET_VALUE", py_var(vm, IARG_SYSRET_VALUE));
    module->attr().set("IARG_SYSRET_ERRNO", py_var(vm, IARG_SYSRET_ERRNO));
    module->attr().set("IARG_FUNCARG_CALLSITE_REFERENCE", py_var(vm, IARG_FUNCARG_CALLSITE_REFERENCE));
    module->attr().set("IARG_FUNCARG_CALLSITE_VALUE", py_var(vm, IARG_FUNCARG_CALLSITE_VALUE));
    module->attr().set("IARG_FUNCARG_ENTRYPOINT_REFERENCE", py_var(vm, IARG_FUNCARG_ENTRYPOINT_REFERENCE));
    module->attr().set("IARG_FUNCARG_ENTRYPOINT_VALUE", py_var(vm, IARG_FUNCARG_ENTRYPOINT_VALUE));
    module->attr().set("IARG_FUNCRET_EXITPOINT_REFERENCE", py_var(vm, IARG_FUNCRET_EXITPOINT_REFERENCE));
    module->attr().set("IARG_FUNCRET_EXITPOINT_VALUE", py_var(vm, IARG_FUNCRET_EXITPOINT_VALUE));
    module->attr().set("IARG_RETURN_IP", py_var(vm, IARG_RETURN_IP));
    module->attr().set("IARG_ORIG_FUNCPTR", py_var(vm, IARG_ORIG_FUNCPTR));
    module->attr().set("IARG_PROTOTYPE", py_var(vm, IARG_PROTOTYPE));
    module->attr().set("IARG_THREAD_ID", py_var(vm, IARG_THREAD_ID));
    module->attr().set("IARG_CONTEXT", py_var(vm, IARG_CONTEXT));
    module->attr().set("IARG_CONST_CONTEXT", py_var(vm, IARG_CONST_CONTEXT));
    module->attr().set("IARG_PARTIAL_CONTEXT", py_var(vm, IARG_PARTIAL_CONTEXT));
    module->attr().set("IARG_PRESERVE", py_var(vm, IARG_PRESERVE));
    module->attr().set("IARG_CHECK_INLINE", py_var(vm, IARG_CHECK_INLINE));
    module->attr().set("IARG_RETURN_REGS", py_var(vm, IARG_RETURN_REGS));
    module->attr().set("IARG_CALL_ORDER", py_var(vm, IARG_CALL_ORDER));
    module->attr().set("IARG_IARGLIST", py_var(vm, IARG_IARGLIST));
    module->attr().set("IARG_FAST_ANALYSIS_CALL", py_var(vm, IARG_FAST_ANALYSIS_CALL));
    module->attr().set("IARG_EXPOSE", py_var(vm, IARG_EXPOSE));
    module->attr().set("IARG_MEMORYOP_EA", py_var(vm, IARG_MEMORYOP_EA));
    module->attr().set("IARG_MEMORYOP_SIZE", py_var(vm, IARG_MEMORYOP_SIZE));
    module->attr().set("IARG_MEMORYOP_MASKED_ON", py_var(vm, IARG_MEMORYOP_MASKED_ON));
    module->attr().set("IARG_TSC", py_var(vm, IARG_TSC));
    // Special IARG to treat python object
    module->attr().set("IARG_PYOBJ", py_var(vm, IARG_PYOBJ));

    //PIN_MEMOP_ENUM
    module->attr().set("PIN_MEMOP_LOAD", py_var(vm, PIN_MEMOP_LOAD));
    module->attr().set("PIN_MEMOP_STORE", py_var(vm, PIN_MEMOP_STORE));

    //PIN_OP_ELEMENT_ACCESS
    module->attr().set("PIN_OP_ELEMENT_ACCESS_READ", py_var(vm, PIN_OP_ELEMENT_ACCESS_READ));
    module->attr().set("PIN_OP_ELEMENT_ACCESS_WRITE", py_var(vm, PIN_OP_ELEMENT_ACCESS_WRITE));
    module->attr().set("PIN_OP_ELEMENT_ACCESS_READWRITE", py_var(vm, PIN_OP_ELEMENT_ACCESS_READWRITE));

    // PROBE_MODE
    module->attr().set("PROBE_MODE_DEFAULT", py_var(vm, PROBE_MODE_DEFAULT));
    module->attr().set("PROBE_MODE_ALLOW_RELOCATION", py_var(vm, PROBE_MODE_ALLOW_RELOCATION));
    module->attr().set("PROBE_MODE_ALLOW_POTENTIAL_BRANCH_TARGET", py_var(vm, PROBE_MODE_ALLOW_POTENTIAL_BRANCH_TARGET));

    // REPLAY_MODE
    module->attr().set("REPLAY_MODE_NONE", py_var(vm, REPLAY_MODE_NONE));
    module->attr().set("REPLAY_MODE_IMAGEOPS", py_var(vm, REPLAY_MODE_IMAGEOPS));
    module->attr().set("REPLAY_MODE_ALL", py_var(vm, REPLAY_MODE_ALL));

    // PIN_PARG
    module->attr().set("PIN_PARG_POINTER", py_var(vm, &PIN_PARG_POINTER));
    module->attr().set("PIN_PARG_BOOL", py_var(vm, &PIN_PARG_BOOL));
    module->attr().set("PIN_PARG_CHAR", py_var(vm, &PIN_PARG_CHAR));
    module->attr().set("PIN_PARG_UCHAR", py_var(vm, &PIN_PARG_UCHAR));
    module->attr().set("PIN_PARG_SCHAR", py_var(vm, &PIN_PARG_SCHAR));
    module->attr().set("PIN_PARG_SHORT", py_var(vm, &PIN_PARG_SHORT));
    module->attr().set("PIN_PARG_USHORT", py_var(vm, &PIN_PARG_USHORT));
    module->attr().set("PIN_PARG_SIZE_T", py_var(vm, &PIN_PARG_SIZE_T));
    module->attr().set("PIN_PARG_INT", py_var(vm, &PIN_PARG_INT));
    module->attr().set("PIN_PARG_UINT", py_var(vm, &PIN_PARG_UINT));
    module->attr().set("PIN_PARG_LONG", py_var(vm, &PIN_PARG_LONG));
    module->attr().set("PIN_PARG_ULONG", py_var(vm, &PIN_PARG_ULONG));
    module->attr().set("PIN_PARG_LONGLONG", py_var(vm, &PIN_PARG_LONGLONG));
    module->attr().set("PIN_PARG_ULONGLONG", py_var(vm, &PIN_PARG_ULONGLONG));
    module->attr().set("PIN_PARG_FLOAT", py_var(vm, &PIN_PARG_FLOAT));
    module->attr().set("PIN_PARG_DOUBLE", py_var(vm, &PIN_PARG_DOUBLE));
    module->attr().set("PIN_PARG_VOID", py_var(vm, &PIN_PARG_VOID));

    // CALLINGSTD_TYPE
    module->attr().set("CALLINGSTD_INVALID", py_var(vm, CALLINGSTD_INVALID));
    module->attr().set("CALLINGSTD_DEFAULT", py_var(vm, CALLINGSTD_DEFAULT));
    module->attr().set("CALLINGSTD_CDECL", py_var(vm, CALLINGSTD_CDECL));
    module->attr().set("CALLINGSTD_REGPARMS", py_var(vm, CALLINGSTD_REGPARMS));
    module->attr().set("CALLINGSTD_STDCALL", py_var(vm, CALLINGSTD_STDCALL));
    module->attr().set("CALLINGSTD_ART", py_var(vm, CALLINGSTD_ART));

    // CONTEXT_CHANGE_REASON
    module->attr().set("CONTEXT_CHANGE_REASON_FATALSIGNAL", py_var(vm, CONTEXT_CHANGE_REASON_FATALSIGNAL));
    module->attr().set("CONTEXT_CHANGE_REASON_SIGNAL", py_var(vm, CONTEXT_CHANGE_REASON_SIGNAL));
    module->attr().set("CONTEXT_CHANGE_REASON_SIGRETURN", py_var(vm, CONTEXT_CHANGE_REASON_SIGRETURN));
    module->attr().set("CONTEXT_CHANGE_REASON_APC", py_var(vm, CONTEXT_CHANGE_REASON_APC));
    module->attr().set("CONTEXT_CHANGE_REASON_EXCEPTION", py_var(vm, CONTEXT_CHANGE_REASON_EXCEPTION));
    module->attr().set("CONTEXT_CHANGE_REASON_CALLBACK", py_var(vm, CONTEXT_CHANGE_REASON_CALLBACK));

    // SMC_ENABLE_DISABLE_TYPE
    module->attr().set("SMC_ENABLE", py_var(vm, SMC_ENABLE));
    module->attr().set("SMC_DISABLE", py_var(vm, SMC_DISABLE));

    // INVALID_TLS_KEY
    module->attr().set("INVALID_TLS_KEY", py_var(vm, INVALID_TLS_KEY));

    //EXCEPT_HANDLING_RESULT
    module->attr().set("EHR_HANDLED", py_var(vm, EHR_HANDLED));
    module->attr().set("EHR_UNHANDLED", py_var(vm, EHR_UNHANDLED));
    module->attr().set("EHR_CONTINUE_SEARCH", py_var(vm, EHR_CONTINUE_SEARCH));

    //XED_ICLASS
    xed_iclass_bind(vm, module);

    // Bind all other pin APIs
    glue_bind_all(vm, module);
    reg_bind_all(vm, module);
    bbl_bind_all(vm, module);
    img_bind_all(vm, module);
    sec_bind_all(vm, module);
    rtn_bind_all(vm, module);
    ins_bind_all(vm, module);
    trace_bind_all(vm, module);
    threads_bind_all(vm, module);
    context_bind_all(vm, module);
    exception_bind_all(vm, module);
}
} // namespace glue

#endif // _GLUE_PIN_H_