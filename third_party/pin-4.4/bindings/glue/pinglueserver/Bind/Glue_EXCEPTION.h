/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_EXCEPTION_H_
#define _GLUE_EXCEPTION_H_

#include "../gluepy.h"

namespace glue
{
enum class E_EXCEPTION_Function
{
    PIN_InitExceptionInfo,
    PIN_InitAccessFaultInfo,
    PIN_InitWindowsExceptionInfo,
    PIN_GetExceptionCode,
    PIN_GetExceptionClass,
    PIN_GetExceptionAddress,
    PIN_SetExceptionAddress,
    PIN_GetFaultyAccessType,
    PIN_GetFaultyAccessAddress,
    PIN_GetFpErrorSet,
    PIN_GetWindowsExceptionCode,
    PIN_CountWindowsExceptionArguments,
    PIN_GetWindowsExceptionArgument,
    PIN_ExceptionToString
    //PIN_RaiseException
};

template< E_EXCEPTION_Function Function, BOOL IsEXCEPTION_INFOFirstArg = TRUE >
inline PyObject* exception_function(VM* vm, ArgsView args)
{
    EXCEPTION_INFO* pExceptInfo;

    if constexpr (IsEXCEPTION_INFOFirstArg)
    {
        pExceptInfo = py_cast< EXCEPTION_INFO* >(vm, args[0]);
    }

    switch (Function)
    {
        case E_EXCEPTION_Function::PIN_InitExceptionInfo:
            PIN_InitExceptionInfo(pExceptInfo, py_cast< EXCEPTION_CODE >(vm, args[1]), py_cast< ADDRINT >(vm, args[2]));
            return vm->None;
        case E_EXCEPTION_Function::PIN_InitAccessFaultInfo:
            PIN_InitAccessFaultInfo(pExceptInfo, py_cast< EXCEPTION_CODE >(vm, args[1]), py_cast< ADDRINT >(vm, args[2]),
                                    py_cast< ADDRINT >(vm, args[3]), py_cast< FAULTY_ACCESS_TYPE >(vm, args[4]));
            return vm->None;
        case E_EXCEPTION_Function::PIN_InitWindowsExceptionInfo:
            PIN_InitWindowsExceptionInfo(pExceptInfo, py_cast< UINT32 >(vm, args[1]), py_cast< ADDRINT >(vm, args[2]),
                                         py_cast< UINT32 >(vm, args[3]), py_cast< ADDRINT* >(vm, args[4]));
        case E_EXCEPTION_Function::PIN_GetExceptionCode:
            return py_var(vm, PIN_GetExceptionCode(pExceptInfo));
        case E_EXCEPTION_Function::PIN_GetExceptionClass:
            return py_var(vm, PIN_GetExceptionClass(py_cast< EXCEPTION_CODE >(vm, args[0])));
        case E_EXCEPTION_Function::PIN_GetExceptionAddress:
            return py_var(vm, PIN_GetExceptionAddress(pExceptInfo));
        case E_EXCEPTION_Function::PIN_SetExceptionAddress:
            PIN_SetExceptionAddress(pExceptInfo, py_cast< ADDRINT >(vm, args[1]));
            return vm->None;
        case E_EXCEPTION_Function::PIN_GetFaultyAccessType:
            return py_var(vm, PIN_GetFaultyAccessType(pExceptInfo));
        case E_EXCEPTION_Function::PIN_GetFaultyAccessAddress:
            return py_var(vm, PIN_GetFaultyAccessAddress(pExceptInfo, py_cast< ADDRINT* >(vm, args[1])));
        case E_EXCEPTION_Function::PIN_GetFpErrorSet:
            return py_var(vm, PIN_GetFpErrorSet(pExceptInfo));
        case E_EXCEPTION_Function::PIN_GetWindowsExceptionCode:
            return py_var(vm, PIN_GetWindowsExceptionCode(pExceptInfo));
        case E_EXCEPTION_Function::PIN_CountWindowsExceptionArguments:
            return py_var(vm, PIN_CountWindowsExceptionArguments(pExceptInfo));
        case E_EXCEPTION_Function::PIN_GetWindowsExceptionArgument:
            return py_var(vm, PIN_GetWindowsExceptionArgument(pExceptInfo, py_cast< UINT32 >(vm, args[1])));
        case E_EXCEPTION_Function::PIN_ExceptionToString:
            return py_var(vm, PIN_ExceptionToString(pExceptInfo));
            // See PIN_ExecuteAt comment, anyway this API is Windows only.
            // case E_EXCEPTION_Function::PIN_RaiseException:
            //     PIN_RaiseException(py_cast< const CONTEXT* >(vm, args[0]), py_cast< THREADID >(vm, args[1]),
            //                        py_cast< const EXCEPTION_INFO* >(vm, args[2]));
            //     return vm->None;

        default:
            return nullptr;
    }
    }

    inline void exception_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "PIN_InitExceptionInfo(pExceptInfo, exceptCode, exceptAddress)",
                 exception_function< E_EXCEPTION_Function::PIN_InitExceptionInfo >);
        vm->bind(module, "PIN_InitAccessFaultInfo(pExceptInfo, exceptCode, exceptAddress, accessAddress, accessType=0)",
                 exception_function< E_EXCEPTION_Function::PIN_InitAccessFaultInfo >);
        vm->bind(module, "PIN_InitWindowsExceptionInfo(pExceptInfo, sysExceptCode, exceptAddress, numArgs=0, pArgs=0)",
                 exception_function< E_EXCEPTION_Function::PIN_InitWindowsExceptionInfo >);
        vm->bind(module, "PIN_GetExceptionCode(pExceptInfo)", exception_function< E_EXCEPTION_Function::PIN_GetExceptionCode >);
        vm->bind(module, "PIN_GetExceptionClass(exceptCode)",
                 exception_function< E_EXCEPTION_Function::PIN_GetExceptionClass, FALSE >);
        vm->bind(module, "PIN_GetExceptionAddress(pExceptInfo)",
                 exception_function< E_EXCEPTION_Function::PIN_GetExceptionAddress >);
        vm->bind(module, "PIN_SetExceptionAddress(pExceptInfo, exceptAddress)",
                 exception_function< E_EXCEPTION_Function::PIN_SetExceptionAddress >);
        vm->bind(module, "PIN_GetFaultyAccessType(pExceptInfo)",
                 exception_function< E_EXCEPTION_Function::PIN_GetFaultyAccessType >);
        vm->bind(module, "PIN_GetFaultyAccessAddress(pExceptInfo, pAccessAddress)",
                 exception_function< E_EXCEPTION_Function::PIN_GetFaultyAccessAddress >);
        vm->bind(module, "PIN_GetFpErrorSet(pExceptInfo)", exception_function< E_EXCEPTION_Function::PIN_GetFpErrorSet >);
        vm->bind(module, "PIN_GetWindowsExceptionCode(pExceptInfo)",
                 exception_function< E_EXCEPTION_Function::PIN_GetWindowsExceptionCode >);
        vm->bind(module, "PIN_CountWindowsExceptionArguments(pExceptInfo)",
                 exception_function< E_EXCEPTION_Function::PIN_CountWindowsExceptionArguments >);
        vm->bind(module, "PIN_GetWindowsExceptionArgument(pExceptInfo, argNum)",
                 exception_function< E_EXCEPTION_Function::PIN_GetWindowsExceptionArgument >);
        vm->bind(module, "PIN_ExceptionToString(pExceptInfo)", exception_function< E_EXCEPTION_Function::PIN_ExceptionToString >);
        // vm->bind(module, "PIN_RaiseException(ctxt, tid, pExceptInfo)",
        //          exception_function< E_EXCEPTION_Function::PIN_RaiseException, FALSE >);

        //EXCEPTION_CLASS
        module->attr().set("EXCEPTCLASS_NONE", py_var(vm, EXCEPTCLASS_NONE));
        module->attr().set("EXCEPTCLASS_UNKNOWN", py_var(vm, EXCEPTCLASS_UNKNOWN));
        module->attr().set("EXCEPTCLASS_ACCESS_FAULT", py_var(vm, EXCEPTCLASS_ACCESS_FAULT));
        module->attr().set("EXCEPTCLASS_INVALID_INS", py_var(vm, EXCEPTCLASS_INVALID_INS));
        module->attr().set("EXCEPTCLASS_INT_ERROR", py_var(vm, EXCEPTCLASS_INT_ERROR));
        module->attr().set("EXCEPTCLASS_FP_ERROR", py_var(vm, EXCEPTCLASS_FP_ERROR));
        module->attr().set("EXCEPTCLASS_MULTIPLE_FP_ERROR", py_var(vm, EXCEPTCLASS_MULTIPLE_FP_ERROR));
        module->attr().set("EXCEPTCLASS_DEBUG", py_var(vm, EXCEPTCLASS_DEBUG));
        module->attr().set("EXCEPTCLASS_OS", py_var(vm, EXCEPTCLASS_OS));
        //EXCEPTION_CODE
        module->attr().set("EXCEPTCODE_NONE", py_var(vm, EXCEPTCODE_NONE));
        module->attr().set("EXCEPTCODE_ACCESS_INVALID_ADDRESS", py_var(vm, EXCEPTCODE_ACCESS_INVALID_ADDRESS));
        module->attr().set("EXCEPTCODE_ACCESS_DENIED", py_var(vm, EXCEPTCODE_ACCESS_DENIED));
        module->attr().set("EXCEPTCODE_ACCESS_INVALID_PAGE", py_var(vm, EXCEPTCODE_ACCESS_INVALID_PAGE));
        module->attr().set("EXCEPTCODE_ACCESS_MISALIGNED", py_var(vm, EXCEPTCODE_ACCESS_MISALIGNED));
        module->attr().set("EXCEPTCODE_ILLEGAL_INS", py_var(vm, EXCEPTCODE_ILLEGAL_INS));
        module->attr().set("EXCEPTCODE_PRIVILEGED_INS", py_var(vm, EXCEPTCODE_PRIVILEGED_INS));
        module->attr().set("EXCEPTCODE_INT_DIVIDE_BY_ZERO", py_var(vm, EXCEPTCODE_INT_DIVIDE_BY_ZERO));
        module->attr().set("EXCEPTCODE_INT_OVERFLOW_TRAP", py_var(vm, EXCEPTCODE_INT_OVERFLOW_TRAP));
        module->attr().set("EXCEPTCODE_INT_BOUNDS_EXCEEDED", py_var(vm, EXCEPTCODE_INT_BOUNDS_EXCEEDED));
        module->attr().set("EXCEPTCODE_X87_DIVIDE_BY_ZERO", py_var(vm, EXCEPTCODE_X87_DIVIDE_BY_ZERO));
        module->attr().set("EXCEPTCODE_X87_OVERFLOW", py_var(vm, EXCEPTCODE_X87_OVERFLOW));
        module->attr().set("EXCEPTCODE_X87_UNDERFLOW", py_var(vm, EXCEPTCODE_X87_UNDERFLOW));
        module->attr().set("EXCEPTCODE_X87_INEXACT_RESULT", py_var(vm, EXCEPTCODE_X87_INEXACT_RESULT));
        module->attr().set("EXCEPTCODE_X87_INVALID_OPERATION", py_var(vm, EXCEPTCODE_X87_INVALID_OPERATION));
        module->attr().set("EXCEPTCODE_X87_DENORMAL_OPERAND", py_var(vm, EXCEPTCODE_X87_DENORMAL_OPERAND));
        module->attr().set("EXCEPTCODE_X87_STACK_ERROR", py_var(vm, EXCEPTCODE_X87_STACK_ERROR));
        module->attr().set("EXCEPTCODE_SIMD_DIVIDE_BY_ZERO", py_var(vm, EXCEPTCODE_SIMD_DIVIDE_BY_ZERO));
        module->attr().set("EXCEPTCODE_SIMD_OVERFLOW", py_var(vm, EXCEPTCODE_SIMD_OVERFLOW));
        module->attr().set("EXCEPTCODE_SIMD_UNDERFLOW", py_var(vm, EXCEPTCODE_SIMD_UNDERFLOW));
        module->attr().set("EXCEPTCODE_SIMD_INEXACT_RESULT", py_var(vm, EXCEPTCODE_SIMD_INEXACT_RESULT));
        module->attr().set("EXCEPTCODE_SIMD_INVALID_OPERATION", py_var(vm, EXCEPTCODE_SIMD_INVALID_OPERATION));
        module->attr().set("EXCEPTCODE_SIMD_DENORMAL_OPERAND", py_var(vm, EXCEPTCODE_SIMD_DENORMAL_OPERAND));
        module->attr().set("EXCEPTCODE_DBG_BREAKPOINT_TRAP", py_var(vm, EXCEPTCODE_DBG_BREAKPOINT_TRAP));
        module->attr().set("EXCEPTCODE_DBG_SINGLE_STEP_TRAP", py_var(vm, EXCEPTCODE_DBG_SINGLE_STEP_TRAP));
        module->attr().set("EXCEPTCODE_ACCESS_WINDOWS_GUARD_PAGE", py_var(vm, EXCEPTCODE_ACCESS_WINDOWS_GUARD_PAGE));
        module->attr().set("EXCEPTCODE_ACCESS_WINDOWS_STACK_OVERFLOW", py_var(vm, EXCEPTCODE_ACCESS_WINDOWS_STACK_OVERFLOW));
        module->attr().set("EXCEPTCODE_WINDOWS", py_var(vm, EXCEPTCODE_WINDOWS));
        module->attr().set("EXCEPTCODE_RECEIVED_UNKNOWN", py_var(vm, EXCEPTCODE_RECEIVED_UNKNOWN));
        module->attr().set("EXCEPTCODE_RECEIVED_ACCESS_FAULT", py_var(vm, EXCEPTCODE_RECEIVED_ACCESS_FAULT));
        module->attr().set("EXCEPTCODE_RECEIVED_AMBIGUOUS_X87", py_var(vm, EXCEPTCODE_RECEIVED_AMBIGUOUS_X87));
        module->attr().set("EXCEPTCODE_RECEIVED_AMBIGUOUS_SIMD", py_var(vm, EXCEPTCODE_RECEIVED_AMBIGUOUS_SIMD));
        //FAULTY_ACCESS_TYPE
        module->attr().set("FAULTY_ACCESS_TYPE_UNKNOWN", py_var(vm, FAULTY_ACCESS_TYPE_UNKNOWN));
        module->attr().set("FAULTY_ACCESS_READ", py_var(vm, FAULTY_ACCESS_READ));
        module->attr().set("FAULTY_ACCESS_WRITE", py_var(vm, FAULTY_ACCESS_WRITE));
        module->attr().set("FAULTY_ACCESS_EXECUTE", py_var(vm, FAULTY_ACCESS_EXECUTE));
        //FPERROR
        module->attr().set("FPERROR_DIVIDE_BY_ZERO", py_var(vm, FPERROR_DIVIDE_BY_ZERO));
        module->attr().set("FPERROR_OVERFLOW", py_var(vm, FPERROR_OVERFLOW));
        module->attr().set("FPERROR_UNDERFLOW", py_var(vm, FPERROR_UNDERFLOW));
        module->attr().set("FPERROR_INEXACT_RESULT", py_var(vm, FPERROR_INEXACT_RESULT));
        module->attr().set("FPERROR_INVALID_OPERATION", py_var(vm, FPERROR_INVALID_OPERATION));
        module->attr().set("FPERROR_DENORMAL_OPERAND", py_var(vm, FPERROR_DENORMAL_OPERAND));
        module->attr().set("FPERROR_X87_STACK_ERROR", py_var(vm, FPERROR_X87_STACK_ERROR));
    }

} // namespace glue

#endif // _GLUE_EXCEPTION_H_