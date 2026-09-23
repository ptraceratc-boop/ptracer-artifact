/*
 * Copyright (C) 2005-2025 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#include "pin.H"
#include "gluepy.h"
#include <list>
#include <vector>
#include <map>
#include <fstream>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>
#include <regex>
#include <optional>

#include "Glue.h"
#include "Bind/Glue_PIN.h"

#include <rscproto.h>
#include <txll_utils.h>
#include "Glue_messages.h"
#include "pinglue_common.h"
#include "rscproto_helper.h"
#include "Glue_channel_pool.h"
#include "Glue_async_msg.h"
#include "Glue_timer_thread.h"

std::mutex Pinglue_GIL::GIL_; // Define the GIL declared in gluepy.h

namespace
{

/**
 * @brief A structure holding pinglueserver state data.
 * 
 */
struct Tool_state
{
    /** @brief Channel for getting requests from pinglue client*/
    glue::t_unique_channel serverChannel;
    /** @brief A channel pool to use when sending messages on txll */
    std::unique_ptr< glue::Client_channel_pool > clientChannelPool;
    /** @brief  state to use when getting requests from pinglue client */
    t_rsc_state serverState;
    /** @brief  state to use when sending requests to pinglue client */
    t_rsc_state clientState;
    /** @brief  The seed to use for RSC proto messages (created by pinglue client) */
    t_rsc_key_seed seed;
    /** @brief A pool of asynchronous messages for reuse. */
    std::unique_ptr< glue::Async_msg_pool > asyncMsgPool;
    /** @brief A queue of asynchronous messages to send. */
    glue::Async_msg_queue asyncMsgQueue;
    /** @brief The thread responsible for sending asynchronous messages. */
    std::thread asyncMsgWorkerThread;
    /** @brief The manager responsible for Glue timers. */
    std::unique_ptr< glue::Timer_thread_manager > timerManager;

    /**
     * @brief Destructor for the Tool_state structure.
     * 
     * Signals the asynchronous message worker thread to exit by pushing a nullptr onto the queue
     * and then joins the thread to ensure clean shutdown and resource deallocation.
     */
    ~Tool_state()
    {
        asyncMsgQueue.push(nullptr);

        if (asyncMsgWorkerThread.joinable())
        {
            asyncMsgWorkerThread.join();
        }
    }
};

/**
 * @brief A structure holding a single pocketpy VM state data.
 * 
 */
struct VM_state
{
    /** @brief The pocketpy vm instance used along the tool */
    std::unique_ptr< VM > vm;
    /** @brief The pin module for binding Pin APIs */
    PyObject* pinModule;
};

// Pingluetool and pocketpy related variables

/** @brief pinglueserver state data */
Tool_state toolState;

/** @brief pocketpy VM state data 
 *  Currently we hold only one VM, in the future we may have a VM per thread. 
*/
VM_state toolVM;

/** @brief RCS proto window size */
constexpr uint32_t WINDOW_SIZE = 128;

/** @brief A key we use to save the result callback name for each service module */
constexpr const char* RESULT_CB_NAME = "resultCallbackName";
/** @brief A key we use to save the result callback address for each service module */
constexpr const char* RESULT_CB_ADDRESS = "resultCallbackAddress";
/** @brief A key we use to save the 5 Alpha Numeric characters of the service */
constexpr const char* SERVICE_UNIQUE_NAME = "name";

#define PY_VAR(x) py_var(toolVM.vm.get(), x)

/**
 * @brief KNOB for running the tool for testing a pintool written in script.
 *        if this knob is not passed the tool will run in Messages/Requests mode (the main mode).
 */
KNOB< std::string > KnobPythonScript(KNOB_MODE_WRITEONCE, "pintool", "gluetool", "", "the PinGlue Python script");

/** @brief Pin Glue Server Knobs for main tool mode: */
KNOB< UINT64 > KnobServerTxid(KNOB_MODE_WRITEONCE, "pintool", "glue-server-txid", "0", "ID of the PinTx-LL main server channel");
KNOB< UINT32 > KnobClientRSCid(KNOB_MODE_WRITEONCE, "pintool", "rsc-client-id", "0", "RSC Client ID");
KNOB< std::string > KnobRscHkdfSeed(KNOB_MODE_WRITEONCE, "pintool", "rsc-hkdf-seed", "0",
                                    "256 bit random seed for the RSC HKDF algorithm");
KNOB< std::string > KnobServicesDir(KNOB_MODE_WRITEONCE, "pintool", "services-dir", "Services",
                                    "A path to a folder containing service scripts");
/** @brief Default response timeout is 1 second */
KNOB< UINT32 > KnobResponseTimeout(KNOB_MODE_WRITEONCE, "pintool", "response-timeout", "1000000",
                                   "Timeout period for server to wait for client response in microseconds");
/** @brief Default channel pool capacity is 10 */
KNOB< UINT32 > KnobChannelPoolCapacity(KNOB_MODE_WRITEONCE, "pintool", "channel-pool-capacity", "10",
                                       "The capacity of the channel pool");
/** @brief Default async messages pool size is 10 */
KNOB< UINT32 > KnobAsyncMsgPoolSize(KNOB_MODE_WRITEONCE, "pintool", "async-msg-pool-size", "10",
                                    "The size of async messages pool");

/**
 * @brief A structure holding the script analysis callback information
 */
typedef struct Script_anl_cb_info
{
    /** @brief A pointer to the Python object of the analysis callback. */
    const PyObject* cb;
    /** @brief A pointer to a list of argument types that the callback function expects. */
    const glue::t_iarg_type_list* argsTypeList;
} t_script_anl_cb_info;

/**
 * @brief A structure holding the remote function callback information
 */
struct Remote_func_call_info
{
    /** @brief The remote channel ID. */
    UINT64 channelId;
    /** @brief The name of the remote function. */
    const char* remoteFuncName;
    /** @brief A unique name (like service unique name). */
    const char* name;
    /** @brief Flag indicating if the remote function call should be non-blocking. */
    bool noBlock;
};

/**
 * @brief A cache for saving same glue::t_iarg_type_list that allocated in instrumentation time 
 *  and will be pass to the same analysis callback signature (same variadic types) through t_script_anl_cb_info.
 */
std::set< glue::t_iarg_type_list > iargTypesListsCache;

/**
 * @brief A map for saving for each pair of <python analysis CB, iarg_type_list> the t_script_anl_cb_info to pass to the analysis routine.
 *  and will be pass to the same analysis callback signature (same variadic types) through t_script_anl_cb_info.
 */
typedef std::map< std::pair< const PyObject*, const glue::t_iarg_type_list* >, std::unique_ptr< t_script_anl_cb_info > >
    t_script_anl_CB_info_map;
// NOTE: We may give up on t_script_anl_cb_info and send to analysis callback only the std::pair ptr,
// and if so also no need a map but only a set of std::pair< const PyObject*, const glue::t_iarg_type_list* >.
// but for now we prefer to send t_script_anl_cb_info to allow the ability to send more stuff in the future
// through this struct (from instrumentation to analysis).

/**
 * @brief scriptAnlCBInfoMap is a cache for saving same t_script_anl_cb_info that allocated in instrumentation time 
 *  and will pass to the same analysis callback signature (same variadic types).
 */
t_script_anl_CB_info_map scriptAnlCBInfoMap;

/**
 * @brief Helper function to retrieve the cached glue::t_iarg_type_list ptr.
 * 
 * @param[in] iargTypesList the glue::t_iarg_type_list to retrieve from cache.
 * 
 * @return  The cached glue::t_iarg_type_list ptr.
 * 
 */
const glue::t_iarg_type_list* gluetool_get_cached_IARG_type_list(const glue::t_iarg_type_list& iargTypesList)
{
    // Use emplace to insert or retrieve
    auto result = iargTypesListsCache.emplace(iargTypesList);

    return &(*result.first);
}

/**
 * @brief Helper function to retrieve the cached t_script_anl_cb_info ptr that contains the required input ptrs in
 *  t_script_anl_cb_info->cb && in t_script_anl_cb_info->iargTypesList.
 * 
 * @param[in] cb            The required cb ptr that will be reside in t_script_anl_cb_info->cb.
 * @param[in] iargTypesList The required iargTypesList that will be reside in t_script_anl_cb_info->iargTypesList.
 * 
 * @return  The cached t_script_anl_cb_info ptr.
 * 
 */
const t_script_anl_cb_info* gluetool_get_script_anl_cb_info(PyObject* cb, const glue::t_iarg_type_list& iargTypesList)
{
    const auto cachedTypesListPtr = gluetool_get_cached_IARG_type_list(iargTypesList);

    auto keyPair = std::make_pair(cb, cachedTypesListPtr);

    // Check if the desired t_script_anl_cb_info ptr already exists (and allocated)
    auto it = scriptAnlCBInfoMap.find(keyPair);

    if (scriptAnlCBInfoMap.end() == it)
    {
        // Key doesn't exist, allocate and insert a new t_script_anl_cb_info
        try
        {
            scriptAnlCBInfoMap[keyPair] = std::make_unique< t_script_anl_cb_info >(t_script_anl_cb_info {cb, cachedTypesListPtr});
        }
        catch (const std::bad_alloc& e)
        {
            std::cerr << "ERROR! Out Of Memory!" << std::endl;
            exit(1);
        }

        return scriptAnlCBInfoMap[keyPair].get();
    }

    // Key already exists, retrieve the allocated t_script_anl_cb_info
    return it->second.get();
}

/**
 * @brief Analysis callback function template for gluetool.
 *
 * This function template is used as a callback for gluetool analysis callbacks.
 * It can be specialized to either return an ADDRINT value or void, depending on the
 * IsReturnADDRINT template parameter.
 *
 * @tparam IsReturnADDRINT  A boolean template parameter that determines the return type.
 *                          If true, the function returns an ADDRINT, otherwise it returns void.
 * @param [in] scriptCbInfo A pointer to a t_script_anl_cb_info structure containing callback information.
 * @param [in] ...              Variable arguments that are passed to the callback function.
 * @return                  Returns an ADDRINT if IsReturnADDRINT is true, otherwise returns void.
 */
template< BOOL IsReturnADDRINT >
typename std::conditional< IsReturnADDRINT, ADDRINT, VOID >::type gluetool_analysis_callback(t_script_anl_cb_info* scriptCbInfo,
                                                                                             ...)
{
    va_list args;
    va_start(args, scriptCbInfo);

    std::vector< PyObject* > pyArgs;
    pyArgs.reserve((*(scriptCbInfo->argsTypeList)).size());

    Scoped_GIL_guard lock;
    for (IARG_TYPE type : *(scriptCbInfo->argsTypeList))
    {
        pyArgs.emplace_back(glue::get_py_arg(toolVM.vm.get(), args, type));
    }

    va_end(args);

    try
    {
        if constexpr (IsReturnADDRINT)
        {
            return py_cast< ADDRINT >(toolVM.vm.get(),
                                      glue::vm_call_unpack(toolVM.vm.get(), (PyObject*)scriptCbInfo->cb, pyArgs));
        }
        else
        {
            (VOID) glue::vm_call_unpack(toolVM.vm.get(), (PyObject*)scriptCbInfo->cb, pyArgs);
        }
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

/**
 * @brief The gluetool analysis callback that calls to script analysis callback when it defined without params.
 * 
 * @param[in] scriptCB script callback.
 * 
 */
template< BOOL IsReturnADDRINT >
typename std::conditional< IsReturnADDRINT, ADDRINT, VOID >::type gluetool_analysis_callbackNP(VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        if constexpr (IsReturnADDRINT)
        {
            return py_cast< ADDRINT >(toolVM.vm.get(), toolVM.vm->call((PyObject*)scriptCB));
        }
        else
        {
            (void)toolVM.vm->call((PyObject*)scriptCB);
        }
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

/** @brief Primary template for Is_Index_Specialization struct 
 *         If T is not a specialization of INDEX, value is false
*/
template< typename T > struct Is_Index_Specialization : std::false_type
{
};

/**
 * @brief Partial specialization for Is_Index_Specialization struct
 *        If T is INDEX<N>, value is true
 */
template< int N > struct Is_Index_Specialization< INDEX< N > > : std::true_type
{
};

/**
 * @brief The binding callback that will be called for <X>_InsertCall Pin APIs.
 * 
 * This template function is used to insert calls for <X>_InsertCall Pin APIs. 
 * The template parameters are used to specify the class type (`ClassType`),
 * the return type of the function pointer (`RetType`), and the function pointer type (`InsertFuncPtr`).
 * 
 * @tparam ClassType The class type for the callback (first arg).
 * @tparam RetType The return type of the function pointer.
 * @tparam InsertFuncPtr The type of the function pointer for the insertion call.
 * 
 * @param vm   The pocketpy virtual machine context.
 * @param args The arguments for the insertion call.
 * 
 * @return PyObject* the PIN _Insert API return value
 */
template< class ClassType, class RetType, RetType InsertFuncPtr(ClassType, IPOINT, AFUNPTR, ...) >
PyObject* gluetool_insert_call(VM* vm, ArgsView args)
{
    // args[0] - ClassType/INDEX (e.g TRACE/BBL), args[1] - IPOINT, args[2] - PyObject* (scriptCB), args[3] - *args (python variadic args)
    ClassType obj;
    if constexpr (Is_Index_Specialization< ClassType >::value)
    {
        obj.index = py_cast< INT32 >(vm, args[0]);
    }
    else if constexpr (std::is_same_v< ClassType, TRACE >)
    {
        obj = py_cast< TRACE >(vm, args[0]);
    }
    else
    {
        static_assert(sizeof(ClassType) == 0, "Unhandled ClassType");
    }

    Tuple pyVariadicArgs = py_cast< Tuple >(vm, args[3]);
    auto varSize         = pyVariadicArgs.size();

    auto res                = vm->None;
    bool isAnlReturnAddrint = std::is_same_v< decltype(InsertFuncPtr), decltype(TRACE_InsertIfCall) > ||
                              std::is_same_v< decltype(InsertFuncPtr), decltype(BBL_InsertIfCall) > ||
                              std::is_same_v< decltype(InsertFuncPtr), decltype(INS_InsertIfCall) >;

    // Optimization in case of python analysis routine define without params.
    if (0 == varSize)
    {
        if constexpr (std::is_same_v< RetType, VOID >)
        {
            // TODO see how to replace the following if else here and same in more 3 places in this function,
            // where the all change in if else on isAnlReturnAddrint that gluetool_analysis_callback called with template arg true or false
            // but since isAnlReturnAddrint could also define dynamic (handle_insert_variadic_args return value) it probably
            // need to move to another function or find another solution.
            if (isAnlReturnAddrint)
            {
                InsertFuncPtr(obj, py_cast< IPOINT >(vm, args[1]), (AFUNPTR)gluetool_analysis_callbackNP< TRUE >, IARG_PTR,
                              args[2], IARG_END);
            }
            else
            {
                InsertFuncPtr(obj, py_cast< IPOINT >(vm, args[1]), (AFUNPTR)gluetool_analysis_callbackNP< FALSE >, IARG_PTR,
                              args[2], IARG_END);
            }
        }
        else
        {
            if (isAnlReturnAddrint)
            {
                res = py_var(vm, InsertFuncPtr(obj, py_cast< IPOINT >(vm, args[1]), (AFUNPTR)gluetool_analysis_callbackNP< TRUE >,
                                               IARG_PTR, args[2], IARG_END));
            }
            else
            {
                res = py_var(vm, InsertFuncPtr(obj, py_cast< IPOINT >(vm, args[1]),
                                               (AFUNPTR)gluetool_analysis_callbackNP< FALSE >, IARG_PTR, args[2], IARG_END));
            }
        }
        return res;
    }

    IARGLIST pinVariadicArgs = IARGLIST_Alloc();
    glue::t_iarg_type_list typesList;

    isAnlReturnAddrint =
        isAnlReturnAddrint || glue::handle_insert_variadic_args(vm, varSize, pyVariadicArgs, pinVariadicArgs, typesList);
    const auto scriptCbInfoPtr = gluetool_get_script_anl_cb_info(args[2], typesList);

    if constexpr (std::is_same_v< RetType, VOID >)
    {
        if (isAnlReturnAddrint)
        {
            InsertFuncPtr(obj, py_cast< IPOINT >(vm, args[1]), (AFUNPTR)gluetool_analysis_callback< TRUE >, IARG_PTR,
                          scriptCbInfoPtr, IARG_IARGLIST, pinVariadicArgs, IARG_END);
        }
        else
        {
            InsertFuncPtr(obj, py_cast< IPOINT >(vm, args[1]), (AFUNPTR)gluetool_analysis_callback< FALSE >, IARG_PTR,
                          scriptCbInfoPtr, IARG_IARGLIST, pinVariadicArgs, IARG_END);
        }
    }
    else
    {
        if (isAnlReturnAddrint)
        {
            res = py_var(vm, InsertFuncPtr(obj, py_cast< IPOINT >(vm, args[1]), (AFUNPTR)gluetool_analysis_callback< TRUE >,
                                           IARG_PTR, scriptCbInfoPtr, IARG_IARGLIST, pinVariadicArgs, IARG_END));
        }
        else
        {
            res = py_var(vm, InsertFuncPtr(obj, py_cast< IPOINT >(vm, args[1]), (AFUNPTR)gluetool_analysis_callback< FALSE >,
                                           IARG_PTR, scriptCbInfoPtr, IARG_IARGLIST, pinVariadicArgs, IARG_END));
        }
    }

    IARGLIST_Free(pinVariadicArgs);

    return res;
}

// Like previous function but for PIN_InsertCallProbed(ADDRINT addr, AFUNPTR funptr, ...) that is without IPOINT param.
// We may join this function with previous function by adding type trait struct to check if ClassType supports IPOINT
// but for now for simplicity/readability we split it to 2 functions.
PyObject* gluetool_pin_insert_call_probed(VM* vm, ArgsView args)
{
    // args[0] - ADDRINT, args[1] - PyObject* (scriptCB), args[2] - *args (python variadic args)
    ADDRINT addr = py_cast< ADDRINT >(vm, args[0]);

    Tuple pyVariadicArgs = py_cast< Tuple >(vm, args[2]);
    auto varSize         = pyVariadicArgs.size();

    // Optimization in case of python analysis routine define without params.
    if (0 == varSize)
    {
        BOOL res = PIN_InsertCallProbed(addr, (AFUNPTR)gluetool_analysis_callbackNP< FALSE >, IARG_PTR, args[1], IARG_END);
        return py_var(vm, res);
    }

    IARGLIST pinVariadicArgs = IARGLIST_Alloc();
    glue::t_iarg_type_list typesList;

    glue::handle_insert_variadic_args(vm, varSize, pyVariadicArgs, pinVariadicArgs, typesList);
    const auto scriptCbInfoPtr = gluetool_get_script_anl_cb_info(args[1], typesList);

    BOOL res = PIN_InsertCallProbed(addr, (AFUNPTR)gluetool_analysis_callback< FALSE >, IARG_PTR, scriptCbInfoPtr, IARG_IARGLIST,
                                    pinVariadicArgs, IARG_END);

    IARGLIST_Free(pinVariadicArgs);

    return py_var(vm, res);
}

// Special function for RTN_InsertCallProbedEx
PyObject* gluetool_rtn_insert_call_probedEx(VM* vm, ArgsView args)
{
    // args[0] - RTN, args[1] - IPOINT, args[2] - PROBE_MODE, args[3] - PyObject* (scriptCB), args[4] - *args (python variadic args)
    RTN rtn;
    rtn.index = py_cast< INT32 >(vm, args[0]);

    Tuple pyVariadicArgs = py_cast< Tuple >(vm, args[4]);
    auto varSize         = pyVariadicArgs.size();

    // Optimization in case of python analysis routine define without params.
    if (0 == varSize)
    {
        BOOL res = RTN_InsertCallProbedEx(rtn, py_cast< IPOINT >(vm, args[1]), py_cast< PROBE_MODE >(vm, args[2]),
                                          (AFUNPTR)gluetool_analysis_callbackNP< FALSE >, IARG_PTR, args[3], IARG_END);
        return py_var(vm, res);
    }

    IARGLIST pinVariadicArgs = IARGLIST_Alloc();
    glue::t_iarg_type_list typesList;

    glue::handle_insert_variadic_args(vm, varSize, pyVariadicArgs, pinVariadicArgs, typesList);
    const auto scriptCbInfoPtr = gluetool_get_script_anl_cb_info(args[3], typesList);

    BOOL res = RTN_InsertCallProbedEx(rtn, py_cast< IPOINT >(vm, args[1]), py_cast< PROBE_MODE >(vm, args[2]),
                                      (AFUNPTR)gluetool_analysis_callback< FALSE >, IARG_PTR, scriptCbInfoPtr, IARG_IARGLIST,
                                      pinVariadicArgs, IARG_END);

    IARGLIST_Free(pinVariadicArgs);

    return py_var(vm, res);
}

// Special function for RTN_ReplaceSignature.. APIs
template< BOOL IsProbe = FALSE, BOOL IsProbeEx = FALSE > PyObject* gluetool_rtn_replace_signature(VM* vm, ArgsView args)
{
    // args of RTN_ReplaceSignature and RTN_ReplaceSignatureProbed:
    //  args[0] - RTN, args[1] - PyObject* (scriptCB), args[2] - proto, args[3] - *args (python variadic args - One IARG_TYPE must be IARG_PROTOTYPE of the original application function)
    // args of RTN_ReplaceSignatureProbedEx:
    //  args[0] - RTN, args[1] - PROBE_MODE, args[2] - PyObject* (scriptCB), args[3] - proto, args[4] - *args

    RTN rtn;
    rtn.index = py_cast< INT32 >(vm, args[0]);

    PyObject* scriptCB   = IsProbeEx ? args[2] : args[1];
    auto protoObj        = IsProbeEx ? py_cast< glue::Glue_Proto* >(vm, args[3]) : py_cast< glue::Glue_Proto* >(vm, args[2]);
    Tuple pyVariadicArgs = IsProbeEx ? py_cast< Tuple >(vm, args[4]) : py_cast< Tuple >(vm, args[3]);
    auto varSize         = pyVariadicArgs.size();

    IARGLIST pinVariadicArgs = IARGLIST_Alloc();
    glue::t_iarg_type_list typesList;
    IARGLIST_AddArguments(pinVariadicArgs, IARG_PROTOTYPE, protoObj->proto, IARG_END);
    glue::handle_insert_variadic_args(vm, varSize, pyVariadicArgs, pinVariadicArgs, typesList);
    const auto scriptCbInfoPtr = gluetool_get_script_anl_cb_info(scriptCB, typesList);
    AFUNPTR res                = nullptr;

    bool isAnlReturnAddrint = (protoObj->retParg->_parg != PARG_VOID);
    if constexpr (IsProbe)
    {
        // RTN_xxx acquire the client lock - prevent deadlock
        Scoped_GIL_temp_release tempUnlock;
        if constexpr (IsProbeEx)
        {
            if (isAnlReturnAddrint)
            {
                res = RTN_ReplaceSignatureProbedEx(rtn, py_cast< PROBE_MODE >(vm, args[1]),
                                                   (AFUNPTR)gluetool_analysis_callback< TRUE >, IARG_PTR, scriptCbInfoPtr,
                                                   IARG_IARGLIST, pinVariadicArgs, IARG_END);
            }
            else
            {
                res = RTN_ReplaceSignatureProbedEx(rtn, py_cast< PROBE_MODE >(vm, args[1]),
                                                   (AFUNPTR)gluetool_analysis_callback< FALSE >, IARG_PTR, scriptCbInfoPtr,
                                                   IARG_IARGLIST, pinVariadicArgs, IARG_END);
            }
        }
        else
        {
            if (isAnlReturnAddrint)
            {
                res = RTN_ReplaceSignatureProbed(rtn, (AFUNPTR)gluetool_analysis_callback< TRUE >, IARG_PTR, scriptCbInfoPtr,
                                                 IARG_IARGLIST, pinVariadicArgs, IARG_END);
            }
            else
            {
                res = RTN_ReplaceSignatureProbed(rtn, (AFUNPTR)gluetool_analysis_callback< FALSE >, IARG_PTR, scriptCbInfoPtr,
                                                 IARG_IARGLIST, pinVariadicArgs, IARG_END);
            }
        }
    }
    else
    {
        if (isAnlReturnAddrint)
        {
            res = RTN_ReplaceSignature(rtn, (AFUNPTR)gluetool_analysis_callback< TRUE >, IARG_PTR, scriptCbInfoPtr, IARG_IARGLIST,
                                       pinVariadicArgs, IARG_END);
        }
        else
        {
            res = RTN_ReplaceSignature(rtn, (AFUNPTR)gluetool_analysis_callback< FALSE >, IARG_PTR, scriptCbInfoPtr,
                                       IARG_IARGLIST, pinVariadicArgs, IARG_END);
        }
    }

    IARGLIST_Free(pinVariadicArgs);

    return py_var(vm, res);
}

/**
 * @brief The gluetool instrumentation callback that is passed to <X>_Add<X>Function Pin APIs.
 * 
 * This function template is used as the instrumentation callback that is passed to <X>_Add<X>Function Pin APIs,
 * where <X> represents the specific API. 
 * e.g INS_AddInstrumentFunction(gluetool_instrumentation_callback, script_cb);
 *     IMG_AddUnloadFunction(gluetool_instrumentation_callback, script_cb);   
 * 
 * The template parameter `ClassType` specifies the type of the object
 * being instrumented, and the `scriptCB` parameter is a pointer to the script callback function.
 * 
 * @tparam ClassType The type of the object being instrumented.
 * 
 * @param obj The object being instrumented.
 * @param scriptCB A pointer to the script callback function.
 */
template< class ClassType > VOID gluetool_instrumentation_callback(ClassType obj, VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        if constexpr (Is_Index_Specialization< ClassType >::value)
        {
            (void)toolVM.vm->call((PyObject*)scriptCB, PY_VAR(obj.index));
        }
        else
        {
            (void)toolVM.vm->call((PyObject*)scriptCB, PY_VAR(obj));
        }
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

/**
 * @brief The gluetool callback that is passed to PIN_Add<X>Function APIs.
 * 
 * This function template is used as the callback that is passed to PIN_Add<X>Function APIs.
 *  e.g PIN_AddFiniFunction(gluetool_general_callback, scriptCB)
 * 
 * The template parameter `ClassType` specifies
 * the type of the object passed to the callback, and the `scriptCB` parameter is a pointer to
 * the script callback function.
 * 
 * @tparam ClassType The type of the object passed to the callback.
 * 
 * @param obj The object passed to the callback
 * @param scriptCB A pointer to the script callback function.
 */
template< class ClassType > VOID gluetool_general_callback(ClassType obj, VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        (void)toolVM.vm->call((PyObject*)scriptCB, PY_VAR(obj));
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

// Overload - without ClassType arg
VOID gluetool_general_callback(VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        (void)toolVM.vm->call((PyObject*)scriptCB);
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

// For all thread callbacks APIs from the following prototype
VOID gluetool_thread_callback(THREADID threadIndex, CONTEXT* ctxt, ADDRINT std, VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        (void)toolVM.vm->call((PyObject*)scriptCB, PY_VAR(threadIndex), PY_VAR(ctxt), PY_VAR(std));
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

// Special function for PIN_AddThreadFiniFunction (that getting const CONTEXT*)
VOID gluetool_thread_fini_callback(THREADID threadIndex, const CONTEXT* ctxt, INT32 std, VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        (void)toolVM.vm->call((PyObject*)scriptCB, PY_VAR(threadIndex), PY_VAR(ctxt), PY_VAR(std));
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

// Special function for PIN_AddThreadAttachFunction
VOID gluetool_thread_attach_callback(THREADID threadIndex, CONTEXT* ctxt, VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        (void)toolVM.vm->call((PyObject*)scriptCB, PY_VAR(threadIndex), PY_VAR(ctxt));
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

// Special function for PIN_AddThreadDetachFunction
VOID gluetool_thread_detach_callback(THREADID threadIndex, const CONTEXT* ctxt, VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        (void)toolVM.vm->call((PyObject*)scriptCB, PY_VAR(threadIndex), PY_VAR(ctxt));
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

// Special function for PIN_InterceptSignal
BOOL gluetool_intercept_signal_callback(THREADID tid, INT32 sig, CONTEXT* ctxt, BOOL hasHandler,
                                        const EXCEPTION_INFO* pExceptInfo, VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        // vm->call not implemented in pocketpy for more than 4 args, therefor we use here vm->vectorcall
        std::vector< PyObject* > pyArgs = {PY_VAR(tid), PY_VAR(sig), PY_VAR(ctxt), PY_VAR(hasHandler), PY_VAR(pExceptInfo)};
        return py_cast< BOOL >(toolVM.vm.get(), glue::vm_call_unpack(toolVM.vm.get(), (PyObject*)scriptCB, pyArgs));
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

// Special function for PIN_AddContextChangeFunction
VOID gluetool_context_change_callback(THREADID tid, CONTEXT_CHANGE_REASON reason, const CONTEXT* from, CONTEXT* to, INT32 info,
                                      VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        // vm->call not implemented in pocketpy for more than 4 args, therefor we use here vm->vectorcall
        std::vector< PyObject* > pyArgs = {PY_VAR(tid), PY_VAR(reason), PY_VAR(from), PY_VAR(to), PY_VAR(info)};
        (void)glue::vm_call_unpack(toolVM.vm.get(), (PyObject*)scriptCB, pyArgs);
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

// Special function for TRACE_AddSmcDetectedFunction
VOID gluetool_smc_detected_callback(ADDRINT traceStartAddress, ADDRINT traceEndAddress, VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        (void)toolVM.vm->call((PyObject*)scriptCB, PY_VAR(traceStartAddress), PY_VAR(traceEndAddress));
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

// Special function for PIN_AddInternalExceptionHandler
EXCEPT_HANDLING_RESULT gluetool_internal_exception_callback(THREADID tid, EXCEPTION_INFO* pExceptInfo,
                                                            PHYSICAL_CONTEXT* pPhysCtxt, VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        return py_cast< EXCEPT_HANDLING_RESULT >(
            toolVM.vm.get(), toolVM.vm->call((PyObject*)scriptCB, PY_VAR(tid), PY_VAR(pExceptInfo), PY_VAR(pPhysCtxt)));
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        // TODO send an error request message
        exit(1);
    }
}

/**
 * @brief Callback function for Glue timer events.
 * 
 * @param [in] scriptCB A pointer to the Python callback function to be called.
 *
 */
void gluetool_timer_callback(VOID* scriptCB)
{
    try
    {
        Scoped_GIL_guard lock;
        (void)toolVM.vm->call((PyObject*)scriptCB);
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        exit(1);
    }
}

/**
 * @brief Create a new timer and binds it to a Python callback function.
 * 
 * @return A PyObject representing the timer ID.
 */
PyObject* gluetool_bind_timer_create(VM* vm, ArgsView args)
{
    auto scriptCb = args[0];
    auto interval = py_cast< unsigned int >(vm, args[1]);

    auto timerId = toolState.timerManager->add_timer(gluetool_timer_callback, interval, scriptCb);

    if (std::numeric_limits< size_t >::max() == timerId)
    {
        vm->RuntimeError("Maximum number of timers has been reached");
    }

    return py_var(vm, timerId);
}

PyObject* gluetool_bind_timer_start(VM* vm, ArgsView args)
{
    auto timerId = py_cast< size_t >(vm, args[0]);
    auto res     = toolState.timerManager->start_timer(timerId);

    return py_var(vm, res);
}

/**
 * @brief Stops a timer with the given timer ID (args[0]).
 *
 * This operation does not wait for the timer to actually stop
 * 
 * @return A PyObject representing the result of the stop operation: 
 *         True on Success, False on error or if ID not found.
 */
PyObject* gluetool_bind_timer_stop(VM* vm, ArgsView args)
{
    auto timerId = py_cast< size_t >(vm, args[0]);
    auto res     = toolState.timerManager->stop_timer(timerId);

    return py_var(vm, res);
}

/**
 * @brief Stops and close a timer with the given timer ID (args[0]).
 *
 * This operation does not wait for the timer to actually stop
 * 
 * @return A PyObject representing the result of the stop operation: 
 *         True on Success, False on error or if ID not found.
 */
PyObject* gluetool_bind_timer_close(VM* vm, ArgsView args)
{
    auto timerId = py_cast< size_t >(vm, args[0]);
    auto res     = toolState.timerManager->close_timer(timerId);

    return py_var(vm, res);
}

/**
 * @brief Wait for a timer with the given timer ID (args[0]).
 * 
 * args[1] is the timeout in milliseconds to wait for the timer to complete.
 * If this value is (-1) then this function wait indefinitely 
 * 
 * @param vm 
 * @param args 
 * @return A PyObject representing the result of the stop operation: 
 *         True on Success, False on timeout or if ID not found.
 */
PyObject* gluetool_bind_timer_wait(VM* vm, ArgsView args)
{
    auto timerId = py_cast< size_t >(vm, args[0]);
    auto timeout = py_cast< unsigned >(vm, args[1]);

    // Release the lock to allow other threads to proceed - it will be re-acquired when we go out of scope
    Scoped_GIL_temp_release tempUnlock;
    auto res = toolState.timerManager->wait_timer(timerId, timeout);
    return py_var(vm, res);
}

/**
 * @brief The Scripting Engine Bootstrap.
 */
void gluetool_scripting_engine_bootstrap()
{
    glue::bind_pin_apis_and_attributes(toolVM.vm.get(), toolVM.pinModule);

    // Bind Glue Timer APIs
    try
    {
        toolState.timerManager = std::make_unique< glue::Timer_thread_manager >();
    }
    catch (const std::bad_alloc& e)
    {
        std::cerr << "pinglueserver: ERROR! Out Of Memory!" << std::endl;
        exit(1);
    }
    toolVM.vm->bind(toolVM.pinModule, "GLUE_TimerCreate(cb, interval)", gluetool_bind_timer_create);
    toolVM.vm->bind(toolVM.pinModule, "GLUE_TimerStart(id)", gluetool_bind_timer_start);
    toolVM.vm->bind(toolVM.pinModule, "GLUE_TimerStop(id)", gluetool_bind_timer_stop);
    toolVM.vm->bind(toolVM.pinModule, "GLUE_TimerClose(id)", gluetool_bind_timer_close);
    toolVM.vm->bind(toolVM.pinModule, "GLUE_TimerWait(id, timeout)", gluetool_bind_timer_wait);

    // Bind _Insert APIs
    toolVM.vm->bind(toolVM.pinModule, "BBL_InsertCall(bbl, IPOINT, cb, *args)",
                    gluetool_insert_call< BBL, VOID, BBL_InsertCall >);
    toolVM.vm->bind(toolVM.pinModule, "BBL_InsertIfCall(bbl, IPOINT, cb, *args)",
                    gluetool_insert_call< BBL, VOID, BBL_InsertIfCall >);
    toolVM.vm->bind(toolVM.pinModule, "BBL_InsertThenCall(bbl, IPOINT, cb, *args)",
                    gluetool_insert_call< BBL, VOID, BBL_InsertThenCall >);
    toolVM.vm->bind(toolVM.pinModule, "RTN_InsertCall(rtn, IPOINT, cb, *args)",
                    gluetool_insert_call< RTN, VOID, RTN_InsertCall >);
    toolVM.vm->bind(toolVM.pinModule, "RTN_InsertCallProbed(rtn, IPOINT, cb, *args)",
                    gluetool_insert_call< RTN, BOOL, RTN_InsertCallProbed >);
    toolVM.vm->bind(toolVM.pinModule, "INS_InsertCall(ins, IPOINT, cb, *args)",
                    gluetool_insert_call< INS, VOID, INS_InsertCall >);
    toolVM.vm->bind(toolVM.pinModule, "INS_InsertPredicatedCall(ins, IPOINT, cb, *args)",
                    gluetool_insert_call< INS, VOID, INS_InsertPredicatedCall >);
    toolVM.vm->bind(toolVM.pinModule, "INS_InsertThenCall(ins, IPOINT, cb, *args)",
                    gluetool_insert_call< INS, VOID, INS_InsertThenCall >);
    toolVM.vm->bind(toolVM.pinModule, "INS_InsertIfCall(ins, IPOINT, cb, *args)",
                    gluetool_insert_call< INS, VOID, INS_InsertIfCall >);
    toolVM.vm->bind(toolVM.pinModule, "INS_InsertIfPredicatedCall(ins, IPOINT, cb, *args)",
                    gluetool_insert_call< INS, VOID, INS_InsertIfPredicatedCall >);
    toolVM.vm->bind(toolVM.pinModule, "INS_InsertThenPredicatedCall(ins, IPOINT, cb, *args)",
                    gluetool_insert_call< INS, VOID, INS_InsertThenPredicatedCall >);
    toolVM.vm->bind(toolVM.pinModule, "TRACE_InsertCall(trace, IPOINT, cb, *args)",
                    gluetool_insert_call< TRACE, VOID, TRACE_InsertCall >);
    toolVM.vm->bind(toolVM.pinModule, "TRACE_InsertThenCall(trace, IPOINT, cb, *args)",
                    gluetool_insert_call< TRACE, VOID, TRACE_InsertThenCall >);
    toolVM.vm->bind(toolVM.pinModule, "TRACE_InsertIfCall(trace, IPOINT, cb, *args)",
                    gluetool_insert_call< TRACE, VOID, TRACE_InsertIfCall >);

    toolVM.vm->bind(toolVM.pinModule, "RTN_InsertCallProbedEx(rtn, IPOINT, PROBE_MODE, cb, *args)",
                    gluetool_rtn_insert_call_probedEx);
    toolVM.vm->bind(toolVM.pinModule, "PIN_InsertCallProbed(addr, cb, *args)", gluetool_pin_insert_call_probed);

    // Bind RTN_Replace APIs
    toolVM.vm->bind(toolVM.pinModule, "RTN_ReplaceSignature(replacedRtn, cb, proto, *args)", gluetool_rtn_replace_signature);
    toolVM.vm->bind(toolVM.pinModule, "RTN_ReplaceSignatureProbed(replacedRtn, cb, proto, *args)",
                    gluetool_rtn_replace_signature< TRUE >);
    toolVM.vm->bind(toolVM.pinModule, "RTN_ReplaceSignatureProbedEx(replacedRtn, mode, cb, proto, *args)",
                    gluetool_rtn_replace_signature< TRUE, TRUE >);

    // Bind _AddInstrumentFunction APIs
    toolVM.vm->bind(toolVM.pinModule, "INS_AddInstrumentFunction(ins_cb)",
                    [](VM* vm, ArgsView args)
                    {
                        // INS_AddInstrumentFunction acquire the client lock - prevent deadlock
                        Scoped_GIL_temp_release tempUnlock;
                        INS_AddInstrumentFunction(gluetool_instrumentation_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "IMG_AddInstrumentFunction(image_cb)",
                    [](VM* vm, ArgsView args)
                    {
                        IMG_AddInstrumentFunction(gluetool_instrumentation_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "IMG_AddUnloadFunction(image_unload_cb)",
                    [](VM* vm, ArgsView args)
                    {
                        IMG_AddUnloadFunction(gluetool_instrumentation_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "RTN_AddInstrumentFunction(rtn_cb)",
                    [](VM* vm, ArgsView args)
                    {
                        IMG_AddUnloadFunction(gluetool_instrumentation_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "TRACE_AddInstrumentFunction(trace_cb)",
                    [](VM* vm, ArgsView args)
                    {
                        // TRACE_AddInstrumentFunction acquire the client lock - prevent deadlock
                        Scoped_GIL_temp_release tempUnlock;
                        TRACE_AddInstrumentFunction(gluetool_instrumentation_callback, args[0]);
                        return vm->None;
                    });

    // Bind PIN_Add APIs
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddFiniFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddFiniFunction(gluetool_general_callback, args[0]);

                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddThreadStartFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddThreadStartFunction((THREAD_START_CALLBACK)gluetool_thread_callback, args[0]);

                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddThreadFiniFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddThreadFiniFunction(gluetool_thread_fini_callback, args[0]);

                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddSyscallEntryFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddSyscallEntryFunction((SYSCALL_ENTRY_CALLBACK)gluetool_thread_callback, args[0]);

                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddSyscallExitFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddSyscallExitFunction((SYSCALL_EXIT_CALLBACK)gluetool_thread_callback, args[0]);

                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddPrepareForFiniFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddPrepareForFiniFunction(gluetool_general_callback, args[0]);

                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddOutOfMemoryFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddOutOfMemoryFunction(gluetool_general_callback, args[0]);

                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddDetachFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddDetachFunction(gluetool_general_callback, args[0]);

                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddDetachFunctionProbed(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddDetachFunctionProbed(gluetool_general_callback, args[0]);

                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_Attach(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        // PIN_Attach acquires VM and client locks - prevent deadlock
                        Scoped_GIL_temp_release tempUnlock;
                        ATTACH_STATUS status = PIN_Attach(gluetool_general_callback, args[0]);
                        return py_var(vm, status);
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AttachProbed(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        // PIN_AttachProbed acquires VM and client locks - prevent deadlock
                        Scoped_GIL_temp_release tempUnlock;
                        ATTACH_STATUS status = PIN_AttachProbed(gluetool_general_callback, args[0]);
                        return py_var(vm, status);
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddThreadAttachProbedFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddThreadAttachProbedFunction(gluetool_general_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddThreadAttachFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddThreadAttachFunction(gluetool_thread_attach_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddThreadDetachFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddThreadDetachFunction(gluetool_thread_detach_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddThreadDetachProbedFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddThreadDetachProbedFunction(gluetool_general_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddApplicationStartFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddApplicationStartFunction(gluetool_general_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_InterceptSignal(sig, cb)",
                    [](VM* vm, ArgsView args)
                    {
                        auto res =
                            PIN_InterceptSignal(py_cast< INT32 >(vm, args[0]), gluetool_intercept_signal_callback, args[1]);
                        return py_var(vm, res);
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddContextChangeFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddContextChangeFunction(gluetool_context_change_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "TRACE_AddSmcDetectedFunction(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        TRACE_AddSmcDetectedFunction(gluetool_smc_detected_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_AddInternalExceptionHandler(cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_AddInternalExceptionHandler(gluetool_internal_exception_callback, args[0]);
                        return vm->None;
                    });
    toolVM.vm->bind(toolVM.pinModule, "PIN_TryStart(tid, cb)",
                    [](VM* vm, ArgsView args)
                    {
                        PIN_TryStart(py_cast< THREADID >(vm, args[0]), gluetool_internal_exception_callback, args[1]);
                        return vm->None;
                    });

    //  Not implemented:
    // "PIN_AddMemoryAddressTransFunction(cb)"
    // "PIN_GetMemoryAddressTransFunction(cb)"
    // "PIN_AddFetchFunction(cb)"
    // "PIN_FetchCode(cb)"
    // "PIN_AddForkFunction(cb)"
}

/**
 * @brief Wait for response from client.
 * 
 *  @param[in] vm       the pocketpy vm that this function called from.
 *  @param[in] channel  the txll channel that will get the response.
 *  @param[in] buffer   the buffer that will used for getting the response.
 *  @param[in] rscMsg   an instance of Client rsc proto message
 *   
 * @return bool -       true if a valid response arrived, false for error.
 * 
 *                      throw vm->RuntimeError if no response arrived (timeout) or if rejected by client (reject response arrived) .           
*/
bool wait_and_handle_response(VM* vm, void* channel, uint8_t* buffer, RSC_proto_message_wrapper< E_Rsc_Role::Client >& rscMsg,
                              uint32_t timeout = KnobResponseTimeout.Value())
{
    uint64_t serverChannelId;
    t_syscall_ret ret = glue::pinglue_tx_read(channel, timeout, buffer, glue::BUFFER_SIZE, serverChannelId);
    if (IS_PINOS_SYSCALL_ERROR(ret))
    {
        if (SYSRET(ETIMEDOUT) == ret)
        {
            vm->RuntimeError("Response not arrived from client");
        }

        return false;
    }

    E_rsc_error status = rscMsg.decode(buffer, glue::BUFFER_SIZE);
    if (RscErrorReject == status)
    {
        vm->RuntimeError("Request rejected by client");
    }
    ASSERT(RscErrorSuccess == status, "Error decoding response");

    return true;
}

/**
 * @brief Wait for response from a remote function call and handle the response that should contains the return value to return in the scripting engine.
 * 
 *  @param[in] vm               the pocketpy vm that this function called from, nullptr in case of async remote call.
 *  @param[in] channel          the txll channel that will get the response.
 *  @param[in] buffer           the buffer that will used for getting the response.
 *  @param[in] rscMsg           an instance of Client rsc proto message
 *  @param[out] responseRet     the return value to return in the scripting engine.
 *  @param[in] timeout          the timeout to wait for response in microseconds.
 *   
 * @return bool                 true if a valid response arrived, false on error.
 * 
 *                              throw vm->RuntimeError if no response arrived (timeout)
 *                              throw vm->TypeError if return value type is not supported.
*/
template< bool IsAsyncCall = false >
bool wait_and_handle_remote_call_response(VM* vm, void* channel, uint8_t* buffer,
                                          RSC_proto_message_wrapper< E_Rsc_Role::Client >& rscMsg, PyObject*& responseRet,
                                          uint32_t timeout = KnobResponseTimeout.Value())
{
    uint64_t serverChannelId;
    auto ret = glue::pinglue_tx_read(channel, timeout, buffer, glue::BUFFER_SIZE, serverChannelId);

    if (IS_PINOS_SYSCALL_ERROR(ret))
    {
        if (SYSRET(ETIMEDOUT) == ret)
        {
            // This could happen if client is busy or not listen.
            if constexpr (IsAsyncCall)
            {
                std::cerr << "pinglueserver: Response timeout from async remote call response" << std::endl;
                return false;
            }
            vm->RuntimeError("Response timeout");
        }

        return false;
    }

    E_rsc_error status = rscMsg.decode_without_schema(buffer, glue::BUFFER_SIZE);
    if (RscErrorReject == status)
    {
        if constexpr (IsAsyncCall)
        {
            std::cerr << "pinglueserver: Async remote call request rejected by client" << std::endl;
            return false;
        }
        vm->RuntimeError("Request rejected by client");
    }
    ASSERT(RscErrorSuccess == status, "Error decoding remote call response");

    if constexpr (IsAsyncCall)
    {
        return true;
    }

    size_t decodedArgsSize = rscMsg.get_decoded_args_size();
    ASSERT(1 == decodedArgsSize, "ERROR! decoded args size should be 1");

    t_rpc_arg respRet = rscMsg.get_decoded_args()[0];
    switch (ARG_SCHEMA_TYPE(respRet.argSchema))
    {
        case RpcBoolean:
            responseRet = py_var(vm, (bool)respRet.argData);
            break;
        case RpcFloat:
            responseRet = py_var(vm, *reinterpret_cast< double* >(&respRet.argData));
            break;
        case RpcBuffer:
            responseRet = py_var(vm, (const char*)(respRet.argData));
            break;
        case RpcNil:
            responseRet = vm->None;
            break;
        case RpcUInt:
        case RpcInt:
            responseRet = py_var(vm, respRet.argData);
            break;

        default:
            vm->TypeError("Unsupported return arg type");
    }

    return true;
}

/**
 * @brief Send service result callback message.
 * 
 * @param[in] vm             the pocketpy vm that this function called from.
 * @param[in] serviceModule  pocketpy service module.
 * @param[in] jsonResultStr  json to send.
 * 
 *                           throw vm->RuntimeError if no channel available to send the message.
 */
void gluetool_send_service_result_callback_message(VM* vm, PyObject* serviceModule, const Str& jsonResultStr)
{
    auto cbAddr             = py_cast< UINT64 >(vm, serviceModule->attr(RESULT_CB_ADDRESS));
    auto& cbName            = py_cast< Str& >(vm, serviceModule->attr(RESULT_CB_NAME));
    auto& serviceUniqueName = py_cast< Str& >(vm, serviceModule->attr(SERVICE_UNIQUE_NAME));

    RSC_proto_message_wrapper< E_Rsc_Role::Client > rscMsg(&glue::Service_Result_Schema, &toolState.clientState);

    // Init message
    ASSERT(RscErrorSuccess == rscMsg.init_encode(), "pinglueserver: ERROR init_encode()");

    // Add message args
    ASSERTX(RscErrorSuccess == rscMsg.add_encode_arg(0, (uint64_t)(cbName.c_str()), 1 + (size_t)cbName.size));
    ASSERTX(RscErrorSuccess == rscMsg.add_encode_arg(1, (uint64_t)(jsonResultStr.c_str()), 1 + (size_t)jsonResultStr.size));
    ASSERTX(RscErrorSuccess ==
            rscMsg.add_encode_arg(2, (uint64_t)(serviceUniqueName.c_str()), 1 + (size_t)serviceUniqueName.size));

    // Send the request
    auto uniqueChannel = toolState.clientChannelPool->acquire(1000);
    if (nullptr == uniqueChannel)
    {
        vm->RuntimeError("No channel available, consider increasing the server channel pool capacity or try again");
    }

    uint8_t* buffer = uniqueChannel->buffer;

    // Encode message
    ASSERTX(RscErrorSuccess == rscMsg.encode(buffer, glue::BUFFER_SIZE));

    // Send the request
    void* channel     = uniqueChannel->handle;
    t_syscall_ret ret = tx_send_to(channel, cbAddr, buffer, rscMsg.get_encoding_size());
    if (SYSRET(SUCCESS) != ret)
    {
        std::cerr << "pinglueserver: ERROR from sending the result callback message: " << ret << std::endl;
        exit(1);
    }

    // Wait for response
    if (!wait_and_handle_response(vm, channel, buffer, rscMsg))
    {
        std::cerr << "pinglueserver: an ERROR occurred in wait_and_handle_response, exiting.." << std::endl;
        exit(1);
    }
}

/**
 * @brief Call remote function by sending a remote callback message to client and return the client return value to the scripting engine.
 *        In case of IsAsyncCall is True (no block call), the function only save the encoded message to the async thread buffer pool 
 *          that will send the messages and will not care about client return value (but only wait to valid response)
 * 
 * @tparam IsAsyncCall      A boolean template parameter that determines whether the call is asynchronous (non-blocking).
 * @param[in] vm            pocketpy vm.
 * @param[in] cbChannelId   The receiver channel ID.
 * @param[in] args          A vector of RPC arguments to send to the remote function.
 * 
 * @return the return value to return in script.
 */
template< bool IsAsyncCall = false >
PyObject* gluetool_call_remote_function(VM* vm, UINT64 cbChannelId, std::vector< t_rpc_arg >& args)
{
    RSC_proto_message_wrapper< E_Rsc_Role::Client > rscMsg(&glue::Remote_Call_Schema, &toolState.clientState);
    // Init message
    ASSERT(RscErrorSuccess == rscMsg.init_encode(), "ERROR! init_encode failed");

    // Add message args
    for (auto& arg : args)
    {
        ASSERTX(RscErrorSuccess == rscMsg.add_encode_arg(arg));
    }

    uint8_t* buffer                   = nullptr;
    void* channel                     = nullptr;
    glue::t_unique_async_msg asyncMsg = nullptr;

    if constexpr (IsAsyncCall)
    {
        while (nullptr == (asyncMsg = toolState.asyncMsgPool->acquire()))
        {
            std::this_thread::yield();
        }

        buffer = asyncMsg->buffer;
    }
    else
    {
        auto uniqueChannel = toolState.clientChannelPool->acquire(1000);
        if (nullptr == uniqueChannel)
        {
            vm->RuntimeError("No channel available, consider increasing the server channel pool capacity or try again");
        }

        buffer  = uniqueChannel->buffer;
        channel = uniqueChannel->handle;
    }

    // Encode message
    ASSERT(RscErrorSuccess == rscMsg.encode(buffer, glue::BUFFER_SIZE), "call_remote_function: ERROR encode message");

    if constexpr (IsAsyncCall)
    {
        asyncMsg->msgCounter      = rscMsg.get_message_counter();
        asyncMsg->msgSize         = rscMsg.get_encoding_size();
        asyncMsg->clientChannelId = cbChannelId;

        toolState.asyncMsgQueue.push(std::move(asyncMsg));

        return vm->None;
    }

    // Send the request
    t_syscall_ret ret = tx_send_to(channel, cbChannelId, buffer, rscMsg.get_encoding_size());
    if (SYSRET(SUCCESS) != ret)
    {
        std::cerr << "pinglueserver: ERROR from sending remote function message : " << ret << std::endl;
        exit(1);
    }

    // Wait for response
    PyObject* respRetVal = vm->None;
    if (!wait_and_handle_remote_call_response(vm, channel, buffer, rscMsg, respRetVal))
    {
        std::cerr << "pinglueserver: ERROR occurred in wait_and_handle_remote_call_response, exiting.." << std::endl;
        exit(1);
    }

    return respRetVal;
}

/**
 * @brief Send response to client, when errMssg is mark whether the request was successful.
 * 
 * @param[in] rscMsg            an instance of rsc proto message. 
 * @param[in] clientChannelId   the client txll channel ID.
 * @param[in] errMssg           Only for service response: nullptr if request was registered correctly or error message,  
 * 
 * @param[in] IsServiceResponse  Template parameter, indicate if it's a service response to treat response ret as nill or Error message. 
 *                               Otherwise response ret should treat as bool (true for success).
 * 
 */
template< bool IsServiceResponse = false >
void gluetool_send_response(RSC_proto_message_wrapper< Server >& rscMsg, uint64_t& clientChannelId,
                            std::string* errMssg = nullptr)
{
    int status = rscMsg.init_encode();
    if (RscErrorSuccess != status)
    {
        std::cerr << "pinglueserver: ERROR from init_encode() on response:" << status << std::endl;
        exit(1);
    }

    status = rscMsg.add_encode_nil_args();

    if constexpr (IsServiceResponse)
    {
        if (nullptr == errMssg)
        {
            status |= rscMsg.add_encode_nil_arg();
        }
        else
        {
            t_rpc_arg arg = MAKE_RPC_ARG(glue::Service_Request_Schema.returnValueSchema, (uint64_t)(*errMssg).c_str(),
                                         1 + (*errMssg).size(), RpcArgFlagsNone);
            status |= rscMsg.add_encode_arg(arg);
        }
    }
    else
    {
        ASSERT(nullptr == errMssg, "errMssg should be nullptr");
        status |= rscMsg.add_encode_bool_ret_arg(true);
    }

    status |= rscMsg.encode(toolState.serverChannel->buffer, glue::BUFFER_SIZE);

    // Send the response
    auto ret =
        tx_send_to(toolState.serverChannel->handle, clientChannelId, toolState.serverChannel->buffer, rscMsg.get_encoding_size());

    if (IS_PINOS_SYSCALL_ERROR(ret))
    {
        std::cerr << "pinglueserver: ERROR!!! Server send response to client failed" << std::endl;
        exit(1);
    }
}

/**
 * @brief Bind service special APIs per service.
 * 
 * @param[in] serviceModule  pocketpy service module.
 * 
 */
void gluetool_bind_service_module(PyObject* serviceModule)
{
    using namespace glue;
    toolVM.vm->bind(
        serviceModule, "Glue_SendServiceResultCallback(json_result_Str)",
        [](VM* vm, ArgsView args)
        {
            PyObject* serviceModule = lambda_get_userdata< PyObject* >(args.begin());
            gluetool_send_service_result_callback_message(vm, serviceModule, py_cast< Str& >(vm, args[0]));

            return vm->None;
        },
        serviceModule);

    // The server will bind the calls in script to <script_func_name>(*args) to call the remote function (sending Remote Function Call Message).
    toolVM.vm->bind(
        serviceModule, "GLUE_RegisterRemoteFunction(channel_id, script_func_name, remote_func_name, no_block)",
        [](VM* vm, ArgsView args)
        {
            PyObject* serviceModule = lambda_get_userdata< PyObject* >(args.begin());
            UINT64 channelId;
            if (vm->None == args[0])
            {
                // Send to default service channel.
                channelId = py_cast< UINT64 >(vm, serviceModule->attr(RESULT_CB_ADDRESS));
            }
            else
            {
                channelId = py_cast< UINT64 >(vm, args[0]);
            }

            if (vm->None == args[1] || vm->None == args[2] || vm->None == args[3])
            {
                vm->ValueError("None value");
            }

            const char* scriptFuncName = py_cast< CString >(vm, args[1]);
            const char* remoteFuncName = py_cast< CString >(vm, args[2]);
            bool noBlock               = py_cast< bool >(vm, args[3]);

            auto serviceUniqueName = py_cast< CString >(vm, serviceModule->attr(SERVICE_UNIQUE_NAME));

            Remote_func_call_info* remoteFuncInfo =
                new (std::nothrow) Remote_func_call_info {channelId, strdup(remoteFuncName), serviceUniqueName.ptr, noBlock};
            ASSERT(nullptr != remoteFuncInfo, "Out Of Memory");

            std::string bindSig = std::string(scriptFuncName) + "(*args)";
            toolVM.vm->bind(
                serviceModule, bindSig.c_str(),
                [](VM* vm, ArgsView args)
                {
                    Tuple pyVariadicArgs = py_cast< Tuple >(vm, args[0]);
                    auto varSize         = pyVariadicArgs.size();
                    std::vector< t_rpc_arg > remoteArgs;

                    // Add remoteFuncName
                    auto remoteFuncInfo = lambda_get_userdata< Remote_func_call_info* >(args.begin());
                    t_rpc_arg arg       = MAKE_RPC_ARG(BUFFER_SCHEMA, (uint64_t)remoteFuncInfo->remoteFuncName,
                                                       1 + strlen(remoteFuncInfo->remoteFuncName), RpcArgFlagsNone);
                    remoteArgs.emplace_back(arg);

                    // Add unique name
                    arg = MAKE_RPC_ARG(BUFFER_SCHEMA, (uint64_t)remoteFuncInfo->name, 1 + strlen(remoteFuncInfo->name),
                                       RpcArgFlagsNone);
                    remoteArgs.emplace_back(arg);

                    // Add remote func args
                    for (int i = 0; i < varSize; ++i)
                    {
                        glue::get_rpc_arg_from_py_arg(vm, pyVariadicArgs[i], arg);
                        remoteArgs.emplace_back(arg);
                    }

                    PyObject* ret = vm->None;
                    if (remoteFuncInfo->noBlock)
                    {
                        gluetool_call_remote_function< TRUE >(vm, remoteFuncInfo->channelId, remoteArgs);
                    }
                    else
                    {
                        ret = gluetool_call_remote_function(vm, remoteFuncInfo->channelId, remoteArgs);
                    }

                    return ret;
                },
                remoteFuncInfo); // capture remote_func_name

            return vm->None;
        },
        serviceModule);
}

/**
 * @brief Execute the service request in the scripting engine and send the response to client.
 * 
 *  @param[in] serviceRequest   the service request to execute.
 *  @param[in] rscMsg           an instance of Server rsc proto message
 *  @param[in] clientChannelId  the channel ID to send the response.
 *   
 *  @return std::optional       std::string with error message on error, std::nullopt on success.       
 */
std::optional< std::string > gluetool_exec_service_request(glue::Service_request& serviceRequest,
                                                           RSC_proto_message_wrapper< E_Rsc_Role::Server >& rscMsg,
                                                           uint64_t clientChannelId)
{
    std::string servicePath =
        std::filesystem::path(KnobServicesDir.ValueString()) / std::filesystem::path(serviceRequest.serviceName);

    // We currently support only in services that exist in a file on the file system (even a client service)
    // We may add in the future an option to send in request the entire service code
    if (!std::filesystem::exists(servicePath))
    {
        std::string errMssg = "ERROR!! service not exists in path: " + servicePath;
        std::cerr << "pinglueserver: " << errMssg << std::endl;

        return errMssg;
    }

    try
    {
        Scoped_GIL_guard lock;
        std::ifstream ifs(servicePath);
        std::string serviceContent((std::istreambuf_iterator< char >(ifs)), (std::istreambuf_iterator< char >()));

        PyObject* serviceModule = toolVM.vm->new_module(serviceRequest.serviceName);
        // Save service result callback info.
        serviceModule->attr().set(RESULT_CB_ADDRESS, PY_VAR(serviceRequest.callbackAddress));
        serviceModule->attr().set(RESULT_CB_NAME, PY_VAR(serviceRequest.callbackName));
        serviceModule->attr().set(SERVICE_UNIQUE_NAME, PY_VAR(serviceRequest.name));

        gluetool_bind_service_module(serviceModule);

        for (auto& script : serviceRequest.scripts)
        {
            CodeObject_ code = toolVM.vm->compile(script, servicePath, EXEC_MODE);
            (void)toolVM.vm->_exec(code, serviceModule);
        }

        CodeObject_ code = toolVM.vm->compile(serviceContent, servicePath, EXEC_MODE);
        (void)toolVM.vm->_exec(code, serviceModule);

        // Call main if defined ans send Knobs (the only way do define knobs is with main(*args)),
        // the knobs will be pass to main function that will get the knobs and decide what to do with them.
        if (serviceModule->attr().contains("main"))
        {
            PyObject* f_main = serviceModule->attr("main");

            if (0 != serviceRequest.knobs.size())
            { // Send knobs to main, the main that gets knobs should define with *args - def main(*args)
                std::vector< PyObject* > pyArgs;

                for (auto& knob : serviceRequest.knobs)
                {
                    pyArgs.emplace_back(py_var(toolVM.vm.get(), knob));
                }

                glue::vm_call_unpack(toolVM.vm.get(), f_main, pyArgs);
            }
            else
            { // Optimization in case of no knobs sent.
                (void)toolVM.vm->call(f_main);
            }
        }
    }
    catch (Exception& e)
    {
        return e.summary().c_str();
    }

    return std::nullopt;
}

/**
 * @brief Validate pinglueserver main mode knobs and exit if knobs are not valid.
 *               
 */
void gluetool_validate_knobs()
{
    if ("0" == KnobRscHkdfSeed.Value())
    {
        std::cerr << "pinglueserver: ERROR! Knob '-rsc-hkdf-seed' is missing" << std::endl;
        exit(1);
    }
    // Regular expression pattern to match exactly 64 hexadecimal characters
    std::regex hexPattern("^[0-9a-fA-F]{64}$");
    if (!std::regex_match(KnobRscHkdfSeed.Value(), hexPattern))
    {
        std::cerr << "pinglueserver: ERROR! Knob '-rsc-hkdf-seed' value should contain exactly 64 hexadecimal characters"
                  << std::endl;
        exit(1);
    }
    if (0 == KnobClientRSCid.Value())
    {
        std::cerr << "pinglueserver: ERROR! Knob '-rsc-client-id' is missing" << std::endl;
        exit(1);
    }
    if (0 == KnobServerTxid.Value())
    {
        std::cerr << "pinglueserver: ERROR! Knob '-glue-server-txid' is missing" << std::endl;
        exit(1);
    }
}

/**
 * @brief The worker thread function for sending  asynchronous messages to the client.
 *
 * This function runs in a dedicated worker thread and is responsible for continuously
 * processing messages from the asynchronous message queue. It retrieves messages from
 * the queue, sends them to the client via the available communication channel, and
 * handles the responses. If a nullptr is received from the queue, it signals the thread
 * to exit the loop and terminate.
 */
void async_messages_worker_thread()
{
    for (;;)
    {
        // Pop a message from the asynchronous message queue
        auto asyncMsg = toolState.asyncMsgQueue.pop();
        if (!asyncMsg)
        {
            break;
        }

        RSC_proto_message_wrapper< E_Rsc_Role::Client > rscMsg(&glue::Remote_Call_Schema, &toolState.clientState);
        rscMsg.set_message_counter(asyncMsg->msgCounter);

        auto uniqueChannel = toolState.clientChannelPool->acquire(1000);
        if (nullptr == uniqueChannel)
        {
            std::cerr
                << "pinglueserver: ERROR! No channel available, consider increasing the server channel pool capacity or try again"
                << std::endl;
            exit(1);
        }

        // Send the request to the client
        t_syscall_ret ret = tx_send_to(uniqueChannel->handle, asyncMsg->clientChannelId, asyncMsg->buffer, asyncMsg->msgSize);
        if (SYSRET(SUCCESS) != ret)
        {
            std::cerr << "pinglueserver: ERROR from sending async remote function message : " << ret << std::endl;
            exit(1);
        }

        // Wait for response
        PyObject* respRetVal = nullptr; // Note: response return value is not used in asynchronous calls
        if (!wait_and_handle_remote_call_response< TRUE >(nullptr, uniqueChannel->handle, uniqueChannel->buffer, rscMsg,
                                                          respRetVal))
        {
            std::cerr
                << "pinglueserver: ERROR occurred in wait_and_handle_remote_call_response for async message response, exiting.."
                << std::endl;
            exit(1);
        }
    }
}

/**
 * @brief Initialize the pinglueserver main mode.
 *               
 */
void gluetool_init()
{
    try
    {
        glue::Channel* newChannel = new glue::Channel();
        toolState.serverChannel.reset(newChannel);
    }
    catch (const std::bad_alloc& e)
    {
        std::cerr << "pinglueserver: ERROR! Out Of Memory!" << std::endl;
        exit(1);
    }

    // Open txll channel from ID that created by client
    t_syscall_ret ret = tx_open(KnobServerTxid.Value());
    if (IS_PINOS_SYSCALL_ERROR(ret))
    {
        std::cerr << "pinglueserver: ERROR opening main server channel" << std::endl;
        exit(1);
    }
    toolState.serverChannel->handle = (void*)ret;

    // Initialize global seed variable.
    glue::string_to_seed(KnobRscHkdfSeed.Value(), toolState.seed.bits);

    // Initialize server main channel state
    E_rsc_error status =
        initialize_server_state(KnobClientRSCid.Value(), &toolState.seed, WINDOW_SIZE, nullptr, nullptr, &toolState.serverState);
    if (RscErrorSuccess != status)
    {
        std::cerr << "pinglueserver: ERROR from initialize_server_state" << std::endl;
        exit(status);
    }

    // Initialize server send channel state
    status = initialize_client_state(KnobClientRSCid.Value(), &toolState.seed, nullptr, nullptr, &toolState.clientState);
    if (RscErrorSuccess != status)
    {
        std::cerr << "pinglueserver: ERROR from initialize_server_state" << std::endl;
        exit(status);
    }

    try
    {
        toolState.clientChannelPool =
            std::make_unique< glue::Client_channel_pool >(KnobChannelPoolCapacity.Value(), glue::CHANNEL_SIZE);
        toolState.asyncMsgPool = std::make_unique< glue::Async_msg_pool >(KnobAsyncMsgPoolSize.Value());
    }
    catch (const std::bad_alloc& e)
    {
        std::cerr << "pinglueserver: ERROR! Out Of Memory!" << std::endl;
        exit(1);
    }

    // Add channels to clientChannelPool
    for (auto i = 0; i < KnobChannelPoolCapacity.Value(); ++i)
    {
        if (!toolState.clientChannelPool->add())
        {
            std::cerr << "pinglueserver: ERROR adding channel" << std::endl;
            exit(1);
        }
    }

    // Start the thread that will send the async remote messages (like remote function call messages that marked with No Block)
    toolState.asyncMsgWorkerThread = std::thread(async_messages_worker_thread);
    toolState.asyncMsgWorkerThread.detach();
}

/**
 * @brief Handle the arrived request message from client.
 * 
 *  @param[in] msgBuff          the buffer that used for decode the message.
 *  @param[in] clientChannelId  the channel channel ID that will receive the response.
 * 
 * @return t_rpc_id             the request RPC ID, -1 on failure. 
*/
t_rpc_id gluetool_handle_request_message(uint8_t* msgBuff, uint64_t clientChannelId)
{
    using namespace glue;
    RSC_proto_message_wrapper< E_Rsc_Role::Server > rscMsg(&toolState.serverState);
    t_rpc_id receivedRpcId = -1;
    E_rsc_error status     = rscMsg.init_decode(msgBuff, BUFFER_SIZE, receivedRpcId);
    if (RscErrorSuccess != status)
    {
        std::cerr << "pinglueserver: ERROR from init_decode():" << status << std::endl;
        exit(1);
    }

    switch (receivedRpcId)
    {
        case Service_Request_Schema.rpcId:
        {
            // Decode the message.
            rscMsg.set_schema(&Service_Request_Schema);
            status = rscMsg.decode(msgBuff, BUFFER_SIZE);
            if (RscErrorSuccess != status)
            {
                std::cerr << "pinglueserver: ERROR from decode(), exiting.." << status << std::endl;
                exit(1);
            }

            Service_request serviceRequest;
            serviceRequest.callbackAddress = rscMsg.get_decode_primitive_arg(0);
            rscMsg.get_decode_buffer_arg(1, serviceRequest.name);
            rscMsg.get_decode_buffer_arg(2, serviceRequest.serviceName);
            rscMsg.get_decode_buffer_arg(3, serviceRequest.callbackName);

            // handle knobs
            size_t arraySize;
            if (!rscMsg.is_nil_decode_arg(4))
            {
                t_rpc_arg* knobs = reinterpret_cast< t_rpc_arg* >(rscMsg.get_decode_primitive_arg(4, &arraySize));
                for (auto i = 0; i < arraySize; i++)
                {
                    serviceRequest.knobs.emplace_back((const char*)(knobs[i].argData));
                }
            }

            // handle scripts
            if (!rscMsg.is_nil_decode_arg(5))
            {
                t_rpc_arg* scripts = reinterpret_cast< t_rpc_arg* >(rscMsg.get_decode_primitive_arg(5, &arraySize));
                for (auto i = 0; i < arraySize; i++)
                {
                    serviceRequest.scripts.emplace_back((const char*)(scripts[i].argData));
                }
            }

            auto res = gluetool_exec_service_request(serviceRequest, rscMsg, clientChannelId);

            // Send response

            if (res.has_value()) // Failure
            {
                gluetool_send_response< true >(rscMsg, clientChannelId, &res.value());
                return -1;
            }

            gluetool_send_response< true >(rscMsg, clientChannelId); // Success

            break;
        }
        case Start_Program_Request_Schema.rpcId:
        {
            // Start Program Request has no fields in request

            // Send response
            rscMsg.set_schema(&Start_Program_Request_Schema);
            gluetool_send_response(rscMsg, clientChannelId);

            break;
        }
        default:
            std::cerr << "pinglueserver: ERROR unrecognized rpcId, existing.." << std::endl;
            exit(1);
    }

    return receivedRpcId;
}

/**
 * @brief Wait for requests messages and handle them, start the program when StartProgram message/request arrive.
 */
void gluetool_get_and_handle_requests()
{
    using namespace glue;
    bool anyLogicalRequestProcessed = false;

    t_rpc_id rpcId = 0;

    while (Start_Program_Request_Schema.rpcId != rpcId)
    {
        // Wait 10000 micro seconds (0.01 seconds)
        uint64_t clientChannelId;
        t_syscall_ret ret = glue::pinglue_tx_read(toolState.serverChannel->handle, 10000, toolState.serverChannel->buffer,
                                                  glue::BUFFER_SIZE, clientChannelId);
        if (IS_PINOS_SYSCALL_ERROR(ret))
        {
            ASSERTX(SYSRET(ETIMEDOUT) == ret);
            continue;
        }

        // Handle the arrived request message.
        rpcId = gluetool_handle_request_message(toolState.serverChannel->buffer, clientChannelId);
        if ((-1 != rpcId) && (Start_Program_Request_Schema.rpcId != rpcId))
        {
            anyLogicalRequestProcessed = true;
        }
    }

    // Case of only start program arrived but no additional requests arrived.
    if (!anyLogicalRequestProcessed)
    {
        std::cerr << "pinglueserver: ERROR!! start program arrived but without any other valid request before." << std::endl;
        exit(1);
    }
}

} // anonymous namespace

int main(int argc, char* argv[])
{
    // Initialize Pin
    if (PIN_Init(argc, argv)) return 1;

    // Init all symbols
    PIN_InitSymbolsAlt(SYMBOL_INFO_MODE(UINT32(IFUNC_SYMBOLS) | UINT32(DEBUG_OR_EXPORT_SYMBOLS)));

    try
    {
        // Create a pocketpy virtual machine
        toolVM.vm        = std::make_unique< VM >();
        toolVM.pinModule = toolVM.vm->new_module("pin");

        // Bindings
        gluetool_scripting_engine_bootstrap();

        if (KnobPythonScript.Value().empty())
        {
            // PinGlue Service Mode.
            // Actual service will be loaded in response to request from client
            // Validate knobs
            gluetool_validate_knobs();

            // Open server txll main channel (created by client) and create other server channels.
            gluetool_init();

            // Wait and handle requests
            gluetool_get_and_handle_requests();
        }
        else
        {
            Scoped_GIL_guard lock;
            // PinGlue Pintool Mode
            // Load and execute the Python Pintool script
            const char* filename = KnobPythonScript.Value().c_str();
            std::ifstream ifs(filename);
            if (!ifs)
            {
                std::cerr << filename << " doesn't exists! exiting.." << std::endl;
                exit(1);
            }

            std::string scriptContent((std::istreambuf_iterator< char >(ifs)), (std::istreambuf_iterator< char >()));
            // Fine-grained execution, compile first and then execute.
            CodeObject_ code = toolVM.vm->compile(scriptContent, filename, EXEC_MODE);
            (void)toolVM.vm->_exec(code, toolVM.vm->_main);
        }
    }
    catch (Exception& e)
    {
        std::cerr << e.summary() << std::endl;
        exit(1);
    }

    // Start the application, Never returns
    if (PIN_IsProbeMode())
    {
        PIN_StartProgramProbed();
    }
    else
    {
        PIN_StartProgram();
    }

    return 0;
}
