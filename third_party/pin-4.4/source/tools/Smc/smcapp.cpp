/*
 * Copyright (C) 2008-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  An example of SMC application. 
 */
#include "../Utils/smc_util.h"
#include "../Utils/sys_memory.h"
#include <stdio.h>

/*!
 * Exit with the specified error message
 */
static void Abort(std::string msg)
{
    std::cerr << msg << std::endl;
    exit(1);
}

/*!
 * The main procedure of the application.
 */
int main(int argc, char* argv[])
{
    std::cerr << "SMC in the image of the application" << std::endl;

    // buffer to move foo/bar routines into and execute
    static char staticBuffer[PI_FUNC::MAX_SIZE];
    // Set read-write-execute protection for the buffer
    size_t pageSize = GetPageSize();
    char* firstPage = (char*)(((size_t)staticBuffer) & ~(pageSize - 1));
    char* endPage   = (char*)(((size_t)staticBuffer + sizeof(staticBuffer) + pageSize - 1) & ~(pageSize - 1));
    if (!MemProtect(firstPage, endPage - firstPage, MEM_READ_WRITE_EXEC))
    {
        Abort("MemProtect failed");
    }
    FILE* fp = fopen("smcapp.out", "w");
    fprintf(fp, "%p %p\n", firstPage, endPage);
    printf("App: firstPage %p endPage %p\n", firstPage, endPage);
    fclose(fp);
    for (int i = 0; i < 3; ++i)
    {
        FOO_FUNC fooFunc;
        fooFunc.Copy(staticBuffer).Execute().AssertStatus();
        std::cerr << fooFunc.Name() << ": " << fooFunc.ErrorMessage() << std::endl;

        BAR_FUNC barFunc;
        barFunc.Copy(staticBuffer).Execute().AssertStatus();
        std::cerr << barFunc.Name() << ": " << barFunc.ErrorMessage() << std::endl;
    }
    /*
    std::cerr << "Dynamic code generation" << std::endl;
    void * dynamicBuffer;
    dynamicBuffer = MemAlloc(PI_FUNC::MAX_SIZE, MEM_READ_WRITE_EXEC);
    if (dynamicBuffer == 0) {Abort("MemAlloc failed");}

    {
        FOO_FUNC fooFunc;
        fooFunc.Copy(dynamicBuffer);
        if (!MemProtect(dynamicBuffer, PI_FUNC::MAX_SIZE, MEM_READ_EXEC)) {Abort("MemProtect failed");}
        for (int i = 0; i < 3; ++i)
        {
            fooFunc.Execute().AssertStatus();
            std::cerr << fooFunc.Name() << ": " << fooFunc.ErrorMessage() << std::endl;
        }
    }

    if (!MemProtect(dynamicBuffer, PI_FUNC::MAX_SIZE, MEM_READ_WRITE_EXEC)) {Abort("MemProtect failed");}

    {
        BAR_FUNC barFunc;
        barFunc.Copy(dynamicBuffer);
        if (!MemProtect(dynamicBuffer, PI_FUNC::MAX_SIZE, MEM_READ_EXEC)) {Abort("MemProtect failed");}
        for (int i = 0; i < 3; ++i)
        {
            barFunc.Execute().AssertStatus();
            std::cerr << barFunc.Name() << ": " << barFunc.ErrorMessage() << std::endl;
        }
    }
    */

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
