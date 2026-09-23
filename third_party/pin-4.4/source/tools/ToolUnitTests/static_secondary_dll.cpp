/*
 * Copyright (C) 2008-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  pin tool combined from multi-DLLs (main_dll, dynamic_secondary_dll, static_secondary_dll). 
 */

#include <iostream>
#include <fstream>






std::ofstream outfile;

extern "C" __declspec(dllexport) void Init1(const char* out_filename)
{
    outfile.open(out_filename);
    outfile << std::hex << std::showbase;
}

extern "C" __declspec(dllexport) void BeforeBBL1(void* ip) { outfile << "Before BBL, ip " << ip << std::endl; }

extern "C" __declspec(dllexport) void Fini1() { outfile.close(); }

// Define main - will never be called
// can be avoided by removing /EXPORT:main from link flags
int main(int argc, char* argv[]) { return 0; }
