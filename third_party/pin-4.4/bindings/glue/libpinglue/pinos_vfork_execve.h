/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef ___H__
#define __PINOS_VFORK_EXECVE_H__

#include <pinos.h>
#include <pbits/syscall.h>

#define AT_FDCWD (-100)
#define SIGCHLD 17

struct startupData
{
    void* startupInfo;
    int argc;
    char const** argv;
    char const** envp;
};

struct Clone_args
{
    t_syscall_arg dirFd = t_syscall_arg(AT_FDCWD);
    std::string childProcessPath;
    const char** childArgv = nullptr;
    const char** childEnvp = nullptr;
    bool execveFailed      = false;
};

inline t_syscall_ret pinos_clone_vfork(int (*cloneProc)(Clone_args*), Clone_args* procState, uint8_t* stack, size_t stackSize)
{
    /* We need to pass some additional information to pinos.
     * We will use the stack to pass it. PINOS will remove it once it's read.
     * We will write the following information:
     * stack_limit:  uintptr_t
     * stack_base:   uintptr_t
     * commit_limit: uintptr_t
     * args_stack:   uintptr_t*/
    uint8_t* argsStack    = stack + stackSize;
    uint8_t* alignedStack = argsStack - ((uintptr_t)argsStack % sizeof(uintptr_t)); // Align to uintptr_t
    alignedStack -= sizeof(uintptr_t) * 4;
    ((uintptr_t*)alignedStack)[0] = (uintptr_t)stack;
    ((uintptr_t*)alignedStack)[1] = (uintptr_t)argsStack;
    ((uintptr_t*)alignedStack)[2] = (uintptr_t)stack;
    ((uintptr_t*)alignedStack)[3] = (uintptr_t)argsStack;

    t_syscall_arg cloneArgs[] = {
        t_syscall_arg(cloneProc),
        t_syscall_arg(procState),
        t_syscall_arg(CLONE_VM | CLONE_VFORK | SIGCHLD),
        t_syscall_arg(alignedStack),
        t_syscall_arg(0), // If specified recieves the new thread id in the parent
        t_syscall_arg(0), // Will be set to 0 on thread exit
        t_syscall_arg(0), // tls for thread - we don't care when using CLONE_VFORK
    };

    // pinos requires that the thread calls clone is exist in the pinos thread list.
    OS_Syscall(SYS_link_native_thread, 0, nullptr);

    return OS_Syscall(SYS_clone, SYSCALL_ARG_COUNT(cloneArgs), cloneArgs);
}

int pinos_execveat(Clone_args* clone_args)
{
    t_syscall_arg execveArgs[] {clone_args->dirFd, t_syscall_arg(clone_args->childProcessPath.c_str()),
                                t_syscall_arg(clone_args->childArgv), t_syscall_arg(clone_args->childEnvp), t_syscall_arg(0)};
    return OS_Syscall(SYS_execveat, SYSCALL_ARG_COUNT(execveArgs), execveArgs);

    // If we get here execve failed
    clone_args->execveFailed = true;

    return -5;
}

#endif // __PINOS_VFORK_EXECVE_H__
