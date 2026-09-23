/*
 * Copyright (C) 2005-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _PINGLUE_H
#define _PINGLUE_H

#include "glue_requests.h"

/**
 * @brief Pin Glue return values
 * 
 */
typedef enum
{
    E_PG_Success = 0,
    E_PG_InvalidArg,
    E_PG_UnexpectedMsg,
    E_PG_Internal,
    E_PG_OutOfMemory,
    E_PG_NoPin,
    E_PG_Timeout,
    E_PG_PinExit
} E_pin_glue_ret;

struct Pin_glue;
typedef struct Pin_glue t_pin_glue;

struct Pin_glue_service;
typedef struct Pin_glue_service t_pg_service;
typedef struct Pin_glue_script_fragment t_pg_script_fragment;

/**
 * @brief A structure holding service request data.
 * 
 */
typedef struct Service_request
{
    /** @brief channel to send the result callback message.
     if null, it will register the service default channel */
    t_pg_channel* resultChannel;

    /** @brief A string representing the name of the service */
    const char* serviceName;

    /** @brief The name of the callback function to call with the result */
    const char* callbackName;

    /** @brief Array of knob strings */
    const char** knobs;

    /** @brief The number of Knobs */
    size_t knobsSize;

    /** @brief Array of script fragments */
    const t_pg_script_fragment** scripts;

    /** @brief The number of scripts */
    size_t scriptsSize;

    /** @brief 5 Alpha Numeric characters for naming this service */
    const char name[6];
} t_pg_service_request;

/** @brief a function type that will handle the function call requests */
typedef void(t_pg_remote_cb)(t_pg_remote_func*, t_pg_remote_response*);

/**
 * @brief A structure holding arguments.
 * 
 */
typedef struct Args
{
    /** @brief The number of arguments */
    size_t argc;
    /** @brief Array of arguments */
    const char** argv;
} t_pg_args;

#ifdef __cplusplus
extern "C"
{
#endif

    /**
 * @brief Open a new Pin Glue instance.
 *        This function should be called first initialize and open the pin glue,
 *        then pg object will be passed to the next pinglue APIs.
 * 
 * @param[out]  pg            The object that will hold the pinglue data.     
 * @param[in]   pinArgs       A struct contains array of pin args, if null pin will run without args.
 * @param[in]   pgArgs        A struct contains the pin glue args. 
 *                            If you want to pass the service path then it's part pf pgArgs.
 * @param[in]   program       The program to run pin with. Should not be null.
 * @param[in]   programArgs   A struct contains the program args.
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_open(t_pin_glue** pg, const t_pg_args* pinArgs, const t_pg_args* pgArgs, const char* program,
                                const t_pg_args* programArgs);

    /**
 * @brief Start the program.
 * 
 *  @param[in] pg  the pinglue object.
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_start_program(t_pin_glue* pg);

    /**
 * @brief Wait for termination.
 * 
 *  @param[in]  pg        The pinglue object.   
 *  @param[in]  timeoutUS The timeout to wait in microseconds. 
 *                         0 will return immediately.
 *                         0xFFFFFFFF (-1) will wait indefinitely until pin will finish or an error occurs.                    
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_finish(t_pin_glue* pg, uint32_t timeoutUS);

    /**
 * @brief Close and release pinglue object.
 * 
 *  @param[in]  pg the pinglue object.
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_close(t_pin_glue* pg);

    /**
 * @brief Open a new service.
 *        The service to execute should be pass in serviceRequest parameter.
 * 
 *  @param[out] service        The object that will hold the service.
 *  @param[in]  pg             The pinglue object that the service will run on.
 *  @param[in]  serviceRequest The structure that holds the service request info.
 *                             It should be valid until pinglue_service_start call. 
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_service_open(t_pg_service** service, t_pin_glue* pg, const t_pg_service_request* serviceRequest);

    /**
 * @brief Start the service (should be called before start program).
 * 
 *  @param[in] service  service object.
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_service_start(t_pg_service* service);

    /**
 * @brief Close and release a service object.
 * 
 *  @param[in] service the service object.
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_service_close(t_pg_service* service);

    /**
 * @brief Wait for service to finish and return the service result.
 *        In case remote callback is registered on the main service channel you can also pass remoteCB to wait for it too 
 *        But function will wait for one request per call, handle it and return, if service result will arrive it will store
 *        the result in serviceResult and if remote callback will arrive it will call to remoteCB.
 * 
 *  @param[in]  service       The service object.
 *  @param[in]  timeoutUS     The timeout to wait in microseconds
 *                             0 will return immediately
 *                             0xFFFFFFFF (-1) will wait indefinitely until some message arrives or an error occurs.
 *  @param[in]  remoteCB      The remote callback to call when remote callback will arrived, 
 *                            NULL if NO remote call is registered/expected on service channel.
 *  @param[out] serviceResult The allocated service result data in case of PinGlueSuccess, null otherwise.
 *                            It should be free with pinglue_service_result_free.
 *                            If this function wait also for remoteCB, you can check serviceResult !=0 to know 
 *                            if serviceResult arrived (upon PinGlueSuccess).  
 * 
 * @return E_pin_glue_ret - PinGlueSuccess in case service result or remote callback arrived.
 *                            PinGlueTimeout if no message arrived and timeout occurred.
 */
    E_pin_glue_ret pinglue_service_wait(t_pg_service* service, uint32_t timeoutUS, t_pg_remote_cb remoteCB,
                                        t_pg_service_result** serviceResult);

    /**
 * @brief Free serviceResult that allocated in pinglue_service_wait.
 * 
 *  @param[in] serviceResult the service result.
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_service_result_free(t_pg_service_result* serviceResult);

    /**
 * @brief Open a new channel.
 * 
 *  @param[out] channel the output channel.
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_open_channel(t_pg_channel** channel);

    /**
 * @brief Close channel.
 * 
 *  @param[in] channel the channel to close.
 * 
 * @return E_pin_glue_ret 
 */
    E_pin_glue_ret pinglue_close_channel(t_pg_channel* channel);

    /**
 * @brief Wait for remote callback request on channel that registered for it.
 * 
 *  @param[in]  pg            pinglue object.
 *  @param[in]  channel       channel.
 *  @param[in]  timeoutUS     timeout to wait in microseconds.
 *                             0 will return immediately 
 *                             0xFFFFFFFF (-1) will wait indefinitely until some message arrives or an error occurs.
 *  @param[in]  remoteCB      remote function callback to call when remote callback will arrived.
 * 
 * @return E_pin_glue_ret - PinGlueSuccess if callback arrived and handled properly (response sent successfully)
 *                            PinGlueTimeout in case no callback arrived.
 */
    E_pin_glue_ret pinglue_channel_wait(t_pin_glue* pg, t_pg_channel* channel, uint32_t timeoutUS, t_pg_remote_cb remoteCB);

    /**
 * @brief Register a remote function call in script fragment. 
 * 
 *  @param[in]  channel         The channel that will receive the remote function calls.
 *                              If NULL it will register the default service channel.
 *  @param[in]  scriptFuncName  The script function name that will be replaced to remote function call.
 *  @param[in]  remoteFuncName  The remote function name.
 *  @param[in]  noBlock         Specifies whether the server should send remote function call messages in a non-blocking, asynchronous manner.
 *                              When set to true, it also signifies that the server will ignore the return value of the remote function.
 * 
 * @return t_pg_script_fragment*  A pointer to new script fragment, NULL on failure.
 */
    t_pg_script_fragment* GLUE_register_remote_function(t_pg_channel* channel, const char* scriptFuncName,
                                                        const char* remoteFuncName, bool noBlock);

    /**
 * @brief Create a server script.  
 * 
 *  @param[in]  script       The script.
 *  @param[in]  scriptSize   The script size.
 * 
 * @return t_pg_script_fragment*  A pointer to new script fragment, NULL on failure.
 */
    t_pg_script_fragment* GLUE_server_script(const char* script, size_t scriptSize);

    /**
 * @brief Free script fragment.  
 * 
 *  @param[in]  fragment   The fragment to free.
 * 
 * @return E_pin_glue_ret
 */
    E_pin_glue_ret pinglue_script_fragment_free(t_pg_script_fragment* fragment);

#ifdef __cplusplus
}
#endif

#endif // _PINGLUE_H