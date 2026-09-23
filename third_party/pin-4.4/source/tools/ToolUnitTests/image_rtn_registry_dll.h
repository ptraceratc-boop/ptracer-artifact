/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Copyright (C) 2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// Test DLL for compile-time linking
// Contains registry-related functions
//

#ifndef IMAGE_RTN_REGISTRY_DLL_H
#define IMAGE_RTN_REGISTRY_DLL_H

#include <windows.h>

#ifdef IMAGE_RTN_REGISTRY_DLL_EXPORTS
#define REGISTRY_API __declspec(dllexport)
#else
#define REGISTRY_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    // Read a registry string value
    REGISTRY_API BOOL ReadRegistryString(HKEY hKeyRoot, LPCSTR subKey, LPCSTR valueName, LPSTR buffer, DWORD bufferSize);

    // Read a registry DWORD value
    REGISTRY_API BOOL ReadRegistryDWORD(HKEY hKeyRoot, LPCSTR subKey, LPCSTR valueName, LPDWORD pValue);

    // Callback function type for EnumerateRegistryValues
    typedef void (*RegistryValueCallback)(LPCSTR valueName, DWORD valueType, LPBYTE data, DWORD dataSize, LPVOID context);

    // Enumerate all values in a registry key
    REGISTRY_API LONG EnumerateRegistryValues(HKEY hKeyRoot, LPCSTR subKey, RegistryValueCallback callback, LPVOID context);

#ifdef __cplusplus
}
#endif

#endif // IMAGE_RTN_REGISTRY_DLL_H
