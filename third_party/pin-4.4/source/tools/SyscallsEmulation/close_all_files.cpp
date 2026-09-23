/*
 * Copyright (C) 2016-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
* This file together with application and makefile checks that Pin guards the file descriptors opened by itself and the
* tool (including pin.log), and doesn't let the application to close them.
* The tool also checks that standard file descriptors (0-2) cannot be return by PinCRT (for example for open())
*/
#include <stdio.h>
#include <unistd.h>
#include <tool_macros.h>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include "pin.H"




typedef VOID (*EXITFUNCPTR)(INT code);

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "close_all_files.out", "specify trace file name");

KNOB< std::string > KnobForkedChildOutputFile(KNOB_MODE_WRITEONCE, "pintool", "f", "afterForkInChild_tool.log",
                                              "forked child tool log");

KNOB< BOOL > KnobToolProbeMode(KNOB_MODE_WRITEONCE, "pintool", "probe", "0", "invoke tool in probe mode");

FILE* outc;
std::ofstream outcpp;
int my_pipe[2];
EXITFUNCPTR origExit;

VOID Fini(INT32 code, VOID* v)
{
    const char* my_str = "MyString";
    char buf[16];
    int res;
    res = (int)write(my_pipe[1], my_str, strlen(my_str));
    ASSERTX(res == (int)strlen(my_str));
    res = close(my_pipe[1]);
    ASSERTX(res == 0);
    res = (int)read(my_pipe[0], buf, sizeof(buf));
    ASSERTX(res == (int)strlen(my_str));
    res = close(my_pipe[0]);
    ASSERTX(res == 0);
    fprintf(outc, "C Success!\n");
    fclose(outc);
    outcpp << "C++ Success!" << std::endl;
    outcpp.close();
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    PIN_ERROR("This tool practices writing to file descriptors in Fini" + KNOB_BASE::StringKnobSummary() + "\n");
    return 1;
}

VOID ExitInProbeMode(INT code)
{
    Fini(code, 0);
    (*origExit)(code);
}

/* ===================================================================== */

VOID ImageLoad(IMG img, VOID* v)
{
    RTN exitRtn = RTN_FindByName(img, C_MANGLE("_exit"));
    if (RTN_Valid(exitRtn) && RTN_IsSafeForProbedReplacement(exitRtn))
    {
        origExit = (EXITFUNCPTR)RTN_ReplaceProbed(exitRtn, AFUNPTR(ExitInProbeMode));
    }
}

void AfterForkInChild()
{
    std::string forked_child_output = KnobForkedChildOutputFile.Value();
    ADDRINT flags              = (O_CREAT | O_WRONLY | O_APPEND | O_TRUNC);

    // Closing STD input and output file descriptors.
    // We want to check that next open() of a file on the system will succeed and when writing to this fd
    // it will be printed to the file itself and not to STD.
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    int fd = open(forked_child_output.c_str(), flags, 00600);
    if (fd >= 0)
    {
        const char* my_str = "success\n";
        int res;
        res = (int)write(fd, my_str, strlen(my_str));
        ASSERTX(res == (int)strlen(my_str));
    }
    assert(fd >= 0);
    // Print to stderr to check that PIN stderr fd is still open and print to console
    // and it's not closed by app closeStdFDs().
    std::cerr << "STDERR: AfterForkInChild::hello" << std::endl;
    close(fd);
}

void AfterForkInChildJit(THREADID threadid, const CONTEXT* ctxt, VOID* v) { AfterForkInChild(); }
void AfterForkInChildProbed(UINT32 childPid, void* data) { AfterForkInChild(); }

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv)) return Usage();
    PIN_InitSymbols();

    std::string c_output   = KnobOutputFile.Value() + "_for_c";
    std::string cpp_output = KnobOutputFile.Value() + "_for_cpp";

    outc = fopen(c_output.c_str(), "w");
    ASSERTX(NULL != outc);

    outcpp.open(cpp_output.c_str(), std::ofstream::out);
    ASSERTX(outcpp.good());

    int res = pipe(my_pipe);
    ASSERTX(res == 0);

    // Never returns
    if (KnobToolProbeMode)
    {
        IMG_AddInstrumentFunction(ImageLoad, 0);
        PIN_AddForkFunctionProbed(FPOINT_AFTER_IN_CHILD, AfterForkInChildProbed, 0);
        PIN_StartProgramProbed();
    }
    else
    {
        PIN_AddForkFunction(FPOINT_AFTER_IN_CHILD, AfterForkInChildJit, 0);
        PIN_AddFiniFunction(Fini, 0);
        PIN_StartProgram();
    }

    return 1;
}
