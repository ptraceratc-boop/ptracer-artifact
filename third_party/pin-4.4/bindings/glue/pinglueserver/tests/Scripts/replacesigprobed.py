#
# Copyright (C) 2024-2026 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin

total = 0

info = open("script_replacesigprobed.out", "w")

def Probe_Exit_IA32(orgFuncptr, proto, code):
    global total
    info.write(f"In Probe_Exit_IA32, total calls to NewMalloc:{total}\n")
    info.close()
    
    return pin.GLUE_CallApplicationFunctionProbed(orgFuncptr, proto, code)
	
def NewMalloc(orgFuncptr, proto, arg0, returnIp):
    global total
    total += 1
    info.write(f"NewMalloc ({orgFuncptr}, {arg0}, {hex(returnIp)})\n")

    return pin.GLUE_CallApplicationFunctionProbed(orgFuncptr, proto, arg0)

def image_instrumentation_cb(img):
    rtn = pin.RTN_FindByName(img, "malloc")

    if pin.RTN_Valid(rtn):
        if pin.RTN_IsSafeForProbedReplacement(rtn):
            info.write(f"Replacing malloc in {pin.IMG_Name(img)}\n")
            proto_malloc = pin.PROTO_Allocate(pin.PIN_PARG_POINTER, pin.CALLINGSTD_DEFAULT, "malloc", pin.PIN_PARG_INT)

            pin.RTN_ReplaceSignatureProbed(rtn, NewMalloc, proto_malloc, pin.IARG_ORIG_FUNCPTR, 
                                           pin.IARG_PTR, proto_malloc, pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0, pin.IARG_RETURN_IP)

        else:
            info.write(f"Skip replacing malloc in {pin.IMG_Name(img)} since it is not safe.\n")
            
    exitRtn = pin.RTN_FindByName(img, "_exit")
    if pin.RTN_Valid(exitRtn) and pin.RTN_IsSafeForProbedReplacement(exitRtn):
        proto_exit = pin.PROTO_Allocate(pin.PIN_PARG_VOID, pin.CALLINGSTD_DEFAULT, "_exit", pin.PIN_PARG_INT)

        pin.RTN_ReplaceSignatureProbed(exitRtn, Probe_Exit_IA32, proto_exit, 
                                       pin.IARG_ORIG_FUNCPTR, pin.IARG_PTR, proto_exit,
                                       pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0)
        info.write(f"Replaced exit() in:{pin.IMG_Name(img)}\n")

pin.IMG_AddInstrumentFunction(image_instrumentation_cb)
