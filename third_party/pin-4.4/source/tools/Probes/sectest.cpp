/*
 * Copyright (C) 2010-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include <stdint.h>

#include <iostream>
#include <fstream>
#include <map>
#include <vector>

#include "pin.H"









std::ofstream outfile;

bool FindAddressInImage(IMG img, ADDRINT addr)
{
    for (UINT32 i = 0; i < IMG_NumRegions(img); i++)
    {
        if ((addr >= IMG_RegionLowAddress(img, i)) && (addr <= IMG_RegionHighAddress(img, i)))
        {
            return true;
        }
    }
    return false;
}

void InstImage(IMG img, void* v)
{
    outfile << "-----------" << std::endl << "Image name = " << IMG_Name(img) << std::endl << std::flush;
    ADDRINT mappedStart = IMG_StartAddress(img);
    ADDRINT mappedEnd   = mappedStart + IMG_SizeMapped(img);

    outfile << std::hex << std::showbase;
    outfile << "mapped start " << mappedStart << " mapped end " << mappedEnd << std::endl;

    for (UINT32 i = 0; i < IMG_NumRegions(img); i++)
    {
        outfile << "Region #" << i << ": low addr " << IMG_RegionLowAddress(img, i) << " high address "
                << IMG_RegionHighAddress(img, i) << std::endl;
    }

    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        ADDRINT secAddr = SEC_Address(sec);
        if (!SEC_Size(sec) && (secAddr == (IMG_HighAddress(img) + 1)))
        {
            continue;
        }
        if (secAddr && SEC_Mapped(sec))
        {
            if (!FindAddressInImage(img, secAddr))
            {
                std::cout << "ERROR: Image " << IMG_Name(img) << "Section " << SEC_Name(sec) << " address wrong ptr: " << secAddr
                     << std::endl;
                PIN_ExitProcess(-1);
            }
        }
        outfile << "Section \"";
        outfile.width(30);
        outfile << std::left << SEC_Name(sec) << "\"";
        outfile.width(0);
        outfile << " addr ptr " << secAddr << std::endl;
    }
}

/*!
 *  Print out help message.
 */
INT32 Usage()
{
    std::cerr << "Pintool Usage. " << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;

    return -1;
}

int main(int argc, char** argv)
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    outfile.open("sectest.out");

    IMG_AddInstrumentFunction(InstImage, 0);

    PIN_StartProgramProbed();
    return 0;
}
