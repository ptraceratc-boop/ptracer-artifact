/*
 * Copyright (C) 2024-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  This is a PIN tool that exercises PIN_SpawnInternalThread() at two possible points:
 *  1. before StartProgram() call and 2. After StartProgram() call and when the main
 *  executable entry is reached.
 */

#include "pin.H"
#include <iostream>
#include <fstream>





KNOB< BOOL > LateThread(KNOB_MODE_WRITEONCE, "pintool", "late", "0",
                        "Late internal thread creation, if value 1 then after StartProgram");
BOOL tool_thread_visited = FALSE;
PIN_THREAD_UID global_uid;

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    std::cerr << "This tool spawns an internal thread at different stages of the session." << std::endl << std::endl;

    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;

    return -1;
}

static VOID tool_thread(VOID* arg)
{
    tool_thread_visited = TRUE;
    std::cout << "in tool_thread\n";
    LOG("in tool_thread\n");
}

/*!
 * Print out analysis results.
 * This function is called when the application exits.
 * @param[in]   code            exit code of the application
 * @param[in]   v               value specified by the tool in the 
 *                              PIN_AddFiniFunction function call
 */
VOID Fini(INT32 code, VOID* v)
{
    if (tool_thread_visited)
    {
        std::cout << "OK: tool_thread visited\n";
        LOG("OK: tool_thread visited\n");

        INT32 rc;
        BOOL isdone = PIN_WaitForThreadTermination(global_uid, 1000, &rc);

        if (isdone)
        {
            std::cout << "SUCCESS: tool_thread terminated properly\n";
            LOG("SUCCESS: tool_thread terminated properly\n");
        }
        else
        {
            std::cout << "ERROR: tool_thread NOT terminated properly\n";
            LOG("ERROR: tool_thread NOT terminated properly\n");
            PIN_ExitProcess(1);
        }
    }
    else
    {
        std::cout << "ERROR: tool_thread NOT visited\n";
        LOG("ERROR: tool_thread NOT visited\n");
        PIN_ExitProcess(1);
    }
}

VOID notify_main()
{
    std::cout << "in notify_main\n";

    // Later thread creation mode
    if (LateThread.Value())
    {
        THREADID intTid = PIN_SpawnInternalThread(tool_thread, NULL, 0, &global_uid);
        ASSERT(intTid != INVALID_THREADID, "Fail to spawn internal thread");
    }
}

static VOID notify_image_load(IMG img, VOID*)
{
    if (!IMG_IsMainExecutable(img)) return;

    std::cout << "in notify_image_load: main executable\n";

    RTN rtn = RTN_FindByAddress(IMG_EntryAddress(img));

    if (!RTN_Valid(rtn))
    {
        std::cout << "Executable entry not found \n";
        return;
    }

    std::cout << "Executable entry found \n";

    RTN_Open(rtn);
    RTN_InsertCall(rtn, IPOINT_BEFORE, MAKE_AFUNPTR(notify_main), IARG_END);
    RTN_Close(rtn);
}

/*!
 * The main procedure of the tool.
 * This function is called when the application image is loaded but not yet started.
 * @param[in]   argc            total number of elements in the argv array
 * @param[in]   argv            array of command line arguments, 
 *                              including pin -t <toolname> -- ...
 */
int main(int argc, char* argv[])
{
    // Initialize PIN library. Print help message if -h(elp) is specified
    // in the command line or the command line is invalid
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    PIN_InitSymbols();

    if (LateThread.Value())
    {
        std::cout << "Late thread creation mode\n";
    }
    else
    {
        std::cout << "Early thread creation mode\n";
    }

    // Register function to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    IMG_AddInstrumentFunction(notify_image_load, 0);

    // early thread creation mode
    if (!LateThread.Value())
    {
        PIN_SpawnInternalThread(tool_thread, NULL, 0, &global_uid);
    }

    // Start the program, never returns
    LOG("Calling PIN_StartProgram\n");
    PIN_StartProgram();

    return 0;
}
