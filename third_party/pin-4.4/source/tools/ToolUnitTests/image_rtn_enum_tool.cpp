/*
 * Copyright (C) 2026-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*
 * Copyright (C) 2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

//
// This tool enumerates all loaded images (executable and DLLs) and searches
// for specific routines (RTNs) in each image.
// It demonstrates:
// 1. Image enumeration
// 2. Routine searching across multiple images
// 3. Handling compile-time linked and runtime-loaded DLLs
//

#include <iostream>
#include <fstream>
#include <set>
#include <string>
#include "pin.H"

// Command line knob for output file name
KNOB< std::string > KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "", "specify output file name");

// Output file
std::ofstream outFile;

// Set to track which images we've already processed
std::set< std::string > processedImages;

// Routines to search for (modify this list for different routines)
const char* targetRoutines[] = {
    "main",               // Main executable
    "ReadRegistryString", // From compile-time linked registry DLL
    "EnablePrivilege"     // From runtime-loaded privilege DLL
};

const int numTargetRoutines = sizeof(targetRoutines) / sizeof(targetRoutines[0]);

// Function to check if a string contains a substring (case insensitive)
BOOL ContainsSubstring(const std::string& str, const std::string& substr)
{
    std::string strLower    = str;
    std::string substrLower = substr;

    // Convert to lowercase for case-insensitive comparison
    for (size_t i = 0; i < strLower.length(); i++)
    {
        strLower[i] = tolower(strLower[i]);
    }
    for (size_t i = 0; i < substrLower.length(); i++)
    {
        substrLower[i] = tolower(substrLower[i]);
    }

    return strLower.find(substrLower) != std::string::npos;
}

// Analyze an image and search for target routines
VOID AnalyzeImage(IMG img)
{
    std::string imgName = IMG_Name(img);

    // Skip if already processed
    if (processedImages.find(imgName) != processedImages.end())
    {
        return;
    }

    processedImages.insert(imgName);

    outFile << "========================================" << std::endl;
    outFile << "Image: " << imgName << std::endl;
    outFile << "Image ID: " << IMG_Id(img) << std::endl;
    outFile << "Load Offset: 0x" << std::hex << IMG_LoadOffset(img) << std::dec << std::endl;
    outFile << "Low Address: 0x" << std::hex << IMG_LowAddress(img) << std::dec << std::endl;
    outFile << "High Address: 0x" << std::hex << IMG_HighAddress(img) << std::dec << std::endl;
    outFile << "Is Main Executable: " << (IMG_IsMainExecutable(img) ? "Yes" : "No") << std::endl;
    outFile << "========================================" << std::endl;

    // Determine which image we're analyzing
    bool isMainExecutable = IMG_IsMainExecutable(img);
    bool isRegistryDll    = ContainsSubstring(imgName, "image_rtn_registry_dll");
    bool isPrivilegeDll   = ContainsSubstring(imgName, "image_rtn_privilege_dll");

    // Determine which routine to search for in this image
    const char* targetRoutine = NULL;
    if (isMainExecutable)
    {
        targetRoutine = "main";
    }
    else if (isRegistryDll)
    {
        targetRoutine = "ReadRegistryString";
    }
    else if (isPrivilegeDll)
    {
        targetRoutine = "EnablePrivilege";
    }

    // Count routines found
    int routinesFound = 0;
    int totalRoutines = 0;

    // Iterate through all routines in the image
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec))
    {
        for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn))
        {
            totalRoutines++;
            std::string rtnName         = RTN_Name(rtn);
            std::string undecoratedName = PIN_UndecorateSymbolName(rtnName, UNDECORATION_NAME_ONLY);

            // Check if this routine matches the target routine for this image
            if (targetRoutine != NULL && undecoratedName == targetRoutine)
            {
                RTN_Open(rtn);

                outFile << "  [FOUND] Routine: " << undecoratedName << std::endl;
                outFile << "    Raw Name: " << rtnName << std::endl;
                outFile << "    Address: 0x" << std::hex << RTN_Address(rtn) << std::dec << std::endl;
                outFile << "    Size: " << RTN_Size(rtn) << " bytes" << std::endl;
                outFile << "    Number of Instructions: " << RTN_NumIns(rtn) << std::endl;

                RTN_Close(rtn);
                routinesFound++;
                break; // Found the target routine, no need to continue searching
            }
        }
    }

    outFile << "Total routines in image: " << totalRoutines << std::endl;
    if (routinesFound > 0)
    {
        outFile << "Total target routines found in this image: " << routinesFound << std::endl;
    }
    else
    {
        outFile << "No target routines found in this image." << std::endl;
    }

    outFile << std::endl;
    outFile.flush();
}

// Pin calls this function every time a new image is loaded
VOID ImageLoad(IMG img, VOID* v)
{
    std::string imgName = IMG_Name(img);

    // Only process the main executable and our two specific DLLs
    bool isMainExecutable = IMG_IsMainExecutable(img);
    bool isRegistryDll    = ContainsSubstring(imgName, "image_rtn_registry_dll");
    bool isPrivilegeDll   = ContainsSubstring(imgName, "image_rtn_privilege_dll");

    // Skip system DLLs and other images
    if (!isMainExecutable && !isRegistryDll && !isPrivilegeDll)
    {
        return;
    }

    outFile << "[IMAGE LOAD EVENT] Loading: " << IMG_Name(img) << std::endl;
    outFile.flush();

    AnalyzeImage(img);
}

// Pin calls this function every time an image is unloaded
VOID ImageUnload(IMG img, VOID* v)
{
    std::string imgName = IMG_Name(img);

    // Only log unload events for images we care about
    bool isMainExecutable = IMG_IsMainExecutable(img);
    bool isRegistryDll    = ContainsSubstring(imgName, "image_rtn_registry_dll");
    bool isPrivilegeDll   = ContainsSubstring(imgName, "image_rtn_privilege_dll");

    if (isMainExecutable || isRegistryDll || isPrivilegeDll)
    {
        outFile << "[IMAGE UNLOAD EVENT] Unloading: " << IMG_Name(img) << std::endl;
        outFile.flush();
    }
}

// This function is called when the application exits
VOID Fini(INT32 code, VOID* v)
{
    outFile << std::endl;
    outFile << "========================================" << std::endl;
    outFile << "Application Exit" << std::endl;
    outFile << "Exit Code: " << code << std::endl;
    outFile << "========================================" << std::endl;
    outFile << std::endl;

    // Summary
    outFile << "Summary:" << std::endl;
    outFile << "Total unique images processed: " << processedImages.size() << std::endl;
    outFile << "Images:" << std::endl;
    for (std::set< std::string >::iterator it = processedImages.begin(); it != processedImages.end(); ++it)
    {
        outFile << "  - " << *it << std::endl;
    }

    outFile.close();
}

// Print usage information
INT32 Usage()
{
    std::cerr << "This tool enumerates images and searches for specific routines" << std::endl;
    std::cerr << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

int main(INT32 argc, CHAR** argv)
{
    // Initialize symbol processing
    PIN_InitSymbols();

    // Initialize PIN
    if (PIN_Init(argc, argv))
    {
        return Usage();
    }

    // Open output file using the output file name from command line
    std::string outputFileName = KnobOutputFile.Value();
    outFile.open(outputFileName.c_str());
    if (!outFile.is_open())
    {
        std::cerr << "Error: Could not open output file: " << outputFileName << std::endl;
        return 1;
    }

    outFile << "========================================" << std::endl;
    outFile << "Image & Routine Enumeration Tool" << std::endl;
    outFile << "Mode: " << (PIN_IsProbeMode() ? "PROBE" : "JIT") << std::endl;
    outFile << "========================================" << std::endl;
    outFile << "Target Routines:" << std::endl;
    for (int i = 0; i < numTargetRoutines; i++)
    {
        outFile << "  - " << targetRoutines[i] << std::endl;
    }
    outFile << "========================================" << std::endl;
    outFile << std::endl;
    outFile.flush();

    // Register image load/unload callbacks
    IMG_AddInstrumentFunction(ImageLoad, 0);
    IMG_AddUnloadFunction(ImageUnload, 0);

    // Register Fini to be called when the application exits
    PIN_AddFiniFunction(Fini, 0);

    // Start the program based on mode
    if (PIN_IsProbeMode())
    {
        PIN_StartProgramProbed();
    }
    else
    {
        PIN_StartProgram();
    }

    return 0;
}
