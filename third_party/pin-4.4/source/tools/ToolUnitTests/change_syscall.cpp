/*
 * Copyright (C) 2008-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * This test verifies that Pin can change system call numbers and arguments.
 * Use in conjuction with the "change_syscall.c" application.
 */

#include <signal.h>
#include <string.h>
#include "pin.H"

BOOL IsSigaction(ADDRINT sysnum)
{
#ifdef __NR_sigaction
    if (sysnum == __NR_sigaction) return TRUE;
#endif
#ifdef __NR_rt_sigaction
    if (sysnum == __NR_rt_sigaction) return TRUE;
#endif
    return FALSE;
}

VOID OnSyscallEntry(THREADID threadIndex, CONTEXT* ctxt, SYSCALL_STANDARD std, VOID* v)
{
    ADDRINT sysnum = PIN_GetSyscallNumber(ctxt, std);
    ADDRINT arg0   = PIN_GetSyscallArgument(ctxt, std, 0);

    // Extract the syscall number without the int80 mask
    sysnum = sysnum & 0xFFFF;

    char* filetoopen = NULL;
#ifdef __NR_openat
    if (sysnum == __NR_openat)
    {
        // open() is implemented using openat() in newer Bionic versions. The file is held in the second argument.
        ADDRINT arg1 = (PIN_GetSyscallArgument(ctxt, std, 1));
        filetoopen   = reinterpret_cast< char* >(arg1);
    }
#endif
    if (sysnum == __NR_open)
    {
        filetoopen = reinterpret_cast< char* >(arg0);
    }
    if (filetoopen != NULL && strncmp(filetoopen, "does-not-exist1", sizeof("does-not-exist1") - 1) == 0)
    {
        PIN_SetSyscallNumber(ctxt, std, __NR_getpid);
    }

    if (IsSigaction(sysnum) && (arg0 == SIGUSR1))
    {
        PIN_SetSyscallNumber(ctxt, std, __NR_getpid);
    }

    if (filetoopen && strncmp(filetoopen, "does-not-exist2", sizeof("does-not-exist2") - 1) == 0)
    {
        PIN_SetSyscallNumber(ctxt, std, __NR_exit);
        PIN_SetSyscallArgument(ctxt, std, 0, 0);
    }
}

int main(int argc, char* argv[])
{
    PIN_Init(argc, argv);

    PIN_AddSyscallEntryFunction(OnSyscallEntry, 0);

    PIN_StartProgram();
    return 0;
}
