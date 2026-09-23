/*
 * Copyright (C) 2010-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  An example of Windows application that raises exception and uses NtContinue to recover.
 */
#include <windows.h>

static void MyExceptionReturn()
{
    // Test success.
    // Call stack is not valid since this entry point is reached via NtContinue, so exit(0) is not safe.
    TerminateProcess(GetCurrentProcess(), 0);
}

static CONTEXT winCtxt;
/*!
 * Exception filter. 
 */
static int MyExceptionFilter(LPEXCEPTION_POINTERS exceptPtr)
{
    winCtxt = *(exceptPtr->ContextRecord);
#ifdef TARGET_IA32
    winCtxt.Eip          = reinterpret_cast< uintptr_t >(MyExceptionReturn);
    winCtxt.ContextFlags = CONTEXT_FULL | CONTEXT_EXTENDED_REGISTERS;
#else
    winCtxt.Rip          = reinterpret_cast< uintptr_t >(MyExceptionReturn);
    winCtxt.ContextFlags = CONTEXT_FULL;
#endif

    HMODULE hNtdll = GetModuleHandle("ntdll.dll");
    typedef LONG (__stdcall * NtContinue_t)(PCONTEXT contextRecord, BOOLEAN testAlert);
    auto NtContinue = (NtContinue_t)GetProcAddress(hNtdll, "NtContinue");
    if (nullptr == NtContinue)
    {
        exit(4);
    }

    // Validate NtContinue with null pointer to context.
    NTSTATUS status = NtContinue(NULL, FALSE); // non-alertable
    // Should return with !NT_SUCCESS(status)
    if (0 <= status) // NT_SUCCESS.
    {
        exit(3);
    }

    // Validate NtContinue with regular context.
    NtContinue(&winCtxt, FALSE); // non-alertable
    // Should continue execution at MyExceptionReturn with the state set to winCtxt
    exit(2);
}

/*!
 * The main procedure of the application.
 */
int main(int argc, char* argv[])
{
    __try
    {
        char* p = 0;
        p++;
        *p = 0; // Should cause ACCESS_VIOLATION exception.
    }
    __except (MyExceptionFilter(GetExceptionInformation()))
    {
    }

    // Should not reach this point.
    exit(1);
}
