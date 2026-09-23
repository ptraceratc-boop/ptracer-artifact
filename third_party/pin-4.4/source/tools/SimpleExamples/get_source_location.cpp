/*
 * Copyright (C) 2012-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * This tool demonstrates the usage of the PIN_GetSourceLocation API from an instrumentation routine. You
 * may notice that there are no analysis routines in this example.
 *
 * Note: According to the Pin User Guide, calling PIN_GetSourceLocation from an analysis routine requires
 *       that the client lock be taken first.
 *
 */

#include <iostream>
#include <fstream>
#include "pin.H"








/* ================================================================== */
// Global variables
/* ================================================================== */

/* ===================================================================== */
// Command line switches
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(
    KNOB_MODE_WRITEONCE, "pintool", "o", "",
    "specify file name for the tool's output. If no filename is specified, the output will be directed to stdout.");

/* ===================================================================== */
// Utilities
/* ===================================================================== */

// Print out help message.
INT32 Usage()
{
    std::cerr << "This tool demonstrates the usage of the PIN_GetSourceLocation API." << std::endl;
    std::cerr << std::endl << KNOB_BASE::StringKnobSummary() << std::endl;

    return -1;
}

// This is a utility function for acquiring and printing the source information.
static void output(ADDRINT address, std::ostream* printTo, INS ins = INS_Invalid())
{
    std::string filename; // This will hold the source file name.
    INT32 line = 0;  // This will hold the line number within the file.

    // In this example, we don't print the column number so there is no reason to obtain it.
    // Simply pass a NULL pointer instead. Also, acquiring the client lock is not required in
    // instrumentation functions, only in analysis functions.
    //
    PIN_GetSourceLocation(address, NULL, &line, &filename);

    // Prepare the output strings.
    std::string asmOrFuncName;
    if (INS_Valid(ins))
    {
        asmOrFuncName = INS_Disassemble(ins); // For an instruction, get the disassembly.
    }
    else
    {
        asmOrFuncName = RTN_FindNameByAddress(address); // For a routine, get its name.
    }

    // For output cleanliness, print only if source was found.
    if (!filename.empty())
    {
        *printTo << "0x" << address << " " << asmOrFuncName << " #" << filename << ":" << std::dec << line << std::endl;
    }
}

/* ===================================================================== */
// Instrumentation callbacks
/* ===================================================================== */

// IMG instrumentation routine - called once per image upon image load
static VOID ImageLoad(IMG img, VOID* v)
{
    // For simplicity, instrument only the main image. This can be extended to any other image of course.
    if (IMG_IsMainExecutable(img))
    {
        // To find all the instructions in the image, we traverse the sections of the image.
        for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
        {
            // For each section, process all RTNs.
            for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
            {
                // Many RTN APIs require that the RTN be opened first.
                RTN_Open(rtn);
                output(RTN_Address(rtn), static_cast< std::ostream* >(v)); // Calls PIN_GetSourceLocation for the RTN address.

                // Call PIN_GetSourceLocation for all the instructions of the RTN.
                for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
                {
                    output(INS_Address(ins), static_cast< std::ostream* >(v),
                           ins); // Calls PIN_GetSourceLocation for a single instruction.
                }
                RTN_Close(rtn); // Don't forget to close the RTN once you're done.
            }
        }
    }
}

// Adding fini function just to close the output file
VOID Fini(INT32 code, VOID* v)
{
    if (!KnobOutputFile.Value().empty() && v != NULL)
    {
        static_cast< std::ofstream* >(v)->close();
    }
}

/* ===================================================================== */
// main
/* ===================================================================== */

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    std::ofstream outFile;
    if (!KnobOutputFile.Value().empty())
    {
        outFile.open(KnobOutputFile.Value().c_str());
    }

    IMG_AddInstrumentFunction(ImageLoad, (KnobOutputFile.Value().empty()) ? &std::cout : &outFile);

    // Register function to be called when the application exits
    PIN_AddFiniFunction(Fini, &outFile);

    // Never returns
    PIN_StartProgram();

    return 0;
}
