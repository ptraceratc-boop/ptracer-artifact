/*
 * Copyright (C) 2005-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iomanip>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include "pin.H"







KNOB< std::string > KnobInputFile(KNOB_MODE_WRITEONCE, "pintool", "i", "<imagename>", "specify an image to read");

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This tool disassembles an image." << std::endl << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

typedef struct
{
    ADDRINT start;
    ADDRINT end;
} RTN_INTERNAL_RANGE;

std::vector< RTN_INTERNAL_RANGE > rtnInternalRangeList;
/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    IMG img = IMG_Open(KnobInputFile);

    if (!IMG_Valid(img))
    {
        std::cout << "Could not open " << KnobInputFile.Value() << std::endl;
        exit(1);
    }

    std::cout << std::hex;
    rtnInternalRangeList.clear();

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        std::cout << "Section: " << std::setw(8) << SEC_Address(sec) << " " << SEC_Name(sec) << std::endl;

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
                        rtnInternalRangeList.push_back(rtnInternalRange);
                        std::cout << "  rtnInternalRangeList.push_back " << std::setw(8) << std::hex << rtnInternalRange.start << " "
                                  << std::setw(8) << std::hex << rtnInternalRange.end << std::endl;
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

            RTN_Close(rtn);
            rtnInternalRangeList.clear();
        }
    }
    IMG_Close(img);
}
