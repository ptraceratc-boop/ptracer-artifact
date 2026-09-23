#!/usr/bin/env python3

#
# Copyright (C) 2026-2026 Intel Corporation.
# SPDX-License-Identifier: MIT
#

"""
This script generates many separate environment variables and invokes Pin with the test executable
to verify that Pin can handle many separate environment variables.
Usage:
    call_envlen.py -pin <pin_executable> [pin arguments] -app <exe_name> <num_envs> <value_size> [execve]
Example:
    call_envlen.py -pin ../../../pin -app obj-intel64/envlen 1000 100
    Creates 1000 environment variables with fixed names (TEST_ENV_VAR_0001, TEST_ENV_VAR_0002, ...)
    Each value is 100 bytes long.
Example with additional Pin arguments and execve:
    call_envlen.py -pin ../../../pin -xyzzy -app obj-intel64/envlen 1000 100 execve
    Same as above but adds the "execve" argument to the executable to trigger re-launch.
Note:
    Variable names are fixed length (TEST_ENV_VAR_####), so total size per variable is
    name_length (17) + 1 (for '=') + value_size + 1 (for '\\0')
"""

import sys
import os
import subprocess
import platform

def main():
    # Parse arguments: -pin <pin_executable> [pin arguments] -app <exe_name> <num_envs> <value_size> [execve]
    if "-pin" not in sys.argv or "-app" not in sys.argv:
        print("Usage: {} -pin <pin_executable> [pin arguments] -app <exe_name> <num_envs> <value_size> [execve]".format(sys.argv[0]))
        sys.exit(1)

    pin_idx = sys.argv.index("-pin")
    app_idx = sys.argv.index("-app")

    if pin_idx >= app_idx:
        print("Error: -pin must appear before -app")
        sys.exit(1)

    pin_exe = sys.argv[pin_idx + 1]
    pin_args = sys.argv[pin_idx + 2:app_idx]

    app_args = sys.argv[app_idx + 1:]
    if len(app_args) < 3:
        print("Usage: {} -pin <pin_executable> [pin arguments] -app <exe_name> <num_envs> <value_size> [execve]".format(sys.argv[0]))
        sys.exit(1)

    exe = app_args[0]
    num_envs = int(app_args[1])
    value_size = int(app_args[2])

    # Check if execve parameter is present
    add_execve = len(app_args) > 3 and app_args[3] == "execve"

    # ENV_SIZE is the length of the value only (not including the variable name)
    # Variable names are fixed length: TEST_ENV_VAR_0001, TEST_ENV_VAR_0002, etc.

    if value_size < 1:
        print("Error: VALUE_SIZE must be at least 1")
        sys.exit(1)

    # Create a base value of the requested size
    base_value = 'A' * value_size

    # Set environment variables with fixed-width numbering (4 digits, supports up to 9999 vars)
    env = os.environ.copy()
    for i in range(1, num_envs + 1):
        var_name = "TEST_ENV_VAR_{:04d}".format(i)
        env[var_name] = base_value

    # Build Pin command: pin [pin_args] -xyzzy -follow-execv [-ignore_follow_execv_fail 0] -- <exe> [execve]
    cmd = [pin_exe] + pin_args + ["-xyzzy", "-follow-execv"]
    # Add -ignore_follow_execv_fail only on Windows
    if platform.system() == "Windows":
        cmd.extend(["-ignore_follow_execv_fail", "0"])
    cmd.extend(["--", exe])
    if add_execve:
        cmd.append("execve")

    # Execute Pin with the program
    result = subprocess.run(cmd, env=env)
    if result.returncode != 0:
        print("Error: process exited with code 0x{:08X}".format(result.returncode & 0xFFFFFFFF), file=sys.stderr)

    return result.returncode

if __name__ == '__main__':
    sys.exit(int(main()))
