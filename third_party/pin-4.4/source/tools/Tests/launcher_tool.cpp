/*
 * Copyright (C) 2012-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <stdlib.h>
#include <string>



/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
INT32 Usage()
{
    std::cerr << "This tool checks that the launcher setups the environment correctly. \n"
            "\n";
    std::cerr << KNOB_BASE::StringKnobSummary();
    std::cerr << std::endl;
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */
int main(int argc, char* argv[])
{
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

#if defined(TARGET_LINUX)
    // Check that LD_LIBRARY_PATH contains the required libraries to the VM.
    //
    char* ld_library_path = getenv("LD_LIBRARY_PATH");
    if (ld_library_path)
    {
        std::string ld_path(ld_library_path);
        // Adding ':' after each std::string because they all new to be before the old values in the
        // LD_LIBRARY_PATH.
        //
    }
#endif // TARGET_LINUX

    std::cout << "Tool success!" << std::endl;

    // Never returns
    PIN_StartProgram();

    return 0;
}
/* ===================================================================== */
/* eof */
/* ===================================================================== */
