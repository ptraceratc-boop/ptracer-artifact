/*
 * Copyright (C) 2007-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#define _CRT_SECURE_NO_DEPRECATE

#define UNICODE
#include <Windows.h>
#include <direct.h>
#include <iostream>
#include <string>




// launch types:
// 1. make <compiler> <compile flags> <source file name> <exe name>
//    - copy <source file name> to <unicode source file name>
//    - compile <unicode source file name>, executable name will be <exe name>
//    - delete all files it created except from the executable
// 2. test <path>/pin <pin flags + [pin tool]> -- <exe name>
//    - copy <exe name> to <unicode exe name>
//    - launch Pin
//    - delete the files it created

int make(int argc, wchar_t* argv[], wchar_t* envp[]);
int test(int argc, wchar_t* argv[], wchar_t* envp[]);

int wmain(int argc, wchar_t* argv[], wchar_t* envp[])
{
    if (_wcsicmp(argv[1], L"make") == 0)
    {
        return make(argc - 2, &argv[2], envp);
    }
    if (_wcsicmp(argv[1], L"test") == 0)
    {
        return test(argc - 2, &argv[2], envp);
    }

    std::wcout << "Bad arguments to Windows Unicode test launcher" << std::endl;
    return 0;
}

//Internationalization in Japanese (encoded in UTF16)
static const wchar_t i18n[] = {0x56fd, 0x969B, 0x5316, 0x0000};
static const std::wstring i18nStr(i18n);

// <compiler> <compile flags> <source file name>
int make(int argc, wchar_t* argv[], wchar_t* envp[])
{
    std::wstring compilerLine = L"";
    std::wstring env          = L"";
    for (int i = 0; envp[i] != NULL; i++)
    {
        env += std::wstring(L"set ") + std::wstring(envp[i]) + std::wstring(L"&");
    }
    //Copy all env. vars to the compiler cmd
    compilerLine += env;

    //Build Compiler command line
    for (int i = 0; i < argc - 1; i++)
    {
        compilerLine += std::wstring(argv[i]) + std::wstring(L" ");
    }

    // "copy <source file name> <Unicode file name>"
    std::wstring sourceFullFileName = argv[argc - 1];

    // split exeFullFileName into dirName and exeName
    size_t namePos  = sourceFullFileName.rfind(L'/') + 1;
    std::wstring srcName = sourceFullFileName.substr(namePos);
    std::wstring dirName = sourceFullFileName.substr(0, namePos);

    std::wstring newSourceFileName = std::wstring(L"prefix_") + i18nStr + std::wstring(L"_") + srcName;
    std::wstring copyStr           = std::wstring(L"cd ") + dirName + std::wstring(L"&copy ") + srcName + std::wstring(L" ") +
                           newSourceFileName + std::wstring(L"&cd ..");

    _wsystem(copyStr.c_str());

    // "<compiler> <compile flags> <Unicode file name>"
    compilerLine += std::wstring(L" ") + newSourceFileName;
    _wsystem(compilerLine.c_str());

    // "del <unicode name>.*"
    std::wstring delStr = std::wstring(L"del ") + newSourceFileName;
    _wsystem(delStr.c_str());

    return 0;
}

// <path>/pin <pin flags + [pin tool]> -- <exe name>
int test(int argc, wchar_t* argv[], wchar_t* envp[])
{
    std::wstring cmdLine = L"";

    // "copy <exe name> <unicode exe name>"
    // "copy <pdb name> <unicode pdb name>"
    std::wstring exeFullFileName = argv[argc - 1];

    // split exeFullFileName into dirName and exeName
    size_t namePos  = exeFullFileName.rfind(L'/') + 1;
    std::wstring exeName = exeFullFileName.substr(namePos);
    std::wstring dirName = exeFullFileName.substr(0, namePos);
    //exeFullFileName.replace()

    // compose new exe name that contains unicode chars
    std::wstring newExeName = std::wstring(L"prefix_") + i18nStr + std::wstring(L"_") + exeName;

    // move original exe to unicode exe
    std::wstring copyStr = std::wstring(L"cd ") + dirName + std::wstring(L"&&(move ") + exeName + std::wstring(L" ") + newExeName
                           + std::wstring(L"&cd ..)");
    _wsystem(copyStr.c_str());

    // take care for pdb file
    // remove .exe and add .pdb
    std::wstring baseName = exeName.substr(0, exeName.rfind(L".exe"));
    std::wstring pdbName  = baseName + std::wstring(L".pdb");

    std::wstring newBaseName = newExeName.substr(0, newExeName.rfind(L".exe"));
    std::wstring newPdbName  = newBaseName + std::wstring(L".pdb");

    // move original pdb file to unicode pdb
    copyStr = std::wstring(L"cd ") + dirName + std::wstring(L"&&(move ") + pdbName + std::wstring(L" ") + newPdbName +
              std::wstring(L"&cd ..)");
	// Renaming .pdb file doesn't work, since Pin's symbol server searches .pdb file using its base name embedded in .exe.
	// This test currently is not capable to modify this name in debug directory of the .exe
	// So the move is suppressed.
	// Base name of the .pdb file in current directory matches one in debug directory of the renamed .exe,
	// so Pin's symbol server should find the .pdb in current directory.
    //_wsystem(copyStr.c_str());

    // Build command line
    for (int i = 0; i < argc - 1; i++)
    {
        if ((i > 0) && (_wcsicmp(L"-uni_param", argv[i - 1])) == 0)
        {
            std::wstring newUnicodeParamName = dirName + newExeName;
            cmdLine += newUnicodeParamName + std::wstring(L" ");
            continue;
        }
        cmdLine += std::wstring(argv[i]) + std::wstring(L" ");
    }
    cmdLine += dirName + newExeName + std::wstring(L" ") + i18nStr;

    int ret = 0;

    // Invoke Pin
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(STARTUPINFO);
    memset(&pi, 0, sizeof(pi));
    // command line: "<path>/pin.exe -- <unicode name>.exe <unicode param>"
    if (!CreateProcess(NULL, (LPWSTR)cmdLine.c_str(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        std::cout << "Couldn't invoke pin" << std::endl;
        ret = 1;
    }
    else
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    // "del <unicode name>.*"
    std::wstring delStr =
        std::wstring(L"cd ") + dirName + std::wstring(L"&del ") + newBaseName + std::wstring(L".* ") + std::wstring(L"& cd ..");
    _wsystem(delStr.c_str());

    return ret;
}
