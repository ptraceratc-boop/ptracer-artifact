/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <unistd.h>
#include "ipind_plugin.h"
#include "rpc_schema.h"

/*
 * This plugin supports one rpc id - the one that is defined in rpc_schema.h.
 * This rpc returns the pid as a return value.
 */

E_plugin_type get_plugin_type(IPindPlugin* self) { return RPC; }

t_rpc_message_schema const* get_rpc_schema(IPindPlugin* self, t_rpc_id rpcId)
{
    if (rpcId == TEST_SCHEMA.rpcId)
    {
        return &TEST_SCHEMA;
    }
    return nullptr;
}

void do_rpc(IPindPlugin* self, t_rpc_id rpcId, t_arg_count argCount, t_rpc_arg* rpcArgs, t_rpc_ret* retRpcArg)
{
    if (rpcId == TEST_SCHEMA.rpcId)
    {
        // The return value is the pid
        retRpcArg->argSchema   = TEST_SCHEMA.returnValueSchema;
        retRpcArg->argDataSize = ARG_SCHEMA_SIZE(TEST_SCHEMA.returnValueSchema);
        retRpcArg->argData     = (uint64_t)getpid();
        retRpcArg->flags       = RpcArgFlagsNone;
    }
}

IRPCPlugin pluginFuncs {0};

extern "C" PLUGIN__DLLVIS IPindPlugin* load_plugin(const char* name)
{
    pluginFuncs.base_.get_plugin_type = get_plugin_type;
    pluginFuncs.get_rpc_schema        = get_rpc_schema;
    pluginFuncs.do_rpc                = do_rpc;

    return reinterpret_cast< IPindPlugin* >(&pluginFuncs);
}

extern "C" void PLUGIN__DLLVIS unload_plugin(IPindPlugin*) {}
