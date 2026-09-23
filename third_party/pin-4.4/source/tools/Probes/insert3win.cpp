/*
 * Copyright (C) 2008-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  This is an example of probing RtlAllocateHeap
 */

#include "pin.H"
#include <windows/pinrt_windows.h>
#include <iostream>







VOID Before_RtlAllocateHeap0(WINDOWS::PVOID hHeap, WINDOWS::ULONG dwFlags, WINDOWS::SIZE_T dwBytes)
{
    std::cout << "Before_RtlAllocateHeap0: RtlAllocateHeap( " << std::hex << (ADDRINT)hHeap << ", " << dwFlags << ", " << dwBytes << ")"
         << std::dec << std::endl
         << std::flush;
}

VOID Before_RtlAllocateHeap1(WINDOWS::PVOID hHeap, WINDOWS::ULONG dwFlags, WINDOWS::SIZE_T dwBytes)
{
    std::cout << "Before_RtlAllocateHeap1: RtlAllocateHeap( " << std::hex << (ADDRINT)hHeap << ", " << dwFlags << ", " << dwBytes << ")"
         << std::dec << std::endl
         << std::flush;
}

VOID Before_Free0(WINDOWS::PVOID ptr) { std::cout << "Before_Free0: Free " << std::hex << (ADDRINT)ptr << std::dec << std::endl << std::flush; }

VOID Before_Free1(WINDOWS::PVOID ptr) { std::cout << "Before_Free1: Free " << std::hex << (ADDRINT)ptr << std::dec << std::endl << std::flush; }

/*
 * process_loaded_image: Called every time when new image is loaded.
 */
static VOID process_loaded_image(IMG image, VOID* value)
{
    if (!IMG_Valid(image)) return;

    RTN allocRtn = RTN_FindByName(image, "RtlAllocateHeap");
    if (RTN_Valid(allocRtn) && RTN_IsSafeForProbedInsertion(allocRtn))
    {
        std::cout << "RTN_InsertCallProbed on "
             << "RtlAllocateHeap" << std::endl
             << std::flush;

        if (RTN_InsertCallProbed(allocRtn, IPOINT_BEFORE, AFUNPTR(Before_RtlAllocateHeap0), IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                                 IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_END) &&
            RTN_InsertCallProbed(allocRtn, IPOINT_BEFORE, AFUNPTR(Before_RtlAllocateHeap1), IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
                                 IARG_FUNCARG_ENTRYPOINT_VALUE, 1, IARG_FUNCARG_ENTRYPOINT_VALUE, 2, IARG_END))
        {
        }
        else
        {
        }
    }

    RTN freeRtn = RTN_FindByName(image, "free");

    if (RTN_Valid(freeRtn) && RTN_IsSafeForProbedInsertion(freeRtn))
    {
        std::cout << "RTN_InsertCallProbed on "
             << "Free" << std::endl
             << std::flush;

        RTN_InsertCallProbed(freeRtn, IPOINT_BEFORE, AFUNPTR(Before_Free0), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);

        RTN_InsertCallProbed(freeRtn, IPOINT_BEFORE, AFUNPTR(Before_Free1), IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
    }
}

int main(int argc, char** argv)
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) return -1;

    IMG_AddInstrumentFunction(process_loaded_image, 0);
    PIN_StartProgramProbed();
}
