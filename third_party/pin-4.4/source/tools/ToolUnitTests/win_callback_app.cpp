/*
 * Copyright (C) 2010-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * This test application is used to verify correctness of handling Windows callbacks in Pin.
 */

#include <windows.h>
#include <string>
#include <iostream>






#define USR_MESSAGE (WM_USER + 1)

// Print the specified error message and abort the process
static void Abort(const std::string& errorMessage)
{
    std::cout << "[win_callback_app] Failure: " << errorMessage << std::endl;
    exit(1);
}

// Execute a system call from a Window procedure. This procedure can be replaced by a tool.
extern "C" __declspec(dllexport) void SyscallInCallback() { Sleep(1); }

// Window (callback) procedure
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case USR_MESSAGE:
            std::cout << "[win_callback_app] Received USR_MESSAGE, lParam = " << std::hex << lParam << std::endl;

            SyscallInCallback(); // execute a system call (in a replacement routine)
            return lParam;       // return from callback

        case WM_DESTROY:
            PostQuitMessage(0);
            break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// Thread root procedure that sends a message to the window whose handle is specified by <pArg>
DWORD WINAPI ThreadProc(LPVOID pArg)
{
    HWND hWnd = (HWND)pArg;

    std::cout << "[win_callback_app] Sending USR_MESSAGE ..." << std::endl;
    LRESULT lRes = SendMessage(hWnd, USR_MESSAGE, 0, 0x1234);
    std::cout << "[win_callback_app] USR_MESSAGE sent. Result = " << std::hex << lRes << std::endl;

    if (lRes != 0x1234)
    {
        Abort("SendMessage");
    }

    SendMessage(hWnd, WM_CLOSE, 0, 0);
    return 0;
}

// Main procedure: create a window and a thread that sends a message (callback) to the window.
int main()
{
    std::cout << "[win_callback_app] Registering window class ..." << std::endl;

    WNDCLASS wc;
    wc.hInstance     = NULL;
    wc.lpszClassName = "TestWndClass";
    wc.lpfnWndProc   = (WNDPROC)WindowProc;
    wc.style         = 0;
    wc.hIcon         = NULL;
    wc.hCursor       = NULL;
    wc.lpszMenuName  = NULL;
    wc.cbClsExtra    = 0;
    wc.cbWndExtra    = 0;
    wc.hbrBackground = (HBRUSH)NULL;

    if (RegisterClass(&wc) == 0)
    {
        Abort("RegisterClass");
    }

    //--------------------------------------------------------------------------------
    std::cout << "[win_callback_app] Creating a window ..." << std::endl;

    HWND hWnd = CreateWindow("TestWndClass", // window class
                             "",             // title
                             0,              // style
                             CW_USEDEFAULT,  // default horizontal position
                             CW_USEDEFAULT,  // default vertical position
                             CW_USEDEFAULT,  // default width
                             CW_USEDEFAULT,  // default height
                             HWND_MESSAGE,   // parent
                             (HMENU)NULL,    // menu
                             NULL,           // application instance
                             NULL);          // window-creation data

    if (hWnd == 0)
    {
        Abort("CreateWindow");
    }

    //--------------------------------------------------------------------------------
    std::cout << "[win_callback_app] Creating a thread ..." << std::endl;

    HANDLE hThread = CreateThread(NULL, 0, ThreadProc, (LPVOID)hWnd, 0, 0);

    if (hThread == NULL)
    {
        Abort("CreateThread");
    }

    //--------------------------------------------------------------------------------
    std::cout << "[win_callback_app] Receiving messages ..." << std::endl;

    while (TRUE)
    {
        // Use PeekMessage() instead of GetMessage() to expose bug from Mantis #2221
        MSG msg;
        BOOL bRet = PeekMessage(&msg, hWnd, 0, 0, PM_REMOVE);
        if (!bRet)
        {
            Sleep(10);
        }
        else if (msg.message == WM_QUIT)
        {
            break;
        }
    }

    std::cout << "[win_callback_app] Success" << std::endl;
    return 0;
}
