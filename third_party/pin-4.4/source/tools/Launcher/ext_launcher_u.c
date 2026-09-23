/*
 * Copyright (C) 2024-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *
 * This test checks the ability to run Pin launcher in an environemnt which is not the normal kit.
 * This mimics the behaviour used by some of our clients in which pin launcher, executables and libraries are moved around
 * and therefore Pin launcher needs to know where to find everything.
 * To support this Pin launcher supply flags that allow specifying the locations of Pin executables and libraries.
 * In order to mimic this behaviour pin launcher is copied into Kit/source/tools/Launcher folder.
 * Current application (external launcher) is calling the copied launcher. That way Pin launcher will be called from 
 * a location in which binaries it needs are not in the location it expect them to be (relative to it) and will crash if
 * the above flags are not supplied correctly.
 * Currently this tests the following scenarios (controlled by environment variables):
 * 1. Calling Pin launcher with all flags (64-bit 32-bit). Test case should pass
 * 2. Calling Pin launcher with 64-bit only flags on a 64 bit application (TEST_CASE_2=1). Test case should pass
 * 3. Calling Pin launcher with a missing flag -pinbin_path_64 (TEST_CASE_3=1). When one of the executables flags is provided 
 *    all executables flags should be provided (in same bitness). Therefore this test case is expected to fail with proper 
 *    error message.
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

/* For testing purposes only */
#if 0
/*!
 * Prints the command line arguments.
 * @param child_argv Command line arguments array. Must be null terminated.
 */
static void print_argv_chunks(char** child_argv)
{
    char** p = child_argv;
    unsigned int i = 0;
    printf("\n");
    while (*p)
    {
        printf("argv[%d] = [%s]\n", i, *p);
        p++;
        i++;
    }
}
#endif

char* appendPath(const char* s1, const char* s2, const char* s3)
{
    int n   = 1;
    char* p = 0;
    if (s1) n += strnlen(s1, PATH_MAX);
    if (s2) n += strnlen(s2, PATH_MAX);
    if (s3) n += strnlen(s3, PATH_MAX);
    p = (char*)calloc(n, sizeof(char));
    if (p == NULL) abort();
    if (s1) strcpy(p, s1);
    if (s2) strcat(p, s2);
    if (s3) strcat(p, s3);
    return p;
}

/*!
 * Build command line that will execute Pin launcher with all relevant flags
 */
static char** build_child_argv(int argc, char** argv)
{
#define EXT_LAUNCHER_ARG_COUNT 25 // 12*2 + 1

    int var = 0, user_arg = 0, child_argv_ind = 0;
    char** child_argv = (char**)malloc(sizeof(char*) * (argc + EXT_LAUNCHER_ARG_COUNT + 1));

    if (child_argv == NULL)
    {
        abort();
    }

    const char* kitRoot = getenv("PIN_KIT_ROOT_PATH");
    assert(NULL != kitRoot);

    const bool is_test_case2 = (NULL != getenv("TEST_CASE_2"));
    const bool is_test_case3 = (NULL != getenv("TEST_CASE_3"));
    const bool is_test_case4 = (NULL != getenv("TEST_CASE_4"));
    const bool is_test_case5 = (NULL != getenv("TEST_CASE_5"));

    child_argv[child_argv_ind++] = appendPath(kitRoot, "/source/tools/Launcher/cloned_pin", NULL);

    if (!is_test_case4) // Check error on not giving the flags at all
    {

        child_argv[child_argv_ind++] = strdup("-pin_ld_path_64");
        child_argv[child_argv_ind++] = appendPath(kitRoot, "/intel64/pinrt/bin/pin.ld.so", NULL);

        if (is_test_case5) // Check error on giving wrong path
        {
            child_argv[child_argv_ind++] = strdup("-pind_path_64");
            child_argv[child_argv_ind++] = appendPath(kitRoot, "/wrong/path", NULL);
        }

        else
        {
            child_argv[child_argv_ind++] = strdup("-pind_path_64");
            child_argv[child_argv_ind++] = appendPath(kitRoot, "/intel64/pinrt/bin/pind", NULL);
        }

        if (!is_test_case3)
        {
            child_argv[child_argv_ind++] = strdup("-pinbin_path_64");
            child_argv[child_argv_ind++] = appendPath(kitRoot, "/intel64/bin/pinbin", NULL);
        }

        child_argv[child_argv_ind++] = strdup("-pin_lib_64");
        child_argv[child_argv_ind++] = appendPath(kitRoot, "/intel64/pinrt/lib", NULL);

        child_argv[child_argv_ind++] = strdup("-pin_lib_64");
        child_argv[child_argv_ind++] = appendPath(kitRoot, "/intel64/lib", NULL);

        child_argv[child_argv_ind++] = strdup("-pin_lib_64");
        child_argv[child_argv_ind++] = appendPath(kitRoot, "/extras/xed-intel64/lib", NULL);

        if (!is_test_case2)
        {
            child_argv[child_argv_ind++] = strdup("-pin_ld_path_32");
            child_argv[child_argv_ind++] = appendPath(kitRoot, "/ia32/pinrt/bin/pin.ld.so", NULL);

            if(is_test_case5) // Check error on giving wrong path
            {
                child_argv[child_argv_ind++] = strdup("-pind_path_32");
                child_argv[child_argv_ind++] = appendPath(kitRoot, "/wrong/path", NULL);
            }

            else
            {
               child_argv[child_argv_ind++] = strdup("-pind_path_32");
            child_argv[child_argv_ind++] = appendPath(kitRoot, "/ia32/pinrt/bin/pind", NULL);
            }

            child_argv[child_argv_ind++] = strdup("-pinbin_path_32");
            child_argv[child_argv_ind++] = appendPath(kitRoot, "/ia32/bin/pinbin", NULL);

            child_argv[child_argv_ind++] = strdup("-pin_lib_32");
            child_argv[child_argv_ind++] = appendPath(kitRoot, "/ia32/pinrt/lib", NULL);

            child_argv[child_argv_ind++] = strdup("-pin_lib_32");
            child_argv[child_argv_ind++] = appendPath(kitRoot, "/ia32/lib", NULL);

            child_argv[child_argv_ind++] = strdup("-pin_lib_32");
            child_argv[child_argv_ind++] = appendPath(kitRoot, "/extras/xed-ia32/lib", NULL);
        }

        // ./pin \
        // -pin_ld_path_64 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/intel64/pinrt/bin/pin.ld.so \
        // -pin_ld_path_32 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/ia32/pinrt/bin/pin.ld.so \
        // -pind_path_64 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/intel64/pinrt/bin/pind \
        // -pind_path_32 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/ia32/pinrt/bin/pind \
        // -pinbin_path_64 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/intel64/bin/pinbin \
        // -pinbin_path_32 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/ia32/bin/pinbin  \
        // -pin_lib_64 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/intel64/pinrt/lib \
        // -pin_lib_64 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/intel64/lib \
        // -pin_lib_64 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/extras/xed-intel64/lib \
        // -pin_lib_32 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/ia32/pinrt/lib \
        // -pin_lib_32 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/ia32/lib \
        // -pin_lib_32 /nfs/iil/ptl/bistro/speret2/PIN/pin4/build/pin-internal-w-x-y-gcc-linux/extras/xed-ia32/lib
    }

    /* Copy original command line parameters. */
    for (var = 1; var < argc; ++var)
    {
        child_argv[child_argv_ind++] = argv[var];
    }

    /* Null terminate the array. */
    child_argv[child_argv_ind++] = NULL;

    return child_argv;

#undef EXT_LAUNCHER_ARG_COUNT
}

/*!
 * External launcher runs copied launcer with needed flags
 */
int main(int orig_argc, char** orig_argv)
{
    char* path_to_cmd;
    char** child_argv;

    if (orig_argv == NULL || orig_argv[0] == NULL) abort();

    child_argv  = build_child_argv(orig_argc, orig_argv);
    path_to_cmd = child_argv[0];

    /* For testing purposes */
#if 0
     print_argv_chunks(child_argv);
#endif

    return execv(path_to_cmd, child_argv);
}
