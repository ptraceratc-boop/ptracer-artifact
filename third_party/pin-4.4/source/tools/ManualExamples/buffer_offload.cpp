/*
 * Copyright (C) 2024-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <cstddef>
#include <unistd.h>
#include "pin.H"
#include "buffer_offload.h"

/*
 * This pintool demonstrates Pin 4.x new RPC (Remote Procedure Call) mechanism which allows Pintools to offload
 * processing to a remote process.
 * This pintool collects information about memory accesses in a trace buffer, and when the buffer 
 * gets full transmits the buffer to a remote function for further processing.
 * The pintool is responsible for the instrumentation and data collection. The remote  function implemented as part of a
 * Pin server plugin (buffer_offload_plugin.cpp), is responsible for analyzing the collected data. The schema for the
 * RPC is shared between the Pintool and the plugin and is located in buffer_offload.h.
 */

// A knob for setting the name of the file into which the remote plugin will write the analysis report
KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "memory_analysis.log", "output file");

// The id of the trace buffer assigned by Pin
BUFFER_ID buffeId = 0;

/**
 * @brief Transmit an open file request the remote plugin
 */
static bool open_out_file(const std::string& outFileName)
{
    bool fileOpened = false;
    return remote::do_rpc< OPEN_OUT_FILE_SCHEMA >(fileOpened, outFileName) && fileOpened;
}
/**
 * @brief Transmit a trace buffer when it's full to the remote plugin
 */
static bool mem_analyze(VOID* buffer, uint32_t bufferSize)
{
    return remote::do_rpc< MEM_ANALYZE_SCHEMA, void >(remote::rpc_buffer(buffer, bufferSize));
}
/**
 * @brief This function is called before the application exits, and it returns the lowest and highest
 * addresses accessed by the application from the remote plugin. It demonstrates the use of OUT parameters.
 */
static bool get_mem_access_info(ADDRINT& addrMin, ADDRINT& addrMax, unsigned& topRangesCount, MEMREF* topRanges)
{
    bool ret = false;
    if (remote::do_rpc< GET_MEM_ACCESS_INFO_SCHEMA >(
            ret, addrMin, addrMax, topRangesCount,
            remote::rpc_buffer(topRanges, topRangesCount * sizeof(MEMREF), RpcArgFlagsDataEmpty)))
    {
        return ret;
    }
    return false;
}

/**
 * @brief Initialize the remote plugin log file
 * 
 */
static void InitializeRemoteLogger()
{
    std::filesystem::path outfile = KnobOutputFile.Value();
    if (outfile.is_relative())
    {
        outfile = std::filesystem::current_path() / outfile;
    }

#if (TARGET_WINDOWS)
    // The Windows LSC implementation for ::getcwd (Source/pinrt/pinos/lsc/support/windows/syscallimp/getcwd.cpp)
    // prefixes the path with '/' - intentionally. However std::ofstream::open is not happy with the '/' so we remove it.
    std::string fullpath = outfile.string();
    if (0 == fullpath.find("/"))
    {
        outfile = fullpath.substr(1, fullpath.size() - 1);
    }
#endif

    std::cout << "Report will be written to " << outfile << std::endl;
    ASSERTX(open_out_file(outfile.string()));
}

/*
 * This function is called by Pin when the trace buffer gets full.
 * In this function we do not process the trace buffer but rather transmit it to the remote
 * plugin for further processing.
 */
static VOID* BufferFull(BUFFER_ID id, THREADID tid, const CONTEXT* ctxt, VOID* buffer, UINT64 numElements, VOID* v)
{
    ASSERTX(mem_analyze(buffer, numElements * sizeof(MEMREF)));
    return buffer;
}

VOID Fini(INT32 code, VOID* v)
{
    ADDRINT addrMin = 0, addrMax = 0;
    MEMREF topRanges[6] {};
    unsigned topRangesCount = sizeof(topRanges) / sizeof(MEMREF);
    ASSERTX(get_mem_access_info(addrMin, addrMax, topRangesCount, topRanges));

    std::cout << "Lowest address accessed 0x" << std::hex << addrMin << " ; Highest address accessed 0x" << std::hex << addrMax
              << std::endl;
    std::cout << "Largest " << topRangesCount << " ranges are:" << std::endl;
    for (unsigned i = 0; i < topRangesCount; ++i)
    {
        std::cout << '\t' << "Base: 0x" << std::hex << uintptr_t(topRanges[i].ea) << std::dec << " Size: " << topRanges[i].size
                  << " bytes" << std::endl;
    }
}

VOID Trace(TRACE trace, VOID* v)
{
    for (BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl))
    {
        for (INS ins = BBL_InsHead(bbl); INS_Valid(ins); ins = INS_Next(ins))
        {
            UINT32 memoryOperands = INS_MemoryOperandCount(ins);

            for (UINT32 memOp = 0; memOp < memoryOperands; memOp++)
            {
                UINT32 numBytesAccessed = INS_MemoryOperandSize(ins, memOp);
                INS_InsertFillBuffer(ins,                                           // The application instruction
                                     IPOINT_BEFORE,                                 // before the instruction executes
                                     buffeId,                                       // The id of the buffer whose record is filled
                                     IARG_MEMORYOP_EA, memOp, offsetof(MEMREF, ea), // effective address
                                     IARG_UINT32, numBytesAccessed, offsetof(MEMREF, size), // number of bytes read/written
                                     IARG_END);
            }
        }
    }
}

INT32 Usage()
{
    std::cerr
        << "This tool demonstrates offloading analysis work to a remote process. "
        << "Instead of doing the processing in the analysis routine we send the data using an RPC message to the remote process"
        << std::endl;
    std::cerr << std::endl << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    // There is a physical limit for transmitting buffers to the remote process - 65535 bytes.
    // However we need to leave some room for RSC-RPC Message headers
    // We should limit the trace buffer size to not exceed that limit since we want to transmit the full buffer.
    const size_t bufferSizeLimit = PIN_CalculateSafeRPCDataSize(&MEM_ANALYZE_SCHEMA);
    const size_t pageSize        = getpagesize();
    ASSERTX(bufferSizeLimit >= pageSize);
    auto numPages = bufferSizeLimit / pageSize;
    buffeId       = PIN_DefineTraceBuffer(sizeof(MEMREF), numPages, BufferFull, 0);

    if (buffeId == BUFFER_ID_INVALID)
    {
        std::cerr << "Error: could not allocate initial buffer" << std::endl;
        return 1;
    }

    InitializeRemoteLogger();
    TRACE_AddInstrumentFunction(Trace, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();

    return 0;
}
