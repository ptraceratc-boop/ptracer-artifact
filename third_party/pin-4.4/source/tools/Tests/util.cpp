/*
 * Copyright (C) 2010-2021 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  General tests for pin util functions.
 */

#include <fstream>
#include "pin_tests_util.H"

VOID CharsUtilTest()
{
    CHAR c = 'a';
    TEST('A' == CharToUpper(c), "CharToUpper failed");

    TEST(10 == CharToHexDigit(c), "CharToHexDigit failed");

    TEST(false == CharIsSpace(c), "CharIsSpace failed");
    TEST(true == CharIsSpace(' '), "CharIsSpace failed");

    //printf("Chars util tests completed\n");
}

VOID StringsUtilTest()
{
    std::string str = "hello/world";
    TEST(str == Joinpath("hello", "world"), "Joinpath failed");
    TEST(str == Joinpath("hello/", "world"), "Joinpath failed");

    TEST(("DD1,234,567,890" == StringBignum(1234567890, 15, 'D')), "StringBignum failed");
    // if digits < len(number), digits and padding are irrelevant
    TEST("-1,234,567,890" == StringBignum(-1234567890, 7, 'B'), "StringBignum failed");

    TEST(("T" == StringBool(true)) && ("F" == StringBool(false)), "StringBool failed");

    TEST(("Y" == StringTri(TRI_YES)) && ("N" == StringTri(TRI_NO)) && ("M" == StringTri(TRI_MAYBE)), "StringTri failed");

    TEST(float(100) == FLT64FromString("1.0E2"), "FLT64FromString failed");

    //printf("Strings util tests completed\n");
}

VOID TempFileTest()
{
    CHAR tmpl[18]  = "/tmp/test.XXXXXX";
    CHAR* filename = CreateTmpFileName(tmpl, sizeof tmpl);

    fstream tmpStream(filename);
    TEST(tmpStream.is_open(), "Fail to open " + std::string(filename) + "\n");
    //printf("%s is now opened\n", filename);

    tmpStream.close();

    INT32 removed = remove(filename);
    TEST(removed == 0, "Can't remove tmp file " + std::string(filename) + "\n");
    //printf("%s is now removed\n", filename);
}

VOID TimeTest()
{
    MilliSecondsElapsed();
    TEST(MilliSecondsElapsed() == 0, "MilliSecondsElapsed failed");
}

int main(INT32 argc, CHAR** argv)
{
    PIN_Init(argc, argv);

    CharsUtilTest();
    StringsUtilTest();
    TempFileTest();
    TimeTest();

    TEST(PIN_VmFullPath() != std::string(), "PIN_VmFullPath failed");
    BreakMe(); // Empty function

    // Never returns
    PIN_StartProgram();

    return 0;
}
