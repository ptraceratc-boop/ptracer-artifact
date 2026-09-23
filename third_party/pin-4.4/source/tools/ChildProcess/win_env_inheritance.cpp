/*
 * Copyright (C) 2021-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include <iostream>
#include <fstream>
#include <unistd.h>





__declspec(dllimport) extern CHAR** environ;

KNOB< std::string > KnobParentEnvLog(KNOB_MODE_OVERWRITE, "pintool", "parentEnvLog", "ParentEnvLog.log", "");
KNOB< std::string > KnobChildEnvLog(KNOB_MODE_OVERWRITE, "pintool", "childEnvLog", "ChildEnvLog.log", "");
KNOB< std::string > KnobGrandChildEnvLog(KNOB_MODE_OVERWRITE, "pintool", "grandChildEnvLog", "GrandChildEnvLog.log", "");

struct
{
    INT32 generation          = -1;
    const CHAR* strGeneration = "";

    const CHAR* ENV1_NAME  = "PARENT";
    const CHAR* ENV1_VALUE = "ANAKIN_SKYWALKER";

    const CHAR* ENV2_NAME  = "CHILD";
    const CHAR* ENV2_VALUE = "LEIA_ORGANA";

    const CHAR* ENV3_NAME  = "GRANDCHILD";
    const CHAR* ENV3_VALUE = "BEN_SOLO";

} static Data;

extern int access(const char*, int);

// use mutex to synchronize outputs from multiple instances of this tool
VOID MutexWriteToStdout(CHAR* msg);

// Check that envName exists\doesn't exists in current environment
VOID CheckEnv(const CHAR* envName, BOOL shouldExists, const CHAR* envExpectedValue = NULL);

// Delete a file
VOID RemoveFile(const CHAR* file);

// Print all envrionment variables to a file
VOID PrintEnvironmentVariables(const CHAR* fileName);

// Safetly print message
VOID MutexWriteToStdout(CHAR* msg);

// Make some final checks before existing
VOID Fini(INT32 code, VOID* v);

// Functionality per generation
VOID IAmParent();
VOID IAmChild();
VOID IAmGrandChild();

VOID CheckEnv(const CHAR* envName, BOOL shouldExists, const CHAR* envExpectedValue)
{
    const CHAR* envActualVal = getenv(envName);

    if (shouldExists)
    {
        ASSERT(envActualVal, "\nGeneration " + Data.strGeneration + ":Environment variable " + envName + "Doesn't exists.");
        ASSERTX(envExpectedValue);
        ASSERT(!strcmp(envActualVal, envExpectedValue), std::string("") + "\nGeneration " + Data.strGeneration +
                                                            ":Environment variable " + envName +
                                                            "Incorrect value."
                                                            " Expected: " +
                                                            envExpectedValue + ", Actual: " + envActualVal);
    }
    else
    {
        ASSERT(!envActualVal, "\nGeneration " + Data.strGeneration + ":Environment variable " + envName + " exists.");
    }
}

VOID RemoveFile(const CHAR* file)
{
    // If file exists
    if (access(file, F_OK) == 0)
        // Try to remove it
        ASSERT(!remove(file), "Failed to remove the file: " + file);
}

VOID PrintEnvironmentVariables(const CHAR* fileName)
{
    std::ofstream file;
    file.open(fileName);
    ASSERT(!file.fail(), "Failed open fail: " + fileName);
    for (CHAR** entry = environ; *entry; entry++)
        file << *entry << "\n";
    file.close();
}

VOID IAmGrandChild()
{
    RemoveFile(KnobGrandChildEnvLog.Value().c_str());

    CheckEnv(Data.ENV1_NAME, TRUE, Data.ENV1_VALUE);
    CheckEnv(Data.ENV2_NAME, TRUE, Data.ENV2_VALUE);

    CheckEnv(Data.ENV3_NAME, FALSE);
    setenv(Data.ENV3_NAME, Data.ENV3_VALUE, 1);
    CheckEnv(Data.ENV3_NAME, TRUE, Data.ENV3_VALUE);

    PrintEnvironmentVariables(KnobGrandChildEnvLog.Value().c_str());
}

VOID IAmChild()
{
    RemoveFile(KnobChildEnvLog.Value().c_str());

    CheckEnv(Data.ENV1_NAME, TRUE, Data.ENV1_VALUE);

    CheckEnv(Data.ENV2_NAME, FALSE);
    setenv(Data.ENV2_NAME, Data.ENV2_VALUE, 1);
    CheckEnv(Data.ENV2_NAME, TRUE, Data.ENV2_VALUE);

    CheckEnv(Data.ENV3_NAME, FALSE);

    PrintEnvironmentVariables(KnobChildEnvLog.Value().c_str());
}

// Call only on parent process
VOID Fini(INT32 code, VOID* v)
{
    // Make sure that the other processes hasn't changed the parent's environment
    CheckEnv(Data.ENV1_NAME, TRUE, Data.ENV1_VALUE);
    CheckEnv(Data.ENV2_NAME, FALSE);
    CheckEnv(Data.ENV3_NAME, FALSE);

    printf("\nParent process finished successfully");
    fflush(stdout);
}

VOID IAmParent()
{
    PIN_AddFiniFunction(Fini, 0);

    RemoveFile(KnobParentEnvLog.Value().c_str());

    CheckEnv(Data.ENV1_NAME, FALSE);
    setenv(Data.ENV1_NAME, Data.ENV1_VALUE, 1);
    CheckEnv(Data.ENV1_NAME, TRUE, Data.ENV1_VALUE);
    unsetenv(Data.ENV1_NAME);
    CheckEnv(Data.ENV1_NAME, FALSE);
    setenv(Data.ENV1_NAME, Data.ENV1_VALUE, 1);
    CheckEnv(Data.ENV1_NAME, TRUE, Data.ENV1_VALUE);

    CheckEnv(Data.ENV2_NAME, FALSE);
    CheckEnv(Data.ENV3_NAME, FALSE);

    PrintEnvironmentVariables(KnobParentEnvLog.Value().c_str());
}

VOID FindGeneration(INT32 argc, CHAR** argv)
{
    Data.strGeneration = NULL;
    for (uint32_t i = 0; i < argc - 1; i++)
    {
        if (!strcmp(argv[i], "-currentGeneration"))
        {
            Data.strGeneration = argv[i + 1];
            break;
        }
    }

    ASSERT(Data.strGeneration, "\nCouldn't find arg: currentGeneration");

    CHAR* endptr    = NULL;
    Data.generation = strtol(Data.strGeneration, &endptr, 10);
    ASSERT(endptr != Data.strGeneration || !*endptr, "\nCouldn't parse arg: " + Data.strGeneration);

    ASSERTX(Data.generation <= 3 && Data.generation >= 1);
}

int main(INT32 argc, CHAR** argv)
{
    PIN_InitSymbols();
    PIN_Init(argc, argv);

    FindGeneration(argc, argv);

    switch (Data.generation)
    {
        case 1:
            IAmParent();
            break;
        case 2:
            IAmChild();
            break;
        case 3:
            IAmGrandChild();
            break;
        default:
            ASSERTX(FALSE);
            break;
    }

    PIN_StartProgram();
    return 0;
}
