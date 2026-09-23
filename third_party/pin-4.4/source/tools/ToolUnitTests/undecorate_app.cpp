/*
 * Copyright (C) 2009-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/* This application does nothing
 * It is used only for checking symbol demangling feature
 */

#include <vector>
#include <string>



#ifdef TARGET_WINDOWS
#include "windows.h"
#define EXPORT_SYM __declspec(dllexport)
#else
#define EXPORT_SYM
#endif

EXPORT_SYM class A
{
  public:
    EXPORT_SYM void MyMethod1(const std::vector< int >& param1) {}
    EXPORT_SYM void MyMethod2(std::string param2) {}
    EXPORT_SYM void MyMethod3(std::string param3) const {}
};

template< class T > EXPORT_SYM T MyMethod(T& arg) { return arg; }

template< class B > class MyMethod1 : public B
{
  public:
    EXPORT_SYM MyMethod1() {}
};

/*
 * This is a template function, therefore the symbol name will include the std::string
 * "operator<". Pin should not corrupt this std::string.
 */
template< class T > EXPORT_SYM T my_operator(const T& src) { return src; }

EXPORT_SYM void Foo()
{
    int i;
    volatile int* ip = (volatile int*)&i;
    *ip              = 0;
}

extern "C" EXPORT_SYM void MyBar()
{
    int i;
    volatile int* ip = (volatile int*)&i;
    *ip = 0;
}

// If you add new names, then update undecorate.cpp as well, so that it knows about them...
int main()
{
    A a;
    MyMethod1< A > goo;
    std::vector< int > v;
    v.push_back(1);

    a.MyMethod1(v);
    a.MyMethod2("MyString");
    a.MyMethod3("Foo");
    MyMethod(a);

    Foo();
    MyBar();

    int b = my_operator< int >(0);

    return 0;
}
