#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

"""
Tests Description:

Service: Client Test Service - inscount

Test Overview:
The primary objectives of this test are:
1. The basic operations of the pynbin module's APIs, including:
    - Initialization with pynbin.Pin()
    - Argument parsing with pynbin.Pin.parse_args()
    - Service instantiation with Service()
    - Instrumentation setup with pin.instrument()
    - Execution control with pin.start()
    - Target function specification with service.with_function_target()
    - Result retrieval with service.wait_for_result()
    - Completion synchronization with pin.wait_until_done().
2. The functionality of client-defined callbacks and their transmission to the server using service.with_function_target():
    - Verification of input-output argument handling for each supported data type.
    - Ensuring the service correctly dispatches arguments to the corresponding client callback (client_callback for single arguments, client_callback_no_args, and client_callback_all_types).
    - Validation that callbacks return the same value and type as the input argument, including testing 'None' return values for void functions without arguments.
3. The functionality of server-defined callbacks, which are annotated with @pinglue_server_script and dispatched using service.with_function_target():
    - Declaration of the three server callbacks: foo, foo_no_args, foo_all_types.
    - Verification of input-output argument handling, similar to the client callback tests.
4. The functionality of Service.wait_for_result():
    - Evaluation of the service's result callback mechanism, where the total instruction count is sent back to the client, which then verifies that a non-zero value is received.

This test suite is designed to ensure the robustness and reliability of the pynbin module's API and its interaction with client and server callbacks, as well as the accurate transmission and processing of data between these components.
"""


import sys, json, os

sys.path.insert(0, "../")

from pynbin import *

@pinglue_function(remote_name="client_callback")
def callback(arg):
    return arg

@pinglue_function(remote_name="client_callback_no_args")
def callback_no_args():
    return

@pinglue_function(remote_name="client_callback_all_types")
def callback_all_types(*args):
    assert args ==  ("HELLO", 2.5, -1, 4294967296, True, False, None)
    return 2.5

@pinglue_server_script(remote_name="foo")
def server_script_foo(arg):
    return arg


@pinglue_server_script(remote_name="foo_no_args")
def server_script_foo_no_args():
    return

@pinglue_server_script(remote_name="foo_all_types")
def server_script_foo_all_types(*args):
    assert args ==  ("HELLO", 2.5, -1, 4294967296, True, False, None)
    return 2.5


# Note GLUE_PIN_KIT environment variable MUST be set.
#
# To run the test with cmd args, run:
# python basic.py --pinargs="arg1 arg2 ..." --glueargs="<arg1 arg2 ...>" -- <program> <prog_arg1> <prog_arg2> ...
# When -- <program> is mandatory and others are optional.
# example: python basic.py --glueargs="-services-path TestServices" -- ls -la ../
if __name__ == "__main__":
    if 1 < len(sys.argv):
        print("Running basic test with cmd line args")
        pin = pynbin.Pin.parse_args() 
    else:
        print("Running basic test on /bin/ls")
        pin = pynbin.Pin("/bin/ls", program_args=["-la"], glue_args = ["-services-path", "TestServices"])
    
    service = Service("inscount").with_function_target(
        callback=callback)
    service = service.with_function_target(callback=callback_no_args)
    service = service.with_function_target(callback=callback_all_types)
    service = service.with_function_target(callback=server_script_foo)
    service = service.with_function_target(callback=server_script_foo_no_args)
    service = service.with_function_target(callback=server_script_foo_all_types)

    pin.instrument(service)

    pin.start(wait_done=False)

    json_result_string  = service.wait_for_result()

    print("Printing json_result_string")    
    print(json_result_string)
    
    data = json.loads(json_result_string)
    assert int == type(data["Count"]) 
    assert 0 < data["Count"]

    pin.wait_until_done()