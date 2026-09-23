/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_RTN_H_
#define _GLUE_RTN_H_

#include "../gluepy.h"

namespace glue
{
enum class E_RTN_Function
{
    Address,
    Close,
    CreateAt,
    DynamicMethodId,
    FindByAddress,
    FindByName,
    FindNameByAddress,
    Funptr,
    Id,
    IFuncImplementation,
    IFuncResolver,
    InsHead,
    InsHeadOnly,
    InsTail,
    Invalid,
    IsArtificial,
    IsDynamic,
    IsSafeForProbedInsertion,
    IsSafeForProbedReplacementEx,
    IsSafeForProbedReplacement,
    Name,
    Next,
    NumIns,
    Open,
    Prev,
    Range,
    Sec,
    Size,
    Sym,
    Valid
};

template< E_RTN_Function Function, BOOL IsRtnFirstArg = TRUE > inline PyObject* rtn_function(VM* vm, ArgsView args)
{
    RTN rtn;
    if constexpr (IsRtnFirstArg)
    {
        rtn.index = py_cast< INT32 >(vm, args[0]);
    }

    switch (Function)
    {
        case E_RTN_Function::Address:
            return py_var(vm, RTN_Address(rtn));
        case E_RTN_Function::Close:
            RTN_Close(rtn);
            return vm->None;
        case E_RTN_Function::CreateAt:
            return py_var(vm, RTN_CreateAt(py_cast< ADDRINT >(vm, args[0]), py_cast< const char* >(vm, args[1])).index);
        case E_RTN_Function::DynamicMethodId:
            return py_var(vm, RTN_DynamicMethodId(rtn));
        case E_RTN_Function::FindByAddress:
            return py_var(vm, RTN_FindByAddress(py_cast< ADDRINT >(vm, args[0])).index);
        case E_RTN_Function::FindNameByAddress:
            return py_var(vm, RTN_FindNameByAddress(py_cast< ADDRINT >(vm, args[0])));
        case E_RTN_Function::Funptr:
            return py_var(vm, RTN_Funptr(rtn));
        case E_RTN_Function::Id:
            return py_var(vm, RTN_Id(rtn));
        case E_RTN_Function::FindByName:
        {
            IMG img;
            img.index = py_cast< INT32 >(vm, args[0]);
            auto name = py_cast< const CHAR* >(vm, args[1]);
            return py_var(vm, RTN_FindByName(img, name).index);
        }
        case E_RTN_Function::IFuncImplementation:
            return py_var(vm, RTN_IFuncImplementation(rtn).index);
        case E_RTN_Function::IFuncResolver:
            return py_var(vm, RTN_IFuncResolver(rtn).index);
        case E_RTN_Function::InsHead:
            return py_var(vm, RTN_InsHead(rtn).index);
        case E_RTN_Function::InsHeadOnly:
            return py_var(vm, RTN_InsHeadOnly(rtn).index);
        case E_RTN_Function::InsTail:
            return py_var(vm, RTN_InsTail(rtn).index);
        case E_RTN_Function::Invalid:
            return py_var(vm, RTN_Invalid().index);
        case E_RTN_Function::IsArtificial:
            return py_var(vm, RTN_IsArtificial(rtn));
        case E_RTN_Function::IsDynamic:
            return py_var(vm, RTN_IsDynamic(rtn));
        case E_RTN_Function::IsSafeForProbedInsertion:
            return py_var(vm, RTN_IsSafeForProbedInsertion(rtn));
        case E_RTN_Function::IsSafeForProbedReplacementEx:
            return py_var(vm, RTN_IsSafeForProbedReplacementEx(rtn, py_cast< PROBE_MODE >(vm, args[1])));
        case E_RTN_Function::IsSafeForProbedReplacement:
            return py_var(vm, RTN_IsSafeForProbedReplacement(rtn));
        case E_RTN_Function::Name:
            return py_var(vm, RTN_Name(rtn));
        case E_RTN_Function::Next:
            return py_var(vm, RTN_Next(rtn).index);
        case E_RTN_Function::NumIns:
            return py_var(vm, RTN_NumIns(rtn));
        case E_RTN_Function::Open:
            RTN_Open(rtn);
            return vm->None;
        case E_RTN_Function::Prev:
            return py_var(vm, RTN_Prev(rtn).index);
        case E_RTN_Function::Range:
            return py_var(vm, RTN_Range(rtn));
        case E_RTN_Function::Sec:
            return py_var(vm, RTN_Sec(rtn).index);
        case E_RTN_Function::Size:
            return py_var(vm, RTN_Size(rtn));
        case E_RTN_Function::Sym:
            return py_var(vm, RTN_Sym(rtn).index);
        case E_RTN_Function::Valid:
            return py_var(vm, RTN_Valid(rtn));
        default:
            return nullptr;
    }
    }

    inline void rtn_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "RTN_Address(rtn)", rtn_function< E_RTN_Function::Address >);
        vm->bind(module, "RTN_Close(rtn)", rtn_function< E_RTN_Function::Close >);
        vm->bind(module, "RTN_CreateAt(address, name)", rtn_function< E_RTN_Function::CreateAt, FALSE >);
        vm->bind(module, "RTN_DynamicMethodId(rtn)", rtn_function< E_RTN_Function::DynamicMethodId >);
        vm->bind(module, "RTN_FindByAddress(address)", rtn_function< E_RTN_Function::FindByAddress, FALSE >);
        vm->bind(module, "RTN_FindNameByAddress(address)", rtn_function< E_RTN_Function::FindNameByAddress, FALSE >);
        vm->bind(module, "RTN_Funptr(rtn)", rtn_function< E_RTN_Function::Funptr >);
        vm->bind(module, "RTN_Id(rtn)", rtn_function< E_RTN_Function::Id >);
        vm->bind(module, "RTN_FindByName(img, name)", rtn_function< E_RTN_Function::FindByName, FALSE >);
        vm->bind(module, "RTN_IFuncImplementation(rtn)", rtn_function< E_RTN_Function::IFuncImplementation >);
        vm->bind(module, "RTN_IFuncResolver(rtn)", rtn_function< E_RTN_Function::IFuncResolver >);
        vm->bind(module, "RTN_InsHead(rtn)", rtn_function< E_RTN_Function::InsHead >);
        vm->bind(module, "RTN_InsHeadOnly(rtn)", rtn_function< E_RTN_Function::InsHeadOnly >);
        vm->bind(module, "RTN_InsTail(rtn)", rtn_function< E_RTN_Function::InsTail >);
        vm->bind(module, "RTN_Invalid()", rtn_function< E_RTN_Function::Invalid, FALSE >);
        vm->bind(module, "RTN_IsArtificial(rtn)", rtn_function< E_RTN_Function::IsArtificial >);
        vm->bind(module, "RTN_IsDynamic(rtn)", rtn_function< E_RTN_Function::IsDynamic >);
        vm->bind(module, "RTN_IsSafeForProbedInsertion(rtn)", rtn_function< E_RTN_Function::IsSafeForProbedInsertion >);
        vm->bind(module, "RTN_IsSafeForProbedReplacementEx(rtn, mode)",
                 rtn_function< E_RTN_Function::IsSafeForProbedReplacementEx >);
        vm->bind(module, "RTN_IsSafeForProbedReplacement(rtn)", rtn_function< E_RTN_Function::IsSafeForProbedReplacement >);
        vm->bind(module, "RTN_Name(rtn)", rtn_function< E_RTN_Function::Name >);
        vm->bind(module, "RTN_Next(rtn)", rtn_function< E_RTN_Function::Next >);
        vm->bind(module, "RTN_NumIns(rtn)", rtn_function< E_RTN_Function::NumIns >);
        vm->bind(module, "RTN_Open(rtn)", rtn_function< E_RTN_Function::Open >);
        vm->bind(module, "RTN_Prev(rtn)", rtn_function< E_RTN_Function::Prev >);
        vm->bind(module, "RTN_Range(rtn)", rtn_function< E_RTN_Function::Range >);
        vm->bind(module, "RTN_Sec(rtn)", rtn_function< E_RTN_Function::Sec >);
        vm->bind(module, "RTN_Size(rtn)", rtn_function< E_RTN_Function::Size >);
        vm->bind(module, "RTN_Sym(rtn)", rtn_function< E_RTN_Function::Sym >);
        vm->bind(module, "RTN_Valid(rtn)", rtn_function< E_RTN_Function::Valid >);
    }
} // namespace glue

#endif // _GLUE_RTN_H_
