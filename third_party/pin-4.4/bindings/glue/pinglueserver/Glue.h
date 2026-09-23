/*
 * Copyright (C) 2024-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_H_
#define _GLUE_H_

#include "pin.H"
#include "gluepy.h"
#include <rscproto.h>
#include "pinglue_common.h"

namespace glue
{
// Constants and types
typedef std::vector< IARG_TYPE > t_iarg_type_list;
constexpr IARG_TYPE IARG_PYOBJ = (IARG_TYPE)(IARG_END + 1);

/**
 * @struct Glue_Proto
 * @brief This struct is used to associate a PIN PROTO with its argument types.
 *
 * The Glue_Proto struct serves as a container that save a specific proto
 * and the list of it's argument types. Those types will be used 
 * in GLUE_CallApplicationFunction when converting the args that passed by user.
 */
struct Glue_Proto
{
    /** @brief A PIN PROTO */
    PROTO proto;
    /** @brief The return value PARG_T* */
    PARG_T* retParg;
    /** @brief list of PARG_T* */
    std::vector< PARG_T* > pargsTypeList;
};

/**
 * @brief Helper function to call pocketpy vm with variadic arguments.
 * 
 * @param[in] vm       pocketpy vm ptr.
 * @param[in] callable python function.
 * @param[in] args     variadic args to unpack.
 * 
 * * @return The python return value.
 */
inline PyObject* vm_call_unpack(VM* vm, PyObject* callable, std::vector< PyObject* >& args)
{
    vm->s_data.push(callable);
    vm->s_data.push(PY_NULL);
    for (auto v : args)
    {
        vm->s_data.push(v);
    }

    return vm->vectorcall(args.size());
}

/**
 * @brief Helper function to get a python argument from a va_list of IARG_TYPE elements.
 * 
 * @param[in] vm      pocketpy vm ptr.
 * @param[in] list    va_list.
 * @param[in] argType argType of the next va_list arg.
 * 
 * @return The python argument.
 * 
 */
inline PyObject* get_py_arg(VM* vm, va_list& list, IARG_TYPE argType)
{
    switch (argType)
    {
        case glue::IARG_PYOBJ:
            return va_arg(list, PyObject*);

        case IARG_BOOL:
            return py_var(vm, static_cast< bool >(va_arg(list, ADDRINT)));
        case IARG_UINT32:
            return py_var(vm, va_arg(list, UINT32));
        case IARG_UINT64:
            return py_var(vm, va_arg(list, UINT64));
        case IARG_ADDRINT:
            return py_var(vm, va_arg(list, ADDRINT));
        case IARG_PTR:
            return py_var(vm, va_arg(list, VOID*));
        default:
            vm->TypeError("Unsupported IARG_TYPE: " + std::to_string(argType));
            return nullptr;
    }
}

/**
 * @brief Helper function to get t_rpc_arg from python argument.
 * 
 * @param[in]  vm      pocketpy vm ptr.
 * @param[in]  pyArg   the python argument.
 * @param[out] arg     the result t_rpc_arg.
 * 
 * @return void
 * 
 *                     raise:
 *                           vm->TypeError in case the python arg type is not supported.
 */
inline void get_rpc_arg_from_py_arg(VM* vm, PyObject* pyArg, t_rpc_arg& arg)
{
    // Currently we support only in: tp_int, tp_float, tp_bool, tp_str, None (nil)
    if (is_type(pyArg, vm->tp_str))
    {
        Str& str = py_cast< Str& >(vm, pyArg);
        arg      = MAKE_RPC_ARG(BUFFER_SCHEMA, (uint64_t)(str.c_str()), 1 + (size_t)str.size, RpcArgFlagsNone);
    }
    else if (is_int(pyArg))
    {
        arg = MAKE_RPC_ARG(INT_SCHEMA, (uint64_t)py_cast< int64_t >(vm, pyArg), ARG_SCHEMA_SIZE(INT_SCHEMA), RpcArgFlagsNone);
    }
    else if (is_float(pyArg))
    {
        // float is always treat as double
        double d = py_cast< double >(vm, pyArg);
        arg      = MAKE_RPC_ARG(FLOAT_SCHEMA, *reinterpret_cast< uint64_t* >(&d), ARG_SCHEMA_SIZE(FLOAT_SCHEMA), RpcArgFlagsNone);
    }
    else if (is_type(pyArg, vm->tp_bool))
    {
        arg = MAKE_RPC_ARG(BOOL_SCHEMA, (uint64_t)py_cast< bool >(vm, pyArg), ARG_SCHEMA_SIZE(BOOL_SCHEMA), RpcArgFlagsNone);
    }
    else if (vm->None == pyArg)
    {
        arg = MAKE_RPC_ARG(NIL_SCHEMA, 0, 0, RpcArgFlagsNone);
    }
    else
    {
        vm->TypeError("Unsupported arg type: " + Str(_type_name(vm, vm->_tp(pyArg)).sv()));
    }
}

/**
 * @brief Helper function to handle the conversion from python callback variadic arguments 
 *        to pin variadic args that will be passed to PIN _Insert APIs.
 * 
 * @param[in]  vm               pocketpy vm ptr.
 * @param[in]  varSize          size of pyVariadicArgs.
 * @param[in]  pyVariadicArgs   python variadic args.
 * @param[out] pinVariadicArgs  pin variadic args to fill.
 * @param[out] typesList        list contains the pin IARG_TYPE respectively to pinVariadicArgs elements.
 * 
 * @return bool - false if the analysis routine prototype return void, true if it return ADDRINT.
 *                e.g. In case IARG_RETURN_REGS is in variadic args the analysis return value is ADDRINT
 */
inline bool handle_insert_variadic_args(VM* vm, int varSize, Tuple& pyVariadicArgs, IARGLIST pinVariadicArgs,
                                        t_iarg_type_list& typesList)
{
    bool res = false;

    int varIdx = 0;
    while (varIdx < varSize)
    {
        IARG_TYPE iargType = py_cast< IARG_TYPE >(vm, pyVariadicArgs[varIdx++]);
        switch (iargType)
        {
            // UINT32 - additional arg required
            case IARG_UINT32:
            case IARG_MEMORYOP_SIZE:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, py_cast< UINT32 >(vm, pyVariadicArgs[varIdx++]), IARG_END);
                typesList.emplace_back(IARG_UINT32);
                break;
            // UINT32 that take no arguments
            case IARG_MEMORYREAD_SIZE:
            case IARG_MEMORYWRITE_SIZE:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, IARG_END);
                typesList.emplace_back(IARG_UINT32);
                break;
            // UINT64 - additional arg required
            case IARG_UINT64:
            case IARG_TSC:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, py_cast< UINT64 >(vm, pyVariadicArgs[varIdx++]), IARG_END);
                typesList.emplace_back(IARG_UINT64);
                break;
            // ADDRINT arg that is only for PIN (not for analysis routine)
            case IARG_CALL_ORDER:
            case IARG_EXPOSE:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, py_cast< ADDRINT >(vm, pyVariadicArgs[varIdx++]), IARG_END);
                break;
            // No type - only for PIN (not for analysis routine)
            case IARG_FAST_ANALYSIS_CALL:
            case IARG_CHECK_INLINE:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, IARG_END);
                break;
            case IARG_RETURN_REGS:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, py_cast< ADDRINT >(vm, pyVariadicArgs[varIdx++]), IARG_END);
                res = true;
                break;
            // BOOL - additional arg required
            case IARG_BOOL:
            case IARG_MEMORYOP_MASKED_ON:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, py_cast< BOOL >(vm, pyVariadicArgs[varIdx++]), IARG_END);
                typesList.emplace_back(IARG_BOOL);
                break;
            // BOOL that take no arguments
            case IARG_BRANCH_TAKEN:
            case IARG_EXECUTING:
            case IARG_FIRST_REP_ITERATION:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, IARG_END);
                typesList.emplace_back(IARG_BOOL);
                break;
            case glue::IARG_PYOBJ:
                IARGLIST_AddArguments(pinVariadicArgs, IARG_ADDRINT, pyVariadicArgs[varIdx++], IARG_END);
                typesList.emplace_back(glue::IARG_PYOBJ);
                break;
            // ADDRINT - additional arg required
            case IARG_ADDRINT:
            case IARG_FUNCARG_ENTRYPOINT_VALUE:
            case IARG_MEMORYOP_EA:
            case IARG_REG_VALUE:
            case IARG_SYSARG_VALUE:
            case IARG_FUNCARG_CALLSITE_VALUE:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, py_cast< ADDRINT >(vm, pyVariadicArgs[varIdx++]), IARG_END);
                typesList.emplace_back(IARG_ADDRINT);
                break;

            // ADDRINT that take no arguments
            case IARG_INST_PTR:
            case IARG_MEMORYREAD_EA:
            case IARG_MEMORYREAD2_EA:
            case IARG_MEMORYWRITE_EA:
            case IARG_MEMORYREAD_PTR:
            case IARG_MEMORYREAD2_PTR:
            case IARG_MEMORYWRITE_PTR:
            case IARG_MEMORYOP_PTR:
            case IARG_EXPLICIT_MEMORY_EA:
            case IARG_BRANCH_TARGET_ADDR:
            case IARG_FALLTHROUGH_ADDR:
            case IARG_SYSCALL_NUMBER:
            case IARG_SYSRET_VALUE:
            case IARG_SYSRET_ERRNO:
            case IARG_FUNCRET_EXITPOINT_VALUE:
            case IARG_RETURN_IP:
            case IARG_THREAD_ID:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, IARG_END);
                typesList.emplace_back(IARG_ADDRINT);
                break;

            // REFERENCE arg that is only for PIN (not for analysis routine)
            case IARG_PROTOTYPE:
            {
                auto protoObj = py_cast< Glue_Proto* >(vm, pyVariadicArgs[varIdx++]);
                IARGLIST_AddArguments(pinVariadicArgs, iargType, protoObj->proto, IARG_END);
                break;
            }
            case IARG_PRESERVE:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, py_cast< VOID* >(vm, pyVariadicArgs[varIdx++]), IARG_END);
                break;

            // REFERENCE (pointer type) - additional arg required
            case IARG_PTR:
            case IARG_REG_REFERENCE:
            case IARG_REG_CONST_REFERENCE:
            case IARG_SYSARG_REFERENCE:
            case IARG_FUNCARG_ENTRYPOINT_REFERENCE:
            case IARG_MULTI_ELEMENT_OPERAND:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, py_cast< VOID* >(vm, pyVariadicArgs[varIdx++]), IARG_END);
                typesList.emplace_back(IARG_PTR);
                break;

            // REFERENCE (pointer type) - additional arg required
            case IARG_ORIG_FUNCPTR:
            case IARG_FUNCRET_EXITPOINT_REFERENCE:
            case IARG_MULTI_MEMORYACCESS_EA:
            case IARG_CONTEXT:
            case IARG_PARTIAL_CONTEXT:
            case IARG_CONST_CONTEXT:
                IARGLIST_AddArguments(pinVariadicArgs, iargType, IARG_END);
                typesList.emplace_back(IARG_PTR);
                break;

            default:
                std::cerr << "ERROR!! IARG_TYPE is not supported in element: " << varIdx - 1 << ", for type: " << iargType
                          << std::endl;
                exit(1);
        }
    }

    return res;
}

} // namespace glue

#endif // _GLUE_H_
