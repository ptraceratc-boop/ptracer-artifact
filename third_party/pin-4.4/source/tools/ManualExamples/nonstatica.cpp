/*
 * Copyright (C) 2005-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// At Image load time, look at all INSs of all RTNs, check that the IPs of the INSs are not duplicated in the RTN
//

#include <stdio.h>
#include <iomanip>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "pin.H"






typedef struct
{
    ADDRINT start;
    ADDRINT end;
} RTN_INTERNAL_RANGE;

std::vector< RTN_INTERNAL_RANGE > rtnInternalRangeList;

// Pin calls this function every time a new img is loaded

VOID ImageLoad(IMG img, VOID* v)
{
    std::cout << "Img: " << IMG_Name(img) << std::endl;
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            std::cout << "  Rtn: " << std::setw(8) << std::hex << RTN_Address(rtn) << " " << RTN_Name(rtn) << std::endl;
            std::string path;
            INT32 line;
            PIN_GetSourceLocation(RTN_Address(rtn), NULL, &line, &path);

            if (path != "")
            {
                std::cout << "File " << path << " Line " << line << std::endl;
            }

            // Prepare for processing of RTN, an  RTN is not broken up into BBLs,
            // it is merely a sequence of INSs
            RTN_Open(rtn);

            if (!INS_Valid(RTN_InsHead(rtn)))
            {
                RTN_Close(rtn);
                continue;
            }
            RTN_INTERNAL_RANGE rtnInternalRange;
            rtnInternalRange.start = INS_Address(RTN_InsHead(rtn));
            rtnInternalRange.end   = INS_Address(RTN_InsHead(rtn)) + INS_Size(RTN_InsHead(rtn));
            INS lastIns            = INS_Invalid();
            for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins))
            {
                std::cout << "    " << std::setw(8) << std::hex << INS_Address(ins) << " " << INS_Disassemble(ins) << std::endl;
                if (INS_Valid(lastIns))
                {
                    if ((INS_Address(lastIns) + INS_Size(lastIns)) == INS_Address(ins))
                    {
                        rtnInternalRange.end = INS_Address(ins) + INS_Size(ins);
                    }
                    else
                    {
                        std::cout << "  rtnInternalRangeList.push_back " << std::setw(8) << std::hex << rtnInternalRange.start << " "
                                  << std::setw(8) << std::hex << rtnInternalRange.end << std::endl;
                        rtnInternalRangeList.push_back(rtnInternalRange);
                        // make sure this ins has not already appeared in this RTN
                        for (std::vector< RTN_INTERNAL_RANGE >::iterator ri = rtnInternalRangeList.begin();
                             ri != rtnInternalRangeList.end(); ri++)
                        {
                            if ((INS_Address(ins) >= ri->start) && (INS_Address(ins) < ri->end))
                            {
                                std::cout << "***Error - above instruction already appeared in this RTN\n";
                                std::cout << "  in rtnInternalRangeList " << std::setw(8) << std::hex << ri->start << " " << std::setw(8) << std::hex
                                          << ri->end << std::endl;
                                exit(1);
                            }
                        }
                        rtnInternalRange.start = INS_Address(ins);
                        rtnInternalRange.end   = INS_Address(ins) + INS_Size(ins);
                    }
                }
                lastIns = ins;
            }

            // to preserve space, release data associated with RTN after we have processed it
            RTN_Close(rtn);
            rtnInternalRangeList.clear();
        }
    }
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char* argv[])
{
    // prepare for image instrumentation mode
    PIN_InitSymbols();

    // Initialize pin
    if (PIN_Init(argc, argv)) return 1;

    // Register ImageLoad to be called when an image is loaded
    IMG_AddInstrumentFunction(ImageLoad, 0);

    // Start the program, never returns
    PIN_StartProgram();

    return 0;
}
