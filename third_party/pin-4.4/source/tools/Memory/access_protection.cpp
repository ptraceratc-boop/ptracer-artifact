/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
  * This pintool validates the accessibility APIs : 
  *     PIN_CheckReadAccess, PIN_CheckReadAccessEx, PIN_CheckWriteAccess, PIN_CheckWriteAccessEx
  * It validates allocations done either by the application and captured in OnNotifyPinAfterMmap
  * or allocations done by the pintool.
  */
#include "pin.H"

// #include <sys/mman.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

static VOID VerifyException(EXCEPTION_INFO& exceptInfo, void* addr, bool is_mapped, bool has_access)
{
    printf("%p %s\n", addr, exceptInfo.ToString().c_str());

    EXCEPTION_CLASS expectedClass = EXCEPTCLASS_NONE;
    EXCEPTION_CODE expectedCode   = EXCEPTCODE_NONE;
    if (!has_access)
    {
        expectedClass = EXCEPTCLASS_ACCESS_FAULT;

#ifdef TARGET_LINUX
        expectedCode = is_mapped ? EXCEPTCODE_ACCESS_DENIED : EXCEPTCODE_ACCESS_INVALID_ADDRESS;
#else
        expectedCode = EXCEPTCODE_RECEIVED_ACCESS_FAULT;
#endif
    }

    ASSERT(expectedCode == exceptInfo.GetExceptCode(), hexstr(addr) + " Unexpected EXCEPTION_CODE " + exceptInfo.ToString());
    ASSERT(expectedClass == exceptInfo.GetExceptClass(), hexstr(addr) + " Unexpected EXCEPTION_CLASS " + exceptInfo.ToString());

    if (EXCEPTCLASS_NONE != exceptInfo.GetExceptClass())
    {
        ADDRINT accessAddress = 0;
        ASSERTX(TRUE == exceptInfo.GetFaultyAccessAddress(&accessAddress));
        ASSERT((ADDRINT)addr == accessAddress, hexstr(addr) + " Unexpected Faulty Access Address " + exceptInfo.ToString());
    }
}

static VOID VerifyAccessibility(void* addr, bool is_mapped, bool access_read, bool access_write)
{
    printf("Verify Accessibility %p mapped %d read %d write %d \n", addr, (int)is_mapped, (int)access_read, (int)access_write);
    EXCEPTION_INFO exceptInfo;
    ASSERTX(nullptr != addr);
    ASSERTX(access_read == PIN_CheckReadAccess(addr));
    ASSERTX(access_write == PIN_CheckWriteAccess(addr));
    ASSERTX(access_read == PIN_CheckReadAccessEx(addr, &exceptInfo));
    VerifyException(exceptInfo, addr, is_mapped, access_read);
    ASSERTX(access_write == PIN_CheckWriteAccessEx(addr, &exceptInfo));
    VerifyException(exceptInfo, addr, is_mapped, access_write);
}

static VOID OnNotifyPinAfterMmap(void* addr, bool is_mapped, bool access_read, bool access_write)
{
    VerifyAccessibility(addr, is_mapped, access_read, access_write);
}

static VOID OnTrace()
{
    static BOOL tested = TRUE;
    if (!tested)
    {
        tested = true;

        constexpr bool prot_read  = true;
        constexpr bool prot_write = true;
        constexpr bool is_mapped  = true;

        // PROT_NONE
        void* addr = mmap(NULL, getpagesize(), PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
        VerifyAccessibility(addr, is_mapped, !prot_read, !prot_write);

        // PROT_READ | PROT_WRITE
        addr = mmap(NULL, getpagesize(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        VerifyAccessibility(addr, is_mapped, prot_read, prot_write);

        // PROT_READ
        addr = mmap(NULL, getpagesize(), PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
        VerifyAccessibility(addr, is_mapped, prot_read, !prot_write);

        // PROT_WRITE
        addr = mmap(NULL, getpagesize(), PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        VerifyAccessibility(addr, is_mapped, !prot_read, prot_write);
    }
}

static VOID Trace(TRACE trace, VOID* v) { TRACE_InsertCall(trace, IPOINT_BEFORE, (AFUNPTR)OnTrace, IARG_END); }

VOID Image(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        RTN rtn = RTN_FindByName(img, "_NotifyPinAfterMmap");
        if (!RTN_Valid(rtn))
        {
            rtn = RTN_FindByName(img, "NotifyPinAfterMmap");
        }
        ASSERT(RTN_Valid(rtn), "Failed to find _NotifyPinAfterMmap/NotifyPinAfterMmap in " + IMG_Name(img));

        RTN_Open(rtn);
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR)OnNotifyPinAfterMmap, IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                       IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
                       IARG_END);
        RTN_Close(rtn);
    }
}

int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        fprintf(stderr, "PIN_Init failed \n");
        return 1;
    }
    IMG_AddInstrumentFunction(Image, 0);
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_StartProgram();
    return 0;
}
