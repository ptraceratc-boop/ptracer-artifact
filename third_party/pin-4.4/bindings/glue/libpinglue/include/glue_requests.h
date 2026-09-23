/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_REQUESTS_H
#define _GLUE_REQUESTS_H

#include <rscproto.h>

struct Channel;
typedef struct Channel t_pg_channel;

// Function name is up to length of 79 chars.
// Although Python func name length is unlimited, however lines longer than 79 characters violate pep style guides.
const size_t MAX_FUNC_NAME = 79;

/**
 * @brief A structure holding service result data.
 * 
 */
typedef struct Pinglue_service_result
{
    /** @brief The name of the remote function */
    const char* funcName;

    /** @brief A buffer containing a JSON string with the result */
    const char* result;

    /** @brief The length of result */
    size_t resultLength;

    /** @brief 5 Alpha Numeric characters named for this service */
    const char* name;
} t_pg_service_result;

/**
 * @brief A structure holding remote request data.
 * 
 */
typedef struct Pinglue_remote_request
{
    /** @brief An array of arguments depending on the callback */
    const t_rpc_arg* args;

    /** @brief The number of args */
    size_t argsSize;
} t_pg_remote_request;

/**
 * @brief A structure holding remote function data.
 * 
 */
typedef struct Pinglue_remote_func
{
    /** @brief request */
    t_pg_remote_request request;

    /** @brief A string contains the remote function name */
    const char* functionName;

    /** @brief 5 Alpha Numeric characters named in request */
    const char* name;
} t_pg_remote_func;

/**
 * @brief A structure holding remote error request data.
 * 
 */
typedef struct Pinglue_remote_error
{
    /** @brief request */
    t_pg_remote_request request;

    /** @brief error ID */
    uint16_t errorId;

    /** @brief A string contains the error message */
    const char* errorMessage;
} t_pg_remote_error;

/** @brief a function type for delete an rpc arg */
typedef void (*t_pg_rpc_arg_deleter)(t_rpc_arg*, void*);

/**
 * @brief A structure holding remote function response data.
 * 
 */
typedef struct Pinglue_remote_response
{
    /** @brief The return value.
     *  Note that in case of string (RpcBuffer), the pointer to the string 
     *  should be valid after the function return (global or on the heap).
    */
    t_rpc_arg ret;

    /** @brief A deleter to be called on ret after sending the response, 
     *          or NULL if no deleter (like in case ret is a primitive type)
     *         NOTE!! it must be set to NULL if it's not set to a valid deleter.
    */
    t_pg_rpc_arg_deleter deleter;

    /** @brief A state to pass to deleter if necessary */
    void* state;

    /** @brief true if request handled, false to send reject response to the server */
    bool handled;

    /** @brief true to send an error massage to server to cancel the service, false otherwise */
    bool cancel;
} t_pg_remote_response;

#endif // _GLUE_REQUESTS_H
