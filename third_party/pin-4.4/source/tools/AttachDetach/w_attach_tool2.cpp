/*
 * Copyright (C) 2008-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//includes section start

#include "pin.H"

#include <iostream>

#include <fstream>

#include <time.h>

#include <sys/timeb.h>







namespace WIND
{
//required for waitortimercallback and ptimerapcroutine
#define _WIN32_WINNT 0x0501

//include sockets
#include <winsock2.h>

// include windows.h in its own namespace to avoid conflicting definitions
#include <windows.h>

const DWORD tls_out_of_indexes = TLS_OUT_OF_INDEXES;
} // namespace WIND

//includes section footer

//globals section start

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "w_attach_tool2.txt", "specify tool log file name");

KNOB< BOOL > KnobDoPrint(KNOB_MODE_WRITEONCE, "pintool", "do_print", "0",
                         "set if print from replacement functions to outputfile is desired");

KNOB< UINT32 > KnobStressDetachReAttach(KNOB_MODE_WRITEONCE, "pintool", "stress_dr", "0",
                                        "stress test for detach & reattach mechanism");

/* ===================================================================== */

INT32 Usage()
{
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    std::cerr.flush();
    return -1;
}

std::ofstream OutFile;

WIND::DWORD dwIndexTls;

WIND::HRESULT(APIENTRY* fptrPrintDlgExW)(WIND::LPPRINTDLGEXW);

WIND::RPC_STATUS(WINAPI* fptrRpcNsBindingLookupBeginW)(unsigned long, WIND::RPC_WSTR, WIND::RPC_IF_HANDLE, WIND::UUID __RPC_FAR*,
                                                       unsigned long, WIND::RPC_NS_HANDLE __RPC_FAR*);

WIND::HANDLE(WINAPI* fptrGetStdHandle)(WIND::DWORD);

WIND::BOOL(WINAPI* fptrReleaseMutex)(WIND::HANDLE);

WIND::BOOL(WINAPI* fptrGetOverlappedResult)(WIND::HANDLE, WIND::LPOVERLAPPED, WIND::LPDWORD, WIND::BOOL);

WIND::DWORD(WINAPI* fptrWaitForSingleObject)(WIND::HANDLE, WIND::DWORD);

long(WINAPI* fptrBroadcastSystemMessageA)(WIND::DWORD, WIND::LPDWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

WIND::DWORD(WINAPI* fptrSignalObjectAndWait)(WIND::HANDLE, WIND::HANDLE, WIND::DWORD, WIND::BOOL);

WIND::RPC_STATUS(WINAPI* fptrRpcNsBindingLookupBeginA)(unsigned long, WIND::RPC_CSTR, WIND::RPC_IF_HANDLE, WIND::UUID __RPC_FAR*,
                                                       unsigned long, WIND::RPC_NS_HANDLE __RPC_FAR*);

WIND::HRESULT(APIENTRY* fptrPrintDlgExA)(WIND::LPPRINTDLGEXA);

WIND::HANDLE(WINAPI* fptrCreateTimerQueue)();

WIND::RPC_STATUS(WINAPI* fptrRpcNsBindingImportNext)(WIND::RPC_NS_HANDLE, WIND::RPC_BINDING_HANDLE __RPC_FAR*);

WIND::DWORD(WINAPI* fptrQueueUserAPC)(WIND::PAPCFUNC, WIND::HANDLE, WIND::ULONG_PTR);

WIND::RPC_STATUS(WINAPI* fptrRpcNsBindingLookupNext)(WIND::RPC_NS_HANDLE, WIND::RPC_BINDING_VECTOR __RPC_FAR* __RPC_FAR*);

int(WSAAPI* fptrsend)(WIND::SOCKET, const char FAR*, int, int);

WIND::BOOL(WINAPI* fptrPeekMessageA)(WIND::LPMSG, WIND::HWND, WIND::UINT, WIND::UINT, WIND::UINT);

VOID(WINAPI* fptrRtlLeaveCriticalSection)(WIND::LPCRITICAL_SECTION);

WIND::BOOL(WINAPI* fptrReadFileScatter)(WIND::HANDLE, WIND::FILE_SEGMENT_ELEMENT**, WIND::DWORD, __reserved WIND::LPDWORD,
                                        WIND::LPOVERLAPPED);

WIND::BOOL(WINAPI* fptrWaitCommEvent)(WIND::HANDLE, WIND::LPDWORD, WIND::LPOVERLAPPED);

WIND::LRESULT(WINAPI* fptrSendMessageW)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

int(WSAAPI* fptrWSARecv)(WIND::SOCKET, WIND::LPWSABUF, WIND::DWORD, WIND::LPDWORD, WIND::LPDWORD, WIND::LPWSAOVERLAPPED,
                         WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE);

WIND::LRESULT(WINAPI* fptrSendMessageA)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

WIND::BOOL(WINAPI* fptrWriteFile)(WIND::HANDLE, __in_bcount(nNumberOfBytesToWrite) WIND::LPCVOID, WIND::DWORD, WIND::LPDWORD,
                                  WIND::LPOVERLAPPED);

WIND::DWORD(WINAPI* fptrResumeThread)(WIND::HANDLE);

WIND::RPC_STATUS(WINAPI* fptrRpcMgmtEpEltInqNextA)(WIND::RPC_EP_INQ_HANDLE, WIND::RPC_IF_ID __RPC_FAR*,
                                                   WIND::RPC_BINDING_HANDLE __RPC_FAR*, WIND::UUID __RPC_FAR*,
                                                   __deref_opt_out WIND::RPC_CSTR __RPC_FAR*);

WIND::DWORD(WINAPI* fptrMsgWaitForMultipleObjectsEx)(WIND::DWORD, __in_ecount_opt(nCount) CONST WIND::HANDLE*, WIND::DWORD,
                                                     WIND::DWORD, WIND::DWORD);

long(WINAPI* fptrBroadcastSystemMessageW)(WIND::DWORD, WIND::LPDWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

WIND::BOOL(WINAPI* fptrConnectNamedPipe)(WIND::HANDLE, WIND::LPOVERLAPPED);

VOID(WINAPI* fptrFreeLibraryAndExitThread)(WIND::HMODULE, WIND::DWORD);

VOID(WINAPI* fptrInitializeCriticalSection)(WIND::LPCRITICAL_SECTION);

int(WSAAPI* fptrrecvfrom)(WIND::SOCKET, char FAR*, int, int, struct sockaddr FAR*, int FAR*);

WIND::INT_PTR(WINAPI* fptrDialogBoxIndirectParamW)(WIND::HINSTANCE, WIND::LPCDLGTEMPLATEW, WIND::HWND, WIND::DLGPROC,
                                                   WIND::LPARAM);

WIND::BOOL(WINAPI* fptrDeleteTimerQueue)(WIND::HANDLE);

WIND::DWORD(WINAPI* fptrMsgWaitForMultipleObjects)(WIND::DWORD, __in_ecount_opt(nCount) CONST WIND::HANDLE*, WIND::BOOL,
                                                   WIND::DWORD, WIND::DWORD);

WIND::BOOL(WINAPI* fptrQueueUserWorkItem)(WIND::LPTHREAD_START_ROUTINE, WIND::PVOID, WIND::ULONG);

WIND::HANDLE(WINAPI* fptrConnectToPrinterDlg)(WIND::HWND, WIND::DWORD);

WIND::BOOL(WINAPI* fptrDeviceIoControl)(WIND::HANDLE, WIND::DWORD, __in_bcount_opt(nInBufferSize) WIND::LPVOID, WIND::DWORD,
                                        __out_bcount_part_opt(nOutBufferSize, *lpBytesReturned) WIND::LPVOID, WIND::DWORD,
                                        WIND::LPDWORD, WIND::LPOVERLAPPED);

WIND::SOCKET(WSAAPI* fptrWSASocketA)(int, int, int, WIND::LPWSAPROTOCOL_INFOA, WIND::GROUP, WIND::DWORD);

int(WSAAPI* fptrWSARecvFrom)(WIND::SOCKET, WIND::LPWSABUF, WIND::DWORD, WIND::LPDWORD, WIND::LPDWORD, struct sockaddr FAR*,
                             WIND::LPINT, WIND::LPWSAOVERLAPPED, WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE);

WIND::RPC_STATUS(WINAPI* fptrRpcStringBindingComposeA)(WIND::RPC_CSTR, WIND::RPC_CSTR, WIND::RPC_CSTR, WIND::RPC_CSTR,
                                                       WIND::RPC_CSTR, __deref_opt_out WIND::RPC_CSTR __RPC_FAR*);

WIND::WSAEVENT(WSAAPI* fptrWSACreateEvent)();

VOID(WINAPI* fptrExitProcess)(WIND::UINT);

int(WSAAPI* fptrconnect)(WIND::SOCKET, const struct sockaddr FAR*, int);

WIND::BOOL(WINAPI* fptrGetMessageW)(WIND::LPMSG, WIND::HWND, WIND::UINT, WIND::UINT);

WIND::BOOL(WSAAPI* fptrWSAResetEvent)(WIND::WSAEVENT);

WIND::RPC_STATUS(WINAPI* fptrRpcStringBindingComposeW)(WIND::RPC_WSTR, WIND::RPC_WSTR, WIND::RPC_WSTR, WIND::RPC_WSTR,
                                                       WIND::RPC_WSTR, __deref_opt_out WIND::RPC_WSTR __RPC_FAR*);

WIND::BOOL(APIENTRY* fptrPrintDlgA)(WIND::LPPRINTDLGA);

WIND::HANDLE(WINAPI* fptrOpenWaitableTimerA)(WIND::DWORD, WIND::BOOL, WIND::LPCSTR);

WIND::HHOOK(WINAPI* fptrSetWindowsHookExW)(int, WIND::HOOKPROC, WIND::HINSTANCE, WIND::DWORD);

int(WSAAPI* fptrWSASend)(WIND::SOCKET, WIND::LPWSABUF, WIND::DWORD, WIND::LPDWORD, WIND::DWORD, WIND::LPWSAOVERLAPPED,
                         WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE);

WIND::HANDLE(WINAPI* fptrOpenWaitableTimerW)(WIND::DWORD, WIND::BOOL, WIND::LPCWSTR);

WIND::BOOL(WINAPI* fptrDeleteTimerQueueTimer)(WIND::HANDLE, WIND::HANDLE, WIND::HANDLE);

WIND::RPC_STATUS(WINAPI* fptrRpcNsBindingImportDone)(WIND::RPC_NS_HANDLE __RPC_FAR*);

WIND::BOOL(WINAPI* fptrReadConsoleInputW)(WIND::HANDLE, WIND::PINPUT_RECORD, WIND::DWORD, WIND::LPDWORD);

WIND::HANDLE(WINAPI* fptrCreateMutexA)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::LPCSTR);

WIND::BOOL(WINAPI* fptrRegisterWaitForSingleObject)(WIND::PHANDLE, WIND::HANDLE, WIND::WAITORTIMERCALLBACK, WIND::PVOID,
                                                    WIND::ULONG, WIND::ULONG);

WIND::RPC_STATUS(WINAPI* fptrRpcMgmtInqIfIds)(WIND::RPC_BINDING_HANDLE, WIND::RPC_IF_ID_VECTOR __RPC_FAR* __RPC_FAR*);

WIND::HANDLE(WINAPI* fptrCreateSemaphoreA)(WIND::LPSECURITY_ATTRIBUTES, WIND::LONG, WIND::LONG, WIND::LPCSTR);

WIND::HANDLE(WINAPI* fptrCreateThread)(WIND::LPSECURITY_ATTRIBUTES, WIND::SIZE_T, WIND::LPTHREAD_START_ROUTINE, WIND::LPVOID,
                                       WIND::DWORD, WIND::LPDWORD);

long(WINAPI* fptrBroadcastSystemMessageExW)(WIND::DWORD, WIND::LPDWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM, WIND::PBSMINFO);

WIND::BOOL(WINAPI* fptrCreateProcessAsUserA)(WIND::HANDLE, WIND::LPCSTR, WIND::LPSTR, WIND::LPSECURITY_ATTRIBUTES,
                                             WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::DWORD, WIND::LPVOID, WIND::LPCSTR,
                                             WIND::LPSTARTUPINFOA, WIND::LPPROCESS_INFORMATION);

WIND::HANDLE(WINAPI* fptrCreateSemaphoreW)(WIND::LPSECURITY_ATTRIBUTES, WIND::LONG, WIND::LONG, WIND::LPCWSTR);

WIND::HANDLE(WINAPI* fptrCreateMutexW)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::LPCWSTR);

WIND::RPC_STATUS (*fptrRpcMgmtWaitServerListen)();

VOID(WINAPI* fptrExitThread)(WIND::DWORD);

int(WSAAPI* fptrrecv)(WIND::SOCKET, char FAR*, int, int);

WIND::BOOL(WINAPI* fptrCreateProcessAsUserW)(WIND::HANDLE, WIND::LPCWSTR, WIND::LPWSTR, WIND::LPSECURITY_ATTRIBUTES,
                                             WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::DWORD, WIND::LPVOID, WIND::LPCWSTR,
                                             WIND::LPSTARTUPINFOW, WIND::LPPROCESS_INFORMATION);

VOID(WINAPI* fptrRtlEnterCriticalSection)(WIND::LPCRITICAL_SECTION);

WIND::BOOL(WINAPI* fptrReadConsoleA)(WIND::HANDLE, WIND::LPVOID, WIND::DWORD, WIND::LPDWORD, WIND::LPVOID);

VOID(WINAPI* fptrRtlDeleteCriticalSection)(WIND::LPCRITICAL_SECTION);

WIND::DWORD(WINAPI* fptrWaitForMultipleObjectsEx)(WIND::DWORD, __in_ecount(nCount) CONST WIND::HANDLE*, WIND::BOOL, WIND::DWORD,
                                                  WIND::BOOL);

WIND::BOOL(WINAPI* fptrTerminateProcess)(WIND::HANDLE, WIND::UINT);

WIND::BOOL(WINAPI* fptrFindCloseChangeNotification)(WIND::HANDLE);

WIND::HANDLE(WINAPI* fptrFindFirstPrinterChangeNotification)(WIND::HANDLE, WIND::DWORD, WIND::DWORD, WIND::LPVOID);

WIND::DWORD(WINAPI* fptrWaitForInputIdle)(WIND::HANDLE, WIND::DWORD);

unsigned char*(WINAPI* fptrNdrNsSendReceive)(WIND::PMIDL_STUB_MESSAGE, unsigned char*, WIND::RPC_BINDING_HANDLE*);

WIND::BOOL(WINAPI* fptrFindClosePrinterChangeNotification)(WIND::HANDLE);

WIND::INT_PTR(WINAPI* fptrDialogBoxIndirectParamA)(WIND::HINSTANCE, WIND::LPCDLGTEMPLATEA, WIND::HWND, WIND::DLGPROC,
                                                   WIND::LPARAM);

WIND::BOOL(WINAPI* fptrReadConsoleW)(WIND::HANDLE, WIND::LPVOID, WIND::DWORD, WIND::LPDWORD, WIND::LPVOID);

WIND::BOOL(WINAPI* fptrGetMessageA)(WIND::LPMSG, WIND::HWND, WIND::UINT, WIND::UINT);

WIND::BOOL(WINAPI* fptrSetWaitableTimer)(WIND::HANDLE, const WIND::LARGE_INTEGER*, WIND::LONG, WIND::PTIMERAPCROUTINE,
                                         WIND::LPVOID, WIND::BOOL);

WIND::DWORD(WINAPI* fptrSleepEx)(WIND::DWORD, WIND::BOOL);

WIND::HMODULE(WINAPI* fptrLoadLibraryExW)(WIND::LPCWSTR, __reserved WIND::HANDLE, unsigned long);

WIND::BOOL(WINAPI* fptrCallNamedPipeW)(WIND::LPCWSTR, __in_bcount_opt(nInBufferSize) WIND::LPVOID, WIND::DWORD,
                                       __out_bcount_part_opt(nOutBufferSize, *lpBytesRead) WIND::LPVOID, WIND::DWORD,
                                       WIND::LPDWORD, WIND::DWORD);

WIND::BOOL(WINAPI* fptrSendNotifyMessageA)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

WIND::BOOL(WINAPI* fptrInitializeCriticalSectionAndSpinCount)(WIND::LPCRITICAL_SECTION, WIND::DWORD);

WIND::BOOL(APIENTRY* fptrPrintDlgW)(WIND::LPPRINTDLGW);

WIND::RPC_STATUS(WINAPI* fptrRpcMgmtEpEltInqNextW)(WIND::RPC_EP_INQ_HANDLE, WIND::RPC_IF_ID __RPC_FAR*,
                                                   WIND::RPC_BINDING_HANDLE __RPC_FAR*, WIND::UUID __RPC_FAR*,
                                                   __deref_opt_out WIND::RPC_WSTR __RPC_FAR*);

WIND::RPC_STATUS(WINAPI* fptrRpcMgmtEpEltInqDone)(WIND::RPC_EP_INQ_HANDLE __RPC_FAR*);

WIND::BOOL(WINAPI* fptrPostMessageA)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

WIND::BOOL(WINAPI* fptrTerminateThread)(WIND::HANDLE, WIND::DWORD);

WIND::HMODULE(WINAPI* fptrLoadLibraryW)(WIND::LPCWSTR);

WIND::HANDLE(WINAPI* fptrCreateMailslotW)(WIND::LPCWSTR, WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES);

WIND::BOOL(WINAPI* fptrSetEvent)(WIND::HANDLE);

WIND::BOOL(WINAPI* fptrReadConsoleInputA)(WIND::HANDLE, WIND::PINPUT_RECORD, WIND::DWORD, WIND::LPDWORD);

WIND::BOOL(WINAPI* fptrCreateProcessWithLogonW)(WIND::LPCWSTR, WIND::LPCWSTR, WIND::LPCWSTR, WIND::DWORD, WIND::LPCWSTR,
                                                WIND::LPWSTR, WIND::DWORD, WIND::LPVOID, WIND::LPCWSTR, WIND::LPSTARTUPINFOW,
                                                WIND::LPPROCESS_INFORMATION);

WIND::BOOL(WINAPI* fptrCallNamedPipeA)(WIND::LPCSTR, __in_bcount_opt(nInBufferSize) WIND::LPVOID, WIND::DWORD,
                                       __out_bcount_part_opt(nOutBufferSize, *lpBytesRead) WIND::LPVOID, WIND::DWORD,
                                       WIND::LPDWORD, WIND::DWORD);

WIND::BOOL(WINAPI* fptrSendNotifyMessageW)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

WIND::HANDLE(WINAPI* fptrCreateMailslotA)(WIND::LPCSTR, WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES);

int(WSAAPI* fptrselect)(int, WIND::fd_set FAR*, WIND::fd_set FAR*, WIND::fd_set FAR*, const struct timeval FAR*);

WIND::HANDLE(WINAPI* fptrCreateRemoteThread)(WIND::HANDLE, WIND::LPSECURITY_ATTRIBUTES, WIND::SIZE_T,
                                             WIND::LPTHREAD_START_ROUTINE, WIND::LPVOID, WIND::DWORD, WIND::LPDWORD);

WIND::HHOOK(WINAPI* fptrSetWindowsHookExA)(int, WIND::HOOKPROC, WIND::HINSTANCE, WIND::DWORD);

long(WINAPI* fptrBroadcastSystemMessageExA)(WIND::DWORD, WIND::LPDWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM, WIND::PBSMINFO);

WIND::HANDLE(WINAPI* fptrOpenProcess)(WIND::DWORD, WIND::BOOL, WIND::DWORD);

WIND::DWORD(WSAAPI* fptrWSAWaitForMultipleEvents)(WIND::DWORD, const WIND::WSAEVENT FAR*, WIND::BOOL, WIND::DWORD, WIND::BOOL);

WIND::RPC_STATUS(WINAPI* fptrRpcMgmtEpEltInqBegin)(WIND::RPC_BINDING_HANDLE, unsigned long, WIND::RPC_IF_ID __RPC_FAR*,
                                                   unsigned long, WIND::UUID __RPC_FAR*, WIND::RPC_EP_INQ_HANDLE __RPC_FAR*);

WIND::BOOL(WINAPI* fptrWriteFileGather)(WIND::HANDLE, WIND::FILE_SEGMENT_ELEMENT**, WIND::DWORD, __reserved WIND::LPDWORD,
                                        WIND::LPOVERLAPPED);

WIND::DWORD(WINAPI* fptrWaitForMultipleObjects)(WIND::DWORD, __in_ecount(nCount) CONST WIND::HANDLE*, WIND::BOOL, WIND::DWORD);

unsigned char*(WINAPI* fptrNdrSendReceive)(WIND::PMIDL_STUB_MESSAGE, unsigned char*);

WIND::HANDLE(WINAPI* fptrCreateWaitableTimerW)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::LPCWSTR);

WIND::BOOL(WINAPI* fptrWaitMessage)();

WIND::DWORD(WINAPI* fptrSetCriticalSectionSpinCount)(WIND::LPCRITICAL_SECTION, WIND::DWORD);

WIND::BOOL(WINAPI* fptrWaitNamedPipeW)(WIND::LPCWSTR, WIND::DWORD);

int(WSAAPI* fptrsendto)(WIND::SOCKET, const char FAR*, int, int, const struct sockaddr FAR*, int);

WIND::BOOL(WINAPI* fptrWaitNamedPipeA)(WIND::LPCSTR, WIND::DWORD);

WIND::BOOL(WINAPI* fptrResetEvent)(WIND::HANDLE);

WIND::BOOL(WINAPI* fptrCreateTimerQueueTimer)(WIND::PHANDLE, WIND::HANDLE, WIND::WAITORTIMERCALLBACK, WIND::PVOID, WIND::DWORD,
                                              WIND::DWORD, WIND::ULONG);

WIND::HANDLE(WINAPI* fptrCreateWaitableTimerA)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::LPCSTR);

WIND::BOOL(WINAPI* fptrDuplicateHandle)(WIND::HANDLE, WIND::HANDLE, WIND::HANDLE, WIND::LPHANDLE, WIND::DWORD, WIND::BOOL,
                                        WIND::DWORD);

int(WSAAPI* fptrclosesocket)(WIND::SOCKET);

WIND::FARPROC(WINAPI* fptrGetProcAddress)(WIND::HMODULE, WIND::LPCSTR);

WIND::SOCKET(WSAAPI* fptrsocket)(int, int, int);

WIND::BOOL(WSAAPI* fptrWSASetEvent)(WIND::WSAEVENT);

WIND::HANDLE(WINAPI* fptrCreateEventW)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::BOOL, WIND::LPCWSTR);

WIND::BOOL(WINAPI* fptrPostThreadMessageW)(WIND::DWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

int(WSAAPI* fptrWSASendTo)(WIND::SOCKET, WIND::LPWSABUF, WIND::DWORD, WIND::LPDWORD, WIND::DWORD, const struct sockaddr FAR*, int,
                           WIND::LPWSAOVERLAPPED, WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE);

WIND::HANDLE(WINAPI* fptrCreateFileW)(WIND::LPCWSTR, WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES, WIND::DWORD,
                                      WIND::DWORD, WIND::HANDLE);

WIND::HANDLE(WINAPI* fptrCreateEventA)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::BOOL, WIND::LPCSTR);

WIND::RPC_STATUS(WINAPI* fptrRpcServerListen)(unsigned int, unsigned int, unsigned int);

WIND::HANDLE(WINAPI* fptrCreateFileA)(WIND::LPCSTR, WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES, WIND::DWORD,
                                      WIND::DWORD, WIND::HANDLE);

WIND::BOOL(WINAPI* fptrDeleteTimerQueueEx)(WIND::HANDLE, WIND::HANDLE);

WIND::LRESULT(WINAPI* fptrSendMessageTimeoutW)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM, WIND::UINT, WIND::UINT,
                                               WIND::PDWORD_PTR);

WIND::BOOL(WINAPI* fptrPostThreadMessageA)(WIND::DWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

WIND::SOCKET(WSAAPI* fptrWSASocketW)(int, int, int, WIND::LPWSAPROTOCOL_INFOW, WIND::GROUP, WIND::DWORD);

WIND::BOOL(WSAAPI* fptrWSAGetOverlappedResult)(WIND::SOCKET, WIND::LPWSAOVERLAPPED, WIND::LPDWORD, WIND::BOOL, WIND::LPDWORD);

int(WSAAPI* fptrWSAConnect)(WIND::SOCKET, const struct sockaddr FAR*, int, WIND::LPWSABUF, WIND::LPWSABUF, WIND::LPQOS,
                            WIND::LPQOS);

WIND::HANDLE(WINAPI* fptrFindFirstChangeNotificationA)(WIND::LPCSTR, WIND::BOOL, WIND::DWORD);

WIND::HANDLE(WINAPI* fptrCreateNamedPipeW)(WIND::LPCWSTR, WIND::DWORD, WIND::DWORD, WIND::DWORD, WIND::DWORD, WIND::DWORD,
                                           WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES);

WIND::SOCKET(WSAAPI* fptraccept)(WIND::SOCKET, struct sockaddr FAR*, int FAR*);

WIND::HANDLE(WINAPI* fptrOpenSemaphoreA)(WIND::DWORD, WIND::BOOL, WIND::LPCSTR);

WIND::BOOL(WINAPI* fptrUnregisterWaitEx)(WIND::HANDLE, WIND::HANDLE);

WIND::RPC_STATUS(WINAPI* fptrRpcMgmtIsServerListening)(WIND::RPC_BINDING_HANDLE);

WIND::DWORD(WINAPI* fptrWaitForSingleObjectEx)(WIND::HANDLE, WIND::DWORD, WIND::BOOL);

WIND::HANDLE(WINAPI* fptrCreateNamedPipeA)(WIND::LPCSTR, WIND::DWORD, WIND::DWORD, WIND::DWORD, WIND::DWORD, WIND::DWORD,
                                           WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES);

WIND::HANDLE(WINAPI* fptrFindFirstChangeNotificationW)(WIND::LPCWSTR, WIND::BOOL, WIND::DWORD);

int(WINAPI* fptrMessageBoxExA)(WIND::HWND, WIND::LPCSTR, WIND::LPCSTR, WIND::UINT, WIND::WORD);

WIND::BOOL(WINAPI* fptrGetQueuedCompletionStatus)(WIND::HANDLE, WIND::LPDWORD, WIND::PULONG_PTR, WIND::LPOVERLAPPED, WIND::DWORD);

WIND::BOOL(WINAPI* fptrCancelWaitableTimer)(WIND::HANDLE);

int(WINAPI* fptrMessageBoxW)(WIND::HWND, WIND::LPCWSTR, WIND::LPCWSTR, WIND::UINT);

WIND::SOCKET(WSAAPI* fptrWSAAccept)(WIND::SOCKET, struct sockaddr FAR*, WIND::LPINT, WIND::LPCONDITIONPROC, WIND::DWORD_PTR);

WIND::BOOL(WINAPI* fptrUnregisterWait)(WIND::HANDLE);

WIND::BOOL(WSAAPI* fptrWSACloseEvent)(WIND::WSAEVENT);

WIND::HANDLE(WINAPI* fptrCreateIoCompletionPort)(WIND::HANDLE, WIND::HANDLE, WIND::ULONG_PTR, WIND::DWORD);

WIND::INT_PTR(WINAPI* fptrDialogBoxParamW)(WIND::HINSTANCE, WIND::LPCWSTR, WIND::HWND, WIND::DLGPROC, WIND::LPARAM);

int(WINAPI* fptrMessageBoxA)(WIND::HWND, WIND::LPCSTR, WIND::LPCSTR, WIND::UINT);

int(WINAPI* fptrMessageBoxExW)(WIND::HWND, WIND::LPCWSTR, WIND::LPCWSTR, WIND::UINT, WIND::WORD);

WIND::INT_PTR(WINAPI* fptrDialogBoxParamA)(WIND::HINSTANCE, WIND::LPCSTR, WIND::HWND, WIND::DLGPROC, WIND::LPARAM);

VOID(WINAPI* fptrSleep)(WIND::DWORD);

WIND::RPC_STATUS(WINAPI* fptrRpcNsBindingLookupDone)(WIND::RPC_NS_HANDLE __RPC_FAR*);

WIND::HANDLE(WINAPI* fptrOpenMutexA)(WIND::DWORD, WIND::BOOL, WIND::LPCSTR);

WIND::DWORD(WINAPI* fptrSuspendThread)(WIND::HANDLE);

VOID(WINAPI* fptrRaiseException)
(WIND::DWORD, WIND::DWORD, WIND::DWORD, __in_ecount_opt(nNumberOfArguments) CONST WIND::ULONG_PTR*);

WIND::RPC_STATUS(WINAPI* fptrRpcNsBindingImportBeginW)(unsigned long, WIND::RPC_WSTR, WIND::RPC_IF_HANDLE, WIND::UUID __RPC_FAR*,
                                                       WIND::RPC_NS_HANDLE __RPC_FAR*);

WIND::BOOL(WINAPI* fptrReleaseSemaphore)(WIND::HANDLE, WIND::LONG, WIND::LPLONG);

WIND::HFILE(WINAPI* fptrOpenFile)(WIND::LPCSTR, WIND::LPOFSTRUCT, WIND::UINT);

WIND::BOOL(WINAPI* fptrReadFile)(WIND::HANDLE, __out_bcount_part(nNumberOfBytesToRead, *lpNumberOfBytesRead) WIND::LPVOID,
                                 WIND::DWORD, WIND::LPDWORD, WIND::LPOVERLAPPED);

WIND::BOOL(WINAPI* fptrWriteFileEx)(WIND::HANDLE, __in_bcount(nNumberOfBytesToWrite) WIND::LPCVOID, WIND::DWORD,
                                    WIND::LPOVERLAPPED, WIND::LPOVERLAPPED_COMPLETION_ROUTINE);

WIND::RPC_STATUS(WINAPI* fptrRpcNsBindingImportBeginA)(unsigned long, WIND::RPC_CSTR, WIND::RPC_IF_HANDLE, WIND::UUID __RPC_FAR*,
                                                       WIND::RPC_NS_HANDLE __RPC_FAR*);

WIND::BOOL(WINAPI* fptrPulseEvent)(WIND::HANDLE);

WIND::RPC_STATUS(WINAPI* fptrRpcCancelThread)(void*);

WIND::BOOL(WINAPI* fptrCloseHandle)(WIND::HANDLE);

WIND::HANDLE(WINAPI* fptrOpenMutexW)(WIND::DWORD, WIND::BOOL, WIND::LPCWSTR);

WIND::BOOL(WINAPI* fptrPostMessageW)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM);

WIND::RPC_STATUS(WINAPI* fptrRpcEpResolveBinding)(WIND::RPC_BINDING_HANDLE, WIND::RPC_IF_HANDLE);

WIND::BOOL(WINAPI* fptrBindIoCompletionCallback)(WIND::HANDLE, WIND::LPOVERLAPPED_COMPLETION_ROUTINE, WIND::ULONG);

WIND::BOOL(WINAPI* fptrPeekMessageW)(WIND::LPMSG, WIND::HWND, WIND::UINT, WIND::UINT, WIND::UINT);

WIND::LRESULT(WINAPI* fptrSendMessageTimeoutA)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM, WIND::UINT, WIND::UINT,
                                               WIND::PDWORD_PTR);

WIND::HANDLE(WINAPI* fptrOpenEventA)(WIND::DWORD, WIND::BOOL, WIND::LPCSTR);

WIND::BOOL(WINAPI* fptrCreateProcessA)(WIND::LPCSTR, WIND::LPSTR, WIND::LPSECURITY_ATTRIBUTES, WIND::LPSECURITY_ATTRIBUTES,
                                       WIND::BOOL, WIND::DWORD, WIND::LPVOID, WIND::LPCSTR, WIND::LPSTARTUPINFOA,
                                       WIND::LPPROCESS_INFORMATION);

WIND::HANDLE(WINAPI* fptrOpenEventW)(WIND::DWORD, WIND::BOOL, WIND::LPCWSTR);

WIND::BOOL(WINAPI* fptrPostQueuedCompletionStatus)(WIND::HANDLE, WIND::DWORD, WIND::ULONG_PTR, WIND::LPOVERLAPPED);

WIND::BOOL(WINAPI* fptrCreateProcessW)(WIND::LPCWSTR, WIND::LPWSTR, WIND::LPSECURITY_ATTRIBUTES, WIND::LPSECURITY_ATTRIBUTES,
                                       WIND::BOOL, WIND::DWORD, WIND::LPVOID, WIND::LPCWSTR, WIND::LPSTARTUPINFOW,
                                       WIND::LPPROCESS_INFORMATION);

WIND::BOOL(WINAPI* fptrTransactNamedPipe)(WIND::HANDLE, __in_bcount_opt(nInBufferSize) WIND::LPVOID, WIND::DWORD,
                                          __out_bcount_part_opt(nOutBufferSize, *lpBytesRead) WIND::LPVOID, WIND::DWORD,
                                          WIND::LPDWORD, WIND::LPOVERLAPPED);

WIND::BOOL(WINAPI* fptrReadFileEx)(WIND::HANDLE, __out_bcount(nNumberOfBytesToRead) WIND::LPVOID, WIND::DWORD, WIND::LPOVERLAPPED,
                                   WIND::LPOVERLAPPED_COMPLETION_ROUTINE);

WIND::HANDLE(WINAPI* fptrOpenSemaphoreW)(WIND::DWORD, WIND::BOOL, WIND::LPCWSTR);

WIND::BOOL(WINAPI* fptrReplyMessage)(WIND::LRESULT);

typedef int* INT_PTR;

//globals section footer

//functions section start

std::string CurrentTime()

{
    // Utility function to return the time

    char tmpbuf[128];
    _strtime_s(tmpbuf, 128);
    return std::string("[") + std::string(tmpbuf) + std::string("] ");
}

typedef int(__cdecl* DO_LOOP_TYPE)();

static volatile int doLoopPred = 1;

static volatile int globalCounter = 0;

static volatile int attachCycles = 0;

int rep_DoLoop()
{
    PIN_LockClient();

    int localPred = doLoopPred;

    PIN_UnlockClient();

    return localPred;
}

WIND::HRESULT APIENTRY myPrintDlgExW(WIND::LPPRINTDLGEXW lpDlg)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PrintDlgExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HRESULT res = fptrPrintDlgExW(lpDlg);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PrintDlgExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcNsBindingLookupBeginW(unsigned long EntryNameSyntax, WIND::RPC_WSTR EntryName,
                                                   WIND::RPC_IF_HANDLE IfSpec, WIND::UUID __RPC_FAR* ObjUuid,
                                                   unsigned long BindingMaxCount, WIND::RPC_NS_HANDLE __RPC_FAR* LookupContext)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcNsBindingLookupBeginW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res =
        fptrRpcNsBindingLookupBeginW(EntryNameSyntax, EntryName, IfSpec, ObjUuid, BindingMaxCount, LookupContext);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcNsBindingLookupBeginW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myGetStdHandle(WIND::DWORD hStdHandle)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering GetStdHandle" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrGetStdHandle(hStdHandle);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving GetStdHandle" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myReleaseMutex(WIND::HANDLE hMutex)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReleaseMutex" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReleaseMutex(hMutex);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReleaseMutex" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myGetOverlappedResult(WIND::HANDLE hFile, WIND::LPOVERLAPPED lpOverlapped,
                                        WIND::LPDWORD lpNumberOfBytesTransferred, WIND::BOOL bWait)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering GetOverlappedResult" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrGetOverlappedResult(hFile, lpOverlapped, lpNumberOfBytesTransferred, bWait);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving GetOverlappedResult" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI myWaitForSingleObject(WIND::HANDLE hHandle, WIND::DWORD dwMilliseconds)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WaitForSingleObject" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrWaitForSingleObject(hHandle, dwMilliseconds);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WaitForSingleObject" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

long WINAPI myBroadcastSystemMessageA(WIND::DWORD flags, WIND::LPDWORD lpInfo, WIND::UINT Msg, WIND::WPARAM wParam,
                                      WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering BroadcastSystemMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    long res = fptrBroadcastSystemMessageA(flags, lpInfo, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving BroadcastSystemMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI mySignalObjectAndWait(WIND::HANDLE hObjectToSignal, WIND::HANDLE hObjectToWaitOn, WIND::DWORD dwMilliseconds,
                                         WIND::BOOL bAlertable)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SignalObjectAndWait" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrSignalObjectAndWait(hObjectToSignal, hObjectToWaitOn, dwMilliseconds, bAlertable);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SignalObjectAndWait" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcNsBindingLookupBeginA(unsigned long EntryNameSyntax, WIND::RPC_CSTR EntryName,
                                                   WIND::RPC_IF_HANDLE IfSpec, WIND::UUID __RPC_FAR* ObjUuid,
                                                   unsigned long BindingMaxCount, WIND::RPC_NS_HANDLE __RPC_FAR* LookupContext)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcNsBindingLookupBeginA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res =
        fptrRpcNsBindingLookupBeginA(EntryNameSyntax, EntryName, IfSpec, ObjUuid, BindingMaxCount, LookupContext);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcNsBindingLookupBeginA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HRESULT APIENTRY myPrintDlgExA(WIND::LPPRINTDLGEXA lpDlg)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PrintDlgExA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HRESULT res = fptrPrintDlgExA(lpDlg);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PrintDlgExA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateTimerQueue()

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateTimerQueue" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateTimerQueue();

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateTimerQueue" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcNsBindingImportNext(WIND::RPC_NS_HANDLE ImportContext, WIND::RPC_BINDING_HANDLE __RPC_FAR* Binding)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcNsBindingImportNext" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcNsBindingImportNext(ImportContext, Binding);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcNsBindingImportNext" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI myQueueUserAPC(WIND::PAPCFUNC pfnAPC, WIND::HANDLE hThread, WIND::ULONG_PTR dwData)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering QueueUserAPC" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrQueueUserAPC(pfnAPC, hThread, dwData);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving QueueUserAPC" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcNsBindingLookupNext(WIND::RPC_NS_HANDLE LookupContext,
                                                 WIND::RPC_BINDING_VECTOR __RPC_FAR* __RPC_FAR* BindingVec)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcNsBindingLookupNext" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcNsBindingLookupNext(LookupContext, BindingVec);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcNsBindingLookupNext" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WSAAPI mysend(WIND::SOCKET s, const char FAR* buf, int len, int flags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering send" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrsend(s, buf, len, flags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving send" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myPeekMessageA(WIND::LPMSG lpMsg, WIND::HWND hWnd, WIND::UINT wMsgFilterMin, WIND::UINT wMsgFilterMax,
                                 WIND::UINT wRemoveMsg)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PeekMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PeekMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

VOID WINAPI myRtlLeaveCriticalSection(WIND::LPCRITICAL_SECTION lpCriticalSection)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RtlLeaveCriticalSection" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    fptrRtlLeaveCriticalSection(lpCriticalSection);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RtlLeaveCriticalSection" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);
}

WIND::BOOL WINAPI myReadFileScatter(WIND::HANDLE hFile, WIND::FILE_SEGMENT_ELEMENT** aSegmentArray,
                                    WIND::DWORD nNumberOfBytesToRead, __reserved WIND::LPDWORD lpReserved,
                                    WIND::LPOVERLAPPED lpOverlapped)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReadFileScatter" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReadFileScatter(hFile, aSegmentArray, nNumberOfBytesToRead, lpReserved, lpOverlapped);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReadFileScatter" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myWaitCommEvent(WIND::HANDLE hFile, WIND::LPDWORD lpEvtMask, WIND::LPOVERLAPPED lpOverlapped)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WaitCommEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWaitCommEvent(hFile, lpEvtMask, lpOverlapped);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WaitCommEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::LRESULT WINAPI mySendMessageW(WIND::HWND hWnd, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SendMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::LRESULT res = fptrSendMessageW(hWnd, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SendMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WSAAPI myWSARecv(WIND::SOCKET s, WIND::LPWSABUF lpBuffers, WIND::DWORD dwBufferCount, WIND::LPDWORD lpNumberOfBytesRecvd,
                     WIND::LPDWORD lpFlags, WIND::LPWSAOVERLAPPED lpOverlapped,
                     WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSARecv" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrWSARecv(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpOverlapped, lpCompletionRoutine);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSARecv" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::LRESULT WINAPI mySendMessageA(WIND::HWND hWnd, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SendMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::LRESULT res = fptrSendMessageA(hWnd, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SendMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myWriteFile(WIND::HANDLE hFile, __in_bcount(nNumberOfBytesToWrite) WIND::LPCVOID lpBuffer,
                              WIND::DWORD nNumberOfBytesToWrite, WIND::LPDWORD lpNumberOfBytesWritten,
                              WIND::LPOVERLAPPED lpOverlapped)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WriteFile" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWriteFile(hFile, lpBuffer, nNumberOfBytesToWrite, lpNumberOfBytesWritten, lpOverlapped);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WriteFile" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI myResumeThread(WIND::HANDLE hThread)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ResumeThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrResumeThread(hThread);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ResumeThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcMgmtEpEltInqNextA(WIND::RPC_EP_INQ_HANDLE InquiryContext, WIND::RPC_IF_ID __RPC_FAR* IfId,
                                               WIND::RPC_BINDING_HANDLE __RPC_FAR* Binding, WIND::UUID __RPC_FAR* ObjectUuid,
                                               __deref_opt_out WIND::RPC_CSTR __RPC_FAR* Annotation)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcMgmtEpEltInqNextA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcMgmtEpEltInqNextA(InquiryContext, IfId, Binding, ObjectUuid, Annotation);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcMgmtEpEltInqNextA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI myMsgWaitForMultipleObjectsEx(WIND::DWORD nCount, __in_ecount_opt(nCount) CONST WIND::HANDLE* pHandles,
                                                 WIND::DWORD dwMilliseconds, WIND::DWORD dwWakeMask, WIND::DWORD dwFlags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering MsgWaitForMultipleObjectsEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrMsgWaitForMultipleObjectsEx(nCount, pHandles, dwMilliseconds, dwWakeMask, dwFlags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving MsgWaitForMultipleObjectsEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

long WINAPI myBroadcastSystemMessageW(WIND::DWORD flags, WIND::LPDWORD lpInfo, WIND::UINT Msg, WIND::WPARAM wParam,
                                      WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering BroadcastSystemMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    long res = fptrBroadcastSystemMessageW(flags, lpInfo, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving BroadcastSystemMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myConnectNamedPipe(WIND::HANDLE hNamedPipe, WIND::LPOVERLAPPED lpOverlapped)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ConnectNamedPipe" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrConnectNamedPipe(hNamedPipe, lpOverlapped);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ConnectNamedPipe" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

VOID WINAPI myFreeLibraryAndExitThread(WIND::HMODULE hLibModule, WIND::DWORD dwExitCode)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering FreeLibraryAndExitThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    fptrFreeLibraryAndExitThread(hLibModule, dwExitCode);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving FreeLibraryAndExitThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);
}

VOID WINAPI myInitializeCriticalSection(WIND::LPCRITICAL_SECTION lpCriticalSection)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering InitializeCriticalSection" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    fptrInitializeCriticalSection(lpCriticalSection);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving InitializeCriticalSection" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);
}

int WSAAPI myrecvfrom(WIND::SOCKET s, char FAR* buf, int len, int flags, struct sockaddr FAR* from, int FAR* fromlen)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering recvfrom" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrrecvfrom(s, buf, len, flags, from, fromlen);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving recvfrom" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::INT_PTR WINAPI myDialogBoxIndirectParamW(WIND::HINSTANCE hInstance, WIND::LPCDLGTEMPLATEW hDialogTemplate,
                                               WIND::HWND hWndParent, WIND::DLGPROC lpDialogFunc, WIND::LPARAM dwInitParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering DialogBoxIndirectParamW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::INT_PTR res = fptrDialogBoxIndirectParamW(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving DialogBoxIndirectParamW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myDeleteTimerQueue(WIND::HANDLE TimerQueue)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering DeleteTimerQueue" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrDeleteTimerQueue(TimerQueue);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving DeleteTimerQueue" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI myMsgWaitForMultipleObjects(WIND::DWORD nCount, __in_ecount_opt(nCount) CONST WIND::HANDLE* pHandles,
                                               WIND::BOOL fWaitAll, WIND::DWORD dwMilliseconds, WIND::DWORD dwWakeMask)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering MsgWaitForMultipleObjects" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrMsgWaitForMultipleObjects(nCount, pHandles, fWaitAll, dwMilliseconds, dwWakeMask);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving MsgWaitForMultipleObjects" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myQueueUserWorkItem(WIND::LPTHREAD_START_ROUTINE Function, WIND::PVOID Context, WIND::ULONG Flags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering QueueUserWorkItem" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrQueueUserWorkItem(Function, Context, Flags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving QueueUserWorkItem" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myConnectToPrinterDlg(WIND::HWND hwnd, WIND::DWORD Flags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ConnectToPrinterDlg" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrConnectToPrinterDlg(hwnd, Flags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ConnectToPrinterDlg" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myDeviceIoControl(WIND::HANDLE hDevice, WIND::DWORD dwIoControlCode,
                                    __in_bcount_opt(nInBufferSize) WIND::LPVOID lpInBuffer, WIND::DWORD nInBufferSize,
                                    __out_bcount_part_opt(nOutBufferSize, *lpBytesReturned) WIND::LPVOID lpOutBuffer,
                                    WIND::DWORD nOutBufferSize, WIND::LPDWORD lpBytesReturned, WIND::LPOVERLAPPED lpOverlapped)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering DeviceIoControl" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrDeviceIoControl(hDevice, dwIoControlCode, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize,
                                         lpBytesReturned, lpOverlapped);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving DeviceIoControl" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::SOCKET WSAAPI myWSASocketA(int af, int type, int protocol, WIND::LPWSAPROTOCOL_INFOA lpProtocolInfo, WIND::GROUP g,
                                 WIND::DWORD dwFlags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSASocketA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::SOCKET res = fptrWSASocketA(af, type, protocol, lpProtocolInfo, g, dwFlags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSASocketA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WSAAPI myWSARecvFrom(WIND::SOCKET s, WIND::LPWSABUF lpBuffers, WIND::DWORD dwBufferCount, WIND::LPDWORD lpNumberOfBytesRecvd,
                         WIND::LPDWORD lpFlags, struct sockaddr FAR* lpFrom, WIND::LPINT lpFromlen,
                         WIND::LPWSAOVERLAPPED lpOverlapped, WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSARecvFrom" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrWSARecvFrom(s, lpBuffers, dwBufferCount, lpNumberOfBytesRecvd, lpFlags, lpFrom, lpFromlen, lpOverlapped,
                              lpCompletionRoutine);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSARecvFrom" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcStringBindingComposeA(WIND::RPC_CSTR ObjUuid, WIND::RPC_CSTR Protseq, WIND::RPC_CSTR NetworkAddr,
                                                   WIND::RPC_CSTR Endpoint, WIND::RPC_CSTR Options,
                                                   __deref_opt_out WIND::RPC_CSTR __RPC_FAR* StringBinding)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcStringBindingComposeA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcStringBindingComposeA(ObjUuid, Protseq, NetworkAddr, Endpoint, Options, StringBinding);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcStringBindingComposeA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::WSAEVENT WSAAPI myWSACreateEvent()

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSACreateEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::WSAEVENT res = fptrWSACreateEvent();

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSACreateEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

VOID WINAPI myExitProcess(WIND::UINT uExitCode)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ExitProcess" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    fptrExitProcess(uExitCode);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ExitProcess" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);
}

int WSAAPI myconnect(WIND::SOCKET s, const struct sockaddr FAR* name, int namelen)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering connect" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrconnect(s, name, namelen);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving connect" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myGetMessageW(WIND::LPMSG lpMsg, WIND::HWND hWnd, WIND::UINT wMsgFilterMin, WIND::UINT wMsgFilterMax)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering GetMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrGetMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving GetMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WSAAPI myWSAResetEvent(WIND::WSAEVENT hEvent)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSAResetEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWSAResetEvent(hEvent);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSAResetEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcStringBindingComposeW(WIND::RPC_WSTR ObjUuid, WIND::RPC_WSTR Protseq, WIND::RPC_WSTR NetworkAddr,
                                                   WIND::RPC_WSTR Endpoint, WIND::RPC_WSTR Options,
                                                   __deref_opt_out WIND::RPC_WSTR __RPC_FAR* StringBinding)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcStringBindingComposeW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcStringBindingComposeW(ObjUuid, Protseq, NetworkAddr, Endpoint, Options, StringBinding);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcStringBindingComposeW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL APIENTRY myPrintDlgA(WIND::LPPRINTDLGA lpDlg)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PrintDlgA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPrintDlgA(lpDlg);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PrintDlgA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myOpenWaitableTimerA(WIND::DWORD dwDesiredAccess, WIND::BOOL bInheritHandle, WIND::LPCSTR lpTimerName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenWaitableTimerA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrOpenWaitableTimerA(dwDesiredAccess, bInheritHandle, lpTimerName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenWaitableTimerA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HHOOK WINAPI mySetWindowsHookExW(int idHook, WIND::HOOKPROC lpfn, WIND::HINSTANCE hmod, WIND::DWORD dwThreadId)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SetWindowsHookExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HHOOK res = fptrSetWindowsHookExW(idHook, lpfn, hmod, dwThreadId);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SetWindowsHookExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WSAAPI myWSASend(WIND::SOCKET s, WIND::LPWSABUF lpBuffers, WIND::DWORD dwBufferCount, WIND::LPDWORD lpNumberOfBytesSent,
                     WIND::DWORD dwFlags, WIND::LPWSAOVERLAPPED lpOverlapped,
                     WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSASend" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrWSASend(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpOverlapped, lpCompletionRoutine);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSASend" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myOpenWaitableTimerW(WIND::DWORD dwDesiredAccess, WIND::BOOL bInheritHandle, WIND::LPCWSTR lpTimerName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenWaitableTimerW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrOpenWaitableTimerW(dwDesiredAccess, bInheritHandle, lpTimerName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenWaitableTimerW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myDeleteTimerQueueTimer(WIND::HANDLE TimerQueue, WIND::HANDLE Timer, WIND::HANDLE CompletionEvent)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering DeleteTimerQueueTimer" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrDeleteTimerQueueTimer(TimerQueue, Timer, CompletionEvent);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving DeleteTimerQueueTimer" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcNsBindingImportDone(WIND::RPC_NS_HANDLE __RPC_FAR* ImportContext)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcNsBindingImportDone" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcNsBindingImportDone(ImportContext);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcNsBindingImportDone" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myReadConsoleInputW(WIND::HANDLE hConsoleInput, WIND::PINPUT_RECORD lpBuffer, WIND::DWORD nLength,
                                      WIND::LPDWORD lpNumberOfEventsRead)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReadConsoleInputW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReadConsoleInputW(hConsoleInput, lpBuffer, nLength, lpNumberOfEventsRead);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReadConsoleInputW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateMutexA(WIND::LPSECURITY_ATTRIBUTES lpMutexAttributes, WIND::BOOL bInitialOwner, WIND::LPCSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateMutexA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateMutexA(lpMutexAttributes, bInitialOwner, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateMutexA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myRegisterWaitForSingleObject(WIND::PHANDLE phNewWaitObject, WIND::HANDLE hObject,
                                                WIND::WAITORTIMERCALLBACK Callback, WIND::PVOID Context,
                                                WIND::ULONG dwMilliseconds, WIND::ULONG dwFlags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RegisterWaitForSingleObject" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrRegisterWaitForSingleObject(phNewWaitObject, hObject, Callback, Context, dwMilliseconds, dwFlags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RegisterWaitForSingleObject" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcMgmtInqIfIds(WIND::RPC_BINDING_HANDLE Binding,
                                          WIND::RPC_IF_ID_VECTOR __RPC_FAR* __RPC_FAR* IfIdVector)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcMgmtInqIfIds" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcMgmtInqIfIds(Binding, IfIdVector);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcMgmtInqIfIds" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateSemaphoreA(WIND::LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, WIND::LONG lInitialCount,
                                       WIND::LONG lMaximumCount, WIND::LPCSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateSemaphoreA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateSemaphoreA(lpSemaphoreAttributes, lInitialCount, lMaximumCount, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateSemaphoreA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateThread(WIND::LPSECURITY_ATTRIBUTES lpThreadAttributes, WIND::SIZE_T dwStackSize,
                                   WIND::LPTHREAD_START_ROUTINE lpStartAddress, WIND::LPVOID lpParameter,
                                   WIND::DWORD dwCreationFlags, WIND::LPDWORD lpThreadId)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res =
        fptrCreateThread(lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags, lpThreadId);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

long WINAPI myBroadcastSystemMessageExW(WIND::DWORD flags, WIND::LPDWORD lpInfo, WIND::UINT Msg, WIND::WPARAM wParam,
                                        WIND::LPARAM lParam, WIND::PBSMINFO pbsmInfo)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering BroadcastSystemMessageExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    long res = fptrBroadcastSystemMessageExW(flags, lpInfo, Msg, wParam, lParam, pbsmInfo);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving BroadcastSystemMessageExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCreateProcessAsUserA(WIND::HANDLE hToken, WIND::LPCSTR lpApplicationName, WIND::LPSTR lpCommandLine,
                                         WIND::LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                         WIND::LPSECURITY_ATTRIBUTES lpThreadAttributes, WIND::BOOL bInheritHandles,
                                         WIND::DWORD dwCreationFlags, WIND::LPVOID lpEnvironment, WIND::LPCSTR lpCurrentDirectory,
                                         WIND::LPSTARTUPINFOA lpStartupInfo, WIND::LPPROCESS_INFORMATION lpProcessInformation)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateProcessAsUserA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrCreateProcessAsUserA(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
                                              bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo,
                                              lpProcessInformation);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateProcessAsUserA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateSemaphoreW(WIND::LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, WIND::LONG lInitialCount,
                                       WIND::LONG lMaximumCount, WIND::LPCWSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateSemaphoreW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateSemaphoreW(lpSemaphoreAttributes, lInitialCount, lMaximumCount, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateSemaphoreW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateMutexW(WIND::LPSECURITY_ATTRIBUTES lpMutexAttributes, WIND::BOOL bInitialOwner, WIND::LPCWSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateMutexW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateMutexW(lpMutexAttributes, bInitialOwner, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateMutexW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS myRpcMgmtWaitServerListen()

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcMgmtWaitServerListen" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcMgmtWaitServerListen();

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcMgmtWaitServerListen" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

VOID WINAPI myExitThread(WIND::DWORD dwExitCode)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ExitThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    fptrExitThread(dwExitCode);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ExitThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);
}

int WSAAPI myrecv(WIND::SOCKET s, char FAR* buf, int len, int flags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering recv" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrrecv(s, buf, len, flags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving recv" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCreateProcessAsUserW(WIND::HANDLE hToken, WIND::LPCWSTR lpApplicationName, WIND::LPWSTR lpCommandLine,
                                         WIND::LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                         WIND::LPSECURITY_ATTRIBUTES lpThreadAttributes, WIND::BOOL bInheritHandles,
                                         WIND::DWORD dwCreationFlags, WIND::LPVOID lpEnvironment,
                                         WIND::LPCWSTR lpCurrentDirectory, WIND::LPSTARTUPINFOW lpStartupInfo,
                                         WIND::LPPROCESS_INFORMATION lpProcessInformation)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateProcessAsUserW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrCreateProcessAsUserW(hToken, lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes,
                                              bInheritHandles, dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo,
                                              lpProcessInformation);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateProcessAsUserW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

VOID WINAPI myRtlEnterCriticalSection(WIND::LPCRITICAL_SECTION lpCriticalSection)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RtlEnterCriticalSection" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    fptrRtlEnterCriticalSection(lpCriticalSection);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RtlEnterCriticalSection" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);
}

WIND::BOOL WINAPI myReadConsoleA(WIND::HANDLE hConsoleInput, WIND::LPVOID lpBuffer, WIND::DWORD nNumberOfCharsToRead,
                                 WIND::LPDWORD lpNumberOfCharsRead, WIND::LPVOID lpReserved)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReadConsoleA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReadConsoleA(hConsoleInput, lpBuffer, nNumberOfCharsToRead, lpNumberOfCharsRead, lpReserved);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReadConsoleA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

VOID WINAPI myRtlDeleteCriticalSection(WIND::LPCRITICAL_SECTION lpCriticalSection)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RtlDeleteCriticalSection" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    fptrRtlDeleteCriticalSection(lpCriticalSection);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RtlDeleteCriticalSection" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);
}

WIND::DWORD WINAPI myWaitForMultipleObjectsEx(WIND::DWORD nCount, __in_ecount(nCount) CONST WIND::HANDLE* lpHandles,
                                              WIND::BOOL bWaitAll, WIND::DWORD dwMilliseconds, WIND::BOOL bAlertable)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WaitForMultipleObjectsEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrWaitForMultipleObjectsEx(nCount, lpHandles, bWaitAll, dwMilliseconds, bAlertable);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WaitForMultipleObjectsEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myTerminateProcess(WIND::HANDLE hProcess, WIND::UINT uExitCode)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering TerminateProcess" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrTerminateProcess(hProcess, uExitCode);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving TerminateProcess" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myFindCloseChangeNotification(WIND::HANDLE hChangeHandle)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering FindCloseChangeNotification" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrFindCloseChangeNotification(hChangeHandle);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving FindCloseChangeNotification" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myFindFirstPrinterChangeNotification(WIND::HANDLE hPrinter, WIND::DWORD fdwFlags, WIND::DWORD fdwOptions,
                                                         WIND::LPVOID pPrinterNotifyOptions)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering FindFirstPrinterChangeNotification" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrFindFirstPrinterChangeNotification(hPrinter, fdwFlags, fdwOptions, pPrinterNotifyOptions);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving FindFirstPrinterChangeNotification" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI myWaitForInputIdle(WIND::HANDLE hProcess, WIND::DWORD dwMilliseconds)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WaitForInputIdle" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrWaitForInputIdle(hProcess, dwMilliseconds);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WaitForInputIdle" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

unsigned char* WINAPI myNdrNsSendReceive(WIND::PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pBufferEnd,
                                         WIND::RPC_BINDING_HANDLE* pAutoHandle)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering NdrNsSendReceive" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    unsigned char* res = fptrNdrNsSendReceive(pStubMsg, pBufferEnd, pAutoHandle);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving NdrNsSendReceive" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myFindClosePrinterChangeNotification(WIND::HANDLE hChange)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering FindClosePrinterChangeNotification" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrFindClosePrinterChangeNotification(hChange);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving FindClosePrinterChangeNotification" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::INT_PTR WINAPI myDialogBoxIndirectParamA(WIND::HINSTANCE hInstance, WIND::LPCDLGTEMPLATEA hDialogTemplate,
                                               WIND::HWND hWndParent, WIND::DLGPROC lpDialogFunc, WIND::LPARAM dwInitParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering DialogBoxIndirectParamA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::INT_PTR res = fptrDialogBoxIndirectParamA(hInstance, hDialogTemplate, hWndParent, lpDialogFunc, dwInitParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving DialogBoxIndirectParamA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myReadConsoleW(WIND::HANDLE hConsoleInput, WIND::LPVOID lpBuffer, WIND::DWORD nNumberOfCharsToRead,
                                 WIND::LPDWORD lpNumberOfCharsRead, WIND::LPVOID lpReserved)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReadConsoleW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReadConsoleW(hConsoleInput, lpBuffer, nNumberOfCharsToRead, lpNumberOfCharsRead, lpReserved);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReadConsoleW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myGetMessageA(WIND::LPMSG lpMsg, WIND::HWND hWnd, WIND::UINT wMsgFilterMin, WIND::UINT wMsgFilterMax)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering GetMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrGetMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving GetMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI mySetWaitableTimer(WIND::HANDLE hTimer, const WIND::LARGE_INTEGER* lpDueTime, WIND::LONG lPeriod,
                                     WIND::PTIMERAPCROUTINE pfnCompletionRoutine, WIND::LPVOID lpArgToCompletionRoutine,
                                     WIND::BOOL fResume)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SetWaitableTimer" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrSetWaitableTimer(hTimer, lpDueTime, lPeriod, pfnCompletionRoutine, lpArgToCompletionRoutine, fResume);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SetWaitableTimer" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI mySleepEx(WIND::DWORD dwMilliseconds, WIND::BOOL bAlertable)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SleepEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrSleepEx(dwMilliseconds, bAlertable);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SleepEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HMODULE WINAPI myLoadLibraryExW(WIND::LPCWSTR lpLibFileName, __reserved WIND::HANDLE hFile, unsigned long dwFlags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering LoadLibraryExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HMODULE res = fptrLoadLibraryExW(lpLibFileName, hFile, dwFlags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        PIN_LockClient();

        globalCounter++;

        if (globalCounter >= 100 && attachCycles >= KnobStressDetachReAttach.Value() && doLoopPred != 0)
        {
            //eventhough this is not an error - print to std::cerr (in order to see it on the screen)
            std::cerr << "exiting from application!" << std::endl;
            doLoopPred = 0;
        }

        PIN_UnlockClient();

        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving LoadLibraryExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCallNamedPipeW(WIND::LPCWSTR lpNamedPipeName, __in_bcount_opt(nInBufferSize) WIND::LPVOID lpInBuffer,
                                   WIND::DWORD nInBufferSize,
                                   __out_bcount_part_opt(nOutBufferSize, *lpBytesRead) WIND::LPVOID lpOutBuffer,
                                   WIND::DWORD nOutBufferSize, WIND::LPDWORD lpBytesRead, WIND::DWORD nTimeOut)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CallNamedPipeW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res =
        fptrCallNamedPipeW(lpNamedPipeName, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesRead, nTimeOut);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CallNamedPipeW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI mySendNotifyMessageA(WIND::HWND hWnd, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SendNotifyMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrSendNotifyMessageA(hWnd, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SendNotifyMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myInitializeCriticalSectionAndSpinCount(WIND::LPCRITICAL_SECTION lpCriticalSection, WIND::DWORD dwSpinCount)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering InitializeCriticalSectionAndSpinCount" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrInitializeCriticalSectionAndSpinCount(lpCriticalSection, dwSpinCount);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving InitializeCriticalSectionAndSpinCount" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL APIENTRY myPrintDlgW(WIND::LPPRINTDLGW lpDlg)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PrintDlgW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPrintDlgW(lpDlg);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PrintDlgW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcMgmtEpEltInqNextW(WIND::RPC_EP_INQ_HANDLE InquiryContext, WIND::RPC_IF_ID __RPC_FAR* IfId,
                                               WIND::RPC_BINDING_HANDLE __RPC_FAR* Binding, WIND::UUID __RPC_FAR* ObjectUuid,
                                               __deref_opt_out WIND::RPC_WSTR __RPC_FAR* Annotation)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcMgmtEpEltInqNextW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcMgmtEpEltInqNextW(InquiryContext, IfId, Binding, ObjectUuid, Annotation);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcMgmtEpEltInqNextW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcMgmtEpEltInqDone(WIND::RPC_EP_INQ_HANDLE __RPC_FAR* InquiryContext)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcMgmtEpEltInqDone" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcMgmtEpEltInqDone(InquiryContext);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcMgmtEpEltInqDone" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myPostMessageA(WIND::HWND hWnd, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PostMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPostMessageA(hWnd, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PostMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myTerminateThread(WIND::HANDLE hThread, WIND::DWORD dwExitCode)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering TerminateThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrTerminateThread(hThread, dwExitCode);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving TerminateThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HMODULE WINAPI myLoadLibraryW(WIND::LPCWSTR lpLibFileName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering LoadLibraryW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HMODULE res = fptrLoadLibraryW(lpLibFileName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);

        PIN_LockClient();

        globalCounter++;

        if (globalCounter >= 100 && attachCycles >= KnobStressDetachReAttach.Value() && doLoopPred != 0)
        {
            //eventhough this is not an error - print to std::cerr (in order to see it on the screen)
            std::cerr << "success -  exiting from application!" << std::endl;
            doLoopPred = 0;
        }

        PIN_UnlockClient();

        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving LoadLibraryW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateMailslotW(WIND::LPCWSTR lpName, WIND::DWORD nMaxMessageSize, WIND::DWORD lReadTimeout,
                                      WIND::LPSECURITY_ATTRIBUTES lpSecurityAttributes)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateMailslotW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateMailslotW(lpName, nMaxMessageSize, lReadTimeout, lpSecurityAttributes);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateMailslotW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI mySetEvent(WIND::HANDLE hEvent)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SetEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrSetEvent(hEvent);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SetEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myReadConsoleInputA(WIND::HANDLE hConsoleInput, WIND::PINPUT_RECORD lpBuffer, WIND::DWORD nLength,
                                      WIND::LPDWORD lpNumberOfEventsRead)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReadConsoleInputA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReadConsoleInputA(hConsoleInput, lpBuffer, nLength, lpNumberOfEventsRead);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReadConsoleInputA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCreateProcessWithLogonW(WIND::LPCWSTR lpUsername, WIND::LPCWSTR lpDomain, WIND::LPCWSTR lpPassword,
                                            WIND::DWORD dwLogonFlags, WIND::LPCWSTR lpApplicationName, WIND::LPWSTR lpCommandLine,
                                            WIND::DWORD dwCreationFlags, WIND::LPVOID lpEnvironment,
                                            WIND::LPCWSTR lpCurrentDirectory, WIND::LPSTARTUPINFOW lpStartupInfo,
                                            WIND::LPPROCESS_INFORMATION lpProcessInformation)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateProcessWithLogonW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res =
        fptrCreateProcessWithLogonW(lpUsername, lpDomain, lpPassword, dwLogonFlags, lpApplicationName, lpCommandLine,
                                    dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateProcessWithLogonW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCallNamedPipeA(WIND::LPCSTR lpNamedPipeName, __in_bcount_opt(nInBufferSize) WIND::LPVOID lpInBuffer,
                                   WIND::DWORD nInBufferSize,
                                   __out_bcount_part_opt(nOutBufferSize, *lpBytesRead) WIND::LPVOID lpOutBuffer,
                                   WIND::DWORD nOutBufferSize, WIND::LPDWORD lpBytesRead, WIND::DWORD nTimeOut)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CallNamedPipeA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res =
        fptrCallNamedPipeA(lpNamedPipeName, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesRead, nTimeOut);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CallNamedPipeA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI mySendNotifyMessageW(WIND::HWND hWnd, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SendNotifyMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrSendNotifyMessageW(hWnd, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SendNotifyMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateMailslotA(WIND::LPCSTR lpName, WIND::DWORD nMaxMessageSize, WIND::DWORD lReadTimeout,
                                      WIND::LPSECURITY_ATTRIBUTES lpSecurityAttributes)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateMailslotA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateMailslotA(lpName, nMaxMessageSize, lReadTimeout, lpSecurityAttributes);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateMailslotA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WSAAPI myselect(int nfds, WIND::fd_set FAR* readfds, WIND::fd_set FAR* writefds, WIND::fd_set FAR* exceptfds,
                    const struct timeval FAR* timeout)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering select" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrselect(nfds, readfds, writefds, exceptfds, timeout);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving select" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateRemoteThread(WIND::HANDLE hProcess, WIND::LPSECURITY_ATTRIBUTES lpThreadAttributes,
                                         WIND::SIZE_T dwStackSize, WIND::LPTHREAD_START_ROUTINE lpStartAddress,
                                         WIND::LPVOID lpParameter, WIND::DWORD dwCreationFlags, WIND::LPDWORD lpThreadId)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateRemoteThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateRemoteThread(hProcess, lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter,
                                              dwCreationFlags, lpThreadId);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateRemoteThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HHOOK WINAPI mySetWindowsHookExA(int idHook, WIND::HOOKPROC lpfn, WIND::HINSTANCE hmod, WIND::DWORD dwThreadId)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SetWindowsHookExA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HHOOK res = fptrSetWindowsHookExA(idHook, lpfn, hmod, dwThreadId);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SetWindowsHookExA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

long WINAPI myBroadcastSystemMessageExA(WIND::DWORD flags, WIND::LPDWORD lpInfo, WIND::UINT Msg, WIND::WPARAM wParam,
                                        WIND::LPARAM lParam, WIND::PBSMINFO pbsmInfo)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering BroadcastSystemMessageExA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    long res = fptrBroadcastSystemMessageExA(flags, lpInfo, Msg, wParam, lParam, pbsmInfo);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving BroadcastSystemMessageExA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myOpenProcess(WIND::DWORD dwDesiredAccess, WIND::BOOL bInheritHandle, WIND::DWORD dwProcessId)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenProcess" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrOpenProcess(dwDesiredAccess, bInheritHandle, dwProcessId);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenProcess" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WSAAPI myWSAWaitForMultipleEvents(WIND::DWORD cEvents, const WIND::WSAEVENT FAR* lphEvents, WIND::BOOL fWaitAll,
                                              WIND::DWORD dwTimeout, WIND::BOOL fAlertable)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSAWaitForMultipleEvents" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrWSAWaitForMultipleEvents(cEvents, lphEvents, fWaitAll, dwTimeout, fAlertable);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSAWaitForMultipleEvents" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcMgmtEpEltInqBegin(WIND::RPC_BINDING_HANDLE EpBinding, unsigned long InquiryType,
                                               WIND::RPC_IF_ID __RPC_FAR* IfId, unsigned long VersOption,
                                               WIND::UUID __RPC_FAR* ObjectUuid,
                                               WIND::RPC_EP_INQ_HANDLE __RPC_FAR* InquiryContext)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcMgmtEpEltInqBegin" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcMgmtEpEltInqBegin(EpBinding, InquiryType, IfId, VersOption, ObjectUuid, InquiryContext);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcMgmtEpEltInqBegin" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myWriteFileGather(WIND::HANDLE hFile, WIND::FILE_SEGMENT_ELEMENT** aSegmentArray,
                                    WIND::DWORD nNumberOfBytesToWrite, __reserved WIND::LPDWORD lpReserved,
                                    WIND::LPOVERLAPPED lpOverlapped)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WriteFileGather" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWriteFileGather(hFile, aSegmentArray, nNumberOfBytesToWrite, lpReserved, lpOverlapped);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WriteFileGather" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI myWaitForMultipleObjects(WIND::DWORD nCount, __in_ecount(nCount) CONST WIND::HANDLE* lpHandles,
                                            WIND::BOOL bWaitAll, WIND::DWORD dwMilliseconds)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WaitForMultipleObjects" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrWaitForMultipleObjects(nCount, lpHandles, bWaitAll, dwMilliseconds);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WaitForMultipleObjects" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

unsigned char* WINAPI myNdrSendReceive(WIND::PMIDL_STUB_MESSAGE pStubMsg, unsigned char* pBufferEnd)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering NdrSendReceive" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    unsigned char* res = fptrNdrSendReceive(pStubMsg, pBufferEnd);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving NdrSendReceive" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateWaitableTimerW(WIND::LPSECURITY_ATTRIBUTES lpTimerAttributes, WIND::BOOL bManualReset,
                                           WIND::LPCWSTR lpTimerName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateWaitableTimerW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateWaitableTimerW(lpTimerAttributes, bManualReset, lpTimerName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateWaitableTimerW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myWaitMessage()

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WaitMessage" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWaitMessage();

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WaitMessage" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI mySetCriticalSectionSpinCount(WIND::LPCRITICAL_SECTION lpCriticalSection, WIND::DWORD dwSpinCount)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SetCriticalSectionSpinCount" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrSetCriticalSectionSpinCount(lpCriticalSection, dwSpinCount);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SetCriticalSectionSpinCount" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myWaitNamedPipeW(WIND::LPCWSTR lpNamedPipeName, WIND::DWORD nTimeOut)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WaitNamedPipeW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWaitNamedPipeW(lpNamedPipeName, nTimeOut);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WaitNamedPipeW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WSAAPI mysendto(WIND::SOCKET s, const char FAR* buf, int len, int flags, const struct sockaddr FAR* to, int tolen)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering sendto" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrsendto(s, buf, len, flags, to, tolen);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving sendto" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myWaitNamedPipeA(WIND::LPCSTR lpNamedPipeName, WIND::DWORD nTimeOut)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WaitNamedPipeA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWaitNamedPipeA(lpNamedPipeName, nTimeOut);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WaitNamedPipeA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myResetEvent(WIND::HANDLE hEvent)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ResetEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrResetEvent(hEvent);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ResetEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCreateTimerQueueTimer(WIND::PHANDLE phNewTimer, WIND::HANDLE TimerQueue, WIND::WAITORTIMERCALLBACK Callback,
                                          WIND::PVOID Parameter, WIND::DWORD DueTime, WIND::DWORD Period, WIND::ULONG Flags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateTimerQueueTimer" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrCreateTimerQueueTimer(phNewTimer, TimerQueue, Callback, Parameter, DueTime, Period, Flags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateTimerQueueTimer" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateWaitableTimerA(WIND::LPSECURITY_ATTRIBUTES lpTimerAttributes, WIND::BOOL bManualReset,
                                           WIND::LPCSTR lpTimerName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateWaitableTimerA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateWaitableTimerA(lpTimerAttributes, bManualReset, lpTimerName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateWaitableTimerA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myDuplicateHandle(WIND::HANDLE hSourceProcessHandle, WIND::HANDLE hSourceHandle,
                                    WIND::HANDLE hTargetProcessHandle, WIND::LPHANDLE lpTargetHandle, WIND::DWORD dwDesiredAccess,
                                    WIND::BOOL bInheritHandle, WIND::DWORD dwOptions)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering DuplicateHandle" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrDuplicateHandle(hSourceProcessHandle, hSourceHandle, hTargetProcessHandle, lpTargetHandle,
                                         dwDesiredAccess, bInheritHandle, dwOptions);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving DuplicateHandle" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WSAAPI myclosesocket(WIND::SOCKET s)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering closesocket" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrclosesocket(s);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving closesocket" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::FARPROC WINAPI myGetProcAddress(WIND::HMODULE hModule, WIND::LPCSTR lpProcName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering GetProcAddress" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::FARPROC res = fptrGetProcAddress(hModule, lpProcName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving GetProcAddress" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::SOCKET WSAAPI mysocket(int af, int type, int protocol)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering socket" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::SOCKET res = fptrsocket(af, type, protocol);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving socket" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WSAAPI myWSASetEvent(WIND::WSAEVENT hEvent)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSASetEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWSASetEvent(hEvent);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSASetEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateEventW(WIND::LPSECURITY_ATTRIBUTES lpEventAttributes, WIND::BOOL bManualReset,
                                   WIND::BOOL bInitialState, WIND::LPCWSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateEventW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateEventW(lpEventAttributes, bManualReset, bInitialState, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateEventW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myPostThreadMessageW(WIND::DWORD idThread, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PostThreadMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPostThreadMessageW(idThread, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PostThreadMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WSAAPI myWSASendTo(WIND::SOCKET s, WIND::LPWSABUF lpBuffers, WIND::DWORD dwBufferCount, WIND::LPDWORD lpNumberOfBytesSent,
                       WIND::DWORD dwFlags, const struct sockaddr FAR* lpTo, int iTolen, WIND::LPWSAOVERLAPPED lpOverlapped,
                       WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSASendTo" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res =
        fptrWSASendTo(s, lpBuffers, dwBufferCount, lpNumberOfBytesSent, dwFlags, lpTo, iTolen, lpOverlapped, lpCompletionRoutine);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSASendTo" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateFileW(WIND::LPCWSTR lpFileName, WIND::DWORD dwDesiredAccess, WIND::DWORD dwShareMode,
                                  WIND::LPSECURITY_ATTRIBUTES lpSecurityAttributes, WIND::DWORD dwCreationDisposition,
                                  WIND::DWORD dwFlagsAndAttributes, WIND::HANDLE hTemplateFile)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateFileW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition,
                                       dwFlagsAndAttributes, hTemplateFile);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateFileW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateEventA(WIND::LPSECURITY_ATTRIBUTES lpEventAttributes, WIND::BOOL bManualReset,
                                   WIND::BOOL bInitialState, WIND::LPCSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateEventA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateEventA(lpEventAttributes, bManualReset, bInitialState, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateEventA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcServerListen(unsigned int MinimumCallThreads, unsigned int MaxCalls, unsigned int DontWait)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcServerListen" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcServerListen(MinimumCallThreads, MaxCalls, DontWait);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcServerListen" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateFileA(WIND::LPCSTR lpFileName, WIND::DWORD dwDesiredAccess, WIND::DWORD dwShareMode,
                                  WIND::LPSECURITY_ATTRIBUTES lpSecurityAttributes, WIND::DWORD dwCreationDisposition,
                                  WIND::DWORD dwFlagsAndAttributes, WIND::HANDLE hTemplateFile)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateFileA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateFileA(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition,
                                       dwFlagsAndAttributes, hTemplateFile);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateFileA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myDeleteTimerQueueEx(WIND::HANDLE TimerQueue, WIND::HANDLE CompletionEvent)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering DeleteTimerQueueEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrDeleteTimerQueueEx(TimerQueue, CompletionEvent);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving DeleteTimerQueueEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::LRESULT WINAPI mySendMessageTimeoutW(WIND::HWND hWnd, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam,
                                           WIND::UINT fuFlags, WIND::UINT uTimeout, WIND::PDWORD_PTR lpdwResult)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SendMessageTimeoutW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::LRESULT res = fptrSendMessageTimeoutW(hWnd, Msg, wParam, lParam, fuFlags, uTimeout, lpdwResult);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SendMessageTimeoutW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myPostThreadMessageA(WIND::DWORD idThread, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PostThreadMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPostThreadMessageA(idThread, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PostThreadMessageA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::SOCKET WSAAPI myWSASocketW(int af, int type, int protocol, WIND::LPWSAPROTOCOL_INFOW lpProtocolInfo, WIND::GROUP g,
                                 WIND::DWORD dwFlags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSASocketW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::SOCKET res = fptrWSASocketW(af, type, protocol, lpProtocolInfo, g, dwFlags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSASocketW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WSAAPI myWSAGetOverlappedResult(WIND::SOCKET s, WIND::LPWSAOVERLAPPED lpOverlapped, WIND::LPDWORD lpcbTransfer,
                                           WIND::BOOL fWait, WIND::LPDWORD lpdwFlags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSAGetOverlappedResult" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWSAGetOverlappedResult(s, lpOverlapped, lpcbTransfer, fWait, lpdwFlags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSAGetOverlappedResult" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WSAAPI myWSAConnect(WIND::SOCKET s, const struct sockaddr FAR* name, int namelen, WIND::LPWSABUF lpCallerData,
                        WIND::LPWSABUF lpCalleeData, WIND::LPQOS lpSQOS, WIND::LPQOS lpGQOS)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSAConnect" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrWSAConnect(s, name, namelen, lpCallerData, lpCalleeData, lpSQOS, lpGQOS);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSAConnect" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myFindFirstChangeNotificationA(WIND::LPCSTR lpPathName, WIND::BOOL bWatchSubtree, WIND::DWORD dwNotifyFilter)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering FindFirstChangeNotificationA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrFindFirstChangeNotificationA(lpPathName, bWatchSubtree, dwNotifyFilter);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving FindFirstChangeNotificationA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateNamedPipeW(WIND::LPCWSTR lpName, WIND::DWORD dwOpenMode, WIND::DWORD dwPipeMode,
                                       WIND::DWORD nMaxInstances, WIND::DWORD nOutBufferSize, WIND::DWORD nInBufferSize,
                                       WIND::DWORD nDefaultTimeOut, WIND::LPSECURITY_ATTRIBUTES lpSecurityAttributes)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateNamedPipeW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateNamedPipeW(lpName, dwOpenMode, dwPipeMode, nMaxInstances, nOutBufferSize, nInBufferSize,
                                            nDefaultTimeOut, lpSecurityAttributes);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateNamedPipeW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::SOCKET WSAAPI myaccept(WIND::SOCKET s, struct sockaddr FAR* addr, int FAR* addrlen)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering accept" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::SOCKET res = fptraccept(s, addr, addrlen);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving accept" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myOpenSemaphoreA(WIND::DWORD dwDesiredAccess, WIND::BOOL bInheritHandle, WIND::LPCSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenSemaphoreA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrOpenSemaphoreA(dwDesiredAccess, bInheritHandle, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenSemaphoreA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myUnregisterWaitEx(WIND::HANDLE WaitHandle, WIND::HANDLE CompletionEvent)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering UnregisterWaitEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrUnregisterWaitEx(WaitHandle, CompletionEvent);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving UnregisterWaitEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcMgmtIsServerListening(WIND::RPC_BINDING_HANDLE Binding)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcMgmtIsServerListening" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcMgmtIsServerListening(Binding);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcMgmtIsServerListening" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI myWaitForSingleObjectEx(WIND::HANDLE hHandle, WIND::DWORD dwMilliseconds, WIND::BOOL bAlertable)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WaitForSingleObjectEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrWaitForSingleObjectEx(hHandle, dwMilliseconds, bAlertable);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WaitForSingleObjectEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateNamedPipeA(WIND::LPCSTR lpName, WIND::DWORD dwOpenMode, WIND::DWORD dwPipeMode,
                                       WIND::DWORD nMaxInstances, WIND::DWORD nOutBufferSize, WIND::DWORD nInBufferSize,
                                       WIND::DWORD nDefaultTimeOut, WIND::LPSECURITY_ATTRIBUTES lpSecurityAttributes)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateNamedPipeA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateNamedPipeA(lpName, dwOpenMode, dwPipeMode, nMaxInstances, nOutBufferSize, nInBufferSize,
                                            nDefaultTimeOut, lpSecurityAttributes);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateNamedPipeA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myFindFirstChangeNotificationW(WIND::LPCWSTR lpPathName, WIND::BOOL bWatchSubtree, WIND::DWORD dwNotifyFilter)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering FindFirstChangeNotificationW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrFindFirstChangeNotificationW(lpPathName, bWatchSubtree, dwNotifyFilter);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving FindFirstChangeNotificationW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WINAPI myMessageBoxExA(WIND::HWND hWnd, WIND::LPCSTR lpText, WIND::LPCSTR lpCaption, WIND::UINT uType, WIND::WORD wLanguageId)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering MessageBoxExA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrMessageBoxExA(hWnd, lpText, lpCaption, uType, wLanguageId);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving MessageBoxExA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myGetQueuedCompletionStatus(WIND::HANDLE CompletionPort, WIND::LPDWORD lpNumberOfBytesTransferred,
                                              WIND::PULONG_PTR lpCompletionKey, WIND::LPOVERLAPPED lpOverlapped,
                                              WIND::DWORD dwMilliseconds)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering GetQueuedCompletionStatus" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res =
        fptrGetQueuedCompletionStatus(CompletionPort, lpNumberOfBytesTransferred, lpCompletionKey, lpOverlapped, dwMilliseconds);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving GetQueuedCompletionStatus" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCancelWaitableTimer(WIND::HANDLE hTimer)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CancelWaitableTimer" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrCancelWaitableTimer(hTimer);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CancelWaitableTimer" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WINAPI myMessageBoxW(WIND::HWND hWnd, WIND::LPCWSTR lpText, WIND::LPCWSTR lpCaption, WIND::UINT uType)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering MessageBoxW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrMessageBoxW(hWnd, lpText, lpCaption, uType);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving MessageBoxW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::SOCKET WSAAPI myWSAAccept(WIND::SOCKET s, struct sockaddr FAR* addr, WIND::LPINT addrlen,
                                WIND::LPCONDITIONPROC lpfnCondition, WIND::DWORD_PTR dwCallbackData)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSAAccept" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::SOCKET res = fptrWSAAccept(s, addr, addrlen, lpfnCondition, dwCallbackData);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSAAccept" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myUnregisterWait(WIND::HANDLE WaitHandle)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering UnregisterWait" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrUnregisterWait(WaitHandle);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving UnregisterWait" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WSAAPI myWSACloseEvent(WIND::WSAEVENT hEvent)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WSACloseEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWSACloseEvent(hEvent);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WSACloseEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myCreateIoCompletionPort(WIND::HANDLE FileHandle, WIND::HANDLE ExistingCompletionPort,
                                             WIND::ULONG_PTR CompletionKey, WIND::DWORD NumberOfConcurrentThreads)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateIoCompletionPort" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrCreateIoCompletionPort(FileHandle, ExistingCompletionPort, CompletionKey, NumberOfConcurrentThreads);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateIoCompletionPort" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::INT_PTR WINAPI myDialogBoxParamW(WIND::HINSTANCE hInstance, WIND::LPCWSTR lpTemplateName, WIND::HWND hWndParent,
                                       WIND::DLGPROC lpDialogFunc, WIND::LPARAM dwInitParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering DialogBoxParamW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::INT_PTR res = fptrDialogBoxParamW(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving DialogBoxParamW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WINAPI myMessageBoxA(WIND::HWND hWnd, WIND::LPCSTR lpText, WIND::LPCSTR lpCaption, WIND::UINT uType)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering MessageBoxA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrMessageBoxA(hWnd, lpText, lpCaption, uType);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving MessageBoxA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

int WINAPI myMessageBoxExW(WIND::HWND hWnd, WIND::LPCWSTR lpText, WIND::LPCWSTR lpCaption, WIND::UINT uType,
                           WIND::WORD wLanguageId)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering MessageBoxExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    int res = fptrMessageBoxExW(hWnd, lpText, lpCaption, uType, wLanguageId);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving MessageBoxExW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::INT_PTR WINAPI myDialogBoxParamA(WIND::HINSTANCE hInstance, WIND::LPCSTR lpTemplateName, WIND::HWND hWndParent,
                                       WIND::DLGPROC lpDialogFunc, WIND::LPARAM dwInitParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering DialogBoxParamA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::INT_PTR res = fptrDialogBoxParamA(hInstance, lpTemplateName, hWndParent, lpDialogFunc, dwInitParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving DialogBoxParamA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

VOID WINAPI mySleep(WIND::DWORD dwMilliseconds)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering Sleep" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    fptrSleep(dwMilliseconds);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving Sleep" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);
}

WIND::RPC_STATUS WINAPI myRpcNsBindingLookupDone(WIND::RPC_NS_HANDLE __RPC_FAR* LookupContext)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcNsBindingLookupDone" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcNsBindingLookupDone(LookupContext);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcNsBindingLookupDone" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myOpenMutexA(WIND::DWORD dwDesiredAccess, WIND::BOOL bInheritHandle, WIND::LPCSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenMutexA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrOpenMutexA(dwDesiredAccess, bInheritHandle, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenMutexA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::DWORD WINAPI mySuspendThread(WIND::HANDLE hThread)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SuspendThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::DWORD res = fptrSuspendThread(hThread);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SuspendThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

VOID WINAPI myRaiseException(WIND::DWORD dwExceptionCode, WIND::DWORD dwExceptionFlags, WIND::DWORD nNumberOfArguments,
                             __in_ecount_opt(nNumberOfArguments) CONST WIND::ULONG_PTR* lpArguments)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RaiseException" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    fptrRaiseException(dwExceptionCode, dwExceptionFlags, nNumberOfArguments, lpArguments);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RaiseException" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);
}

WIND::RPC_STATUS WINAPI myRpcNsBindingImportBeginW(unsigned long EntryNameSyntax, WIND::RPC_WSTR EntryName,
                                                   WIND::RPC_IF_HANDLE IfSpec, WIND::UUID __RPC_FAR* ObjUuid,
                                                   WIND::RPC_NS_HANDLE __RPC_FAR* ImportContext)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcNsBindingImportBeginW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcNsBindingImportBeginW(EntryNameSyntax, EntryName, IfSpec, ObjUuid, ImportContext);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcNsBindingImportBeginW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myReleaseSemaphore(WIND::HANDLE hSemaphore, WIND::LONG lReleaseCount, WIND::LPLONG lpPreviousCount)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReleaseSemaphore" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReleaseSemaphore(hSemaphore, lReleaseCount, lpPreviousCount);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReleaseSemaphore" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HFILE WINAPI myOpenFile(WIND::LPCSTR lpFileName, WIND::LPOFSTRUCT lpReOpenBuff, WIND::UINT uStyle)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenFile" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HFILE res = fptrOpenFile(lpFileName, lpReOpenBuff, uStyle);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenFile" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myReadFile(WIND::HANDLE hFile,
                             __out_bcount_part(nNumberOfBytesToRead, *lpNumberOfBytesRead) WIND::LPVOID lpBuffer,
                             WIND::DWORD nNumberOfBytesToRead, WIND::LPDWORD lpNumberOfBytesRead, WIND::LPOVERLAPPED lpOverlapped)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReadFile" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReadFile" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myWriteFileEx(WIND::HANDLE hFile, __in_bcount(nNumberOfBytesToWrite) WIND::LPCVOID lpBuffer,
                                WIND::DWORD nNumberOfBytesToWrite, WIND::LPOVERLAPPED lpOverlapped,
                                WIND::LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering WriteFileEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrWriteFileEx(hFile, lpBuffer, nNumberOfBytesToWrite, lpOverlapped, lpCompletionRoutine);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving WriteFileEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcNsBindingImportBeginA(unsigned long EntryNameSyntax, WIND::RPC_CSTR EntryName,
                                                   WIND::RPC_IF_HANDLE IfSpec, WIND::UUID __RPC_FAR* ObjUuid,
                                                   WIND::RPC_NS_HANDLE __RPC_FAR* ImportContext)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcNsBindingImportBeginA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcNsBindingImportBeginA(EntryNameSyntax, EntryName, IfSpec, ObjUuid, ImportContext);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcNsBindingImportBeginA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myPulseEvent(WIND::HANDLE hEvent)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PulseEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPulseEvent(hEvent);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PulseEvent" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcCancelThread(void* Thread)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcCancelThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcCancelThread(Thread);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcCancelThread" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCloseHandle(WIND::HANDLE hObject)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CloseHandle" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrCloseHandle(hObject);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CloseHandle" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myOpenMutexW(WIND::DWORD dwDesiredAccess, WIND::BOOL bInheritHandle, WIND::LPCWSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenMutexW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrOpenMutexW(dwDesiredAccess, bInheritHandle, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenMutexW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myPostMessageW(WIND::HWND hWnd, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PostMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPostMessageW(hWnd, Msg, wParam, lParam);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PostMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::RPC_STATUS WINAPI myRpcEpResolveBinding(WIND::RPC_BINDING_HANDLE Binding, WIND::RPC_IF_HANDLE IfSpec)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering RpcEpResolveBinding" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::RPC_STATUS res = fptrRpcEpResolveBinding(Binding, IfSpec);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving RpcEpResolveBinding" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myBindIoCompletionCallback(WIND::HANDLE FileHandle, WIND::LPOVERLAPPED_COMPLETION_ROUTINE Function,
                                             WIND::ULONG Flags)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering BindIoCompletionCallback" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrBindIoCompletionCallback(FileHandle, Function, Flags);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving BindIoCompletionCallback" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myPeekMessageW(WIND::LPMSG lpMsg, WIND::HWND hWnd, WIND::UINT wMsgFilterMin, WIND::UINT wMsgFilterMax,
                                 WIND::UINT wRemoveMsg)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PeekMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PeekMessageW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::LRESULT WINAPI mySendMessageTimeoutA(WIND::HWND hWnd, WIND::UINT Msg, WIND::WPARAM wParam, WIND::LPARAM lParam,
                                           WIND::UINT fuFlags, WIND::UINT uTimeout, WIND::PDWORD_PTR lpdwResult)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering SendMessageTimeoutA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::LRESULT res = fptrSendMessageTimeoutA(hWnd, Msg, wParam, lParam, fuFlags, uTimeout, lpdwResult);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving SendMessageTimeoutA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myOpenEventA(WIND::DWORD dwDesiredAccess, WIND::BOOL bInheritHandle, WIND::LPCSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenEventA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrOpenEventA(dwDesiredAccess, bInheritHandle, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenEventA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCreateProcessA(WIND::LPCSTR lpApplicationName, WIND::LPSTR lpCommandLine,
                                   WIND::LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                   WIND::LPSECURITY_ATTRIBUTES lpThreadAttributes, WIND::BOOL bInheritHandles,
                                   WIND::DWORD dwCreationFlags, WIND::LPVOID lpEnvironment, WIND::LPCSTR lpCurrentDirectory,
                                   WIND::LPSTARTUPINFOA lpStartupInfo, WIND::LPPROCESS_INFORMATION lpProcessInformation)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateProcessA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res =
        fptrCreateProcessA(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles,
                           dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateProcessA" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myOpenEventW(WIND::DWORD dwDesiredAccess, WIND::BOOL bInheritHandle, WIND::LPCWSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenEventW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrOpenEventW(dwDesiredAccess, bInheritHandle, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenEventW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myPostQueuedCompletionStatus(WIND::HANDLE CompletionPort, WIND::DWORD dwNumberOfBytesTransferred,
                                               WIND::ULONG_PTR dwCompletionKey, WIND::LPOVERLAPPED lpOverlapped)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering PostQueuedCompletionStatus" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrPostQueuedCompletionStatus(CompletionPort, dwNumberOfBytesTransferred, dwCompletionKey, lpOverlapped);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving PostQueuedCompletionStatus" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myCreateProcessW(WIND::LPCWSTR lpApplicationName, WIND::LPWSTR lpCommandLine,
                                   WIND::LPSECURITY_ATTRIBUTES lpProcessAttributes,
                                   WIND::LPSECURITY_ATTRIBUTES lpThreadAttributes, WIND::BOOL bInheritHandles,
                                   WIND::DWORD dwCreationFlags, WIND::LPVOID lpEnvironment, WIND::LPCWSTR lpCurrentDirectory,
                                   WIND::LPSTARTUPINFOW lpStartupInfo, WIND::LPPROCESS_INFORMATION lpProcessInformation)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering CreateProcessW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res =
        fptrCreateProcessW(lpApplicationName, lpCommandLine, lpProcessAttributes, lpThreadAttributes, bInheritHandles,
                           dwCreationFlags, lpEnvironment, lpCurrentDirectory, lpStartupInfo, lpProcessInformation);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving CreateProcessW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myTransactNamedPipe(WIND::HANDLE hNamedPipe, __in_bcount_opt(nInBufferSize) WIND::LPVOID lpInBuffer,
                                      WIND::DWORD nInBufferSize,
                                      __out_bcount_part_opt(nOutBufferSize, *lpBytesRead) WIND::LPVOID lpOutBuffer,
                                      WIND::DWORD nOutBufferSize, WIND::LPDWORD lpBytesRead, WIND::LPOVERLAPPED lpOverlapped)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering TransactNamedPipe" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res =
        fptrTransactNamedPipe(hNamedPipe, lpInBuffer, nInBufferSize, lpOutBuffer, nOutBufferSize, lpBytesRead, lpOverlapped);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving TransactNamedPipe" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myReadFileEx(WIND::HANDLE hFile, __out_bcount(nNumberOfBytesToRead) WIND::LPVOID lpBuffer,
                               WIND::DWORD nNumberOfBytesToRead, WIND::LPOVERLAPPED lpOverlapped,
                               WIND::LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReadFileEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReadFileEx(hFile, lpBuffer, nNumberOfBytesToRead, lpOverlapped, lpCompletionRoutine);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReadFileEx" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::HANDLE WINAPI myOpenSemaphoreW(WIND::DWORD dwDesiredAccess, WIND::BOOL bInheritHandle, WIND::LPCWSTR lpName)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering OpenSemaphoreW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::HANDLE res = fptrOpenSemaphoreW(dwDesiredAccess, bInheritHandle, lpName);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving OpenSemaphoreW" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

WIND::BOOL WINAPI myReplyMessage(WIND::LRESULT lResult)

{
    WIND::DWORD dwBeforeError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Entering ReplyMessage" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwBeforeError);

    WIND::BOOL res = fptrReplyMessage(lResult);

    WIND::DWORD dwAfterError = WIND::GetLastError();

    if (!(WIND::TlsGetValue(dwIndexTls)))

    {
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);
        if (KnobDoPrint)
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Leaving ReplyMessage" << std::endl;
            OutFile.flush();
        }
        WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
    }

    WIND::SetLastError(dwAfterError);

    return res;
}

//functions section footer

void ImgLoad(IMG img, void* v)

{
    // Called every time a new image is loaded
    // Used to locate routines we want to probe

    WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)1);

    if ((IMG_Name(img).find("comdlg32.dll") != std::string::npos) || (IMG_Name(img).find("COMDLG32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("COMDLG32.dll") != std::string::npos))

    {
        RTN rtnPrintDlgExW = RTN_FindByName(img, "PrintDlgExW");
        if (RTN_Valid(rtnPrintDlgExW) && RTN_IsSafeForProbedReplacement(rtnPrintDlgExW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PrintDlgExW at " << RTN_Address(rtnPrintDlgExW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnPrintDlgExW, AFUNPTR(myPrintDlgExW)));
            fptrPrintDlgExW = (WIND::HRESULT(APIENTRY*)(WIND::LPPRINTDLGEXW))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcns4.dll") != std::string::npos) || (IMG_Name(img).find("RPCNS4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCNS4.dll") != std::string::npos))

    {
        RTN rtnRpcNsBindingLookupBeginW = RTN_FindByName(img, "RpcNsBindingLookupBeginW");
        if (RTN_Valid(rtnRpcNsBindingLookupBeginW) && RTN_IsSafeForProbedReplacement(rtnRpcNsBindingLookupBeginW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcNsBindingLookupBeginW at " << RTN_Address(rtnRpcNsBindingLookupBeginW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnRpcNsBindingLookupBeginW, AFUNPTR(myRpcNsBindingLookupBeginW)));
            fptrRpcNsBindingLookupBeginW =
                (WIND::RPC_STATUS(WINAPI*)(unsigned long, WIND::RPC_WSTR, WIND::RPC_IF_HANDLE, WIND::UUID __RPC_FAR*,
                                           unsigned long, WIND::RPC_NS_HANDLE __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnGetStdHandle = RTN_FindByName(img, "GetStdHandle");
        if (RTN_Valid(rtnGetStdHandle) && RTN_IsSafeForProbedReplacement(rtnGetStdHandle))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for GetStdHandle at " << RTN_Address(rtnGetStdHandle) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnGetStdHandle, AFUNPTR(myGetStdHandle)));
            fptrGetStdHandle = (WIND::HANDLE(WINAPI*)(WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnReleaseMutex = RTN_FindByName(img, "ReleaseMutex");
        if (RTN_Valid(rtnReleaseMutex) && RTN_IsSafeForProbedReplacement(rtnReleaseMutex))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReleaseMutex at " << RTN_Address(rtnReleaseMutex) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnReleaseMutex, AFUNPTR(myReleaseMutex)));
            fptrReleaseMutex = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnGetOverlappedResult = RTN_FindByName(img, "GetOverlappedResult");
        if (RTN_Valid(rtnGetOverlappedResult) && RTN_IsSafeForProbedReplacement(rtnGetOverlappedResult))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for GetOverlappedResult at " << RTN_Address(rtnGetOverlappedResult) << std::endl;
            OutFile.flush();
            AFUNPTR fptr            = (RTN_ReplaceProbed(rtnGetOverlappedResult, AFUNPTR(myGetOverlappedResult)));
            fptrGetOverlappedResult = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::LPOVERLAPPED, WIND::LPDWORD, WIND::BOOL))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWaitForSingleObject = RTN_FindByName(img, "WaitForSingleObject");
        if (RTN_Valid(rtnWaitForSingleObject) && RTN_IsSafeForProbedReplacement(rtnWaitForSingleObject))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WaitForSingleObject at " << RTN_Address(rtnWaitForSingleObject) << std::endl;
            OutFile.flush();
            AFUNPTR fptr            = (RTN_ReplaceProbed(rtnWaitForSingleObject, AFUNPTR(myWaitForSingleObject)));
            fptrWaitForSingleObject = (WIND::DWORD(WINAPI*)(WIND::HANDLE, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnBroadcastSystemMessageA = RTN_FindByName(img, "BroadcastSystemMessageA");
        if (RTN_Valid(rtnBroadcastSystemMessageA) && RTN_IsSafeForProbedReplacement(rtnBroadcastSystemMessageA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for BroadcastSystemMessageA at " << RTN_Address(rtnBroadcastSystemMessageA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                = (RTN_ReplaceProbed(rtnBroadcastSystemMessageA, AFUNPTR(myBroadcastSystemMessageA)));
            fptrBroadcastSystemMessageA = (long(WINAPI*)(WIND::DWORD, WIND::LPDWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnSignalObjectAndWait = RTN_FindByName(img, "SignalObjectAndWait");
        if (RTN_Valid(rtnSignalObjectAndWait) && RTN_IsSafeForProbedReplacement(rtnSignalObjectAndWait))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SignalObjectAndWait at " << RTN_Address(rtnSignalObjectAndWait) << std::endl;
            OutFile.flush();
            AFUNPTR fptr            = (RTN_ReplaceProbed(rtnSignalObjectAndWait, AFUNPTR(mySignalObjectAndWait)));
            fptrSignalObjectAndWait = (WIND::DWORD(WINAPI*)(WIND::HANDLE, WIND::HANDLE, WIND::DWORD, WIND::BOOL))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcns4.dll") != std::string::npos) || (IMG_Name(img).find("RPCNS4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCNS4.dll") != std::string::npos))

    {
        RTN rtnRpcNsBindingLookupBeginA = RTN_FindByName(img, "RpcNsBindingLookupBeginA");
        if (RTN_Valid(rtnRpcNsBindingLookupBeginA) && RTN_IsSafeForProbedReplacement(rtnRpcNsBindingLookupBeginA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcNsBindingLookupBeginA at " << RTN_Address(rtnRpcNsBindingLookupBeginA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnRpcNsBindingLookupBeginA, AFUNPTR(myRpcNsBindingLookupBeginA)));
            fptrRpcNsBindingLookupBeginA =
                (WIND::RPC_STATUS(WINAPI*)(unsigned long, WIND::RPC_CSTR, WIND::RPC_IF_HANDLE, WIND::UUID __RPC_FAR*,
                                           unsigned long, WIND::RPC_NS_HANDLE __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("comdlg32.dll") != std::string::npos) || (IMG_Name(img).find("COMDLG32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("COMDLG32.dll") != std::string::npos))

    {
        RTN rtnPrintDlgExA = RTN_FindByName(img, "PrintDlgExA");
        if (RTN_Valid(rtnPrintDlgExA) && RTN_IsSafeForProbedReplacement(rtnPrintDlgExA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PrintDlgExA at " << RTN_Address(rtnPrintDlgExA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnPrintDlgExA, AFUNPTR(myPrintDlgExA)));
            fptrPrintDlgExA = (WIND::HRESULT(APIENTRY*)(WIND::LPPRINTDLGEXA))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateTimerQueue = RTN_FindByName(img, "CreateTimerQueue");
        if (RTN_Valid(rtnCreateTimerQueue) && RTN_IsSafeForProbedReplacement(rtnCreateTimerQueue))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateTimerQueue at " << RTN_Address(rtnCreateTimerQueue) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnCreateTimerQueue, AFUNPTR(myCreateTimerQueue)));
            fptrCreateTimerQueue = (WIND::HANDLE(WINAPI*)())fptr;
        }
    }

    if ((IMG_Name(img).find("rpcns4.dll") != std::string::npos) || (IMG_Name(img).find("RPCNS4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCNS4.dll") != std::string::npos))

    {
        RTN rtnRpcNsBindingImportNext = RTN_FindByName(img, "RpcNsBindingImportNext");
        if (RTN_Valid(rtnRpcNsBindingImportNext) && RTN_IsSafeForProbedReplacement(rtnRpcNsBindingImportNext))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcNsBindingImportNext at " << RTN_Address(rtnRpcNsBindingImportNext) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnRpcNsBindingImportNext, AFUNPTR(myRpcNsBindingImportNext)));
            fptrRpcNsBindingImportNext =
                (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_NS_HANDLE, WIND::RPC_BINDING_HANDLE __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnQueueUserAPC = RTN_FindByName(img, "QueueUserAPC");
        if (RTN_Valid(rtnQueueUserAPC) && RTN_IsSafeForProbedReplacement(rtnQueueUserAPC))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for QueueUserAPC at " << RTN_Address(rtnQueueUserAPC) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnQueueUserAPC, AFUNPTR(myQueueUserAPC)));
            fptrQueueUserAPC = (WIND::DWORD(WINAPI*)(WIND::PAPCFUNC, WIND::HANDLE, WIND::ULONG_PTR))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcns4.dll") != std::string::npos) || (IMG_Name(img).find("RPCNS4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCNS4.dll") != std::string::npos))

    {
        RTN rtnRpcNsBindingLookupNext = RTN_FindByName(img, "RpcNsBindingLookupNext");
        if (RTN_Valid(rtnRpcNsBindingLookupNext) && RTN_IsSafeForProbedReplacement(rtnRpcNsBindingLookupNext))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcNsBindingLookupNext at " << RTN_Address(rtnRpcNsBindingLookupNext) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnRpcNsBindingLookupNext, AFUNPTR(myRpcNsBindingLookupNext)));
            fptrRpcNsBindingLookupNext =
                (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_NS_HANDLE, WIND::RPC_BINDING_VECTOR __RPC_FAR * __RPC_FAR*)) fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnsend = RTN_FindByName(img, "send");
        if (RTN_Valid(rtnsend) && RTN_IsSafeForProbedReplacement(rtnsend))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for send at " << RTN_Address(rtnsend) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnsend, AFUNPTR(mysend)));
            fptrsend     = (int(WSAAPI*)(WIND::SOCKET, const char FAR*, int, int))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnPeekMessageA = RTN_FindByName(img, "PeekMessageA");
        if (RTN_Valid(rtnPeekMessageA) && RTN_IsSafeForProbedReplacement(rtnPeekMessageA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PeekMessageA at " << RTN_Address(rtnPeekMessageA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnPeekMessageA, AFUNPTR(myPeekMessageA)));
            fptrPeekMessageA = (WIND::BOOL(WINAPI*)(WIND::LPMSG, WIND::HWND, WIND::UINT, WIND::UINT, WIND::UINT))fptr;
        }
    }

    if ((IMG_Name(img).find("ntdll.dll") != std::string::npos) || (IMG_Name(img).find("NTDLL.DLL") != std::string::npos) ||
        (IMG_Name(img).find("NTDLL.dll") != std::string::npos))

    {
        RTN rtnRtlLeaveCriticalSection = RTN_FindByName(img, "RtlLeaveCriticalSection");
        if (RTN_Valid(rtnRtlLeaveCriticalSection) && RTN_IsSafeForProbedReplacement(rtnRtlLeaveCriticalSection))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RtlLeaveCriticalSection at " << RTN_Address(rtnRtlLeaveCriticalSection) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                = (RTN_ReplaceProbed(rtnRtlLeaveCriticalSection, AFUNPTR(myRtlLeaveCriticalSection)));
            fptrRtlLeaveCriticalSection = (VOID(WINAPI*)(WIND::LPCRITICAL_SECTION))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnReadFileScatter = RTN_FindByName(img, "ReadFileScatter");
        if (RTN_Valid(rtnReadFileScatter) && RTN_IsSafeForProbedReplacement(rtnReadFileScatter))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReadFileScatter at " << RTN_Address(rtnReadFileScatter) << std::endl;
            OutFile.flush();
            AFUNPTR fptr        = (RTN_ReplaceProbed(rtnReadFileScatter, AFUNPTR(myReadFileScatter)));
            fptrReadFileScatter = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::FILE_SEGMENT_ELEMENT**, WIND::DWORD,
                                                       __reserved WIND::LPDWORD, WIND::LPOVERLAPPED))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWaitCommEvent = RTN_FindByName(img, "WaitCommEvent");
        if (RTN_Valid(rtnWaitCommEvent) && RTN_IsSafeForProbedReplacement(rtnWaitCommEvent))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WaitCommEvent at " << RTN_Address(rtnWaitCommEvent) << std::endl;
            OutFile.flush();
            AFUNPTR fptr      = (RTN_ReplaceProbed(rtnWaitCommEvent, AFUNPTR(myWaitCommEvent)));
            fptrWaitCommEvent = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::LPDWORD, WIND::LPOVERLAPPED))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnSendMessageW = RTN_FindByName(img, "SendMessageW");
        if (RTN_Valid(rtnSendMessageW) && RTN_IsSafeForProbedReplacement(rtnSendMessageW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SendMessageW at " << RTN_Address(rtnSendMessageW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnSendMessageW, AFUNPTR(mySendMessageW)));
            fptrSendMessageW = (WIND::LRESULT(WINAPI*)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSARecv = RTN_FindByName(img, "WSARecv");
        if (RTN_Valid(rtnWSARecv) && RTN_IsSafeForProbedReplacement(rtnWSARecv))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSARecv at " << RTN_Address(rtnWSARecv) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnWSARecv, AFUNPTR(myWSARecv)));
            fptrWSARecv  = (int(WSAAPI*)(WIND::SOCKET, WIND::LPWSABUF, WIND::DWORD, WIND::LPDWORD, WIND::LPDWORD,
                                        WIND::LPWSAOVERLAPPED, WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnSendMessageA = RTN_FindByName(img, "SendMessageA");
        if (RTN_Valid(rtnSendMessageA) && RTN_IsSafeForProbedReplacement(rtnSendMessageA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SendMessageA at " << RTN_Address(rtnSendMessageA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnSendMessageA, AFUNPTR(mySendMessageA)));
            fptrSendMessageA = (WIND::LRESULT(WINAPI*)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWriteFile = RTN_FindByName(img, "WriteFile");
        if (RTN_Valid(rtnWriteFile) && RTN_IsSafeForProbedReplacement(rtnWriteFile))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WriteFile at " << RTN_Address(rtnWriteFile) << std::endl;
            OutFile.flush();
            AFUNPTR fptr  = (RTN_ReplaceProbed(rtnWriteFile, AFUNPTR(myWriteFile)));
            fptrWriteFile = (WIND::BOOL(WINAPI*)(WIND::HANDLE, __in_bcount(nNumberOfBytesToWrite) WIND::LPCVOID, WIND::DWORD,
                                                 WIND::LPDWORD, WIND::LPOVERLAPPED))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnResumeThread = RTN_FindByName(img, "ResumeThread");
        if (RTN_Valid(rtnResumeThread) && RTN_IsSafeForProbedReplacement(rtnResumeThread))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ResumeThread at " << RTN_Address(rtnResumeThread) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnResumeThread, AFUNPTR(myResumeThread)));
            fptrResumeThread = (WIND::DWORD(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcMgmtEpEltInqNextA = RTN_FindByName(img, "RpcMgmtEpEltInqNextA");
        if (RTN_Valid(rtnRpcMgmtEpEltInqNextA) && RTN_IsSafeForProbedReplacement(rtnRpcMgmtEpEltInqNextA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcMgmtEpEltInqNextA at " << RTN_Address(rtnRpcMgmtEpEltInqNextA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr             = (RTN_ReplaceProbed(rtnRpcMgmtEpEltInqNextA, AFUNPTR(myRpcMgmtEpEltInqNextA)));
            fptrRpcMgmtEpEltInqNextA = (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_EP_INQ_HANDLE, WIND::RPC_IF_ID __RPC_FAR*,
                                                                  WIND::RPC_BINDING_HANDLE __RPC_FAR*, WIND::UUID __RPC_FAR*,
                                                                  __deref_opt_out WIND::RPC_CSTR __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnMsgWaitForMultipleObjectsEx = RTN_FindByName(img, "MsgWaitForMultipleObjectsEx");
        if (RTN_Valid(rtnMsgWaitForMultipleObjectsEx) && RTN_IsSafeForProbedReplacement(rtnMsgWaitForMultipleObjectsEx))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for MsgWaitForMultipleObjectsEx at " << RTN_Address(rtnMsgWaitForMultipleObjectsEx)
                    << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnMsgWaitForMultipleObjectsEx, AFUNPTR(myMsgWaitForMultipleObjectsEx)));
            fptrMsgWaitForMultipleObjectsEx = (WIND::DWORD(WINAPI*)(WIND::DWORD, __in_ecount_opt(nCount) CONST WIND::HANDLE*,
                                                                    WIND::DWORD, WIND::DWORD, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnBroadcastSystemMessageW = RTN_FindByName(img, "BroadcastSystemMessageW");
        if (RTN_Valid(rtnBroadcastSystemMessageW) && RTN_IsSafeForProbedReplacement(rtnBroadcastSystemMessageW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for BroadcastSystemMessageW at " << RTN_Address(rtnBroadcastSystemMessageW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                = (RTN_ReplaceProbed(rtnBroadcastSystemMessageW, AFUNPTR(myBroadcastSystemMessageW)));
            fptrBroadcastSystemMessageW = (long(WINAPI*)(WIND::DWORD, WIND::LPDWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnConnectNamedPipe = RTN_FindByName(img, "ConnectNamedPipe");
        if (RTN_Valid(rtnConnectNamedPipe) && RTN_IsSafeForProbedReplacement(rtnConnectNamedPipe))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ConnectNamedPipe at " << RTN_Address(rtnConnectNamedPipe) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnConnectNamedPipe, AFUNPTR(myConnectNamedPipe)));
            fptrConnectNamedPipe = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::LPOVERLAPPED))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnFreeLibraryAndExitThread = RTN_FindByName(img, "FreeLibraryAndExitThread");
        if (RTN_Valid(rtnFreeLibraryAndExitThread) && RTN_IsSafeForProbedReplacement(rtnFreeLibraryAndExitThread))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for FreeLibraryAndExitThread at " << RTN_Address(rtnFreeLibraryAndExitThread) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                 = (RTN_ReplaceProbed(rtnFreeLibraryAndExitThread, AFUNPTR(myFreeLibraryAndExitThread)));
            fptrFreeLibraryAndExitThread = (VOID(WINAPI*)(WIND::HMODULE, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnInitializeCriticalSection = RTN_FindByName(img, "InitializeCriticalSection");
        if (RTN_Valid(rtnInitializeCriticalSection) && RTN_IsSafeForProbedReplacement(rtnInitializeCriticalSection))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for InitializeCriticalSection at " << RTN_Address(rtnInitializeCriticalSection) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnInitializeCriticalSection, AFUNPTR(myInitializeCriticalSection)));
            fptrInitializeCriticalSection = (VOID(WINAPI*)(WIND::LPCRITICAL_SECTION))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnrecvfrom = RTN_FindByName(img, "recvfrom");
        if (RTN_Valid(rtnrecvfrom) && RTN_IsSafeForProbedReplacement(rtnrecvfrom))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for recvfrom at " << RTN_Address(rtnrecvfrom) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnrecvfrom, AFUNPTR(myrecvfrom)));
            fptrrecvfrom = (int(WSAAPI*)(WIND::SOCKET, char FAR*, int, int, struct sockaddr FAR*, int FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnDialogBoxIndirectParamW = RTN_FindByName(img, "DialogBoxIndirectParamW");
        if (RTN_Valid(rtnDialogBoxIndirectParamW) && RTN_IsSafeForProbedReplacement(rtnDialogBoxIndirectParamW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for DialogBoxIndirectParamW at " << RTN_Address(rtnDialogBoxIndirectParamW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnDialogBoxIndirectParamW, AFUNPTR(myDialogBoxIndirectParamW)));
            fptrDialogBoxIndirectParamW =
                (WIND::INT_PTR(WINAPI*)(WIND::HINSTANCE, WIND::LPCDLGTEMPLATEW, WIND::HWND, WIND::DLGPROC, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnDeleteTimerQueue = RTN_FindByName(img, "DeleteTimerQueue");
        if (RTN_Valid(rtnDeleteTimerQueue) && RTN_IsSafeForProbedReplacement(rtnDeleteTimerQueue))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for DeleteTimerQueue at " << RTN_Address(rtnDeleteTimerQueue) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnDeleteTimerQueue, AFUNPTR(myDeleteTimerQueue)));
            fptrDeleteTimerQueue = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnMsgWaitForMultipleObjects = RTN_FindByName(img, "MsgWaitForMultipleObjects");
        if (RTN_Valid(rtnMsgWaitForMultipleObjects) && RTN_IsSafeForProbedReplacement(rtnMsgWaitForMultipleObjects))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for MsgWaitForMultipleObjects at " << RTN_Address(rtnMsgWaitForMultipleObjects) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnMsgWaitForMultipleObjects, AFUNPTR(myMsgWaitForMultipleObjects)));
            fptrMsgWaitForMultipleObjects = (WIND::DWORD(WINAPI*)(WIND::DWORD, __in_ecount_opt(nCount) CONST WIND::HANDLE*,
                                                                  WIND::BOOL, WIND::DWORD, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnQueueUserWorkItem = RTN_FindByName(img, "QueueUserWorkItem");
        if (RTN_Valid(rtnQueueUserWorkItem) && RTN_IsSafeForProbedReplacement(rtnQueueUserWorkItem))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for QueueUserWorkItem at " << RTN_Address(rtnQueueUserWorkItem) << std::endl;
            OutFile.flush();
            AFUNPTR fptr          = (RTN_ReplaceProbed(rtnQueueUserWorkItem, AFUNPTR(myQueueUserWorkItem)));
            fptrQueueUserWorkItem = (WIND::BOOL(WINAPI*)(WIND::LPTHREAD_START_ROUTINE, WIND::PVOID, WIND::ULONG))fptr;
        }
    }

    if ((IMG_Name(img).find("winspool.drv") != std::string::npos) || (IMG_Name(img).find("WINSPOOL.DRV") != std::string::npos) ||
        (IMG_Name(img).find("WINSPOOL.drv") != std::string::npos))

    {
        RTN rtnConnectToPrinterDlg = RTN_FindByName(img, "ConnectToPrinterDlg");
        if (RTN_Valid(rtnConnectToPrinterDlg) && RTN_IsSafeForProbedReplacement(rtnConnectToPrinterDlg))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ConnectToPrinterDlg at " << RTN_Address(rtnConnectToPrinterDlg) << std::endl;
            OutFile.flush();
            AFUNPTR fptr            = (RTN_ReplaceProbed(rtnConnectToPrinterDlg, AFUNPTR(myConnectToPrinterDlg)));
            fptrConnectToPrinterDlg = (WIND::HANDLE(WINAPI*)(WIND::HWND, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnDeviceIoControl = RTN_FindByName(img, "DeviceIoControl");
        if (RTN_Valid(rtnDeviceIoControl) && RTN_IsSafeForProbedReplacement(rtnDeviceIoControl))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for DeviceIoControl at " << RTN_Address(rtnDeviceIoControl) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnDeviceIoControl, AFUNPTR(myDeviceIoControl)));
            fptrDeviceIoControl =
                (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::DWORD, __in_bcount_opt(nInBufferSize) WIND::LPVOID, WIND::DWORD,
                                     __out_bcount_part_opt(nOutBufferSize, *lpBytesReturned) WIND::LPVOID, WIND::DWORD,
                                     WIND::LPDWORD, WIND::LPOVERLAPPED))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSASocketA = RTN_FindByName(img, "WSASocketA");
        if (RTN_Valid(rtnWSASocketA) && RTN_IsSafeForProbedReplacement(rtnWSASocketA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSASocketA at " << RTN_Address(rtnWSASocketA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnWSASocketA, AFUNPTR(myWSASocketA)));
            fptrWSASocketA = (WIND::SOCKET(WSAAPI*)(int, int, int, WIND::LPWSAPROTOCOL_INFOA, WIND::GROUP, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSARecvFrom = RTN_FindByName(img, "WSARecvFrom");
        if (RTN_Valid(rtnWSARecvFrom) && RTN_IsSafeForProbedReplacement(rtnWSARecvFrom))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSARecvFrom at " << RTN_Address(rtnWSARecvFrom) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnWSARecvFrom, AFUNPTR(myWSARecvFrom)));
            fptrWSARecvFrom =
                (int(WSAAPI*)(WIND::SOCKET, WIND::LPWSABUF, WIND::DWORD, WIND::LPDWORD, WIND::LPDWORD, struct sockaddr FAR*,
                              WIND::LPINT, WIND::LPWSAOVERLAPPED, WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcStringBindingComposeA = RTN_FindByName(img, "RpcStringBindingComposeA");
        if (RTN_Valid(rtnRpcStringBindingComposeA) && RTN_IsSafeForProbedReplacement(rtnRpcStringBindingComposeA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcStringBindingComposeA at " << RTN_Address(rtnRpcStringBindingComposeA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnRpcStringBindingComposeA, AFUNPTR(myRpcStringBindingComposeA)));
            fptrRpcStringBindingComposeA =
                (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_CSTR, WIND::RPC_CSTR, WIND::RPC_CSTR, WIND::RPC_CSTR, WIND::RPC_CSTR,
                                           __deref_opt_out WIND::RPC_CSTR __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSACreateEvent = RTN_FindByName(img, "WSACreateEvent");
        if (RTN_Valid(rtnWSACreateEvent) && RTN_IsSafeForProbedReplacement(rtnWSACreateEvent))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSACreateEvent at " << RTN_Address(rtnWSACreateEvent) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnWSACreateEvent, AFUNPTR(myWSACreateEvent)));
            fptrWSACreateEvent = (WIND::WSAEVENT(WSAAPI*)())fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnExitProcess = RTN_FindByName(img, "ExitProcess");
        if (RTN_Valid(rtnExitProcess) && RTN_IsSafeForProbedReplacement(rtnExitProcess))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ExitProcess at " << RTN_Address(rtnExitProcess) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnExitProcess, AFUNPTR(myExitProcess)));
            fptrExitProcess = (VOID(WINAPI*)(WIND::UINT))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnconnect = RTN_FindByName(img, "connect");
        if (RTN_Valid(rtnconnect) && RTN_IsSafeForProbedReplacement(rtnconnect))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for connect at " << RTN_Address(rtnconnect) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnconnect, AFUNPTR(myconnect)));
            fptrconnect  = (int(WSAAPI*)(WIND::SOCKET, const struct sockaddr FAR*, int))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnGetMessageW = RTN_FindByName(img, "GetMessageW");
        if (RTN_Valid(rtnGetMessageW) && RTN_IsSafeForProbedReplacement(rtnGetMessageW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for GetMessageW at " << RTN_Address(rtnGetMessageW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnGetMessageW, AFUNPTR(myGetMessageW)));
            fptrGetMessageW = (WIND::BOOL(WINAPI*)(WIND::LPMSG, WIND::HWND, WIND::UINT, WIND::UINT))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSAResetEvent = RTN_FindByName(img, "WSAResetEvent");
        if (RTN_Valid(rtnWSAResetEvent) && RTN_IsSafeForProbedReplacement(rtnWSAResetEvent))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSAResetEvent at " << RTN_Address(rtnWSAResetEvent) << std::endl;
            OutFile.flush();
            AFUNPTR fptr      = (RTN_ReplaceProbed(rtnWSAResetEvent, AFUNPTR(myWSAResetEvent)));
            fptrWSAResetEvent = (WIND::BOOL(WSAAPI*)(WIND::WSAEVENT))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcStringBindingComposeW = RTN_FindByName(img, "RpcStringBindingComposeW");
        if (RTN_Valid(rtnRpcStringBindingComposeW) && RTN_IsSafeForProbedReplacement(rtnRpcStringBindingComposeW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcStringBindingComposeW at " << RTN_Address(rtnRpcStringBindingComposeW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnRpcStringBindingComposeW, AFUNPTR(myRpcStringBindingComposeW)));
            fptrRpcStringBindingComposeW =
                (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_WSTR, WIND::RPC_WSTR, WIND::RPC_WSTR, WIND::RPC_WSTR, WIND::RPC_WSTR,
                                           __deref_opt_out WIND::RPC_WSTR __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("comdlg32.dll") != std::string::npos) || (IMG_Name(img).find("COMDLG32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("COMDLG32.dll") != std::string::npos))

    {
        RTN rtnPrintDlgA = RTN_FindByName(img, "PrintDlgA");
        if (RTN_Valid(rtnPrintDlgA) && RTN_IsSafeForProbedReplacement(rtnPrintDlgA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PrintDlgA at " << RTN_Address(rtnPrintDlgA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr  = (RTN_ReplaceProbed(rtnPrintDlgA, AFUNPTR(myPrintDlgA)));
            fptrPrintDlgA = (WIND::BOOL(APIENTRY*)(WIND::LPPRINTDLGA))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenWaitableTimerA = RTN_FindByName(img, "OpenWaitableTimerA");
        if (RTN_Valid(rtnOpenWaitableTimerA) && RTN_IsSafeForProbedReplacement(rtnOpenWaitableTimerA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenWaitableTimerA at " << RTN_Address(rtnOpenWaitableTimerA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr           = (RTN_ReplaceProbed(rtnOpenWaitableTimerA, AFUNPTR(myOpenWaitableTimerA)));
            fptrOpenWaitableTimerA = (WIND::HANDLE(WINAPI*)(WIND::DWORD, WIND::BOOL, WIND::LPCSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnSetWindowsHookExW = RTN_FindByName(img, "SetWindowsHookExW");
        if (RTN_Valid(rtnSetWindowsHookExW) && RTN_IsSafeForProbedReplacement(rtnSetWindowsHookExW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SetWindowsHookExW at " << RTN_Address(rtnSetWindowsHookExW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr          = (RTN_ReplaceProbed(rtnSetWindowsHookExW, AFUNPTR(mySetWindowsHookExW)));
            fptrSetWindowsHookExW = (WIND::HHOOK(WINAPI*)(int, WIND::HOOKPROC, WIND::HINSTANCE, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSASend = RTN_FindByName(img, "WSASend");
        if (RTN_Valid(rtnWSASend) && RTN_IsSafeForProbedReplacement(rtnWSASend))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSASend at " << RTN_Address(rtnWSASend) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnWSASend, AFUNPTR(myWSASend)));
            fptrWSASend  = (int(WSAAPI*)(WIND::SOCKET, WIND::LPWSABUF, WIND::DWORD, WIND::LPDWORD, WIND::DWORD,
                                        WIND::LPWSAOVERLAPPED, WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenWaitableTimerW = RTN_FindByName(img, "OpenWaitableTimerW");
        if (RTN_Valid(rtnOpenWaitableTimerW) && RTN_IsSafeForProbedReplacement(rtnOpenWaitableTimerW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenWaitableTimerW at " << RTN_Address(rtnOpenWaitableTimerW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr           = (RTN_ReplaceProbed(rtnOpenWaitableTimerW, AFUNPTR(myOpenWaitableTimerW)));
            fptrOpenWaitableTimerW = (WIND::HANDLE(WINAPI*)(WIND::DWORD, WIND::BOOL, WIND::LPCWSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnDeleteTimerQueueTimer = RTN_FindByName(img, "DeleteTimerQueueTimer");
        if (RTN_Valid(rtnDeleteTimerQueueTimer) && RTN_IsSafeForProbedReplacement(rtnDeleteTimerQueueTimer))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for DeleteTimerQueueTimer at " << RTN_Address(rtnDeleteTimerQueueTimer) << std::endl;
            OutFile.flush();
            AFUNPTR fptr              = (RTN_ReplaceProbed(rtnDeleteTimerQueueTimer, AFUNPTR(myDeleteTimerQueueTimer)));
            fptrDeleteTimerQueueTimer = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::HANDLE, WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcns4.dll") != std::string::npos) || (IMG_Name(img).find("RPCNS4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCNS4.dll") != std::string::npos))

    {
        RTN rtnRpcNsBindingImportDone = RTN_FindByName(img, "RpcNsBindingImportDone");
        if (RTN_Valid(rtnRpcNsBindingImportDone) && RTN_IsSafeForProbedReplacement(rtnRpcNsBindingImportDone))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcNsBindingImportDone at " << RTN_Address(rtnRpcNsBindingImportDone) << std::endl;
            OutFile.flush();
            AFUNPTR fptr               = (RTN_ReplaceProbed(rtnRpcNsBindingImportDone, AFUNPTR(myRpcNsBindingImportDone)));
            fptrRpcNsBindingImportDone = (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_NS_HANDLE __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnReadConsoleInputW = RTN_FindByName(img, "ReadConsoleInputW");
        if (RTN_Valid(rtnReadConsoleInputW) && RTN_IsSafeForProbedReplacement(rtnReadConsoleInputW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReadConsoleInputW at " << RTN_Address(rtnReadConsoleInputW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr          = (RTN_ReplaceProbed(rtnReadConsoleInputW, AFUNPTR(myReadConsoleInputW)));
            fptrReadConsoleInputW = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::PINPUT_RECORD, WIND::DWORD, WIND::LPDWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateMutexA = RTN_FindByName(img, "CreateMutexA");
        if (RTN_Valid(rtnCreateMutexA) && RTN_IsSafeForProbedReplacement(rtnCreateMutexA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateMutexA at " << RTN_Address(rtnCreateMutexA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnCreateMutexA, AFUNPTR(myCreateMutexA)));
            fptrCreateMutexA = (WIND::HANDLE(WINAPI*)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::LPCSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnRegisterWaitForSingleObject = RTN_FindByName(img, "RegisterWaitForSingleObject");
        if (RTN_Valid(rtnRegisterWaitForSingleObject) && RTN_IsSafeForProbedReplacement(rtnRegisterWaitForSingleObject))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RegisterWaitForSingleObject at " << RTN_Address(rtnRegisterWaitForSingleObject)
                    << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnRegisterWaitForSingleObject, AFUNPTR(myRegisterWaitForSingleObject)));
            fptrRegisterWaitForSingleObject = (WIND::BOOL(WINAPI*)(WIND::PHANDLE, WIND::HANDLE, WIND::WAITORTIMERCALLBACK,
                                                                   WIND::PVOID, WIND::ULONG, WIND::ULONG))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcMgmtInqIfIds = RTN_FindByName(img, "RpcMgmtInqIfIds");
        if (RTN_Valid(rtnRpcMgmtInqIfIds) && RTN_IsSafeForProbedReplacement(rtnRpcMgmtInqIfIds))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcMgmtInqIfIds at " << RTN_Address(rtnRpcMgmtInqIfIds) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnRpcMgmtInqIfIds, AFUNPTR(myRpcMgmtInqIfIds)));
            fptrRpcMgmtInqIfIds =
                (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_BINDING_HANDLE, WIND::RPC_IF_ID_VECTOR __RPC_FAR * __RPC_FAR*)) fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateSemaphoreA = RTN_FindByName(img, "CreateSemaphoreA");
        if (RTN_Valid(rtnCreateSemaphoreA) && RTN_IsSafeForProbedReplacement(rtnCreateSemaphoreA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateSemaphoreA at " << RTN_Address(rtnCreateSemaphoreA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnCreateSemaphoreA, AFUNPTR(myCreateSemaphoreA)));
            fptrCreateSemaphoreA = (WIND::HANDLE(WINAPI*)(WIND::LPSECURITY_ATTRIBUTES, WIND::LONG, WIND::LONG, WIND::LPCSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateThread = RTN_FindByName(img, "CreateThread");
        if (RTN_Valid(rtnCreateThread) && RTN_IsSafeForProbedReplacement(rtnCreateThread))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateThread at " << RTN_Address(rtnCreateThread) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnCreateThread, AFUNPTR(myCreateThread)));
            fptrCreateThread = (WIND::HANDLE(WINAPI*)(WIND::LPSECURITY_ATTRIBUTES, WIND::SIZE_T, WIND::LPTHREAD_START_ROUTINE,
                                                      WIND::LPVOID, WIND::DWORD, WIND::LPDWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnBroadcastSystemMessageExW = RTN_FindByName(img, "BroadcastSystemMessageExW");
        if (RTN_Valid(rtnBroadcastSystemMessageExW) && RTN_IsSafeForProbedReplacement(rtnBroadcastSystemMessageExW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for BroadcastSystemMessageExW at " << RTN_Address(rtnBroadcastSystemMessageExW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnBroadcastSystemMessageExW, AFUNPTR(myBroadcastSystemMessageExW)));
            fptrBroadcastSystemMessageExW =
                (long(WINAPI*)(WIND::DWORD, WIND::LPDWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM, WIND::PBSMINFO))fptr;
        }
    }

    if ((IMG_Name(img).find("advapi32.dll") != std::string::npos) || (IMG_Name(img).find("ADVAPI32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("ADVAPI32.dll") != std::string::npos))

    {
        RTN rtnCreateProcessAsUserA = RTN_FindByName(img, "CreateProcessAsUserA");
        if (RTN_Valid(rtnCreateProcessAsUserA) && RTN_IsSafeForProbedReplacement(rtnCreateProcessAsUserA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateProcessAsUserA at " << RTN_Address(rtnCreateProcessAsUserA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr             = (RTN_ReplaceProbed(rtnCreateProcessAsUserA, AFUNPTR(myCreateProcessAsUserA)));
            fptrCreateProcessAsUserA = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::LPCSTR, WIND::LPSTR, WIND::LPSECURITY_ATTRIBUTES,
                                                            WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::DWORD, WIND::LPVOID,
                                                            WIND::LPCSTR, WIND::LPSTARTUPINFOA, WIND::LPPROCESS_INFORMATION))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateSemaphoreW = RTN_FindByName(img, "CreateSemaphoreW");
        if (RTN_Valid(rtnCreateSemaphoreW) && RTN_IsSafeForProbedReplacement(rtnCreateSemaphoreW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateSemaphoreW at " << RTN_Address(rtnCreateSemaphoreW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnCreateSemaphoreW, AFUNPTR(myCreateSemaphoreW)));
            fptrCreateSemaphoreW =
                (WIND::HANDLE(WINAPI*)(WIND::LPSECURITY_ATTRIBUTES, WIND::LONG, WIND::LONG, WIND::LPCWSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateMutexW = RTN_FindByName(img, "CreateMutexW");
        if (RTN_Valid(rtnCreateMutexW) && RTN_IsSafeForProbedReplacement(rtnCreateMutexW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateMutexW at " << RTN_Address(rtnCreateMutexW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnCreateMutexW, AFUNPTR(myCreateMutexW)));
            fptrCreateMutexW = (WIND::HANDLE(WINAPI*)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::LPCWSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcMgmtWaitServerListen = RTN_FindByName(img, "RpcMgmtWaitServerListen");
        if (RTN_Valid(rtnRpcMgmtWaitServerListen) && RTN_IsSafeForProbedReplacement(rtnRpcMgmtWaitServerListen))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcMgmtWaitServerListen at " << RTN_Address(rtnRpcMgmtWaitServerListen) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                = (RTN_ReplaceProbed(rtnRpcMgmtWaitServerListen, AFUNPTR(myRpcMgmtWaitServerListen)));
            fptrRpcMgmtWaitServerListen = (WIND::RPC_STATUS(*)())fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnExitThread = RTN_FindByName(img, "ExitThread");
        if (RTN_Valid(rtnExitThread) && RTN_IsSafeForProbedReplacement(rtnExitThread))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ExitThread at " << RTN_Address(rtnExitThread) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnExitThread, AFUNPTR(myExitThread)));
            fptrExitThread = (VOID(WINAPI*)(WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnrecv = RTN_FindByName(img, "recv");
        if (RTN_Valid(rtnrecv) && RTN_IsSafeForProbedReplacement(rtnrecv))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for recv at " << RTN_Address(rtnrecv) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnrecv, AFUNPTR(myrecv)));
            fptrrecv     = (int(WSAAPI*)(WIND::SOCKET, char FAR*, int, int))fptr;
        }
    }

    if ((IMG_Name(img).find("advapi32.dll") != std::string::npos) || (IMG_Name(img).find("ADVAPI32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("ADVAPI32.dll") != std::string::npos))

    {
        RTN rtnCreateProcessAsUserW = RTN_FindByName(img, "CreateProcessAsUserW");
        if (RTN_Valid(rtnCreateProcessAsUserW) && RTN_IsSafeForProbedReplacement(rtnCreateProcessAsUserW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateProcessAsUserW at " << RTN_Address(rtnCreateProcessAsUserW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr             = (RTN_ReplaceProbed(rtnCreateProcessAsUserW, AFUNPTR(myCreateProcessAsUserW)));
            fptrCreateProcessAsUserW = (WIND::BOOL(WINAPI*)(
                WIND::HANDLE, WIND::LPCWSTR, WIND::LPWSTR, WIND::LPSECURITY_ATTRIBUTES, WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL,
                WIND::DWORD, WIND::LPVOID, WIND::LPCWSTR, WIND::LPSTARTUPINFOW, WIND::LPPROCESS_INFORMATION))fptr;
        }
    }

    if ((IMG_Name(img).find("ntdll.dll") != std::string::npos) || (IMG_Name(img).find("NTDLL.DLL") != std::string::npos) ||
        (IMG_Name(img).find("NTDLL.dll") != std::string::npos))

    {
        RTN rtnRtlEnterCriticalSection = RTN_FindByName(img, "RtlEnterCriticalSection");
        if (RTN_Valid(rtnRtlEnterCriticalSection) && RTN_IsSafeForProbedReplacement(rtnRtlEnterCriticalSection))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RtlEnterCriticalSection at " << RTN_Address(rtnRtlEnterCriticalSection) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                = (RTN_ReplaceProbed(rtnRtlEnterCriticalSection, AFUNPTR(myRtlEnterCriticalSection)));
            fptrRtlEnterCriticalSection = (VOID(WINAPI*)(WIND::LPCRITICAL_SECTION))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnReadConsoleA = RTN_FindByName(img, "ReadConsoleA");
        if (RTN_Valid(rtnReadConsoleA) && RTN_IsSafeForProbedReplacement(rtnReadConsoleA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReadConsoleA at " << RTN_Address(rtnReadConsoleA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnReadConsoleA, AFUNPTR(myReadConsoleA)));
            fptrReadConsoleA = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::LPVOID, WIND::DWORD, WIND::LPDWORD, WIND::LPVOID))fptr;
        }
    }

    if ((IMG_Name(img).find("ntdll.dll") != std::string::npos) || (IMG_Name(img).find("NTDLL.DLL") != std::string::npos) ||
        (IMG_Name(img).find("NTDLL.dll") != std::string::npos))

    {
        RTN rtnRtlDeleteCriticalSection = RTN_FindByName(img, "RtlDeleteCriticalSection");
        if (RTN_Valid(rtnRtlDeleteCriticalSection) && RTN_IsSafeForProbedReplacement(rtnRtlDeleteCriticalSection))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RtlDeleteCriticalSection at " << RTN_Address(rtnRtlDeleteCriticalSection) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                 = (RTN_ReplaceProbed(rtnRtlDeleteCriticalSection, AFUNPTR(myRtlDeleteCriticalSection)));
            fptrRtlDeleteCriticalSection = (VOID(WINAPI*)(WIND::LPCRITICAL_SECTION))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWaitForMultipleObjectsEx = RTN_FindByName(img, "WaitForMultipleObjectsEx");
        if (RTN_Valid(rtnWaitForMultipleObjectsEx) && RTN_IsSafeForProbedReplacement(rtnWaitForMultipleObjectsEx))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WaitForMultipleObjectsEx at " << RTN_Address(rtnWaitForMultipleObjectsEx) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                 = (RTN_ReplaceProbed(rtnWaitForMultipleObjectsEx, AFUNPTR(myWaitForMultipleObjectsEx)));
            fptrWaitForMultipleObjectsEx = (WIND::DWORD(WINAPI*)(WIND::DWORD, __in_ecount(nCount) CONST WIND::HANDLE*, WIND::BOOL,
                                                                 WIND::DWORD, WIND::BOOL))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnTerminateProcess = RTN_FindByName(img, "TerminateProcess");
        if (RTN_Valid(rtnTerminateProcess) && RTN_IsSafeForProbedReplacement(rtnTerminateProcess))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for TerminateProcess at " << RTN_Address(rtnTerminateProcess) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnTerminateProcess, AFUNPTR(myTerminateProcess)));
            fptrTerminateProcess = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::UINT))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnFindCloseChangeNotification = RTN_FindByName(img, "FindCloseChangeNotification");
        if (RTN_Valid(rtnFindCloseChangeNotification) && RTN_IsSafeForProbedReplacement(rtnFindCloseChangeNotification))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for FindCloseChangeNotification at " << RTN_Address(rtnFindCloseChangeNotification)
                    << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnFindCloseChangeNotification, AFUNPTR(myFindCloseChangeNotification)));
            fptrFindCloseChangeNotification = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("winspool.drv") != std::string::npos) || (IMG_Name(img).find("WINSPOOL.DRV") != std::string::npos) ||
        (IMG_Name(img).find("WINSPOOL.drv") != std::string::npos))

    {
        RTN rtnFindFirstPrinterChangeNotification = RTN_FindByName(img, "FindFirstPrinterChangeNotification");
        if (RTN_Valid(rtnFindFirstPrinterChangeNotification) &&
            RTN_IsSafeForProbedReplacement(rtnFindFirstPrinterChangeNotification))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for FindFirstPrinterChangeNotification at "
                    << RTN_Address(rtnFindFirstPrinterChangeNotification) << std::endl;
            OutFile.flush();
            AFUNPTR fptr =
                (RTN_ReplaceProbed(rtnFindFirstPrinterChangeNotification, AFUNPTR(myFindFirstPrinterChangeNotification)));
            fptrFindFirstPrinterChangeNotification =
                (WIND::HANDLE(WINAPI*)(WIND::HANDLE, WIND::DWORD, WIND::DWORD, WIND::LPVOID))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnWaitForInputIdle = RTN_FindByName(img, "WaitForInputIdle");
        if (RTN_Valid(rtnWaitForInputIdle) && RTN_IsSafeForProbedReplacement(rtnWaitForInputIdle))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WaitForInputIdle at " << RTN_Address(rtnWaitForInputIdle) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnWaitForInputIdle, AFUNPTR(myWaitForInputIdle)));
            fptrWaitForInputIdle = (WIND::DWORD(WINAPI*)(WIND::HANDLE, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnNdrNsSendReceive = RTN_FindByName(img, "NdrNsSendReceive");
        if (RTN_Valid(rtnNdrNsSendReceive) && RTN_IsSafeForProbedReplacement(rtnNdrNsSendReceive))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for NdrNsSendReceive at " << RTN_Address(rtnNdrNsSendReceive) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnNdrNsSendReceive, AFUNPTR(myNdrNsSendReceive)));
            fptrNdrNsSendReceive =
                (unsigned char*(WINAPI*)(WIND::PMIDL_STUB_MESSAGE, unsigned char*, WIND::RPC_BINDING_HANDLE*))fptr;
        }
    }

    if ((IMG_Name(img).find("winspool.drv") != std::string::npos) || (IMG_Name(img).find("WINSPOOL.DRV") != std::string::npos) ||
        (IMG_Name(img).find("WINSPOOL.drv") != std::string::npos))

    {
        RTN rtnFindClosePrinterChangeNotification = RTN_FindByName(img, "FindClosePrinterChangeNotification");
        if (RTN_Valid(rtnFindClosePrinterChangeNotification) &&
            RTN_IsSafeForProbedReplacement(rtnFindClosePrinterChangeNotification))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for FindClosePrinterChangeNotification at "
                    << RTN_Address(rtnFindClosePrinterChangeNotification) << std::endl;
            OutFile.flush();
            AFUNPTR fptr =
                (RTN_ReplaceProbed(rtnFindClosePrinterChangeNotification, AFUNPTR(myFindClosePrinterChangeNotification)));
            fptrFindClosePrinterChangeNotification = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnDialogBoxIndirectParamA = RTN_FindByName(img, "DialogBoxIndirectParamA");
        if (RTN_Valid(rtnDialogBoxIndirectParamA) && RTN_IsSafeForProbedReplacement(rtnDialogBoxIndirectParamA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for DialogBoxIndirectParamA at " << RTN_Address(rtnDialogBoxIndirectParamA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnDialogBoxIndirectParamA, AFUNPTR(myDialogBoxIndirectParamA)));
            fptrDialogBoxIndirectParamA =
                (WIND::INT_PTR(WINAPI*)(WIND::HINSTANCE, WIND::LPCDLGTEMPLATEA, WIND::HWND, WIND::DLGPROC, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnReadConsoleW = RTN_FindByName(img, "ReadConsoleW");
        if (RTN_Valid(rtnReadConsoleW) && RTN_IsSafeForProbedReplacement(rtnReadConsoleW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReadConsoleW at " << RTN_Address(rtnReadConsoleW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnReadConsoleW, AFUNPTR(myReadConsoleW)));
            fptrReadConsoleW = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::LPVOID, WIND::DWORD, WIND::LPDWORD, WIND::LPVOID))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnGetMessageA = RTN_FindByName(img, "GetMessageA");
        if (RTN_Valid(rtnGetMessageA) && RTN_IsSafeForProbedReplacement(rtnGetMessageA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for GetMessageA at " << RTN_Address(rtnGetMessageA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnGetMessageA, AFUNPTR(myGetMessageA)));
            fptrGetMessageA = (WIND::BOOL(WINAPI*)(WIND::LPMSG, WIND::HWND, WIND::UINT, WIND::UINT))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnSetWaitableTimer = RTN_FindByName(img, "SetWaitableTimer");
        if (RTN_Valid(rtnSetWaitableTimer) && RTN_IsSafeForProbedReplacement(rtnSetWaitableTimer))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SetWaitableTimer at " << RTN_Address(rtnSetWaitableTimer) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnSetWaitableTimer, AFUNPTR(mySetWaitableTimer)));
            fptrSetWaitableTimer = (WIND::BOOL(WINAPI*)(WIND::HANDLE, const WIND::LARGE_INTEGER*, WIND::LONG,
                                                        WIND::PTIMERAPCROUTINE, WIND::LPVOID, WIND::BOOL))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnSleepEx = RTN_FindByName(img, "SleepEx");
        if (RTN_Valid(rtnSleepEx) && RTN_IsSafeForProbedReplacement(rtnSleepEx))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SleepEx at " << RTN_Address(rtnSleepEx) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnSleepEx, AFUNPTR(mySleepEx)));
            fptrSleepEx  = (WIND::DWORD(WINAPI*)(WIND::DWORD, WIND::BOOL))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnLoadLibraryExW = RTN_FindByName(img, "LoadLibraryExW");
        if (RTN_Valid(rtnLoadLibraryExW) && RTN_IsSafeForProbedReplacement(rtnLoadLibraryExW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for LoadLibraryExW at " << RTN_Address(rtnLoadLibraryExW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnLoadLibraryExW, AFUNPTR(myLoadLibraryExW)));
            fptrLoadLibraryExW = (WIND::HMODULE(WINAPI*)(WIND::LPCWSTR, __reserved WIND::HANDLE, unsigned long))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCallNamedPipeW = RTN_FindByName(img, "CallNamedPipeW");
        if (RTN_Valid(rtnCallNamedPipeW) && RTN_IsSafeForProbedReplacement(rtnCallNamedPipeW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CallNamedPipeW at " << RTN_Address(rtnCallNamedPipeW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnCallNamedPipeW, AFUNPTR(myCallNamedPipeW)));
            fptrCallNamedPipeW = (WIND::BOOL(WINAPI*)(WIND::LPCWSTR, __in_bcount_opt(nInBufferSize) WIND::LPVOID, WIND::DWORD,
                                                      __out_bcount_part_opt(nOutBufferSize, *lpBytesRead) WIND::LPVOID,
                                                      WIND::DWORD, WIND::LPDWORD, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnSendNotifyMessageA = RTN_FindByName(img, "SendNotifyMessageA");
        if (RTN_Valid(rtnSendNotifyMessageA) && RTN_IsSafeForProbedReplacement(rtnSendNotifyMessageA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SendNotifyMessageA at " << RTN_Address(rtnSendNotifyMessageA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr           = (RTN_ReplaceProbed(rtnSendNotifyMessageA, AFUNPTR(mySendNotifyMessageA)));
            fptrSendNotifyMessageA = (WIND::BOOL(WINAPI*)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnInitializeCriticalSectionAndSpinCount = RTN_FindByName(img, "InitializeCriticalSectionAndSpinCount");
        if (RTN_Valid(rtnInitializeCriticalSectionAndSpinCount) &&
            RTN_IsSafeForProbedReplacement(rtnInitializeCriticalSectionAndSpinCount))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for InitializeCriticalSectionAndSpinCount at "
                    << RTN_Address(rtnInitializeCriticalSectionAndSpinCount) << std::endl;
            OutFile.flush();
            AFUNPTR fptr =
                (RTN_ReplaceProbed(rtnInitializeCriticalSectionAndSpinCount, AFUNPTR(myInitializeCriticalSectionAndSpinCount)));
            fptrInitializeCriticalSectionAndSpinCount = (WIND::BOOL(WINAPI*)(WIND::LPCRITICAL_SECTION, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("comdlg32.dll") != std::string::npos) || (IMG_Name(img).find("COMDLG32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("COMDLG32.dll") != std::string::npos))

    {
        RTN rtnPrintDlgW = RTN_FindByName(img, "PrintDlgW");
        if (RTN_Valid(rtnPrintDlgW) && RTN_IsSafeForProbedReplacement(rtnPrintDlgW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PrintDlgW at " << RTN_Address(rtnPrintDlgW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr  = (RTN_ReplaceProbed(rtnPrintDlgW, AFUNPTR(myPrintDlgW)));
            fptrPrintDlgW = (WIND::BOOL(APIENTRY*)(WIND::LPPRINTDLGW))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcMgmtEpEltInqNextW = RTN_FindByName(img, "RpcMgmtEpEltInqNextW");
        if (RTN_Valid(rtnRpcMgmtEpEltInqNextW) && RTN_IsSafeForProbedReplacement(rtnRpcMgmtEpEltInqNextW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcMgmtEpEltInqNextW at " << RTN_Address(rtnRpcMgmtEpEltInqNextW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr             = (RTN_ReplaceProbed(rtnRpcMgmtEpEltInqNextW, AFUNPTR(myRpcMgmtEpEltInqNextW)));
            fptrRpcMgmtEpEltInqNextW = (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_EP_INQ_HANDLE, WIND::RPC_IF_ID __RPC_FAR*,
                                                                  WIND::RPC_BINDING_HANDLE __RPC_FAR*, WIND::UUID __RPC_FAR*,
                                                                  __deref_opt_out WIND::RPC_WSTR __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcMgmtEpEltInqDone = RTN_FindByName(img, "RpcMgmtEpEltInqDone");
        if (RTN_Valid(rtnRpcMgmtEpEltInqDone) && RTN_IsSafeForProbedReplacement(rtnRpcMgmtEpEltInqDone))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcMgmtEpEltInqDone at " << RTN_Address(rtnRpcMgmtEpEltInqDone) << std::endl;
            OutFile.flush();
            AFUNPTR fptr            = (RTN_ReplaceProbed(rtnRpcMgmtEpEltInqDone, AFUNPTR(myRpcMgmtEpEltInqDone)));
            fptrRpcMgmtEpEltInqDone = (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_EP_INQ_HANDLE __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnPostMessageA = RTN_FindByName(img, "PostMessageA");
        if (RTN_Valid(rtnPostMessageA) && RTN_IsSafeForProbedReplacement(rtnPostMessageA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PostMessageA at " << RTN_Address(rtnPostMessageA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnPostMessageA, AFUNPTR(myPostMessageA)));
            fptrPostMessageA = (WIND::BOOL(WINAPI*)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnTerminateThread = RTN_FindByName(img, "TerminateThread");
        if (RTN_Valid(rtnTerminateThread) && RTN_IsSafeForProbedReplacement(rtnTerminateThread))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for TerminateThread at " << RTN_Address(rtnTerminateThread) << std::endl;
            OutFile.flush();
            AFUNPTR fptr        = (RTN_ReplaceProbed(rtnTerminateThread, AFUNPTR(myTerminateThread)));
            fptrTerminateThread = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnLoadLibraryW = RTN_FindByName(img, "LoadLibraryW");
        if (RTN_Valid(rtnLoadLibraryW) && RTN_IsSafeForProbedReplacement(rtnLoadLibraryW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for LoadLibraryW at " << RTN_Address(rtnLoadLibraryW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnLoadLibraryW, AFUNPTR(myLoadLibraryW)));
            fptrLoadLibraryW = (WIND::HMODULE(WINAPI*)(WIND::LPCWSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateMailslotW = RTN_FindByName(img, "CreateMailslotW");
        if (RTN_Valid(rtnCreateMailslotW) && RTN_IsSafeForProbedReplacement(rtnCreateMailslotW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateMailslotW at " << RTN_Address(rtnCreateMailslotW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnCreateMailslotW, AFUNPTR(myCreateMailslotW)));
            fptrCreateMailslotW =
                (WIND::HANDLE(WINAPI*)(WIND::LPCWSTR, WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnSetEvent = RTN_FindByName(img, "SetEvent");
        if (RTN_Valid(rtnSetEvent) && RTN_IsSafeForProbedReplacement(rtnSetEvent))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SetEvent at " << RTN_Address(rtnSetEvent) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnSetEvent, AFUNPTR(mySetEvent)));
            fptrSetEvent = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnReadConsoleInputA = RTN_FindByName(img, "ReadConsoleInputA");
        if (RTN_Valid(rtnReadConsoleInputA) && RTN_IsSafeForProbedReplacement(rtnReadConsoleInputA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReadConsoleInputA at " << RTN_Address(rtnReadConsoleInputA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr          = (RTN_ReplaceProbed(rtnReadConsoleInputA, AFUNPTR(myReadConsoleInputA)));
            fptrReadConsoleInputA = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::PINPUT_RECORD, WIND::DWORD, WIND::LPDWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("advapi32.dll") != std::string::npos) || (IMG_Name(img).find("ADVAPI32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("ADVAPI32.dll") != std::string::npos))

    {
        RTN rtnCreateProcessWithLogonW = RTN_FindByName(img, "CreateProcessWithLogonW");
        if (RTN_Valid(rtnCreateProcessWithLogonW) && RTN_IsSafeForProbedReplacement(rtnCreateProcessWithLogonW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateProcessWithLogonW at " << RTN_Address(rtnCreateProcessWithLogonW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                = (RTN_ReplaceProbed(rtnCreateProcessWithLogonW, AFUNPTR(myCreateProcessWithLogonW)));
            fptrCreateProcessWithLogonW = (WIND::BOOL(WINAPI*)(
                WIND::LPCWSTR, WIND::LPCWSTR, WIND::LPCWSTR, WIND::DWORD, WIND::LPCWSTR, WIND::LPWSTR, WIND::DWORD, WIND::LPVOID,
                WIND::LPCWSTR, WIND::LPSTARTUPINFOW, WIND::LPPROCESS_INFORMATION))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCallNamedPipeA = RTN_FindByName(img, "CallNamedPipeA");
        if (RTN_Valid(rtnCallNamedPipeA) && RTN_IsSafeForProbedReplacement(rtnCallNamedPipeA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CallNamedPipeA at " << RTN_Address(rtnCallNamedPipeA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnCallNamedPipeA, AFUNPTR(myCallNamedPipeA)));
            fptrCallNamedPipeA = (WIND::BOOL(WINAPI*)(WIND::LPCSTR, __in_bcount_opt(nInBufferSize) WIND::LPVOID, WIND::DWORD,
                                                      __out_bcount_part_opt(nOutBufferSize, *lpBytesRead) WIND::LPVOID,
                                                      WIND::DWORD, WIND::LPDWORD, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnSendNotifyMessageW = RTN_FindByName(img, "SendNotifyMessageW");
        if (RTN_Valid(rtnSendNotifyMessageW) && RTN_IsSafeForProbedReplacement(rtnSendNotifyMessageW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SendNotifyMessageW at " << RTN_Address(rtnSendNotifyMessageW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr           = (RTN_ReplaceProbed(rtnSendNotifyMessageW, AFUNPTR(mySendNotifyMessageW)));
            fptrSendNotifyMessageW = (WIND::BOOL(WINAPI*)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateMailslotA = RTN_FindByName(img, "CreateMailslotA");
        if (RTN_Valid(rtnCreateMailslotA) && RTN_IsSafeForProbedReplacement(rtnCreateMailslotA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateMailslotA at " << RTN_Address(rtnCreateMailslotA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnCreateMailslotA, AFUNPTR(myCreateMailslotA)));
            fptrCreateMailslotA =
                (WIND::HANDLE(WINAPI*)(WIND::LPCSTR, WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnselect = RTN_FindByName(img, "select");
        if (RTN_Valid(rtnselect) && RTN_IsSafeForProbedReplacement(rtnselect))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for select at " << RTN_Address(rtnselect) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnselect, AFUNPTR(myselect)));
            fptrselect =
                (int(WSAAPI*)(int, WIND::fd_set FAR*, WIND::fd_set FAR*, WIND::fd_set FAR*, const struct timeval FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateRemoteThread = RTN_FindByName(img, "CreateRemoteThread");
        if (RTN_Valid(rtnCreateRemoteThread) && RTN_IsSafeForProbedReplacement(rtnCreateRemoteThread))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateRemoteThread at " << RTN_Address(rtnCreateRemoteThread) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnCreateRemoteThread, AFUNPTR(myCreateRemoteThread)));
            fptrCreateRemoteThread =
                (WIND::HANDLE(WINAPI*)(WIND::HANDLE, WIND::LPSECURITY_ATTRIBUTES, WIND::SIZE_T, WIND::LPTHREAD_START_ROUTINE,
                                       WIND::LPVOID, WIND::DWORD, WIND::LPDWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnSetWindowsHookExA = RTN_FindByName(img, "SetWindowsHookExA");
        if (RTN_Valid(rtnSetWindowsHookExA) && RTN_IsSafeForProbedReplacement(rtnSetWindowsHookExA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SetWindowsHookExA at " << RTN_Address(rtnSetWindowsHookExA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr          = (RTN_ReplaceProbed(rtnSetWindowsHookExA, AFUNPTR(mySetWindowsHookExA)));
            fptrSetWindowsHookExA = (WIND::HHOOK(WINAPI*)(int, WIND::HOOKPROC, WIND::HINSTANCE, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnBroadcastSystemMessageExA = RTN_FindByName(img, "BroadcastSystemMessageExA");
        if (RTN_Valid(rtnBroadcastSystemMessageExA) && RTN_IsSafeForProbedReplacement(rtnBroadcastSystemMessageExA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for BroadcastSystemMessageExA at " << RTN_Address(rtnBroadcastSystemMessageExA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnBroadcastSystemMessageExA, AFUNPTR(myBroadcastSystemMessageExA)));
            fptrBroadcastSystemMessageExA =
                (long(WINAPI*)(WIND::DWORD, WIND::LPDWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM, WIND::PBSMINFO))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenProcess = RTN_FindByName(img, "OpenProcess");
        if (RTN_Valid(rtnOpenProcess) && RTN_IsSafeForProbedReplacement(rtnOpenProcess))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenProcess at " << RTN_Address(rtnOpenProcess) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnOpenProcess, AFUNPTR(myOpenProcess)));
            fptrOpenProcess = (WIND::HANDLE(WINAPI*)(WIND::DWORD, WIND::BOOL, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSAWaitForMultipleEvents = RTN_FindByName(img, "WSAWaitForMultipleEvents");
        if (RTN_Valid(rtnWSAWaitForMultipleEvents) && RTN_IsSafeForProbedReplacement(rtnWSAWaitForMultipleEvents))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSAWaitForMultipleEvents at " << RTN_Address(rtnWSAWaitForMultipleEvents) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnWSAWaitForMultipleEvents, AFUNPTR(myWSAWaitForMultipleEvents)));
            fptrWSAWaitForMultipleEvents =
                (WIND::DWORD(WSAAPI*)(WIND::DWORD, const WIND::WSAEVENT FAR*, WIND::BOOL, WIND::DWORD, WIND::BOOL))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcMgmtEpEltInqBegin = RTN_FindByName(img, "RpcMgmtEpEltInqBegin");
        if (RTN_Valid(rtnRpcMgmtEpEltInqBegin) && RTN_IsSafeForProbedReplacement(rtnRpcMgmtEpEltInqBegin))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcMgmtEpEltInqBegin at " << RTN_Address(rtnRpcMgmtEpEltInqBegin) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnRpcMgmtEpEltInqBegin, AFUNPTR(myRpcMgmtEpEltInqBegin)));
            fptrRpcMgmtEpEltInqBegin =
                (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_BINDING_HANDLE, unsigned long, WIND::RPC_IF_ID __RPC_FAR*, unsigned long,
                                           WIND::UUID __RPC_FAR*, WIND::RPC_EP_INQ_HANDLE __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWriteFileGather = RTN_FindByName(img, "WriteFileGather");
        if (RTN_Valid(rtnWriteFileGather) && RTN_IsSafeForProbedReplacement(rtnWriteFileGather))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WriteFileGather at " << RTN_Address(rtnWriteFileGather) << std::endl;
            OutFile.flush();
            AFUNPTR fptr        = (RTN_ReplaceProbed(rtnWriteFileGather, AFUNPTR(myWriteFileGather)));
            fptrWriteFileGather = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::FILE_SEGMENT_ELEMENT**, WIND::DWORD,
                                                       __reserved WIND::LPDWORD, WIND::LPOVERLAPPED))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWaitForMultipleObjects = RTN_FindByName(img, "WaitForMultipleObjects");
        if (RTN_Valid(rtnWaitForMultipleObjects) && RTN_IsSafeForProbedReplacement(rtnWaitForMultipleObjects))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WaitForMultipleObjects at " << RTN_Address(rtnWaitForMultipleObjects) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnWaitForMultipleObjects, AFUNPTR(myWaitForMultipleObjects)));
            fptrWaitForMultipleObjects =
                (WIND::DWORD(WINAPI*)(WIND::DWORD, __in_ecount(nCount) CONST WIND::HANDLE*, WIND::BOOL, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnNdrSendReceive = RTN_FindByName(img, "NdrSendReceive");
        if (RTN_Valid(rtnNdrSendReceive) && RTN_IsSafeForProbedReplacement(rtnNdrSendReceive))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for NdrSendReceive at " << RTN_Address(rtnNdrSendReceive) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnNdrSendReceive, AFUNPTR(myNdrSendReceive)));
            fptrNdrSendReceive = (unsigned char*(WINAPI*)(WIND::PMIDL_STUB_MESSAGE, unsigned char*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateWaitableTimerW = RTN_FindByName(img, "CreateWaitableTimerW");
        if (RTN_Valid(rtnCreateWaitableTimerW) && RTN_IsSafeForProbedReplacement(rtnCreateWaitableTimerW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateWaitableTimerW at " << RTN_Address(rtnCreateWaitableTimerW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr             = (RTN_ReplaceProbed(rtnCreateWaitableTimerW, AFUNPTR(myCreateWaitableTimerW)));
            fptrCreateWaitableTimerW = (WIND::HANDLE(WINAPI*)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::LPCWSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnWaitMessage = RTN_FindByName(img, "WaitMessage");
        if (RTN_Valid(rtnWaitMessage) && RTN_IsSafeForProbedReplacement(rtnWaitMessage))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WaitMessage at " << RTN_Address(rtnWaitMessage) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnWaitMessage, AFUNPTR(myWaitMessage)));
            fptrWaitMessage = (WIND::BOOL(WINAPI*)())fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnSetCriticalSectionSpinCount = RTN_FindByName(img, "SetCriticalSectionSpinCount");
        if (RTN_Valid(rtnSetCriticalSectionSpinCount) && RTN_IsSafeForProbedReplacement(rtnSetCriticalSectionSpinCount))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SetCriticalSectionSpinCount at " << RTN_Address(rtnSetCriticalSectionSpinCount)
                    << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnSetCriticalSectionSpinCount, AFUNPTR(mySetCriticalSectionSpinCount)));
            fptrSetCriticalSectionSpinCount = (WIND::DWORD(WINAPI*)(WIND::LPCRITICAL_SECTION, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWaitNamedPipeW = RTN_FindByName(img, "WaitNamedPipeW");
        if (RTN_Valid(rtnWaitNamedPipeW) && RTN_IsSafeForProbedReplacement(rtnWaitNamedPipeW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WaitNamedPipeW at " << RTN_Address(rtnWaitNamedPipeW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnWaitNamedPipeW, AFUNPTR(myWaitNamedPipeW)));
            fptrWaitNamedPipeW = (WIND::BOOL(WINAPI*)(WIND::LPCWSTR, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnsendto = RTN_FindByName(img, "sendto");
        if (RTN_Valid(rtnsendto) && RTN_IsSafeForProbedReplacement(rtnsendto))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for sendto at " << RTN_Address(rtnsendto) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnsendto, AFUNPTR(mysendto)));
            fptrsendto   = (int(WSAAPI*)(WIND::SOCKET, const char FAR*, int, int, const struct sockaddr FAR*, int))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWaitNamedPipeA = RTN_FindByName(img, "WaitNamedPipeA");
        if (RTN_Valid(rtnWaitNamedPipeA) && RTN_IsSafeForProbedReplacement(rtnWaitNamedPipeA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WaitNamedPipeA at " << RTN_Address(rtnWaitNamedPipeA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnWaitNamedPipeA, AFUNPTR(myWaitNamedPipeA)));
            fptrWaitNamedPipeA = (WIND::BOOL(WINAPI*)(WIND::LPCSTR, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnResetEvent = RTN_FindByName(img, "ResetEvent");
        if (RTN_Valid(rtnResetEvent) && RTN_IsSafeForProbedReplacement(rtnResetEvent))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ResetEvent at " << RTN_Address(rtnResetEvent) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnResetEvent, AFUNPTR(myResetEvent)));
            fptrResetEvent = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateTimerQueueTimer = RTN_FindByName(img, "CreateTimerQueueTimer");
        if (RTN_Valid(rtnCreateTimerQueueTimer) && RTN_IsSafeForProbedReplacement(rtnCreateTimerQueueTimer))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateTimerQueueTimer at " << RTN_Address(rtnCreateTimerQueueTimer) << std::endl;
            OutFile.flush();
            AFUNPTR fptr              = (RTN_ReplaceProbed(rtnCreateTimerQueueTimer, AFUNPTR(myCreateTimerQueueTimer)));
            fptrCreateTimerQueueTimer = (WIND::BOOL(WINAPI*)(WIND::PHANDLE, WIND::HANDLE, WIND::WAITORTIMERCALLBACK, WIND::PVOID,
                                                             WIND::DWORD, WIND::DWORD, WIND::ULONG))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateWaitableTimerA = RTN_FindByName(img, "CreateWaitableTimerA");
        if (RTN_Valid(rtnCreateWaitableTimerA) && RTN_IsSafeForProbedReplacement(rtnCreateWaitableTimerA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateWaitableTimerA at " << RTN_Address(rtnCreateWaitableTimerA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr             = (RTN_ReplaceProbed(rtnCreateWaitableTimerA, AFUNPTR(myCreateWaitableTimerA)));
            fptrCreateWaitableTimerA = (WIND::HANDLE(WINAPI*)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::LPCSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnDuplicateHandle = RTN_FindByName(img, "DuplicateHandle");
        if (RTN_Valid(rtnDuplicateHandle) && RTN_IsSafeForProbedReplacement(rtnDuplicateHandle))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for DuplicateHandle at " << RTN_Address(rtnDuplicateHandle) << std::endl;
            OutFile.flush();
            AFUNPTR fptr        = (RTN_ReplaceProbed(rtnDuplicateHandle, AFUNPTR(myDuplicateHandle)));
            fptrDuplicateHandle = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::HANDLE, WIND::HANDLE, WIND::LPHANDLE, WIND::DWORD,
                                                       WIND::BOOL, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnclosesocket = RTN_FindByName(img, "closesocket");
        if (RTN_Valid(rtnclosesocket) && RTN_IsSafeForProbedReplacement(rtnclosesocket))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for closesocket at " << RTN_Address(rtnclosesocket) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnclosesocket, AFUNPTR(myclosesocket)));
            fptrclosesocket = (int(WSAAPI*)(WIND::SOCKET))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnGetProcAddress = RTN_FindByName(img, "GetProcAddress");
        if (RTN_Valid(rtnGetProcAddress) && RTN_IsSafeForProbedReplacement(rtnGetProcAddress))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for GetProcAddress at " << RTN_Address(rtnGetProcAddress) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnGetProcAddress, AFUNPTR(myGetProcAddress)));
            fptrGetProcAddress = (WIND::FARPROC(WINAPI*)(WIND::HMODULE, WIND::LPCSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnsocket = RTN_FindByName(img, "socket");
        if (RTN_Valid(rtnsocket) && RTN_IsSafeForProbedReplacement(rtnsocket))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for socket at " << RTN_Address(rtnsocket) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnsocket, AFUNPTR(mysocket)));
            fptrsocket   = (WIND::SOCKET(WSAAPI*)(int, int, int))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSASetEvent = RTN_FindByName(img, "WSASetEvent");
        if (RTN_Valid(rtnWSASetEvent) && RTN_IsSafeForProbedReplacement(rtnWSASetEvent))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSASetEvent at " << RTN_Address(rtnWSASetEvent) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnWSASetEvent, AFUNPTR(myWSASetEvent)));
            fptrWSASetEvent = (WIND::BOOL(WSAAPI*)(WIND::WSAEVENT))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateEventW = RTN_FindByName(img, "CreateEventW");
        if (RTN_Valid(rtnCreateEventW) && RTN_IsSafeForProbedReplacement(rtnCreateEventW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateEventW at " << RTN_Address(rtnCreateEventW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnCreateEventW, AFUNPTR(myCreateEventW)));
            fptrCreateEventW = (WIND::HANDLE(WINAPI*)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::BOOL, WIND::LPCWSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnPostThreadMessageW = RTN_FindByName(img, "PostThreadMessageW");
        if (RTN_Valid(rtnPostThreadMessageW) && RTN_IsSafeForProbedReplacement(rtnPostThreadMessageW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PostThreadMessageW at " << RTN_Address(rtnPostThreadMessageW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr           = (RTN_ReplaceProbed(rtnPostThreadMessageW, AFUNPTR(myPostThreadMessageW)));
            fptrPostThreadMessageW = (WIND::BOOL(WINAPI*)(WIND::DWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSASendTo = RTN_FindByName(img, "WSASendTo");
        if (RTN_Valid(rtnWSASendTo) && RTN_IsSafeForProbedReplacement(rtnWSASendTo))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSASendTo at " << RTN_Address(rtnWSASendTo) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnWSASendTo, AFUNPTR(myWSASendTo)));
            fptrWSASendTo =
                (int(WSAAPI*)(WIND::SOCKET, WIND::LPWSABUF, WIND::DWORD, WIND::LPDWORD, WIND::DWORD, const struct sockaddr FAR*,
                              int, WIND::LPWSAOVERLAPPED, WIND::LPWSAOVERLAPPED_COMPLETION_ROUTINE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateFileW = RTN_FindByName(img, "CreateFileW");
        if (RTN_Valid(rtnCreateFileW) && RTN_IsSafeForProbedReplacement(rtnCreateFileW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateFileW at " << RTN_Address(rtnCreateFileW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnCreateFileW, AFUNPTR(myCreateFileW)));
            fptrCreateFileW = (WIND::HANDLE(WINAPI*)(WIND::LPCWSTR, WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES,
                                                     WIND::DWORD, WIND::DWORD, WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateEventA = RTN_FindByName(img, "CreateEventA");
        if (RTN_Valid(rtnCreateEventA) && RTN_IsSafeForProbedReplacement(rtnCreateEventA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateEventA at " << RTN_Address(rtnCreateEventA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnCreateEventA, AFUNPTR(myCreateEventA)));
            fptrCreateEventA = (WIND::HANDLE(WINAPI*)(WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::BOOL, WIND::LPCSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcServerListen = RTN_FindByName(img, "RpcServerListen");
        if (RTN_Valid(rtnRpcServerListen) && RTN_IsSafeForProbedReplacement(rtnRpcServerListen))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcServerListen at " << RTN_Address(rtnRpcServerListen) << std::endl;
            OutFile.flush();
            AFUNPTR fptr        = (RTN_ReplaceProbed(rtnRpcServerListen, AFUNPTR(myRpcServerListen)));
            fptrRpcServerListen = (WIND::RPC_STATUS(WINAPI*)(unsigned int, unsigned int, unsigned int))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateFileA = RTN_FindByName(img, "CreateFileA");
        if (RTN_Valid(rtnCreateFileA) && RTN_IsSafeForProbedReplacement(rtnCreateFileA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateFileA at " << RTN_Address(rtnCreateFileA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnCreateFileA, AFUNPTR(myCreateFileA)));
            fptrCreateFileA = (WIND::HANDLE(WINAPI*)(WIND::LPCSTR, WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES,
                                                     WIND::DWORD, WIND::DWORD, WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnDeleteTimerQueueEx = RTN_FindByName(img, "DeleteTimerQueueEx");
        if (RTN_Valid(rtnDeleteTimerQueueEx) && RTN_IsSafeForProbedReplacement(rtnDeleteTimerQueueEx))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for DeleteTimerQueueEx at " << RTN_Address(rtnDeleteTimerQueueEx) << std::endl;
            OutFile.flush();
            AFUNPTR fptr           = (RTN_ReplaceProbed(rtnDeleteTimerQueueEx, AFUNPTR(myDeleteTimerQueueEx)));
            fptrDeleteTimerQueueEx = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnSendMessageTimeoutW = RTN_FindByName(img, "SendMessageTimeoutW");
        if (RTN_Valid(rtnSendMessageTimeoutW) && RTN_IsSafeForProbedReplacement(rtnSendMessageTimeoutW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SendMessageTimeoutW at " << RTN_Address(rtnSendMessageTimeoutW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr            = (RTN_ReplaceProbed(rtnSendMessageTimeoutW, AFUNPTR(mySendMessageTimeoutW)));
            fptrSendMessageTimeoutW = (WIND::LRESULT(WINAPI*)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM, WIND::UINT,
                                                              WIND::UINT, WIND::PDWORD_PTR))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnPostThreadMessageA = RTN_FindByName(img, "PostThreadMessageA");
        if (RTN_Valid(rtnPostThreadMessageA) && RTN_IsSafeForProbedReplacement(rtnPostThreadMessageA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PostThreadMessageA at " << RTN_Address(rtnPostThreadMessageA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr           = (RTN_ReplaceProbed(rtnPostThreadMessageA, AFUNPTR(myPostThreadMessageA)));
            fptrPostThreadMessageA = (WIND::BOOL(WINAPI*)(WIND::DWORD, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSASocketW = RTN_FindByName(img, "WSASocketW");
        if (RTN_Valid(rtnWSASocketW) && RTN_IsSafeForProbedReplacement(rtnWSASocketW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSASocketW at " << RTN_Address(rtnWSASocketW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnWSASocketW, AFUNPTR(myWSASocketW)));
            fptrWSASocketW = (WIND::SOCKET(WSAAPI*)(int, int, int, WIND::LPWSAPROTOCOL_INFOW, WIND::GROUP, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSAGetOverlappedResult = RTN_FindByName(img, "WSAGetOverlappedResult");
        if (RTN_Valid(rtnWSAGetOverlappedResult) && RTN_IsSafeForProbedReplacement(rtnWSAGetOverlappedResult))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSAGetOverlappedResult at " << RTN_Address(rtnWSAGetOverlappedResult) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnWSAGetOverlappedResult, AFUNPTR(myWSAGetOverlappedResult)));
            fptrWSAGetOverlappedResult =
                (WIND::BOOL(WSAAPI*)(WIND::SOCKET, WIND::LPWSAOVERLAPPED, WIND::LPDWORD, WIND::BOOL, WIND::LPDWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSAConnect = RTN_FindByName(img, "WSAConnect");
        if (RTN_Valid(rtnWSAConnect) && RTN_IsSafeForProbedReplacement(rtnWSAConnect))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSAConnect at " << RTN_Address(rtnWSAConnect) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnWSAConnect, AFUNPTR(myWSAConnect)));
            fptrWSAConnect = (int(WSAAPI*)(WIND::SOCKET, const struct sockaddr FAR*, int, WIND::LPWSABUF, WIND::LPWSABUF,
                                           WIND::LPQOS, WIND::LPQOS))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnFindFirstChangeNotificationA = RTN_FindByName(img, "FindFirstChangeNotificationA");
        if (RTN_Valid(rtnFindFirstChangeNotificationA) && RTN_IsSafeForProbedReplacement(rtnFindFirstChangeNotificationA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for FindFirstChangeNotificationA at " << RTN_Address(rtnFindFirstChangeNotificationA)
                    << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnFindFirstChangeNotificationA, AFUNPTR(myFindFirstChangeNotificationA)));
            fptrFindFirstChangeNotificationA = (WIND::HANDLE(WINAPI*)(WIND::LPCSTR, WIND::BOOL, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateNamedPipeW = RTN_FindByName(img, "CreateNamedPipeW");
        if (RTN_Valid(rtnCreateNamedPipeW) && RTN_IsSafeForProbedReplacement(rtnCreateNamedPipeW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateNamedPipeW at " << RTN_Address(rtnCreateNamedPipeW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnCreateNamedPipeW, AFUNPTR(myCreateNamedPipeW)));
            fptrCreateNamedPipeW = (WIND::HANDLE(WINAPI*)(WIND::LPCWSTR, WIND::DWORD, WIND::DWORD, WIND::DWORD, WIND::DWORD,
                                                          WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnaccept = RTN_FindByName(img, "accept");
        if (RTN_Valid(rtnaccept) && RTN_IsSafeForProbedReplacement(rtnaccept))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for accept at " << RTN_Address(rtnaccept) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnaccept, AFUNPTR(myaccept)));
            fptraccept   = (WIND::SOCKET(WSAAPI*)(WIND::SOCKET, struct sockaddr FAR*, int FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenSemaphoreA = RTN_FindByName(img, "OpenSemaphoreA");
        if (RTN_Valid(rtnOpenSemaphoreA) && RTN_IsSafeForProbedReplacement(rtnOpenSemaphoreA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenSemaphoreA at " << RTN_Address(rtnOpenSemaphoreA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnOpenSemaphoreA, AFUNPTR(myOpenSemaphoreA)));
            fptrOpenSemaphoreA = (WIND::HANDLE(WINAPI*)(WIND::DWORD, WIND::BOOL, WIND::LPCSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnUnregisterWaitEx = RTN_FindByName(img, "UnregisterWaitEx");
        if (RTN_Valid(rtnUnregisterWaitEx) && RTN_IsSafeForProbedReplacement(rtnUnregisterWaitEx))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for UnregisterWaitEx at " << RTN_Address(rtnUnregisterWaitEx) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnUnregisterWaitEx, AFUNPTR(myUnregisterWaitEx)));
            fptrUnregisterWaitEx = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcMgmtIsServerListening = RTN_FindByName(img, "RpcMgmtIsServerListening");
        if (RTN_Valid(rtnRpcMgmtIsServerListening) && RTN_IsSafeForProbedReplacement(rtnRpcMgmtIsServerListening))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcMgmtIsServerListening at " << RTN_Address(rtnRpcMgmtIsServerListening) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                 = (RTN_ReplaceProbed(rtnRpcMgmtIsServerListening, AFUNPTR(myRpcMgmtIsServerListening)));
            fptrRpcMgmtIsServerListening = (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_BINDING_HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWaitForSingleObjectEx = RTN_FindByName(img, "WaitForSingleObjectEx");
        if (RTN_Valid(rtnWaitForSingleObjectEx) && RTN_IsSafeForProbedReplacement(rtnWaitForSingleObjectEx))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WaitForSingleObjectEx at " << RTN_Address(rtnWaitForSingleObjectEx) << std::endl;
            OutFile.flush();
            AFUNPTR fptr              = (RTN_ReplaceProbed(rtnWaitForSingleObjectEx, AFUNPTR(myWaitForSingleObjectEx)));
            fptrWaitForSingleObjectEx = (WIND::DWORD(WINAPI*)(WIND::HANDLE, WIND::DWORD, WIND::BOOL))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateNamedPipeA = RTN_FindByName(img, "CreateNamedPipeA");
        if (RTN_Valid(rtnCreateNamedPipeA) && RTN_IsSafeForProbedReplacement(rtnCreateNamedPipeA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateNamedPipeA at " << RTN_Address(rtnCreateNamedPipeA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnCreateNamedPipeA, AFUNPTR(myCreateNamedPipeA)));
            fptrCreateNamedPipeA = (WIND::HANDLE(WINAPI*)(WIND::LPCSTR, WIND::DWORD, WIND::DWORD, WIND::DWORD, WIND::DWORD,
                                                          WIND::DWORD, WIND::DWORD, WIND::LPSECURITY_ATTRIBUTES))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnFindFirstChangeNotificationW = RTN_FindByName(img, "FindFirstChangeNotificationW");
        if (RTN_Valid(rtnFindFirstChangeNotificationW) && RTN_IsSafeForProbedReplacement(rtnFindFirstChangeNotificationW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for FindFirstChangeNotificationW at " << RTN_Address(rtnFindFirstChangeNotificationW)
                    << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnFindFirstChangeNotificationW, AFUNPTR(myFindFirstChangeNotificationW)));
            fptrFindFirstChangeNotificationW = (WIND::HANDLE(WINAPI*)(WIND::LPCWSTR, WIND::BOOL, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnMessageBoxExA = RTN_FindByName(img, "MessageBoxExA");
        if (RTN_Valid(rtnMessageBoxExA) && RTN_IsSafeForProbedReplacement(rtnMessageBoxExA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for MessageBoxExA at " << RTN_Address(rtnMessageBoxExA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr      = (RTN_ReplaceProbed(rtnMessageBoxExA, AFUNPTR(myMessageBoxExA)));
            fptrMessageBoxExA = (int(WINAPI*)(WIND::HWND, WIND::LPCSTR, WIND::LPCSTR, WIND::UINT, WIND::WORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnGetQueuedCompletionStatus = RTN_FindByName(img, "GetQueuedCompletionStatus");
        if (RTN_Valid(rtnGetQueuedCompletionStatus) && RTN_IsSafeForProbedReplacement(rtnGetQueuedCompletionStatus))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for GetQueuedCompletionStatus at " << RTN_Address(rtnGetQueuedCompletionStatus) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnGetQueuedCompletionStatus, AFUNPTR(myGetQueuedCompletionStatus)));
            fptrGetQueuedCompletionStatus =
                (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::LPDWORD, WIND::PULONG_PTR, WIND::LPOVERLAPPED, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCancelWaitableTimer = RTN_FindByName(img, "CancelWaitableTimer");
        if (RTN_Valid(rtnCancelWaitableTimer) && RTN_IsSafeForProbedReplacement(rtnCancelWaitableTimer))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CancelWaitableTimer at " << RTN_Address(rtnCancelWaitableTimer) << std::endl;
            OutFile.flush();
            AFUNPTR fptr            = (RTN_ReplaceProbed(rtnCancelWaitableTimer, AFUNPTR(myCancelWaitableTimer)));
            fptrCancelWaitableTimer = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnMessageBoxW = RTN_FindByName(img, "MessageBoxW");
        if (RTN_Valid(rtnMessageBoxW) && RTN_IsSafeForProbedReplacement(rtnMessageBoxW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for MessageBoxW at " << RTN_Address(rtnMessageBoxW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnMessageBoxW, AFUNPTR(myMessageBoxW)));
            fptrMessageBoxW = (int(WINAPI*)(WIND::HWND, WIND::LPCWSTR, WIND::LPCWSTR, WIND::UINT))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSAAccept = RTN_FindByName(img, "WSAAccept");
        if (RTN_Valid(rtnWSAAccept) && RTN_IsSafeForProbedReplacement(rtnWSAAccept))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSAAccept at " << RTN_Address(rtnWSAAccept) << std::endl;
            OutFile.flush();
            AFUNPTR fptr  = (RTN_ReplaceProbed(rtnWSAAccept, AFUNPTR(myWSAAccept)));
            fptrWSAAccept = (WIND::SOCKET(WSAAPI*)(WIND::SOCKET, struct sockaddr FAR*, WIND::LPINT, WIND::LPCONDITIONPROC,
                                                   WIND::DWORD_PTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnUnregisterWait = RTN_FindByName(img, "UnregisterWait");
        if (RTN_Valid(rtnUnregisterWait) && RTN_IsSafeForProbedReplacement(rtnUnregisterWait))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for UnregisterWait at " << RTN_Address(rtnUnregisterWait) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnUnregisterWait, AFUNPTR(myUnregisterWait)));
            fptrUnregisterWait = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("ws2_32.dll") != std::string::npos) || (IMG_Name(img).find("WS2_32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("WS2_32.dll") != std::string::npos))

    {
        RTN rtnWSACloseEvent = RTN_FindByName(img, "WSACloseEvent");
        if (RTN_Valid(rtnWSACloseEvent) && RTN_IsSafeForProbedReplacement(rtnWSACloseEvent))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WSACloseEvent at " << RTN_Address(rtnWSACloseEvent) << std::endl;
            OutFile.flush();
            AFUNPTR fptr      = (RTN_ReplaceProbed(rtnWSACloseEvent, AFUNPTR(myWSACloseEvent)));
            fptrWSACloseEvent = (WIND::BOOL(WSAAPI*)(WIND::WSAEVENT))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateIoCompletionPort = RTN_FindByName(img, "CreateIoCompletionPort");
        if (RTN_Valid(rtnCreateIoCompletionPort) && RTN_IsSafeForProbedReplacement(rtnCreateIoCompletionPort))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateIoCompletionPort at " << RTN_Address(rtnCreateIoCompletionPort) << std::endl;
            OutFile.flush();
            AFUNPTR fptr               = (RTN_ReplaceProbed(rtnCreateIoCompletionPort, AFUNPTR(myCreateIoCompletionPort)));
            fptrCreateIoCompletionPort = (WIND::HANDLE(WINAPI*)(WIND::HANDLE, WIND::HANDLE, WIND::ULONG_PTR, WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnDialogBoxParamW = RTN_FindByName(img, "DialogBoxParamW");
        if (RTN_Valid(rtnDialogBoxParamW) && RTN_IsSafeForProbedReplacement(rtnDialogBoxParamW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for DialogBoxParamW at " << RTN_Address(rtnDialogBoxParamW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnDialogBoxParamW, AFUNPTR(myDialogBoxParamW)));
            fptrDialogBoxParamW =
                (WIND::INT_PTR(WINAPI*)(WIND::HINSTANCE, WIND::LPCWSTR, WIND::HWND, WIND::DLGPROC, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnMessageBoxA = RTN_FindByName(img, "MessageBoxA");
        if (RTN_Valid(rtnMessageBoxA) && RTN_IsSafeForProbedReplacement(rtnMessageBoxA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for MessageBoxA at " << RTN_Address(rtnMessageBoxA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnMessageBoxA, AFUNPTR(myMessageBoxA)));
            fptrMessageBoxA = (int(WINAPI*)(WIND::HWND, WIND::LPCSTR, WIND::LPCSTR, WIND::UINT))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnMessageBoxExW = RTN_FindByName(img, "MessageBoxExW");
        if (RTN_Valid(rtnMessageBoxExW) && RTN_IsSafeForProbedReplacement(rtnMessageBoxExW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for MessageBoxExW at " << RTN_Address(rtnMessageBoxExW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr      = (RTN_ReplaceProbed(rtnMessageBoxExW, AFUNPTR(myMessageBoxExW)));
            fptrMessageBoxExW = (int(WINAPI*)(WIND::HWND, WIND::LPCWSTR, WIND::LPCWSTR, WIND::UINT, WIND::WORD))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnDialogBoxParamA = RTN_FindByName(img, "DialogBoxParamA");
        if (RTN_Valid(rtnDialogBoxParamA) && RTN_IsSafeForProbedReplacement(rtnDialogBoxParamA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for DialogBoxParamA at " << RTN_Address(rtnDialogBoxParamA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnDialogBoxParamA, AFUNPTR(myDialogBoxParamA)));
            fptrDialogBoxParamA =
                (WIND::INT_PTR(WINAPI*)(WIND::HINSTANCE, WIND::LPCSTR, WIND::HWND, WIND::DLGPROC, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnSleep = RTN_FindByName(img, "Sleep");
        if (RTN_Valid(rtnSleep) && RTN_IsSafeForProbedReplacement(rtnSleep))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for Sleep at " << RTN_Address(rtnSleep) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnSleep, AFUNPTR(mySleep)));
            fptrSleep    = (VOID(WINAPI*)(WIND::DWORD))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcns4.dll") != std::string::npos) || (IMG_Name(img).find("RPCNS4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCNS4.dll") != std::string::npos))

    {
        RTN rtnRpcNsBindingLookupDone = RTN_FindByName(img, "RpcNsBindingLookupDone");
        if (RTN_Valid(rtnRpcNsBindingLookupDone) && RTN_IsSafeForProbedReplacement(rtnRpcNsBindingLookupDone))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcNsBindingLookupDone at " << RTN_Address(rtnRpcNsBindingLookupDone) << std::endl;
            OutFile.flush();
            AFUNPTR fptr               = (RTN_ReplaceProbed(rtnRpcNsBindingLookupDone, AFUNPTR(myRpcNsBindingLookupDone)));
            fptrRpcNsBindingLookupDone = (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_NS_HANDLE __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenMutexA = RTN_FindByName(img, "OpenMutexA");
        if (RTN_Valid(rtnOpenMutexA) && RTN_IsSafeForProbedReplacement(rtnOpenMutexA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenMutexA at " << RTN_Address(rtnOpenMutexA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnOpenMutexA, AFUNPTR(myOpenMutexA)));
            fptrOpenMutexA = (WIND::HANDLE(WINAPI*)(WIND::DWORD, WIND::BOOL, WIND::LPCSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnSuspendThread = RTN_FindByName(img, "SuspendThread");
        if (RTN_Valid(rtnSuspendThread) && RTN_IsSafeForProbedReplacement(rtnSuspendThread))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SuspendThread at " << RTN_Address(rtnSuspendThread) << std::endl;
            OutFile.flush();
            AFUNPTR fptr      = (RTN_ReplaceProbed(rtnSuspendThread, AFUNPTR(mySuspendThread)));
            fptrSuspendThread = (WIND::DWORD(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnRaiseException = RTN_FindByName(img, "RaiseException");
        if (RTN_Valid(rtnRaiseException) && RTN_IsSafeForProbedReplacement(rtnRaiseException))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RaiseException at " << RTN_Address(rtnRaiseException) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnRaiseException, AFUNPTR(myRaiseException)));
            fptrRaiseException = (VOID(WINAPI*)(WIND::DWORD, WIND::DWORD, WIND::DWORD,
                                                __in_ecount_opt(nNumberOfArguments) CONST WIND::ULONG_PTR*))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcns4.dll") != std::string::npos) || (IMG_Name(img).find("RPCNS4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCNS4.dll") != std::string::npos))

    {
        RTN rtnRpcNsBindingImportBeginW = RTN_FindByName(img, "RpcNsBindingImportBeginW");
        if (RTN_Valid(rtnRpcNsBindingImportBeginW) && RTN_IsSafeForProbedReplacement(rtnRpcNsBindingImportBeginW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcNsBindingImportBeginW at " << RTN_Address(rtnRpcNsBindingImportBeginW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                 = (RTN_ReplaceProbed(rtnRpcNsBindingImportBeginW, AFUNPTR(myRpcNsBindingImportBeginW)));
            fptrRpcNsBindingImportBeginW = (WIND::RPC_STATUS(WINAPI*)(unsigned long, WIND::RPC_WSTR, WIND::RPC_IF_HANDLE,
                                                                      WIND::UUID __RPC_FAR*, WIND::RPC_NS_HANDLE __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnReleaseSemaphore = RTN_FindByName(img, "ReleaseSemaphore");
        if (RTN_Valid(rtnReleaseSemaphore) && RTN_IsSafeForProbedReplacement(rtnReleaseSemaphore))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReleaseSemaphore at " << RTN_Address(rtnReleaseSemaphore) << std::endl;
            OutFile.flush();
            AFUNPTR fptr         = (RTN_ReplaceProbed(rtnReleaseSemaphore, AFUNPTR(myReleaseSemaphore)));
            fptrReleaseSemaphore = (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::LONG, WIND::LPLONG))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenFile = RTN_FindByName(img, "OpenFile");
        if (RTN_Valid(rtnOpenFile) && RTN_IsSafeForProbedReplacement(rtnOpenFile))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenFile at " << RTN_Address(rtnOpenFile) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnOpenFile, AFUNPTR(myOpenFile)));
            fptrOpenFile = (WIND::HFILE(WINAPI*)(WIND::LPCSTR, WIND::LPOFSTRUCT, WIND::UINT))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnReadFile = RTN_FindByName(img, "ReadFile");
        if (RTN_Valid(rtnReadFile) && RTN_IsSafeForProbedReplacement(rtnReadFile))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReadFile at " << RTN_Address(rtnReadFile) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnReadFile, AFUNPTR(myReadFile)));
            fptrReadFile =
                (WIND::BOOL(WINAPI*)(WIND::HANDLE, __out_bcount_part(nNumberOfBytesToRead, *lpNumberOfBytesRead) WIND::LPVOID,
                                     WIND::DWORD, WIND::LPDWORD, WIND::LPOVERLAPPED))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnWriteFileEx = RTN_FindByName(img, "WriteFileEx");
        if (RTN_Valid(rtnWriteFileEx) && RTN_IsSafeForProbedReplacement(rtnWriteFileEx))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for WriteFileEx at " << RTN_Address(rtnWriteFileEx) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnWriteFileEx, AFUNPTR(myWriteFileEx)));
            fptrWriteFileEx = (WIND::BOOL(WINAPI*)(WIND::HANDLE, __in_bcount(nNumberOfBytesToWrite) WIND::LPCVOID, WIND::DWORD,
                                                   WIND::LPOVERLAPPED, WIND::LPOVERLAPPED_COMPLETION_ROUTINE))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcns4.dll") != std::string::npos) || (IMG_Name(img).find("RPCNS4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCNS4.dll") != std::string::npos))

    {
        RTN rtnRpcNsBindingImportBeginA = RTN_FindByName(img, "RpcNsBindingImportBeginA");
        if (RTN_Valid(rtnRpcNsBindingImportBeginA) && RTN_IsSafeForProbedReplacement(rtnRpcNsBindingImportBeginA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcNsBindingImportBeginA at " << RTN_Address(rtnRpcNsBindingImportBeginA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr                 = (RTN_ReplaceProbed(rtnRpcNsBindingImportBeginA, AFUNPTR(myRpcNsBindingImportBeginA)));
            fptrRpcNsBindingImportBeginA = (WIND::RPC_STATUS(WINAPI*)(unsigned long, WIND::RPC_CSTR, WIND::RPC_IF_HANDLE,
                                                                      WIND::UUID __RPC_FAR*, WIND::RPC_NS_HANDLE __RPC_FAR*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnPulseEvent = RTN_FindByName(img, "PulseEvent");
        if (RTN_Valid(rtnPulseEvent) && RTN_IsSafeForProbedReplacement(rtnPulseEvent))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PulseEvent at " << RTN_Address(rtnPulseEvent) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnPulseEvent, AFUNPTR(myPulseEvent)));
            fptrPulseEvent = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcCancelThread = RTN_FindByName(img, "RpcCancelThread");
        if (RTN_Valid(rtnRpcCancelThread) && RTN_IsSafeForProbedReplacement(rtnRpcCancelThread))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcCancelThread at " << RTN_Address(rtnRpcCancelThread) << std::endl;
            OutFile.flush();
            AFUNPTR fptr        = (RTN_ReplaceProbed(rtnRpcCancelThread, AFUNPTR(myRpcCancelThread)));
            fptrRpcCancelThread = (WIND::RPC_STATUS(WINAPI*)(void*))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCloseHandle = RTN_FindByName(img, "CloseHandle");
        if (RTN_Valid(rtnCloseHandle) && RTN_IsSafeForProbedReplacement(rtnCloseHandle))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CloseHandle at " << RTN_Address(rtnCloseHandle) << std::endl;
            OutFile.flush();
            AFUNPTR fptr    = (RTN_ReplaceProbed(rtnCloseHandle, AFUNPTR(myCloseHandle)));
            fptrCloseHandle = (WIND::BOOL(WINAPI*)(WIND::HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenMutexW = RTN_FindByName(img, "OpenMutexW");
        if (RTN_Valid(rtnOpenMutexW) && RTN_IsSafeForProbedReplacement(rtnOpenMutexW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenMutexW at " << RTN_Address(rtnOpenMutexW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnOpenMutexW, AFUNPTR(myOpenMutexW)));
            fptrOpenMutexW = (WIND::HANDLE(WINAPI*)(WIND::DWORD, WIND::BOOL, WIND::LPCWSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnPostMessageW = RTN_FindByName(img, "PostMessageW");
        if (RTN_Valid(rtnPostMessageW) && RTN_IsSafeForProbedReplacement(rtnPostMessageW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PostMessageW at " << RTN_Address(rtnPostMessageW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnPostMessageW, AFUNPTR(myPostMessageW)));
            fptrPostMessageW = (WIND::BOOL(WINAPI*)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM))fptr;
        }
    }

    if ((IMG_Name(img).find("rpcrt4.dll") != std::string::npos) || (IMG_Name(img).find("RPCRT4.DLL") != std::string::npos) ||
        (IMG_Name(img).find("RPCRT4.dll") != std::string::npos))

    {
        RTN rtnRpcEpResolveBinding = RTN_FindByName(img, "RpcEpResolveBinding");
        if (RTN_Valid(rtnRpcEpResolveBinding) && RTN_IsSafeForProbedReplacement(rtnRpcEpResolveBinding))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for RpcEpResolveBinding at " << RTN_Address(rtnRpcEpResolveBinding) << std::endl;
            OutFile.flush();
            AFUNPTR fptr            = (RTN_ReplaceProbed(rtnRpcEpResolveBinding, AFUNPTR(myRpcEpResolveBinding)));
            fptrRpcEpResolveBinding = (WIND::RPC_STATUS(WINAPI*)(WIND::RPC_BINDING_HANDLE, WIND::RPC_IF_HANDLE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnBindIoCompletionCallback = RTN_FindByName(img, "BindIoCompletionCallback");
        if (RTN_Valid(rtnBindIoCompletionCallback) && RTN_IsSafeForProbedReplacement(rtnBindIoCompletionCallback))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for BindIoCompletionCallback at " << RTN_Address(rtnBindIoCompletionCallback) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnBindIoCompletionCallback, AFUNPTR(myBindIoCompletionCallback)));
            fptrBindIoCompletionCallback =
                (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::LPOVERLAPPED_COMPLETION_ROUTINE, WIND::ULONG))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnPeekMessageW = RTN_FindByName(img, "PeekMessageW");
        if (RTN_Valid(rtnPeekMessageW) && RTN_IsSafeForProbedReplacement(rtnPeekMessageW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PeekMessageW at " << RTN_Address(rtnPeekMessageW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnPeekMessageW, AFUNPTR(myPeekMessageW)));
            fptrPeekMessageW = (WIND::BOOL(WINAPI*)(WIND::LPMSG, WIND::HWND, WIND::UINT, WIND::UINT, WIND::UINT))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnSendMessageTimeoutA = RTN_FindByName(img, "SendMessageTimeoutA");
        if (RTN_Valid(rtnSendMessageTimeoutA) && RTN_IsSafeForProbedReplacement(rtnSendMessageTimeoutA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for SendMessageTimeoutA at " << RTN_Address(rtnSendMessageTimeoutA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr            = (RTN_ReplaceProbed(rtnSendMessageTimeoutA, AFUNPTR(mySendMessageTimeoutA)));
            fptrSendMessageTimeoutA = (WIND::LRESULT(WINAPI*)(WIND::HWND, WIND::UINT, WIND::WPARAM, WIND::LPARAM, WIND::UINT,
                                                              WIND::UINT, WIND::PDWORD_PTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenEventA = RTN_FindByName(img, "OpenEventA");
        if (RTN_Valid(rtnOpenEventA) && RTN_IsSafeForProbedReplacement(rtnOpenEventA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenEventA at " << RTN_Address(rtnOpenEventA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnOpenEventA, AFUNPTR(myOpenEventA)));
            fptrOpenEventA = (WIND::HANDLE(WINAPI*)(WIND::DWORD, WIND::BOOL, WIND::LPCSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateProcessA = RTN_FindByName(img, "CreateProcessA");
        if (RTN_Valid(rtnCreateProcessA) && RTN_IsSafeForProbedReplacement(rtnCreateProcessA))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateProcessA at " << RTN_Address(rtnCreateProcessA) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnCreateProcessA, AFUNPTR(myCreateProcessA)));
            fptrCreateProcessA = (WIND::BOOL(WINAPI*)(WIND::LPCSTR, WIND::LPSTR, WIND::LPSECURITY_ATTRIBUTES,
                                                      WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::DWORD, WIND::LPVOID,
                                                      WIND::LPCSTR, WIND::LPSTARTUPINFOA, WIND::LPPROCESS_INFORMATION))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenEventW = RTN_FindByName(img, "OpenEventW");
        if (RTN_Valid(rtnOpenEventW) && RTN_IsSafeForProbedReplacement(rtnOpenEventW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenEventW at " << RTN_Address(rtnOpenEventW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnOpenEventW, AFUNPTR(myOpenEventW)));
            fptrOpenEventW = (WIND::HANDLE(WINAPI*)(WIND::DWORD, WIND::BOOL, WIND::LPCWSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnPostQueuedCompletionStatus = RTN_FindByName(img, "PostQueuedCompletionStatus");
        if (RTN_Valid(rtnPostQueuedCompletionStatus) && RTN_IsSafeForProbedReplacement(rtnPostQueuedCompletionStatus))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for PostQueuedCompletionStatus at " << RTN_Address(rtnPostQueuedCompletionStatus) << std::endl;
            OutFile.flush();
            AFUNPTR fptr = (RTN_ReplaceProbed(rtnPostQueuedCompletionStatus, AFUNPTR(myPostQueuedCompletionStatus)));
            fptrPostQueuedCompletionStatus =
                (WIND::BOOL(WINAPI*)(WIND::HANDLE, WIND::DWORD, WIND::ULONG_PTR, WIND::LPOVERLAPPED))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnCreateProcessW = RTN_FindByName(img, "CreateProcessW");
        if (RTN_Valid(rtnCreateProcessW) && RTN_IsSafeForProbedReplacement(rtnCreateProcessW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for CreateProcessW at " << RTN_Address(rtnCreateProcessW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnCreateProcessW, AFUNPTR(myCreateProcessW)));
            fptrCreateProcessW = (WIND::BOOL(WINAPI*)(WIND::LPCWSTR, WIND::LPWSTR, WIND::LPSECURITY_ATTRIBUTES,
                                                      WIND::LPSECURITY_ATTRIBUTES, WIND::BOOL, WIND::DWORD, WIND::LPVOID,
                                                      WIND::LPCWSTR, WIND::LPSTARTUPINFOW, WIND::LPPROCESS_INFORMATION))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnTransactNamedPipe = RTN_FindByName(img, "TransactNamedPipe");
        if (RTN_Valid(rtnTransactNamedPipe) && RTN_IsSafeForProbedReplacement(rtnTransactNamedPipe))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for TransactNamedPipe at " << RTN_Address(rtnTransactNamedPipe) << std::endl;
            OutFile.flush();
            AFUNPTR fptr          = (RTN_ReplaceProbed(rtnTransactNamedPipe, AFUNPTR(myTransactNamedPipe)));
            fptrTransactNamedPipe = (WIND::BOOL(WINAPI*)(WIND::HANDLE, __in_bcount_opt(nInBufferSize) WIND::LPVOID, WIND::DWORD,
                                                         __out_bcount_part_opt(nOutBufferSize, *lpBytesRead) WIND::LPVOID,
                                                         WIND::DWORD, WIND::LPDWORD, WIND::LPOVERLAPPED))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnReadFileEx = RTN_FindByName(img, "ReadFileEx");
        if (RTN_Valid(rtnReadFileEx) && RTN_IsSafeForProbedReplacement(rtnReadFileEx))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReadFileEx at " << RTN_Address(rtnReadFileEx) << std::endl;
            OutFile.flush();
            AFUNPTR fptr   = (RTN_ReplaceProbed(rtnReadFileEx, AFUNPTR(myReadFileEx)));
            fptrReadFileEx = (WIND::BOOL(WINAPI*)(WIND::HANDLE, __out_bcount(nNumberOfBytesToRead) WIND::LPVOID, WIND::DWORD,
                                                  WIND::LPOVERLAPPED, WIND::LPOVERLAPPED_COMPLETION_ROUTINE))fptr;
        }
    }

    if ((IMG_Name(img).find("kernel32.dll") != std::string::npos) || (IMG_Name(img).find("KERNEL32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("KERNEL32.dll") != std::string::npos))

    {
        RTN rtnOpenSemaphoreW = RTN_FindByName(img, "OpenSemaphoreW");
        if (RTN_Valid(rtnOpenSemaphoreW) && RTN_IsSafeForProbedReplacement(rtnOpenSemaphoreW))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for OpenSemaphoreW at " << RTN_Address(rtnOpenSemaphoreW) << std::endl;
            OutFile.flush();
            AFUNPTR fptr       = (RTN_ReplaceProbed(rtnOpenSemaphoreW, AFUNPTR(myOpenSemaphoreW)));
            fptrOpenSemaphoreW = (WIND::HANDLE(WINAPI*)(WIND::DWORD, WIND::BOOL, WIND::LPCWSTR))fptr;
        }
    }

    if ((IMG_Name(img).find("user32.dll") != std::string::npos) || (IMG_Name(img).find("USER32.DLL") != std::string::npos) ||
        (IMG_Name(img).find("USER32.dll") != std::string::npos))

    {
        RTN rtnReplyMessage = RTN_FindByName(img, "ReplyMessage");
        if (RTN_Valid(rtnReplyMessage) && RTN_IsSafeForProbedReplacement(rtnReplyMessage))
        {
            OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                    << "Inserting probe for ReplyMessage at " << RTN_Address(rtnReplyMessage) << std::endl;
            OutFile.flush();
            AFUNPTR fptr     = (RTN_ReplaceProbed(rtnReplyMessage, AFUNPTR(myReplyMessage)));
            fptrReplyMessage = (WIND::BOOL(WINAPI*)(WIND::LRESULT))fptr;
        }
    }

    {
        const std::string sFuncName("DoLoop");

        for (SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym))
        {
            std::string undFuncName = PIN_UndecorateSymbolName(SYM_Name(sym), UNDECORATION_NAME_ONLY);
            if (undFuncName == sFuncName)
            {
                RTN rtnDoLoop = RTN_FindByAddress(IMG_LowAddress(img) + SYM_Value(sym));
                if (RTN_Valid(rtnDoLoop) && RTN_IsSafeForProbedReplacement(rtnDoLoop))
                {
                    OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
                            << "Inserting probe for DoLoop at " << RTN_Address(rtnDoLoop) << std::endl;
                    OutFile.flush();
                    //eventhough this is not an error - print to std::cerr (in order to see it on the screen)
                    std::cerr << "Inserting a probe in DoLoop() in " << IMG_Name(img) << std::endl;
                    RTN_ReplaceProbed(rtnDoLoop, AFUNPTR(rep_DoLoop));
                }
            }
        }
    }

    WIND::TlsSetValue(dwIndexTls, (WIND::LPVOID)0);
}

VOID AttachMain(VOID* v);

VOID DetachComplete(VOID* v)
{
    std::cerr << "detach completed---";

    PIN_AttachProbed(AttachMain, 0);
}

VOID AppStart(VOID* v)
{
    std::cerr << "application started, # attach cycles = " << attachCycles << "---";
    if (attachCycles < KnobStressDetachReAttach.Value())
    {
        attachCycles++;
        PIN_DetachProbed();
    }
}

VOID AttachMain(VOID* v)
{
    globalCounter = 0;
    doLoopPred    = 1;

    IMG_AddInstrumentFunction(ImgLoad, 0);

    PIN_AddApplicationStartFunction(AppStart, 0);

    PIN_AddDetachFunctionProbed(DetachComplete, 0);

    std::cerr << "attach completed---";
}

int main(int argc, char* argv[])

{
    PIN_InitSymbols();

    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    OutFile.open(KnobOutputFile.Value().c_str());
    OutFile << std::hex;
    OutFile.setf(std::ios::showbase);
    OutFile << CurrentTime() << " " << WIND::GetCurrentThreadId() << " "
            << "started!" << std::endl;
    OutFile.flush();

    //allocate space on tls for our use
    if ((dwIndexTls = WIND::TlsAlloc()) == WIND::tls_out_of_indexes)
    {
        OutFile << "TlsAlloc failed" << std::endl;
        return 1;
    }

    IMG_AddInstrumentFunction(ImgLoad, 0);

    PIN_AddApplicationStartFunction(AppStart, 0);

    PIN_AddDetachFunctionProbed(DetachComplete, 0);

    PIN_StartProgramProbed();

    return 0;
}
