/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_SYM_H_
#define _GLUE_SYM_H_

#include "../gluepy.h"

namespace glue
{
enum class E_SYM_Function
{
    Address,
    Dynamic,
    GeneratedByPin,
    IFuncImplementation,
    IFuncResolver,
    Index,
    Invalid,
    Name,
    Next,
    PIN_UndecorateSymbolName,
    Prev,
    Valid,
    Value
};

template< E_SYM_Function Function, BOOL IsSymFirstArg = TRUE > inline PyObject* sym_function(VM* vm, ArgsView args)
{
    SYM sym;
    if constexpr (IsSymFirstArg)
    {
        sym.index = py_cast< INT32 >(vm, args[0]);
    }

    switch (Function)
    {
        case E_SYM_Function::Address:
            return py_var(vm, SYM_Address(sym));
        case E_SYM_Function::Dynamic:
            return py_var(vm, SYM_Dynamic(sym));
        case E_SYM_Function::GeneratedByPin:
            return py_var(vm, SYM_GeneratedByPin(sym));
        case E_SYM_Function::IFuncImplementation:
            return py_var(vm, SYM_IFuncImplementation(sym));
        case E_SYM_Function::IFuncResolver:
            return py_var(vm, SYM_IFuncResolver(sym));
        case E_SYM_Function::Index:
            return py_var(vm, SYM_Index(sym));
        case E_SYM_Function::Invalid:
            return py_var(vm, SYM_Invalid().index);
        case E_SYM_Function::Name:
            return py_var(vm, SYM_Name(sym));
        case E_SYM_Function::Next:
            return py_var(vm, SYM_Next(sym).index);
        case E_SYM_Function::PIN_UndecorateSymbolName:
            return py_var(vm,
                          PIN_UndecorateSymbolName(py_cast< const char* >(vm, args[0]), py_cast< UNDECORATION >(vm, args[1])));
        case E_SYM_Function::Prev:
            return py_var(vm, SYM_Prev(sym).index);
        case E_SYM_Function::Valid:
            return py_var(vm, SYM_Valid(sym));
        case E_SYM_Function::Value:
            return py_var(vm, SYM_Value(sym));
        default:
            return nullptr;
    }
    }

    inline void sym_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "SYM_Address(sym)", sym_function< E_SYM_Function::Address >);
        vm->bind(module, "SYM_Dynamic(sym)", sym_function< E_SYM_Function::Dynamic >);
        vm->bind(module, "SYM_GeneratedByPin(sym)", sym_function< E_SYM_Function::GeneratedByPin >);
        vm->bind(module, "SYM_IFuncImplementation(sym)", sym_function< E_SYM_Function::IFuncImplementation >);
        vm->bind(module, "SYM_IFuncResolver(sym)", sym_function< E_SYM_Function::IFuncResolver >);
        vm->bind(module, "SYM_Index(sym)", sym_function< E_SYM_Function::Index >);
        vm->bind(module, "SYM_Invalid()", sym_function< E_SYM_Function::Invalid >, FALSE);
        vm->bind(module, "SYM_Name(sym)", sym_function< E_SYM_Function::Name >);
        vm->bind(module, "SYM_Next(sym)", sym_function< E_SYM_Function::Next >);
        vm->bind(module, "PIN_UndecorateSymbolName(symbolName, style)", sym_function< E_SYM_Function::PIN_UndecorateSymbolName >,
                 FALSE);
        vm->bind(module, "SYM_Prev(sym)", sym_function< E_SYM_Function::Prev >);
        vm->bind(module, "SYM_Valid(sym)", sym_function< E_SYM_Function::Valid >);
        vm->bind(module, "SYM_Value(sym)", sym_function< E_SYM_Function::Value >);

        // UNDECORATION
        module->attr().set("UNDECORATION_COMPLETE", py_var(vm, UNDECORATION_COMPLETE));
        module->attr().set("UNDECORATION_NAME_ONLY", py_var(vm, UNDECORATION_NAME_ONLY));
    }

} // namespace glue

#endif // _GLUE_SYM_H_
