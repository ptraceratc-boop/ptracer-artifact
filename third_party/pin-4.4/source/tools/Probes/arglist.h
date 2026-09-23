/*
 * Copyright (C) 2008-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _ARGLIST_H_
#define _ARGLIST_H_

#include <string>
#include <list>

class ARGUMENTS_LIST
{
  public:
    ARGUMENTS_LIST() : m_argv(0) {}
    ARGUMENTS_LIST(int argc, const char* const* argv);
    ~ARGUMENTS_LIST();

    void Add(const std::string& arg);
    int Argc() const;
    char** Argv();
    std::string String() const;

  private:
    void CleanArray();
    void BuildArray();

    std::list< std::string > m_argvStrList;
    char** m_argv;
};

#endif
