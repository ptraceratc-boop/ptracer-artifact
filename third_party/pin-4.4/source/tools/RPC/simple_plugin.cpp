/*
 * Copyright (C) 2025-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <cassert>
#include <cstring>
#include <fstream>
#include "ipind_plugin.h"
#include <iostream>

/*
 * This is a simple plugin that does nothing but prints it is loaded and unloaded.
 */

namespace
{
static const char* SIMPLE_PLUGIN_NAME_1 = "simple plugin 1";

static const char* SIMPLE_PLUGIN_NAME_2 = "simple plugin 2";

static const char* SIMPLE_PLUGIN_NAME_3 = "simple plugin 3";

struct simple_plugin
{
    /**
         * @brief The RPC plugin function pointers
         * 
         */
    IRPCPlugin rpcPlugin;

    /**
         * @brief The name of the plugin.
         * 
         */
    const char* pluginName;
};

/*
    * This function is called by the server to query whether this plugin supports the input rpcId.
    * If it does then the function should return a pointer to the appropriate message schema.
    * If it doesn't then the function should return nullptr;
    */
t_rpc_message_schema const* get_rpc_schema(IPindPlugin* self, t_rpc_id rpcId) { return nullptr; }

/*
    * This function is called by the server only if get_rpc_schema(rpcId) returned a non-null schema.
    */
void do_rpc(IPindPlugin* self, t_rpc_id rpcId, t_arg_count argCount, t_rpc_arg* rpcArgs, t_rpc_ret* retRpcArg) {}

/* 
    * This function should always return RPC.
    */
E_plugin_type get_plugin_type(IPindPlugin* self) { return RPC; }

bool init(IPindPlugin* self, int argc, const char* const argv[]) { return true; }

void uninit(IPindPlugin* self)
{
    auto this_          = reinterpret_cast< simple_plugin* >(self);
    std::string message = std::string(this_->pluginName) + " is unloaded!\n";
    std::cout << message << std::endl;
}
} // namespace

/*
 * Load the plugin.
 * The plugin must fill in all the entries of the struct with valid function pointers.
 * The implementation can be empty but it must be a valid function.
 */
PLUGIN_EXTERNC PLUGIN__DLLVIS struct IPindPlugin* load_plugin(const char* name)
{
    auto simple1used = false;
    auto simple2used = false;
    auto simple3used = false;

    if (0 == strncmp(SIMPLE_PLUGIN_NAME_1, name, strlen(SIMPLE_PLUGIN_NAME_1)))
    {
        simple1used = true;
    }
    else if (0 == strncmp(SIMPLE_PLUGIN_NAME_2, name, strlen(SIMPLE_PLUGIN_NAME_2)))
    {
        simple2used = true;
    }
    else if (0 == strncmp(SIMPLE_PLUGIN_NAME_3, name, strlen(SIMPLE_PLUGIN_NAME_3)))
    {
        simple3used = true;
    }

    if (simple1used || simple2used || simple3used)
    {
        simple_plugin* plugin = new (std::nothrow) simple_plugin;

        if (nullptr != plugin)
        {
            IRPCPlugin* rpcPlugin = &plugin->rpcPlugin;
            memset(rpcPlugin, 0, sizeof(IRPCPlugin));
            rpcPlugin->base_.get_plugin_type = get_plugin_type;
            rpcPlugin->base_.init            = init;
            rpcPlugin->base_.uninit          = uninit;
            rpcPlugin->get_rpc_schema        = get_rpc_schema;
            rpcPlugin->do_rpc                = do_rpc;

            if (simple1used)
            {
                plugin->pluginName = SIMPLE_PLUGIN_NAME_1;
            }
            else if (simple2used)
            {
                plugin->pluginName = SIMPLE_PLUGIN_NAME_2;
            }
            else
            {
                plugin->pluginName = SIMPLE_PLUGIN_NAME_3;
            }

            return (IPindPlugin*)rpcPlugin;
        }
    }

    return nullptr; // We don't know the requested plugin or couldn't allocate it
}

/*
 * Unload the plugin - create the report and write it to a file.
 */
PLUGIN_EXTERNC PLUGIN__DLLVIS void unload_plugin(struct IPindPlugin* plugin) { delete plugin; }
