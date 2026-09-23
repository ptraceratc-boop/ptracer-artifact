/*
 * Copyright (C) 2006-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* ===================================================================== */
/*! @file
  This is a negative test.
*/

/* ===================================================================== */
#include "pin.H"
#include <iostream>




/* ===================================================================== */
VOID ImageLoad(IMG img, VOID* v)
{
    if (IMG_IsMainExecutable(img))
    {
        std::string s = "333";
        INT64 i  = Uint64FromString(s);
        std::cout << s << "=" << i << std::endl;

        s        = "0xa";
        UINT64 j = Uint64FromString(s);
        std::cout << s << "=" << j << std::endl;

        s = "0Xb";
        j = Uint64FromString(s);
        std::cout << s << "=" << j << std::endl;

        s = "12345678901234567890";
        j = Uint64FromString(s);
        std::cout << s << "=" << j << std::endl;

        // this should report an error
        s = "123456789012345678901";
        j = Uint64FromString(s);
        std::cout << s << "=" << j << std::endl;
    }
}

/* ===================================================================== */
int main(INT32 argc, CHAR* argv[])
{
    PIN_InitSymbols();

    PIN_Init(argc, argv);

    IMG_AddInstrumentFunction(ImageLoad, 0);

    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
