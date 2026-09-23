/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_BBL_H_
#define _GLUE_BBL_H_

#include "../gluepy.h"

namespace glue
{
enum class E_BBL_Function
{
    Address,
    HasFallThrough,
    InsHead,
    InsTail,
    Next,
    NumIns,
    Original,
    Prev,
    Size,
    Valid
};

template< E_BBL_Function Function > inline PyObject* bbl_function(VM* vm, ArgsView args)
{
    BBL bbl;
    bbl.index = py_cast< INT32 >(vm, args[0]);

    switch (Function)
    {
        case E_BBL_Function::Address:
            return py_var(vm, BBL_Address(bbl));
        case E_BBL_Function::HasFallThrough:
            return py_var(vm, BBL_HasFallThrough(bbl));
        case E_BBL_Function::InsHead:
            return py_var(vm, BBL_InsHead(bbl).index);
        case E_BBL_Function::InsTail:
            return py_var(vm, BBL_InsTail(bbl).index);
        case E_BBL_Function::Next:
            return py_var(vm, BBL_Next(bbl).index);
        case E_BBL_Function::NumIns:
            return py_var(vm, BBL_NumIns(bbl));
        case E_BBL_Function::Original:
            return py_var(vm, BBL_Original(bbl));
        case E_BBL_Function::Prev:
            return py_var(vm, BBL_Prev(bbl).index);
        case E_BBL_Function::Size:
            return py_var(vm, BBL_Size(bbl));
        case E_BBL_Function::Valid:
            return py_var(vm, BBL_Valid(bbl));
        default:
            return nullptr;
    }
    }

    inline void bbl_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "BBL_Address(bbl)", bbl_function< E_BBL_Function::Address >);
        vm->bind(module, "BBL_HasFallThrough(bbl)", bbl_function< E_BBL_Function::HasFallThrough >);
        vm->bind(module, "BBL_InsHead(bbl)", bbl_function< E_BBL_Function::InsHead >);
        vm->bind(module, "BBL_InsTail(bbl)", bbl_function< E_BBL_Function::InsTail >);
        vm->bind(module, "BBL_Next(bbl)", bbl_function< E_BBL_Function::Next >);
        vm->bind(module, "BBL_NumIns(bbl)", bbl_function< E_BBL_Function::NumIns >);
        vm->bind(module, "BBL_Original(bbl)", bbl_function< E_BBL_Function::Original >);
        vm->bind(module, "BBL_Prev(bbl)", bbl_function< E_BBL_Function::Prev >);
        vm->bind(module, "BBL_Size(bbl)", bbl_function< E_BBL_Function::Size >);
        vm->bind(module, "BBL_Valid(bbl)", bbl_function< E_BBL_Function::Valid >);
    }

} // namespace glue

#endif // _GLUE_BBL_H_