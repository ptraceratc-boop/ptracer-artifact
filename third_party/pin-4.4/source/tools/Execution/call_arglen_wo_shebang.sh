#
# Copyright (C) 2026-2026 Intel Corporation.
# SPDX-License-Identifier: MIT
#

# This script generates a long string using the specified length
# then it invokes the test executable with this string as first parameter
# in order to have pin instrument the shell script and check
# that pin knows how to handle long parameters or alternatively to exit
# with the right error message
#
# call_arglen_w_shebang.sh <exe_name> <arg_length>
#
# Example: call_arglen_w_shebang.sh arglen 5000
#   Creates an argument that is 5000 bytes long

if [ $# -lt 2 ]; then
    echo "Usage: $0 <exe_name> <arg_length>"
    exit 1
fi

EXE=$1
ARG_LENGTH=$2

# Create an argument of the requested length using repeated 'A' characters
ARG='A'
i=1
while [ $i -lt $ARG_LENGTH ]; do
    ARG="${ARG}A"
    i=$((i + 1))
done


$EXE $ARG execve
