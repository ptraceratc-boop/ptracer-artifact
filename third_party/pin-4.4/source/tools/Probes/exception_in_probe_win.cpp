/*
 * Copyright (C) 2023-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
 * This pintool replaces a routine (RtlLeaveCriticalSection) that causes exception in probed bytes.
 * In addition it generates exceptions inside the pintool (access violation and stack overflow) and catches them.
 * The test validates that exceptions thrown from the application (from LeaveCriticalSection) are passed properly to the application.
 */

#include <stdio.h>
#include <string>
#include <fstream>
#include <iostream>
#include "pin.H"

#include <windows/pinrt_windows.h>

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name");
std::ostream* Out = NULL;

// convert a givennull-terminated std::string into lowercase.
static char* str_to_lower(char* str)
{
    if (!str) return NULL;
    for (int i = 0; str[i]; i++)
    {
        str[i] = tolower((unsigned char)str[i]);
    }
    return str;
}

static KNOB< BOOL > KnobReplaceSignature(KNOB_MODE_WRITEONCE, "pintool", "replace_sig", "0",
                                         "Use RTN_ReplaceSignatureProbed instead of RTN_ReplaceProbed");

// Signature of RtlLeaveCriticalSection
typedef VOID(WINAPI* rtl_leave_critical_section_call_t)(__inout WINDOWS::LPCRITICAL_SECTION lpCriticalSection);

// Address of original RtlLeaveCriticalSection
void (*g_RtlLeaveCriticalSection_ptr)(void);

int (*g__resetstkoflw_ptr)();

/*
 * This original function is all the time being optimized by clang
 * which goes around the stack violation. Therefore the following
 * coding which enforces clang to generate code that violates
 * stack access
 */
__declspec(noinline) int GenerateStackOverflow()
{
    volatile int arr[0x10000000];
    for (int i = 0; i < 0x1000000; i += 0x10)
    {
        arr[i] = i;
        arr[0x10000000 - 1] += i;
    }

    return arr[0x10000000 - 1];
}

int* g_ptr;

/*
 * Same with access violation: clang detects and generates ud
 * to go around we need to hide the NULL ptr value
 */
void do_access_violation() { *g_ptr = 17; }

namespace WINDOWS
{
int exception_filter(int exception_code)
{
    if (exception_code == EXCEPTION_STACK_OVERFLOW)
    {
        *Out << "Stack Overflow exception (EXCEPTION_STACK_OVERFLOW) caught in 'RtlLeaveCriticalSection' replacement routine"
             << std::endl;
    }
    if (exception_code == EXCEPTION_ACCESS_VIOLATION)
    {
        *Out << "Access Violation exception (EXCEPTION_ACCESS_VIOLATION) caught in 'RtlLeaveCriticalSection' replacement routine"
             << std::endl;
    }
    *Out << "Exception code = " << std::hex << exception_code << std::endl;
    return EXCEPTION_EXECUTE_HANDLER;
}

STATIC VOID GenerateAndCatchExceptions()
{
    // Generate an access violation exception and catch it
    __try
    {
        do_access_violation();
    }
    __except (exception_filter(GetExceptionCode()))
    {}

    // Generate a stack overflow exception and catch it
    __try
    {
        volatile int i = GenerateStackOverflow();
    }
    __except (exception_filter(GetExceptionCode()))
    {
        g__resetstkoflw_ptr();
    }
}
} // namespace WINDOWS

VOID WINAPI RtlLeaveCriticalSection_rep(__inout WINDOWS::LPCRITICAL_SECTION lpCriticalSection)
{
    if (lpCriticalSection == NULL)
    {
        WINDOWS::GenerateAndCatchExceptions();
    }
    // Call the original fuction (Will also generate an exception)
    return (*(rtl_leave_critical_section_call_t)g_RtlLeaveCriticalSection_ptr)(lpCriticalSection);
}

VOID WINAPI RtlLeaveCriticalSection_repsig(rtl_leave_critical_section_call_t orig_RtlLeaveCriticalSection,
                                           WINDOWS::LPCRITICAL_SECTION lpCriticalSection, ADDRINT returnIp)
{
    if (lpCriticalSection == NULL)
    {
        WINDOWS::GenerateAndCatchExceptions();

        orig_RtlLeaveCriticalSection(lpCriticalSection);

        // Will not execute if exception occurs in previous statement
        *Out << "Caller IP = " << std::hex << returnIp << std::endl << std::flush;
    }
    else
    {
        orig_RtlLeaveCriticalSection(lpCriticalSection);
    }
}

static const char* extract_mod_name_with_ext(const char* full)
{
    const char* slash = NULL;
    char* module_name = NULL;

    slash = strrchr(full, '\\');

    if (slash)
    {
        module_name = str_to_lower(strdup(++slash));
    }
    else
    {
        module_name = str_to_lower(strdup(full));
    }

    return module_name;
}

static VOID instrument_module(IMG img, VOID* data)
{
    const char* module_name = extract_mod_name_with_ext(IMG_Name(img).c_str());

    if (strcmp(module_name, "ntdll.dll") == 0)
    {
        RTN routine = RTN_FindByName(img, "RtlLeaveCriticalSection");
        ASSERTX(RTN_Valid(routine));
        if (KnobReplaceSignature)
        {
            PROTO leave_proto =
                PROTO_Allocate(PIN_PARG(void), CALLINGSTD_STDCALL, "RtlLeaveCriticalSection", PIN_PARG(void*), PIN_PARG_END());
            g_RtlLeaveCriticalSection_ptr =
                RTN_ReplaceSignatureProbed(routine, (AFUNPTR)(RtlLeaveCriticalSection_repsig), // analysis routine
                                           IARG_PROTOTYPE, leave_proto,                        // prototype
                                           IARG_ORIG_FUNCPTR,                                  // address of replaced function
                                           IARG_FUNCARG_ENTRYPOINT_VALUE, 0,                   // arg 0
                                           IARG_RETURN_IP,                                     // return address in caller
                                           IARG_END);
        }
        else
        {
            g_RtlLeaveCriticalSection_ptr = RTN_ReplaceProbed(routine, (AFUNPTR)(RtlLeaveCriticalSection_rep));
        }
        ASSERTX(g_RtlLeaveCriticalSection_ptr != NULL);
    }
    else if (0 == strcmp(module_name, "msvcrt.dll") || 0 == strcmp(module_name, "ucrtbase.dll") ||
             0 == strcmp(module_name, "msvcrtd.dll") || 0 == strcmp(module_name, "ucrtbased.dll"))
    {
        RTN routine = RTN_FindByName(img, "_resetstkoflw");
        ASSERTX(RTN_Valid(routine));
        g__resetstkoflw_ptr = (int (*)())RTN_Address(routine);
        ASSERTX(g__resetstkoflw_ptr != NULL);
    }
    free((void*)(module_name));
}

static VOID on_module_loading(IMG img, VOID* data)
{
    unsigned long origAttrs = 0;

    if (IMG_Valid(img))
    {
        if (!IMG_IsMainExecutable(img))
        {
            instrument_module(img, data);
        }
    }
}

int main(int argc, char** argv)
{
    PIN_InitSymbolsAlt(EXPORT_SYMBOLS);
    g_ptr = NULL;

    if (!PIN_Init(argc, argv))
    {
        Out = KnobOutputFile.Value().empty() ? &std::cout : new std::ofstream(KnobOutputFile.Value().c_str());
        *Out << std::hex;
        Out->setf(std::ios::showbase);
        IMG_AddInstrumentFunction(on_module_loading, 0);
        PIN_StartProgramProbed();
    }

    exit(1);
}
