/*
 * Copyright (C) 2007-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <string>
#include <iostream>

#include <windows/pinrt_windows.h>

//=======================================================================
// This is a tool that instruments the GetLastError() function and changes
// TEB.LastErrorValue. This change should be seen in the application.
//
// Also this tool uses very big automatic variables in order to verify that
// pin handles stack bounds correctly (TEB is not affected).
//=======================================================================

// Address of the GetLastError API.
ADDRINT pfnGetLastError = 0;

typedef UINT64 COUNTER;
const UINT32 MAX_INDEX = 8000;
const UINT32 VECT_SIZE = 4;

struct CSTATS
{
    CSTATS() { memset(counters, 0, sizeof(COUNTER) * MAX_INDEX); }
    COUNTER counters[MAX_INDEX];
};

// very big static object initialized before main()
std::vector< CSTATS > MyGlobalVect(VECT_SIZE);

//=======================================================================
//
VOID InGetLastError()
{
    static BOOL first = TRUE;
    if (first)
    {
        std::cout << "In GetLastError" << std::endl;
        // test very big variables on stack
        first = FALSE;
        std::vector< CSTATS > myVect(VECT_SIZE);
        myVect[0].counters[0] = 1;
        MyGlobalVect          = myVect;

        CSTATS myStat;
        myStat.counters[0] = 2;
        MyGlobalVect[1]    = myStat;
    }
    if (777 == WINDOWS::GetLastError())
    {
        // Change TEB.LastErrorValue to something different from 777
        // Since Pin does not modify nor restore the TEB, this will cause
        // the application to see a different value than 777.
        // Pin cannot guarantee proper isolation from the application if Win32 APIs are used.
        // Pintools are discouraged from using Win32 APIs directly
        WINDOWS::SetLastError(999);
    }
}

//=======================================================================
// This function is called for every instruction and instruments the
// GetLastError() function
VOID Instruction(INS ins, VOID* v)
{
    if (INS_Address(ins) == pfnGetLastError)
    {
        INS_InsertCall(ins, IPOINT_BEFORE, MAKE_AFUNPTR(InGetLastError), IARG_END);
    }
}

//=======================================================================
int main(int argc, CHAR* argv[])
{
    pfnGetLastError = (ADDRINT)WINDOWS::GetProcAddress(WINDOWS::GetModuleHandle("kernel32.dll"), "GetLastError");
    PIN_Init(argc, argv);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_StartProgram();

    return 0;
}
