/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <cstdint>
#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include "tool_macros.h"

/*
 * This Pin tool checks that a given set of memory address ranges are
 * valid application memory at runtime.
 *
 * The user provides one or more memory ranges (start:end in hex) via the
 * -restrict_memory knob. The tool then walks through every page in each
 * range and verifies that the page is mapped and belongs to the application.
 *
 * The check runs once, either when a specific function ("PinAttached") is
 * found in the application, or when the application starts - whichever
 * comes first.
*/

std::ofstream output_file;
bool memory_ranges_checked = false;

// Knob for memory ranges - can be specified multiple times
KNOB< std::string > KnobMemoryRanges(KNOB_MODE_APPEND, "pintool", "restrict_memory", "",
                                     "Memory range to check (format: start:end). Can be used multiple times.");
KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify file name for output");

// Structure to hold memory range
struct MemoryRange
{
    uintptr_t start;
    uintptr_t end;

    MemoryRange(uintptr_t s, uintptr_t e) : start(s), end(e) {}
};

std::vector< MemoryRange > memory_ranges;

int MyPinAttached() { return 1; }

void CheckMemoryRanges();

void CheckMemoryRangesOnce()
{
    if (!memory_ranges_checked)
    {
        CheckMemoryRanges();
        memory_ranges_checked = true;
    }
}

// Function to parse hex string to address
uintptr_t ParseHexAddress(const std::string& hex_str)
{
    uintptr_t addr;
    std::stringstream ss;
    std::string clean_str = hex_str;

    // Remove 0x prefix if present
    if (clean_str.substr(0, 2) == "0x")
    {
        clean_str = clean_str.substr(2);
    }

    ss << std::hex << clean_str;
    ss >> addr;
    return addr;
}

// Function to parse a single memory range
bool ParseSingleRange(const std::string& range_str)
{
    size_t colon_pos = range_str.find(':');
    if (colon_pos == std::string::npos)
    {
        output_file << "Invalid range format: " << range_str << " (expected start:end)" << std::endl;
        return false;
    }

    std::string start_str = range_str.substr(0, colon_pos);
    std::string end_str   = range_str.substr(colon_pos + 1);

    uintptr_t start_addr = ParseHexAddress(start_str);
    uintptr_t end_addr   = ParseHexAddress(end_str);

    if (start_addr >= end_addr)
    {
        output_file << "Invalid range: start >= end (0x" << std::hex << start_addr << " >= 0x" << end_addr << ")" << std::endl;
        return false;
    }

    memory_ranges.push_back(MemoryRange(start_addr, end_addr));
    return true;
}

// Function to parse all memory ranges from knob
bool ParseMemoryRanges()
{
    // Get all values from the knob (since it's KNOB_MODE_APPEND)
    for (UINT32 i = 0; i < KnobMemoryRanges.NumberOfValues(); i++)
    {
        if (!ParseSingleRange(KnobMemoryRanges.Value(i)))
        {
            return false;
        }
    }

    if (memory_ranges.empty())
    {
        output_file << "No memory ranges specified. Use -restrict_memory start:end" << std::endl;
        return false;
    }

    return true;
}

// Function to check memory ranges using Pin APIs
void CheckMemoryRanges()
{
    output_file << "\n=== Memory Range Analysis ===" << std::endl;
    ASSERTX(!memory_ranges.empty());
    for (size_t i = 0; i < memory_ranges.size(); i++)
    {
        const MemoryRange& range = memory_ranges[i];

        pinrt::t_memattr page_attributes = 0;
        // check all pages in the range
        for (uintptr_t addr = range.start; addr < range.end; addr += getpagesize())
        {
            bool query_result = pinrt::query_mem_range(reinterpret_cast< const void* >(addr), getpagesize(), &page_attributes,
                                                       true // refreshFromOs = true to ensure we see OS state (reserved ranges)
            );

            // Note FFU: After PINOS part is finished this test should also check for the restrict attribute

            ASSERTX(query_result);
            bool is_mapped = pinrt::is_page_mapped(page_attributes);
            bool is_app    = pinrt::is_page_application(page_attributes);
            // For this test, we expect all pages in the range to be mapped and application pages
            ASSERTX(is_mapped);
            ASSERTX(is_app);
        }
    }
}

// Application start callback
VOID ApplicationStart(VOID* v)
{
    CheckMemoryRangesOnce();
}

VOID ImageLoad(IMG img, void* v)
{
    RTN rtn = RTN_FindByName(img, C_MANGLE("PinAttached"));
    if (RTN_Valid(rtn))
    {
        if (PIN_IsProbeMode())
        {
            ASSERTX(RTN_IsSafeForProbedReplacement(rtn));
            RTN_ReplaceProbed(rtn, AFUNPTR(MyPinAttached));
        }
        else
        {
            RTN_Replace(rtn, AFUNPTR(MyPinAttached));
        }
        CheckMemoryRangesOnce();
    }
}

// Fini function
VOID Fini(INT32 code, VOID* v)
{
    if (memory_ranges_checked)
    {
        output_file << "\nSUCCESS!" << std::endl;
    }
    else
    {
        output_file << "\nERROR: memory ranges were not checked" << std::endl;
    }
    output_file.close();
}

// Main function
int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    // Initialize PIN
    if (PIN_Init(argc, argv)) return -1;

    //open output file
    output_file.open(KnobOutputFile.Value().c_str());
    // Parse memory ranges from knobs
    if (!ParseMemoryRanges())
    {
        return -1;
    }

    // Register callbacks
    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddApplicationStartFunction(ApplicationStart, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Start the program
    if (PIN_IsProbeMode())
    {
        PIN_StartProgramProbed();
    }
    else
    {
        PIN_StartProgram();
    }

    return 0;
}