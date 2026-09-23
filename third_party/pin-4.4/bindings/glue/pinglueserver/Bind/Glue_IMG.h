/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_IMG_H_
#define _GLUE_IMG_H_

#include "../pocketpy.h"

namespace glue
{
enum class E_IMG_Function
{
    APP_ImgHead,
    APP_ImgTail,
    Close,
    DynamicRawData,
    EntryAddress,
    FindByAddress,
    FindImgById,
    GetLoaderInfo,
    Gp,
    HasProperty,
    HighAddress,
    Id,
    Invalid,
    IsInterpreter,
    IsMainExecutable,
    IsStaticExecutable,
    IsVDSO,
    LoadOffset,
    LowAddress,
    Name,
    Next,
    NumRegions,
    Open,
    Prev,
    RegionHighAddress,
    RegionLowAddress,
    RegsymHead,
    ReplayImageLoad,
    SecHead,
    SecTail,
    SetLoaderInfo,
    SizeMapped,
    StartAddress,
    Type,
    Valid,
    hasLinesData
};

template< E_IMG_Function Function, BOOL IsImgFirstArg = TRUE > inline PyObject* img_function(VM* vm, ArgsView args)
{
    IMG img;
    if constexpr (IsImgFirstArg)
    {
        img.index = py_cast< INT32 >(vm, args[0]);
    }

    switch (Function)
    {
        case E_IMG_Function::APP_ImgHead:
            return py_var(vm, APP_ImgHead().index);
        case E_IMG_Function::APP_ImgTail:
            return py_var(vm, APP_ImgTail().index);
        case E_IMG_Function::Close:
            IMG_Close(img);
            return vm->None;
        case E_IMG_Function::DynamicRawData:
            return py_var(vm, IMG_DynamicRawData(img));
        case E_IMG_Function::EntryAddress:
            return py_var(vm, IMG_EntryAddress(img));
        case E_IMG_Function::FindByAddress:
            return py_var(vm, IMG_FindByAddress(py_cast< ADDRINT >(vm, args[0])).index);
        case E_IMG_Function::FindImgById:
            return py_var(vm, IMG_FindImgById(py_cast< UINT32 >(vm, args[0])).index);
        case E_IMG_Function::GetLoaderInfo:
            return py_var(vm, IMG_GetLoaderInfo(img));
        case E_IMG_Function::Gp:
            return py_var(vm, IMG_Gp(img));
        case E_IMG_Function::HasProperty:
            return py_var(vm, IMG_HasProperty(img, py_cast< IMG_PROPERTY >(vm, args[1])));
        case E_IMG_Function::HighAddress:
            return py_var(vm, IMG_HighAddress(img));
        case E_IMG_Function::Id:
            return py_var(vm, IMG_Id(img));
        case E_IMG_Function::Invalid:
            return py_var(vm, IMG_Invalid().index);
        case E_IMG_Function::IsInterpreter:
            return py_var(vm, IMG_IsInterpreter(img));
        case E_IMG_Function::IsMainExecutable:
            return py_var(vm, IMG_IsMainExecutable(img));
        case E_IMG_Function::IsStaticExecutable:
            return py_var(vm, IMG_IsStaticExecutable(img));
        case E_IMG_Function::IsVDSO:
            return py_var(vm, IMG_IsVDSO(img));
        case E_IMG_Function::LoadOffset:
            return py_var(vm, IMG_LoadOffset(img));
        case E_IMG_Function::LowAddress:
            return py_var(vm, IMG_LowAddress(img));
        case E_IMG_Function::Name:
            return py_var(vm, IMG_Name(img));
        case E_IMG_Function::Next:
            return py_var(vm, IMG_Next(img).index);
        case E_IMG_Function::NumRegions:
            return py_var(vm, IMG_NumRegions(img));
        case E_IMG_Function::Open:
            return py_var(vm, IMG_Open(py_cast< const char* >(vm, args[0])).index);
        case E_IMG_Function::Prev:
            return py_var(vm, IMG_Prev(img).index);
        case E_IMG_Function::RegionHighAddress:
            return py_var(vm, IMG_RegionHighAddress(img, py_cast< UINT32 >(vm, args[1])));
        case E_IMG_Function::RegionLowAddress:
            return py_var(vm, IMG_RegionLowAddress(img, py_cast< UINT32 >(vm, args[1])));
        case E_IMG_Function::RegsymHead:
            return py_var(vm, IMG_RegsymHead(img).index);
        case E_IMG_Function::ReplayImageLoad:
        {
            // IMG_ReplayImageLoad acquires VM and Client lock - prevent deadlock
            Scoped_GIL_temp_release tempUnlock;
            IMG_ReplayImageLoad(img);
            return vm->None;
        }
        case E_IMG_Function::SecHead:
            return py_var(vm, IMG_SecHead(img).index);
        case E_IMG_Function::SecTail:
            return py_var(vm, IMG_SecTail(img).index);
        case E_IMG_Function::SetLoaderInfo:
            IMG_SetLoaderInfo(img, py_cast< VOID* >(vm, args[1]));
            return vm->None;
        case E_IMG_Function::SizeMapped:
            return py_var(vm, IMG_SizeMapped(img));
        case E_IMG_Function::StartAddress:
            return py_var(vm, IMG_StartAddress(img));
        case E_IMG_Function::Type:
            return py_var(vm, IMG_Type(img));
        case E_IMG_Function::Valid:
            return py_var(vm, IMG_Valid(img));
        case E_IMG_Function::hasLinesData:
            return py_var(vm, IMG_hasLinesData(img));
        default:
            return nullptr;
    }
    }

    inline void img_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "IMG_CreateAt(filename, start, size, load_offset, main_executable)",
                 [](VM* vm, ArgsView args)
                 {
                     auto filename       = py_cast< const char* >(vm, args[0]);
                     auto start          = py_cast< ADDRINT >(vm, args[1]);
                     auto size           = py_cast< USIZE >(vm, args[2]);
                     auto loadOffset     = py_cast< ADDRINT >(vm, args[3]);
                     BOOL mainExecutable = py_cast< BOOL >(vm, args[4]);
                     // IMG_CreateAt may deadlock with an instrumentation routine
                     // that holds the client lock but waits for the GIL
                     Scoped_GIL_temp_release tempUnlock;
                     auto img = IMG_CreateAt(filename, start, size, loadOffset, mainExecutable);
                     return py_var(vm, img.index);
                 });

        vm->bind(module, "APP_ImgHead()", img_function< E_IMG_Function::APP_ImgHead, FALSE >);
        vm->bind(module, "APP_ImgTail()", img_function< E_IMG_Function::APP_ImgTail, FALSE >);
        vm->bind(module, "IMG_Close(img)", img_function< E_IMG_Function::Close >);
        vm->bind(module, "IMG_DynamicRawData(img)", img_function< E_IMG_Function::DynamicRawData >);
        vm->bind(module, "IMG_EntryAddress(img)", img_function< E_IMG_Function::EntryAddress >);
        vm->bind(module, "IMG_FindByAddress(address)", img_function< E_IMG_Function::FindByAddress, FALSE >);
        vm->bind(module, "IMG_FindImgById(id)", img_function< E_IMG_Function::FindImgById, FALSE >);
        vm->bind(module, "IMG_GetLoaderInfo(img)", img_function< E_IMG_Function::GetLoaderInfo >);
        vm->bind(module, "IMG_Gp(img)", img_function< E_IMG_Function::Gp >);
        vm->bind(module, "IMG_HasProperty(img, property)", img_function< E_IMG_Function::HasProperty >);
        vm->bind(module, "IMG_HighAddress(img)", img_function< E_IMG_Function::HighAddress >);
        vm->bind(module, "IMG_Id(img)", img_function< E_IMG_Function::Id >);
        vm->bind(module, "IMG_Invalid()", img_function< E_IMG_Function::Invalid, FALSE >);
        vm->bind(module, "IMG_IsInterpreter(img)", img_function< E_IMG_Function::IsInterpreter >);
        vm->bind(module, "IMG_IsMainExecutable(img)", img_function< E_IMG_Function::IsMainExecutable >);
        vm->bind(module, "IMG_IsStaticExecutable(img)", img_function< E_IMG_Function::IsStaticExecutable >);
        vm->bind(module, "IMG_IsVDSO(img)", img_function< E_IMG_Function::IsVDSO >);
        vm->bind(module, "IMG_LoadOffset(img)", img_function< E_IMG_Function::LoadOffset >);
        vm->bind(module, "IMG_LowAddress(img)", img_function< E_IMG_Function::LowAddress >);
        vm->bind(module, "IMG_Name(img)", img_function< E_IMG_Function::Name >);
        vm->bind(module, "IMG_Next(img)", img_function< E_IMG_Function::Next >);
        vm->bind(module, "IMG_NumRegions(img)", img_function< E_IMG_Function::NumRegions >);
        vm->bind(module, "IMG_Open(filename)", img_function< E_IMG_Function::Open, FALSE >);
        vm->bind(module, "IMG_Prev(img)", img_function< E_IMG_Function::Prev >);
        vm->bind(module, "IMG_RegionHighAddress(img, n)", img_function< E_IMG_Function::RegionHighAddress >);
        vm->bind(module, "IMG_RegionLowAddress(img, n)", img_function< E_IMG_Function::RegionLowAddress >);
        vm->bind(module, "IMG_RegsymHead(img)", img_function< E_IMG_Function::RegsymHead >);
        vm->bind(module, "IMG_ReplayImageLoad(img)", img_function< E_IMG_Function::ReplayImageLoad >);
        vm->bind(module, "IMG_SecHead(img)", img_function< E_IMG_Function::SecHead >);
        vm->bind(module, "IMG_SecTail(img)", img_function< E_IMG_Function::SecTail >);
        vm->bind(module, "IMG_SetLoaderInfo(img, loader_info)", img_function< E_IMG_Function::SetLoaderInfo >);
        vm->bind(module, "IMG_SizeMapped(img)", img_function< E_IMG_Function::SizeMapped >);
        vm->bind(module, "IMG_StartAddress(img)", img_function< E_IMG_Function::StartAddress >);
        vm->bind(module, "IMG_Type(img)", img_function< E_IMG_Function::Type >);
        vm->bind(module, "IMG_Valid(img)", img_function< E_IMG_Function::Valid >);
        vm->bind(module, "IMG_hasLinesData(img)", img_function< E_IMG_Function::hasLinesData >);

        // IMG_TYPE
        module->attr().set("IMG_TYPE_INVALID", py_var(vm, IMG_TYPE_INVALID));
        module->attr().set("IMG_TYPE_STATIC", py_var(vm, IMG_TYPE_STATIC));
        module->attr().set("IMG_TYPE_SHARED", py_var(vm, IMG_TYPE_SHARED));
        module->attr().set("IMG_TYPE_SHAREDLIB", py_var(vm, IMG_TYPE_SHAREDLIB));
        module->attr().set("IMG_TYPE_RELOCATABLE", py_var(vm, IMG_TYPE_RELOCATABLE));
        module->attr().set("IMG_TYPE_DYNAMIC_CODE", py_var(vm, IMG_TYPE_DYNAMIC_CODE));
        module->attr().set("IMG_TYPE_API_CREATED", py_var(vm, IMG_TYPE_API_CREATED));

        // IMG_PROPERTY
        module->attr().set("IMG_PROPERTY_INVALID", py_var(vm, IMG_PROPERTY_INVALID));
        module->attr().set("IMG_PROPERTY_SHSTK_ENABLED", py_var(vm, IMG_PROPERTY_SHSTK_ENABLED));
        module->attr().set("IMG_PROPERTY_IBT_ENABLED", py_var(vm, IMG_PROPERTY_IBT_ENABLED));
    }

} // namespace glue

#endif // _GLUE_IMG_H_
