/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _RPC_SCHEMA_H_
#define _RPC_SCHEMA_H_

#include <rscschema.h>

constexpr t_rpc_id TEST_RPC_ID = 3000;

constexpr auto TEST_SCHEMA =
    pinrt::rscschema::RPC_message_schema_wrapper< TEST_RPC_ID, pinrt::rscschema::Uint_schema_v< uint64_t >,
                                                  pinrt::rscschema::Uint_schema_v< uint32_t > >::schema;

#endif // _RPC_SCHEMA_H_
