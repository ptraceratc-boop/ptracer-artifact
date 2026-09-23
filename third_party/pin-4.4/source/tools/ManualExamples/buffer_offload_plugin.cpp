/*
 * Copyright (C) 2024-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <cassert>
#include <cstring>
#include <fstream>
#include <utility>
#include <set>
#include <map>
#include <iomanip>
#include <mutex>
#include <iostream>
#include <string>
#if __has_include(<filesystem>)
#include <filesystem>
#endif
#if defined(TARGET_WINDOWS)
#include <process.h>
#define getpid() _getpid()
#else
#include <unistd.h>
#endif
#include "ipind_plugin.h"
#include "buffer_offload.h"

/**
 * @brief Represents a buffer offload plugins
 * 
 */
struct Buffer_offload_plugin
{
    static constexpr int MAX_TOP_RANGES = 3;
    /**
     * @brief The RPC plugin function pointers
     * 
     */
    IRPCPlugin rpcPlugin;

    /**
     * @brief Holds the internal state of the plugin
     * 
     */
    struct Buffer_offload_plugin_state
    {
        /**
         * @brief Output file opened in response to RPCID_OPEN_OUT_FILE
         * 
         */
        std::ofstream outfile;
        /** @brief A set containing memory access requests
         * 
         * Access entries are added in response to RPCID_MEM_ANALYZE
         * 
         * Each pair represents a <low_address, high_address> memory region that has been 
         * accessed by the application.
         */
        std::set< std::pair< uint8_t*, uint8_t* > > accesses;

        /**
         * @brief Add a memory region access to the accesses map
         * 
         * Aggregate each memory reference into a structure that represents a map of contiguous memory ranges.
         * For example if a memory reference accessed 4 bytes in address 1000 then we will insert range (1000,1003).
         * Then if an additional memory reference accessed 2 bytes in address 1004 then we will merge (1000,1003) 
         * with the new range (1004,1005) into one contiguous range (1000,1005).
         * Eventually the set will represent the fragmentation of the memory as far as read/write.
         * 
         * @param[in] low    The start of the memory access 
         * @param[in] high   The end of the memory access 
         */
        void add_access(uint8_t* low, uint8_t* high)
        {
            static std::mutex mtx;

            // We need to use a lock to protect writes to 'accesses' since do_rpc() may be called from
            // multiple threads and isn't thread safe.
            mtx.lock();

            auto range = std::make_pair(low, high);
            auto it_ge = accesses.lower_bound(range);
            auto it_lt = (accesses.begin() != it_ge) ? std::prev(it_ge) : accesses.end();

            if ((accesses.end() != it_ge) && (range.second + 1 >= it_ge->first))
            {
                range = std::make_pair(range.first, it_ge->second);
                accesses.erase(it_ge);
            }
            if ((accesses.end() != it_lt) && (it_lt->second + 1 >= range.first))
            {
                range = std::make_pair(it_lt->first, range.second);
                accesses.erase(it_lt);
            }

            accesses.insert(range);

            mtx.unlock();
        }
    };

    /**
     * @brief internal state of the plugin. 
     * 
     * We hold this as a pointer and not hold the
     * state members directly so to keep Buffer_offload_plugin standard layout so it's
     * pointer may be safely casted to IRPCPlugin* nad vice-versa
     * 
     */
    Buffer_offload_plugin_state* state_;

    /**
     * @brief Implement RPCID_OPEN_OUT_FILE
     * 
     * @param[in]       argCount  The number of RPC arguments
     * @param[in, out]  rpcArgs   Array of RPC arguments.
     *                            For this RPC we expect just a single argument which is a buffer holding
     *                            the output file name as null terminated std::string. 
     * @param retRpcArg           The return value of the RPC. We return true on success.
     */
    void do_open_outfile(t_arg_count argCount, t_rpc_arg* rpcArgs, t_rpc_ret* retRpcArg)
    {
        std::string outfilePath = (char*)rpcArgs[0].argData;
        state_->outfile.open(outfilePath);

        std::cout << "Log Open Requested for: " << outfilePath << std::endl;

        rpcArgs[0].flags = RpcArgFlagsDataEmpty; // This is an IN argument. Mark no data when returning

        // Fill in the return value of this operation, in this case a boolean
        // that is set to true if the file opened successfully, false otherwise.
        retRpcArg->argSchema   = OPEN_OUT_FILE_SCHEMA.returnValueSchema;
        retRpcArg->argDataSize = ARG_SCHEMA_SIZE(OPEN_OUT_FILE_SCHEMA.returnValueSchema);
        retRpcArg->argData     = (uint64_t)(state_->outfile.is_open() && state_->outfile.good());
        retRpcArg->flags       = RpcArgFlagsNone;
    }

    /**
     * @brief Implement RPCID_MEM_ANALYZE
     * 
     * Iterate on the structs in the buffer and process them.
     * 
     * @param[in]       argCount  The number of RPC arguments
     * @param[in, out]  rpcArgs   Array of RPC arguments.
     *                            For this RPC we expect just a single argument which is a buffer holding
     *                            an array of MEMREF structures
     * @param retRpcArg           The return value of the RPC
     */
    void do_analyze_mem_access(t_arg_count argCount, t_rpc_arg* rpcArgs, t_rpc_ret* retRpcArg)
    {
        MEMREF* refs = reinterpret_cast< MEMREF* >(rpcArgs[0].argData);
        auto count   = rpcArgs[0].argDataSize / sizeof(MEMREF);

        for (auto i = 0; i < count; i++)
        {
            auto low  = refs[i].ea;
            auto high = refs[i].ea + refs[i].size - 1;
            state_->add_access(low, high);
        }

        rpcArgs[0].flags = RpcArgFlagsDataEmpty; // This is an IN argument. Mark no data when returning

        // Fill in the return value of this operation, in this case it is a Nil
        // which means there is no return value. We should still fill in this
        // struct so that the server will know what kind of return value to encode
        // in the response.
        retRpcArg->argSchema   = MEM_ANALYZE_SCHEMA.returnValueSchema;
        retRpcArg->argDataSize = ARG_SCHEMA_SIZE(MEM_ANALYZE_SCHEMA.returnValueSchema);
        retRpcArg->argData     = 0;
        retRpcArg->flags       = RpcArgFlagsNone;
    }

    /**
     * @brief Implement RPCID_GET_MEM_ACCESS_INFO
     * 
     * @param[in]       argCount  The number of RPC arguments
     * @param[in, out]  rpcArgs   Array of RPC arguments.
     *                            For this RPC we expect to have two output arguments receiving the
     *                            lowest & highest memory addresses accessed by the program.
     * @param retRpcArg           The return value of the RPC
     */
    void do_get_mem_access_info(t_arg_count argCount, t_rpc_arg* rpcArgs, t_rpc_ret* retRpcArg)
    {
        // Fill in the [out] args
        retRpcArg->argData = 0; // Start with setting the ret to false (failure)
        if (!state_->accesses.empty())
        {
            // We get the maximum ranges we can place in the out buffer from the client
            auto maxClientTopRangeCount = (int)rpcArgs[2].argData;

            // For this RPC we treat  arguments as OUT arguments so
            // we explicitely make sure all fields schemas and size fields are properly
            // initialized.
            rpcArgs[0].argData     = uintptr_t(state_->accesses.begin()->first);
            rpcArgs[0].argSchema   = GET_MEM_ACCESS_INFO_SCHEMA.argSchemaArray[0];
            rpcArgs[0].argDataSize = ARG_SCHEMA_SIZE(GET_MEM_ACCESS_INFO_SCHEMA.argSchemaArray[0]);
            rpcArgs[0].flags       = RpcArgFlagsNone;
            rpcArgs[1].argData     = uintptr_t(state_->accesses.rbegin()->second);
            rpcArgs[1].argSchema   = GET_MEM_ACCESS_INFO_SCHEMA.argSchemaArray[1];
            rpcArgs[1].argDataSize = ARG_SCHEMA_SIZE(GET_MEM_ACCESS_INFO_SCHEMA.argSchemaArray[1]);
            rpcArgs[1].flags       = RpcArgFlagsNone;

            auto maxTopRangesCount = maxClientTopRangeCount < MAX_TOP_RANGES ? maxClientTopRangeCount : MAX_TOP_RANGES;
            rpcArgs[2].argData     = maxTopRangesCount;

            // Allocate memory for the top ranges
            size_t bufferSize = maxTopRangesCount * sizeof(MEMREF);
            MEMREF* topRanges = (MEMREF*)malloc(bufferSize);
            if (nullptr != topRanges)
            {
                memset(topRanges, 0, bufferSize);

                // Find the maxTopRangesCount largest ranges
                for (auto& range : state_->accesses)
                {
                    auto rangeSize = range.second - range.first + 1;
                    for (int i = 0; i < maxTopRangesCount; ++i)
                    {
                        if (rangeSize > topRanges[i].size)
                        {
                            for (int j = maxTopRangesCount - 1; j > i; --j)
                            {
                                topRanges[j] = topRanges[j - 1];
                            }
                            topRanges[i].ea   = range.first;
                            topRanges[i].size = rangeSize;
                            break;
                        }
                    }
                }

                if (rpcArgs[3].deleter)
                {
                    // Free data currently in arg if argData was allocated for us by pind
                    rpcArgs[3].deleter(reinterpret_cast< void* >(rpcArgs[3].argData));
                }
                rpcArgs[3].deleter = free; // Our deleter for memory we allocate - pind will call this to free our memory
                                           // after the response to the client is encoded
                rpcArgs[3].argData     = reinterpret_cast< uint64_t >(topRanges);
                rpcArgs[3].argDataSize = bufferSize;
                rpcArgs[3].argSchema   = GET_MEM_ACCESS_INFO_SCHEMA.argSchemaArray[3];
                rpcArgs[3].flags       = RpcArgFlagsNone;

                retRpcArg->argData = 1; // Mark success
            }
        } // NO NEED FOR ELSE

        // Fill in the return value of this operation, in this case it is a Nil
        // which means there is no return value. We should still fill in this
        // struct so that the server will know what kind of return value to encode
        // in the response.
        retRpcArg->argSchema   = GET_MEM_ACCESS_INFO_SCHEMA.returnValueSchema;
        retRpcArg->argDataSize = ARG_SCHEMA_SIZE(GET_MEM_ACCESS_INFO_SCHEMA.returnValueSchema);
        retRpcArg->flags       = RpcArgFlagsNone;
    }

    /**
     * @brief Do the final analysis and write the report to the file
     * 
     */
    void write_report()
    {
        if (state_->accesses.size() == 0) return;

        // We want to create a map of (key=block size) and (value=number of blocks of that size).
        // Go over the map of accesses. For each access calculate its size.
        // Then increment the appropriate entry in the map.
        std::map< size_t, uint32_t > blocks;
        for (auto const& pair : state_->accesses)
        {
            auto accessSize = (pair.second - pair.first + 1);
            blocks[accessSize]++;
        }
        state_->outfile << "Overall " << std::dec << state_->accesses.size() << " accesses to contiguous memory ranges."
                        << std::endl;
        state_->outfile << "Breakdown by contiguous range size:" << std::endl;
        state_->outfile << std::left << std::setw(10) << "block size"
                        << " | # blocks" << std::endl;
        state_->outfile << "---------- | ----------" << std::endl;
        for (auto const& block : blocks)
        {
            state_->outfile << std::left << std::setw(10) << std::dec << block.first << " | " << block.second << std::endl;
        }
    }
};

/*
 * This function is called by the server to query whether this plugin supports the input rpcId.
 * If it does then the function should return a pointer to the appropriate message schema.
 * If it doesn't then the function should return nullptr;
 */
t_rpc_message_schema const* get_rpc_schema(IPindPlugin* self, t_rpc_id rpcId)
{
    if (rpcId == OPEN_OUT_FILE_SCHEMA.rpcId)
    {
        return &OPEN_OUT_FILE_SCHEMA;
    }
    else if (rpcId == MEM_ANALYZE_SCHEMA.rpcId)
    {
        return &MEM_ANALYZE_SCHEMA;
    }
    else if (rpcId == GET_MEM_ACCESS_INFO_SCHEMA.rpcId)
    {
        return &GET_MEM_ACCESS_INFO_SCHEMA;
    }
    return nullptr;
}

/*
 * This function is called by the server only if get_rpc_schema(rpcId) returned a non-null schema.
 */
void do_rpc(IPindPlugin* self, t_rpc_id rpcId, t_arg_count argCount, t_rpc_arg* rpcArgs, t_rpc_ret* retRpcArg)
{
    auto this_ = reinterpret_cast< Buffer_offload_plugin* >(self);
    if (rpcId == OPEN_OUT_FILE_SCHEMA.rpcId)
    {
        return this_->do_open_outfile(argCount, rpcArgs, retRpcArg);
    }
    else if (rpcId == MEM_ANALYZE_SCHEMA.rpcId)
    {
        return this_->do_analyze_mem_access(argCount, rpcArgs, retRpcArg);
    }
    else if (rpcId == GET_MEM_ACCESS_INFO_SCHEMA.rpcId)
    {
        return this_->do_get_mem_access_info(argCount, rpcArgs, retRpcArg);
    }
}

/* 
 * This function should always return RPC.
 */
E_plugin_type get_plugin_type(IPindPlugin* self) { return RPC; }

bool init(IPindPlugin* self, int argc, const char* const argv[])
{
    assert(2 == argc);
    assert(std::string(argv[0]) == "-dummy-knob");
    assert(std::string(argv[1]) == "1");

    auto this_    = reinterpret_cast< Buffer_offload_plugin* >(self);
    this_->state_ = new Buffer_offload_plugin::Buffer_offload_plugin_state;
    return true;
}

void uninit(IPindPlugin* self)
{
    plugin_log_verbose(self, "buffer offload plugin is being unloaded\n");

#if __has_include(<filesystem>)
    // Check that plugin log file was created
    std::string filename = "buffer_offload_plugin.log.";
    filename += std::to_string(getpid());
    if (!std::filesystem::exists(filename))
    {
        std::cerr << filename << " plugin log filename does not exists" << std::endl;
        // Doing exit(-1) from the plugin doesn't make the application crash since it's being done from the pind process.
        // Checking it from the makefile.
        // exit(-1);
    }
#endif

    auto this_ = reinterpret_cast< Buffer_offload_plugin* >(self);
    this_->write_report();
    delete this_->state_;
}

/*
 * Load the plugin.
 * The plugin must fill in all the entries of the struct with valid function pointers.
 * The implementation can be empty but it must be a valid function.
 */
PLUGIN_EXTERNC PLUGIN__DLLVIS struct IPindPlugin* load_plugin(const char* name)
{
    static const char* OFFLOAD_PLUGIN_NAME = "buffer offload plugin";

    if (0 == strncmp(OFFLOAD_PLUGIN_NAME, name, sizeof(OFFLOAD_PLUGIN_NAME)))
    {
        Buffer_offload_plugin* plugin = new (std::nothrow) Buffer_offload_plugin;

        if (nullptr != plugin)
        {
            IRPCPlugin* rpcPlugin = &plugin->rpcPlugin;
            memset(rpcPlugin, 0, sizeof(IRPCPlugin));
            rpcPlugin->base_.get_plugin_type = get_plugin_type;
            rpcPlugin->base_.init            = init;
            rpcPlugin->base_.uninit          = uninit;
            rpcPlugin->get_rpc_schema        = get_rpc_schema;
            rpcPlugin->do_rpc                = do_rpc;

            return (IPindPlugin*)rpcPlugin;
        }
    }

    return nullptr; // We don't know the requested plugin or couldn't allocate it
}

/*
 * Unload the plugin - create the report and write it to a file.
 */
PLUGIN_EXTERNC PLUGIN__DLLVIS void unload_plugin(struct IPindPlugin* plugin) { delete plugin; }
