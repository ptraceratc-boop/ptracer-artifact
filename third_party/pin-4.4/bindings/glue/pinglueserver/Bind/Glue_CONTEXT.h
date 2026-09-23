/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_CONTEXT_H_
#define _GLUE_CONTEXT_H_

#include "../gluepy.h"

namespace glue
{
enum class E_CONTEXT_Function
{
    PIN_SupportsProcessorState,
    PIN_ContextContainsState,
    PIN_SetContextRegval,
    PIN_GetContextRegval,
    PIN_SetContextReg,
    PIN_GetContextReg,
    PIN_SetContextFPState,
    PIN_GetContextFPState,
    PIN_SetContextFXSave,
    PIN_GetContextFXSave,
    //PIN_GetFullContextRegsSet,
    PIN_SaveContext,
    //PIN_ExecuteAt,
    PIN_GetInitialContextForUnwind,
    PIN_Backtrace,

    // PhysicalContext
    PIN_SetPhysicalContextReg,
    PIN_GetPhysicalContextReg,
    PIN_SetPhysicalContextFPState,
    PIN_GetPhysicalContextFPState
};

template< E_CONTEXT_Function Function > inline PyObject* context_function(VM* vm, ArgsView args)
{
    switch (Function)
    {
        case E_CONTEXT_Function::PIN_SupportsProcessorState:
            return py_var(vm, PIN_SupportsProcessorState(py_cast< PROCESSOR_STATE >(vm, args[0])));
        case E_CONTEXT_Function::PIN_ContextContainsState:
            return py_var(vm,
                          PIN_ContextContainsState(py_cast< CONTEXT* >(vm, args[0]), py_cast< PROCESSOR_STATE >(vm, args[1])));
        case E_CONTEXT_Function::PIN_SetContextRegval:
            PIN_SetContextRegval(py_cast< CONTEXT* >(vm, args[0]), py_cast< REG >(vm, args[1]),
                                 py_cast< const UINT8* >(vm, args[2]));
            return vm->None;
        case E_CONTEXT_Function::PIN_GetContextRegval:
            PIN_GetContextRegval(py_cast< const CONTEXT* >(vm, args[0]), py_cast< REG >(vm, args[1]),
                                 py_cast< UINT8* >(vm, args[2]));
            return vm->None;
        case E_CONTEXT_Function::PIN_SetContextReg:
            PIN_SetContextReg(py_cast< CONTEXT* >(vm, args[0]), py_cast< REG >(vm, args[1]), py_cast< ADDRINT >(vm, args[2]));
        case E_CONTEXT_Function::PIN_GetContextReg:
            return py_var(vm, PIN_GetContextReg(py_cast< const CONTEXT* >(vm, args[0]), py_cast< REG >(vm, args[1])));
        case E_CONTEXT_Function::PIN_SetContextFPState:
            PIN_SetContextFPState(py_cast< CONTEXT* >(vm, args[0]), py_cast< const FPSTATE* >(vm, args[1]));
            return vm->None;
        case E_CONTEXT_Function::PIN_GetContextFPState:
            PIN_GetContextFPState(py_cast< CONTEXT* >(vm, args[0]), py_cast< FPSTATE* >(vm, args[1]));
            return vm->None;
        case E_CONTEXT_Function::PIN_SetContextFXSave:
            PIN_SetContextFXSave(py_cast< CONTEXT* >(vm, args[0]), py_cast< const FXSAVE* >(vm, args[1]));
            return vm->None;
        case E_CONTEXT_Function::PIN_GetContextFXSave:
            PIN_GetContextFXSave(py_cast< const CONTEXT* >(vm, args[0]), py_cast< FXSAVE* >(vm, args[1]));
            return vm->None;
        // case E_CONTEXT_Function::PIN_GetFullContextRegsSet:
        //     return py_var(vm, PIN_GetFullContextRegsSet());
        // TODO decide if/how to bind REGSET
        case E_CONTEXT_Function::PIN_SaveContext:
            PIN_SaveContext(py_cast< const CONTEXT* >(vm, args[0]), py_cast< CONTEXT* >(vm, args[1]));
            return vm->None;
        // TODO PINT-6156: since the following API is never return we have to implement it with exception
        // that will be catch in the analysis routine that will call to PIN_ExecuteAt
        // case E_CONTEXT_Function::PIN_ExecuteAt:
        //     PIN_UnlockClient();
        //     PIN_ExecuteAt(py_cast< const CONTEXT* >(vm, args[0]));
        //     // This API never returns, the following is not needed
        //     PIN_LockClient();
        //     return vm->None;
        case E_CONTEXT_Function::PIN_GetInitialContextForUnwind:
            return py_var(vm,
                          PIN_GetInitialContextForUnwind(py_cast< const CONTEXT* >(vm, args[0]), py_cast< void* >(vm, args[1])));
        case E_CONTEXT_Function::PIN_Backtrace:
        {
            auto ret = PIN_Backtrace(py_cast< const CONTEXT* >(vm, args[0]), py_cast< void** >(vm, args[1]),
                                     py_cast< int >(vm, args[2]));

            return py_var(vm, ret);
        }
        case E_CONTEXT_Function::PIN_SetPhysicalContextReg:
            PIN_SetPhysicalContextReg(py_cast< PHYSICAL_CONTEXT* >(vm, args[0]), py_cast< REG >(vm, args[1]),
                                      py_cast< ADDRINT >(vm, args[2]));
            return vm->None;
        case E_CONTEXT_Function::PIN_GetPhysicalContextReg:
            return py_var(vm, PIN_GetPhysicalContextReg(py_cast< PHYSICAL_CONTEXT* >(vm, args[0]), py_cast< REG >(vm, args[1])));
        case E_CONTEXT_Function::PIN_SetPhysicalContextFPState:
            PIN_SetPhysicalContextFPState(py_cast< PHYSICAL_CONTEXT* >(vm, args[0]), py_cast< const VOID* >(vm, args[1]));
            return vm->None;
        case E_CONTEXT_Function::PIN_GetPhysicalContextFPState:
            PIN_GetPhysicalContextFPState(py_cast< const PHYSICAL_CONTEXT* >(vm, args[0]), py_cast< VOID* >(vm, args[1]));
            return vm->None;

        default:
            return nullptr;
    }
    }

    inline void context_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "PIN_SupportsProcessorState(state)", context_function< E_CONTEXT_Function::PIN_SupportsProcessorState >);
        vm->bind(module, "PIN_ContextContainsState(ctxt, state)",
                 context_function< E_CONTEXT_Function::PIN_ContextContainsState >);
        vm->bind(module, "PIN_SetContextRegval(ctxt, reg, val)", context_function< E_CONTEXT_Function::PIN_SetContextRegval >);
        vm->bind(module, "PIN_GetContextRegval(ctxt, reg, val)", context_function< E_CONTEXT_Function::PIN_GetContextRegval >);
        vm->bind(module, "PIN_SetContextReg(ctxt, reg, val)", context_function< E_CONTEXT_Function::PIN_SetContextReg >);
        vm->bind(module, "PIN_GetContextReg(ctxt, reg)", context_function< E_CONTEXT_Function::PIN_GetContextReg >);
        vm->bind(module, "PIN_SetContextFPState(ctxt, fpstate)", context_function< E_CONTEXT_Function::PIN_SetContextFPState >);
        vm->bind(module, "PIN_GetContextFPState(ctxt, fpstate)", context_function< E_CONTEXT_Function::PIN_GetContextFPState >);
        vm->bind(module, "PIN_SetContextFXSave(ctxt, fxsave)", context_function< E_CONTEXT_Function::PIN_SetContextFXSave >);
        vm->bind(module, "PIN_GetContextFXSave(ctxt, fxsave)", context_function< E_CONTEXT_Function::PIN_GetContextFXSave >);
        //vm->bind(module, "PIN_GetFullContextRegsSet()", context_function< E_CONTEXT_Function::PIN_GetFullContextRegsSet >);
        vm->bind(module, "PIN_SaveContext(ctxtFrom, ctxtTo)", context_function< E_CONTEXT_Function::PIN_SaveContext >);
        //vm->bind(module, "PIN_ExecuteAt(ctxt)", context_function< E_CONTEXT_Function::PIN_ExecuteAt >);
        vm->bind(module, "PIN_GetInitialContextForUnwind(ctxt, cursor)",
                 context_function< E_CONTEXT_Function::PIN_GetInitialContextForUnwind >);
        vm->bind(module, "PIN_Backtrace(ctxt, buffer, size)", context_function< E_CONTEXT_Function::PIN_Backtrace >);

        vm->bind(module, "PIN_SetPhysicalContextReg(pPhysCtxt, reg, val)",
                 context_function< E_CONTEXT_Function::PIN_SetPhysicalContextReg >);
        vm->bind(module, "PIN_GetPhysicalContextReg(pPhysCtxt, reg)",
                 context_function< E_CONTEXT_Function::PIN_GetPhysicalContextReg >);
        vm->bind(module, "PIN_SetPhysicalContextFPState(pPhysCtxt, fpstate)",
                 context_function< E_CONTEXT_Function::PIN_SetPhysicalContextFPState >);
        vm->bind(module, "PIN_GetPhysicalContextFPState(pPhysCtxt, fpstate)",
                 context_function< E_CONTEXT_Function::PIN_GetPhysicalContextFPState >);

        // PROCESSOR_STATE
        module->attr().set("PROCESSOR_STATE_X87", py_var(vm, PROCESSOR_STATE_X87));
        module->attr().set("PROCESSOR_STATE_XMM", py_var(vm, PROCESSOR_STATE_XMM));
        module->attr().set("PROCESSOR_STATE_YMM", py_var(vm, PROCESSOR_STATE_YMM));
        module->attr().set("PROCESSOR_STATE_ZMM", py_var(vm, PROCESSOR_STATE_ZMM));
        module->attr().set("PROCESSOR_STATE_TMM", py_var(vm, PROCESSOR_STATE_TMM));
    }

} // namespace glue

#endif // _GLUE_CONTEXT_H_