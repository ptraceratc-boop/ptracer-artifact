#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

info = open("script_invocation.out", "w")

def Taken(ctxt):
    takenIp = pin.PIN_GetContextReg(ctxt, pin.REG_INST_PTR)
    info.write(f"Taken: IP = {hex(takenIp)}\n")
    
def Before(ctxt):
    beforeIP = pin.PIN_GetContextReg(ctxt, pin.REG_INST_PTR)
    info.write(f"Before: IP = {hex(beforeIP)}\n")
    
def After(ctxt):
    afterIP = pin.PIN_GetContextReg(ctxt, pin.REG_INST_PTR)
    info.write(f"After: IP = {hex(afterIP)}\n")

def image_load(img):
    sec = pin.IMG_SecHead(img)
    while(pin.SEC_Valid(sec)):
        rtn = pin.SEC_RtnHead(sec)
        while(pin.RTN_Valid(rtn)):
            pin.RTN_Open(rtn)
            pin.RTN_InsertCall(rtn, pin.IPOINT_AFTER, After, pin.IARG_CONTEXT)
            
            ins = pin.RTN_InsHead(rtn)
            while(pin.INS_Valid(ins)):
                if pin.INS_IsRet(ins):
                    pin.INS_InsertCall(ins, pin.IPOINT_BEFORE, Before, pin.IARG_CONTEXT)
                    pin.INS_InsertCall(ins, pin.IPOINT_TAKEN_BRANCH, Taken, pin.IARG_CONTEXT)
                
                ins = pin.INS_Next(ins)
                
            pin.RTN_Close(rtn)
            rtn = pin.RTN_Next(rtn)
        
        sec = pin.SEC_Next(sec)
    
def fini(code):
    info.close()

pin.IMG_AddInstrumentFunction(image_load)
pin.PIN_AddFiniFunction(fini)
