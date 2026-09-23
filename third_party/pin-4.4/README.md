# Pin

Copyright (C) 2004-2026 Intel Corporation.  
SPDX-License-Identifier: MIT

## Overview

Pin is a tool for the instrumentation of programs. It supports the Linux(R) and Windows operating
systems and executables for the IA-32 and Intel(R) 64-bit architectures.

For license information, see the `intel-simplified-software-license.txt` file and the `licensing`
directory.

For information on how to use Pin, read the manual in `doc/html/index.html`.

For questions and bug reports, please visit <https://groups.io/g/pinheads>.

For downloading Intel(R) X86 Encoder Decoder, please visit <https://github.com/intelxed>.

## System Requirements

### CPU

- Pin is supported on systems with Intel processors. Incompatible or proprietary instructions in
  non-Intel processors may cause Pin to function incorrectly. Any attempt to instrument code not
  supported by Intel processors may lead to failures.

### Linux

- Pin is supported on Linux distros with Linux kernel version >= 4.12.14.
- Building Pin Tools on Linux is supported from GCC 6.1 up to GCC 14. GCC 10 or higher is
  recommended, due to limitations in earlier GCC versions.

### Windows

- Pin is supported on (Desktop) Windows 11 23H2 or higher, and (Server) Windows Server 2022 or
  higher.
- Building Pin Tools on Windows requires LLVM toolchain versions 15 or 16.
  - Use of Microsoft CL Compiler is not supported due to LLVM libcxx restrictions.
  - It is possible to use a standalone LLVM toolchain with a stand-alone up-to-date Windows SDK. 
  - If no Windows headers are used, it is possible to use a standalone LLVM toolchain without a Windows SDK.
  - It is possible to also use the LLVM toolchain installed with Visual Studio. If Visual Studio is used,
    Pin only supports Visual Studio 2022 Version 17.6.xx. Newer versions use a newer versions of the LLVM toolchain,
    which are not supported by Pin. Older versions of Visual Studio 2022 use an older version of the LLVM toolchain,
    which are not supported by Pin.

## Installation

To install a kit, unpack a downloaded kit and change to the directory.

- For Linux kits, use `tar xzf <downloaded kit file name>` to unpack the kit.
- For Windows kits, use the zip folders feature of Windows or any unzip tool to unpack the kit.

Kit names are of the form:

- `pin-4.<minor>-<build>-g<commit>-<compiler>-<platform>.tar.gz` for Linux kits
- `pin-4.<minor>-<build>-g<commit>-<compiler>-<platform>.zip` for Windows kits

For example:

- `pin-4.0-99625-gc5b279576-gcc-linux.tar.gz`
- `pin-4.0-99625-gc5b279576-clang-windows.zip`

For better security, install in a secure location.

## Example Usage

This example applies to a 64-bit application. For a 32-bit application, use `obj-ia32` instead of
`obj-intel64` and add `TARGET=ia32` to the `make` command.

To build and run a sample tool on Linux:

```bash
cd source/tools/SimpleExamples
make obj-intel64/opcodemix.so
../../../pin -t obj-intel64/opcodemix.so -- /bin/ls
```

This will instrument and run `/bin/ls`. The output for this tool is in `opcodemix.out`.

To build and run a sample tool on Windows, open a command line prompt (depending on the application
type) and run:

```text
cd source\tools\SimpleExamples
make obj-intel64/opcodemix.dll
..\..\..\pin.exe -t obj-intel64\opcodemix.dll -- cmd /C dir
```

This will instrument and run `cmd /C dir`. The output for this tool is in `opcodemix.out`.

Refer to the Examples section in the Pin User Guide for more usage examples.

## Restrictions

- Tools are restricted from linking with any system libraries and/or calling any system calls. See
  the paragraph on PinCRT in the "Additional information for PinTool writers" section for more
  information.
- Pin on Windows requires `msdia140.dll`. This DLL is distributed with the kit.
- There is a known problem of using pin on systems protected by the "McAfee(R) Host Intrusion
  Prevention" antivirus software. See the "Additional Information for Using Pin on Windows"
  section for more information.
- There is a known problem of attaching pin to a running process on Linux systems that prevent the
  use of `ptrace` attach using the `sysctl /proc/sys/kernel/yama/ptrace_scope`. See the
  "Additional information for using Pin on Linux" section for more information.
- Pin performs memory allocations in the application's address space. As a result, memory
  allocations performed by the application can fail. For example, some applications use the
  SmartHeap utility which could perform huge memory allocations.
- There are known problems using Pin with the Google Chrome(TM) browser.
  - On Windows, Pin may fail to attach to a running Chrome process.
  - On Linux, Pin may crash when instrumenting Chrome.
  - A possible workaround is to launch Chrome with the `--no-sandbox` command line switch.
- Pin on Linux can read debug information in formats up to DWARF 5, inclusive.
  - DWARF 6 may work but was not tested.
  - Compressed debug information is not supported.
- Building Pin Tools on Linux only requires linking with both `libdwarf.so` and `libpindwarf.so`,
  provided with Pin Kit.
- There are known issues when instrumenting CET-enabled applications with Pin.
  - Pin disables CET for instrumented process in Launch mode.
  - Instrumenting CET-enabled applications in Attach mode might end in a random crash.
- On Windows, the total combined size of the command line and environment variables block must be
  less than ~60KB for `-follow-execve` to work correctly. If `-follow-execve` is not used, then
  Pin will not explicitly limit the size of these blocks, but they may be limited by the OS.
- On Linux, the size of the command line must be less than ~60KB for `-follow-execve` to work
  correctly. If `-follow-execve` is not used, then Pin will not explicitly limit the size of the
  command line, but it may be limited by the OS.
- Pin's `FPSTATE` structure does not include AMX XTILE_DATA (tile data registers `REG_TMM0`
  through `REG_TMM7`). To read or write individual tile data registers, use
  `PIN_GetContextRegval`/`PIN_SetContextRegval` with the corresponding `REG_TMM*` register.
- On Linux, AMX XTILE_DATA is currently not included in the signal context delivered to
  application signal handlers. This has two implications for threads that have ever executed an
  AMX instruction:
  - Application signal handlers that read or modify tile data registers via the `ucontext_t` FP
    state area will not see the correct tile data values.
  - Calling `PIN_Detach()` from within an application signal handler may result in loss of
    extended register state (YMM, ZMM, opmask, tile configuration).
  This is tracked in PINT-6831 and will be addressed in a future release.
- On Windows, AMX XTILE_CFG and XTILE_DATA are not currently included in the exception
  context delivered to application exception handlers (SEH/VEH).

## Additional Information for PinTool Writers

- Pin is built and distributed with its own OS-agnostic, compiler-agnostic runtime, named Pin RT.
  Pin RT exposes three layers of generic APIs which practically eliminate Pin's and the tools'
  dependency on the host system:
  1. A generic operating system interface, providing POSIX style system call interfaces.
  2. A Musl-based C runtime layer supplying a standard C implementation.
  3. A modern up-to-date C++ runtime based on LLVM's libcxx, supporting C++17 on both Linux and
     Windows. Please note that the current version does not support C++ RTTI, and C++ exceptions
     are supported only on Linux.

  Tools are obliged to use, and link with, Pin RT instead of any system runtime. Tools must
  refrain from using any native system calls, and use Pin RT APIs for any needed functionality.
  This limitation can be alleviated by employing Pin's Remote Procedure Calls (RPCs). For further
  details, consult the "Executing Remote Procedures" section in the Pin User Guide.
- Pin RT does not support the Boost C++ libraries due to lack of C++ RTTI.
- Due to a compatibility issue between operating systems, pin does *not* provide support for
  registering `atexit` functions inside pintools, which means that the behavior of a pintool that
  does so is undefined. If you need such functionality, register a `Fini` function instead. In
  probe mode, the `Fini` function may or may not be called.
- The default `malloc` implementation used by Pin is not async-signal-safe. Correctly written
  tools should avoid allocating or freeing memory using `malloc`, `free`, and similar APIs in
  signal handlers (Linux; see `man 7 signal-safety`) or exception handlers (Windows). However,
  some legacy tools may rely on Pin 3.x `malloc` behavior, which was async-signal-safe. Even Pin's
  internal signal handling is not async safe under some use-cases.
  - If you encounter deadlocks or memory corruption related to memory allocation or freeing inside
    signal or exception handlers, it is possible to add the following knobs to Pin's invocation:
    `-xyzzy -async-safe-malloc`. Passing this knob will use the old Pin 3.x implementation.

## Additional Information for Using Pin on Windows

### General Issues

- Pin provides transparent support for exceptions in the application, but prohibits using
  exceptions in the tool under Windows. If you need to assert some condition, use the `ASSERT()`
  macro defined by pin instead of the standard `assert()`.
- The Image API does not work for GCC-compiled applications.
- There is a known problem of using pin on systems protected by the "McAfee(R) Host Intrusion
  Prevention" antivirus software. We did not test coexistence of pin with other antivirus products
  that perform run-time execution monitoring.
- Pin may not instrument applications that restrict loading of DLLs from non-local drives if Pin
  and or pintool binaries are located on a network drive. To work around this problem, install all
  Pin and Pin tool binaries on a local drive.
- Multi-byte characters support: using the UTF-8 code page, for process arguments and environment
  of Pin binaries is available starting from Windows build 18362, which corresponds to Windows 10
  Version 1903. This covers Windows 10, Windows 11, Windows Server 2022 and Windows Server 2025.

### PinADX Support

- Pin Advanced Debugging Extensions (PinADX) supports Visual Studio 2022.

## Additional Information for Using Pin on Linux

### General Issues

- There is a known problem of attaching Pin to a running process on Linux systems that prevent the
  use of `ptrace` attach using the `sysctl /proc/sys/kernel/yama/ptrace_scope`. There is no
  problem in launching an application with Pin with this limitation. To resolve this, set the
  `kernel/yama/ptrace_scope` `sysctl` to `0`.

---

# Recent Changes

## Changes Added in Pin 4.4

- New features in this version:
  - Support for Intel(R) Advanced Performance Extensions (Intel(R) APX), Linux and Intel(R) 64
    only:
    - APX support is currently limited to Linux. Windows is **not** supported in this version.
    - Pin can now instrument applications that contain APX-encoded instructions, including the
      new REX2 and extended EVEX encodings, the NDD (new data destination), NF (no flags) and
      ZU (zero upper) modifiers, and the new instructions `PUSH2`/`POP2`, `PUSHP`/`POPP`,
      `JMPABS`, `CCMP`/`CTEST` and `CFCMOVcc`.
    - APX decoding is now enabled automatically when the processor supports APX, and is
      disabled when it does not. This supersedes the Pin 4.1 note about enabling APX decoding
      through a callback registered with `PIN_AddXedDecodeCallbackFunction` - no such callback
      is needed anymore. On a processor without APX, APX-encoded instructions are still
      rejected by the decoder, since they cannot be executed on such a processor anyway.
    - The 16 extended general purpose registers (EGPRs) r16-r31 were added to the `REG` enum as
      `REG_R16` through `REG_R31`, together with their sub-registers `REG_R16B`/`REG_R16W`/
      `REG_R16D` through `REG_R31B`/`REG_R31W`/`REG_R31D`, and the range delimiters
      `REG_EGPR_BASE` and `REG_EGPR_LAST`. The new `REG_is_egpr()` query returns TRUE for a
      full-width EGPR.
    - EGPRs are part of the general purpose register class, so `REG_GR_LAST` is now `REG_R31`.
      Tools that iterate `REG_GR_BASE` to `REG_GR_LAST` will now also visit r16-r31. A new
      delimiter, `REG_GR_LEGACY_LAST`, marks the last of the original 16 general purpose
      registers, so a tool that wants only those can iterate `REG_GR_BASE` to
      `REG_GR_LEGACY_LAST`. See the breaking changes below.
    - `FPSTATE` now includes the APX EGPR state (XSAVE state component 19, 128 bytes at offset
      960), so EGPR values can be read and written in bulk with `PIN_GetContextFPState` and
      `PIN_SetContextFPState`. The size of `FPSTATE` is unchanged. See the breaking changes
      below.
    - `PROCESSOR_STATE_APX` was added to the `PROCESSOR_STATE` enum, for use with
      `PIN_SupportsProcessorState()` and `PIN_ContextContainsState()`.
    - EGPRs are supported by the existing register, context and operand APIs, with no new API
      required. This includes `PIN_GetContextReg`/`PIN_SetContextReg`,
      `PIN_GetContextRegval`/`PIN_SetContextRegval`, `IARG_CONTEXT`, `IARG_CONST_CONTEXT`,
      `IARG_PARTIAL_CONTEXT`, `IARG_REG_VALUE`, `IARG_REG_REFERENCE`,
      `IARG_REG_CONST_REFERENCE`, `INS_RegR`/`INS_RegW`, `INS_OperandReg`, and
      `INS_MemoryBaseReg`/`INS_MemoryIndexReg`.
    - `INS_DirectControlFlowTargetAddress()` now returns the correct 64-bit absolute target for
      `JMPABS`.
    - `INS_RewriteMemoryOperand()` is supported for the stack operand of `PUSH2` and `POP2`.
    - `INS_IsPredicated()` returns FALSE for `CFCMOVcc`, `CCMP` and `CTEST`, and the predicated
      call APIs therefore never skip an analysis call for these instructions. Unlike `CMOVcc`,
      these instructions always write their destination - a `CFCMOVcc` register destination is
      written on both paths, and `CCMP`/`CTEST` always write flags - so there is no
      "not executed" case to report. Only the memory access of `CFCMOVcc` is conditional. Tools
      that need the condition must inspect the instruction and the flags themselves.
    - Instructions that use the NF modifier correctly report that they do not write any flag,
      so tools tracking flag liveness see the flags as preserved across them.
    - Known limitations of the APX support in this version:
      - In probe mode, the `CONTEXT` passed to a callback currently does not contain the EGPR values.
        Individual EGPRs are delivered correctly through `IARG_REG_VALUE`. This matches the
        existing probe mode limitation for the XMM, YMM and ZMM registers.
  - Reworked memory manager, giving finer control over where Pin and the Pintool place their
    memory and how Pin reacts when the application competes for the same addresses. A new "Memory
    management" chapter was added to the Pin User Guide, describing every memory-management knob,
    its platform requirements, and the rules Pin applies when validating combinations of these
    knobs at startup:
    - New `-assert_on_memory_conflict mode` knob controls what Pin does when an application
      memory-management request conflicts with an existing Pin or Pintool mapping. `FULL` (the
      default) asserts for any conflicting request, `FIXED_ONLY` asserts only for requests that
      must operate on a specific address, and `OFF` disables the assertion and returns the failure
      to the application (`OFF` is equivalent to the previous behavior). Note that such assertions
      are expected, especially for multithreaded applications. This may inconvenience at first, but
      is intended to let users know that Pin is affecting the application's behavior in possibly
      unexpected ways. The provided memory knobs allow avoiding such conflicts, and it is always
      possible to disable the assertion if such behavior modification is acceptable for workload
      under test.
    - `-pin_memory_range base:end` is now supported on Windows in addition to Linux, and now also
      governs code-cache memory and Pin-owned image loading. By default this is a placement hint,
      so if memory cannot be allocated from the specified ranges, Pin silently falls back to regular
      allocations from the system. 
    - New `-enforce_pin_range_allocations 1` knob turns `-pin_memory_range` from a placement hint
      into a placement requirement. In this mode an allocation that cannot be satisfied from the
      configured ranges fails with an out-of-memory condition (see `PIN_AddOutOfMemoryFunction()`)
      instead of silently falling back to another address, giving strict separation between the
      application address space and Pin's address space.
    - New `-restrict_memory base:end` knob (Linux and Windows, repeatable) prevents Pin and the
      Pintool from ever allocating inside the specified ranges, leaving them available to
      applications that request allocation using specific addresses. This knob is not supported
      in probe mode and cannot be used together with `-reserve_memory`. If `-pin_memory_range` is
      also specified, the ranges must not overlap.
    - `-reserve_memory file` is now supported on Windows in addition to Linux, and
      `PIN_WasMemoryReservedInLoadTime()` is available on both operating systems.
  - Changed code-cache size defaults and limits, and the corresponding minimum for
    `-pin_memory_size`:
    - On Intel(R) 64, the default JIT-mode code cache was increased from 256 MB to 1 GB. The
      probe-mode default (16 MB) and the maximum accepted value (2 GB) are unchanged.
    - On IA-32, the defaults are unchanged (128 MB in JIT mode, 8 MB in probe mode), but the
      maximum accepted value is now 128 MB. A larger value is clamped to it, with a warning.
    - `-pin_memory_size` must be large enough to hold the code cache, so its minimum accepted
      value changed accordingly. With the default code cache it is 1218 MB on Intel(R) 64 in JIT
      mode and 210 MB in probe mode, and 256 MB on IA-32 in JIT mode and 136 MB in probe mode.
      When `-cc_memory_size CC_SIZE` is given explicitly, the minimum is 194 MB + `CC_SIZE` on
      Intel(R) 64 and 128 MB + `CC_SIZE` on IA-32.
    - The same minimum is applied to the combined size of the `-pin_memory_range` ranges when
      `-enforce_pin_range_allocations` is used. Note that in this mode both constraints apply, so
      the memory effectively available to Pin and the Pintool is the smaller of `-pin_memory_size`
      and the accumulated size of the `-pin_memory_range` ranges. Tools that limit Pin's memory
      should review these values, since a `-pin_memory_size` or a set of ranges that was accepted
      by Pin 4.3 may now be rejected at startup on Intel(R) 64.
  - Improved file-descriptor isolation on Linux, so that an application can no longer accidentally
    close or overwrite descriptors owned by Pin or the Pintool. Pin now emulates `close_range`,
    `dup2` and `dup3` in JIT mode, and intercepts `close_range`, `dup2`, `dup3` and the libc
    `syscall()` function in probe mode.
  - Updated Intel(R) X86 Encoder Decoder (XED) to version `v2026.08.23`.
  - Updated `libdwarf` to version 2.3.2.
  - Reduced Pin's internal locking and thread bookkeeping overhead, improving performance of
    multi-threaded applications.
  - Pintools can now be built on Windows with the Windows SDK shipped with Visual Studio 2026.
    Building Pintools is still limited to the LLVM toolchain versions listed in the System
    Requirements.
  - Clarified the documentation of the `IARG_MEMORY*_EA` and `IARG_MEMORY*_PTR` arguments, of the
    analysis-routine call order required by `INS_RewriteMemoryOperand()`, and of the fact that
    `PIN_SetContextReg` accepts only full integer registers (use `PIN_SetContextRegval` for
    partial registers).
- Notable bugs fixed in this version:
  - Fix an illegal instruction exception when `IARG_PARTIAL_CONTEXT` names registers that the
    processor does not implement. Previously, passing a register set built with
    `REGSET_AddAll()` made Pin generate spill code for registers such as the EGPRs, the tile
    registers or the AVX-512 registers on a machine lacking them, which faulted when executed.
    Pin now filters the requested set by the registers the current processor actually supports,
    so tools no longer need to remove unsupported registers by hand.
  - Fix a memory leak in the generation of analysis calls that take a `CONTEXT`. The register
    set computed for the spill area context was allocated on every analysis call generation and
    never freed, so each regeneration of a trace leaked one register set. The sets are now
    owned and shared by Pin.
  - [Linux] Fix `SYS_prepare_follow_execve failed with error: 2 (No such file or directory)` when
    an application uses `close_range()` to close descriptors owned by Pin.
  - [Linux] Fix a case where an internal Pin signal could be delivered to an unrelated process
    after a thread id was recycled.
  - Fix a failure to look up a file descriptor that was created with `dup2()` or `dup3()`.
  - [Windows] Fix an injection failure reported by the launcher as exit code `0x7f` when Pin's server
    process inherits the same native handle for both standard output and standard error.
  - [Windows][IA-32] Fix corruption of the `ebx` register during structured exception handling
    (`__try`/`__except`) unwinding.
  - Fix an instruction encoding failure caused by folding an immediate that is too large for the
    rewritten instruction.
  - Fix a startup failure caused by an insufficient initial virtual memory area count in some
    replay scenarios using `-reserve_memory`.
  - Fix an assert in Pin's logger when heavy logging is requested on a slow file system, and make
    sure that pending log messages are flushed when the process exits.
  - Fix rare thread handle allocation failures and a possible memory corruption in applications
    that create more than 64K threads over their lifetime.
- Breaking changes in this version:
  - The APX EGPR state and the deprecated MPX `BND` state occupy the same offset in the XSAVE
    area and are mutually exclusive, so they are now declared as a union in `FPSTATE`. Tools
    that access the `BND` fields directly must use `_vstate._mpx._bndRegs` and
    `_vstate._mpx._bndCSR` instead of `_vstate._bndRegs` and `_vstate._bndCSR`. The layout,
    field offsets and the size of `FPSTATE` are unchanged.
  - Tools that iterate the general purpose registers from `REG_GR_BASE` to `REG_GR_LAST` now
    also see r16-r31, on every machine and on both operating systems, because the `REG` enum
    does not depend on the processor or the platform. Iterate to `REG_GR_LEGACY_LAST` instead
    to keep the previous set, and use `PIN_SupportsProcessorState(PROCESSOR_STATE_APX)` to
    determine whether the current processor actually implements the extended registers.
  - The default for `-assert_on_memory_conflict` is `FULL`, while the previous behavior was
    equivalent to `OFF`. This means that you can expect assertions on any memory-management
    conflicting request by the application, including those that were previously ignored.
    We know for a fact that this is more likely to happen for multithreaded applications. 
  - The following knobs were removed:
    - `-cache_block_size` (deprecated in Pin 4.3) - the code-cache block size is now fixed at
      256 KB.
    - `-cc_memory_size_64` - use `-cc_memory_size` on all targets.
    - `-xyzzy -cc_memory_range` (deprecated in Pin 4.3) - the code cache is now allocated through
      Pin's regular memory manager and follows `-pin_memory_range` and `-pin_memory_size`.
    - `-disable_syscall_discovery` - system call discovery is now disabled automatically in probe
      mode, so passing `-probe` is sufficient.
  - `-cc_memory_size`, which was announced as deprecated in Pin 4.3, was kept and reimplemented
    instead of being removed. It is now the single code-cache size knob on both IA-32 and Intel64,
    it must be a multiple of the fixed 256 KB block size, and the memory it requests is allocated
    through Pin's memory manager.
  - `PROTO_Free` is deprecated and will be removed in a future version of Pin. The function has no
    effect - a `PROTO` is owned by Pin and remains valid for the lifetime of the instrumentation
    that references it - so calls to it can simply be removed.
  - `-reserve_memory` can no longer be used in attach mode on either Linux or Windows. It also
    remains unsupported in probe mode and with `-follow_execv`, and it is ignored when
    `-pin_memory_range` is specified.
  - Memory-management knobs are no longer passed to child processes with `-follow_execv`, so child
    processes use their default values. A Pintool that needs them in a child process must add them
    from a `PIN_AddFollowChildProcessFunction()` callback, using `CHILD_PROCESS_GetPinCommandLine()`
    to retrieve the prepared command line and `CHILD_PROCESS_SetPinCommandLine()` to set the
    updated one.
  - [Intel64] The values of the `REG` enumeration changed, because the APX extended general purpose
    registers were inserted into it.

## Changes Added in Pin 4.3.1

- Notable bugs fixed in this version:
  - Fix an assert during startup on when using `-reserve_memory` knob to reserve more than 64K ranges.

## Changes Added in Pin 4.3

- New features in this version:
  - AMX tile configuration support in FPSTATE and Context API (Intel 64 only):
    - `FPSTATE` now includes the AMX tile configuration (XTILE_CFG) state component (64 bytes).
      The tile configuration is now also accessible via `PIN_GetContextFPState`/`PIN_SetContextFPState`
      in addition to `PIN_GetContextRegval`/`PIN_SetContextRegval` with `REG_TILECONFIG`.
    - Note: XTILE_DATA (the 8 KB tile data registers `REG_TMM0`-`REG_TMM7`) is **not** included
      in `FPSTATE`. Individual tile data registers can be accessed via `PIN_GetContextRegval` and
      `PIN_SetContextRegval` with `REG_TMM0` through `REG_TMM7`.
  - Known limitation: XTILE_DATA is currently not included in the signal context delivered to
    application signal handlers on threads that have used AMX instructions. This also affects
    `PIN_Detach()` from within a signal handler. See the Restrictions section for details.
  - Update Musl to version 1.2.6 with patches for CVE-2026-40200 and CVE-2026-6042.
  - PIN_SpawnApplicationThread now returns the native OS thread ID of the spawned thread, or `INVALID_NATIVE_TID` on
    failure. This allows tools to track and manage threads created via `PIN_SpawnApplicationThread` more effectively.
    See the documentation of `PIN_SpawnApplicationThread` for more information.
  - Add new `-daemonize` knob. This knob redirects the Pin server process (pind) standard input/output/error to
    /dev/null on Linux and NUL on Windows This releases any inherited file descriptors from the parent process,
    which is useful when the caller launches Pin in attach mode via popen or similar mechanisms and waits for EOF
    on stdout/stderr pipes. Without this knob, pind keeps those pipe descriptors open as long as the application is alive,
    preventing the caller from detecting that Pin has finished launching. Note that passing this knob will also redirect
    all output to standard output/error from the Pintool to /dev/null or NUL.
    - On Windows, the Pin launcher in attach mode does not exit until the application being attached to terminates. So although
      passing `-daemonize` will redirect pind's standard output/error to NUL, the caller will still wait for the application to
      terminate before the Pin's launcher returns. This behavior may change in future versions of Pin.
    - When attaching to a process running in docker container on Linux, in non interactive mode, it is recommended to use `-daemonize`
      to avoid a crash after the launcher exits and docker closes the standard input/output/error pipes.
  - Make default Windows launcher (`pin.exe`) 64-bit to support launching on machines without Wow64 support. The previous 32-bit
    launcher is still available as `pin32.exe`.
  - Add `CHILD_PROCESS_GetPinCommandLine` API to retrieve the Pin commandline from the `FOLLOW_CHILD_PROCESS_CALLBACK` callback.
  - The following knobs are deprecated and will be removed in the next version:
    - `-cache_block_size`
    - `-cc_memory_size`
    - `-xyzzy -cc_memory_range`
- Notable bugs fixed in this version:
  - [Windows] Fix a crash while processing debug symbols for some binaries with post link modifications.
  - [Windows] Fix various PinADX issues.
  - [Windows] Fix PinADX plugin installation for Visual Studio 2022.
  - [Windows] Fix a failure to launch when the top level directory of the Pin kit is located is a single letter.
  - [Linux] Fix a tools makefile issue that prevent building tools when the environment variable `CXX` is set.
- Breaking changes in this version:
  - The following deprecated APIs were removed:
    - CALLBACK_GetExecutionPriority - use `CALLBACK_GetExecutionOrder` instead.
    - CALLBACK_SetExecutionPriority - use `CALLBACK_SetExecutionOrder` instead.
    - IMG_Entry - use `IMG_EntryAddress` instead.
    - INS_DirectBranchOrCallTargetAddress - use `INS_DirectControlFlowTargetAddress` instead.
    - INS_IsBranchOrCall - use `INS_IsControlFlow` instead.
    - INS_IsDirectBranchOrCall - use `INS_IsDirectControlFlow` instead.
    - INS_IsIndirectBranchOrCall - use `INS_IsIndirectControlFlow` instead.
    - INS_MemoryReadSize - use `INS_MemoryOperandSize` instead.
    - INS_MemoryWriteSize - use `INS_MemoryOperandSize` instead.
    - `INS_XedExactMapToPinReg` with `unsigned int` - use `INS_XedExactMapToPinReg` with `xed_reg_enum_t`
    instead.
    - `PIN_Sleep` - use `std::this_thread::sleep_for()` instead.
    - `PIN_ThreadUid` - use the pThreadUid output argument of `PIN_SpawnInternalThread` instead, which is a
       unique identifier of the thread that can be used in `PIN_WaitForThreadTermination`. Note that the
       value is not guaranteed to be unique after the thread terminates, and is waited upon using
       `PIN_WaitForThreadTermination`. See the documentation of `PIN_SpawnInternalThread` and
       `PIN_WaitForThreadTermination` for more information.
    - `PIN_Yield` - use `std::this_thread::yield()` instead.
  - If writing a custom build tool target using the Pin build tool makefiles, use `TOOL_CXX`, `TOOL_LINKER`,
    and `TOOL_ARCHIVER` variables instead of `CXX`, `LINKER`, and `ARCHIVER` to ensure
    the correct compiler and linker are used. See the "Defining Build Rules for Tools and Applications"
    section in the Pin User Guide for more information.
  - PIN_SpawnApplicationThread now returns the native OS thread ID of the spawned thread, or `INVALID_NATIVE_TID` on
    failure, instead of `BOOL`. Tools may or may not have to change the way they handle `PIN_SpawnApplicationThread`
    return values. This may not be necessary because `INVALID_NATIVE_TID` is equivalent to `FALSE`, and a valid value
    will evaluate to `true` when checked directly in boolean context. Regardless, it is recommended to check for
    `INVALID_NATIVE_TID` explicitly for better readability and to avoid confusion with the previous boolean return
    type. See the documentation of `PIN_SpawnApplicationThread` for more information.

## Changes Added in Pin 4.2.1

- Notable bugs fixed in this version:
  - Patch CVE-2026-40200 & CVE-2026-6042 in Musl
  - Fix a rare deadlock during application startup on Windows
  - Fix signing of PinADX Visual Studio extension

## Changes Added in Pin 4.2

- This version introduces the following enhancements:
  - LIP (Low Integrity Processes) support on Windows:
    - Pin can now instrument low integrity processes on Windows. This includes both launching and
      attaching to low integrity processes.
  - Improved and unified debug symbol processing for Windows and Linux:
    - Added `IMG_AddPreDebugInfoProcessCallback` API to allow tools to control debug symbol
      processing on a per-image basis. Returning `FALSE` from the callback skips debug symbol
      processing for that image.
    - Linux now supports controlling symbol modes via `PIN_InitSymbolsAlt`, similar to Windows.
      See `PIN_InitSymbolsAlt` documentation.
    - See breaking changes below.
  - API enhancement: `PIN_GetSyscallNumber()` now available in syscall exit callbacks:
    - The `PIN_GetSyscallNumber()` API function can now be used in both syscall entry and syscall
      exit callbacks. Previously, this function was only available for use in syscall entry
      callbacks.
    - This enhancement enables tools to retrieve the syscall number when handling syscall exit
      events, simplifying syscall tracking and analysis workflows.
  - Improved performance of `fork` and `execve` on Linux in probe mode:
    - To take full advantage of this improvement, Pin should be called with the `-probe` knob.
  - Improved performance of attach flows on Linux.
  - Lift command line and environment block size limitations on both Windows and Linux:
    - Pin no longer imposes limitations on the size of command line arguments and environment
      variable blocks.
    - On Windows, the total combined size of the command line and environment variables block must
      be less than ~60KB for `-follow-execve` to work correctly. If `-follow-execve` is not used,
      then Pin will not explicitly limit the size of these blocks, but they may be limited by the
      OS.
    - On Linux, the size of the command line must be less than ~60KB for `-follow-execve` to work
      correctly. If `-follow-execve` is not used, then Pin will not explicitly limit the size of
      the command line, but it may be limited by the OS.
- Notable bugs fixed in this version:
  - Pin incorrectly restores `r11` register after syscall.
  - Pin fails to instrument certain Windows applications due to bad `SYSCALL` mapping.
  - Several issues related to running Pin inside containers or on containerized applications on
    Linux.
  - Deadlock inside an application using `waitpid` to wait for all children (`pid == -1`).
  - Deadlock during detach and reattach flows on Linux.
  - Failure to inject when pin is called from a different drive from where Pin is installed on
    Windows.
  - Failure to inject under certain conditions on Windows in Azure DevOps environment.
  - Pin fails to instrument `regedit.exe` on Windows.
  - Pin hangs when instrumenting 3DMark with `-follow-execve` on Windows.
- Breaking changes in this version:
  - Windows: Pin now processes both debug symbols and export symbols when debug info is available.
    Previously, export symbols were only processed when debug symbols were unavailable. The
    previous behavior of `DEBUG_OR_EXPORT_SYMBOLS` is now deprecated. Export symbols are always
    processed. See below.
    - Exported symbols pointing to ILT, Incremental Link Table, thunks are now detected and
      renamed with `@ILT` suffix to avoid conflicts with debug info symbols when debug symbols are
      present. See `PIN_InitSymbolsAlt` documentation. This change should not affect correctness of
      existing Pintools.
  - Linux: Pin now creates new symbols from debug information for functions that do not have
    corresponding symbols in the symbol tables. Previously, debug info was only used to merge or
    adjust existing symbols.
  - `DEBUG_SYMBOLS` is treated as `DEBUG_AND_EXPORT_SYMBOLS` on all supported platforms. This
    means the export and symbol-table symbols are always processed when debug symbol processing is
    requested.
  - The `SYMBOL_INFO_MODE` enum retains the `DEBUG_OR_EXPORT_SYMBOLS` entry. However, it is a
    synonym for `DEBUG_AND_EXPORT_SYMBOLS`. `DEBUG_AND_EXPORT_SYMBOLS` is now the default for
    `PIN_InitSymbols()` on both Windows and Linux. New implementations should not use the
    `DEBUG_OR_EXPORT_SYMBOLS` enum, as it does not work as the name implies.
  - This version removes the `-pin_image_memory_range` knob that was non-functional starting with
    Pin 4.0. Starting with Pin 4.2, the `-pin_memory_range` knob is also used to specify where Pin
    is loaded as well as the memory ranges allowed for Pin's internal use. Note that this knob is
    currently supported only on Linux and is not supported on Windows.
  - The behavior of the `-pin_memory_range` knob is changed compared with Pin 3.x. Starting with
    this version, Pin will attempt to use memory ranges specified by the `-pin_memory_range` knob
    for loading Pin and for its internal memory allocations. However, if these ranges are not
    available, Pin will still use other memory ranges for its operation instead of failing. This
    change allows Pin to operate correctly in more environments, but it also means that the
    `-pin_memory_range` knob is now a hint rather than a strict requirement. Note that this knob
    is currently supported only on Linux and is not supported on Windows.

## Changes Added in Pin 4.1

- This version introduces the following enhancements:
  - Reduce memory consumption on Windows.
  - Add initial support for images mapped using large pages on Windows.
  - Add support for `PIN_SpawnApplicationThread` on Windows.
  - Add new APIs `PIN_GetWindowsSyscallFromKey` and `PIN_GetKeyFromWindowsSyscall`.
  - Add syscall emulation for `__NR_execveat` on Linux.
  - Improve `umask` isolation on Linux.
- Notable bugs fixed in this version:
  - Pin decodes APX instructions although APX is not supported yet.
    - If APX decoding is for any reason desirable, it should be enabled within a callback
      registered using `PIN_AddXedDecodeCallbackFunction`.
  - Pin launcher erroneously parses the application's `-h` knob.
  - Deadlock when application forks with certain Pin logging enabled.
  - Pin is unable to instrument a shell script without a shebang on Linux.

## Changes Added in Pin 4.0

- This version of Pin 4.x is feature-aligned with Pin 3.32. See breaking changes in the section
  below.
- Pin 4.0 highlights include:
  - Pin 3.x compatibility.
  - Full support for C++17, including `filesystem`, threads, and `atomic`.
  - Modern, OS-agnostic, isolated runtime layer.
  - Efficient, POSIX-compliant memory and thread management.
  - Cross-platform compatibility.
  - Support of out-of-process workload offloading.
- Pin launcher (`PIN_KIT/pin`) changes:
  - Linux: `PIN_KIT/pin` is now a 64-bit executable instead of 32-bit, and can run both 64-bit
    and 32-bit applications as before.
  - Linux: `PIN_KIT/pin32` was added in case Pin needs to run on systems without 64-bit support.
  - Launcher sources were removed from the kit sources.
  - Users who utilize the Pin launcher from an unmodified kit will continue to use it as they have
    until now. Unmodified here means the Pin kit hierarchy, including the location of Pin launcher
    and its dependent binaries relative to it, has not changed.
  - Users who move Pin launcher and its dependent libraries in a way that breaks the original kit
    hierarchy, meaning dependent binaries are no longer relative to Pin launcher in the same way
    they were in the original kit, can use new Pin launcher command-line arguments such as
    `-pin_ld_path_64`, `-pin_lib_64`, and others to specify paths of binaries and library folders
    that Pin launcher needs in order to execute correctly.
  - Users can write their own launcher that will run Pin launcher together with these command-line
    arguments.
  - For more information, run `PIN_KIT/pin -help`.
  - Some usage examples can be found under `<pinkit>/source/tools/Launcher`.
- Added new API functions `PIN_CheckReadAccessEx` and `PIN_CheckWriteAccessEx`.

## Known Issues (Most Recent Version)

- The following knobs are not supported:
  - `-debug_info_max_size`
  - `-memrestrict`
  - `-memlimit`
  - `-restrict_memory` in probe mode
- Windows: stack overflow for **internal** threads is not handled properly and cannot be
  intercepted by the Pintool. Pin will terminate with a segmentation fault.
- Debugging an instrumented application using PinADX may fail or hang on exit on Windows.
- Instrumented application remains stuck after Pin exited. This happens rarely and is not expected
  to affect normal use-cases.

## Breaking Changes Between Pin 3 and Pin 4

Pin 4.x introduces a suite of substantial infrastructure enhancements over its predecessor, Pin 3.

To fully understand the scope and implications of these changes, it is strongly advised to read
the Pin User Guide, with particular attention to the sections highlighted in "What's New in Pin
4.x".

Below is a summary of the critical breaking changes when transitioning from Pin 3 to Pin 4:

- The OS-API provided in Pin 3 is no longer supported in Pin 4. Code previously utilizing the
  OS-API should transition to the corresponding Pin RT API, which is compatible with both Windows
  and Linux platforms.
- The `InstLib` folder provided various utilities that were used by some tools adapted from SDE
  and Pinplay. Since Pinplay has long been part of SDE, these utilities and tools are removed from
  Pin. These utilities may be found as part of the SDE kit.
- Pin 4.x shifts to a Musl-based C runtime for improved ISO C and POSIX compliance, departing from
  Pin 3's Bionic-based runtime. Pintools, which previously relied on Bionic's specific behaviors,
  may need to revise their code to align with the more standardized runtime environment. While Pin
  4's C runtime strives for conformity with ISO C and POSIX standards, it does have its own set of
  deviations. These are outlined in the "Pin C Runtime Deviations From Standard Documentation"
  section of the Pin User Guide.
- The following threading-related behaviors have changed in Pin 4:
  1. `OS_THREAD_ID` is no longer synonymous with `NATIVE_TID`, the native system thread
     identifier. Furthermore, internal thread ID representations such as `OS_THREAD_ID` and
     `THREADID` are now subject to cycling and cannot be assumed to remain unique throughout the
     application's execution. For a more in-depth explanation, consult the "Understanding Thread
     Ids in Pin" section of the Pin User Guide.
  2. The functions `PIN_ThreadUid`, `PIN_Yield`, and `PIN_Sleep` were removed starting with Pin 4.3.
  3. The function `PIN_SpawnInternalThread` has been made more flexible and can now be invoked
     from any point within the code. Additionally, as of Pin 4.0, internal threads can also be
     created using `pthread_create()` and `std::thread()`. For more information, review the
     documentation for `PIN_SpawnInternalThread`.
- The following changes are Linux-specific:
  4. The Pin launcher code is no longer included. Users who need to utilize the Pin launcher
     independently of the Pin kit must now provide additional input parameters. Detailed
     information on this change is provided in the "Recent Changes" section above.
- The following changes are Windows-specific:
  5. Due to constraints associated with LLVM libcxx, Pintools can no longer be compiled using the
     Microsoft MSVC Compiler. Refer to the System Requirements section for more details.
  6. Direct inclusion of `Windows.h` should be replaced with `<windows/pinrt_windows.h>` to
     prevent type conflicts. For guidance on resolving these conflicts, see the "Conflicts between
     Pin and Windows" section in the Pin User Guide.
  7. Pin 4.x changes the way multi-byte, Unicode, characters are handled on Windows. See the
     "Additional information for using Pin on Windows" section above for more details.
- Pin knobs are not parsed nor activated in case of missing application or missing pid to attach
  to. Pin knobs will be activated only if `-- <app>` or `--pid <pid>` was specified.

---

## Disclaimer and Legal Information

The information in this document is subject to change without notice and
Intel Corporation assumes no responsibility or liability for any
errors or inaccuracies that may appear in this document or any
software that may be provided in association with this document. This
document and the software described in it are furnished under license
and may only be used or copied in accordance with the terms of the
license. No license, express or implied, by estoppel or otherwise, to
any intellectual property rights is granted by this document. The
information in this document is provided in connection with Intel
products and should not be construed as a commitment by Intel
Corporation.

EXCEPT AS PROVIDED IN INTEL'S TERMS AND CONDITIONS OF SALE FOR SUCH
PRODUCTS, INTEL ASSUMES NO LIABILITY WHATSOEVER, AND INTEL DISCLAIMS
ANY EXPRESS OR IMPLIED WARRANTY, RELATING TO SALE AND/OR USE OF INTEL
PRODUCTS INCLUDING LIABILITY OR WARRANTIES RELATING TO FITNESS FOR A
PARTICULAR PURPOSE, MERCHANTABILITY, OR INFRINGEMENT OF ANY PATENT,
COPYRIGHT OR OTHER INTELLECTUAL PROPERTY RIGHT. Intel products are not
intended for use in medical, life saving, life sustaining, critical
control or safety systems, or in nuclear facility applications.

Designers must not rely on the absence or characteristics of any
features or instructions marked "reserved" or "undefined." Intel
reserves these for future definition and shall have no responsibility
whatsoever for conflicts or incompatibilities arising from future
changes to them.

The software described in this document may contain software defects
which may cause the product to deviate from published
specifications. Current characterized software defects are available
on request.

Intel and Pentium are trademarks or registered trademarks of
Intel Corporation or its subsidiaries in the U.S. and other countries.

Intel, Xeon, and Intel Xeon Phi are trademarks of Intel Corporation in
the U.S. and/or other countries.

Google Chrome(TM) browser is a trademark of Google LLC. in the U.S. and other countries.

Linux(R) is the registered trademark of Linus Torvalds in the U.S. and other countries.

McAffer(R) is a registered trademark of McAfee, LLC in the U.S. and other countries.

Microsoft, Windows, and the Windows logo are trademarks, or registered trademarks
of Microsoft Corporation in the United States and/or other countries.

*Other names and brands may be claimed as the property of others.

Copyright 2004-2026 Intel Corporation.

Intel Corporation, 2200 Mission College Blvd., Santa Clara, CA 95052-8119, USA.