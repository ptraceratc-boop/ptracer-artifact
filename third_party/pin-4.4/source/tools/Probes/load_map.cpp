/*
 * Copyright (C) 2009-2023 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
  The tool checks that page permissions in application process are not changed
  after Pin writes a probe
*/

/* ===================================================================== */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <string>

#include "pin.H"
#include <list>
#include "tool_macros.h"








/* Memory range */
struct RANGE_ATTR
{
    RANGE_ATTR(ADDRINT start, ADDRINT end, std::string attr) : _start(start), _end(end), _attr(attr) {}
    ADDRINT _start;
    ADDRINT _end;
    std::string _attr;
};

/* A list of memory ranges */
class RANGES
{
  public:
    /* Get memory attributes under the specified address */
    BOOL GetAttributes(ADDRINT addr, std::string* attr);
    VOID AddRange(ADDRINT start, ADDRINT end, std::string attr);

  private:
    std::list< RANGE_ATTR > _ranges;
};

VOID RANGES::AddRange(ADDRINT start, ADDRINT end, std::string attr) { _ranges.push_back(RANGE_ATTR(start, end, attr)); }

/* Find range and get its attriutes */
BOOL RANGES::GetAttributes(ADDRINT addr, std::string* attrStr)
{
    std::list< RANGE_ATTR >::iterator it = _ranges.begin();
    for (; it != _ranges.end(); it++)
    {
        if ((it->_start <= addr) && (it->_end > addr))
        {
            *attrStr = it->_attr;
            return TRUE;
        }
    }
    return FALSE;
}

#if defined(TARGET_LINUX)
#define MAX_NUM_OF_RANGES_PER_FILENAME 30

/* Read /proc/self/maps and fill fileMap with ranges */
VOID FillFileMap(const char* name, RANGES* fileMap)
{
    FILE* fp = fopen("/proc/self/maps", "r");
    char buff[1024];
    char attributes[MAX_NUM_OF_RANGES_PER_FILENAME][10];
    unsigned long mapl[MAX_NUM_OF_RANGES_PER_FILENAME], maph[MAX_NUM_OF_RANGES_PER_FILENAME];
    int nRange = 0;
    while (fgets(buff, 1024, fp) != NULL)
    {
        if (strstr(buff, name) != 0)
        {
            ASSERTX(nRange < MAX_NUM_OF_RANGES_PER_FILENAME);
            if (sscanf(buff, "%lx-%lx %s", &mapl[nRange], &maph[nRange], attributes[nRange]) != 3) continue;
            nRange++;
        }
    }
    fclose(fp);
    for (int i = 0; i < nRange; i++)
    {
        fileMap->AddRange(mapl[i], maph[i], attributes[i]);
    }
}
#endif

VOID ToolDoNothing() { std::cout << "Tool replacement - nothing to do" << std::endl; }

VOID ToolOne(size_t nBytes) { std::cout << "Tool replacement - print 1" << std::endl; }

BOOL PutProbeAndCheckAttributes(IMG img, const char* rtnName, AFUNPTR rtnReplacement)
{
    RTN rtn = RTN_FindByName(img, rtnName);
    if (RTN_Valid(rtn) && RTN_IsSafeForProbedReplacement(rtn))
    {
        std::string shortName      = IMG_Name(img);
        std::string::size_type pos = shortName.rfind('/');
        if (pos != std::string::npos) shortName = shortName.substr(pos + 1);

        RANGES fileMapBeforeProbe;
        FillFileMap(shortName.c_str(), &fileMapBeforeProbe);

        std::cout << "Looking at file " << shortName << std::endl;

        ADDRINT addr = RTN_Address(rtn);
        std::string origAttr;
        BOOL res = fileMapBeforeProbe.GetAttributes(addr, &origAttr);
        if (!res)
        {
            std::cerr << "Failed to read original page attributes from /proc/self/maps" << std::endl;
            std::cerr << "The bug is in the test" << std::endl;
            exit(-1);
        }

        RTN_ReplaceProbed(rtn, rtnReplacement);

        RANGES fileMapAfterProbe;
        FillFileMap(shortName.c_str(), &fileMapAfterProbe);
        std::string newAttr = "-cant-read-maps-file-";
        res            = fileMapAfterProbe.GetAttributes(addr, &newAttr);
        if (!res)
        {
            std::cerr << "Failed to read new page attributes from /proc/self/maps" << std::endl;
            std::cerr << "The bug is in the test" << std::endl;
            exit(-1);
        }
        if (newAttr != origAttr)
        {
            std::cout << "Original map was changes around address " << std::hex << addr << std::endl;
            std::cout << "Org attributes: " << origAttr << " New attributes " << newAttr << std::endl;
            exit(-1);
        }
        else
        {
            std::cout << "Original map was preserved around address " << std::hex << addr << std::endl;
            std::cout << "Attributes: " << newAttr << std::endl;
        }
        return TRUE;
    }
    return FALSE;
}

VOID ImageLoad(IMG img, VOID* arg)
{
    UINT32* numOfInstrumentedRtnsPtr = (UINT32*)arg;
    if (PutProbeAndCheckAttributes(img, C_MANGLE("do_nothing"), (AFUNPTR)ToolDoNothing))
    {
        (*numOfInstrumentedRtnsPtr)++;
        std::cout << std::dec << *numOfInstrumentedRtnsPtr << " routines were instrumented" << std::endl;
    }

    if (PutProbeAndCheckAttributes(img, C_MANGLE("one"), (AFUNPTR)ToolOne))
    {
        (*numOfInstrumentedRtnsPtr)++;
        std::cout << std::dec << *numOfInstrumentedRtnsPtr << " routines were instrumented" << std::endl;
    }
}

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);
    PIN_InitSymbols();

    UINT32 numOfInstrumentedRtns = 0;
    IMG_AddInstrumentFunction(ImageLoad, (VOID*)&numOfInstrumentedRtns);

    // Never returns
    PIN_StartProgramProbed();

    return 0;
}
