/*
 * Copyright (C) 2005-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <memory>
#include <mutex>
#include <new>
#include <cinttypes>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include <pinglue_common.h>
#include <rscproto_helper.h>
#include <pinglue.h>
#include "pinos_vfork_execve.h"

/**
 * @brief A structure holding a txll channel with a dedicated buffer.
 * 
 */
struct Channel
{
    /** @brief the channel address */
    void* handle = nullptr;

    /** @brief the channel id */
    uint64_t id;

    /** @brief a buffer that will used for the channel read/write operations */
    std::unique_ptr< uint8_t[] > buffer;
};

/** @brief A structure define a custom deleter for channel */
struct Channel_deleter
{
    void operator()(t_pg_channel* channel) const { (void)pinglue_close_channel(channel); }
};

/** @brief A unique_ptr for t_pg_channel with specialized deleter */
typedef std::unique_ptr< t_pg_channel, Channel_deleter > t_pg_unique_channel;

/**
 * @brief A structure holding a pinglue data.
 * 
 */
struct Pin_glue
{
    /** @brief The client channel for sending messages to server */
    t_pg_unique_channel clientChannel;

    /** @brief The server (pinglueserver) channel that will receive client messages */
    t_pg_unique_channel serverChannel;

    /** @brief The glue server pid */
    pid_t serverPid;

    /** @brief The glue client pid */
    pid_t clientPid;

    /** @brief 256 bit random seed */
    t_rsc_key_seed seed;

    /** @brief The pinglue state to use when sending RSC messages to pinglueserver
     (that communicate with pinglueserver server state) */
    t_rsc_state clientState;
    /** @brief The pinglue state to use when receiving RSC messages from pinglueserver 
     (that communicates with pinglueserver client state) */
    t_rsc_state serverState;

    /** @brief Mutex for serverState in case the serverState 
     * will used when listening for requests in multiple threads */
    std::mutex serverStateMutex;
};

/**
 * @brief A structure holding a script fragment.
 * 
 */
struct Pin_glue_script_fragment
{
    /** @brief The size of script */
    size_t size;
    /** @brief script */
    char script[];
};

/**
 * @brief A structure holding a service request message.
 * 
 */
struct Service_request_message
{
    /** @brief A buffer contains a service request message*/
    std::unique_ptr< uint8_t[] > msg;

    /** @brief message size*/
    size_t msgSize;

    /** @brief rsc message counter*/
    t_rsc_message_counter msgCounter;
};

/**
 * @brief A structure holding a service data.
 * 
 */
struct Pin_glue_service
{
    /** @brief the pin glue object registered for this service */
    t_pin_glue* pg = nullptr;

    /** @brief the default service channel for receiving requests from the server*/
    t_pg_unique_channel receiveChannel;

    /** @brief the service request message*/
    std::unique_ptr< Service_request_message > requestMsg;
};

namespace
{
constexpr uint32_t WINDOW_SIZE = 128;

/**
 * @brief Create a new txll channel.
 * 
 *  @param[out] channel  the created channel on success.
 * 
 * @return E_pin_glue_ret 
*/
template< bool WithBuffer = true > E_pin_glue_ret channel_create(t_pg_unique_channel& channel)
{
    try
    {
        t_pg_channel* newChannel = new t_pg_channel();
        channel.reset(newChannel);
    }
    catch (const std::bad_alloc& e)
    {
        return E_PG_OutOfMemory;
    }

    t_syscall_ret ret = tx_create(glue::CHANNEL_SIZE);
    if (IS_PINOS_SYSCALL_ERROR(ret))
    {
        return E_PG_Internal;
    }

    uint64_t id = tx_get_txid((void*)ret);
    if (-1 == id)
    {
        return E_PG_Internal;
    }

    channel->handle = (void*)ret;
    channel->id     = id;

    if constexpr (WithBuffer)
    {
        try
        {
            channel->buffer = std::make_unique< uint8_t[] >(glue::BUFFER_SIZE);
        }
        catch (const std::bad_alloc& e)
        {
            return E_PG_OutOfMemory;
        }
    }

    return E_PG_Success;
}

/**
 * @brief Close and delete channel.
 * 
 *  @param[in] channel  the channel to destroy.
 * 
 * @return E_pin_glue_ret 
*/
E_pin_glue_ret channel_destroy(t_pg_channel* channel)
{
    if (nullptr == channel)
    {
        return E_PG_InvalidArg;
    }

    t_syscall_ret ret = tx_close(channel->handle);
    if (SYSRET(SUCCESS) != ret)
    {
        return E_PG_Internal;
    }

    delete channel;

    return E_PG_Success;
}

/**
 * @brief Create glue channels, one main channel for client to send requests
 *         and one channel for server (pinglueserver) to receive client requests.
 * 
 *  @param[in,out] pgObj  the pgObj that will holds the created channels.
 * 
 * @return E_pin_glue_ret 
*/
E_pin_glue_ret create_glue_channels(t_pin_glue& pgObj)
{
    // Create client channel
    auto res = channel_create(pgObj.clientChannel);
    if (E_PG_Success != res)
    {
        return res;
    }

    // Create server channel
    res = channel_create< false >(pgObj.serverChannel);
    if (E_PG_Success != res)
    {
        return res;
    }

    return E_PG_Success;
}

/**
 * @brief Start pin (server) with the requested data.
 * 
 *  @param[in,out] pgObj   the pin glue object contains the seed, clientID and server channel ID
 *  @param[in] pinArgs     pin arguments
 *  @param[in] app         the app to run with pin
 *  @param[in] appArgs     app arguments
 *  @param[in] servicePath the service path to pass to the server, if nullptr the default server services path will be sent. 
 * 
 * The following variables can be set via environment variables:
 * GLUE_PIN_KIT     - MUST be set: the path of PIN KIT, used for pin binary location and pinglue server location
 * GLUE_SERVER_PATH - pinglue server location
 * GLUE_TOOL_SO     - pinglueserver.so for 64 bit location
 * GLUE_TOOL_SO_32  - pinglueserver.so for 32 bit location
 * 
 * @return E_pin_glue_ret 
*/
E_pin_glue_ret start_pin_server(t_pin_glue& pgObj, const t_pg_args* pinArgs, const char* app, const t_pg_args* appArgs,
                                const char* servicePath)
{
    auto envPinKit = std::getenv("GLUE_PIN_KIT");
    if (NULL == envPinKit)
    {
        std::cerr << "libpinglue: ERROR!! GLUE_PIN_KIT environment variable is not set!" << std::endl;
        return E_PG_InvalidArg;
    }

    const std::string PIN_KIT = envPinKit;
    auto envServerPath        = std::getenv("GLUE_SERVER_PATH");
    const std::string SERVER_PATH =
        envServerPath ? envServerPath : (std::filesystem::path(PIN_KIT) / "bindings/glue/pinglueserver");
    const std::string PIN_BIN      = std::filesystem::path(PIN_KIT) / "pin";
    auto envToolSO                 = std::getenv("GLUE_TOOL_SO");
    const std::string GLUE_TOOL_SO =
        envToolSO ? envToolSO : (std::filesystem::path(SERVER_PATH) / "obj-intel64/pinglueserver.so");
    auto envToolSO32               = std::getenv("GLUE_TOOL_SO_32");
    const std::string GLUE_TOOL_SO_32 =
        envToolSO32 ? envToolSO32 : (std::filesystem::path(SERVER_PATH) / "obj-ia32/pinglueserver.so");

    Clone_args args {};
    args.childProcessPath = PIN_BIN;

    glue::generate_random_seed(pgObj.seed.bits);

    std::string seedStr = glue::seed_to_string(pgObj.seed.bits);

    // Set server exec args
    std::vector< const char* > execArgs;

    execArgs.emplace_back(args.childProcessPath.c_str());
    if (nullptr != pinArgs && nullptr != pinArgs->argv)
    {
        for (size_t i = 0; i < pinArgs->argc; ++i)
        {
            execArgs.emplace_back(pinArgs->argv[i]);
        }
    }

    auto serverChannelStr = std::to_string(pgObj.serverChannel->id);
    auto clientPIdStr     = std::to_string(pgObj.clientPid);

    execArgs.emplace_back("-t64");
    execArgs.emplace_back(GLUE_TOOL_SO.c_str());
    execArgs.emplace_back("-t");
    execArgs.emplace_back(GLUE_TOOL_SO_32.c_str());
    execArgs.emplace_back("-glue-server-txid");
    execArgs.emplace_back(serverChannelStr.c_str());
    execArgs.emplace_back("-rsc-client-id");
    execArgs.emplace_back(clientPIdStr.c_str());
    execArgs.emplace_back("-rsc-hkdf-seed");
    execArgs.emplace_back(seedStr.c_str());
    execArgs.emplace_back("-services-dir");

    std::string servicePathStr;
    if (nullptr != servicePath)
    {
        execArgs.emplace_back(servicePath);
    }
    else
    {
        // Pass the server Services dir
        servicePathStr = std::filesystem::path(SERVER_PATH) / "Services";
        execArgs.emplace_back(servicePathStr.c_str());
    }
    execArgs.emplace_back("--");
    execArgs.emplace_back(app);
    if (nullptr != appArgs && nullptr != appArgs->argv)
    {
        for (size_t i = 0; i < appArgs->argc; ++i)
        {
            execArgs.emplace_back(appArgs->argv[i]);
        }
    }
    execArgs.emplace_back(nullptr);

    args.childArgv = execArgs.data();
#if DEBUG
    printf("PinGlue command:");
    for (auto& pStr : execArgs)
    {
        if (nullptr != pStr)
        {
            printf(" %s", pStr);
        }
    }
    printf("\n");
#endif
    // Exec the server
    uint8_t stack[256 * 1024] = {0};

    auto cpid                 = pinos_clone_vfork(pinos_execveat, &args, stack, sizeof(stack));

    // auto cpid                 = fork();
    // if (0 ==cpid)
    // {
    //     execvp(args.childArgv[0], (char**)args.childArgv);
    // }

    // pid_t cpid;

    // posix_spawn(&cpid, args.childArgv[0],
    //                    nullptr,
    //                    nullptr,
    //                    (char**)args.childArgv,
    //                    environ);

    if (IS_PINOS_SYSCALL_ERROR(cpid) || args.execveFailed)
    {
        return E_PG_NoPin;
    }

    pgObj.serverPid = cpid;

    return E_PG_Success;
}

/**
 * @brief Check if serviceRequest fields are valid.
 * 
 *  @param[in] serviceRequest  a service request.
 * 
 * @return true if valid, false otherwise.
*/
bool is_valid_service_request(const t_pg_service_request& serviceRequest)
{
    if ((nullptr == serviceRequest.serviceName) || (nullptr == serviceRequest.callbackName) ||
        ((sizeof(serviceRequest.name) - 1) != strnlen(serviceRequest.name, sizeof(serviceRequest.name))))
    {
        return false;
    }

    if (((nullptr != serviceRequest.scripts) && (0 == serviceRequest.scriptsSize)) ||
        ((nullptr != serviceRequest.knobs) && (0 == serviceRequest.knobsSize)))
    {
        return false;
    }

    return true;
}

/**
 * @brief Add and encode service request args to RSC message
 * 
 *  @param[in]  pgObj            the pin glue object
 *  @param[in]  serviceRequest   the service request
 *  @param[in]  resultChannelID  the result channel id that will receive the result
 *  @param[out] serviceReqMsg    the service request message that will be sent in pinglue_service_start
 *   
 * @return E_pin_glue_ret 
*/
E_pin_glue_ret encode_service_request(t_pin_glue& pgObj, const t_pg_service_request* serviceRequest, uint64_t resultChannelID,
                                      std::unique_ptr< Service_request_message >& serviceReqMsg)
{
    using namespace glue;

    constexpr auto BUFFER_SCHEMA = BUFFER_ARG_SCHEMA();

    RSC_proto_message_wrapper< E_Rsc_Role::Client > rscMsg(&Service_Request_Schema, &(pgObj.clientState));
    // Init message
    int status = rscMsg.init_encode();
    if (RscErrorSuccess != status)
    {
        return E_PG_Internal;
    }

    std::unique_ptr< t_rpc_arg[] > knobs;
    std::unique_ptr< t_rpc_arg[] > scripts;

    status |= rscMsg.add_encode_arg(0, resultChannelID);
    status |= rscMsg.add_encode_arg(1, (uint64_t)serviceRequest->name, sizeof(serviceRequest->name)); // 5 Alpha Numeric character
    status |=
        rscMsg.add_encode_arg(2, (uint64_t)serviceRequest->serviceName, 1 + strnlen(serviceRequest->serviceName, MAX_FUNC_NAME));
    status |= rscMsg.add_encode_arg(3, (uint64_t)serviceRequest->callbackName,
                                    1 + strnlen(serviceRequest->callbackName, MAX_FUNC_NAME));
    if (nullptr == serviceRequest->knobs)
    {
        status |= rscMsg.add_encode_nil_arg();
    }
    else
    {
        try
        {
            knobs = std::make_unique< t_rpc_arg[] >(serviceRequest->knobsSize);
        }
        catch (const std::bad_alloc& e)
        {
            return E_PG_OutOfMemory;
        }
        for (auto i = 0; i < serviceRequest->knobsSize; i++)
        {
            knobs[i] = MAKE_RPC_ARG(BUFFER_SCHEMA, reinterpret_cast< uint64_t >(serviceRequest->knobs[i]),
                                    1 + strnlen(serviceRequest->knobs[i], 4_KB), RpcArgFlagsNone);
        }
        status |= rscMsg.add_encode_arg(4, (uint64_t)knobs.get(), serviceRequest->knobsSize);
    }

    if (nullptr == serviceRequest->scripts)
    {
        status |= rscMsg.add_encode_nil_arg();
    }
    else
    {
        try
        {
            scripts = std::make_unique< t_rpc_arg[] >(serviceRequest->scriptsSize);
        }
        catch (const std::bad_alloc& e)
        {
            return E_PG_OutOfMemory;
        }

        for (auto i = 0; i < serviceRequest->scriptsSize; i++)
        {
            scripts[i] = MAKE_RPC_ARG(BUFFER_SCHEMA, reinterpret_cast< uint64_t >(serviceRequest->scripts[i]->script),
                                      1 + serviceRequest->scripts[i]->size, RpcArgFlagsNone);
        }

        status |= rscMsg.add_encode_arg(4, (uint64_t)scripts.get(), serviceRequest->scriptsSize);
    }

    if (RscErrorSuccess != status)
    {
        return E_PG_Internal;
    }

    // Encode
    serviceReqMsg->msgSize = rscMsg.get_encoding_size();
    assert(-1 != serviceReqMsg->msgSize);

    try
    {
        serviceReqMsg->msg = std::make_unique< uint8_t[] >(serviceReqMsg->msgSize);
    }
    catch (const std::bad_alloc& e)
    {
        return E_PG_OutOfMemory;
    }

    status = rscMsg.encode(serviceReqMsg->msg.get(), serviceReqMsg->msgSize);
    if (RscErrorSuccess != status)
    {
        return E_PG_Internal;
    }

    serviceReqMsg->msgCounter = rscMsg.get_message_counter();

    return E_PG_Success;
}

/**
 * @brief wait for a response from the server (pinglueserver) after sending the request and handle the response.
 *        In response ret: 
 *          in start program response it should contain true for success
 *          in service response it should contain nill or Error Message. 
 * 
 *  @param[in] channel            the channel to wait for the response.
 *  @param[in] rscMsg             an instance of Client rsc proto message
 *  @param[in] timeout            the timeout in microseconds to wait, default is 3 seconds.
 * 
 *  @param[in] IsServiceResponse  Template parameter, indicate if it's service response to treat response ret as nill or Error message. 
 *                                Otherwise response ret should treat as bool (true for success and false for failure).
 *   
 * @return E_pin_glue_ret
*/
template< bool IsServiceResponse = false >
E_pin_glue_ret wait_and_handle_response(t_pg_channel* channel, RSC_proto_message_wrapper< E_Rsc_Role::Client >& rscMsg,
                                        uint32_t timeout = 3000000)
{
    // Wait and read message
    uint64_t serverChannelId;
    t_syscall_ret ret =
        glue::pinglue_tx_read(channel->handle, timeout, channel->buffer.get(), glue::BUFFER_SIZE, serverChannelId);
    if (IS_PINOS_SYSCALL_ERROR(ret))
    {
        if (SYSRET(ETIMEDOUT) == ret)
        {
            return E_PG_Timeout;
        }
        return E_PG_Internal;
    }

    E_rsc_error status = rscMsg.decode(channel->buffer.get(), glue::BUFFER_SIZE);
    if (RscErrorSuccess != status)
    {
        return E_PG_Internal;
    }

    if constexpr (IsServiceResponse)
    {
        if (!rscMsg.is_nil_decode_arg(glue::Service_Request_Schema.argCount))
        {
            std::cerr << "libpinglue: Service Response error:"
                      << (const char*)rscMsg.get_decode_primitive_arg(glue::Service_Request_Schema.argCount) << std::endl;

            return E_PG_Internal;
        }
    }
    else if (true != rscMsg.get_decode_bool_ret_arg())
    {
        return E_PG_Internal;
    }

    return E_PG_Success;
}

/**
 * @brief Send reject response.
 * 
 *  @param[in] channel          the channel to send the response on.
 *  @param[in] senderChannelId  the channel ID that will receive the response.
 *  @param[in] rscMsg           an instance of Server rsc proto message
 *   
 * @return E_pin_glue_ret
 */
E_pin_glue_ret send_reject_response(t_pg_channel* channel, uint64_t senderChannelId,
                                    RSC_proto_message_wrapper< E_Rsc_Role::Server >& rscMsg)
{
    int intStatus = rscMsg.init_encode();
    intStatus |= rscMsg.encode(channel->buffer.get(), glue::BUFFER_SIZE, true);
    if (RscErrorSuccess != intStatus)
    {
        return E_PG_Internal;
    }

    size_t msgSize = rscMsg.get_encoding_size();
    auto res       = tx_send_to(channel->handle, senderChannelId, channel->buffer.get(), msgSize);
    if (IS_PINOS_SYSCALL_ERROR(res))
    {
        return E_PG_Internal;
    }

    return E_PG_Success;
}

/**
 * @brief Handle the request for remote function call and send the t_pg_func_response (remote function return value) to server.
 * 
 *  @param[in] channel          the channel to send the response on.
 *  @param[in] senderChannelId  the channel ID that will receive the response.
 *  @param[in] rscMsg           an instance of Server rsc proto message
 *  @param[in] remoteCB         the remote callback function to call with the request.
 *   
 * @return E_pin_glue_ret
*/
E_pin_glue_ret handle_remote_func_cb(t_pg_channel* channel, uint64_t senderChannelId,
                                     RSC_proto_message_wrapper< E_Rsc_Role::Server >& rscMsg, t_pg_remote_cb remoteCB)
{
    E_rsc_error status = rscMsg.decode_without_schema(channel->buffer.get(), glue::BUFFER_SIZE);
    if (RscErrorSuccess != status)
    {
        return E_PG_Internal;
    }

    t_pg_remote_func remoteFunc;
    remoteFunc.functionName = (const char*)rscMsg.get_decode_primitive_arg(0);
    remoteFunc.name         = (const char*)rscMsg.get_decode_primitive_arg(1);

    size_t decodedArgsSize = rscMsg.get_decoded_args_size();
    if (2 < decodedArgsSize)
    {
        remoteFunc.request.args     = &(rscMsg.get_decoded_args()[2]);
        remoteFunc.request.argsSize = (decodedArgsSize - 2);
    }
    else
    {
        remoteFunc.request.args     = nullptr;
        remoteFunc.request.argsSize = 0;
    }

    // The schema is only for rpc ID logic in RSC_proto_message_wrapper
    rscMsg.set_schema(&glue::Remote_Call_Schema);

    // Call the remote function
    t_pg_remote_response response;
    response.deleter = nullptr;
    response.handled = false;
    response.cancel  = false;

    remoteCB(&remoteFunc, &response);

    // TODO NYI - if false send an error message to server to cancel the service.
    assert(false == response.cancel);

    // Send response
    int intStatus = rscMsg.init_encode();

    if (false == response.handled)
    {
        (void)send_reject_response(channel, senderChannelId, rscMsg);
        return E_PG_Internal;
    }

    intStatus |= rscMsg.add_encode_arg(response.ret);
    intStatus |= rscMsg.encode(channel->buffer.get(), glue::BUFFER_SIZE);

    assert(RscErrorSuccess == intStatus);

    size_t msgSize = rscMsg.get_encoding_size();
    assert(-1 != msgSize);

    auto ret = tx_send_to(channel->handle, senderChannelId, channel->buffer.get(), msgSize);

    // Call to response deleter if defined
    if (nullptr != response.deleter)
    {
        response.deleter(&response.ret, response.state);
    }

    if (IS_PINOS_SYSCALL_ERROR(ret))
    {
        return E_PG_Internal;
    }

    return E_PG_Success;
}

/**
 * @brief Handle the request with the service result and send the response.
 * 
 *  @param[in] channel          the channel to send the response on.
 *  @param[in] senderChannelId  the channel ID that will receive the response.
 *  @param[in] rscMsg           an instance of Server rsc proto message
 *  @param[out] serviceResult   the structure to fill with the service result data.
 *   
 * @return E_pin_glue_ret
*/
E_pin_glue_ret handle_service_result_msg(t_pg_channel* channel, uint64_t senderChannelId,
                                         RSC_proto_message_wrapper< E_Rsc_Role::Server >& rscMsg,
                                         std::unique_ptr< t_pg_service_result >& serviceResult)
{
    E_rsc_error status = rscMsg.decode(channel->buffer.get(), glue::BUFFER_SIZE);
    if (RscErrorSuccess != status)
    {
        (void)send_reject_response(channel, senderChannelId, rscMsg);
        return E_PG_Internal;
    }

    std::unique_ptr< char[] > funcName;
    std::unique_ptr< char[] > result;
    std::unique_ptr< char[] > name; // 5 Alpha Numeric characters named for this service
    try
    {
        funcName = std::make_unique< char[] >(rscMsg.get_decoded_args()[0].argDataSize);
        result   = std::make_unique< char[] >(rscMsg.get_decoded_args()[1].argDataSize);
        name     = std::make_unique< char[] >(rscMsg.get_decoded_args()[2].argDataSize);
    }
    catch (const std::bad_alloc& e)
    {
        return E_PG_OutOfMemory;
    }

    rscMsg.get_decode_buffer_arg(0, (void*)funcName.get());
    rscMsg.get_decode_buffer_arg(1, (void*)result.get());
    rscMsg.get_decode_buffer_arg(2, (void*)name.get());

    serviceResult->funcName     = funcName.release();
    serviceResult->result       = result.release();
    serviceResult->resultLength = rscMsg.get_decoded_args()[1].argDataSize;
    serviceResult->name         = name.release();

    // Send response
    int intStatus = rscMsg.init_encode();
    intStatus |= rscMsg.add_encode_nil_args();
    intStatus |= rscMsg.add_encode_nil_arg(); // ret is always void
    intStatus |= rscMsg.encode(channel->buffer.get(), glue::BUFFER_SIZE);
    if (RscErrorSuccess != intStatus)
    {
        return E_PG_Internal;
    }

    size_t msgSize = rscMsg.get_encoding_size();
    assert(-1 != msgSize);

    auto ret = tx_send_to(channel->handle, senderChannelId, channel->buffer.get(), msgSize);
    if (SYSRET(SUCCESS) != ret)
    {
        return E_PG_Internal;
    }

    return E_PG_Success;
}

/**
 * @brief Try to read messages from the server on a given channel, and init the rsc message.  
 * 
 *  @param[in]  channel          the registered channel to get the messages from the server
 *  @param[in]  timeoutUS        the timeout to wait in micro seconds, 
 *                               -1 to wait indefinitely until some message arrives or an error occurs.
 *  @param[in]  rscMsg           an instance of Server rsc proto message
 *  @param[out] senderChannelId  the channel ID that sent the message
 *  @param[out] rpcId            the RPC ID of the arrived message
 *   
 * @return E_pin_glue_ret
*/
E_pin_glue_ret handle_server_messages(t_pg_channel* channel, uint32_t timeoutUS,
                                      RSC_proto_message_wrapper< E_Rsc_Role::Server >& rscMsg, uint64_t& senderChannelId,
                                      t_rpc_id& rpcId)
{
    bool continueRead = (-1 == timeoutUS);

    do
    {
        // Check if any message arrived
        t_syscall_ret ret =
            glue::pinglue_tx_read(channel->handle, timeoutUS, channel->buffer.get(), glue::BUFFER_SIZE, senderChannelId);
        if (IS_PINOS_SYSCALL_ERROR(ret))
        {
            if (SYSRET(ETIMEDOUT) != ret)
            {
                return E_PG_Internal;
            }
            if (!continueRead)
            {
                return E_PG_Timeout;
            }
        }
        else // SUCCESS - massage arrived
        {
            continueRead = false;
        }
    }
    while (continueRead);

    // Check and init rsc message.
    E_rsc_error status = rscMsg.init_decode(channel->buffer.get(), glue::BUFFER_SIZE, rpcId);
    if (RscErrorSuccess != status)
    {
        return E_PG_Internal;
    }

    return E_PG_Success;
}

/**
 * @brief Extracts the services_path value from pgArgs if passed.
 * 
 *  @param[in]  pgArgs  A struct contains array of pin args, could be nullptr.
 *   
 * @return const char*  The value of services path if found. nullptr if not found.
*/
const char* extract_services_path_from_pgArgs(const t_pg_args* pgArgs)
{
    if (nullptr == pgArgs)
    {
        return nullptr;
    }

    for (auto i = 0; i < pgArgs->argc; ++i)
    {
        if (std::strcmp(pgArgs->argv[i], "-services-path") == 0 && i + 1 < pgArgs->argc)
        {
            return pgArgs->argv[i + 1];
        }
    }
    return nullptr;
}

/**
 * @brief Create a new t_pg_script_fragment
 * 
 *  @param[in]  scriptSize  the script size.
 *   
 * @return t_pg_script_fragment*    A pointer to the new script fragment, nullptr on failure.
*/
t_pg_script_fragment* script_fragment_create(size_t scriptSize)
{
    uint8_t* buffer = new (std::nothrow) uint8_t[sizeof(Pin_glue_script_fragment) + scriptSize];
    if (nullptr == buffer)
    {
        return nullptr;
    }
    return reinterpret_cast< Pin_glue_script_fragment* >(buffer);
}

} // anonymous namespace

////////////////////////////////////////////////////////
//
// Implementation of the API functions
//
////////////////////////////////////////////////////////

/*
 * pinglue_open
 */
E_pin_glue_ret pinglue_open(t_pin_glue** pg, const t_pg_args* pinArgs, const t_pg_args* pgArgs, const char* program,
                            const t_pg_args* programArgs)
{
    if (nullptr == pg || nullptr == program)
    {
        return E_PG_InvalidArg;
    }
    // Init PINOS
    static void* state = nullptr;
    if (nullptr == state)
    {
        if (t_syscall_ret(0) != OS_Init(&state, nullptr))
        {
            return E_PG_Internal;
        }
    }

    std::unique_ptr< t_pin_glue > pgObj = std::make_unique< t_pin_glue >();

    // Save client pid
    auto sysRet = OS_Syscall(SYS_getpid, 0, nullptr);
    if (0 > sysRet)
    {
        return E_PG_Internal;
    }
    pgObj->clientPid = sysRet;

    // Create the txll channels for client and server and save their info in pgObj.
    auto pgRet = create_glue_channels(*pgObj.get());
    if (E_PG_Success != pgRet)
    {
        return pgRet;
    }

    // Extract servicePath from pgArgs
    const char* servicePath = extract_services_path_from_pgArgs(pgArgs);

    //  Spawn an instance of Pin + pinglueserver passing all required parameters to Pin, pintool and instrumented application.
    pgRet = start_pin_server(*pgObj.get(), pinArgs, program, programArgs, servicePath);
    if (E_PG_Success != pgRet)
    {
        return pgRet;
    }

    // Initialize client state for sending rsc proto messages.
    E_rsc_error status = initialize_client_state(pgObj->clientPid, &(pgObj->seed), nullptr, nullptr, &(pgObj->clientState));
    if (RscErrorSuccess != status)
    {
        return E_PG_Internal;
    }

    // Initialize client state for getting rsc proto messages.
    status = initialize_server_state(pgObj->clientPid, &(pgObj->seed), WINDOW_SIZE, nullptr, nullptr, &(pgObj->serverState));
    if (RscErrorSuccess != status)
    {
        return E_PG_Internal;
    }

    *pg = pgObj.release();

    return E_PG_Success;
}

/*
 * pinglue_start_program
 */
E_pin_glue_ret pinglue_start_program(t_pin_glue* pg)
{
    if (nullptr == pg)
    {
        return E_PG_InvalidArg;
    }

    RSC_proto_message_wrapper< E_Rsc_Role::Client > rscMsg(&glue::Start_Program_Request_Schema, &(pg->clientState));

    if (RscErrorSuccess != rscMsg.init_encode())
    {
        return E_PG_Internal;
    }

    if (RscErrorSuccess != rscMsg.encode(pg->clientChannel->buffer.get(), glue::BUFFER_SIZE))
    {
        return E_PG_Internal;
    }

    // Send the request
    size_t msgSize = rscMsg.get_encoding_size();
    assert(-1 != msgSize);

    t_syscall_ret ret = tx_send_to(pg->clientChannel->handle, pg->serverChannel->id, pg->clientChannel->buffer.get(), msgSize);
    if (SYSRET(SUCCESS) != ret)
    {
        return E_PG_Internal;
    }

    // Wait for response with a timeout
    E_pin_glue_ret glueRet = wait_and_handle_response(pg->clientChannel.get(), rscMsg);
    if (E_PG_Success != glueRet)
    {
        return glueRet;
    }

    return E_PG_Success;
}

/*
 * pinglue_wait_for_server
 */
E_pin_glue_ret pinglue_finish(t_pin_glue* pg, uint32_t timeoutUS)
{
    if (nullptr == pg)
    {
        return E_PG_InvalidArg;
    }

    int64_t remainingTime = static_cast< int64_t >(timeoutUS);
    int options           = (-1 == timeoutUS) ? 0 : WNOHANG;

    int status;
    t_syscall_arg waitArgs[] {t_syscall_arg(pg->serverPid), t_syscall_arg(&status), t_syscall_arg(options),
                              t_syscall_arg(nullptr)};
    auto argsSize = SYSCALL_ARG_COUNT(waitArgs);
    do
    {
        auto ret = OS_Syscall(SYS_wait4, argsSize, waitArgs);
        if (IS_PINOS_SYSCALL_ERROR(ret))
        {
            if ((SYSRET(EAGAIN) == ret) || (SYSRET(EINTR) == ret))
            {
                continue;
            }

            return E_PG_Internal;
        }

        if (ret == pg->serverPid)
        {
            if (!WIFEXITED(status) || (0 != WEXITSTATUS(status)))
            {
                return E_PG_PinExit;
            }
            return E_PG_Success;
        }

        // Note: we can come here only in case of WNOHANG.
        if (0 >= remainingTime)
        {
            break;
        }

        // Sleep 1000 micro seconds
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
        remainingTime -= 1000;
    }
    while (true);

    return E_PG_Timeout;
}

/*
 * pinglue_close
 */
E_pin_glue_ret pinglue_close(t_pin_glue* pg)
{
    if (nullptr == pg)
    {
        return E_PG_InvalidArg;
    }

    release_client_state(&(pg->clientState));
    release_server_state(&(pg->serverState));

    delete pg;

    return E_PG_Success;
}

/*
 * pinglue_service_open
 */
E_pin_glue_ret pinglue_service_open(t_pg_service** service, t_pin_glue* pg, const t_pg_service_request* serviceRequest)
{
    if ((nullptr == service) || (nullptr == pg) || (nullptr == serviceRequest))
    {
        return E_PG_InvalidArg;
    }
    if (!is_valid_service_request(*serviceRequest))
    {
        return E_PG_InvalidArg;
    }

    std::unique_ptr< t_pg_service > serviceObj;
    try
    {
        serviceObj = std::make_unique< t_pg_service >();
    }
    catch (const std::bad_alloc& e)
    {
        return E_PG_OutOfMemory;
    }

    serviceObj->pg = pg;

    if (nullptr != serviceRequest->resultChannel)
    {
        serviceObj->receiveChannel.reset(serviceRequest->resultChannel);
    }
    else
    {
        auto res = channel_create(serviceObj->receiveChannel);
        if (E_PG_Success != res)
        {
            return res;
        }
    }

    // Encode service request
    try
    {
        serviceObj->requestMsg = std::make_unique< Service_request_message >();
    }
    catch (const std::bad_alloc& e)
    {
        return E_PG_OutOfMemory;
    }

    auto res = encode_service_request(*pg, serviceRequest, serviceObj->receiveChannel->id, serviceObj->requestMsg);
    if (E_PG_Success != res)
    {
        return res;
    }

    *service = serviceObj.release();

    return E_PG_Success;
}

/*
 * pinglue_service_start
 */
E_pin_glue_ret pinglue_service_start(t_pg_service* service)
{
    if (nullptr == service)
    {
        return E_PG_InvalidArg;
    }

    t_pin_glue* pg                      = service->pg;
    Service_request_message* requestMsg = service->requestMsg.get();
    if (nullptr == requestMsg)
    {
        return E_PG_InvalidArg;
    }

    // Send the request
    t_syscall_ret ret = tx_send_to(pg->clientChannel->handle, pg->serverChannel->id, requestMsg->msg.get(), requestMsg->msgSize);
    if (SYSRET(SUCCESS) != ret)
    {
        return E_PG_Internal;
    }

    // Wait for response with a timeout
    RSC_proto_message_wrapper< E_Rsc_Role::Client > rscMsg(&glue::Service_Request_Schema, &(pg->clientState));
    rscMsg.set_message_counter(requestMsg->msgCounter);

    return wait_and_handle_response< true >(pg->clientChannel.get(), rscMsg);
}

/*
 * pinglue_service_close
 */
E_pin_glue_ret pinglue_service_close(t_pg_service* service)
{
    if (nullptr == service)
    {
        return E_PG_InvalidArg;
    }

    delete service;

    return E_PG_Success;
}

/*
 * pinglue_service_wait
 */
E_pin_glue_ret pinglue_service_wait(t_pg_service* service, uint32_t timeoutUS, t_pg_remote_cb remoteCB,
                                    t_pg_service_result** serviceResult)
{
    if ((nullptr == service) || (nullptr == serviceResult))
    {
        return E_PG_InvalidArg;
    }

    t_pin_glue* pg        = service->pg;
    t_pg_channel* channel = service->receiveChannel.get();
    RSC_proto_message_wrapper< E_Rsc_Role::Server > rscMsg(&(pg->serverState), &(pg->serverStateMutex));

    // Check if any valid message arrived
    uint64_t senderChannelId;
    t_rpc_id receivedRpcId;
    auto ret = handle_server_messages(channel, timeoutUS, rscMsg, senderChannelId, receivedRpcId);
    if (E_PG_Success != ret)
    {
        return ret;
    }

    // Handle the arrived message according to it's type (RPC ID)
    if (glue::SERVICE_RESULT_RPC_ID == receivedRpcId)
    {
        rscMsg.set_schema(&glue::Service_Result_Schema);
        std::unique_ptr< t_pg_service_result > serviceResultObj;
        try
        {
            serviceResultObj = std::make_unique< t_pg_service_result >();
        }
        catch (const std::bad_alloc& e)
        {
            return E_PG_OutOfMemory;
        }

        auto ret = handle_service_result_msg(channel, senderChannelId, rscMsg, serviceResultObj);
        if (E_PG_Success == ret)
        {
            *serviceResult = serviceResultObj.release();
        }

        return ret;
    }

    if (glue::REMOTE_CALL_RPC_ID == receivedRpcId)
    {
        if (nullptr == remoteCB)
        {
            std::cerr << "libpinglue: pinglue_service_wait ERROR!! remote call arrived but remoteCB arg is not passed"
                      << std::endl;
            return E_PG_InvalidArg;
        }

        return handle_remote_func_cb(channel, senderChannelId, rscMsg, remoteCB);
    }

    return E_PG_Internal;
}

/*
 * pinglue_service_result_free
 */
E_pin_glue_ret pinglue_service_result_free(t_pg_service_result* serviceResult)
{
    if (nullptr == serviceResult)
    {
        return E_PG_InvalidArg;
    }

    delete[] serviceResult->funcName;
    delete[] serviceResult->result;
    delete[] serviceResult->name;
    delete serviceResult;

    return E_PG_Success;
}

/*
 * pinglue_open_channel
 */
E_pin_glue_ret pinglue_open_channel(t_pg_channel** channel)
{
    if (nullptr == channel)
    {
        return E_PG_InvalidArg;
    }

    t_pg_unique_channel channelObj;

    auto res = channel_create(channelObj);
    if (E_PG_Success != res)
    {
        return res;
    }

    *channel = channelObj.release();

    return E_PG_Success;
}

/*
 * pinglue_close_channel
 */
E_pin_glue_ret pinglue_close_channel(t_pg_channel* channel) { return channel_destroy(channel); }

/*
 * pinglue_channel_wait
 */
E_pin_glue_ret pinglue_channel_wait(t_pin_glue* pg, t_pg_channel* channel, uint32_t timeoutUS, t_pg_remote_cb remoteCB)
{
    if ((nullptr == pg) || (nullptr == channel) || (nullptr == remoteCB))
    {
        return E_PG_InvalidArg;
    }

    // Check if any valid message arrived
    RSC_proto_message_wrapper< E_Rsc_Role::Server > rscMsg(&(pg->serverState), &(pg->serverStateMutex));
    uint64_t senderChannelId;
    t_rpc_id receivedRpcId;

    auto ret = handle_server_messages(channel, timeoutUS, rscMsg, senderChannelId, receivedRpcId);
    if (E_PG_Success != ret)
    {
        return ret;
    }

    // Handle the arrived message
    if (receivedRpcId == glue::REMOTE_CALL_RPC_ID)
    {
        return handle_remote_func_cb(channel, senderChannelId, rscMsg, remoteCB);
    }

    return E_PG_Internal;
}

/*
 * GLUE_register_remote_function
 */
t_pg_script_fragment* GLUE_register_remote_function(t_pg_channel* channel, const char* scriptFuncName, const char* remoteFuncName,
                                                    bool noBlock)
{
    if ((nullptr == scriptFuncName) || (nullptr == remoteFuncName))
    {
        return nullptr;
    }

    // The size without scriptFuncName and remoteFuncName
    // is 37 plus 20 (max decimal digits in uint64_t) + 1 (null terminator)
    constexpr size_t START_SIZE = 58;
    size_t resultScriptSize     = START_SIZE + strnlen(scriptFuncName, MAX_FUNC_NAME) + strnlen(remoteFuncName, MAX_FUNC_NAME);

    auto fragment = script_fragment_create(resultScriptSize);
    if (nullptr == fragment)
    {
        return nullptr;
    }

    const char* noBlockStr = noBlock ? "True" : "False";
    int res                = -1;
    if (nullptr == channel)
    {
        res = snprintf(fragment->script, resultScriptSize, "GLUE_RegisterRemoteFunction(None, '%s', '%s', %s)", scriptFuncName,
                       remoteFuncName, noBlockStr);
    }
    else
    {
        res = snprintf(fragment->script, resultScriptSize, "GLUE_RegisterRemoteFunction(%" PRIu64 ", '%s', '%s', %s)",
                       channel->id, scriptFuncName, remoteFuncName, noBlockStr);
    }
    assert((0 < res) && (res < resultScriptSize));

    fragment->size = res;

    return fragment;
}

/*
 * GLUE_server_script
 */
t_pg_script_fragment* GLUE_server_script(const char* script, size_t scriptSize)
{
    if ((nullptr == script) || (0 == scriptSize))
    {
        return nullptr;
    }

    auto fragment = script_fragment_create(scriptSize + 1); // + 1 to null terminator.
    if (nullptr == fragment)
    {
        return nullptr;
    }

    memcpy(fragment->script, script, scriptSize);
    fragment->script[scriptSize] = '\0';
    fragment->size               = scriptSize;

    return fragment;
}

/*
 * pinglue_script_fragment_free
 */
E_pin_glue_ret pinglue_script_fragment_free(t_pg_script_fragment* fragment)
{
    if (nullptr == fragment)
    {
        return E_PG_InvalidArg;
    }

    auto buffer = reinterpret_cast< uint8_t* >(fragment);
    delete[] buffer;

    return E_PG_Success;
}