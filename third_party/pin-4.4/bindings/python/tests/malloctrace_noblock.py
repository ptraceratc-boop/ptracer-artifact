#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import sys, json, os

sys.path.insert(0, "../")

from pynbin import *

@pinglue_noblock
@pinglue_function(remote_name="Arg1Before")
def arg1_before(name, size):
    size = hex(size) if size else 0
    print(f"{name}({size})\n")

@pinglue_function(remote_name="MallocAfter")
@pinglue_noblock
def malloc_after(ret):
    print(f"  returns {hex(ret)}\n")


# Note GLUE_PIN_KIT environment variable MUST be set.
# e.g: tc shell> setenv GLUE_PIN_KIT ../../../../build/pin-internal-w-x-y-gcc-linux
#
# To run the test with cmd args, run:
# python malloctrace_noblock.py --pinargs="arg1 arg2 ..." --glueargs="arg1 arg2 ..." -- <program> <prog_arg1> <prog_arg2> ...
# When -- <program> is mandatory and others are optional.
# example: python malloctrace_noblock.py --glueargs="-services-path TestServices" -- ls ../ -la
if __name__ == "__main__":
    if 1 < len(sys.argv):
        print("Running malloctrace_noblock test with cmd line args")
        pin = pynbin.Pin.parse_args() 
    else:
        print("Running malloctrace_noblock test on /bin/ls")
        pin = pynbin.Pin("/bin/ls", program_args=["-la"])
    
    service = Service("malloctrace").with_function_target(
        callback=arg1_before).with_function_target(callback=malloc_after)

    pin.instrument(service)

    pin.start(wait_done=False)

    json_result_string  = service.wait_for_result()

    print("Printing json_result_string")    
    print(json_result_string)
    
    data = json.loads(json_result_string)
    assert "Dummy" == data["Name"]

    pin.wait_until_done()