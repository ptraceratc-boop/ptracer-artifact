/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_GLUE_H_
#define _GLUE_GLUE_H_

#include "../gluepy.h"

namespace glue
{
constexpr int MAX_APP_ARGS = 8;

/* @brief Define a type for the function pointer that takes a variable number of uintptr_t arguments */
using FP_GENERIC = uintptr_t (*)(...);

/**
 * @brief Fills function arguments from python args with a vector of parameter types.
 *
 * This function is designed to take a vector of parameter types (pargVec) and use it to
 * populate an array of function arguments (funcArgs) that will be passed to app function.
 *
 * @param vm                   pocketpy vm
 * @param [in]  pyVariadicArgs A reference to a Tuple object that filled with the python arguments.
 * @param [out] funcArgs       A reference to an array of uintptr_t that will be filled with the function arguments.
 *                             The array size is fixed at 8 elements.
 * @param [in]  pargVec        A reference to a vector of PARG_T pointers, representing the parameter types of pyVariadicArgs.
 */
inline void glue_fill_args_from_pargs(VM* vm, Tuple& pyVariadicArgs, uintptr_t (&funcArgs)[MAX_APP_ARGS],
                                      std::vector< PARG_T* >& pargVec)
{
    size_t argsIDX = 0;
    for (PARG_T* parg : pargVec)
    {
        switch (parg->_parg)
        {
            case PARG_POINTER:
                funcArgs[argsIDX] = (uintptr_t)py_cast< ADDRINT >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_BOOL:
                funcArgs[argsIDX] = (uintptr_t)py_cast< bool >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_CHAR:
                funcArgs[argsIDX] = (uintptr_t)py_cast< char >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_UCHAR:
                funcArgs[argsIDX] = (uintptr_t)py_cast< unsigned char >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_SCHAR:
                funcArgs[argsIDX] = (uintptr_t)py_cast< signed char >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_SHORT:
                funcArgs[argsIDX] = (uintptr_t)py_cast< short >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_USHORT:
                funcArgs[argsIDX] = (uintptr_t)py_cast< unsigned short >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_INT:
                funcArgs[argsIDX] = (uintptr_t)py_cast< int >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_UINT:
                funcArgs[argsIDX] = (uintptr_t)py_cast< unsigned int >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_LONG:
                funcArgs[argsIDX] = (uintptr_t)py_cast< long >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_ULONG:
                funcArgs[argsIDX] = (uintptr_t)py_cast< unsigned long >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_LONGLONG:
                funcArgs[argsIDX] = (uintptr_t)py_cast< long long >(vm, pyVariadicArgs[argsIDX]);
                break;
            case PARG_ULONGLONG:
                funcArgs[argsIDX] = (uintptr_t)py_cast< unsigned long long >(vm, pyVariadicArgs[argsIDX]);
                break;

            default:
                // from PROTO_Allocate() doc:
                // currently PARG_FLOAT, PARG_DOUBLE are supported as return type only and can not be used as function argument types
                vm->TypeError("Unsupported PARG_TYPE type: " + std::to_string(parg->_parg));
        }
        ++argsIDX;
    }
}

inline PyObject* glue_call_application_function_probed(VM* vm, ArgsView args)
{
    // Current implementation supports args size up to 8.
    static_assert(MAX_APP_ARGS == 8, "");
    ASSERTX(3 == args.size());

    AFUNPTR origFunPtr   = py_cast< AFUNPTR >(vm, args[0]);
    auto protoObj        = py_cast< Glue_Proto* >(vm, args[1]);
    Tuple pyVariadicArgs = py_cast< Tuple >(vm, args[2]);
    ASSERTX(pyVariadicArgs.size() <= MAX_APP_ARGS);
    ASSERTX(protoObj->pargsTypeList.size() == pyVariadicArgs.size());

    uintptr_t funcArgs[MAX_APP_ARGS] = {0};
    uintptr_t ret                    = 0;

    glue_fill_args_from_pargs(vm, pyVariadicArgs, funcArgs, protoObj->pargsTypeList);

    // Release the GIL so that the call can re-enter python if necessary
    // and to prevent deadlocks with instrumentation routines that may be
    // waiting to acquire the GIL but also hold Pin Client lock
    Scoped_GIL_temp_release tempUnlock;
    ret = (uintptr_t)((FP_GENERIC)origFunPtr)(funcArgs[0], funcArgs[1], funcArgs[2], funcArgs[3], funcArgs[4], funcArgs[5],
                                              funcArgs[6], funcArgs[7]);

    return py_var(vm, ret);
}

inline PyObject* glue_call_application_function(VM* vm, ArgsView args)
{
    // Current implementation supports args size up to 8.
    static_assert(MAX_APP_ARGS == 8, "");
    ASSERTX(7 == args.size());

    auto ctxt            = py_cast< const CONTEXT* >(vm, args[0]);
    auto tid             = py_cast< THREADID >(vm, args[1]);
    auto cstype          = py_cast< CALLINGSTD_TYPE >(vm, args[2]);
    auto origFunPtr      = py_cast< AFUNPTR >(vm, args[3]);
    auto isNatively      = py_cast< BOOL >(vm, args[4]);
    auto protoObj        = py_cast< Glue_Proto* >(vm, args[5]);
    Tuple pyVariadicArgs = py_cast< Tuple >(vm, args[6]);

    ASSERTX(pyVariadicArgs.size() <= MAX_APP_ARGS);
    ASSERTX(protoObj->pargsTypeList.size() == pyVariadicArgs.size());

    uintptr_t funcArgs[MAX_APP_ARGS] = {0};
    uintptr_t ret                    = 0;

    glue_fill_args_from_pargs(vm, pyVariadicArgs, funcArgs, protoObj->pargsTypeList);

    CALL_APPLICATION_FUNCTION_PARAM param;
    memset(&param, 0, sizeof(param));
    param.native = isNatively;

    // Release the GIL so that the call can re-enter python if necessary
    // and to prevent deadlocks with instrumentation routines that may be
    // waiting to acquire the GIL but also hold Pin Client lock
    Scoped_GIL_temp_release tempUnlock;
    PIN_CallApplicationFunction(ctxt, tid, cstype, origFunPtr, &param, PIN_PARG(uintptr_t), &ret, PIN_PARG(uintptr_t),
                                funcArgs[0], PIN_PARG(uintptr_t), funcArgs[1], PIN_PARG(uintptr_t), funcArgs[2],
                                PIN_PARG(uintptr_t), funcArgs[3], PIN_PARG(uintptr_t), funcArgs[4], PIN_PARG(uintptr_t),
                                funcArgs[5], PIN_PARG(uintptr_t), funcArgs[6], PIN_PARG(uintptr_t), funcArgs[7], PIN_PARG_END());

    return py_var(vm, ret);
}

inline void glue_bind_all(VM* vm, PyObject* module)
{
    if (PIN_IsProbeMode())
    {
        vm->bind(module, "GLUE_CallApplicationFunctionProbed(origFunPtr, proto, *args)", glue_call_application_function_probed);
    }
    else
    {
        vm->bind(module, "GLUE_CallApplicationFunction(ctxt, tid, cstype, origFunPtr, isNatively, proto, *args)",
                 glue_call_application_function);
    }
}

} // namespace glue

#endif // _GLUE_GLUE_H_
