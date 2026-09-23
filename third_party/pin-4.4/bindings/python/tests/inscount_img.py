#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import sys, json

sys.path.insert(0, "../") # We assume that the pynbin directory is located at ../

from pynbin import *

@pinglue_function(remote_name="should_instrument_image")
def check_image(imgName, isMainExecutable):
    """ This function will be called in response to the service calling should_instrument_image
    If we were to call this function should_instrument_image we could have avoided
    setting the remote name and just decorate the function with
    @pinglue_function
    """
    if isMainExecutable:
        print(f"Requesting instruction count for {imgName}")
        return True
    return False

# Note GLUE_PIN_KIT environment variable MUST be set.
# To run the test with cmd args, run:
# python instcount_img.py --pinargs="arg1 arg2 ..." --glueargs="<arg1 arg2 ...>" -- <program> <prog_arg1> <prog_arg2> ...
# When -- <program> is mandatory and others are optional.
# example: python instcount_img.py --glueargs="-services-path TestServices" -- ls -la ./
if __name__ == "__main__":
    
    # Create a PinGlue instance from the command line arguments
    pin = pynbin.Pin.parse_args() 
    
    # Create a service object for the service named inscount_img
    service = Service("inscount_img").with_function_target(callback=check_image)

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
    assert 0 < data["Count"]
