/*
 * Copyright (C) 2012-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * This tool mimics the behavior of TPSS on Linux by adding probes to various libncurses functions.
 * However, in this tool these probes are merely empty wrappers that call the original functions.
 * The objective of the test is to verify that probe generation and insertion don't cause Pin
 * to crash.
 */

#include "pin.H"
#include <iostream>
#include <fstream>







std::ofstream OutFile;

/* ===================================================================== */
/* Commandline Switches                                                  */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "tpss_lin_libcurses.txt", "specify tool log file name");

/* ===================================================================== */
/* Utility functions                                                     */
/* ===================================================================== */

// Print help information
INT32 Usage()
{
    std::cerr << "This tool mimics the behavior of TPSS on Linux by adding probes to various libcurses functions." << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return 1;
}

// Utility function to return the time
std::string CurrentTime()
{
    char tmpbuf[128];
    time_t thetime = time(NULL);
    ctime_r(&thetime, tmpbuf);
    return tmpbuf;
}

/* ===================================================================== */
/* Function signatures - these functions will be probed                  */
/* ===================================================================== */

int (*fptrgetch)(void);

int (*fptrmvgetch)(int y, int x);

/* ===================================================================== */
/* Probes - implementation of the wrapper functions                      */
/* ===================================================================== */

int mygetch(void)
{
    OutFile << CurrentTime() << "mygetch called " << std::endl;
    OutFile.flush();
    int res = fptrgetch();

    return res;
}

int mymvgetch(int y, int x)
{
    OutFile << CurrentTime() << "mymvgetch called " << std::endl;
    OutFile.flush();
    int res = fptrmvgetch(y, x);

    return res;
}

/* ===================================================================== */
/* Instrumnetation functions                                             */
/* ===================================================================== */

// Image load callback - inserts the probes.
void ImgLoad(IMG img, void* v)
{
    // Called every time a new image is loaded

    if ((IMG_Name(img).find("libncurses.so") != std::string::npos) || (IMG_Name(img).find("LIBNCURSES.SO") != std::string::npos) ||
        (IMG_Name(img).find("LIBNCURSES.so") != std::string::npos))
    {
        RTN rtngetch = RTN_FindByName(img, "getch");
        if (RTN_Valid(rtngetch) && RTN_IsSafeForProbedReplacement(rtngetch))
        {
            OutFile << CurrentTime() << "Inserting probe for getch at " << RTN_Address(rtngetch) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtngetch, AFUNPTR(mygetch)));
            fptrgetch    = (int (*)())fptr;
        }

        RTN rtnmvgetch = RTN_FindByName(img, "mvgetch");
        if (RTN_Valid(rtnmvgetch) && RTN_IsSafeForProbedReplacement(rtnmvgetch))
        {
            OutFile << CurrentTime() << "Inserting probe for mvgetch at " << RTN_Address(rtnmvgetch) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnmvgetch, AFUNPTR(mymvgetch)));
            fptrmvgetch  = (int (*)(int, int))fptr;
        }
    }
    // finished instrumentation
}

/* ===================================================================== */
/* Main function                                                         */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    // Initialize Pin
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    OutFile.open(KnobOutputFile.Value().c_str());
    OutFile << std::hex;
    OutFile.setf(std::ios::showbase);
    OutFile << CurrentTime() << "started!" << std::endl;
    OutFile.flush();

    // Register the instrumentation callback
    IMG_AddInstrumentFunction(ImgLoad, 0);

    // Start the application
    PIN_StartProgramProbed(); // never returns

    return 0;
}
