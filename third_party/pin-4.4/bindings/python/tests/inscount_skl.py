#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

# A client side PynBin script that implements instruction counting using a skeleton service

import sys, json

sys.path.insert(0, "../") # We assume that the pynbin directory is located at ../

from pynbin import *

# Start declaring PinGlue Server functions

@pinglue_server_script
def docount(c):
    global total
    total += c

@pinglue_server_script
def trace_instrumentation(trace):
    bbl = pin.TRACE_BblHead(trace) 
    while(pin.BBL_Valid(bbl)):
        pin.BBL_InsertCall(bbl, pin.IPOINT_BEFORE, docount, pin.IARG_UINT32, pin.BBL_NumIns(bbl))
        bbl = pin.BBL_Next(bbl)

@pinglue_server_script
def fini(code):
    """Callback function called when the program exits
    Args:
    code: The exit code of the program
    """
    print(f"inscount: The total number of executed instructions are:{total}")
    send_result_callback_message()

@pinglue_server_script
def send_result_callback_message():
    data = {
        "Count": total,
        "Error": "AOK"
    }
    json_string = json.dumps(data)
    print(f"In send_result_callback_message: JSON data: {json_string}")
    # Set the service result (as JSON) to the client
    Glue_SendServiceResultCallback(json_string)

@pinglue_server_script
def send_result_on_error(e):
    data = {
        "Count": total,
        "Error": str(e)
    }
    json_string = json.dumps(data)
    print(f"In send_result_on_error: JSON data: {json_string}")
    # Set the service result (as JSON) to the client
    Glue_SendServiceResultCallback(json_string)

@pinglue_server_script
def instrument():
    global total
    total = 0
    pin.TRACE_AddInstrumentFunction(trace_instrumentation)
    pin.PIN_AddFiniFunction(fini)

# We don't set process_knobs and it's OK

# The PynBin Client Code

if __name__ == "__main__":
    
    # Create a PinGlue instance from the command line arguments
    pin = pynbin.Pin.parse_args() 
    
    # Create a service object for the service named inscount_img
    service = Service("skeleton") \
        .with_function_target(docount) \
        .with_function_target(trace_instrumentation) \
        .with_function_target(fini) \
        .with_function_target(send_result_callback_message) \
        .with_function_target(send_result_on_error) \
        .with_function_target(instrument)

    # Request the PinGlue server to open the service
    pin.instrument(service)

    # Start executing the instrumented application
    # If no argument is passed or wait_done is True then this function
    # Will block until Pin is done executing the instrumented application.
    # This will create a deadlock if the service requires calling client callbacks
    # unless the following call to service.wait_for_result() is done in a 
    # separate thread
    pin.start(wait_done=False)

    # Wait for the service to complete. Upon return json_result_string will 
    # contain the result sent using Glue_SendServiceResultCallback
    json_result_string  = service.wait_for_result()

    # We need to call this function explicitely because we passed wait_done=False
    # to pin.start
    pin.wait_until_done()

    print("Printing json_result_string")    
    print(json_result_string)
    
    data = json.loads(json_result_string)
    assert int == type(data["Count"]) 
    if 0 == data["Count"]:
        print(data["Error"])
