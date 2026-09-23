/*
 * Copyright (C) 2009-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#define UNICODE
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_DEPRECATE

#include <Windows.h>
#include <Winsvc.h>
#include <string>
#include <iostream>
#include <fstream>








static wchar_t* ServiceNameW        = L"PinLauncherService";
static wchar_t* ServiceDisplayNameW = L"Pin Launcher Service";
static wchar_t* BinaryNameW         = L"w_pin_service_launcher.exe";

// perform administrative actions, this class is used
// when the process runs as regular process (not as service)
class SERVICE_ADMIN
{
  public:
    static void Main(int argc, wchar_t* argv[]);

  private:
    BOOL Create();
    BOOL Delete();
    BOOL Start(DWORD argc, LPCTSTR* argv);
    BOOL Stop();

    void Usage();
};

// functions to be used when the process runs as service
class SERVICE_MANAGER
{
  public:
    static void WINAPI Main(DWORD argc, LPTSTR* argv);
    static void WINAPI ControlHandler(DWORD opcode);
    static void CreatePinProcessAndWait(DWORD argc, LPCWSTR* argv);
    static void StopService(); //never returns
    static void OpenFile();

  private:
    static void LaunchProcess(std::wstring cmdLine);

    static SERVICE_STATUS status;
    static SERVICE_STATUS_HANDLE statusHandle;
    static std::ofstream outFile;
};

SERVICE_STATUS SERVICE_MANAGER::status;
SERVICE_STATUS_HANDLE SERVICE_MANAGER::statusHandle;
std::ofstream SERVICE_MANAGER::outFile;

// main function

int wmain(int argc, wchar_t* argv[])
{
    if ((argc >= 2) && ((wcscmp(argv[1], L"-admin") == 0) || (wcscmp(argv[1], L"-help") == 0)))
    {
        SERVICE_ADMIN::Main(argc, argv);
    }
    else
    {
        SERVICE_MANAGER::OpenFile();
        SERVICE_TABLE_ENTRY dispatchData[] = {{ServiceNameW, SERVICE_MANAGER::Main}, {NULL, NULL}};
        StartServiceCtrlDispatcher(dispatchData);
    }
    return 0;
}

//manager impl

void SERVICE_MANAGER::CreatePinProcessAndWait(DWORD argc, LPCWSTR* argv)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(STARTUPINFO);
    memset(&pi, 0, sizeof(pi));

    std::wstring cmdLine = L"";
    for (DWORD i = 0; i < argc; i++)
    {
        cmdLine += std::wstring(L"\"") + argv[i] + L"\"";
        if (i < argc - 1)
        {
            cmdLine += L" ";
        }
    }

    if (!CreateProcess(NULL, (LPWSTR)cmdLine.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        outFile << "Failed to CreateProcess, " << GetLastError() << std::endl;
    }

    if (WaitForSingleObject(pi.hProcess, 60 * 1000) != WAIT_OBJECT_0)
    {
        outFile << "Pin didn't finish running after 60 sec. " << std::endl;
    }

    return;
}

void WINAPI SERVICE_MANAGER::Main(DWORD argc, LPTSTR* argv)
{
    status.dwServiceType             = SERVICE_WIN32;
    status.dwCurrentState            = SERVICE_START_PENDING;
    status.dwControlsAccepted        = SERVICE_ACCEPT_STOP;
    status.dwWin32ExitCode           = 0;
    status.dwServiceSpecificExitCode = 0;
    status.dwCheckPoint              = 0;
    status.dwWaitHint                = 0;

    statusHandle = RegisterServiceCtrlHandler(ServiceNameW, SERVICE_MANAGER::ControlHandler);
    if (statusHandle == (SERVICE_STATUS_HANDLE)NULL)
    {
        outFile << "Failed to RegisterServiceCtrlHandler, " << GetLastError() << std::endl;
        return;
    }

    status.dwCurrentState = SERVICE_RUNNING;
    status.dwCheckPoint   = 0;
    status.dwWaitHint     = 0;

    if (!SetServiceStatus(statusHandle, &status))
    {
        outFile << "Failed to SetServiceStatus, " << GetLastError() << std::endl;
        return;
    }

    CreatePinProcessAndWait(argc - 1, (LPCWSTR*)&argv[1]);

    StopService();

    return;
}

void WINAPI SERVICE_MANAGER::ControlHandler(DWORD opcode)
{
    switch (opcode)
    {
        case SERVICE_CONTROL_PAUSE:
            status.dwCurrentState = SERVICE_PAUSED;
            break;
        case SERVICE_CONTROL_CONTINUE:
            status.dwCurrentState = SERVICE_RUNNING;
            break;
        case SERVICE_CONTROL_STOP:
            status.dwWin32ExitCode = 0;
            status.dwCurrentState  = SERVICE_STOPPED;
            status.dwCheckPoint    = 0;
            status.dwWaitHint      = 0;
            outFile.flush();
            outFile.close();
            SetServiceStatus(statusHandle, &status);
            break;
        case SERVICE_CONTROL_INTERROGATE:
            break;
    }
    return;
}

void SERVICE_MANAGER::StopService()
{
    wchar_t binPathArr[2048];
    GetModuleFileName(NULL, binPathArr, 2048);
    LPCTSTR binPath = binPathArr;
    std::wstring cmdLine = std::wstring(L"\"") + binPath + L"\"";
    cmdLine += L" -admin -stop";
    LaunchProcess(cmdLine);
}

void SERVICE_MANAGER::LaunchProcess(std::wstring cmdLine)
{
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(STARTUPINFO);
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcess(NULL, (LPWSTR)cmdLine.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        outFile << "Failed to CreateProcess, " << GetLastError() << std::endl;
    }
    return;
}

void SERVICE_MANAGER::OpenFile()
{
    wchar_t binPathArr[2048];
    GetModuleFileName(NULL, binPathArr, 2048);

    size_t serviceExeNameLen       = wcslen(BinaryNameW);
    size_t servicePathNameLen      = wcslen(binPathArr) - serviceExeNameLen;
    binPathArr[servicePathNameLen] = L'\0';

    SetCurrentDirectory(binPathArr);

    std::wstring fileName = binPathArr;
    fileName += BinaryNameW;
    fileName += L".service.log";
    outFile.open(fileName.c_str());
}

//admin impl

void SERVICE_ADMIN::Main(int argc, wchar_t* argv[])
{
    SERVICE_ADMIN admin;

    if ((argc == 1) || ((argc == 2) && ((wcscmp(argv[1], L"-admin") == 0) || (wcscmp(argv[1], L"-help") == 0))))
    {
        return admin.Usage();
    }

    if (wcscmp(argv[2], L"-create") == 0)
    {
        if (admin.Create())
        {
            std::cerr << "Created service sucessfully!" << std::endl;
        }
        else
        {
            std::cerr << "Failed to Create service" << std::endl;
        }
    }
    else if (wcscmp(argv[2], L"-start") == 0)
    {
        if (admin.Start((DWORD)(argc - 3), (LPCWSTR*)&argv[3]))
        {
            std::cerr << "Started service sucessfully!" << std::endl;
        }
        else
        {
            std::cerr << "Failed to start service" << std::endl;
        }
    }
    else if (wcscmp(argv[2], L"-stop") == 0)
    {
        if (admin.Stop())
        {
            std::cerr << "Stopped service sucessfully!" << std::endl;
        }
        else
        {
            std::cerr << "Failed to stop service" << std::endl;
        }
    }
    else if (wcscmp(argv[2], L"-delete") == 0)
    {
        if (admin.Delete())
        {
            std::cerr << "Deleted service sucessfully!" << std::endl;
        }
        else
        {
            std::cerr << "Failed to delete service" << std::endl;
        }
    }
    else
    {
        admin.Usage();
    }

    return;
}

BOOL SERVICE_ADMIN::Create()
{
    SC_HANDLE manager;
    manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (manager == NULL)
    {
        std::cerr << "Failed to OpenSCManager, " << GetLastError() << std::endl;
        return false;
    }

    wchar_t binPathArr[2048];
    GetModuleFileName(NULL, binPathArr, 2048);
    std::wstring path = binPathArr;
    path         = L"\"" + path + L"\"";

#if 0 // This code is intended for symbolic link substitution
    size_t slashLocation = path.find(L"\\");
    std::wstring drive = path.substr(0, slashLocation);
    path = path.substr(slashLocation);

    wchar_t targetPath[2048];
    QueryDosDevice(drive.c_str(), targetPath, 2048);
    path = std::wstring(targetPath) + path;
    if(path.find(L"\\??\\") == 0)
    {
        /* Remove \??\ */
        path = path.substr(4);
    }
    else if(path.find(L"\\DosDevices\\") == 0)
    {
        /* Remove \DosDevices\ */
        path = path.substr(12);
    }
#endif

    LPCTSTR binPath = path.c_str();

    SC_HANDLE service;
    service = CreateService(manager, ServiceNameW, ServiceDisplayNameW, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                            SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, binPath, NULL, NULL, NULL,
                            NULL,  // log on as Local System
                            NULL); // no password
    if (service == NULL && (GetLastError() != ERROR_SERVICE_EXISTS))
    {
        std::cerr << "Failed to CreateService, " << GetLastError() << std::endl;
        CloseServiceHandle(manager);
        return false;
    }

    CloseServiceHandle(manager);
    CloseServiceHandle(service);
    return true;
}

BOOL SERVICE_ADMIN::Start(DWORD argc, LPCWSTR* argv)
{
    SC_HANDLE manager;
    manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (manager == NULL)
    {
        std::cerr << "Failed to OpenSCManager, " << GetLastError() << std::endl;
        return false;
    }

    SC_HANDLE service;
    service = OpenService(manager, ServiceNameW, SERVICE_ALL_ACCESS);
    if (service == NULL)
    {
        std::cerr << "Failed to OpenService, " << GetLastError() << std::endl;
        CloseServiceHandle(manager);
        return false;
    }

    if (!StartService(service, argc, argv))
    {
        std::cerr << "Failed to StartService, " << GetLastError() << std::endl;
        CloseServiceHandle(manager);
        CloseServiceHandle(service);
        return false;
    }

    Sleep(12000);

    //maximum 8k
    SERVICE_STATUS_PROCESS* pServiceStatusProcess = (SERVICE_STATUS_PROCESS*)malloc(0x2000);
    pServiceStatusProcess->dwCurrentState         = SERVICE_START_PENDING;

    //let pin to do it's job, it has 60 seconds to complete
    int timer = 12;
    while ((timer != 0) && (pServiceStatusProcess->dwCurrentState != SERVICE_STOPPED))
    {
        timer--;
        DWORD bytesNeeded = 0;
        if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)pServiceStatusProcess, 0x2000, &bytesNeeded))
        {
            std::cout << "Failed to QueryServiceStatusEx, " << GetLastError() << std::endl;
            CloseServiceHandle(manager);
            CloseServiceHandle(service);
            free(pServiceStatusProcess);
            return false;
        }
        Sleep(5000);
    }

    if (pServiceStatusProcess->dwCurrentState != SERVICE_STOPPED)
    {
        std::cerr << "Service haven't stopped after 60 seconds" << std::endl;
        CloseServiceHandle(manager);
        CloseServiceHandle(service);
        free(pServiceStatusProcess);
        return false;
    }

    CloseServiceHandle(manager);
    CloseServiceHandle(service);
    free(pServiceStatusProcess);

    return true;
}

BOOL SERVICE_ADMIN::Stop()
{
    SC_HANDLE manager;
    manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (manager == NULL)
    {
        std::cout << "Failed to OpenSCManager, " << GetLastError() << std::endl;
        return false;
    }

    SC_HANDLE service;
    service = OpenService(manager, ServiceNameW, SERVICE_ALL_ACCESS);
    if (service == NULL)
    {
        std::cout << "Failed to OpenService, " << GetLastError() << std::endl;
        CloseServiceHandle(manager);
        return false;
    }

    SERVICE_STATUS status;
    BOOL res = ControlService(service, SERVICE_CONTROL_STOP, &status);
    if (!res && (GetLastError() != ERROR_SERVICE_NOT_ACTIVE))
    {
        std::cout << "Failed to ControlService, " << GetLastError() << std::endl;
        CloseServiceHandle(manager);
        CloseServiceHandle(service);
        return false;
    }

    CloseServiceHandle(manager);
    CloseServiceHandle(service);
    return true;
}

BOOL SERVICE_ADMIN::Delete()
{
    SC_HANDLE manager;
    manager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (manager == NULL)
    {
        std::cout << "Failed to OpenSCManager, " << GetLastError() << std::endl;
        return false;
    }

    SC_HANDLE service;
    service = OpenService(manager, ServiceNameW, SERVICE_ALL_ACCESS);
    if (service == NULL)
    {
        std::cout << "Failed to OpenService, " << GetLastError() << std::endl;
        CloseServiceHandle(manager);
        return false;
    }

    if (!DeleteService(service))
    {
        std::cout << "Failed to DeleteService, " << GetLastError() << std::endl;
        CloseServiceHandle(manager);
        CloseServiceHandle(service);
        return false;
    }

    CloseServiceHandle(manager);
    CloseServiceHandle(service);
    return true;
}

void SERVICE_ADMIN::Usage()
{
    std::cerr << "Usage:" << std::endl;
    std::cerr << "To create the service: -admin -create" << std::endl;
    std::cerr << "To start the service:  -admin -start <pin> <pin arguments>" << std::endl;
    std::cerr << "To start the service:  -admin -stop" << std::endl;
    std::cerr << "To delete the service: -admin -delete" << std::endl;
}
