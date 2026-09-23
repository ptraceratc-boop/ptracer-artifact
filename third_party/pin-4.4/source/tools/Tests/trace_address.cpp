/*
 * Copyright (C) 2018-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*!@ file
 * Testing TRACE_Address ( ) API
 * Finds tstfunc1,tstfunc2,tstfunc3 by name and makes sure their addresses match a trace address
 *
 * Test app prints addresses of function pointers, of these 3 functions. Comparison is done in make rule.
 *
 * This test tests TRACE_Address only for traces at the beginning of a function.
 * It does not check traces that begin in the middle of a function.
 */

#include <cassert>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <map>
#include <unistd.h>
#include <cstdlib>
#include "pin.H"









//=========================================================
//Global Variables:

//This map holds all Routine Addresses of routines that include "tstfunc" in their name,
//and a boolean value stating if it was reached by Trace_Address.
std::map< ADDRINT, bool > rtnAdds;

std::ostream* TraceFile = NULL;

KNOB< std::string > KnobOutputFile(
    KNOB_MODE_WRITEONCE, "pintool", "o", "",
    "Specify file name for the tool's output. If no filename is specified, the output will be directed to stdout.");

//=========================================================
//instrumentation functions:

VOID Image(IMG img, VOID* v)
{
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            if (std::string::npos != RTN_Name(rtn).find("tstfunc"))
            {
                rtnAdds.insert(std::pair< ADDRINT, bool >(RTN_Address(rtn), false));
            }
        }
    }
}

VOID InstrumentTrace(TRACE trace, VOID* v)
{
    const ADDRINT addr = TRACE_Address(trace);
    assert(BBL_Address(TRACE_BblHead(trace)) == addr);
    if (rtnAdds.find(addr) != rtnAdds.end())
    {
        rtnAdds.find(addr)->second = true;
    }
}

VOID Fini(INT32 code, VOID* v)
{
    for (std::map< ADDRINT, bool >::iterator it = rtnAdds.begin(); it != rtnAdds.end(); it++)
    {
        assert(it->second == true);
        *TraceFile << it->first << "\t";
    }

    if (&std::cout != TraceFile)
    {
        delete TraceFile;
    }
}

int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    TraceFile = KnobOutputFile.Value().empty() ? &std::cerr : new std::ofstream(KnobOutputFile.Value().c_str());
    *TraceFile << std::hex;
    TraceFile->setf(std::ios::showbase);
    TraceFile->setf(std::ios::uppercase);

    IMG_AddInstrumentFunction(Image, 0);

    TRACE_AddInstrumentFunction(InstrumentTrace, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    //Should never reach this point
    return 1;
}
