/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_SEC_H_
#define _GLUE_SEC_H_

#include "../gluepy.h"

namespace glue
{
enum class E_SEC_Function
{
    Address,
    Data,
    Img,
    Invalid,
    IsExecutable,
    IsReadable,
    IsWriteable,
    Mapped,
    Name,
    Next,
    Prev,
    RtnHead,
    RtnTail,
    Size,
    Type,
    Valid
};

template< E_SEC_Function Function, BOOL IsSecFirstArg = TRUE > inline PyObject* sec_function(VM* vm, ArgsView args)
{
    SEC sec;
    if constexpr (IsSecFirstArg)
    {
        sec.index = py_cast< INT32 >(vm, args[0]);
    }

    switch (Function)
    {
        case E_SEC_Function::Address:
            return py_var(vm, SEC_Address(sec));
        case E_SEC_Function::Data:
            return py_var(vm, SEC_Data(sec));
        case E_SEC_Function::Img:
            return py_var(vm, SEC_Img(sec).index);
        case E_SEC_Function::Invalid:
            return py_var(vm, SEC_Invalid().index);
        case E_SEC_Function::IsExecutable:
            return py_var(vm, SEC_IsExecutable(sec));
        case E_SEC_Function::IsReadable:
            return py_var(vm, SEC_IsReadable(sec));
        case E_SEC_Function::IsWriteable:
            return py_var(vm, SEC_IsWriteable(sec));
        case E_SEC_Function::Mapped:
            return py_var(vm, SEC_Mapped(sec));
        case E_SEC_Function::Name:
            return py_var(vm, SEC_Name(sec));
        case E_SEC_Function::Next:
            return py_var(vm, SEC_Next(sec).index);
        case E_SEC_Function::Prev:
            return py_var(vm, SEC_Prev(sec).index);
        case E_SEC_Function::RtnHead:
            return py_var(vm, SEC_RtnHead(sec).index);
        case E_SEC_Function::RtnTail:
            return py_var(vm, SEC_RtnTail(sec).index);
        case E_SEC_Function::Size:
            return py_var(vm, SEC_Size(sec));
        case E_SEC_Function::Type:
            return py_var(vm, SEC_Type(sec));
        case E_SEC_Function::Valid:
            return py_var(vm, SEC_Valid(sec));
        default:
            return nullptr;
    }
    }

    inline void sec_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "SEC_Address(sec)", sec_function< E_SEC_Function::Address >);
        vm->bind(module, "SEC_Data(sec)", sec_function< E_SEC_Function::Data >);
        vm->bind(module, "SEC_Img(sec)", sec_function< E_SEC_Function::Img >);
        vm->bind(module, "SEC_Invalid()", sec_function< E_SEC_Function::Invalid, FALSE >);
        vm->bind(module, "SEC_IsExecutable(sec)", sec_function< E_SEC_Function::IsExecutable >);
        vm->bind(module, "SEC_IsReadable(sec)", sec_function< E_SEC_Function::IsReadable >);
        vm->bind(module, "SEC_IsWriteable(sec)", sec_function< E_SEC_Function::IsWriteable >);
        vm->bind(module, "SEC_Mapped(sec)", sec_function< E_SEC_Function::Mapped >);
        vm->bind(module, "SEC_Name(sec)", sec_function< E_SEC_Function::Name >);
        vm->bind(module, "SEC_Next(sec)", sec_function< E_SEC_Function::Next >);
        vm->bind(module, "SEC_Prev(sec)", sec_function< E_SEC_Function::Prev >);
        vm->bind(module, "SEC_RtnHead(sec)", sec_function< E_SEC_Function::RtnHead >);
        vm->bind(module, "SEC_RtnTail(sec)", sec_function< E_SEC_Function::RtnTail >);
        vm->bind(module, "SEC_Size(sec)", sec_function< E_SEC_Function::Size >);
        vm->bind(module, "SEC_Type(sec)", sec_function< E_SEC_Function::Type >);
        vm->bind(module, "SEC_Valid(sec)", sec_function< E_SEC_Function::Valid >);

        // SEC_TYPE
        module->attr().set("SEC_TYPE_INVALID", py_var(vm, SEC_TYPE_INVALID));
        module->attr().set("SEC_TYPE_UNUSED", py_var(vm, SEC_TYPE_UNUSED));
        module->attr().set("SEC_TYPE_REGREL", py_var(vm, SEC_TYPE_REGREL));
        module->attr().set("SEC_TYPE_DYNREL", py_var(vm, SEC_TYPE_DYNREL));
        module->attr().set("SEC_TYPE_EXEC", py_var(vm, SEC_TYPE_EXEC));
        module->attr().set("SEC_TYPE_DATA", py_var(vm, SEC_TYPE_DATA));
        module->attr().set("SEC_TYPE_DYNAMIC", py_var(vm, SEC_TYPE_DYNAMIC));
        module->attr().set("SEC_TYPE_OPD", py_var(vm, SEC_TYPE_OPD));
        module->attr().set("SEC_TYPE_GOT", py_var(vm, SEC_TYPE_GOT));
        module->attr().set("SEC_TYPE_STACK", py_var(vm, SEC_TYPE_STACK));
        module->attr().set("SEC_TYPE_PLTOFF", py_var(vm, SEC_TYPE_PLTOFF));
        module->attr().set("SEC_TYPE_HASH", py_var(vm, SEC_TYPE_HASH));
        module->attr().set("SEC_TYPE_LSDA", py_var(vm, SEC_TYPE_LSDA));
        module->attr().set("SEC_TYPE_UNWIND", py_var(vm, SEC_TYPE_UNWIND));
        module->attr().set("SEC_TYPE_UNWINDINFO", py_var(vm, SEC_TYPE_UNWINDINFO));
        module->attr().set("SEC_TYPE_REGSYM", py_var(vm, SEC_TYPE_REGSYM));
        module->attr().set("SEC_TYPE_DYNSYM", py_var(vm, SEC_TYPE_DYNSYM));
        module->attr().set("SEC_TYPE_DEBUG", py_var(vm, SEC_TYPE_DEBUG));
        module->attr().set("SEC_TYPE_BSS", py_var(vm, SEC_TYPE_BSS));
        module->attr().set("SEC_TYPE_SYMSTR", py_var(vm, SEC_TYPE_SYMSTR));
        module->attr().set("SEC_TYPE_DYNSTR", py_var(vm, SEC_TYPE_DYNSTR));
        module->attr().set("SEC_TYPE_SECSTR", py_var(vm, SEC_TYPE_SECSTR));
        module->attr().set("SEC_TYPE_COMMENT", py_var(vm, SEC_TYPE_COMMENT));
        module->attr().set("SEC_TYPE_LOOS", py_var(vm, SEC_TYPE_LOOS));
        module->attr().set("SEC_TYPE_USER", py_var(vm, SEC_TYPE_USER));
        module->attr().set("SEC_TYPE_PROC", py_var(vm, SEC_TYPE_PROC));
    }
} // namespace glue

#endif // _GLUE_SEC_H_