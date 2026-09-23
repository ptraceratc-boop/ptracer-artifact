#
# Copyright (C) 2024-2026 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import pin, c

total = 0

info = open("script_replacesig.out", "w")

def fini(code):
    global total
    info.write(f"In fini, total calls to NewMalloc:{total}\n")
    info.close()
	
def NewMalloc(ctxt, tid, orgFuncptr, proto, arg0, returnIp):
    global total
    total += 1
    info.write(f"NewMalloc ({orgFuncptr}, {arg0}, {hex(returnIp)})\n")

    return pin.GLUE_CallApplicationFunction(ctxt, tid, pin.CALLINGSTD_DEFAULT, orgFuncptr, False, proto, arg0)

def image_instrumentation_cb(img):
    rtn = pin.RTN_FindByName(img, "malloc")

    if pin.RTN_Valid(rtn):
        info.write(f"Replacing malloc in {pin.IMG_Name(img)}\n")
        proto_malloc = pin.PROTO_Allocate(pin.PIN_PARG_POINTER, pin.CALLINGSTD_DEFAULT, "malloc", pin.PIN_PARG_INT)

        pin.RTN_ReplaceSignature(rtn, NewMalloc, proto_malloc, pin.IARG_CONST_CONTEXT, 
                                    pin.IARG_THREAD_ID,pin.IARG_ORIG_FUNCPTR, 
                                    pin.IARG_PTR, proto_malloc, pin.IARG_FUNCARG_ENTRYPOINT_VALUE, 0, pin.IARG_RETURN_IP)


pin.IMG_AddInstrumentFunction(image_instrumentation_cb)
pin.PIN_AddFiniFunction(fini)
