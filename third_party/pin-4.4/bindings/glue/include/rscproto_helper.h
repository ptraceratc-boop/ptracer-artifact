/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

// <COMPONENT>: librscproto
// <FILE-TYPE>: implementation

#ifndef _RSCPROTO_HELPER_H_
#define _RSCPROTO_HELPER_H_

#include <rscproto.h>
#include <mutex>

enum E_Rsc_Role
{
    Client = 0,
    Server
};

// This class is a wrapper for client or server
// Client - it used for Sending Request and Getting Response - Encode Request and Decode Response
// Server - it used for Getting Request Sending Response - Decode Request and Encode Response

template< E_Rsc_Role Role > class RSC_proto_message_wrapper
{
  private:
    RSC_proto_message_wrapper() noexcept {}

    constexpr static t_rpc_arg_schema nilSchema_     = NIL_ARG_SCHEMA();
    constexpr static t_rpc_arg nilArg_               = MAKE_RPC_ARG(nilSchema_, 0, 0, RpcArgFlagsNone);
    constexpr static E_rpc_decode_flags decodeFlags_ = (Role == E_Rsc_Role::Client) ? RpcDecResponse : RpcDecDefault;

    const RPC_message_schema* schema_;
    t_rsc_state* state_;
    t_rsc_message* msg_;
    t_rsc_message_counter cntr_;
    t_rpc_arg* decodedArgs_;
    size_t decodedArgsSize_;

    // When the server is listen to requests on multiple threads that uses same server t_rsc_state,
    // he should pass the mutex that shared between all those threads
    // TODO we can use a static var of mutex in this class, but it means we will lock on all server users of this class
    // (even if the client will have 2 t_pin_glue objects and 2 differentt pin servers processes).
    std::mutex* checkRequestMutex_;

  public:
    // Constructor specialization for Client
    template< E_Rsc_Role T = Role, typename = std::enable_if_t< T == E_Rsc_Role::Client > >
    RSC_proto_message_wrapper(const RPC_message_schema* schema, t_rsc_state* state)
        : schema_(schema), state_(state), msg_(nullptr), decodedArgs_(nullptr), decodedArgsSize_(0)
    {}

    // Constructor specialization for Server
    template< E_Rsc_Role T = Role, typename = std::enable_if_t< T == E_Rsc_Role::Server > >
    RSC_proto_message_wrapper(t_rsc_state* state, std::mutex* checkRequestMutex = nullptr)
        : schema_(nullptr), state_(state), msg_(nullptr), decodedArgs_(nullptr), decodedArgsSize_(0),
          checkRequestMutex_(checkRequestMutex)
    {}

    ~RSC_proto_message_wrapper() noexcept
    {
        // The following is based on the internal rsc_proto implementation that protecting against null pointers.
        release_rpc_args(state_, decodedArgs_, decodedArgsSize_);
        release_rsc_message(msg_);
    }

    // Initialize the rsc message
    // In client it should be called first
    // In server it should be called after decode()
    E_rsc_error init_encode() noexcept
    {
        E_rsc_error status = initialize_rsc_message(state_, &msg_);
        if (RscErrorSuccess != status)
        {
            return status;
        }

        // Add rpc id
        status = add_rsc_rpc_request(msg_, schema_->rpcId);

        return status;
    }

    E_rsc_error encode(uint8_t* buff, size_t buffSize, bool isRejectResponse = false) noexcept
    {
        if (nullptr == buff || 0 == buffSize)
        {
            return RscErrorInvalid;
        }

        if constexpr (Role == E_Rsc_Role::Client)
        {
            E_rsc_error status = increment_rsc_message_counter(&cntr_, state_);
            if (RscErrorSuccess != status)
            {
                return status;
            }
        }
        else if constexpr ((Role == E_Rsc_Role::Server))
        {
            if (isRejectResponse)
            {
                return encode_rsc_message(msg_, buff, &buffSize, cntr_, state_->hmacKey, RpcEncReject);
            }
            return encode_rsc_message(msg_, buff, &buffSize, cntr_, state_->hmacKey, RpcEncResponse);
        }

        return encode_rsc_message(msg_, buff, &buffSize, cntr_, state_->hmacKey);
    }

    size_t get_encoding_size() noexcept
    {
        size_t size;
        E_rsc_error status = encode_rsc_message(msg_, nullptr, &size, cntr_, state_->hmacKey);
        if (RscErrorSuccess != status)
        {
            return -1;
        }

        return size;
    }

    t_rsc_message_counter get_message_counter() noexcept { return cntr_; }

    void set_message_counter(t_rsc_message_counter cntr) noexcept { cntr_ = cntr; }

    E_rsc_error add_encode_nil_arg() noexcept { return add_rpc_arg(msg_, const_cast< t_rpc_arg* >(&nilArg_)); }

    // Template specialization for Server
    template< E_Rsc_Role T = Role, typename = std::enable_if_t< T == E_Rsc_Role::Server > > int add_encode_nil_args() noexcept
    {
        int status = RscErrorSuccess;
        for (auto i = 0; i < schema_->argCount; i++)
        {
            status |= add_encode_nil_arg();
        }

        return status;
    }

    E_rsc_error add_encode_arg(t_rpc_arg& arg) { return add_rpc_arg(msg_, &arg); }

    E_rsc_error add_encode_arg(uint8_t schemaIdx, uint64_t val, size_t size = -1) noexcept
    {
        uint8_t encodeArgCount = (Role == E_Rsc_Role::Client) ? schema_->argCount : schema_->argCount + 1;

        if (encodeArgCount <= schemaIdx)
        {
            return RscErrorBadArg;
        }

        size_t argSize = (-1 != size) ? size : ARG_SCHEMA_SIZE(schema_->argSchemaArray[schemaIdx]);
        t_rpc_arg arg  = MAKE_RPC_ARG(schema_->argSchemaArray[schemaIdx], val, argSize, RpcArgFlagsNone);

        return add_rpc_arg(msg_, &arg);
    }

    // Template specialization for Server
    template< E_Rsc_Role T = Role, typename = std::enable_if_t< T == E_Rsc_Role::Server > >
    E_rsc_error init_decode(uint8_t* buffer, size_t size, t_rpc_id& rpcId) noexcept
    {
        // Get information about the message
        t_rsc_message_info msgInfo;
        E_rsc_error status = rsc_message_get_info(buffer, size, &msgInfo);
        if (RscErrorSuccess != status)
        {
            return status;
        }

        // Save messageCounter for response encode
        cntr_ = msgInfo.messageCounter;

        // Check if the client id is known
        if (state_->clientId != msgInfo.clientId)
        {
            return RscErrorBadClientId;
        }

        // Check the validity of the message
        if (nullptr != checkRequestMutex_)
        {
            // We have to lock check_rsc_request_message in case of multiple thread listen for requests on same state
            std::lock_guard< std::mutex > lock(*checkRequestMutex_);

            status = check_rsc_request_message(buffer, size, state_);
        }
        else
        {
            status = check_rsc_request_message(buffer, size, state_);
        }

        if (RscErrorSuccess != status)
        {
            return status;
        }

        // Get the RPC ID
        status = rsc_message_get_rpc_id(buffer, size, &rpcId);

        return status;
    }

    // Template specialization for Server
    void set_schema(const RPC_message_schema* schema) { schema_ = schema; }

    E_rsc_error decode_without_schema(uint8_t* buffer, size_t size)
    {
        if (nullptr == buffer || (0 == size))
        {
            return RscErrorBadArg;
        }

        E_rsc_error status;

        if constexpr (Role == E_Rsc_Role::Client)
        {
            //  Check the response
            status = check_rsc_response_message(buffer, size, cntr_, state_);

            if (RscErrorSuccess != status)
            {
                return status;
            }

            // Get the RPC ID from the response and validate it
            t_rpc_id rpcId;
            status = rsc_message_get_rpc_id(buffer, size, &rpcId);
            if (RscErrorSuccess != status)
            {
                return status;
            }
            if (rpcId != schema_->rpcId)
            {
                return RscErrorInvalid;
            }
        }

        return decode_rsc_rpc_message(state_, buffer, size, nullptr, decodeFlags_, &decodedArgsSize_, &decodedArgs_);
    }

    t_rpc_arg* get_decoded_args() { return decodedArgs_; }

    size_t get_decoded_args_size() { return decodedArgsSize_; }

    E_rsc_error decode(uint8_t* buffer, size_t size) noexcept
    {
        if constexpr (Role == E_Rsc_Role::Server)
        {
            if (nullptr == schema_)
            {
                return RscErrorInvalid;
            }
        }

        if (nullptr == buffer || (0 == size))
        {
            return RscErrorBadArg;
        }

        E_rsc_error status;

        if constexpr (Role == E_Rsc_Role::Client)
        {
            //  Check the response
            status = check_rsc_response_message(buffer, size, cntr_, state_);

            if (RscErrorSuccess != status)
            {
                return status;
            }

            // Get the RPC ID from the response and validate it
            t_rpc_id rpcId;
            status = rsc_message_get_rpc_id(buffer, size, &rpcId);
            if (RscErrorSuccess != status)
            {
                return status;
            }
            if (rpcId != schema_->rpcId)
            {
                return RscErrorInvalid;
            }
        }

        decodedArgsSize_ = (Role == E_Rsc_Role::Client) ? schema_->argCount + 1 : schema_->argCount;
        size_t argCount;
        return decode_rsc_rpc_message(state_, buffer, size, schema_, decodeFlags_, &argCount, &decodedArgs_);
    }

    uint64_t get_decode_primitive_arg(int argIdx, size_t* size = nullptr) noexcept
    {
        if (decodedArgsSize_ <= argIdx)
        {
            return 0; // TODO think if to return error
        }

        if (nullptr != size)
        {
            *size = decodedArgs_[argIdx].argDataSize;
        }

        return decodedArgs_[argIdx].argData;
    }

    void get_decode_buffer_arg(int argIdx, void* buffer, size_t* size = nullptr) noexcept
    {
        if (decodedArgsSize_ <= argIdx)
        {
            return; // TODO think if return error
        }

        memcpy(buffer, (void*)decodedArgs_[argIdx].argData, decodedArgs_[argIdx].argDataSize);

        if (nullptr != size)
        {
            *size = decodedArgs_[argIdx].argDataSize;
        }
    }

    void get_decode_buffer_arg(int argIdx, std::string& buffer) noexcept
    {
        if (decodedArgsSize_ <= argIdx)
        {
            return; // TODO think if return error
        }

        buffer = (char*)decodedArgs_[argIdx].argData;
    }

    bool is_nil_decode_arg(int argIdx) { return (RpcNil == ARG_SCHEMA_TYPE(decodedArgs_[argIdx].argSchema)); }

    bool get_decode_bool_ret_arg() noexcept { return (bool)(decodedArgs_[schema_->argCount].argData); }

    // Template specialization for Server
    template< E_Rsc_Role T = Role, typename = std::enable_if_t< T == E_Rsc_Role::Server > >
    E_rsc_error add_encode_bool_ret_arg(bool ret) noexcept
    {
        t_rpc_arg arg =
            MAKE_RPC_ARG(schema_->returnValueSchema, (uint64_t)ret, ARG_SCHEMA_SIZE(schema_->returnValueSchema), RpcArgFlagsNone);

        return add_rpc_arg(msg_, &arg);
    }
};

#endif // _RSCPROTO_HELPER_H_
