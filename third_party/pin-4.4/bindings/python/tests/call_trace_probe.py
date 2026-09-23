#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import sys, json, os

sys.path.insert(0, "../")

from pynbin import *

@pinglue_function(remote_name="is_to_instrument_rtn")
def call_trace_cb(img_name, rtn_name):
    if "malloc" == rtn_name or "called_func" == rtn_name:
        return True
    return False


# Notes:
# 1. GLUE_PIN_KIT environment variable MUST be set.
if __name__ == "__main__": 
    print("Running call_trace_probe test")
    pin = pynbin.Pin.parse_args()

    exe_path = os.path.abspath(pin.program)

    service = Service("call_trace_probe").with_function_target(callback=call_trace_cb)
    pin.instrument(service)
    pin.start(wait_done=False)
    
    json_result_string  = service.wait_for_result()

    print("Printing json_result_string")    
    print(json_result_string)
    json_result = json.loads(json_result_string)
    expected_key = "called_func@" + exe_path
    assert 5 == json_result[expected_key]["CallCount"]
    
    pin.wait_until_done()
    print("Done!")
