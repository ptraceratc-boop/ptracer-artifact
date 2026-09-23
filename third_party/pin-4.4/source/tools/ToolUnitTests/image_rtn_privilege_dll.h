/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Copyright (C) 2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// Test DLL for runtime loading
// Contains privilege management functions
//

#ifndef IMAGE_RTN_PRIVILEGE_DLL_H
#define IMAGE_RTN_PRIVILEGE_DLL_H

#include <windows.h>

#ifdef IMAGE_RTN_PRIVILEGE_DLL_EXPORTS
#define PRIVILEGE_API __declspec(dllexport)
#else
#define PRIVILEGE_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    // Enable a specific privilege in the current process token
    PRIVILEGE_API BOOL EnablePrivilege(LPCSTR privilegeName);

    // Function pointer type for EnablePrivilege (for runtime loading)
    typedef BOOL (*EnablePrivilegeFunc)(LPCSTR);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_RTN_PRIVILEGE_DLL_H
