#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

"""
Test Description:

Service: Client Test Service - call_trace_mt_replace
Application Target:
    mt_counter.exe

Test Overview:
This test executes a multi-threaded application that has been instrumented with pynbin. In the 'mt_counter.c' program,
specifying the optional program argument "true" (as determined by the 'isFuncReplacedByPin' variable in the main function)
signals that the tool is expected to replace the 'increment_counter' and 'decrement_counter' functions with its own versions,
resulting in the application's global counter being 0 at completion.

The 'call_trace_mt_replace' test instructs the client service 'call_trace_mt_replace' to replace the 'increment_counter'
and 'decrement_counter' functions. The increment and decrement operations are carried out by the tool, which utilizes a global
counter for this purpose. The test aims to confirm that the tool's replacement functions are correctly called and that each invocation
is adequately synchronized with our locking mechanism during server Python function calls. The expectation is that the total count
reported by the tool in the service's result will will match the expected value, as if the original application
functions were executing the logic.

Additionally, this test validates the functionality of 'PIN_AddThreadStartFunction' by tracking and asserting the number of times its callback
is invoked, with the results included in the service's final report.

"""

import sys, json, os

sys.path.insert(0, "../")

from pynbin import *

NUM_INC_THREADS = 100
NUM_DEC_THREADS = 20
COUNT_PER_THREAD = 1000

@pinglue_function(remote_name="is_to_instrument_rtn")
def call_trace_cb(img_name, rtn_name):
    if "increment_counter" == rtn_name or "decrement_counter" == rtn_name:
        return True
    return False

# Notes:
# 1. GLUE_PIN_KIT environment variable MUST be set.
if __name__ == "__main__":
    print("Running call_trace_mt_replace test")
    
    pin = pynbin.Pin.parse_args()
    
    exe_path = os.path.abspath(pin.program)

    service = Service("call_trace_mt_replace").with_function_target(callback=call_trace_cb)

    pin.instrument(service)

    pin.start(wait_done=False)

    json_result_string  = service.wait_for_result()

    print("Printing json_result_string")    
    print(json_result_string)
    json_result = json.loads(json_result_string)
    increment_counter_key = "increment_counter@" + exe_path
    assert (NUM_INC_THREADS * COUNT_PER_THREAD) == json_result[increment_counter_key]["CallCount"] 
    decrement_counter_key = "decrement_counter@" + exe_path
    assert (NUM_DEC_THREADS * COUNT_PER_THREAD) == json_result[decrement_counter_key]["CallCount"] 
    # Plus 1 is for main thread
    assert (NUM_INC_THREADS + NUM_DEC_THREADS + 1) == json_result["total_threads"] 
    expected_global_counter = (NUM_INC_THREADS * COUNT_PER_THREAD) - (NUM_DEC_THREADS * COUNT_PER_THREAD)
    # This checks that since we lock any call to vm the global counter should be same as in app with locks
    assert (expected_global_counter) == json_result["counter"]

    pin.wait_until_done()
    print("Done!")
