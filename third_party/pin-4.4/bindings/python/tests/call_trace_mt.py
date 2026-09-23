#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

"""
Test Description:

Service: Server Service - call_trace
Application Target:
    mt_counter.exe

Test Overview:
This test executes a multi-threaded application that has been instrumented with pynbin. In the 'mt_counter.c' program,
when the optional program argument "true" is not provided (controlled by the 'isFuncReplacedByPin' variable in main),
it indicates that the tool should not replace the 'increment_counter' and 'decrement_counter' functions. Instead, the tool
is expected to instrument these functions, as the 'call_trace' service would do if instructed to insert instrumentation before
these function calls.

In this specific test, we configure the 'call_trace' service to instrument the 'increment_counter' and 'decrement_counter'
functions. We then verify that the application's logic, particularly the global counter, remains unaffected and thread-safe,
as confirmed by the assertion at the end of the application's execution. Additionally, we assert that the number of calls to
the 'increment_counter' and 'decrement_counter' functions matches the expected count, ensuring that the instrumentation is
functioning correctly in a multi-threaded environment.
"""

import sys, json, os

sys.path.insert(0, "../")

from pynbin import *

@pinglue_function(remote_name="is_to_instrument_rtn")
def call_trace_cb(img_name, rtn_name):
    if "increment_counter" == rtn_name or "decrement_counter" == rtn_name:
        return True
    return False

# Notes:
# 1. GLUE_PIN_KIT environment variable MUST be set.
if __name__ == "__main__":
    print("Running call_trace_mt test")
    pin = pynbin.Pin.parse_args()

    exe_path = os.path.abspath(pin.program)
    
    service = Service("call_trace").with_function_target(callback=call_trace_cb)

    pin.instrument(service)

    pin.start(wait_done=False)

    json_result_string  = service.wait_for_result()

    print("Printing json_result_string")    
    print(json_result_string)
    json_result = json.loads(json_result_string)
    increment_counter_key = "increment_counter@" + exe_path
    assert 100000 == json_result[increment_counter_key]["CallCount"] 
    decrement_counter_key = "decrement_counter@" + exe_path
    assert 20000 == json_result[decrement_counter_key]["CallCount"] 

    pin.wait_until_done()
    print("Done!")
