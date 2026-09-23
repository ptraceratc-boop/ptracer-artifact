/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

#include "../include/pinglue.h"

t_pg_service_result* serviceResult = NULL;
volatile bool remoteFuncArrived    = false;

// Global flag to signal thread termination
volatile bool terminateFlag = false;

typedef struct Service_listen_info
{
    t_pg_service* service;
    t_pg_channel* remoteFuncChannel;
    t_pin_glue* pg;
} t_service_listen_info;

const char* RPC_ARG_STR  = "HELLO";
const size_t STATE_VALUE = 7;

void free_rpc_arg(t_rpc_arg* arg, void* state)
{
    printf("test_libpinglue: In free_rpc_arg, arg: %s\n", (const char*)(uintptr_t)arg->argData);
    assert(0 == strcmp((const char*)(uintptr_t)arg->argData, RPC_ARG_STR));
    assert(STATE_VALUE == (size_t)state);
    free((void*)(uintptr_t)arg->argData);
}

void remote_func_cb(t_pg_remote_func* remoteFunc, t_pg_remote_response* resp)
{
    // print the remote func name
    printf("test_libpinglue: In remote_func_cb, functionName: %s\n", remoteFunc->functionName);
    assert(0 == strcmp(remoteFunc->functionName, "remote_bar"));
    printf("test_libpinglue: In remote_func_cb, name: %s\n", remoteFunc->name);
    assert(0 == strcmp(remoteFunc->name, "instc"));

    bool floatF = false, boolF = false, intF = false, uintF = false, strF = false, nilF = false;

    // All supported arguments types are passed once in
    // inscount service for this test in call: foo("Shuki", 2.5, -1, 7, True, False, None)
    // we test here that all arrived with the expected values and in the expected position.
    for (size_t i = 0; i < remoteFunc->request.argsSize; i++)
    {
        uint64_t argData = remoteFunc->request.args[i].argData;
        assert(i < 7);
        switch (ARG_SCHEMA_TYPE(remoteFunc->request.args[i].argSchema))
        {
            case RpcBuffer:
                assert(0 == strcmp((const char*)(uintptr_t)argData, "Shuki"));
                assert(0 == i);
                strF = true;
                break;
            case RpcBoolean:
                assert((4 == i) || (5 == i));
                if (4 == i)
                {
                    assert(true == (bool)argData);
                }
                if (5 == i)
                {
                    assert(false == (bool)argData);
                }
                boolF = true;
                break;
            case RpcFloat:
                assert(1 == i);
                assert(2.5 == (*(double*)(&(argData))));
                floatF = true;
                break;
            case RpcInt:
                assert(2 == i);
                assert(-1 == (int64_t)argData);
                intF = true;
                break;
            case RpcUInt:
                assert(3 == i);
                assert(7 == argData);
                uintF = true;
                break;
            case RpcNil:
                assert(6 == i);
                nilF = true;
                break;
            default:
                assert(0);
                break;
        }
    }

    assert(strF && boolF && floatF && intF && uintF && nilF);

    char* retStr  = strdup(RPC_ARG_STR);
    resp->handled = true;
    resp->ret = (t_rpc_arg)MAKE_RPC_ARG(BUFFER_ARG_SCHEMA(), (uint64_t)(uintptr_t)retStr, 1 + strlen(retStr), RpcArgFlagsNone);
    resp->deleter = free_rpc_arg;
    resp->state   = (void*)STATE_VALUE;
    resp->cancel  = false;

    remoteFuncArrived = true;
}

void* thread_service_listen_func(void* arg)
{
    t_service_listen_info* serviceListenInfo = (t_service_listen_info*)arg;
    t_pg_service* service                    = serviceListenInfo->service;

    int res;
    while (!terminateFlag && 0 == serviceResult)
    {
        pinglue_service_wait(service, 1, remote_func_cb, &serviceResult);
        if (NULL != serviceListenInfo->remoteFuncChannel)
        {
            pinglue_channel_wait(serviceListenInfo->pg, serviceListenInfo->remoteFuncChannel, 1, remote_func_cb);
        }
    }

    printf("test_libpinglue: serviceResult->funcName: %s\n", serviceResult->funcName);
    printf("test_libpinglue: serviceResult->result: %s\n", serviceResult->result);
    printf("test_libpinglue: serviceResult->name: %s\n", serviceResult->name);

    assert(0 == strcmp(serviceResult->name, "instc"));
    assert(0 == strcmp(serviceResult->funcName, "inscount_result"));

    res = pinglue_service_result_free(serviceResult);
    printf("test_libpinglue: Res of pinglue_service_result_free: %d\n", res);
    assert(E_PG_Success == res);
    return NULL;
}

bool test_basic(const char* program, const t_pg_args* programArgs, t_pg_channel* remoteCBChannel)
{
    serviceResult     = NULL;
    remoteFuncArrived = false;
    terminateFlag     = false;

    // assign test services path
    const char* relativePath = "test/Services";
    char fullPath[4096];
    assert(0 != realpath(relativePath, fullPath));

    const char* pgArguments[] = {"-services-path", fullPath};
    t_pg_args pinglueArgs     = {.argc = 2, .argv = pgArguments};

    // Can be used for debugging (just place what you need). This is what we have for now until we improve it 
    // by supporting command line arguments.
    // const char* pinArguments[] = {"-xyzzy", "-pause_tool", "1", "-mesgon", "log_injector"};
    // t_pg_args pinlueArgs     = {.argc = 5, .argv = pinArguments};

    // Open pinglue instance
    t_pin_glue* pg = 0;
    int res        = pinglue_open(&pg, 0/*&pinlueArgs*/, &pinglueArgs, program, programArgs);
    printf("test_libpinglue: Res of pinglue_open: %d\n", res);
    assert(E_PG_Success == res);

    t_pg_script_fragment* scriptFragment1 = GLUE_register_remote_function(remoteCBChannel, "foo", "remote_bar", false);
    assert(NULL != scriptFragment1);
    const char* script2 = "print('message2!!!')";
    // TODO validate message2!!! and message3!!! are indeed printed to terminal.
    t_pg_script_fragment* scriptFragment2 = GLUE_server_script(script2, strlen(script2));
    assert(NULL != scriptFragment2);
    const char* script3                   = "print('message3!!!')";
    t_pg_script_fragment* scriptFragment3 = GLUE_server_script(script3, strlen(script3));
    assert(NULL != scriptFragment3);

    const t_pg_script_fragment* scripts[] = {scriptFragment1, scriptFragment2, scriptFragment3};

    t_pg_service_request serviceRequest = {0, "inscount", "inscount_result", 0, 0, scripts, 3, "instc"};

    // Open service
    t_pg_service* service;
    res = pinglue_service_open(&service, pg, &serviceRequest);
    printf("test_libpinglue: Res of pinglue_service_open: %d\n", res);
    assert(E_PG_Success == res);

    assert(E_PG_Success == pinglue_script_fragment_free(scriptFragment1));
    assert(E_PG_Success == pinglue_script_fragment_free(scriptFragment2));
    assert(E_PG_Success == pinglue_script_fragment_free(scriptFragment3));

    // Create a thread to listen on remote function callbacks
    pthread_t serviceThread;
    t_service_listen_info serviceListenInfo = {service, remoteCBChannel, pg};
    if (pthread_create(&serviceThread, NULL, thread_service_listen_func, (void*)&serviceListenInfo) != 0)
    {
        perror("pthread_create");
        exit(EXIT_FAILURE);
    }

    // Start service
    res = pinglue_service_start(service);
    printf("test_libpinglue: Res of pinglue_service_start: %d\n", res);
    assert(E_PG_Success == res);

    // Start program
    res = pinglue_start_program(pg);
    printf("test_libpinglue: Res of pinglue_start_program: %d\n", res);
    assert(E_PG_Success == res);

    // Wait pin to finish
    res = pinglue_finish(pg, -1);
    printf("test_libpinglue: Res of pinglue_finish: %d\n", res);
    assert(E_PG_Success == res);

    // terminates remote_func_thread.
    terminateFlag = true;

    if (0 != pthread_join(serviceThread, NULL))
    {
        printf("test_libpinglue: Error joining serviceThread\n");
        return false;
    }

    // close service
    res = pinglue_service_close(service);
    printf("test_libpinglue: Res of pinglue_service_close: %d\n", res);
    assert(E_PG_Success == res);

    // close pinglue
    res = pinglue_close(pg);
    printf("test_libpinglue: Res of pinglue_close: %d\n", res);
    assert(E_PG_Success == res);

    assert(NULL != serviceResult);
    assert(true == remoteFuncArrived);

    return true;
}

// test A: 1. Basic test when listening for remote function callback with service channel
//         2. Basic test when listening for remote function in a different channel
bool test_a(const char* program, const t_pg_args* programArgs)
{
    // Basic test when listening for remote function callback with service channel
    assert(test_basic(program, programArgs, NULL));

    // Basic test when listening for remote function in a different channel
    t_pg_channel* channel;
    int res = pinglue_open_channel(&channel);
    printf("test_libpinglue: Res of pinglue_open_channel: %d\n", res);
    assert(E_PG_Success == res);

    assert(test_basic(program, programArgs, channel));

    res = pinglue_close_channel(channel);
    printf("test_libpinglue: Res of pinglue_close_channel: %d\n", res);
    assert(E_PG_Success == res);

    return true;
}

int main(int argc, char** argv)
{
    assert(2 == argc);
    
    char test_path[4096] = {};
    int bits = 0;
    const char* PINKIT   = getenv("GLUE_PIN_KIT");
    const char* TARGET   = argv[1];

    assert(PINKIT);

    printf("Testing libpinglue for %s. PINKIT=%s\n", argv[1], PINKIT);

    if(0 == strcmp("ia32", TARGET))
    {
        bits = 32;
    }
    else if(0 == strcmp("intel64", TARGET))
    {
        bits = 64;
    }

    assert(64 == bits || 32 == bits);

    printf("test_libpinglue: Running %d-bits app tests\n", bits);
    const char* arguments[] = {"test_libpinglue.c", "test_libpinglue.c.copy"};
    t_pg_args programArgs   = {.argc = 2, .argv = arguments};

    snprintf(test_path, sizeof(test_path), "%s/source/tools/Utils/obj-%s/cp-pin.exe", PINKIT, TARGET);

    assert(test_a(test_path, &programArgs));
    remove("test_libpinglue.c.copy");

    printf("test_libpinglue: All tests finished successfully, CONGRATS!! \n");

    return 0;
}
