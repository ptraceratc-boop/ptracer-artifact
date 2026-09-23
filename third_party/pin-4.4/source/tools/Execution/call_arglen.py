#!/usr/bin/env python3

#
# Copyright (C) 2026-2026 Intel Corporation.
# SPDX-License-Identifier: MIT
#

"""
This script generates many separate arguments and invokes Pin with the test executable
to verify that Pin can handle many separate command line arguments.
Usage:
    call_arglen.py -pin <pin_executable> [pin arguments] -app <exe_name> <num_args> <arg_size> [execve]
Example:
    call_arglen.py -pin ../../../pin -app obj-intel64/arglen 1000 100
      Creates 1000 arguments, each 100 bytes long
Example with execve: 
    call_arglen.py -pin ../../../pin -app obj-intel64/arglen 1000 100 execve
    Same as above but adds "execve" argument to the executable to trigger re-launch
"""

import sys
import os
import subprocess
import platform

def main():
    # Parse arguments: -pin <pin_executable> [pin arguments] -app <exe_name> <num_args> <arg_size> [execve]
    if "-pin" not in sys.argv or "-app" not in sys.argv:
        print("Usage: {} -pin <pin_executable> [pin arguments] -app <exe_name> <num_args> <arg_size> [execve]".format(sys.argv[0]))
        sys.exit(1)

    pin_idx = sys.argv.index("-pin")
    app_idx = sys.argv.index("-app")

    if pin_idx >= app_idx:
        print("Error: -pin must appear before -app", file=sys.stderr)
        sys.exit(1)

    pin_exe = sys.argv[pin_idx + 1]
    pin_extra_args = sys.argv[pin_idx + 2:app_idx]

    app_args = sys.argv[app_idx + 1:]
    if len(app_args) < 3:
        print("Usage: {} -pin <pin_executable> [pin arguments] -app <exe_name> <num_args> <arg_size> [execve]".format(sys.argv[0]))
        sys.exit(1)

    exe = app_args[0]
    num_args = int(app_args[1])
    arg_size = int(app_args[2])

    # Check if execve parameter is present
    add_execve = len(app_args) > 3 and app_args[3] == "execve"

    # Create a base argument of the requested size
    base_arg = 'A' * arg_size

    # Build Pin command: pin [pin_extra_args] -xyzzy -follow-execv [-ignore_follow_execv_fail 0] -- <exe> <arg1> <arg2> ... [execve]
    cmd = [pin_exe] + pin_extra_args + ["-xyzzy", "-follow-execv"]
    # Add -ignore_follow_execv_fail only on Windows
    if platform.system() == "Windows":
        cmd.extend(["-ignore_follow_execv_fail", "0"])
    cmd.extend(["--", exe])
    # Add all the arguments
    for i in range(num_args):
        cmd.append(base_arg)
    if add_execve:
        cmd.append("execve")

    # Execute Pin with the program
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print("Error: process exited with code 0x{:08X}".format(result.returncode & 0xFFFFFFFF), file=sys.stderr)

    return result.returncode

if __name__ == '__main__':
    sys.exit(int(main()))

