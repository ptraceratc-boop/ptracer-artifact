#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin
    
def image_instrumentation_cb(img):
    count = 0
    
    sec = pin.IMG_SecHead(img)
    while(pin.SEC_Valid(sec)):
        rtn = pin.SEC_RtnHead(sec)
        while(pin.RTN_Valid(rtn)):
            # Prepare for processing of RTN, an  RTN is not broken up into BBLs,
            # it is merely a sequence of INSs
            pin.RTN_Open(rtn)
            
            ins = pin.RTN_InsHead(rtn)
            while(pin.INS_Valid(ins)):
                count +=1
                ins = pin.INS_Next(ins)

            # to preserve space, release data associated with RTN after we have processed it
            pin.RTN_Close(rtn)
            
            rtn = pin.RTN_Next(rtn)
        
        sec = pin.SEC_Next(sec)

    print(f"Image {pin.IMG_Name(img)} has  {count} instructions")


pin.IMG_AddInstrumentFunction(image_instrumentation_cb)
