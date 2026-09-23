#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

info = open("script_fxsave.out", "w")
printFxaveNow = False

def StartRoutine():
    global printFxaveNow
    printFxaveNow = True

def StopRoutine():
    global printFxaveNow
    printFxaveNow = False

def PrintFXSAVE(ctxt):
    global printFxaveNow
    
    if printFxaveNow == False:
        return
    
    fxsaveSet = FXSAVE()
    buffSet = BUFFER(fxsaveSet.sizeof(), fxsaveSet.addr())
    buffSet.set_uint16(12)
    buffSet.set_uint16(34, 2)
    pin.PIN_SetContextFXSave(ctxt, fxsaveSet.addr())
    
    fxsaveGet = FXSAVE()
    pin.PIN_GetContextFXSave(ctxt, fxsaveGet.addr())
    buffGet = BUFFER(fxsaveGet.sizeof(), fxsaveGet.addr())
    
    info.write(f"_fcw:{buffGet.get_uint16()}\n")
    info.write(f"_fsw:{buffGet.get_uint16(2)}\n")
    
    assert(12 == buffGet.get_uint16())
    assert(34 == buffGet.get_uint16(2))
    
    try:
        buffSet.set_uint16(7, fxsaveGet.sizeof() - 1)
        assert False # Exception should happen here
    except Exception as e:
        info.write(f"An expected exception occurred: {e}")
    

def image_load(img):
    if pin.IMG_IsMainExecutable(img):
        StartRtn = pin.RTN_FindByName(img, "Start")
        assert(pin.RTN_Valid(StartRtn))
        pin.RTN_Open(StartRtn)
        pin.RTN_InsertCall(StartRtn, pin.IPOINT_BEFORE, StartRoutine)
        pin.RTN_Close(StartRtn)

        StopRtn = pin.RTN_FindByName(img, "Stop")
        assert(pin.RTN_Valid(StopRtn))
        pin.RTN_Open(StopRtn)
        pin.RTN_InsertCall(StopRtn, pin.IPOINT_AFTER, StopRoutine)
        pin.RTN_Close(StopRtn)
        
def trace_cb(trace):
    pin.TRACE_InsertCall(trace, pin.IPOINT_BEFORE, PrintFXSAVE, pin.IARG_CONTEXT)
    
def fini(code):
    info.close()

pin.IMG_AddInstrumentFunction(image_load)
pin.TRACE_AddInstrumentFunction(trace_cb)
pin.PIN_AddFiniFunction(fini)
