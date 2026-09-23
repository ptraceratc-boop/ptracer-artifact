/*
 * Copyright (C) 2020-2026 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifdef PIN_G_CHILD_PROCESS_CLIENT_PH
#error duplicate inclusion of child_process_client
#else
#define PIN_G_CHILD_PROCESS_CLIENT_PH
/*! @file
 *  Pin client functions for the follow child process mechanism
 */
/*! @ingroup PIN_PROCESS
 * CHILD_PROCESS - Handle for child process APIs
 */
typedef void* CHILD_PROCESS;

/*! @ingroup PIN_PROCESS
 *  Call back function before child(Windows*)/exec-ed(Linux*) process is starting to execute.
 *  The function can be registered by @ref PIN_AddFollowChildProcessFunction().
 *
 *  @param[in] childProcess   Child process handle, its lifetime is in the scope of the
 *                             callback
 *  @param[in] val            The tool's call-back value
 *
 *  @return TRUE   If user is interested to inject Pin (and tool) into
 *                  child/exec-ed process\n
 *          FALSE  If user is not interested to inject Pin (and tool) into
 *                  child/exec-ed process\n
 */
typedef BOOL (*FOLLOW_CHILD_PROCESS_CALLBACK)(CHILD_PROCESS cProcess, VOID* val);

/*! @ingroup PIN_CONTROL
 * Register a notification function that is called before a child/exec-ed process
 * is starting to execute
 *
 * NOTE: Only one callback can be registered
 *
 * @param[in] fun    Call-back function
 * @param[in] val    Value to pass to the function
 *
 * @return PIN_CALLBACK A handle to a callback that can be used to further modify this callback's properties
 *
 * @note The pin client lock is obtained during the call of this API.
 *
 * @par Availability:
 * \b Mode:  JIT & Probe\n
 * \b O/S:   Windows, Linux\n
 * \b CPU:   All\n
 */
extern PIN_CALLBACK PIN_AddFollowChildProcessFunction(FOLLOW_CHILD_PROCESS_CALLBACK fun, VOID* val);

/*! @ingroup PIN_PROCESS
 * Get process id of child process (meaningful only for Windows*)
 *
 * @param[in] childProcess     child process handle
 *
 * @return  process id of child process
 *
 * @par Availability:
 * \b Mode:  JIT & Probe\n
 * \b O/S:   Windows, Linux\n
 * \b CPU:   All\n
 */
extern OS_PROCESS_ID CHILD_PROCESS_GetId(CHILD_PROCESS cProcess);

/*! @ingroup PIN_PROCESS
 * Get application's original command line (without Pin's command line)
 *
 * @param[in]  childProcess  child process handle
 * @param[out] pArgc         pointer to application's argc
 * @param[out] pArgv         pointer to application's argv
 *                            this data is allocated by Pin and can't be modified by tool,
 *                            its lifetime is in the scope of the callback
 *
 * @par Availability:
 * \b Mode:  JIT & Probe\n
 * \b O/S:   Windows, Linux\n
 * \b CPU:   All\n
 */
extern VOID CHILD_PROCESS_GetCommandLine(CHILD_PROCESS cProcess, INT* pArgc, const CHAR* const** pArgv);

/*! @ingroup PIN_PROCESS
 * Set Pin's (and tool's) command line for the child process
 * (e.g. "c:\\pin_path\\pin -probe -follow_execv -t c:\\pintool_path\\tool --")
 *
 * If this API is not called, Pin will be injected into child/exec-ed process
 * with the same command line as of current process.
 *
 * NOTE:
 * - Providing full path for Pin and tool is the safest way to
 *    ensure they will be found (e.g. changing execution directory by application)
 * - Use same Pin version for all process tree
 *
 * @param[in] childProcess   handle to child process
 * @param[in] argc           Pin's (and tool's) argc
 * @param[in] argv           Pin's (and tool's) argv,
 *                            this data is allocated by tool and can't be modified by Pin,
 *                            its lifetime is in the scope of this function
 *
 * @par Availability:
 * \b Mode:  JIT & Probe\n
 * \b O/S:   Windows, Linux\n
 * \b CPU:   All\n
 */
extern VOID CHILD_PROCESS_SetPinCommandLine(CHILD_PROCESS cProcess, INT argc, const CHAR* const* argv);

/*! @ingroup PIN_PROCESS
 * Get Pin's (and tool's) command line for the child process.
 * The first element (argv[0]) is the Pin executable path.
 *
 * This is useful for tools that need to read the current Pin command line,
 * modify it (e.g. append tool-specific arguments), and set it back via
 * @ref CHILD_PROCESS_SetPinCommandLine().
 *
 * @param[in]  childProcess  child process handle
 * @param[out] pArgc         pointer to Pin's argc
 * @param[out] pArgv         pointer to Pin's argv
 *                            this data is allocated by Pin and can't be modified by tool,
 *                            its lifetime is in the scope of the callback.
 *                            The data is invalidated if @ref CHILD_PROCESS_SetPinCommandLine()
 *                            is called after this call.
 *
 * @par Availability:
 * \b Mode:  JIT & Probe\n
 * \b O/S:   Windows, Linux\n
 * \b CPU:   All\n
 */
extern VOID CHILD_PROCESS_GetPinCommandLine(CHILD_PROCESS cProcess, INT* pArgc, const CHAR* const** pArgv);

#endif // PIN_G_CHILD_PROCESS_CLIENT_PH
