/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_TRACE_H_
#define _GLUE_TRACE_H_

#include "../gluepy.h"

namespace glue
{
enum class E_TRACE_Function
{
    Address,
    BblHead,
    BblTail,
    HasFallThrough,
    NumBbl,
    NumIns,
    Rtn,
    Size
};

template< E_TRACE_Function Function > inline PyObject* trace_function(VM* vm, ArgsView args)
{
    TRACE trace = py_cast< TRACE >(vm, args[0]);

    switch (Function)
    {
        case E_TRACE_Function::Address:
            return py_var(vm, TRACE_Address(trace));
        case E_TRACE_Function::BblHead:
            return py_var(vm, TRACE_BblHead(trace).index);
        case E_TRACE_Function::BblTail:
            return py_var(vm, TRACE_BblTail(trace).index);
        case E_TRACE_Function::HasFallThrough:
            return py_var(vm, TRACE_HasFallThrough(trace));
        case E_TRACE_Function::NumBbl:
            return py_var(vm, TRACE_NumBbl(trace));
        case E_TRACE_Function::NumIns:
            return py_var(vm, TRACE_NumIns(trace));
        case E_TRACE_Function::Rtn:
            return py_var(vm, TRACE_Rtn(trace).index);
        case E_TRACE_Function::Size:
            return py_var(vm, TRACE_Size(trace));
        default:
            return nullptr;
    }
    }

    inline void trace_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "TRACE_Address(trace)", trace_function< E_TRACE_Function::Address >);
        vm->bind(module, "TRACE_BblHead(trace)", trace_function< E_TRACE_Function::BblHead >);
        vm->bind(module, "TRACE_BblTail(trace)", trace_function< E_TRACE_Function::BblTail >);
        vm->bind(module, "TRACE_HasFallThrough(trace)", trace_function< E_TRACE_Function::HasFallThrough >);
        vm->bind(module, "TRACE_NumBbl(trace)", trace_function< E_TRACE_Function::NumBbl >);
        vm->bind(module, "TRACE_NumIns(trace)", trace_function< E_TRACE_Function::NumIns >);
        vm->bind(module, "TRACE_Rtn(trace)", trace_function< E_TRACE_Function::Rtn >);
        vm->bind(module, "TRACE_Size(trace)", trace_function< E_TRACE_Function::Size >);
    }

} // namespace glue

#endif // _GLUE_TRACE_H_
