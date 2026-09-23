/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Opens a Pin-owned file descriptor before application execution. The
 * application overwrites its native descriptor with dup2() or dup3(), then
 * this tool writes through the original generic descriptor at process exit.
 * The tool replaces the application's target-descriptor helper with the
 * native descriptor corresponding to its generic descriptor.
 */

#include "pin.H"

#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>

KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "file written through the generic descriptor");

namespace
{
int g_pinFd = -1;

t_syscall_ret OS_GetNativeFileHandle(t_syscall_arg generic_file_handle)
{
    t_syscall_arg nativeIdArgs[] = {generic_file_handle, static_cast< t_syscall_arg >(Q_FILE_ID)};

    return OS_Syscall(SYS_native_id, SYSCALL_ARG_COUNT(nativeIdArgs), nativeIdArgs);
}

INT32 ReplacementGetPinNativeFileDescriptor()
{
    auto nativeFd = OS_GetNativeFileHandle(g_pinFd);
    ASSERTX(!IS_PINOS_SYSCALL_ERROR(nativeFd));
    return static_cast< INT32 >(nativeFd);
}

VOID WriteThroughPinFileDescriptor(INT32, VOID*)
{
    static constexpr char kMessage[] = "Pin generic file descriptor survived application duplication\n";
    ssize_t bytesWritten             = write(g_pinFd, kMessage, sizeof(kMessage) - 1);
    if (static_cast< ssize_t >(sizeof(kMessage) - 1) != bytesWritten)
    {
        std::perror("write");
        PIN_ExitProcess(1);
    }
}

VOID ReplaceGetPinNativeFileDescriptor(IMG image, VOID*)
{
    if (!IMG_IsMainExecutable(image))
    {
        return;
    }

    RTN routine = RTN_FindByName(image, "GetPinNativeFileDescriptor");
    ASSERTX(RTN_Valid(routine));

    if (PIN_IsProbeMode())
    {
        BOOL isSafeForReplacement = RTN_IsSafeForProbedReplacement(routine);
        ASSERTX(isSafeForReplacement);

        AFUNPTR originalFunction = RTN_ReplaceProbed(routine, AFUNPTR(ReplacementGetPinNativeFileDescriptor));
        ASSERTX(nullptr != originalFunction);
    }
    else
    {
        RTN_Replace(routine, AFUNPTR(ReplacementGetPinNativeFileDescriptor));
    }
}
} // namespace

int main(int argc, char* argv[])
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv))
    {
        return 1;
    }

    g_pinFd = open(KnobOutputFile.Value().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (-1 == g_pinFd)
    {
        std::perror("open");
        return 1;
    }

    IMG_AddInstrumentFunction(ReplaceGetPinNativeFileDescriptor, 0);
    PIN_AddFiniFunction(WriteThroughPinFileDescriptor, 0);

    PIN_StartProgram();

    return 0;
}