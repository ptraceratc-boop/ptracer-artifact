/*
 * Copyright (C) 2024-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_REG_H_
#define _GLUE_REG_H_

#include "../gluepy.h"

namespace glue
{
enum class E_REG_Function
{
    AppFlags,
    corresponding_ymm_reg,
    corresponding_zmm_reg,
    FullRegName,
    get_df_flag_reg_of_type,
    get_full_flags_reg_of_type,
    get_status_flags_reg_of_type,
    IdentityCopy,
    INVALID,
    is_any_app_flags,
    is_any_flags_type,
    is_any_mask,
    is_any_mxcsr,
    is_any_pin_flags,
    is_any_vector_reg,
    is_Any8,
    is_app_df_flag_type,
    is_app_status_flags_type,
    is_application,
    is_avx512_hi16_xmm,
    is_avx512_hi16_ymm,
    is_br,
    is_buffer,
    is_df_flag_type,
    is_df_flag,
    is_flags_any_size_type,
    is_flags_type,
    is_flags,
    is_fr_for_get_context,
    is_fr,
    is_gr_type,
    is_gr,
    is_gr16,
    is_gr32,
    is_gr64,
    is_gr8,
    is_gs_or_fs,
    is_Half16,
    is_Half32,
    is_inst_scratch,
    is_k_mask,
    is_Lower8,
    is_machine,
    is_mm,
    is_mxcsr,
    is_partialreg,
    is_pin_df_flag,
    is_pin_flags,
    is_pin_gpr,
    is_pin_gr_half32,
    is_pin_gr,
    is_pin_gr32,
    is_pin_inst,
    is_pin_k_mask,
    is_pin_status_flags,
    is_pin_tmp,
    is_pin_xmm_ymm_zmm,
    is_pin_xmm,
    is_pin_ymm,
    is_pin_zmm,
    is_pin,
    is_pin64,
    is_pseudo,
    is_reg,
    is_representative_reg,
    is_seg_base,
    is_seg,
    is_st,
    is_stackptr_type,
    is_status_flags_type,
    is_status_flags,
    is_subclass_none,
    is_tmm,
    is_Upper8,
    is_xmm_ymm_zmm,
    is_xmm,
    is_ymm,
    is_zmm,
    LastSupportedXmm,
    LastSupportedYmm,
    LastSupportedZmm,
    PIN_ClaimToolRegister,
    regDefTable,
    regSubClassBitMapTable,
    Size,
    StringShort,
    valid_for_iarg_reg_value,
    valid,
    Width
};

template< E_REG_Function Function, BOOL IsREGFirstArg = TRUE > inline PyObject* reg_function(VM* vm, ArgsView args)
{
    REG reg;
    if constexpr (IsREGFirstArg)
    {
        reg = py_cast< REG >(vm, args[0]);
    }

    // NOTE: We assume here after checking the disassembly (-O0) that the compiler treat each switch case on template parameter like if constexpr
    // and turn it to a single function for each E_REG_Function Function.
    // if not we have to replace it to if constexpr, same in all Bind files.
    switch (Function)
    {
        case E_REG_Function::AppFlags:
            return py_var(vm, REG_AppFlags());
        case E_REG_Function::corresponding_ymm_reg:
            return py_var(vm, REG_corresponding_ymm_reg(reg));
        case E_REG_Function::corresponding_zmm_reg:
            return py_var(vm, REG_corresponding_zmm_reg(reg));
        case E_REG_Function::FullRegName:
            return py_var(vm, REG_FullRegName(reg));
        case E_REG_Function::get_df_flag_reg_of_type:
            return py_var(vm, REG_get_df_flag_reg_of_type(reg));
        case E_REG_Function::get_full_flags_reg_of_type:
            return py_var(vm, REG_get_full_flags_reg_of_type(reg));
        case E_REG_Function::get_status_flags_reg_of_type:
            return py_var(vm, REG_get_status_flags_reg_of_type(reg));
        case E_REG_Function::IdentityCopy:
            return py_var(vm, REG_IdentityCopy(reg));
        case E_REG_Function::INVALID:
            return py_var(vm, REG_INVALID());
        case E_REG_Function::is_any_app_flags:
            return py_var(vm, REG_is_any_app_flags(reg));
        case E_REG_Function::is_any_flags_type:
            return py_var(vm, REG_is_any_flags_type(reg));
        case E_REG_Function::is_any_mask:
            return py_var(vm, REG_is_any_mask(reg));
        case E_REG_Function::is_any_mxcsr:
            return py_var(vm, REG_is_any_mxcsr(reg));
        case E_REG_Function::is_any_pin_flags:
            return py_var(vm, REG_is_any_pin_flags(reg));
        case E_REG_Function::is_any_vector_reg:
            return py_var(vm, REG_is_any_vector_reg(reg));
        case E_REG_Function::is_Any8:
            return py_var(vm, REG_is_Any8(reg));
        case E_REG_Function::is_app_df_flag_type:
            return py_var(vm, REG_is_app_df_flag_type(reg));
        case E_REG_Function::is_app_status_flags_type:
            return py_var(vm, REG_is_app_status_flags_type(reg));
        case E_REG_Function::is_application:
            return py_var(vm, REG_is_application(reg));
        case E_REG_Function::is_avx512_hi16_xmm:
            return py_var(vm, REG_is_avx512_hi16_xmm(reg));
        case E_REG_Function::is_avx512_hi16_ymm:
            return py_var(vm, REG_is_avx512_hi16_ymm(reg));
        case E_REG_Function::is_br:
            return py_var(vm, REG_is_br(reg));
        case E_REG_Function::is_buffer:
            return py_var(vm, REG_is_buffer(reg));
        case E_REG_Function::is_df_flag_type:
            return py_var(vm, REG_is_df_flag_type(reg));
        case E_REG_Function::is_df_flag:
            return py_var(vm, REG_is_df_flag(reg));
        case E_REG_Function::is_flags_any_size_type:
            return py_var(vm, REG_is_flags_any_size_type(reg));
        case E_REG_Function::is_flags_type:
            return py_var(vm, REG_is_flags_type(reg));
        case E_REG_Function::is_flags:
            return py_var(vm, REG_is_flags(reg));
        case E_REG_Function::is_fr_for_get_context:
            return py_var(vm, REG_is_fr_for_get_context(reg));
        case E_REG_Function::is_fr:
            return py_var(vm, REG_is_fr(reg));
        case E_REG_Function::is_gr_type:
            return py_var(vm, REG_is_gr_type(reg));
        case E_REG_Function::is_gr:
            return py_var(vm, REG_is_gr(reg));
        case E_REG_Function::is_gr16:
            return py_var(vm, REG_is_gr16(reg));
        case E_REG_Function::is_gr32:
            return py_var(vm, REG_is_gr32(reg));
        case E_REG_Function::is_gr64:
            return py_var(vm, REG_is_gr64(reg));
        case E_REG_Function::is_gr8:
            return py_var(vm, REG_is_gr8(reg));
        case E_REG_Function::is_gs_or_fs:
            return py_var(vm, REG_is_gs_or_fs(reg));
        case E_REG_Function::is_Half16:
            return py_var(vm, REG_is_Half16(reg));
        case E_REG_Function::is_Half32:
            return py_var(vm, REG_is_Half32(reg));
        case E_REG_Function::is_inst_scratch:
            return py_var(vm, REG_is_inst_scratch(reg));
        case E_REG_Function::is_k_mask:
            return py_var(vm, REG_is_k_mask(reg));
        case E_REG_Function::is_Lower8:
            return py_var(vm, REG_is_Lower8(reg));
        case E_REG_Function::is_machine:
            return py_var(vm, REG_is_machine(reg));
        case E_REG_Function::is_mm:
            return py_var(vm, REG_is_mm(reg));
        case E_REG_Function::is_mxcsr:
            return py_var(vm, REG_is_mxcsr(reg));
        case E_REG_Function::is_partialreg:
            return py_var(vm, REG_is_partialreg(reg));
        case E_REG_Function::is_pin_df_flag:
            return py_var(vm, REG_is_pin_df_flag(reg));
        case E_REG_Function::is_pin_flags:
            return py_var(vm, REG_is_pin_flags(reg));
        case E_REG_Function::is_pin_gpr:
            return py_var(vm, REG_is_pin_gpr(reg));
        case E_REG_Function::is_pin_gr_half32:
            return py_var(vm, REG_is_pin_gr_half32(reg));
        case E_REG_Function::is_pin_gr:
            return py_var(vm, REG_is_pin_gr(reg));
        case E_REG_Function::is_pin_gr32:
            return py_var(vm, REG_is_pin_gr32(reg));
        case E_REG_Function::is_pin_inst:
            return py_var(vm, REG_is_pin_inst(reg));
        case E_REG_Function::is_pin_k_mask:
            return py_var(vm, REG_is_pin_k_mask(reg));
        case E_REG_Function::is_pin_status_flags:
            return py_var(vm, REG_is_pin_status_flags(reg));
        case E_REG_Function::is_pin_tmp:
            return py_var(vm, REG_is_pin_tmp(reg));
        case E_REG_Function::is_pin_xmm_ymm_zmm:
            return py_var(vm, REG_is_pin_xmm_ymm_zmm(reg));
        case E_REG_Function::is_pin_xmm:
            return py_var(vm, REG_is_pin_xmm(reg));
        case E_REG_Function::is_pin_ymm:
            return py_var(vm, REG_is_pin_ymm(reg));
        case E_REG_Function::is_pin_zmm:
            return py_var(vm, REG_is_pin_zmm(reg));
        case E_REG_Function::is_pin:
            return py_var(vm, REG_is_pin(reg));
        case E_REG_Function::is_pin64:
            return py_var(vm, REG_is_pin64(reg));
        case E_REG_Function::is_pseudo:
            return py_var(vm, REG_is_pseudo(reg));
        case E_REG_Function::is_reg:
            return py_var(vm, REG_is_reg(reg));
        case E_REG_Function::is_representative_reg:
            return py_var(vm, REG_is_representative_reg(reg));
        case E_REG_Function::is_seg_base:
            return py_var(vm, REG_is_seg_base(reg));
        case E_REG_Function::is_seg:
            return py_var(vm, REG_is_seg(reg));
        case E_REG_Function::is_st:
            return py_var(vm, REG_is_st(reg));
        case E_REG_Function::is_stackptr_type:
            return py_var(vm, REG_is_stackptr_type(reg));
        case E_REG_Function::is_status_flags_type:
            return py_var(vm, REG_is_status_flags_type(reg));
        case E_REG_Function::is_status_flags:
            return py_var(vm, REG_is_status_flags(reg));
        case E_REG_Function::is_subclass_none:
            return py_var(vm, REG_is_subclass_none(reg));
        case E_REG_Function::is_tmm:
            return py_var(vm, REG_is_tmm(reg));
        case E_REG_Function::is_Upper8:
            return py_var(vm, REG_is_Upper8(reg));
        case E_REG_Function::is_xmm_ymm_zmm:
            return py_var(vm, REG_is_xmm_ymm_zmm(reg));
        case E_REG_Function::is_xmm:
            return py_var(vm, REG_is_xmm(reg));
        case E_REG_Function::is_ymm:
            return py_var(vm, REG_is_ymm(reg));
        case E_REG_Function::is_zmm:
            return py_var(vm, REG_is_zmm(reg));
        case E_REG_Function::LastSupportedXmm:
            return py_var(vm, REG_LastSupportedXmm());
        case E_REG_Function::LastSupportedYmm:
            return py_var(vm, REG_LastSupportedYmm());
        case E_REG_Function::LastSupportedZmm:
            return py_var(vm, REG_LastSupportedZmm());
        case E_REG_Function::PIN_ClaimToolRegister:
            return py_var(vm, PIN_ClaimToolRegister());
        case E_REG_Function::regSubClassBitMapTable:
            return py_var(vm, REG_regSubClassBitMapTable());
        case E_REG_Function::Size:
            return py_var(vm, REG_Size(reg));
        case E_REG_Function::StringShort:
            return py_var(vm, REG_StringShort(reg));
        case E_REG_Function::valid_for_iarg_reg_value:
            return py_var(vm, REG_valid_for_iarg_reg_value(reg));
        case E_REG_Function::valid:
            return py_var(vm, REG_valid(reg));
        case E_REG_Function::Width:
            return py_var(vm, REG_Width(reg));

        default:
            return nullptr;
    }
    }

    inline void reg_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "REG_AppFlags()", reg_function< E_REG_Function::AppFlags, FALSE >);
        vm->bind(module, "REG_corresponding_ymm_reg(reg)", reg_function< E_REG_Function::corresponding_ymm_reg >);
        vm->bind(module, "REG_corresponding_zmm_reg(reg)", reg_function< E_REG_Function::corresponding_zmm_reg >);
        vm->bind(module, "REG_FullRegName(reg)", reg_function< E_REG_Function::FullRegName >);
        vm->bind(module, "REG_get_df_flag_reg_of_type(reg)", reg_function< E_REG_Function::get_df_flag_reg_of_type >);
        vm->bind(module, "REG_get_full_flags_reg_of_type(reg)", reg_function< E_REG_Function::get_full_flags_reg_of_type >);
        vm->bind(module, "REG_get_status_flags_reg_of_type(reg)", reg_function< E_REG_Function::get_status_flags_reg_of_type >);
        vm->bind(module, "REG_IdentityCopy(reg)", reg_function< E_REG_Function::IdentityCopy >);
        vm->bind(module, "REG_INVALID()", reg_function< E_REG_Function::INVALID, FALSE >);
        vm->bind(module, "REG_is_any_app_flags(reg)", reg_function< E_REG_Function::is_any_app_flags >);
        vm->bind(module, "REG_is_any_flags_type(reg)", reg_function< E_REG_Function::is_any_flags_type >);
        vm->bind(module, "REG_is_any_mask(reg)", reg_function< E_REG_Function::is_any_mask >);
        vm->bind(module, "REG_is_any_mxcsr(reg)", reg_function< E_REG_Function::is_any_mxcsr >);
        vm->bind(module, "REG_is_any_pin_flags(reg)", reg_function< E_REG_Function::is_any_pin_flags >);
        vm->bind(module, "REG_is_any_vector_reg(reg)", reg_function< E_REG_Function::is_any_vector_reg >);
        vm->bind(module, "REG_is_Any8(reg)", reg_function< E_REG_Function::is_Any8 >);
        vm->bind(module, "REG_is_app_df_flag_type(reg)", reg_function< E_REG_Function::is_app_df_flag_type >);
        vm->bind(module, "REG_is_app_status_flags_type(reg)", reg_function< E_REG_Function::is_app_status_flags_type >);
        vm->bind(module, "REG_is_application(reg)", reg_function< E_REG_Function::is_application >);
        vm->bind(module, "REG_is_avx512_hi16_xmm(reg)", reg_function< E_REG_Function::is_avx512_hi16_xmm >);
        vm->bind(module, "REG_is_avx512_hi16_ymm(reg)", reg_function< E_REG_Function::is_avx512_hi16_ymm >);
        vm->bind(module, "REG_is_br(reg)", reg_function< E_REG_Function::is_br >);
        vm->bind(module, "REG_is_buffer(reg)", reg_function< E_REG_Function::is_buffer >);
        vm->bind(module, "REG_is_df_flag_type(reg)", reg_function< E_REG_Function::is_df_flag_type >);
        vm->bind(module, "REG_is_df_flag(reg)", reg_function< E_REG_Function::is_df_flag >);
        vm->bind(module, "REG_is_flags_any_size_type(reg)", reg_function< E_REG_Function::is_flags_any_size_type >);
        vm->bind(module, "REG_is_flags_type(reg)", reg_function< E_REG_Function::is_flags_type >);
        vm->bind(module, "REG_is_flags(reg)", reg_function< E_REG_Function::is_flags >);
        vm->bind(module, "REG_is_fr_for_get_context(reg)", reg_function< E_REG_Function::is_fr_for_get_context >);
        vm->bind(module, "REG_is_fr(reg)", reg_function< E_REG_Function::is_fr >);
        vm->bind(module, "REG_is_gr_type(reg)", reg_function< E_REG_Function::is_gr_type >);
        vm->bind(module, "REG_is_gr(reg)", reg_function< E_REG_Function::is_gr >);
        vm->bind(module, "REG_is_gr16(reg)", reg_function< E_REG_Function::is_gr16 >);
        vm->bind(module, "REG_is_gr32(reg)", reg_function< E_REG_Function::is_gr32 >);
        vm->bind(module, "REG_is_gr64(reg)", reg_function< E_REG_Function::is_gr64 >);
        vm->bind(module, "REG_is_gr8(reg)", reg_function< E_REG_Function::is_gr8 >);
        vm->bind(module, "REG_is_gs_or_fs(reg)", reg_function< E_REG_Function::is_gs_or_fs >);
        vm->bind(module, "REG_is_Half16(reg)", reg_function< E_REG_Function::is_Half16 >);
        vm->bind(module, "REG_is_Half32(reg)", reg_function< E_REG_Function::is_Half32 >);
        vm->bind(module, "REG_is_inst_scratch(reg)", reg_function< E_REG_Function::is_inst_scratch >);
        vm->bind(module, "REG_is_k_mask(reg)", reg_function< E_REG_Function::is_k_mask >);
        vm->bind(module, "REG_is_Lower8(reg)", reg_function< E_REG_Function::is_Lower8 >);
        vm->bind(module, "REG_is_machine(reg)", reg_function< E_REG_Function::is_machine >);
        vm->bind(module, "REG_is_mm(reg)", reg_function< E_REG_Function::is_mm >);
        vm->bind(module, "REG_is_mxcsr(reg)", reg_function< E_REG_Function::is_mxcsr >);
        vm->bind(module, "REG_is_partialreg(reg)", reg_function< E_REG_Function::is_partialreg >);
        vm->bind(module, "REG_is_pin_df_flag(reg)", reg_function< E_REG_Function::is_pin_df_flag >);
        vm->bind(module, "REG_is_pin_flags(reg)", reg_function< E_REG_Function::is_pin_flags >);
        vm->bind(module, "REG_is_pin_gpr(reg)", reg_function< E_REG_Function::is_pin_gpr >);
        vm->bind(module, "REG_is_pin_gr_half32(reg)", reg_function< E_REG_Function::is_pin_gr_half32 >);
        vm->bind(module, "REG_is_pin_gr(reg)", reg_function< E_REG_Function::is_pin_gr >);
        vm->bind(module, "REG_is_pin_gr32(reg)", reg_function< E_REG_Function::is_pin_gr32 >);
        vm->bind(module, "REG_is_pin_inst(reg)", reg_function< E_REG_Function::is_pin_inst >);
        vm->bind(module, "REG_is_pin_k_mask(reg)", reg_function< E_REG_Function::is_pin_k_mask >);
        vm->bind(module, "REG_is_pin_status_flags(reg)", reg_function< E_REG_Function::is_pin_status_flags >);
        vm->bind(module, "REG_is_pin_tmp(reg)", reg_function< E_REG_Function::is_pin_tmp >);
        vm->bind(module, "REG_is_pin_xmm_ymm_zmm(reg)", reg_function< E_REG_Function::is_pin_xmm_ymm_zmm >);
        vm->bind(module, "REG_is_pin_xmm(reg)", reg_function< E_REG_Function::is_pin_xmm >);
        vm->bind(module, "REG_is_pin_ymm(reg)", reg_function< E_REG_Function::is_pin_ymm >);
        vm->bind(module, "REG_is_pin_zmm(reg)", reg_function< E_REG_Function::is_pin_zmm >);
        vm->bind(module, "REG_is_pin(reg)", reg_function< E_REG_Function::is_pin >);
        vm->bind(module, "REG_is_pin64(reg)", reg_function< E_REG_Function::is_pin64 >);
        vm->bind(module, "REG_is_pseudo(reg)", reg_function< E_REG_Function::is_pseudo >);
        vm->bind(module, "REG_is_reg(reg)", reg_function< E_REG_Function::is_reg >);
        vm->bind(module, "REG_is_representative_reg(reg)", reg_function< E_REG_Function::is_representative_reg >);
        vm->bind(module, "REG_is_seg_base(reg)", reg_function< E_REG_Function::is_seg_base >);
        vm->bind(module, "REG_is_seg(reg)", reg_function< E_REG_Function::is_seg >);
        vm->bind(module, "REG_is_st(reg)", reg_function< E_REG_Function::is_st >);
        vm->bind(module, "REG_is_stackptr_type(reg)", reg_function< E_REG_Function::is_stackptr_type >);
        vm->bind(module, "REG_is_status_flags_type(reg)", reg_function< E_REG_Function::is_status_flags_type >);
        vm->bind(module, "REG_is_status_flags(reg)", reg_function< E_REG_Function::is_status_flags >);
        vm->bind(module, "REG_is_subclass_none(reg)", reg_function< E_REG_Function::is_subclass_none >);
        vm->bind(module, "REG_is_tmm(reg)", reg_function< E_REG_Function::is_tmm >);
        vm->bind(module, "REG_is_Upper8(reg)", reg_function< E_REG_Function::is_Upper8 >);
        vm->bind(module, "REG_is_xmm_ymm_zmm(reg)", reg_function< E_REG_Function::is_xmm_ymm_zmm >);
        vm->bind(module, "REG_is_xmm(reg)", reg_function< E_REG_Function::is_xmm >);
        vm->bind(module, "REG_is_ymm(reg)", reg_function< E_REG_Function::is_ymm >);
        vm->bind(module, "REG_is_zmm(reg)", reg_function< E_REG_Function::is_zmm >);
        vm->bind(module, "REG_LastSupportedXmm()", reg_function< E_REG_Function::LastSupportedXmm, FALSE >);
        vm->bind(module, "REG_LastSupportedYmm()", reg_function< E_REG_Function::LastSupportedYmm, FALSE >);
        vm->bind(module, "REG_LastSupportedZmm()", reg_function< E_REG_Function::LastSupportedZmm, FALSE >);
        vm->bind(module, "PIN_ClaimToolRegister()", reg_function< E_REG_Function::PIN_ClaimToolRegister, FALSE >);
        vm->bind(module, "REG_regSubClassBitMapTable()", reg_function< E_REG_Function::regSubClassBitMapTable, FALSE >);
        vm->bind(module, "REG_Size(reg)", reg_function< E_REG_Function::Size >);
        vm->bind(module, "REG_StringShort(reg)", reg_function< E_REG_Function::StringShort >);
        vm->bind(module, "REG_valid_for_iarg_reg_value(reg)", reg_function< E_REG_Function::valid_for_iarg_reg_value >);
        vm->bind(module, "REG_valid(reg)", reg_function< E_REG_Function::valid >);
        vm->bind(module, "REG_Width(reg)", reg_function< E_REG_Function::Width >);

        // enum REG
#if defined(TARGET_IA32E)
        // registers in the Intel(R) 64 architecture only
        module->attr().set("REG_RDI", py_var(vm, LEVEL_BASE::REG_RDI));
        module->attr().set("REG_RSI", py_var(vm, LEVEL_BASE::REG_RSI));
        module->attr().set("REG_RBP", py_var(vm, LEVEL_BASE::REG_RBP));
        module->attr().set("REG_RSP", py_var(vm, LEVEL_BASE::REG_RSP));
        module->attr().set("REG_RBX", py_var(vm, LEVEL_BASE::REG_RBX));
        module->attr().set("REG_RDX", py_var(vm, LEVEL_BASE::REG_RDX));
        module->attr().set("REG_RCX", py_var(vm, LEVEL_BASE::REG_RCX));
        module->attr().set("REG_RAX", py_var(vm, LEVEL_BASE::REG_RAX));
        module->attr().set("REG_R8", py_var(vm, LEVEL_BASE::REG_R8));
        module->attr().set("REG_R9", py_var(vm, LEVEL_BASE::REG_R9));
        module->attr().set("REG_R10", py_var(vm, LEVEL_BASE::REG_R10));
        module->attr().set("REG_R11", py_var(vm, LEVEL_BASE::REG_R11));
        module->attr().set("REG_R12", py_var(vm, LEVEL_BASE::REG_R12));
        module->attr().set("REG_R13", py_var(vm, LEVEL_BASE::REG_R13));
        module->attr().set("REG_R14", py_var(vm, LEVEL_BASE::REG_R14));
        module->attr().set("REG_R15", py_var(vm, LEVEL_BASE::REG_R15));
        module->attr().set("REG_RFLAGS", py_var(vm, REG_RFLAGS));
        module->attr().set("REG_RIP", py_var(vm, LEVEL_BASE::REG_RIP));
        module->attr().set("REG_DIL", py_var(vm, REG_DIL));
        module->attr().set("REG_SIL", py_var(vm, REG_SIL));
        module->attr().set("REG_BPL", py_var(vm, REG_BPL));
        module->attr().set("REG_SPL", py_var(vm, REG_SPL));
        module->attr().set("REG_R8B", py_var(vm, REG_R8B));
        module->attr().set("REG_R8W", py_var(vm, REG_R8W));
        module->attr().set("REG_R8D", py_var(vm, REG_R8D));
        module->attr().set("REG_R9B", py_var(vm, REG_R9B));
        module->attr().set("REG_R9W", py_var(vm, REG_R9W));
        module->attr().set("REG_R9D", py_var(vm, REG_R9D));
        module->attr().set("REG_R10B", py_var(vm, REG_R10B));
        module->attr().set("REG_R10W", py_var(vm, REG_R10W));
        module->attr().set("REG_R10D", py_var(vm, REG_R10D));
        module->attr().set("REG_R11B", py_var(vm, REG_R11B));
        module->attr().set("REG_R11W", py_var(vm, REG_R11W));
        module->attr().set("REG_R11D", py_var(vm, REG_R11D));
        module->attr().set("REG_R12B", py_var(vm, REG_R12B));
        module->attr().set("REG_R12W", py_var(vm, REG_R12W));
        module->attr().set("REG_R12D", py_var(vm, REG_R12D));
        module->attr().set("REG_R13B", py_var(vm, REG_R13B));
        module->attr().set("REG_R13W", py_var(vm, REG_R13W));
        module->attr().set("REG_R13D", py_var(vm, REG_R13D));
        module->attr().set("REG_R14B", py_var(vm, REG_R14B));
        module->attr().set("REG_R14W", py_var(vm, REG_R14W));
        module->attr().set("REG_R14D", py_var(vm, REG_R14D));
        module->attr().set("REG_R15B", py_var(vm, REG_R15B));
        module->attr().set("REG_R15W", py_var(vm, REG_R15W));
        module->attr().set("REG_R15D", py_var(vm, REG_R15D));
        module->attr().set("REG_XMM8", py_var(vm, REG_XMM8));
        module->attr().set("REG_XMM9", py_var(vm, REG_XMM9));
        module->attr().set("REG_XMM10", py_var(vm, REG_XMM10));
        module->attr().set("REG_XMM11", py_var(vm, REG_XMM11));
        module->attr().set("REG_XMM12", py_var(vm, REG_XMM12));
        module->attr().set("REG_XMM13", py_var(vm, REG_XMM13));
        module->attr().set("REG_XMM14", py_var(vm, REG_XMM14));
        module->attr().set("REG_XMM15", py_var(vm, REG_XMM15));
        module->attr().set("REG_XMM_AVX512_HI16_FIRST", py_var(vm, REG_XMM_AVX512_HI16_FIRST));
        module->attr().set("REG_XMM16", py_var(vm, REG_XMM16));
        module->attr().set("REG_XMM17", py_var(vm, REG_XMM17));
        module->attr().set("REG_XMM18", py_var(vm, REG_XMM18));
        module->attr().set("REG_XMM19", py_var(vm, REG_XMM19));
        module->attr().set("REG_XMM20", py_var(vm, REG_XMM20));
        module->attr().set("REG_XMM21", py_var(vm, REG_XMM21));
        module->attr().set("REG_XMM22", py_var(vm, REG_XMM22));
        module->attr().set("REG_XMM23", py_var(vm, REG_XMM23));
        module->attr().set("REG_XMM24", py_var(vm, REG_XMM24));
        module->attr().set("REG_XMM25", py_var(vm, REG_XMM25));
        module->attr().set("REG_XMM26", py_var(vm, REG_XMM26));
        module->attr().set("REG_XMM27", py_var(vm, REG_XMM27));
        module->attr().set("REG_XMM28", py_var(vm, REG_XMM28));
        module->attr().set("REG_XMM29", py_var(vm, REG_XMM29));
        module->attr().set("REG_XMM30", py_var(vm, REG_XMM30));
        module->attr().set("REG_XMM31", py_var(vm, REG_XMM31));
        module->attr().set("REG_XMM_AVX512_HI16_LAST", py_var(vm, REG_XMM_AVX512_HI16_LAST));
        module->attr().set("REG_YMM8", py_var(vm, REG_YMM8));
        module->attr().set("REG_YMM9", py_var(vm, REG_YMM9));
        module->attr().set("REG_YMM10", py_var(vm, REG_YMM10));
        module->attr().set("REG_YMM11", py_var(vm, REG_YMM11));
        module->attr().set("REG_YMM12", py_var(vm, REG_YMM12));
        module->attr().set("REG_YMM13", py_var(vm, REG_YMM13));
        module->attr().set("REG_YMM14", py_var(vm, REG_YMM14));
        module->attr().set("REG_YMM15", py_var(vm, REG_YMM15));
        module->attr().set("REG_YMM_AVX512_HI16_FIRST", py_var(vm, REG_YMM_AVX512_HI16_FIRST));
        module->attr().set("REG_YMM16", py_var(vm, REG_YMM16));
        module->attr().set("REG_YMM17", py_var(vm, REG_YMM17));
        module->attr().set("REG_YMM18", py_var(vm, REG_YMM18));
        module->attr().set("REG_YMM19", py_var(vm, REG_YMM19));
        module->attr().set("REG_YMM20", py_var(vm, REG_YMM20));
        module->attr().set("REG_YMM21", py_var(vm, REG_YMM21));
        module->attr().set("REG_YMM22", py_var(vm, REG_YMM22));
        module->attr().set("REG_YMM23", py_var(vm, REG_YMM23));
        module->attr().set("REG_YMM24", py_var(vm, REG_YMM24));
        module->attr().set("REG_YMM25", py_var(vm, REG_YMM25));
        module->attr().set("REG_YMM26", py_var(vm, REG_YMM26));
        module->attr().set("REG_YMM27", py_var(vm, REG_YMM27));
        module->attr().set("REG_YMM28", py_var(vm, REG_YMM28));
        module->attr().set("REG_YMM29", py_var(vm, REG_YMM29));
        module->attr().set("REG_YMM30", py_var(vm, REG_YMM30));
        module->attr().set("REG_YMM31", py_var(vm, REG_YMM31));
        module->attr().set("REG_YMM_AVX512_HI16_LAST", py_var(vm, REG_YMM_AVX512_HI16_LAST));
        module->attr().set("REG_ZMM8", py_var(vm, REG_ZMM8));
        module->attr().set("REG_ZMM9", py_var(vm, REG_ZMM9));
        module->attr().set("REG_ZMM10", py_var(vm, REG_ZMM10));
        module->attr().set("REG_ZMM11", py_var(vm, REG_ZMM11));
        module->attr().set("REG_ZMM12", py_var(vm, REG_ZMM12));
        module->attr().set("REG_ZMM13", py_var(vm, REG_ZMM13));
        module->attr().set("REG_ZMM14", py_var(vm, REG_ZMM14));
        module->attr().set("REG_ZMM15", py_var(vm, REG_ZMM15));
        module->attr().set("REG_ZMM_AVX512_HI16_FIRST", py_var(vm, REG_ZMM_AVX512_HI16_FIRST));
        module->attr().set("REG_ZMM16", py_var(vm, REG_ZMM16));
        module->attr().set("REG_ZMM17", py_var(vm, REG_ZMM17));
        module->attr().set("REG_ZMM18", py_var(vm, REG_ZMM18));
        module->attr().set("REG_ZMM19", py_var(vm, REG_ZMM19));
        module->attr().set("REG_ZMM20", py_var(vm, REG_ZMM20));
        module->attr().set("REG_ZMM21", py_var(vm, REG_ZMM21));
        module->attr().set("REG_ZMM22", py_var(vm, REG_ZMM22));
        module->attr().set("REG_ZMM23", py_var(vm, REG_ZMM23));
        module->attr().set("REG_ZMM24", py_var(vm, REG_ZMM24));
        module->attr().set("REG_ZMM25", py_var(vm, REG_ZMM25));
        module->attr().set("REG_ZMM26", py_var(vm, REG_ZMM26));
        module->attr().set("REG_ZMM27", py_var(vm, REG_ZMM27));
        module->attr().set("REG_ZMM28", py_var(vm, REG_ZMM28));
        module->attr().set("REG_ZMM29", py_var(vm, REG_ZMM29));
        module->attr().set("REG_ZMM30", py_var(vm, REG_ZMM30));
        module->attr().set("REG_ZMM31", py_var(vm, REG_ZMM31));
        module->attr().set("REG_ZMM_AVX512_HI16_LAST", py_var(vm, REG_ZMM_AVX512_HI16_LAST));
        module->attr().set("REG_TMM0", py_var(vm, REG_TMM0));
        module->attr().set("REG_TMM1", py_var(vm, REG_TMM1));
        module->attr().set("REG_TMM2", py_var(vm, REG_TMM2));
        module->attr().set("REG_TMM3", py_var(vm, REG_TMM3));
        module->attr().set("REG_TMM4", py_var(vm, REG_TMM4));
        module->attr().set("REG_TMM5", py_var(vm, REG_TMM5));
        module->attr().set("REG_TMM6", py_var(vm, REG_TMM6));
        module->attr().set("REG_TMM7", py_var(vm, REG_TMM7));
        module->attr().set("REG_TMM_FIRST", py_var(vm, REG_TMM_FIRST));
        module->attr().set("REG_TMM_LAST", py_var(vm, REG_TMM_LAST));
        module->attr().set("REG_TILECONFIG", py_var(vm, REG_TILECONFIG));
        module->attr().set("REG_ORIG_RAX", py_var(vm, REG_ORIG_RAX));
        module->attr().set("REG_CR8", py_var(vm, REG_CR8));
        module->attr().set("REG_INST_G0D", py_var(vm, REG_INST_G0D));
        module->attr().set("REG_INST_G1D", py_var(vm, REG_INST_G1D));
        module->attr().set("REG_INST_G2D", py_var(vm, REG_INST_G2D));
        module->attr().set("REG_INST_G3D", py_var(vm, REG_INST_G3D));
        module->attr().set("REG_INST_G4D", py_var(vm, REG_INST_G4D));
        module->attr().set("REG_INST_G5D", py_var(vm, REG_INST_G5D));
        module->attr().set("REG_INST_G6D", py_var(vm, REG_INST_G6D));
        module->attr().set("REG_INST_G7D", py_var(vm, REG_INST_G7D));
        module->attr().set("REG_INST_G8D", py_var(vm, REG_INST_G8D));
        module->attr().set("REG_INST_G9D", py_var(vm, REG_INST_G9D));
        module->attr().set("REG_INST_G10D", py_var(vm, REG_INST_G10D));
        module->attr().set("REG_INST_G11D", py_var(vm, REG_INST_G11D));
        module->attr().set("REG_INST_G12D", py_var(vm, REG_INST_G12D));
        module->attr().set("REG_INST_G13D", py_var(vm, REG_INST_G13D));
        module->attr().set("REG_INST_G14D", py_var(vm, REG_INST_G14D));
        module->attr().set("REG_INST_G15D", py_var(vm, REG_INST_G15D));
        module->attr().set("REG_INST_G16D", py_var(vm, REG_INST_G16D));
        module->attr().set("REG_INST_G17D", py_var(vm, REG_INST_G17D));
        module->attr().set("REG_INST_G18D", py_var(vm, REG_INST_G18D));
        module->attr().set("REG_INST_G19D", py_var(vm, REG_INST_G19D));
        module->attr().set("REG_INST_G20D", py_var(vm, REG_INST_G20D));
        module->attr().set("REG_INST_G21D", py_var(vm, REG_INST_G21D));
        module->attr().set("REG_INST_G22D", py_var(vm, REG_INST_G22D));
        module->attr().set("REG_INST_G23D", py_var(vm, REG_INST_G23D));
        module->attr().set("REG_INST_G24D", py_var(vm, REG_INST_G24D));
        module->attr().set("REG_INST_G25D", py_var(vm, REG_INST_G25D));
        module->attr().set("REG_INST_G26D", py_var(vm, REG_INST_G26D));
        module->attr().set("REG_INST_G27D", py_var(vm, REG_INST_G27D));
        module->attr().set("REG_INST_G28D", py_var(vm, REG_INST_G28D));
        module->attr().set("REG_INST_G29D", py_var(vm, REG_INST_G29D));
        module->attr().set("REG_PIN_RDI", py_var(vm, REG_PIN_RDI));
        module->attr().set("REG_PIN_RSI", py_var(vm, REG_PIN_RSI));
        module->attr().set("REG_PIN_RBP", py_var(vm, REG_PIN_RBP));
        module->attr().set("REG_PIN_RSP", py_var(vm, REG_PIN_RSP));
        module->attr().set("REG_PIN_RBX", py_var(vm, REG_PIN_RBX));
        module->attr().set("REG_PIN_RDX", py_var(vm, REG_PIN_RDX));
        module->attr().set("REG_PIN_RCX", py_var(vm, REG_PIN_RCX));
        module->attr().set("REG_PIN_RAX", py_var(vm, REG_PIN_RAX));
        module->attr().set("REG_PIN_R8", py_var(vm, REG_PIN_R8));
        module->attr().set("REG_PIN_R9", py_var(vm, REG_PIN_R9));
        module->attr().set("REG_PIN_R10", py_var(vm, REG_PIN_R10));
        module->attr().set("REG_PIN_R11", py_var(vm, REG_PIN_R11));
        module->attr().set("REG_PIN_R12", py_var(vm, REG_PIN_R12));
        module->attr().set("REG_PIN_R13", py_var(vm, REG_PIN_R13));
        module->attr().set("REG_PIN_R14", py_var(vm, REG_PIN_R14));
        module->attr().set("REG_PIN_R15", py_var(vm, REG_PIN_R15));
        module->attr().set("REG_PIN_DIL", py_var(vm, REG_PIN_DIL));
        module->attr().set("REG_PIN_SIL", py_var(vm, REG_PIN_SIL));
        module->attr().set("REG_PIN_BPL", py_var(vm, REG_PIN_BPL));
        module->attr().set("REG_PIN_SPL", py_var(vm, REG_PIN_SPL));
        module->attr().set("REG_PIN_R8B", py_var(vm, REG_PIN_R8B));
        module->attr().set("REG_PIN_R8W", py_var(vm, REG_PIN_R8W));
        module->attr().set("REG_PIN_R8D", py_var(vm, REG_PIN_R8D));
        module->attr().set("REG_PIN_R9B", py_var(vm, REG_PIN_R9B));
        module->attr().set("REG_PIN_R9W", py_var(vm, REG_PIN_R9W));
        module->attr().set("REG_PIN_R9D", py_var(vm, REG_PIN_R9D));
        module->attr().set("REG_PIN_R10B", py_var(vm, REG_PIN_R10B));
        module->attr().set("REG_PIN_R10W", py_var(vm, REG_PIN_R10W));
        module->attr().set("REG_PIN_R10D", py_var(vm, REG_PIN_R10D));
        module->attr().set("REG_PIN_R11B", py_var(vm, REG_PIN_R11B));
        module->attr().set("REG_PIN_R11W", py_var(vm, REG_PIN_R11W));
        module->attr().set("REG_PIN_R11D", py_var(vm, REG_PIN_R11D));
        module->attr().set("REG_PIN_R12B", py_var(vm, REG_PIN_R12B));
        module->attr().set("REG_PIN_R12W", py_var(vm, REG_PIN_R12W));
        module->attr().set("REG_PIN_R12D", py_var(vm, REG_PIN_R12D));
        module->attr().set("REG_PIN_R13B", py_var(vm, REG_PIN_R13B));
        module->attr().set("REG_PIN_R13W", py_var(vm, REG_PIN_R13W));
        module->attr().set("REG_PIN_R13D", py_var(vm, REG_PIN_R13D));
        module->attr().set("REG_PIN_R14B", py_var(vm, REG_PIN_R14B));
        module->attr().set("REG_PIN_R14W", py_var(vm, REG_PIN_R14W));
        module->attr().set("REG_PIN_R14D", py_var(vm, REG_PIN_R14D));
        module->attr().set("REG_PIN_R15B", py_var(vm, REG_PIN_R15B));
        module->attr().set("REG_PIN_R15W", py_var(vm, REG_PIN_R15W));
        module->attr().set("REG_PIN_R15D", py_var(vm, REG_PIN_R15D));
        module->attr().set("REG_PIN_TRACKED_TILECFG", py_var(vm, REG_PIN_TRACKED_TILECFG));
        module->attr().set("REG_PIN_XMM8", py_var(vm, REG_PIN_XMM8));
        module->attr().set("REG_PIN_XMM9", py_var(vm, REG_PIN_XMM9));
        module->attr().set("REG_PIN_XMM10", py_var(vm, REG_PIN_XMM10));
        module->attr().set("REG_PIN_XMM11", py_var(vm, REG_PIN_XMM11));
        module->attr().set("REG_PIN_XMM12", py_var(vm, REG_PIN_XMM12));
        module->attr().set("REG_PIN_XMM13", py_var(vm, REG_PIN_XMM13));
        module->attr().set("REG_PIN_XMM14", py_var(vm, REG_PIN_XMM14));
        module->attr().set("REG_PIN_XMM15", py_var(vm, REG_PIN_XMM15));
        module->attr().set("REG_PIN_XMM_AVX512_HI16_FIRST", py_var(vm, REG_PIN_XMM_AVX512_HI16_FIRST));
        module->attr().set("REG_PIN_XMM16", py_var(vm, REG_PIN_XMM16));
        module->attr().set("REG_PIN_XMM17", py_var(vm, REG_PIN_XMM17));
        module->attr().set("REG_PIN_XMM18", py_var(vm, REG_PIN_XMM18));
        module->attr().set("REG_PIN_XMM19", py_var(vm, REG_PIN_XMM19));
        module->attr().set("REG_PIN_XMM20", py_var(vm, REG_PIN_XMM20));
        module->attr().set("REG_PIN_XMM21", py_var(vm, REG_PIN_XMM21));
        module->attr().set("REG_PIN_XMM22", py_var(vm, REG_PIN_XMM22));
        module->attr().set("REG_PIN_XMM23", py_var(vm, REG_PIN_XMM23));
        module->attr().set("REG_PIN_XMM24", py_var(vm, REG_PIN_XMM24));
        module->attr().set("REG_PIN_XMM25", py_var(vm, REG_PIN_XMM25));
        module->attr().set("REG_PIN_XMM26", py_var(vm, REG_PIN_XMM26));
        module->attr().set("REG_PIN_XMM27", py_var(vm, REG_PIN_XMM27));
        module->attr().set("REG_PIN_XMM28", py_var(vm, REG_PIN_XMM28));
        module->attr().set("REG_PIN_XMM29", py_var(vm, REG_PIN_XMM29));
        module->attr().set("REG_PIN_XMM30", py_var(vm, REG_PIN_XMM30));
        module->attr().set("REG_PIN_XMM31", py_var(vm, REG_PIN_XMM31));
        module->attr().set("REG_PIN_XMM_AVX512_HI16_LAST", py_var(vm, REG_PIN_XMM_AVX512_HI16_LAST));
        module->attr().set("REG_PIN_YMM8", py_var(vm, REG_PIN_YMM8));
        module->attr().set("REG_PIN_YMM9", py_var(vm, REG_PIN_YMM9));
        module->attr().set("REG_PIN_YMM10", py_var(vm, REG_PIN_YMM10));
        module->attr().set("REG_PIN_YMM11", py_var(vm, REG_PIN_YMM11));
        module->attr().set("REG_PIN_YMM12", py_var(vm, REG_PIN_YMM12));
        module->attr().set("REG_PIN_YMM13", py_var(vm, REG_PIN_YMM13));
        module->attr().set("REG_PIN_YMM14", py_var(vm, REG_PIN_YMM14));
        module->attr().set("REG_PIN_YMM15", py_var(vm, REG_PIN_YMM15));
        module->attr().set("REG_PIN_YMM_AVX512_HI16_FIRST", py_var(vm, REG_PIN_YMM_AVX512_HI16_FIRST));
        module->attr().set("REG_PIN_YMM16", py_var(vm, REG_PIN_YMM16));
        module->attr().set("REG_PIN_YMM17", py_var(vm, REG_PIN_YMM17));
        module->attr().set("REG_PIN_YMM18", py_var(vm, REG_PIN_YMM18));
        module->attr().set("REG_PIN_YMM19", py_var(vm, REG_PIN_YMM19));
        module->attr().set("REG_PIN_YMM20", py_var(vm, REG_PIN_YMM20));
        module->attr().set("REG_PIN_YMM21", py_var(vm, REG_PIN_YMM21));
        module->attr().set("REG_PIN_YMM22", py_var(vm, REG_PIN_YMM22));
        module->attr().set("REG_PIN_YMM23", py_var(vm, REG_PIN_YMM23));
        module->attr().set("REG_PIN_YMM24", py_var(vm, REG_PIN_YMM24));
        module->attr().set("REG_PIN_YMM25", py_var(vm, REG_PIN_YMM25));
        module->attr().set("REG_PIN_YMM26", py_var(vm, REG_PIN_YMM26));
        module->attr().set("REG_PIN_YMM27", py_var(vm, REG_PIN_YMM27));
        module->attr().set("REG_PIN_YMM28", py_var(vm, REG_PIN_YMM28));
        module->attr().set("REG_PIN_YMM29", py_var(vm, REG_PIN_YMM29));
        module->attr().set("REG_PIN_YMM30", py_var(vm, REG_PIN_YMM30));
        module->attr().set("REG_PIN_YMM31", py_var(vm, REG_PIN_YMM31));
        module->attr().set("REG_PIN_YMM_AVX512_HI16_LAST", py_var(vm, REG_PIN_YMM_AVX512_HI16_LAST));
        module->attr().set("REG_PIN_ZMM8", py_var(vm, REG_PIN_ZMM8));
        module->attr().set("REG_PIN_ZMM9", py_var(vm, REG_PIN_ZMM9));
        module->attr().set("REG_PIN_ZMM10", py_var(vm, REG_PIN_ZMM10));
        module->attr().set("REG_PIN_ZMM11", py_var(vm, REG_PIN_ZMM11));
        module->attr().set("REG_PIN_ZMM12", py_var(vm, REG_PIN_ZMM12));
        module->attr().set("REG_PIN_ZMM13", py_var(vm, REG_PIN_ZMM13));
        module->attr().set("REG_PIN_ZMM14", py_var(vm, REG_PIN_ZMM14));
        module->attr().set("REG_PIN_ZMM15", py_var(vm, REG_PIN_ZMM15));
        module->attr().set("REG_PIN_ZMM_AVX512_HI16_FIRST", py_var(vm, REG_PIN_ZMM_AVX512_HI16_FIRST));
        module->attr().set("REG_PIN_ZMM16", py_var(vm, REG_PIN_ZMM16));
        module->attr().set("REG_PIN_ZMM17", py_var(vm, REG_PIN_ZMM17));
        module->attr().set("REG_PIN_ZMM18", py_var(vm, REG_PIN_ZMM18));
        module->attr().set("REG_PIN_ZMM19", py_var(vm, REG_PIN_ZMM19));
        module->attr().set("REG_PIN_ZMM20", py_var(vm, REG_PIN_ZMM20));
        module->attr().set("REG_PIN_ZMM21", py_var(vm, REG_PIN_ZMM21));
        module->attr().set("REG_PIN_ZMM22", py_var(vm, REG_PIN_ZMM22));
        module->attr().set("REG_PIN_ZMM23", py_var(vm, REG_PIN_ZMM23));
        module->attr().set("REG_PIN_ZMM24", py_var(vm, REG_PIN_ZMM24));
        module->attr().set("REG_PIN_ZMM25", py_var(vm, REG_PIN_ZMM25));
        module->attr().set("REG_PIN_ZMM26", py_var(vm, REG_PIN_ZMM26));
        module->attr().set("REG_PIN_ZMM27", py_var(vm, REG_PIN_ZMM27));
        module->attr().set("REG_PIN_ZMM28", py_var(vm, REG_PIN_ZMM28));
        module->attr().set("REG_PIN_ZMM29", py_var(vm, REG_PIN_ZMM29));
        module->attr().set("REG_PIN_ZMM30", py_var(vm, REG_PIN_ZMM30));
        module->attr().set("REG_PIN_ZMM31", py_var(vm, REG_PIN_ZMM31));
        module->attr().set("REG_PIN_ZMM_AVX512_HI16_LAST", py_var(vm, REG_PIN_ZMM_AVX512_HI16_LAST));
#endif // #if defined(TARGET_IA32E)

        module->attr().set("REG_INVALID_", py_var(vm, REG_INVALID_));
        module->attr().set("REG_NONE", py_var(vm, REG_NONE));
        module->attr().set("REG_FIRST", py_var(vm, REG_FIRST));
        module->attr().set("REG_RBASE", py_var(vm, REG_RBASE));
        module->attr().set("REG_MACHINE_BASE", py_var(vm, REG_MACHINE_BASE));
        module->attr().set("REG_APPLICATION_BASE", py_var(vm, REG_APPLICATION_BASE));
        module->attr().set("REG_PHYSICAL_INTEGER_BASE", py_var(vm, REG_PHYSICAL_INTEGER_BASE));
        module->attr().set("REG_TO_SPILL_BASE", py_var(vm, REG_TO_SPILL_BASE));
        module->attr().set("REG_GR_BASE", py_var(vm, REG_GR_BASE));
        module->attr().set("REG_GDI", py_var(vm, REG_GDI));
        module->attr().set("REG_GSI", py_var(vm, REG_GSI));
        module->attr().set("REG_GBP", py_var(vm, REG_GBP));
        module->attr().set("REG_STACK_PTR", py_var(vm, REG_STACK_PTR));
        module->attr().set("REG_GBX", py_var(vm, REG_GBX));
        module->attr().set("REG_GDX", py_var(vm, REG_GDX));
        module->attr().set("REG_GCX", py_var(vm, REG_GCX));
        module->attr().set("REG_GAX", py_var(vm, REG_GAX));
        module->attr().set("REG_GR_LAST", py_var(vm, REG_GR_LAST));
        module->attr().set("REG_SEG_BASE", py_var(vm, REG_SEG_BASE));
        module->attr().set("REG_SEG_CS", py_var(vm, REG_SEG_CS));
        module->attr().set("REG_SEG_SS", py_var(vm, REG_SEG_SS));
        module->attr().set("REG_SEG_DS", py_var(vm, REG_SEG_DS));
        module->attr().set("REG_SEG_ES", py_var(vm, REG_SEG_ES));
        module->attr().set("REG_SEG_FS", py_var(vm, REG_SEG_FS));
        module->attr().set("REG_SEG_GS", py_var(vm, REG_SEG_GS));
        module->attr().set("REG_SEG_LAST", py_var(vm, REG_SEG_LAST));
        module->attr().set("REG_GFLAGS", py_var(vm, REG_GFLAGS));
        module->attr().set("REG_INST_PTR", py_var(vm, REG_INST_PTR));
        module->attr().set("REG_PHYSICAL_INTEGER_END", py_var(vm, REG_PHYSICAL_INTEGER_END));
        module->attr().set("REG_AL", py_var(vm, REG_AL));
        module->attr().set("REG_AH", py_var(vm, REG_AH));
        module->attr().set("REG_AX", py_var(vm, REG_AX));
        module->attr().set("REG_CL", py_var(vm, REG_CL));
        module->attr().set("REG_CH", py_var(vm, REG_CH));
        module->attr().set("REG_CX", py_var(vm, REG_CX));
        module->attr().set("REG_DL", py_var(vm, REG_DL));
        module->attr().set("REG_DH", py_var(vm, REG_DH));
        module->attr().set("REG_DX", py_var(vm, REG_DX));
        module->attr().set("REG_BL", py_var(vm, REG_BL));
        module->attr().set("REG_BH", py_var(vm, REG_BH));
        module->attr().set("REG_BX", py_var(vm, REG_BX));
        module->attr().set("REG_BP", py_var(vm, REG_BP));
        module->attr().set("REG_SI", py_var(vm, REG_SI));
        module->attr().set("REG_DI", py_var(vm, REG_DI));
        module->attr().set("REG_SP", py_var(vm, REG_SP));
        module->attr().set("REG_FLAGS", py_var(vm, REG_FLAGS));
        module->attr().set("REG_IP", py_var(vm, REG_IP));
        module->attr().set("REG_EDI", py_var(vm, LEVEL_BASE::REG_EDI));
        module->attr().set("REG_ESI", py_var(vm, LEVEL_BASE::REG_ESI));
        module->attr().set("REG_EBP", py_var(vm, LEVEL_BASE::REG_EBP));
        module->attr().set("REG_ESP", py_var(vm, LEVEL_BASE::REG_ESP));
        module->attr().set("REG_EBX", py_var(vm, LEVEL_BASE::REG_EBX));
        module->attr().set("REG_EDX", py_var(vm, LEVEL_BASE::REG_EDX));
        module->attr().set("REG_ECX", py_var(vm, LEVEL_BASE::REG_ECX));
        module->attr().set("REG_EAX", py_var(vm, LEVEL_BASE::REG_EAX));
        module->attr().set("REG_EFLAGS", py_var(vm, REG_EFLAGS));
        module->attr().set("REG_EIP", py_var(vm, LEVEL_BASE::REG_EIP));
        module->attr().set("REG_MM_BASE", py_var(vm, REG_MM_BASE));
        module->attr().set("REG_MM0", py_var(vm, REG_MM0));
        module->attr().set("REG_MM1", py_var(vm, REG_MM1));
        module->attr().set("REG_MM2", py_var(vm, REG_MM2));
        module->attr().set("REG_MM3", py_var(vm, REG_MM3));
        module->attr().set("REG_MM4", py_var(vm, REG_MM4));
        module->attr().set("REG_MM5", py_var(vm, REG_MM5));
        module->attr().set("REG_MM6", py_var(vm, REG_MM6));
        module->attr().set("REG_MM7", py_var(vm, REG_MM7));
        module->attr().set("REG_MM_LAST", py_var(vm, REG_MM_LAST));
        module->attr().set("REG_XMM_BASE", py_var(vm, REG_XMM_BASE));
        module->attr().set("REG_FIRST_FP_REG", py_var(vm, REG_FIRST_FP_REG));
        module->attr().set("REG_XMM0", py_var(vm, REG_XMM0));
        module->attr().set("REG_XMM1", py_var(vm, REG_XMM1));
        module->attr().set("REG_XMM2", py_var(vm, REG_XMM2));
        module->attr().set("REG_XMM3", py_var(vm, REG_XMM3));
        module->attr().set("REG_XMM4", py_var(vm, REG_XMM4));
        module->attr().set("REG_XMM5", py_var(vm, REG_XMM5));
        module->attr().set("REG_XMM6", py_var(vm, REG_XMM6));
        module->attr().set("REG_XMM7", py_var(vm, REG_XMM7));
        module->attr().set("REG_XMM_SSE_LAST", py_var(vm, REG_XMM_SSE_LAST));
        module->attr().set("REG_XMM_AVX_LAST", py_var(vm, REG_XMM_AVX_LAST));
        module->attr().set("REG_XMM_AVX512_LAST", py_var(vm, REG_XMM_AVX512_LAST));
        module->attr().set("REG_XMM_LAST", py_var(vm, REG_XMM_LAST));
        module->attr().set("REG_YMM_BASE", py_var(vm, REG_YMM_BASE));
        module->attr().set("REG_YMM0", py_var(vm, REG_YMM0));
        module->attr().set("REG_YMM1", py_var(vm, REG_YMM1));
        module->attr().set("REG_YMM2", py_var(vm, REG_YMM2));
        module->attr().set("REG_YMM3", py_var(vm, REG_YMM3));
        module->attr().set("REG_YMM4", py_var(vm, REG_YMM4));
        module->attr().set("REG_YMM5", py_var(vm, REG_YMM5));
        module->attr().set("REG_YMM6", py_var(vm, REG_YMM6));
        module->attr().set("REG_YMM7", py_var(vm, REG_YMM7));
        module->attr().set("REG_YMM_AVX_LAST", py_var(vm, REG_YMM_AVX_LAST));
        module->attr().set("REG_YMM_AVX512_LAST", py_var(vm, REG_YMM_AVX512_LAST));
        module->attr().set("REG_YMM_LAST", py_var(vm, REG_YMM_LAST));
        module->attr().set("REG_ZMM_BASE", py_var(vm, REG_ZMM_BASE));
        module->attr().set("REG_ZMM0", py_var(vm, REG_ZMM0));
        module->attr().set("REG_ZMM1", py_var(vm, REG_ZMM1));
        module->attr().set("REG_ZMM2", py_var(vm, REG_ZMM2));
        module->attr().set("REG_ZMM3", py_var(vm, REG_ZMM3));
        module->attr().set("REG_ZMM4", py_var(vm, REG_ZMM4));
        module->attr().set("REG_ZMM5", py_var(vm, REG_ZMM5));
        module->attr().set("REG_ZMM6", py_var(vm, REG_ZMM6));
        module->attr().set("REG_ZMM7", py_var(vm, REG_ZMM7));
        module->attr().set("REG_ZMM_AVX512_SPLIT_LAST", py_var(vm, REG_ZMM_AVX512_SPLIT_LAST));
        module->attr().set("REG_ZMM_AVX512_LAST", py_var(vm, REG_ZMM_AVX512_LAST));
        module->attr().set("REG_ZMM_LAST", py_var(vm, REG_ZMM_LAST));
        module->attr().set("REG_K_BASE", py_var(vm, REG_K_BASE));
        module->attr().set("REG_K0", py_var(vm, REG_K0));
        module->attr().set("REG_IMPLICIT_FULL_MASK", py_var(vm, REG_IMPLICIT_FULL_MASK));
        module->attr().set("REG_K1", py_var(vm, REG_K1));
        module->attr().set("REG_K2", py_var(vm, REG_K2));
        module->attr().set("REG_K3", py_var(vm, REG_K3));
        module->attr().set("REG_K4", py_var(vm, REG_K4));
        module->attr().set("REG_K5", py_var(vm, REG_K5));
        module->attr().set("REG_K6", py_var(vm, REG_K6));
        module->attr().set("REG_K7", py_var(vm, REG_K7));
        module->attr().set("REG_K_LAST", py_var(vm, REG_K_LAST));
        module->attr().set("REG_MXCSR", py_var(vm, REG_MXCSR));
        module->attr().set("REG_MXCSRMASK", py_var(vm, REG_MXCSRMASK));
        module->attr().set("REG_ORIG_GAX", py_var(vm, REG_ORIG_GAX));
        module->attr().set("REG_FPST_BASE", py_var(vm, REG_FPST_BASE));
        module->attr().set("REG_FPSTATUS_BASE", py_var(vm, REG_FPSTATUS_BASE));
        module->attr().set("REG_FPCW", py_var(vm, REG_FPCW));
        module->attr().set("REG_FPSW", py_var(vm, REG_FPSW));
        module->attr().set("REG_FPTAG", py_var(vm, REG_FPTAG));
        module->attr().set("REG_FPIP_OFF", py_var(vm, REG_FPIP_OFF));
        module->attr().set("REG_FPIP_SEL", py_var(vm, REG_FPIP_SEL));
        module->attr().set("REG_FPOPCODE", py_var(vm, REG_FPOPCODE));
        module->attr().set("REG_FPDP_OFF", py_var(vm, REG_FPDP_OFF));
        module->attr().set("REG_FPDP_SEL", py_var(vm, REG_FPDP_SEL));
        module->attr().set("REG_FPSTATUS_LAST", py_var(vm, REG_FPSTATUS_LAST));
        module->attr().set("REG_ST_BASE", py_var(vm, REG_ST_BASE));
        module->attr().set("REG_ST0", py_var(vm, REG_ST0));
        module->attr().set("REG_ST1", py_var(vm, REG_ST1));
        module->attr().set("REG_ST2", py_var(vm, REG_ST2));
        module->attr().set("REG_ST3", py_var(vm, REG_ST3));
        module->attr().set("REG_ST4", py_var(vm, REG_ST4));
        module->attr().set("REG_ST5", py_var(vm, REG_ST5));
        module->attr().set("REG_ST6", py_var(vm, REG_ST6));
        module->attr().set("REG_ST7", py_var(vm, REG_ST7));
        module->attr().set("REG_ST_LAST", py_var(vm, REG_ST_LAST));
        module->attr().set("REG_FPST_LAST", py_var(vm, REG_FPST_LAST));
        module->attr().set("REG_DR_BASE", py_var(vm, REG_DR_BASE));
        module->attr().set("REG_DR0", py_var(vm, REG_DR0));
        module->attr().set("REG_DR1", py_var(vm, REG_DR1));
        module->attr().set("REG_DR2", py_var(vm, REG_DR2));
        module->attr().set("REG_DR3", py_var(vm, REG_DR3));
        module->attr().set("REG_DR4", py_var(vm, REG_DR4));
        module->attr().set("REG_DR5", py_var(vm, REG_DR5));
        module->attr().set("REG_DR6", py_var(vm, REG_DR6));
        module->attr().set("REG_DR7", py_var(vm, REG_DR7));
        module->attr().set("REG_DR_LAST", py_var(vm, REG_DR_LAST));
        module->attr().set("REG_CR_BASE", py_var(vm, REG_CR_BASE));
        module->attr().set("REG_CR0", py_var(vm, REG_CR0));
        module->attr().set("REG_CR1", py_var(vm, REG_CR1));
        module->attr().set("REG_CR2", py_var(vm, LEVEL_BASE::REG_CR2));
        module->attr().set("REG_CR3", py_var(vm, REG_CR3));
        module->attr().set("REG_CR4", py_var(vm, REG_CR4));
        module->attr().set("REG_CR_LAST", py_var(vm, REG_CR_LAST));
        module->attr().set("REG_TSSR", py_var(vm, REG_TSSR));
        module->attr().set("REG_LDTR", py_var(vm, REG_LDTR));
        module->attr().set("REG_TR_BASE", py_var(vm, REG_TR_BASE));
        module->attr().set("REG_TR", py_var(vm, REG_TR));
        module->attr().set("REG_TR3", py_var(vm, REG_TR3));
        module->attr().set("REG_TR4", py_var(vm, REG_TR4));
        module->attr().set("REG_TR5", py_var(vm, REG_TR5));
        module->attr().set("REG_TR6", py_var(vm, REG_TR6));
        module->attr().set("REG_TR7", py_var(vm, REG_TR7));
        module->attr().set("REG_TR_LAST", py_var(vm, REG_TR_LAST));
        module->attr().set("REG_MACHINE_LAST", py_var(vm, REG_MACHINE_LAST));
        module->attr().set("REG_STATUS_FLAGS", py_var(vm, REG_STATUS_FLAGS));
        module->attr().set("REG_DF_FLAG", py_var(vm, REG_DF_FLAG));
        module->attr().set("REG_APPLICATION_LAST", py_var(vm, REG_APPLICATION_LAST));
        module->attr().set("REG_TOOL_BASE", py_var(vm, REG_TOOL_BASE));
        module->attr().set("REG_SEG_GS_BASE", py_var(vm, REG_SEG_GS_BASE));
        module->attr().set("REG_SEG_FS_BASE", py_var(vm, REG_SEG_FS_BASE));
        module->attr().set("REG_INST_BASE", py_var(vm, REG_INST_BASE));
        module->attr().set("REG_INST_SCRATCH_BASE", py_var(vm, REG_INST_SCRATCH_BASE));
        module->attr().set("REG_INST_G0", py_var(vm, REG_INST_G0));
        module->attr().set("REG_INST_G1", py_var(vm, REG_INST_G1));
        module->attr().set("REG_INST_G2", py_var(vm, REG_INST_G2));
        module->attr().set("REG_INST_G3", py_var(vm, REG_INST_G3));
        module->attr().set("REG_INST_G4", py_var(vm, REG_INST_G4));
        module->attr().set("REG_INST_G5", py_var(vm, REG_INST_G5));
        module->attr().set("REG_INST_G6", py_var(vm, REG_INST_G6));
        module->attr().set("REG_INST_G7", py_var(vm, REG_INST_G7));
        module->attr().set("REG_INST_G8", py_var(vm, REG_INST_G8));
        module->attr().set("REG_INST_G9", py_var(vm, REG_INST_G9));
        module->attr().set("REG_INST_G10", py_var(vm, REG_INST_G10));
        module->attr().set("REG_INST_G11", py_var(vm, REG_INST_G11));
        module->attr().set("REG_INST_G12", py_var(vm, REG_INST_G12));
        module->attr().set("REG_INST_G13", py_var(vm, REG_INST_G13));
        module->attr().set("REG_INST_G14", py_var(vm, REG_INST_G14));
        module->attr().set("REG_INST_G15", py_var(vm, REG_INST_G15));
        module->attr().set("REG_INST_G16", py_var(vm, REG_INST_G16));
        module->attr().set("REG_INST_G17", py_var(vm, REG_INST_G17));
        module->attr().set("REG_INST_G18", py_var(vm, REG_INST_G18));
        module->attr().set("REG_INST_G19", py_var(vm, REG_INST_G19));
        module->attr().set("REG_INST_G20", py_var(vm, REG_INST_G20));
        module->attr().set("REG_INST_G21", py_var(vm, REG_INST_G21));
        module->attr().set("REG_INST_G22", py_var(vm, REG_INST_G22));
        module->attr().set("REG_INST_G23", py_var(vm, REG_INST_G23));
        module->attr().set("REG_INST_G24", py_var(vm, REG_INST_G24));
        module->attr().set("REG_INST_G25", py_var(vm, REG_INST_G25));
        module->attr().set("REG_INST_G26", py_var(vm, REG_INST_G26));
        module->attr().set("REG_INST_G27", py_var(vm, REG_INST_G27));
        module->attr().set("REG_INST_G28", py_var(vm, REG_INST_G28));
        module->attr().set("REG_INST_G29", py_var(vm, REG_INST_G29));
        module->attr().set("REG_INST_TOOL_FIRST", py_var(vm, REG_INST_TOOL_FIRST));
        module->attr().set("REG_INST_TOOL_LAST", py_var(vm, REG_INST_TOOL_LAST));
        module->attr().set("REG_BUF_BASE0", py_var(vm, REG_BUF_BASE0));
        module->attr().set("REG_BUF_BASE1", py_var(vm, REG_BUF_BASE1));
        module->attr().set("REG_BUF_BASE2", py_var(vm, REG_BUF_BASE2));
        module->attr().set("REG_BUF_BASE3", py_var(vm, REG_BUF_BASE3));
        module->attr().set("REG_BUF_BASE4", py_var(vm, REG_BUF_BASE4));
        module->attr().set("REG_BUF_BASE5", py_var(vm, REG_BUF_BASE5));
        module->attr().set("REG_BUF_BASE6", py_var(vm, REG_BUF_BASE6));
        module->attr().set("REG_BUF_BASE7", py_var(vm, REG_BUF_BASE7));
        module->attr().set("REG_BUF_BASE8", py_var(vm, REG_BUF_BASE8));
        module->attr().set("REG_BUF_BASE9", py_var(vm, REG_BUF_BASE9));
        module->attr().set("REG_BUF_BASE_LAST", py_var(vm, REG_BUF_BASE_LAST));
        module->attr().set("REG_BUF_END0", py_var(vm, REG_BUF_END0));
        module->attr().set("REG_BUF_END1", py_var(vm, REG_BUF_END1));
        module->attr().set("REG_BUF_END2", py_var(vm, REG_BUF_END2));
        module->attr().set("REG_BUF_END3", py_var(vm, REG_BUF_END3));
        module->attr().set("REG_BUF_END4", py_var(vm, REG_BUF_END4));
        module->attr().set("REG_BUF_END5", py_var(vm, REG_BUF_END5));
        module->attr().set("REG_BUF_END6", py_var(vm, REG_BUF_END6));
        module->attr().set("REG_BUF_END7", py_var(vm, REG_BUF_END7));
        module->attr().set("REG_BUF_END8", py_var(vm, REG_BUF_END8));
        module->attr().set("REG_BUF_END9", py_var(vm, REG_BUF_END9));
        module->attr().set("REG_BUF_ENDLAST", py_var(vm, REG_BUF_ENDLAST));
        module->attr().set("REG_BUF_LAST", py_var(vm, REG_BUF_LAST));
        module->attr().set("REG_INST_SCRATCH_LAST", py_var(vm, REG_INST_SCRATCH_LAST));
        module->attr().set("REG_TOOL_LAST", py_var(vm, REG_TOOL_LAST));
        module->attr().set("REG_SPECIAL_BASE", py_var(vm, REG_SPECIAL_BASE));
        module->attr().set("REG_X87", py_var(vm, REG_X87));
        module->attr().set("REG_SPECIAL_LAST", py_var(vm, REG_SPECIAL_LAST));
        module->attr().set("REG_PIN_BASE", py_var(vm, REG_PIN_BASE));
        module->attr().set("REG_PIN_SEG_GS_VAL", py_var(vm, REG_PIN_SEG_GS_VAL));
        module->attr().set("REG_PIN_SEG_FS_VAL", py_var(vm, REG_PIN_SEG_FS_VAL));
        module->attr().set("REG_LAST_CONTEXT_REG", py_var(vm, REG_LAST_CONTEXT_REG));
        module->attr().set("REG_PIN_GR_BASE", py_var(vm, REG_PIN_GR_BASE));
        module->attr().set("REG_PIN_EDI", py_var(vm, REG_PIN_EDI));
        module->attr().set("REG_PIN_ESI", py_var(vm, REG_PIN_ESI));
        module->attr().set("REG_PIN_EBP", py_var(vm, REG_PIN_EBP));
        module->attr().set("REG_PIN_ESP", py_var(vm, REG_PIN_ESP));
        module->attr().set("REG_PIN_EBX", py_var(vm, REG_PIN_EBX));
        module->attr().set("REG_PIN_EDX", py_var(vm, REG_PIN_EDX));
        module->attr().set("REG_PIN_ECX", py_var(vm, REG_PIN_ECX));
        module->attr().set("REG_PIN_EAX", py_var(vm, REG_PIN_EAX));
        module->attr().set("REG_PIN_AL", py_var(vm, REG_PIN_AL));
        module->attr().set("REG_PIN_AH", py_var(vm, REG_PIN_AH));
        module->attr().set("REG_PIN_AX", py_var(vm, REG_PIN_AX));
        module->attr().set("REG_PIN_CL", py_var(vm, REG_PIN_CL));
        module->attr().set("REG_PIN_CH", py_var(vm, REG_PIN_CH));
        module->attr().set("REG_PIN_CX", py_var(vm, REG_PIN_CX));
        module->attr().set("REG_PIN_DL", py_var(vm, REG_PIN_DL));
        module->attr().set("REG_PIN_DH", py_var(vm, REG_PIN_DH));
        module->attr().set("REG_PIN_DX", py_var(vm, REG_PIN_DX));
        module->attr().set("REG_PIN_BL", py_var(vm, REG_PIN_BL));
        module->attr().set("REG_PIN_BH", py_var(vm, REG_PIN_BH));
        module->attr().set("REG_PIN_BX", py_var(vm, REG_PIN_BX));
        module->attr().set("REG_PIN_BP", py_var(vm, REG_PIN_BP));
        module->attr().set("REG_PIN_SI", py_var(vm, REG_PIN_SI));
        module->attr().set("REG_PIN_DI", py_var(vm, REG_PIN_DI));
        module->attr().set("REG_PIN_SP", py_var(vm, REG_PIN_SP));
        module->attr().set("REG_PIN_GDI", py_var(vm, REG_PIN_GDI));
        module->attr().set("REG_PIN_GSI", py_var(vm, REG_PIN_GSI));
        module->attr().set("REG_PIN_GBP", py_var(vm, REG_PIN_GBP));
        module->attr().set("REG_PIN_STACK_PTR", py_var(vm, REG_PIN_STACK_PTR));
        module->attr().set("REG_PIN_GBX", py_var(vm, REG_PIN_GBX));
        module->attr().set("REG_PIN_GDX", py_var(vm, REG_PIN_GDX));
        module->attr().set("REG_PIN_GCX", py_var(vm, REG_PIN_GCX));
        module->attr().set("REG_PIN_GAX", py_var(vm, REG_PIN_GAX));
        module->attr().set("REG_PIN_THREAD_ID", py_var(vm, REG_PIN_THREAD_ID));
        module->attr().set("REG_PIN_INDIRREG", py_var(vm, REG_PIN_INDIRREG));
        module->attr().set("REG_PIN_IPRELADDR", py_var(vm, REG_PIN_IPRELADDR));
        module->attr().set("REG_PIN_SYSENTER_RESUMEADDR", py_var(vm, REG_PIN_SYSENTER_RESUMEADDR));
        module->attr().set("REG_PIN_SYSCALL_NEXT_PC", py_var(vm, REG_PIN_SYSCALL_NEXT_PC));
        module->attr().set("REG_PIN_VMENTER", py_var(vm, REG_PIN_VMENTER));
        module->attr().set("REG_PIN_T_BASE", py_var(vm, REG_PIN_T_BASE));
        module->attr().set("REG_PIN_T0", py_var(vm, REG_PIN_T0));
        module->attr().set("REG_PIN_T1", py_var(vm, REG_PIN_T1));
        module->attr().set("REG_PIN_T2", py_var(vm, REG_PIN_T2));
        module->attr().set("REG_PIN_T3", py_var(vm, REG_PIN_T3));
        module->attr().set("REG_PIN_T0D", py_var(vm, REG_PIN_T0D));
        module->attr().set("REG_PIN_T1D", py_var(vm, REG_PIN_T1D));
        module->attr().set("REG_PIN_T2D", py_var(vm, REG_PIN_T2D));
        module->attr().set("REG_PIN_T3D", py_var(vm, REG_PIN_T3D));
        module->attr().set("REG_PIN_T0W", py_var(vm, REG_PIN_T0W));
        module->attr().set("REG_PIN_T1W", py_var(vm, REG_PIN_T1W));
        module->attr().set("REG_PIN_T2W", py_var(vm, REG_PIN_T2W));
        module->attr().set("REG_PIN_T3W", py_var(vm, REG_PIN_T3W));
        module->attr().set("REG_PIN_T0L", py_var(vm, REG_PIN_T0L));
        module->attr().set("REG_PIN_T1L", py_var(vm, REG_PIN_T1L));
        module->attr().set("REG_PIN_T2L", py_var(vm, REG_PIN_T2L));
        module->attr().set("REG_PIN_T3L", py_var(vm, REG_PIN_T3L));
        module->attr().set("REG_PIN_T_LAST", py_var(vm, REG_PIN_T_LAST));
        module->attr().set("REG_PIN_THREAD_IDD", py_var(vm, REG_PIN_THREAD_IDD));
        module->attr().set("REG_TO_SPILL_LAST", py_var(vm, REG_TO_SPILL_LAST));
        module->attr().set("REG_PIN_INST_COND", py_var(vm, REG_PIN_INST_COND));
        module->attr().set("REG_PIN_INST_T0", py_var(vm, REG_PIN_INST_T0));
        module->attr().set("REG_PIN_INST_T1", py_var(vm, REG_PIN_INST_T1));
        module->attr().set("REG_PIN_INST_T2", py_var(vm, REG_PIN_INST_T2));
        module->attr().set("REG_PIN_INST_T3", py_var(vm, REG_PIN_INST_T3));
        module->attr().set("REG_PIN_INST_T0D", py_var(vm, REG_PIN_INST_T0D));
        module->attr().set("REG_PIN_INST_T1D", py_var(vm, REG_PIN_INST_T1D));
        module->attr().set("REG_PIN_INST_T2D", py_var(vm, REG_PIN_INST_T2D));
        module->attr().set("REG_PIN_INST_T3D", py_var(vm, REG_PIN_INST_T3D));
        module->attr().set("REG_PIN_INST_T0W", py_var(vm, REG_PIN_INST_T0W));
        module->attr().set("REG_PIN_INST_T1W", py_var(vm, REG_PIN_INST_T1W));
        module->attr().set("REG_PIN_INST_T2W", py_var(vm, REG_PIN_INST_T2W));
        module->attr().set("REG_PIN_INST_T3W", py_var(vm, REG_PIN_INST_T3W));
        module->attr().set("REG_PIN_INST_T0L", py_var(vm, REG_PIN_INST_T0L));
        module->attr().set("REG_PIN_INST_T1L", py_var(vm, REG_PIN_INST_T1L));
        module->attr().set("REG_PIN_INST_T2L", py_var(vm, REG_PIN_INST_T2L));
        module->attr().set("REG_PIN_INST_T3L", py_var(vm, REG_PIN_INST_T3L));
        module->attr().set("REG_PIN_INST_PRESERVED_PREDICATE", py_var(vm, REG_PIN_INST_PRESERVED_PREDICATE));
        module->attr().set("REG_PIN_FLAGS_BEFORE_AC_CLEARING", py_var(vm, REG_PIN_FLAGS_BEFORE_AC_CLEARING));
        module->attr().set("REG_PIN_BRIDGE_ORIG_SP", py_var(vm, REG_PIN_BRIDGE_ORIG_SP));
        module->attr().set("REG_PIN_BRIDGE_APP_IP", py_var(vm, REG_PIN_BRIDGE_APP_IP));
        module->attr().set("REG_PIN_BRIDGE_SP_BEFORE_ALIGN", py_var(vm, REG_PIN_BRIDGE_SP_BEFORE_ALIGN));
        module->attr().set("REG_PIN_BRIDGE_SP_BEFORE_CALL", py_var(vm, REG_PIN_BRIDGE_SP_BEFORE_CALL));
        module->attr().set("REG_PIN_BRIDGE_SP_BEFORE_MARSHALLING_FRAME", py_var(vm, REG_PIN_BRIDGE_SP_BEFORE_MARSHALLING_FRAME));
        module->attr().set("REG_PIN_BRIDGE_MARSHALLING_FRAME", py_var(vm, REG_PIN_BRIDGE_MARSHALLING_FRAME));
        module->attr().set("REG_PIN_BRIDGE_ON_STACK_CONTEXT_FRAME", py_var(vm, REG_PIN_BRIDGE_ON_STACK_CONTEXT_FRAME));
        module->attr().set("REG_PIN_BRIDGE_ON_STACK_CONTEXT_SP", py_var(vm, REG_PIN_BRIDGE_ON_STACK_CONTEXT_SP));
        module->attr().set("REG_PIN_BRIDGE_MULTI_MEMORYACCESS_FRAME", py_var(vm, REG_PIN_BRIDGE_MULTI_MEMORYACCESS_FRAME));
        module->attr().set("REG_PIN_BRIDGE_MULTI_MEMORYACCESS_SP", py_var(vm, REG_PIN_BRIDGE_MULTI_MEMORYACCESS_SP));
        module->attr().set("REG_PIN_MULTI_MEM_ACCESS_AND_REWRITE_EMULATION_INFO_FRAME",
                           py_var(vm, REG_PIN_MULTI_MEM_ACCESS_AND_REWRITE_EMULATION_INFO_FRAME));
        module->attr().set("REG_PIN_OPERANDS_INFO_OP0", py_var(vm, REG_PIN_OPERANDS_INFO_OP0));
        module->attr().set("REG_PIN_OPERANDS_INFO_OP1", py_var(vm, REG_PIN_OPERANDS_INFO_OP1));
        module->attr().set("REG_PIN_OPERANDS_INFO_OP2", py_var(vm, REG_PIN_OPERANDS_INFO_OP2));
        module->attr().set("REG_PIN_OPERANDS_INFO_OP3", py_var(vm, REG_PIN_OPERANDS_INFO_OP3));
        module->attr().set("REG_PIN_OPERANDS_INFO_GEN_SP", py_var(vm, REG_PIN_OPERANDS_INFO_GEN_SP));
        module->attr().set("REG_PIN_MEM_OPERAND_REWRITE", py_var(vm, REG_PIN_MEM_OPERAND_REWRITE));
        module->attr().set("REG_PIN_BRIDGE_TRANS_MEMORY_CALLBACK_FRAME", py_var(vm, REG_PIN_BRIDGE_TRANS_MEMORY_CALLBACK_FRAME));
        module->attr().set("REG_PIN_BRIDGE_TRANS_MEMORY_CALLBACK_SP", py_var(vm, REG_PIN_BRIDGE_TRANS_MEMORY_CALLBACK_SP));
        module->attr().set("REG_PIN_TRANS_MEMORY_CALLBACK_READ_ADDR", py_var(vm, REG_PIN_TRANS_MEMORY_CALLBACK_READ_ADDR));
        module->attr().set("REG_PIN_TRANS_MEMORY_CALLBACK_READ2_ADDR", py_var(vm, REG_PIN_TRANS_MEMORY_CALLBACK_READ2_ADDR));
        module->attr().set("REG_PIN_TRANS_MEMORY_CALLBACK_WRITE_ADDR", py_var(vm, REG_PIN_TRANS_MEMORY_CALLBACK_WRITE_ADDR));
        module->attr().set("REG_PIN_BRIDGE_SPILL_AREA_CONTEXT_FRAME", py_var(vm, REG_PIN_BRIDGE_SPILL_AREA_CONTEXT_FRAME));
        module->attr().set("REG_PIN_BRIDGE_SPILL_AREA_CONTEXT_SP", py_var(vm, REG_PIN_BRIDGE_SPILL_AREA_CONTEXT_SP));
        module->attr().set("REG_PIN_AVX_IN_USE", py_var(vm, REG_PIN_AVX_IN_USE));
        module->attr().set("REG_PIN_SPILLPTR", py_var(vm, REG_PIN_SPILLPTR));
        module->attr().set("REG_PIN_GR_LAST", py_var(vm, REG_PIN_GR_LAST));
        module->attr().set("REG_PIN_X87", py_var(vm, REG_PIN_X87));
        module->attr().set("REG_PIN_MXCSR", py_var(vm, REG_PIN_MXCSR));
        module->attr().set("REG_PIN_STATUS_FLAGS", py_var(vm, REG_PIN_STATUS_FLAGS));
        module->attr().set("REG_PIN_DF_FLAG", py_var(vm, REG_PIN_DF_FLAG));
        module->attr().set("REG_PIN_FLAGS", py_var(vm, REG_PIN_FLAGS));
        module->attr().set("REG_PIN_XMM_BASE", py_var(vm, REG_PIN_XMM_BASE));
        module->attr().set("REG_PIN_XMM0", py_var(vm, REG_PIN_XMM0));
        module->attr().set("REG_PIN_XMM1", py_var(vm, REG_PIN_XMM1));
        module->attr().set("REG_PIN_XMM2", py_var(vm, REG_PIN_XMM2));
        module->attr().set("REG_PIN_XMM3", py_var(vm, REG_PIN_XMM3));
        module->attr().set("REG_PIN_XMM4", py_var(vm, REG_PIN_XMM4));
        module->attr().set("REG_PIN_XMM5", py_var(vm, REG_PIN_XMM5));
        module->attr().set("REG_PIN_XMM6", py_var(vm, REG_PIN_XMM6));
        module->attr().set("REG_PIN_XMM7", py_var(vm, REG_PIN_XMM7));
        module->attr().set("REG_PIN_XMM_SSE_LAST", py_var(vm, REG_PIN_XMM_SSE_LAST));
        module->attr().set("REG_PIN_XMM_AVX_LAST", py_var(vm, REG_PIN_XMM_AVX_LAST));
        module->attr().set("REG_PIN_XMM_AVX512_LAST", py_var(vm, REG_PIN_XMM_AVX512_LAST));
        module->attr().set("REG_PIN_XMM_LAST", py_var(vm, REG_PIN_XMM_LAST));
        module->attr().set("REG_PIN_YMM_BASE", py_var(vm, REG_PIN_YMM_BASE));
        module->attr().set("REG_PIN_YMM0", py_var(vm, REG_PIN_YMM0));
        module->attr().set("REG_PIN_YMM1", py_var(vm, REG_PIN_YMM1));
        module->attr().set("REG_PIN_YMM2", py_var(vm, REG_PIN_YMM2));
        module->attr().set("REG_PIN_YMM3", py_var(vm, REG_PIN_YMM3));
        module->attr().set("REG_PIN_YMM4", py_var(vm, REG_PIN_YMM4));
        module->attr().set("REG_PIN_YMM5", py_var(vm, REG_PIN_YMM5));
        module->attr().set("REG_PIN_YMM6", py_var(vm, REG_PIN_YMM6));
        module->attr().set("REG_PIN_YMM7", py_var(vm, REG_PIN_YMM7));
        module->attr().set("REG_PIN_YMM_AVX_LAST", py_var(vm, REG_PIN_YMM_AVX_LAST));
        module->attr().set("REG_PIN_YMM_AVX512_LAST", py_var(vm, REG_PIN_YMM_AVX512_LAST));
        module->attr().set("REG_PIN_YMM_LAST", py_var(vm, REG_PIN_YMM_LAST));
        module->attr().set("REG_PIN_ZMM_BASE", py_var(vm, REG_PIN_ZMM_BASE));
        module->attr().set("REG_PIN_ZMM0", py_var(vm, REG_PIN_ZMM0));
        module->attr().set("REG_PIN_ZMM1", py_var(vm, REG_PIN_ZMM1));
        module->attr().set("REG_PIN_ZMM2", py_var(vm, REG_PIN_ZMM2));
        module->attr().set("REG_PIN_ZMM3", py_var(vm, REG_PIN_ZMM3));
        module->attr().set("REG_PIN_ZMM4", py_var(vm, REG_PIN_ZMM4));
        module->attr().set("REG_PIN_ZMM5", py_var(vm, REG_PIN_ZMM5));
        module->attr().set("REG_PIN_ZMM6", py_var(vm, REG_PIN_ZMM6));
        module->attr().set("REG_PIN_ZMM7", py_var(vm, REG_PIN_ZMM7));
        module->attr().set("REG_PIN_ZMM_AVX512_SPLIT_LAST", py_var(vm, REG_PIN_ZMM_AVX512_SPLIT_LAST));
        module->attr().set("REG_PIN_ZMM_AVX512_LAST", py_var(vm, REG_PIN_ZMM_AVX512_LAST));
        module->attr().set("REG_PIN_ZMM_LAST", py_var(vm, REG_PIN_ZMM_LAST));
        module->attr().set("REG_PIN_K_BASE", py_var(vm, REG_PIN_K_BASE));
        module->attr().set("REG_PIN_K0", py_var(vm, REG_PIN_K0));
        module->attr().set("REG_PIN_K1", py_var(vm, REG_PIN_K1));
        module->attr().set("REG_PIN_K2", py_var(vm, REG_PIN_K2));
        module->attr().set("REG_PIN_K3", py_var(vm, REG_PIN_K3));
        module->attr().set("REG_PIN_K4", py_var(vm, REG_PIN_K4));
        module->attr().set("REG_PIN_K5", py_var(vm, REG_PIN_K5));
        module->attr().set("REG_PIN_K6", py_var(vm, REG_PIN_K6));
        module->attr().set("REG_PIN_K7", py_var(vm, REG_PIN_K7));
        module->attr().set("REG_PIN_K_LAST", py_var(vm, REG_PIN_K_LAST));
        module->attr().set("REG_PIN_LAST", py_var(vm, REG_PIN_LAST));
        module->attr().set("REG_LAST", py_var(vm, REG_LAST));
    }

} // namespace glue

#endif // _GLUE_REG_H_