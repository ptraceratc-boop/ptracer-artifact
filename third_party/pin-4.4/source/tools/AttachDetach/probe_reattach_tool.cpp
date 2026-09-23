/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
KNOB< UINT32 > KnobDetachAfter(KNOB_MODE_WRITEONCE, "pintool", "detach_after", "1",
                               "seconds to wait after application start before detaching");

KNOB< UINT32 > KnobReattachAfter(KNOB_MODE_WRITEONCE, "pintool", "reattach_after", "1",
                                 "seconds to wait after detach completion before reattaching");

KNOB< UINT32 > KnobIterations(KNOB_MODE_WRITEONCE, "pintool", "iterations", "1",
                              "number of detach/reattach iterations to perform");

KNOB< std::string > KnobProbesFile(KNOB_MODE_WRITEONCE, "pintool", "probes", "",
                                   "file that lists one routine name per line to probe");

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "output file for test verdict");

std::set< std::string > g_requestedProbeSet;
uint32_t g_probesFoundInSession  = 0;
uint32_t g_probesPlacedInSession = 0;

PIN_LOCK g_logLock;

std::mutex g_stateMutex;
std::condition_variable g_stateCv;

bool g_appStarted      = false;
bool g_detachCompleted = false;
bool g_attachCompleted = false;
bool g_shutdown        = false;

std::atomic< bool > g_toolExited(false);

std::ofstream g_outFile;

VOID ProbeBefore() {}

INT32 Usage()
{
    std::cerr << "Probe mode detach/reattach tool\n\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

VOID Log(const std::string& message)
{
    PIN_GetLock(&g_logLock, PIN_GetTid());
    std::ostream& out = g_outFile.is_open() ? static_cast< std::ostream& >(g_outFile) : std::cerr;
    out << message << std::endl;
    PIN_ReleaseLock(&g_logLock);
}

std::string Trim(const std::string& line)
{
    const std::string whitespace(" \t\r\n");
    const size_t first = line.find_first_not_of(whitespace);
    if (first == std::string::npos)
    {
        return std::string();
    }

    const size_t last = line.find_last_not_of(whitespace);
    return line.substr(first, last - first + 1);
}

bool LoadRequestedProbes()
{
    if (KnobProbesFile.Value().empty())
    {
        std::cerr << "Missing -probes <filename> knob" << std::endl;
        return false;
    }

    std::ifstream probesFile(KnobProbesFile.Value().c_str());
    if (!probesFile)
    {
        std::cerr << "Failed to open probes file: " << KnobProbesFile.Value() << std::endl;
        return false;
    }

    std::string line;
    while (std::getline(probesFile, line))
    {
        std::string probeName = Trim(line);
        if (probeName.empty() || probeName[0] == '#')
        {
            continue;
        }

        g_requestedProbeSet.insert(probeName);
    }

    return true;
}

VOID ResetSessionState(UINT32 nextSessionId)
{
    std::lock_guard< std::mutex > lock(g_stateMutex);
    g_appStarted            = false;
    g_detachCompleted       = false;
    g_attachCompleted       = false;
    g_probesFoundInSession  = 0;
    g_probesPlacedInSession = 0;
}

bool AllRequestedProbesPlaced()
{
    std::lock_guard< std::mutex > lock(g_stateMutex);
    return g_probesFoundInSession >= g_requestedProbeSet.size() && g_probesFoundInSession == g_probesPlacedInSession;
}

VOID TryPlaceProbe(const std::string& imgName, const std::string& probeName, RTN rtn)
{
    std::lock_guard< std::mutex > lock(g_stateMutex);
    g_probesFoundInSession++;
    if (!RTN_IsSafeForProbedInsertion(rtn))
    {
        Log("Routine is not safe for probed insertion: " + probeName + " in image: " + imgName);
        return;
    }

    if (!RTN_InsertCallProbed(rtn, IPOINT_BEFORE, AFUNPTR(ProbeBefore), IARG_END))
    {
        Log("Failed to place probe on routine: " + probeName + " in image: " + imgName);
        return;
    }

    g_probesPlacedInSession++;

    Log("Placed probe on routine: " + probeName + " in image: " + imgName);
}

VOID ImageLoad(IMG img, VOID* v)
{
    auto& imgName = IMG_Name(img);
    Log("Loaded image: " + imgName);
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            const std::string& rtnName = RTN_Name(rtn);
            // On 32-bit Windows, extern "C" __cdecl symbols are prefixed with '_'.
            // Strip it so the name matches the platform-neutral probes file.
            const std::string* lookupName = &rtnName;
            std::string stripped;
            if (rtnName.size() > 1 && rtnName[0] == '_')
            {
                stripped   = rtnName.substr(1);
                lookupName = &stripped;
            }
            if (g_requestedProbeSet.find(*lookupName) != g_requestedProbeSet.end())
            {
                TryPlaceProbe(imgName, *lookupName, rtn);
            }
        }
    }
}

VOID OnApplicationStart(VOID* v)
{
    UINT32 sessionId = static_cast< UINT32 >(reinterpret_cast< ADDRINT >(v));

    {
        std::lock_guard< std::mutex > lock(g_stateMutex);
        g_appStarted = true;
    }
    g_stateCv.notify_all();

    Log(sessionId == 1 ? "Application Start callback received" : "Attach Complete callback received via Application Start");
}

VOID OnDetachComplete(VOID* v)
{
    {
        std::lock_guard< std::mutex > lock(g_stateMutex);
        g_detachCompleted = true;
    }
    g_stateCv.notify_all();
    Log("Detach Complete callback received");
}

VOID RegisterCallbacks(VOID* sessionArg)
{
    IMG_AddInstrumentFunction(ImageLoad, 0);
    PIN_AddApplicationStartFunction(OnApplicationStart, sessionArg);
    PIN_AddDetachFunctionProbed(OnDetachComplete, 0);
}

VOID AttachMain(VOID* arg)
{
    {
        std::lock_guard< std::mutex > lock(g_stateMutex);
        g_attachCompleted = true;
    }
    g_stateCv.notify_all();
    RegisterCallbacks(arg);
}

bool WaitForState(bool& state, const char* description)
{
    std::unique_lock< std::mutex > lock(g_stateMutex);
    g_stateCv.wait(lock, [&]() { return state || g_shutdown; });
    if (g_shutdown)
    {
        Log(std::string("Stopping while waiting for ") + description);
        return false;
    }
    return true;
}

VOID RequestShutdown(INT32 exitCode, const std::string& message)
{
    {
        std::lock_guard< std::mutex > lock(g_stateMutex);
        g_shutdown = true;
    }
    g_stateCv.notify_all();
    Log(message);
    g_toolExited.store(true);
    PIN_ExitProcess(exitCode);
}

VOID ControlThread(VOID* arg)
{
    for (UINT32 iteration = 1; iteration <= KnobIterations.Value(); ++iteration)
    {
        if (!WaitForState(g_appStarted, "application start"))
        {
            return;
        }
        if (!AllRequestedProbesPlaced())
        {
            RequestShutdown(1, "Missing requested probes:");
            return;
        }

        std::this_thread::sleep_for(std::chrono::seconds(KnobDetachAfter.Value()));
        Log("Requesting detach");
        PIN_DetachProbed();

        if (!WaitForState(g_detachCompleted, "detach complete"))
        {
            return;
        }

        std::this_thread::sleep_for(std::chrono::seconds(KnobReattachAfter.Value()));

        const UINT32 nextSessionId = iteration + 1;
        ResetSessionState(nextSessionId);
        Log("Requesting attach");
        PIN_AttachProbed(AttachMain, reinterpret_cast< VOID* >(static_cast< ADDRINT >(nextSessionId)));

        if (!WaitForState(g_attachCompleted, "attach complete"))
        {
            return;
        }
    }

    Log("PASSED - All probes placed in all iterations");
    RequestShutdown(0, "Completed requested detach/reattach iterations");
}
} // namespace

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    if (!LoadRequestedProbes())
    {
        return Usage();
    }

    PIN_InitLock(&g_logLock);

    if (!KnobOutputFile.Value().empty())
    {
        g_outFile.open(KnobOutputFile.Value().c_str());
    }

    RegisterCallbacks(reinterpret_cast< VOID* >(static_cast< ADDRINT >(1)));

    THREADID tid = PIN_SpawnInternalThread(ControlThread, 0, 0, 0);
    if (tid == INVALID_THREADID)
    {
        std::cerr << "Failed to spawn control thread" << std::endl;
        return 1;
    }

    PIN_StartProgramProbed();
    return 0;
}
