/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _PINGLUE_COMMON_H
#define _PINGLUE_COMMON_H

#include <cstring>
#include <string>
#include <random>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>
#include <cassert>

#include <txll_utils.h>
#include <rscproto.h>

namespace glue
{
constexpr unsigned long long operator"" _KB(unsigned long long n) { return n * 1024ULL; }

/** @brief The default txll channel size for all channels*/
constexpr uint32_t CHANNEL_SIZE                 = 64_KB;

/** @brief The default buffer size for all buffers used to send messages on the txll channels*/
constexpr uint32_t BUFFER_SIZE                  = CHANNEL_SIZE;

/** @brief RPC IDs of pin glue messages */
constexpr t_rpc_id SERVICE_REQUEST_RPC_ID       = 0x53455256;
constexpr t_rpc_id SERVICE_RESULT_RPC_ID        = 0xCB000004;
constexpr t_rpc_id START_PROGRAM_RPC_ID         = 0x53545254;
constexpr t_rpc_id REMOTE_CALL_RPC_ID           = 0xCB000006;

/** @brief supported RPC argument schemas for pin glue messages args*/
constexpr t_rpc_arg_schema BUFFER_SCHEMA = BUFFER_ARG_SCHEMA();
constexpr t_rpc_arg_schema INT_SCHEMA    = INT_ARG_SCHEMA(int64_t);
constexpr t_rpc_arg_schema FLOAT_SCHEMA  = FLOAT_ARG_SCHEMA(double); // We treat float always as double
constexpr t_rpc_arg_schema BOOL_SCHEMA   = BOOL_ARG_SCHEMA();
constexpr t_rpc_arg_schema NIL_SCHEMA    = NIL_ARG_SCHEMA();

/** @brief Service request argument schema */
t_rpc_arg_schema serviceArgsSchema[] = {
    UINT_ARG_SCHEMA(uint64_t), // uint64_t callback_address
    BUFFER_ARG_SCHEMA(),       // char name[6]
    BUFFER_ARG_SCHEMA(),       // char* service_name
    BUFFER_ARG_SCHEMA(),       // char* callback_name
    ARRAY_ARG_SCHEMA(),        // char** knobs
    BUFFER_ARG_SCHEMA(),       // Describes the previous array members type
    ARRAY_ARG_SCHEMA(),        // char** scripts
    BUFFER_ARG_SCHEMA()        // Describes the previous array members type
};

/** @brief Service request message schema */
constexpr t_rpc_message_schema Service_Request_Schema = {SERVICE_REQUEST_RPC_ID, 6, serviceArgsSchema, BUFFER_ARG_SCHEMA()};

/** @brief Service program message schema */
constexpr t_rpc_message_schema Start_Program_Request_Schema = {START_PROGRAM_RPC_ID, 0, nullptr, BOOL_ARG_SCHEMA()};

/** @brief Service result argument schema */
t_rpc_arg_schema serviceResultArgsSchema[] = {
    BUFFER_ARG_SCHEMA(), // The name of the remote function
    BUFFER_ARG_SCHEMA(), // A buffer containing a JSON
    BUFFER_ARG_SCHEMA()  // 5 Alpha Numeric characters named for this service
};

/** @brief Service result message schema */
constexpr t_rpc_message_schema Service_Result_Schema = {SERVICE_RESULT_RPC_ID, 3, serviceResultArgsSchema, BOOL_ARG_SCHEMA()};

/** @brief Remote Callback message is decoded without schema (since we support variadic arguments of different types)
           the definition here is only for passing the schema to rscproto_helper that expect to get REMOTE_CALL_RPC_ID from it */
constexpr t_rpc_message_schema Remote_Call_Schema = {REMOTE_CALL_RPC_ID, 0, 0, {}};

/**
 * @brief A wrapper function to tx_wait_data to continue waiting (up to timeout) 
 *         in case tx_wait_data returned EAGAIN (message not ready yet).          
 * 
 *  @param[in] tx_wait_data params.
 * 
 * @return t_syscall_ret - tx_wait_data return values.
 *          In case of EAGAIN it will return ETIMEDOUT.
 */
inline t_syscall_ret pinglue_tx_read(void* chan, uint32_t timeout, uint8_t* p, uint32_t size, uint64_t& senderid)
{
    t_syscall_ret ret;
    int64_t remainingTime = static_cast< int64_t >(timeout);
    auto start            = std::chrono::steady_clock::now();

    while (0 <= remainingTime)
    {
        auto ret = tx_wait_data(chan, p, size, senderid, timeout);
        if (SYSRET(EAGAIN) != ret)
        {
            return ret;
        }

        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast< std::chrono::microseconds >(now - start).count();
        start        = now;
        remainingTime -= elapsed;
        timeout -= elapsed;
    }

    if (SYSRET(EAGAIN) == ret)
    {
        return SYSRET(ETIMEDOUT);
    }

    return ret;
}

/**
 * @brief Generate a new 256 random bits for seed.
 * 
 *  @param[out] seedValues an array of 4 uint64_t to store the generated seed.
 * 
 * @return void
 */
template< size_t N > inline void generate_random_seed(uint64_t (&seedValues)[N])
{
    static_assert(4 == N, "seedValues must be a uint64_t array of length 4");

    std::random_device rd;

    // Populate seedValues with results of two calls to rd() each
    for (int i = 0; i < 4; ++i)
    {
        seedValues[i] = static_cast< uint64_t >(rd()) | (static_cast< uint64_t >(rd()) << 32);
    }
}

/**
 * @brief Convert seed to string that contains 64 hex characters that represents the 256 bits of the seed.
 * 
 *  @param[in] seedValues seed values (array of 4 uint64_t).
 * 
 * @return std::string  - a string contains 64 hex characters.
 */
template< size_t N > inline std::string seed_to_string(const uint64_t (&seedValues)[N])
{
    static_assert(4 == N, "seedValues must be a uint64_t array of length 4");

    std::ostringstream oss;

    // Output each uint64_t value with a width of 16 characters
    for (int i = 0; i < 4; ++i)
    {
        oss << std::hex << std::setw(16) << std::setfill('0') << seedValues[i];
    }

    return oss.str();
}

/**
 * @brief Convert string of 64 hex characters to seed (array of 4 uint64_t).
 * 
 *  @param[in] seedStr      a string contains 64 hex characters.
 *  @param[out] seedValues  the output array contains 4 uint64_t that represents the seed.
 * 
 * @return void.
 */
template< size_t N > inline void string_to_seed(const std::string& seedStr, uint64_t (&seedValues)[N])
{
    assert(64 == seedStr.size());
    static_assert(4 == N, "seedValues must be a uint64_t array of length 4");

    std::istringstream iss(seedStr);

    // Extract 16-character chunks from the stringstream and convert to uint64_t
    for (int i = 0; i < 4; ++i)
    {
        std::string chunk;
        iss >> std::setw(16) >> chunk;

        // Convert the hex string to uint64_t
        seedValues[i] = std::stoull(chunk, nullptr, 16);
    }
}

} // namespace glue

#endif // _GLUE_COMMON_H
