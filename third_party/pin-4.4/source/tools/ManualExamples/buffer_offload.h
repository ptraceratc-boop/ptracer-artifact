/*
 * Copyright (C) 2024-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _BUFFER_OFFLOAD_H_
#define _BUFFER_OFFLOAD_H_

#include <rscschema.h>

/*
 * This struct defines the structure of the record that Pin will write to the trace buffer.
 */
struct MEMREF
{
    uint8_t* ea;   // effective address
    uint32_t size; // number of bytes accessed
};

/*
 * These types define the structure and schema of the RPC messages the pintool uses
 * to pass information to the remote plugin.
 * Each remote procedure should have its own unique rpc id.
 */
constexpr t_rpc_id RPCID_OPEN_OUT_FILE       = RPCID_MIN + 42;
constexpr t_rpc_id RPCID_MEM_ANALYZE         = RPCID_MIN + 43;
constexpr t_rpc_id RPCID_GET_MEM_ACCESS_INFO = RPCID_MIN + 44;

constexpr auto OPEN_OUT_FILE_SCHEMA =
    pinrt::rscschema::RPC_message_schema_wrapper< RPCID_OPEN_OUT_FILE,              // RPCID
                                                  pinrt::rscschema::Bool_schema_v,  // [out] Ret bool - true on success
                                                  pinrt::rscschema::Buffer_schema_v // [in]  Output File name
                                                  >::schema;

constexpr auto MEM_ANALYZE_SCHEMA =
    pinrt::rscschema::RPC_message_schema_wrapper< RPCID_MEM_ANALYZE,                //RPCID
                                                  pinrt::rscschema::Void_schema_v,  // Ret - void
                                                  pinrt::rscschema::Buffer_schema_v // [in] Offload Buffer
                                                  >::schema;

constexpr auto GET_MEM_ACCESS_INFO_SCHEMA = pinrt::rscschema::RPC_message_schema_wrapper<
    RPCID_GET_MEM_ACCESS_INFO,                    // RPCID
    pinrt::rscschema::Bool_schema_v,              // Ret - bool - true on success, false if no accesses were recorded
    pinrt::rscschema::Uint_schema_v< uintptr_t >, // [out] Lowest memory accessed
    pinrt::rscschema::Uint_schema_v< uintptr_t >, // [out] Highest memory accessed
    pinrt::rscschema::Uint_schema_v< unsigned >,  // [in, out] Number of top MEMRANGE structures returned in topRanges,
                                                  // upto the number passed as input
    pinrt::rscschema::Buffer_schema_v             // [out] topRanges
    >::schema;

#endif // _BUFFER_OFFLOAD_H_
