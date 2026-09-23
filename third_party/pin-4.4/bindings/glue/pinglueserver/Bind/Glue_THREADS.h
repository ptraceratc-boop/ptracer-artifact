/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_THREADS_H_
#define _GLUE_THREADS_H_

#include "../gluepy.h"

namespace glue
{
enum class E_THREAD_Function
{
    PIN_StopApplicationThreads,
    PIN_IsThreadStoppedInDebugger,
    PIN_ResumeApplicationThreads,
    PIN_GetStoppedThreadCount,
    PIN_GetStoppedThreadId,
    PIN_GetStoppedThreadContext,
    PIN_GetStoppedThreadWriteableContext,
    PIN_GetTid,
    PIN_ThreadId,
    PIN_GetParentTid,
    PIN_ExitThread,
    PIN_IsApplicationThread
};

template< E_THREAD_Function Function > inline PyObject* thread_function(VM* vm, ArgsView args)
{
    switch (Function)
    {
        case E_THREAD_Function::PIN_StopApplicationThreads:
        {
            // PIN_StopApplicationThreads acquire the VM lock and we don't
            // want to deadlock
            Scoped_GIL_temp_release tempUnlock;
            auto res = PIN_StopApplicationThreads(py_cast< THREADID >(vm, args[0]));
            return py_var(vm, res);
        }
        case E_THREAD_Function::PIN_IsThreadStoppedInDebugger:
            return py_var(vm, PIN_IsThreadStoppedInDebugger(py_cast< THREADID >(vm, args[0])));
        case E_THREAD_Function::PIN_ResumeApplicationThreads:
        {
            // PIN_ResumeApplicationThreads acquire the VM lock and we don't
            // want to deadlock
            Scoped_GIL_temp_release tempUnlock;
            PIN_ResumeApplicationThreads(py_cast< THREADID >(vm, args[0]));
            return vm->None;
        }
        case E_THREAD_Function::PIN_GetStoppedThreadCount:
            return py_var(vm, PIN_GetStoppedThreadCount());
        case E_THREAD_Function::PIN_GetStoppedThreadId:
            return py_var(vm, PIN_GetStoppedThreadId(py_cast< UINT32 >(vm, args[0])));
        case E_THREAD_Function::PIN_GetStoppedThreadContext:
            return py_var(vm, PIN_GetStoppedThreadContext(py_cast< THREADID >(vm, args[0])));
        case E_THREAD_Function::PIN_GetStoppedThreadWriteableContext:
            return py_var(vm, PIN_GetStoppedThreadWriteableContext(py_cast< THREADID >(vm, args[0])));
        case E_THREAD_Function::PIN_GetTid:
            return py_var(vm, PIN_GetTid());
        case E_THREAD_Function::PIN_ThreadId:
            return py_var(vm, PIN_ThreadId());
        case E_THREAD_Function::PIN_GetParentTid:
            return py_var(vm, PIN_GetParentTid());
        case E_THREAD_Function::PIN_ExitThread:
        {
            // Release the lock before exiting the thread
            Pinglue_GIL::unlock();
            PIN_ExitThread(py_cast< INT32 >(vm, args[0]));
            return vm->None; // SHould not get here
        }
        case E_THREAD_Function::PIN_IsApplicationThread:
            return py_var(vm, PIN_IsApplicationThread());

        default:
            return nullptr;
    }
    }

    inline void threads_bind_all(VM* vm, PyObject* module)
    {
        vm->bind(module, "PIN_StopApplicationThreads(tid)", thread_function< E_THREAD_Function::PIN_StopApplicationThreads >);
        vm->bind(module, "PIN_IsThreadStoppedInDebugger(tid)",
                 thread_function< E_THREAD_Function::PIN_IsThreadStoppedInDebugger >);
        vm->bind(module, "PIN_ResumeApplicationThreads(tid)", thread_function< E_THREAD_Function::PIN_ResumeApplicationThreads >);
        vm->bind(module, "PIN_GetStoppedThreadCount()", thread_function< E_THREAD_Function::PIN_GetStoppedThreadCount >);
        vm->bind(module, "PIN_GetStoppedThreadId(i)", thread_function< E_THREAD_Function::PIN_GetStoppedThreadId >);
        vm->bind(module, "PIN_GetStoppedThreadContext(tid)", thread_function< E_THREAD_Function::PIN_GetStoppedThreadContext >);
        vm->bind(module, "PIN_GetStoppedThreadWriteableContext(tid)",
                 thread_function< E_THREAD_Function::PIN_GetStoppedThreadWriteableContext >);
        vm->bind(module, "PIN_GetTid()", thread_function< E_THREAD_Function::PIN_GetTid >);
        vm->bind(module, "PIN_ThreadId()", thread_function< E_THREAD_Function::PIN_ThreadId >);
        vm->bind(module, "PIN_GetParentTid()", thread_function< E_THREAD_Function::PIN_GetParentTid >);
        vm->bind(module, "PIN_ExitThread(exitCode)", thread_function< E_THREAD_Function::PIN_ExitThread >);
        vm->bind(module, "PIN_IsApplicationThread()", thread_function< E_THREAD_Function::PIN_IsApplicationThread >);
    }

} // namespace glue

#endif // _GLUE_THREADS_H_
