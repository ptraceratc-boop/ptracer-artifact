#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

FILE_TO_FIND = "hello"
info = open("script_GetSourceLocation.out", "w")

def image(img):
    global FILE_TO_FIND
    if not pin.IMG_IsMainExecutable(img):
        return
    info.write(f"Image name = {pin.IMG_Name(img)}\n")

    sec = pin.IMG_SecHead(img)
    while(pin.SEC_Valid(sec)):
        rtn = pin.SEC_RtnHead(sec)
        
        while(pin.RTN_Valid(rtn)):
            pin.RTN_Open(rtn)
            
            ins = pin.RTN_InsHead(rtn)
            while(pin.INS_Valid(ins)):
                res = pin.PIN_GetSourceLocation(pin.INS_Address(ins))
                if FILE_TO_FIND in res[2]:
                    info.write(f"column: {res[0]}, line: {res[1]}, srcfile: {res[2]}\n")         
            
                ins = pin.INS_Next(ins)
            
            pin.RTN_Close(rtn)
            rtn = pin.RTN_Next(rtn)
        sec = pin.SEC_Next(sec)    

pin.IMG_AddInstrumentFunction(image)
