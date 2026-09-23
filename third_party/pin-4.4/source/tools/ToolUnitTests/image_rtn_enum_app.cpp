/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Copyright (C) 2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// Test application that links with one DLL at compile time and loads another at runtime
// This application is used to test the image_rtn_enum_tool PIN tool
//
// PIN ATTACH MODE:
// This application supports two execution modes:
//
// 1. NORMAL MODE (default):
//    The application is launched directly by Pin with instrumentation active from the start.
//    Example: pin -t tool.dll -- app.exe
//
// 2. ATTACH MODE (self-attach for testing):
//    The application receives Pin path and tool arguments via command line parameters,
//    loads its DLLs first, then launches Pin to attach to itself mid-execution.
//    This mode tests Pin's ability to attach to an already-running process and instrument
//    images (both the main executable and DLLs) that were loaded before Pin attached.
//
//    Command line format: app.exe -pin <pin_path> -pinarg <pin_and_tool_args...>
//    Example: app.exe -pin c:\pin\pin.exe -pinarg -t tool.dll -o output.txt
//
//    In attach mode, the application:
//    - Parses command line to extract Pin path and arguments
//    - Loads all DLLs and performs initial operations
//    - Launches Pin with "-pid <self_pid> -mt 0" to attach to itself
//    - Waits briefly for Pin to attach
//    - Continues execution under Pin's instrumentation
//

#include <stdio.h>
#include <windows.h>
#include <psapi.h>
#include <string.h>
#include <process.h>
#include "image_rtn_registry_dll.h"  // Compile-time linked DLL
#include "image_rtn_privilege_dll.h" // Runtime-loaded DLL

// Part 1: Test privilege DLL function
BOOL TestPrivilegeDll(EnablePrivilegeFunc EnablePrivilege)
{
    BOOL result;

    // Try to enable SE_LOCK_MEMORY_NAME privilege
    printf("Attempting to enable SE_LOCK_MEMORY_NAME privilege...\n");
    result = EnablePrivilege(SE_LOCK_MEMORY_NAME);
    printf("Result: %s\n", result ? "SUCCESS" : "FAILED (expected without admin rights)");

    return result;
}

// Helper function to check if a DLL is configured for large pages
BOOL CheckDllLargePagesConfiguration(LPCSTR largePageDllsPath, LPCSTR dllName)
{
    DWORD dllValue;

    printf("  Value name: %s\n", dllName);
    if (!ReadRegistryDWORD(HKEY_LOCAL_MACHINE, largePageDllsPath, dllName, &dllValue))
    {
        printf("  Result: NOT FOUND\n");
        printf("Large pages NOT configured: %s not found in LargePageDLLs\n", dllName);
        return FALSE;
    }

    printf("  Value data: 0x%08lX (%lu)\n", dllValue, dllValue);

    if (dllValue == 0)
    {
        printf("Large pages NOT enabled: %s = 0\n", dllName);
        return FALSE;
    }

    printf("DLL large pages ENABLED: %s\n\n", dllName);
    return TRUE;
}

// Helper function to get memory protection string
const char* GetProtectionString(DWORD protection)
{
    // Remove common flags to get base protection
    DWORD baseProtection = protection & 0xFF;

    switch (baseProtection)
    {
        case PAGE_NOACCESS:
            return "PAGE_NOACCESS";
        case PAGE_READONLY:
            return "PAGE_READONLY";
        case PAGE_READWRITE:
            return "PAGE_READWRITE";
        case PAGE_WRITECOPY:
            return "PAGE_WRITECOPY";
        case PAGE_EXECUTE:
            return "PAGE_EXECUTE";
        case PAGE_EXECUTE_READ:
            return "PAGE_EXECUTE_READ";
        case PAGE_EXECUTE_READWRITE:
            return "PAGE_EXECUTE_READWRITE";
        case PAGE_EXECUTE_WRITECOPY:
            return "PAGE_EXECUTE_WRITECOPY";
        default:
            return "UNKNOWN";
    }
}

// Function to get image base address and check access attributes
BOOL CheckImageAccessAttributes(HMODULE hModule, const char* imageName, BOOL isLargePagesEnabled)
{
    MEMORY_BASIC_INFORMATION mbi;
    PVOID baseAddress = (PVOID)hModule;

    printf("\nChecking image: %s\n", imageName);
    printf("  Base address: 0x%p\n", baseAddress);

    // Query memory information for the base address
    if (VirtualQuery(baseAddress, &mbi, sizeof(mbi)) == 0)
    {
        printf("  Error: VirtualQuery failed with error code %lu\n", GetLastError());
        return FALSE;
    }

    printf("  Allocation base: 0x%p\n", mbi.AllocationBase);
    printf("  Region size: 0x%zX bytes\n", mbi.RegionSize);
    printf("  State: 0x%08lX (%s)\n", mbi.State,
           mbi.State == MEM_COMMIT    ? "MEM_COMMIT"
           : mbi.State == MEM_RESERVE ? "MEM_RESERVE"
                                      : "MEM_FREE");
    printf("  Protection: 0x%08lX (%s)\n", mbi.Protect, GetProtectionString(mbi.Protect));
    printf("  Type: 0x%08lX (%s)\n", mbi.Type,
           mbi.Type == MEM_IMAGE    ? "MEM_IMAGE"
           : mbi.Type == MEM_MAPPED ? "MEM_MAPPED"
                                    : "MEM_PRIVATE");

    // Verify expected protection based on large pages configuration
    DWORD expectedProtection = isLargePagesEnabled ? PAGE_EXECUTE_WRITECOPY : PAGE_READONLY;
    DWORD baseProtection     = mbi.Protect & 0xFF;

    if (baseProtection == expectedProtection)
    {
        printf("    Protection matches expected: %s\n", GetProtectionString(expectedProtection));
        return TRUE;
    }
    else
    {
        printf("    Protection mismatch!\n");
        printf("    Expected: %s (0x%08lX)\n", GetProtectionString(expectedProtection), expectedProtection);
        printf("    Actual:   %s (0x%08lX)\n", GetProtectionString(baseProtection), baseProtection);
        return FALSE;
    }
}

// Part 2: Use registry_dll.dll (compile-time linked)
// Verify that the application and its DLLs are configured to use large pages
BOOL ReadLargePagesConfiguration(HMODULE hPrivilegeDll, BOOL hasPrivilege, const char* privilegeDllName,
                                 const char* registryDllName)
{
    char exePath[MAX_PATH];
    char appName[MAX_PATH];
    char registryPath[512];
    char largePageDllsPath[512];
    DWORD dwValue;
    DWORD dllValue;
    BOOL isAppLargePagesEnabled          = FALSE;
    BOOL isRegistryDllLargePagesEnabled  = FALSE;
    BOOL isPrivilegeDllLargePagesEnabled = FALSE;
    BOOL success                         = TRUE;

    printf("Part 2: Verifying large pages configuration (using registry_dll.dll)...\n");

    // Get the current executable name
    if (GetModuleFileNameA(NULL, exePath, sizeof(exePath)) == 0)
    {
        fprintf(stderr, "Error: Failed to get executable path. Error code: %lu\n", GetLastError());
        return FALSE;
    }

    // Extract just the filename from the full path
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash != NULL)
    {
        strcpy(appName, lastSlash + 1);
    }
    else
    {
        strcpy(appName, exePath);
    }

    // If privilege test failed, skip registry reading and set all to FALSE
    if (!hasPrivilege)
    {
        printf("SE_LOCK_MEMORY_NAME privilege not available - large pages cannot be used.\n");
        printf("Skipping registry checks. All images will use standard page protection.\n\n");
        isAppLargePagesEnabled          = FALSE;
        isRegistryDllLargePagesEnabled  = FALSE;
        isPrivilegeDllLargePagesEnabled = FALSE;
    }
    else
    {
        // Build registry paths
        snprintf(registryPath, sizeof(registryPath),
                 "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\%s", appName);

        snprintf(largePageDllsPath, sizeof(largePageDllsPath),
                 "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\%s\\LargePageDLLs", appName);

        // Step 1: Read UseLargePages DWORD value for the application
        printf("Reading registry key: HKEY_LOCAL_MACHINE\\%s\n", registryPath);
        printf("  Value name: UseLargePages\n");

        if (!ReadRegistryDWORD(HKEY_LOCAL_MACHINE, registryPath, "UseLargePages", &dwValue))
        {
            printf("  Result: NOT FOUND\n");
            printf("Large pages NOT configured: UseLargePages registry value not found for %s\n", appName);
            isAppLargePagesEnabled = FALSE;
        }
        else
        {
            printf("  Value data: 0x%08lX (%lu)\n", dwValue, dwValue);

            if (dwValue == 0)
            {
                printf("Large pages NOT enabled: UseLargePages = 0 for %s\n", appName);
                isAppLargePagesEnabled = FALSE;
            }
            else
            {
                printf("Application large pages ENABLED for %s\n\n", appName);
                isAppLargePagesEnabled = TRUE;
            }
        }

        // Step 2: Check each DLL's large pages configuration individually
        printf("Reading registry key: HKEY_LOCAL_MACHINE\\%s\n", largePageDllsPath);

        // Check for image_rtn_registry_dll.dll (use the dynamic name)
        isRegistryDllLargePagesEnabled = CheckDllLargePagesConfiguration(largePageDllsPath, registryDllName);

        // Check for image_rtn_privilege_dll.dll (use the dynamic name)
        isPrivilegeDllLargePagesEnabled = CheckDllLargePagesConfiguration(largePageDllsPath, privilegeDllName);
    }

    // Step 3: Check memory access attributes for all images
    printf("\n==========================================\n");
    printf("Part 3: Checking memory access attributes\n");
    printf("==========================================\n");
    printf("Large pages configuration per image:\n");
    printf("  Application: %s\n", isAppLargePagesEnabled ? "ENABLED" : "DISABLED");
    printf("  Registry DLL: %s\n", isRegistryDllLargePagesEnabled ? "ENABLED" : "DISABLED");
    printf("  Privilege DLL: %s\n", isPrivilegeDllLargePagesEnabled ? "ENABLED" : "DISABLED");

    // Check main application
    HMODULE hMainApp = GetModuleHandle(NULL);
    if (!CheckImageAccessAttributes(hMainApp, appName, isAppLargePagesEnabled))
    {
        success = FALSE;
    }

    // Check registry DLL (compile-time linked)
    HMODULE hRegistryDll = GetModuleHandleA(registryDllName);
    if (hRegistryDll != NULL)
    {
        if (!CheckImageAccessAttributes(hRegistryDll, registryDllName, isRegistryDllLargePagesEnabled))
        {
            success = FALSE;
        }
    }
    else
    {
        printf("\nWarning: Could not get handle for %s\n", registryDllName);
        success = FALSE;
    }

    // Check privilege DLL (runtime loaded)
    if (hPrivilegeDll != NULL)
    {
        if (!CheckImageAccessAttributes(hPrivilegeDll, privilegeDllName, isPrivilegeDllLargePagesEnabled))
        {
            success = FALSE;
        }
    }
    else
    {
        printf("\nWarning: Privilege DLL handle is NULL\n");
        success = FALSE;
    }

    return success;
}

// Parse command line for attach mode: -pin <path> -pinarg <args...>
// Returns TRUE if attach mode is requested (Pin will be launched by the application)
// Returns FALSE for normal mode (application is already running under Pin)
static BOOL ParseCommandLine(int argc, char* argv[], char** pinPath, char*** pinArgs, int* pinArgCount)
{
    BOOL foundPin = FALSE;
    int argIdx    = 1;

    *pinPath     = NULL;
    *pinArgs     = NULL;
    *pinArgCount = 0;

    while (argIdx < argc)
    {
        if (strcmp(argv[argIdx], "-pin") == 0 && argIdx + 1 < argc)
        {
            *pinPath = argv[argIdx + 1];
            foundPin = TRUE;
            argIdx += 2;
        }
        else if (strcmp(argv[argIdx], "-pinarg") == 0)
        {
            // Count remaining arguments
            int remainingArgs = argc - argIdx - 1;
            *pinArgCount      = remainingArgs;
            *pinArgs          = (char**)malloc(sizeof(char*) * (remainingArgs + 1));

            for (int i = 0; i < remainingArgs; i++)
            {
                (*pinArgs)[i] = argv[argIdx + 1 + i];
            }
            (*pinArgs)[remainingArgs] = NULL;
            break;
        }
        else
        {
            argIdx++;
        }
    }

    return foundPin;
}

// Start Pin in attach mode
static void StartPinAttach(const char* pinPath, char** pinArgs, int pinArgCount)
{
    DWORD appPid = GetCurrentProcessId();
    char pidStr[32];
    sprintf(pidStr, "%lu", appPid);

    // Build command line: pin.exe -pid <pid> -mt 0 <pinargs>
    int totalArgs  = 5 + pinArgCount; // pin, -pid, <pid>, -mt, 0, + pinargs
    char** cmdArgs = (char**)malloc(sizeof(char*) * (totalArgs + 1));

    int idx        = 0;
    cmdArgs[idx++] = (char*)pinPath;
    cmdArgs[idx++] = "-pid";
    cmdArgs[idx++] = pidStr;
    cmdArgs[idx++] = "-mt";
    cmdArgs[idx++] = "0";

    for (int i = 0; i < pinArgCount; i++)
    {
        cmdArgs[idx++] = pinArgs[i];
    }
    cmdArgs[idx] = NULL;

    // Build command line string
    char cmdLine[4096] = "";
    for (int i = 0; i < totalArgs; i++)
    {
        if (i > 0) strcat(cmdLine, " ");

        // Quote arguments with spaces
        if (strchr(cmdArgs[i], ' ') != NULL)
        {
            strcat(cmdLine, "\"");
            strcat(cmdLine, cmdArgs[i]);
            strcat(cmdLine, "\"");
        }
        else
        {
            strcat(cmdLine, cmdArgs[i]);
        }
    }

    printf("Launching Pin: %s\n", cmdLine);

    // Spawn Pin process
    STARTUPINFOA si        = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb                  = sizeof(si);

    if (!CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("Error: Failed to launch Pin (error %lu)\n", GetLastError());
    }
    else
    {
        printf("Pin process started (PID: %lu)\n", pi.dwProcessId);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    free(cmdArgs);
}

int main(int argc, char* argv[])
{
    BOOL success                        = TRUE;
    HMODULE hPrivilegeDll               = NULL;
    EnablePrivilegeFunc EnablePrivilege = NULL;
    char* pinPath                       = NULL;
    char** pinArgs                      = NULL;
    int pinArgCount                     = 0;
    char privilegeDllName[256];
    char registryDllName[256];
    char* exeName   = NULL;
    char* exeSuffix = NULL;

    printf("Image RTN Enumeration Test Application\n");
    printf("=======================================\n\n");

    // Determine the DLL names based on the executable name
    // Extract executable name from argv[0] or GetModuleFileName
    exeName = strrchr(argv[0], '\\');
    if (exeName == NULL)
    {
        exeName = strrchr(argv[0], '/');
    }
    exeName = (exeName == NULL) ? argv[0] : (exeName + 1);

    // Find the suffix after "image_rtn_enum_app"
    exeSuffix = strstr(exeName, "image_rtn_enum_app");
    if (exeSuffix == NULL)
    {
        fprintf(stderr, "Error: Executable name '%s' doesn't match expected pattern 'image_rtn_enum_app*'\n", exeName);
        return 1;
    }

    exeSuffix += strlen("image_rtn_enum_app");

    // Find where the suffix ends (before .exe)
    char* dotPos = strstr(exeSuffix, ".exe");
    if (dotPos == NULL || dotPos <= exeSuffix)
    {
        fprintf(stderr, "Error: No suffix found in executable name '%s'\n", exeName);
        fprintf(stderr, "Expected format: image_rtn_enum_app_<suffix>.exe\n");
        return 1;
    }

    // Extract the suffix (e.g., "_jit", "_probe", "_jit_attach", "_probe_attach")
    size_t suffixLen = dotPos - exeSuffix;
    if (suffixLen == 0 || suffixLen >= 100)
    {
        fprintf(stderr, "Error: Invalid suffix length (%zu) in executable name '%s'\n", suffixLen, exeName);
        return 1;
    }

    // Build the DLL names: base_name + suffix + .dll
    snprintf(privilegeDllName, sizeof(privilegeDllName), "image_rtn_privilege_dll%.*s.dll", (int)suffixLen, exeSuffix);
    snprintf(registryDllName, sizeof(registryDllName), "image_rtn_registry_dll%.*s.dll", (int)suffixLen, exeSuffix);

    printf("Executable: %s\n", exeName);
    printf("Determined registry DLL name: %s\n", registryDllName);
    printf("Determined privilege DLL name: %s\n\n", privilegeDllName);

    // Check if we're running in attach mode
    if (ParseCommandLine(argc, argv, &pinPath, &pinArgs, &pinArgCount))
    {
        printf("Running in ATTACH mode\n");
        printf("Will attach Pin after loading DLLs\n\n");
    }

    // Load privilege DLL at runtime
    printf("Loading %s at runtime...\n", privilegeDllName);
    hPrivilegeDll = LoadLibraryA(privilegeDllName);
    if (hPrivilegeDll == NULL)
    {
        fprintf(stderr, "Error: Failed to load %s (Error: %lu)\n", privilegeDllName, GetLastError());
        return 1;
    }

    EnablePrivilege = (EnablePrivilegeFunc)GetProcAddress(hPrivilegeDll, "EnablePrivilege");

    if (EnablePrivilege == NULL)
    {
        fprintf(stderr, "Error: EnablePrivilege function pointer is NULL\n");
        return 1;
    }

    // Part 1: Test privilege DLL
    BOOL hasPrivilege = TestPrivilegeDll(EnablePrivilege);
    if (!hasPrivilege)
    {
        printf("Note: TestPrivilegeDll() returned failure - large pages will not be available\n");
    }
    printf("\n");

    // If attach mode, start Pin now before continuing
    if (pinPath != NULL)
    {
        printf("Starting Pin attach...\n");
        StartPinAttach(pinPath, pinArgs, pinArgCount);

        // Wait a bit for Pin to attach
        printf("Waiting for Pin to attach...\n");
        Sleep(2000);
        printf("Continuing execution...\n\n");
    }

    // Part 2: Use registry_dll.dll (compile-time linked)
    if (!ReadLargePagesConfiguration(hPrivilegeDll, hasPrivilege, privilegeDllName, registryDllName))
    {
        printf("Warning: ReadLargePagesConfiguration() returned failure\n");
        success = FALSE;
    }
    printf("\n");

    printf("\nApplication completed %s.\n", success ? "successfully" : "with warnings");

    // Cleanup: Free privilege DLL
    if (hPrivilegeDll != NULL)
    {
        FreeLibrary(hPrivilegeDll);
        printf("Freed privilege DLL\n");
    }

    // Free attach mode arguments
    if (pinArgs != NULL)
    {
        free(pinArgs);
    }

    return success ? 0 : 1;
}
