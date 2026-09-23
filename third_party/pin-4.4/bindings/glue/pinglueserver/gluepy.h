
/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUEPY_H_
#define _GLUEPY_H_

#include "pocketpy.h"

namespace pkpy
{
PY_CAST_INT(int8_t);
}

#include <mutex>

/**
 * @brief A global lock for accessing the pocketpy VM
 * 
 */
struct Pinglue_GIL
{
    static std::mutex GIL_;

    static inline void lock() { GIL_.lock(); }

    static inline void unlock() { GIL_.unlock(); }
};

/**
 * @brief Acquire the GIL and automatically release it at the end of scope (RAII)
 * 
 */
struct Scoped_GIL_guard
{
    Scoped_GIL_guard() { Pinglue_GIL::lock(); }
    ~Scoped_GIL_guard() { Pinglue_GIL::unlock(); }

    Scoped_GIL_guard(const Scoped_GIL_guard&)            = delete;
    Scoped_GIL_guard(Scoped_GIL_guard&&)                 = delete;
    Scoped_GIL_guard& operator=(const Scoped_GIL_guard&) = delete;
    Scoped_GIL_guard operator=(Scoped_GIL_guard&&)       = delete;
};

/**
 * @brief Release the GIL and automatically re-acquire it at the end of scope (RAII)
 * 
 */
struct Scoped_GIL_temp_release
{
    Scoped_GIL_temp_release() { Pinglue_GIL::unlock(); }
    ~Scoped_GIL_temp_release() { Pinglue_GIL::lock(); }

    Scoped_GIL_temp_release(const Scoped_GIL_temp_release&)            = delete;
    Scoped_GIL_temp_release(Scoped_GIL_temp_release&&)                 = delete;
    Scoped_GIL_temp_release& operator=(const Scoped_GIL_temp_release&) = delete;
    Scoped_GIL_temp_release operator=(Scoped_GIL_temp_release&&)       = delete;
};

using namespace pkpy;

#endif // _GLUEPY_H_
