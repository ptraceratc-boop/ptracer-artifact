#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import sys, json, os

sys.path.insert(0, "../")

from pynbin import *

requestReattach = False

@pinglue_function(remote_name="client_stats_cb")
def stats_cb(json_stats_str):
    print("In client_stats_cb")
    print("Printing json_stats_str")
    print(json_stats_str)
    return None

@pinglue_function(remote_name="client_detach_cb")
def detach_cb(json_stats_str):
    global requestReattach
    print("In client_detach_cb")
    print("Printing json_stats_str")
    print(json_stats_str)
    requestReattach = True
    return None

@pinglue_function(remote_name="client_attach_cb")
def reattach_cb():
    print("In client_attach_cb")
    return None


timer_cb_count = 0

@pinglue_function(remote_name="client_timer_cb")
def timer_cb():
    global timer_cb_count, requestReattach
    timer_cb_count += 1
    if requestReattach:
        print("Requesting Reattach")
        requestReattach = False
        return "Reattach"
    if 1 == timer_cb_count:
        print("Requesting CurrentStats")
        return "CurrentStats" 
    if 2 == timer_cb_count:
        print("Requesting Detach")
        return "Detach"
    if 200 == timer_cb_count:
        print("Requesting Exit")
        return "Exit"
    return "Continue"

# Notes:
# 1. GLUE_PIN_KIT environment variable MUST be set.
if __name__ == "__main__":
    print("Running malloctrace_probe_timer_cb test")
    pin = pynbin.Pin.parse_args()
    
    service = Service("malloctrace_probe_timer_cb", 
                      ["server_log_file=log.out", "is_to_instrument_only_main_exe=False", "timer_interval=40"]) \
                      .with_function_target(callback=timer_cb) \
                      .with_function_target(callback=stats_cb) \
                      .with_function_target(callback=detach_cb) \
                      .with_function_target(callback=reattach_cb)

    pin.instrument(service)

    pin.start(wait_done=False)

    json_result_string  = service.wait_for_result()

    print("Printing json_result_string")    
    print(json_result_string)

    pin.wait_until_done()
    print("Done!")
