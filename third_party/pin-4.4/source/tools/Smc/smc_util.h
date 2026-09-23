/*
 * Copyright (C) 2008-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

/*! @file
 *  Utilities for SMC tests. 
 */
#ifndef SMC_UTIL_H
#define SMC_UTIL_H

#include <string.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <iomanip>
#include <cstdlib>
#include <ctime>
#include "../Utils/runnable.h"
#include "../Utils/sys_memory.h"

#if !defined(TARGET_IA32) && !defined(TARGET_IA32E)
#error Unknown target architecture. Must be IA-32 or Intel64.
#endif

/*!
 * Convert a pointer to <SRC> type into a pointer to <DST> type.
 * Allows any combination of data/function types.
 */
template< typename DST, typename SRC > DST* CastPtr(SRC* src)
{
    union CAST
    {
        DST* dstPtr;
        SRC* srcPtr;
    } cast;
    cast.srcPtr = src;
    return cast.dstPtr;
}

/*!
 * Type of a position-independent function that copy its name into the specified buffer.
 */
typedef void FOO_BAR_T(char*);
typedef FOO_BAR_T* FOO_BAR_PTR;

/*!
 * Implementation of the PI_FUNC for FOO_BAR_T functions.
 */
class FOO_BAR_FUNC : public PI_FUNC
{
  public:
    FOO_BAR_FUNC(FOO_BAR_PTR func, size_t size) : m_func(func), m_size(size), m_throwException(false), m_status(STATUS_OK)
    {
        m_func(m_name);
    }

    // Get/set the exception mode for the Execute() function:
    // TRUE  - execute the function with arguments that cause an exception in the function
    // FALSE - execute the function without exceptions
    bool GetExceptionMode() const { return m_throwException; }
    void SetExceptionMode(bool throwException) { m_throwException = throwException; }

    FUNC_OBJ& Execute()
    {
        if (m_throwException)
        {
            ExecuteThrow();
        }
        else
        {
            ExecuteNoThrow();
        }
        return *this;
    }

    PI_FUNC& Copy(void* buffer)
    {
        memcpy(buffer, CastPtr< void >(m_func), m_size);
        m_func = CastPtr< FOO_BAR_T >(buffer);
        return *this;
    }

    std::string ErrorMessage() const
    {
        switch (m_status)
        {
            case STATUS_OK:
                return "Success";
            case STATUS_HANDLED_EXCEPTION:
                return "Exception handled successfully";
            case STATUS_UNEXPECTED_RESULT:
                return "Unexpected result";
            case STATUS_UNEXPECTED_EXCEPTION:
                return "Unexpected exception";
            default:
                return "Unknown status";
        }
    }

    bool Status() const { return ((m_status == STATUS_OK) || (m_status == STATUS_HANDLED_EXCEPTION)); }
    std::string Name() const { return m_name; }
    void* Start() const { return CastPtr< void >(m_func); }
    size_t Size() const { return m_size; }
    FUNC_OBJ* Clone() const { return new FOO_BAR_FUNC(*this); }

  protected:
    FUNC_OBJ& HandleException(void* exceptIp)
    {
        char* start = CastPtr< char >(m_func);
        char* ip    = CastPtr< char >(exceptIp);
        if (m_throwException && (ip >= start) && (ip < start + m_size))
        {
            ExecuteNoThrow();
            if (m_status == STATUS_OK)
            {
                m_status = STATUS_HANDLED_EXCEPTION;
            }
        }
        else
        {
            m_status = STATUS_UNEXPECTED_EXCEPTION;
        }
        return *this;
    }

  private:
    FOO_BAR_PTR m_func;
    size_t m_size;
    bool m_throwException;
    enum STATUS
    {
        STATUS_OK,
        STATUS_HANDLED_EXCEPTION,
        STATUS_UNEXPECTED_RESULT,
        STATUS_UNEXPECTED_EXCEPTION
    } m_status;
    char m_name[16];

    void ExecuteThrow()
    {
        m_func(0);
        m_status = STATUS_UNEXPECTED_RESULT; // should never get here
    }

    void ExecuteNoThrow()
    {
        char result[16];
        m_func(result);
        m_status = ((strcmp(result, m_name) == 0) ? STATUS_OK : STATUS_UNEXPECTED_RESULT);
    }
};

class FOO_FUNC : public FOO_BAR_FUNC
{
  public:
    FOO_FUNC();
};

class BAR_FUNC : public FOO_BAR_FUNC
{
  public:
    BAR_FUNC();
};

/*!
 * Shared test state for SMC barrier test applications.
 * Tracks pass/fail counts and provides C++-style CheckAndPrint overloads
 * that write standardised PASS/FAIL lines to stderr.
 */
struct SmcTestState
{
    int failures = 0;

    /*!
     * Compare 'expected' against 'got' and print a standardised PASS/FAIL line.
     * Works for any equality-comparable, printable T.
     */
    template< typename T > void CheckAndPrint(const char* barrier, int do_smc, int iter, int loops, T expected, T got)
    {
        const bool pass = (got == expected);
        if (!pass)
        {
            ++failures;
        }
        const int w = loops >= 100 ? 3 : loops >= 10 ? 2 : 1;
        std::cerr << '[' << barrier << "][SMC=" << do_smc << "][" << std::setw(w) << (iter + 1) << '/' << loops << "] "
                  << "expected=" << std::left << std::setw(3) << expected << ' ' << "got=" << std::setw(3) << got << std::right
                  << " -> " << (pass ? "PASS" : "FAIL ***") << '\n';
    }

    /*!
     * Overload for foo/bar position-independent function tests: calls buf as
     * FOO_BAR_PTR to capture the result string, then delegates to the template above.
     */
    void CheckAndPrint(const char* barrier, int do_smc, int iter, int loops, const char* expected, void* buf)
    {
        char got[16] = {};
        ((FOO_BAR_PTR)buf)(got);
        CheckAndPrint(barrier, do_smc, iter, loops, std::string(expected), std::string(got));
    }

    /*! Print the final "Result: X/Y PASS, Z FAIL" summary line to stderr. */
    void PrintResult(int total) const
    {
        std::cerr << "\nResult: " << (total - failures) << '/' << total << " PASS, " << failures << " FAIL\n";
    }
};

/*!
 * Run the standard foo/bar SMC barrier test loop.
 *
 * @param barrierName  human-readable barrier name printed in output lines.
 * @param barrier      zero-argument callable that executes the barrier instruction.
 * @param do_smc       1 = write new code before every call (SMC mode);
 *                     0 = write once, protect RX, repeat (baseline mode).
 * @param loop_count   number of iterations.
 * @return 0 on full success, 1 if any check failed.
 */
template< typename BarrierFn > int RunBarrierTest(const char* barrierName, BarrierFn barrier, int do_smc, int loop_count)
{
    std::cerr << "SMC barrier test: " << barrierName << "  do_smc=" << do_smc << "  loops=" << loop_count << '\n';

    void* buf = MemAlloc(PI_FUNC::MAX_SIZE, MEM_READ_WRITE_EXEC);
    if (!buf)
    {
        std::cerr << "FATAL: MemAlloc failed\n";
        return 1;
    }

    SmcTestState state;
    const clock_t t0 = clock();

    if (!do_smc)
    {
        FOO_FUNC fooFunc;
        fooFunc.Copy(buf);
        if (!MemProtect(buf, PI_FUNC::MAX_SIZE, MEM_READ_EXEC))
        {
            std::cerr << "FATAL: MemProtect(RX) failed\n";
            MemFree(buf, PI_FUNC::MAX_SIZE);
            return 1;
        }
        for (int i = 0; i < loop_count; ++i)
        {
            barrier();
            state.CheckAndPrint(barrierName, do_smc, i, loop_count, "foo", buf);
        }
    }
    else
    {
        for (int i = 0; i < loop_count; ++i)
        {
            {
                FOO_FUNC f;
                f.Copy(buf);
            }
            barrier();
            state.CheckAndPrint(barrierName, do_smc, i, loop_count, "foo", buf);

            {
                BAR_FUNC b;
                b.Copy(buf);
            }
            barrier();
            state.CheckAndPrint(barrierName, do_smc, i, loop_count, "bar", buf);
        }
    }

    const clock_t t1 = clock();
    MemFree(buf, PI_FUNC::MAX_SIZE);

    const int total = loop_count * (do_smc ? 2 : 1);
    std::cerr << "elapsed: " << std::fixed << std::setprecision(3) << (1000.0 * (double)(t1 - t0) / CLOCKS_PER_SEC) << " ms\n";
    state.PrintResult(total);
    return (state.failures > 0) ? 1 : 0;
}

#endif //SMC_UTIL_H

/* ===================================================================== */
/* eof */
/* ===================================================================== */
