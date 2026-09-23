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

#include "image_rtn_registry_dll.h"
#include <stdio.h>

// Read a registry string value
REGISTRY_API BOOL ReadRegistryString(HKEY hKeyRoot, LPCSTR subKey, LPCSTR valueName, 
                                     LPSTR buffer, DWORD bufferSize)
{
    HKEY hKey;
    LONG result;
    DWORD dataType;
    DWORD dataSize = bufferSize;
    
    if (buffer == NULL || bufferSize == 0)
    {
        return FALSE;
    }
    
    result = RegOpenKeyExA(hKeyRoot, subKey, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return FALSE;
    }
    
    result = RegQueryValueExA(hKey, valueName, NULL, &dataType, (LPBYTE)buffer, &dataSize);
    
    RegCloseKey(hKey);
    
    if (result != ERROR_SUCCESS)
    {
        return FALSE;
    }
    
    if (dataType != REG_SZ && dataType != REG_EXPAND_SZ)
    {
        return FALSE;
    }
    
    return TRUE;
}

// Read a registry DWORD value
REGISTRY_API BOOL ReadRegistryDWORD(HKEY hKeyRoot, LPCSTR subKey, LPCSTR valueName, 
                                    LPDWORD pValue)
{
    HKEY hKey;
    LONG result;
    DWORD dataType;
    DWORD dataSize = sizeof(DWORD);
    
    if (pValue == NULL)
    {
        return FALSE;
    }
    
    result = RegOpenKeyExA(hKeyRoot, subKey, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return FALSE;
    }
    
    result = RegQueryValueExA(hKey, valueName, NULL, &dataType, (LPBYTE)pValue, &dataSize);
    
    RegCloseKey(hKey);
    
    if (result != ERROR_SUCCESS)
    {
        return FALSE;
    }
    
    if (dataType != REG_DWORD)
    {
        return FALSE;
    }
    
    return TRUE;
}

// Enumerate all values in a registry key
REGISTRY_API LONG EnumerateRegistryValues(HKEY hKeyRoot, LPCSTR subKey, 
                                          RegistryValueCallback callback, LPVOID context)
{
    HKEY hKey;
    LONG result;
    DWORD index = 0;
    DWORD valueCount = 0;
    char valueName[256];
    BYTE data[1024];
    DWORD valueNameSize;
    DWORD dataSize;
    DWORD valueType;
    
    if (callback == NULL)
    {
        return -1;
    }
    
    result = RegOpenKeyExA(hKeyRoot, subKey, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS)
    {
        return -1;
    }
    
    while (TRUE)
    {
        valueNameSize = sizeof(valueName);
        dataSize = sizeof(data);
        
        result = RegEnumValueA(hKey, index, valueName, &valueNameSize, 
                              NULL, &valueType, data, &dataSize);
        
        if (result == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        else if (result != ERROR_SUCCESS)
        {
            break;
        }
        
        callback(valueName, valueType, data, dataSize, context);
        
        index++;
        valueCount++;
    }
    
    RegCloseKey(hKey);
    
    return valueCount;
}

// DLL entry point
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return TRUE;
}
