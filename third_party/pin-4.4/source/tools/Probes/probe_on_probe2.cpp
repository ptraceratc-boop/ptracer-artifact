/*
 * Copyright (C) 2008-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
*/

#include "pin.H"
#include <windows/pinrt_windows.h>
#include <iostream>
#include <fstream>
#include <string>







/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */

std::ofstream TraceFile;

typedef WINDOWS::BOOL(WINAPI* CREATE_PROCESSA_FUNC)(WINDOWS::LPCTSTR, WINDOWS::LPTSTR, WINDOWS::LPSECURITY_ATTRIBUTES,
                                                    WINDOWS::LPSECURITY_ATTRIBUTES, WINDOWS::BOOL, WINDOWS::DWORD,
                                                    WINDOWS::LPVOID, WINDOWS::LPCTSTR, WINDOWS::LPSTARTUPINFOA,
                                                    WINDOWS::LPPROCESS_INFORMATION);

typedef WINDOWS::BOOL(WINAPI* CREATE_PROCESSW_FUNC)(WINDOWS::LPCWSTR, WINDOWS::LPWSTR, WINDOWS::LPSECURITY_ATTRIBUTES,
                                                    WINDOWS::LPSECURITY_ATTRIBUTES, WINDOWS::BOOL, WINDOWS::DWORD,
                                                    WINDOWS::LPVOID, WINDOWS::LPCWSTR, WINDOWS::LPSTARTUPINFOW,
                                                    WINDOWS::LPPROCESS_INFORMATION);

typedef WINDOWS::BOOL(WINAPI* CREATE_PROCESS_AS_USERA_FUNC)(WINDOWS::HANDLE, WINDOWS::LPCTSTR, WINDOWS::LPTSTR,
                                                            WINDOWS::LPSECURITY_ATTRIBUTES, WINDOWS::LPSECURITY_ATTRIBUTES,
                                                            WINDOWS::BOOL, WINDOWS::DWORD, WINDOWS::LPVOID, WINDOWS::LPCTSTR,
                                                            WINDOWS::LPSTARTUPINFOA, WINDOWS::LPPROCESS_INFORMATION);

typedef WINDOWS::BOOL(WINAPI* CREATE_PROCESS_AS_USERW_FUNC)(WINDOWS::HANDLE, WINDOWS::LPCWSTR, WINDOWS::LPWSTR,
                                                            WINDOWS::LPSECURITY_ATTRIBUTES, WINDOWS::LPSECURITY_ATTRIBUTES,
                                                            WINDOWS::BOOL, WINDOWS::DWORD, WINDOWS::LPVOID, WINDOWS::LPCWSTR,
                                                            WINDOWS::LPSTARTUPINFOW, WINDOWS::LPPROCESS_INFORMATION);
/*
 * A data about function replacement: 
 *   - name
 *   - pointer to original function
 *   - pointer to a wrapper
 */
struct FUNC_REPLACE
{
    FUNC_REPLACE(const std::string& name, VOID* origF, VOID* wrapper) : _funcName(name), _orgFuncPtr(origF), _funcWrapper(wrapper)
    {}
    FUNC_REPLACE() {}
    std::string _funcName;
    VOID* _orgFuncPtr;
    VOID* _funcWrapper;
};

enum FUNC_IDX
{
    FUNC_CreateProcessA,
    FUNC_CreateProcessW,
    FUNC_CreateProcessAsUserA,
    FUNC_CreateProcessAsUserW,
    FUNC_Last
};

class FUNC_REPLACE_CONTAINER
{
  public:
    VOID* OriginalPtr(FUNC_IDX idx) { return _array[idx]._orgFuncPtr; }
    VOID* Wrapper(FUNC_IDX idx) { return _array[idx]._funcWrapper; }
    std::string Name(FUNC_IDX idx) { return _array[idx]._funcName; }
    VOID SetOriginalPtr(FUNC_IDX idx, VOID* fptr) { _array[idx]._orgFuncPtr = fptr; }
    UINT32 Size() { return FUNC_Last; }

    FUNC_REPLACE_CONTAINER();

  private:
    FUNC_REPLACE _array[FUNC_Last];
};

FUNC_REPLACE_CONTAINER funcList;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "probe_on_probe2.outfile", "specify file name");

/* ===================================================================== */

INT32 Usage()
{
    std::cerr << "This pin tool tests probe replacement.\n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}
/*
 * A test wrapper for CreateProcessA()
 */
WINDOWS::BOOL WINAPI WrapCreateProcessA(WINDOWS::LPCTSTR lpApplicationName, WINDOWS::LPTSTR lpCommandLine,
                                        WINDOWS::LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                        WINDOWS::LPSECURITY_ATTRIBUTES lpThreadAttributes, WINDOWS::BOOL bInheritHandles,
                                        WINDOWS::DWORD dwCreationFlags, WINDOWS::LPVOID lpEnvironment,
                                        WINDOWS::LPCTSTR lpCurrentDirectory, WINDOWS::LPSTARTUPINFOA lpStartupInfo,
                                        WINDOWS::LPPROCESS_INFORMATION lpProcessInformation)
{
    TraceFile << "WrapCreateProcessA before" << std::endl;
    VOID* fptr = funcList.OriginalPtr(FUNC_CreateProcessA);
    BOOL ret   = (*((CREATE_PROCESSA_FUNC)fptr))(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
                                               bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo,
                                               lpProcessInformation);
    TraceFile << "WrapCreateProcessA after " << std::endl;
    return ret;
}

/*
 * A test wrapper for CreateProcessW()
 */
WINDOWS::BOOL WINAPI WrapCreateProcessW(WINDOWS::LPCWSTR lpApplicationName, WINDOWS::LPWSTR lpCommandLine,
                                        WINDOWS::LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                        WINDOWS::LPSECURITY_ATTRIBUTES lpThreadAttributes, WINDOWS::BOOL bInheritHandles,
                                        WINDOWS::DWORD dwCreationFlags, WINDOWS::LPVOID lpEnvironment,
                                        WINDOWS::LPCWSTR lpCurrentDirectory, WINDOWS::LPSTARTUPINFOW lpStartupInfo,
                                        WINDOWS::LPPROCESS_INFORMATION lpProcessInformation)
{
    TraceFile << "WrapCreateProcessW before" << std::endl;
    CREATE_PROCESSW_FUNC fptr = (CREATE_PROCESSW_FUNC)funcList.OriginalPtr(FUNC_CreateProcessW);
    BOOL ret = (*fptr)(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles,
                       dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
    TraceFile << "WrapCreateProcessW after " << std::endl;
    return ret;
}

/*
 * A test wrapper for WrapCreateProcessAsUserA()
 */

WINDOWS::BOOL WINAPI WrapCreateProcessAsUserA(WINDOWS::HANDLE hToken, WINDOWS::LPCTSTR lpApplicationName,
                                              WINDOWS::LPTSTR lpCommandLine, WINDOWS::LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                              WINDOWS::LPSECURITY_ATTRIBUTES lpThreadAttributes, WINDOWS::BOOL bInheritHandles,
                                              WINDOWS::DWORD dwCreationFlags, WINDOWS::LPVOID lpEnvironment,
                                              WINDOWS::LPCTSTR lpCurrentDirectory, WINDOWS::LPSTARTUPINFOA lpStartupInfo,
                                              WINDOWS::LPPROCESS_INFORMATION lpProcessInformation)
{
    TraceFile << "CreateProcessAsUserA before" << std::endl;
    BOOL ret = (*(CREATE_PROCESS_AS_USERA_FUNC)funcList.OriginalPtr(FUNC_CreateProcessAsUserA))(
        hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
        lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
    TraceFile << "CreateProcessAsUserA after " << std::endl;
    return ret;
}

/*
 * A test wrapper for WrapCreateProcessAsUserW()
 */
WINDOWS::BOOL WINAPI WrapCreateProcessAsUserW(WINDOWS::HANDLE hToken, WINDOWS::LPCWSTR lpApplicationName,
                                              WINDOWS::LPWSTR lpCommandLine, WINDOWS::LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                              WINDOWS::LPSECURITY_ATTRIBUTES lpThreadAttributes, WINDOWS::BOOL bInheritHandles,
                                              WINDOWS::DWORD dwCreationFlags, WINDOWS::LPVOID lpEnvironment,
                                              WINDOWS::LPCWSTR lpCurrentDirectory, WINDOWS::LPSTARTUPINFOW lpStartupInfo,
                                              WINDOWS::LPPROCESS_INFORMATION lpProcessInformation)
{
    TraceFile << "CreateProcessAsUserW before" << std::endl;
    BOOL ret = (*(CREATE_PROCESS_AS_USERW_FUNC)funcList.OriginalPtr(FUNC_CreateProcessAsUserW))(
        hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles, dwCreationFlags,
        lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);
    TraceFile << "CreateProcessAsUserW after " << std::endl;
    return ret;
}

FUNC_REPLACE_CONTAINER::FUNC_REPLACE_CONTAINER()
{
#define INIT_FENTRY(F) _array[FUNC_##F] = FUNC_REPLACE(std::string(#F), (VOID*)0, (VOID*)Wrap##F)
    INIT_FENTRY(CreateProcessA);
    INIT_FENTRY(CreateProcessW);
    INIT_FENTRY(CreateProcessAsUserA);
    INIT_FENTRY(CreateProcessAsUserW);
}

/* Find a routine that should be replaced and 
 * check whether the probed replacement is possible
 */
BOOL FindAndCheckRtn(IMG img, std::string rtnName, RTN& rtn)
{
    rtn = RTN_FindByName(img, rtnName.c_str());
    if (!RTN_Valid(rtn)) rtn = RTN_FindByName(img, (std::string("_") + rtnName).c_str());

    if (RTN_Valid(rtn))
    {
        if (!RTN_IsSafeForProbedReplacement(rtn))
        {
            TraceFile << "Cannot replace " << RTN_Name(rtn) << " in " << IMG_Name(img) << std::endl;
            exit(1);
        }
        return TRUE;
    }
    return FALSE;
}

BOOL ReplaceProbed(IMG img, FUNC_IDX idx)
{
    RTN rtn;
    if (!FindAndCheckRtn(img, funcList.Name(idx), rtn)) return FALSE;

    VOID* orgPtr = (VOID*)RTN_ReplaceProbed(rtn, AFUNPTR(funcList.Wrapper(idx)));
    funcList.SetOriginalPtr(idx, orgPtr);

    // TraceFile << "Inserted probe for " << funcList.Name(idx) << std::endl;
    return TRUE;
}

/*
 * Return TRUE if baseName matches tail of imageName. Comparison is case-insensitive.
 * @param[in]  imageName  image file name in either form with extension
 * @param[in]  baseName   image base name with extension (e.g. kernel32.dll)
 */
static BOOL CmpBaseImageName(const std::string& imageName, const std::string& baseName)
{
    if (imageName.size() >= baseName.size())
    {
        return strcasecmp(imageName.c_str() + imageName.size() - baseName.size(), baseName.c_str()) == 0;
    }
    return FALSE;
}

/* ===================================================================== */
// Called every time a new image is loaded
// Look for routines that we want to probe
VOID ImageLoad(IMG img, VOID* v)
{
    // Skip all images, but kernel32.dll
    if (!CmpBaseImageName(IMG_Name(img), "kernel32.dll"))
    {
        return;
    }

    for (UINT32 i = 0; i < funcList.Size(); i++)
    {
        ReplaceProbed(img, (FUNC_IDX)i);
    }
}

/* Ensure that Pin wrapper still works */
BOOL FollowChild(CHILD_PROCESS cProcess, VOID* userData)
{
    INT appArgc;
    CHAR const* const* appArgv;

    CHILD_PROCESS_GetCommandLine(cProcess, &appArgc, &appArgv);

    TraceFile << "Command line : " << std::endl;
    for (int i = 0; i < appArgc; i++)
    {
        TraceFile << appArgv[i] << " ";
    }
    TraceFile << std::endl;
    TraceFile << "Pin Wrapper works" << std::endl;
    return FALSE;
}

/* ===================================================================== */

int main(int argc, CHAR* argv[])
{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile << std::hex;
    TraceFile.setf(std::ios::showbase);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_AddFollowChildProcessFunction(FollowChild, 0);

    PIN_StartProgramProbed();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
