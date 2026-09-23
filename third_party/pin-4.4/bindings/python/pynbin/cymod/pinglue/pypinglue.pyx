#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

from libc cimport stdlib
from libc.stdint cimport uint16_t, uint32_t, int64_t, uint64_t
from libc.stddef cimport size_t
from cpython.unicode cimport PyUnicode_AsUTF8
from libc.string cimport memcpy, memset
from enum import Enum
from secrets import choice
import string

ctypedef void (*deleter_t)(void*)

cdef extern from "rscprotomsgtypes.h":
    ctypedef enum E_rpc_arg_type:
        RpcBoolean = 0
        RpcInt
        RpcUInt
        RpcChar
        RpcFloat
        RpcBuffer
        RpcOOBRef
        RpcRecord
        RpcArray
        RpcNil
        RpcVoid = RpcNil
        RpcPaddingNoEncode

    enum E_rpc_arg_flags:
        RpcArgFlagsNone = 0
        RpcArgFlagsDataEmpty = 1

    ctypedef uint32_t t_rpc_arg_schema

    cdef struct RPC_arg:
        uint64_t argData
        size_t argDataSize
        deleter_t deleter
        t_rpc_arg_schema argSchema
        E_rpc_arg_flags flags

    ctypedef RPC_arg t_rpc_arg

# Define the functions that replace the macros in "rscprotomsgtypes.h"
cdef inline t_rpc_arg_schema MAKE_ARG_SCHEMA(E_rpc_arg_type rpcType, size_t size):
    return (<uint32_t>rpcType & 0x000000FF) | ((<uint32_t>size & 0x00001FFF) << 16)

cdef inline t_rpc_arg_schema BOOL_ARG_SCHEMA():
    return MAKE_ARG_SCHEMA(RpcBoolean, 1)

cdef inline t_rpc_arg_schema INT64_ARG_SCHEMA():
    return MAKE_ARG_SCHEMA(RpcInt, sizeof(int64_t))

cdef inline t_rpc_arg_schema DOUBLE_ARG_SCHEMA():
    return MAKE_ARG_SCHEMA(RpcFloat, sizeof(double))

cdef inline t_rpc_arg_schema BUFFER_ARG_SCHEMA():
    return MAKE_ARG_SCHEMA(RpcBuffer, sizeof(uint32_t))

cdef inline t_rpc_arg_schema NIL_ARG_SCHEMA():
    return MAKE_ARG_SCHEMA(RpcNil, 0)

cdef inline E_rpc_arg_type ARG_SCHEMA_TYPE(t_rpc_arg_schema argSchema):
    return <E_rpc_arg_type>(argSchema & 0x000000FF)

cdef inline size_t ARG_SCHEMA_SIZE(argSchema):
    return <size_t>((argSchema >> 16) & 0x00000FFF)

cdef inline t_rpc_arg MAKE_RPC_ARG_EX(t_rpc_arg_schema argSchema, uint64_t argData, size_t argDataSize, E_rpc_arg_flags flags, deleter_t deleter):
    return t_rpc_arg(argData=argData, argDataSize=argDataSize, deleter=deleter, argSchema=argSchema, flags=flags)

cdef inline t_rpc_arg MAKE_RPC_ARG(t_rpc_arg_schema argSchema, uint64_t argData, size_t argDataSize, E_rpc_arg_flags flags):
    return MAKE_RPC_ARG_EX(argSchema, argData, argDataSize, flags, NULL)

cdef extern from "glue_requests.h":
    cdef struct Channel:
        pass
    ctypedef Channel t_pg_channel

    cdef struct Pinglue_service_result:
        const char* funcName
        const char* result
        size_t resultLength
        const char* name
    ctypedef Pinglue_service_result t_pg_service_result

    cdef struct Pinglue_remote_request:
        const t_rpc_arg* args
        size_t argsSize
    ctypedef Pinglue_remote_request t_pg_remote_request

    cdef struct Pinglue_remote_func:
        t_pg_remote_request request
        const char* functionName
        const char* name
    ctypedef Pinglue_remote_func t_pg_remote_func

    cdef struct Pinglue_remote_error:
        t_pg_remote_request request
        uint16_t errorId
        char* errorMessage
    ctypedef Pinglue_remote_error t_pg_remote_error

    ctypedef void (*t_pg_rpc_arg_deleter)(t_rpc_arg*, void*)
    cdef struct Pinglue_remote_response:
        t_rpc_arg ret
        t_pg_rpc_arg_deleter deleter
        void* state
        bint handled
        bint cancel
    ctypedef Pinglue_remote_response t_pg_remote_response


cdef extern from "pinglue.h":

    cdef struct Pin_glue:
        pass
    cdef struct Pin_glue_service:
        pass
    cdef struct Pin_glue_script_fragment:
        pass
    ctypedef Pin_glue t_pin_glue
    ctypedef Pin_glue_service t_pg_service
    ctypedef Pin_glue_script_fragment t_pg_script_fragment
    ctypedef void(*t_pg_remote_cb)(t_pg_remote_func*, t_pg_remote_response*)

    cdef struct Service_request:
        t_pg_channel* resultChannel 
        const char* serviceName
        const char* callbackName
        const char** knobs
        size_t knobsSize
        const t_pg_script_fragment** scripts
        size_t scriptsSize
        const char name[6]
    ctypedef Service_request t_pg_service_request

    cdef struct Args:
        size_t argc
        const char** argv
    ctypedef Args t_pg_args

    ctypedef enum E_pin_glue_ret:
        E_PG_Success = 0
        E_PG_InvalidArg
        E_PG_UnexpectedMsg
        E_PG_Internal
        E_PG_OutOfMemory
        E_PG_NoPin
        E_PG_Timeout
        E_PG_PinExit

    cdef int pinglue_open(t_pin_glue** pg, 
                          const t_pg_args* pinArgs, 
                          const t_pg_args* pgArgs, 
                          const char* program,
                          const t_pg_args* programArgs)

    cdef int pinglue_start_program(t_pin_glue* pg)

    cdef int pinglue_finish(t_pin_glue* pg, 
                            uint32_t timeoutUS) nogil

    cdef int pinglue_close(t_pin_glue* pg)

    cdef int pinglue_service_open(t_pg_service** service, 
                                  t_pin_glue* pg, 
                                  const t_pg_service_request* serviceRequest)

    cdef int pinglue_service_start(t_pg_service* service)

    cdef int pinglue_service_close(t_pg_service* service)

    cdef int pinglue_service_wait(t_pg_service* service, 
                                  uint32_t timeoutUS, 
                                  t_pg_remote_cb remoteCB,
                                  t_pg_service_result** serviceResult) nogil

    cdef int pinglue_service_result_free(t_pg_service_result* serviceResult)

    cdef int pinglue_open_channel(t_pg_channel** channel)

    cdef int pinglue_close_channel(t_pg_channel* channel)

    cdef int pinglue_channel_wait(t_pin_glue* pg, 
                                  t_pg_channel* channel, 
                                  uint32_t timeoutUS, 
                                  t_pg_remote_cb remoteCB)

    cdef t_pg_script_fragment* GLUE_register_remote_function(t_pg_channel* channel, 
                                                             const char* scriptFuncName, 
                                                             const char* remoteFuncName, 
                                                             bint noBlock)
    
    cdef t_pg_script_fragment* GLUE_server_script(const char* script, size_t scriptSize)

    cdef int pinglue_script_fragment_free(t_pg_script_fragment* fragment)

# Global variables

cdef t_rpc_arg_schema BUFFER_SCHEMA = BUFFER_ARG_SCHEMA()
cdef t_rpc_arg_schema INT_SCHEMA = INT64_ARG_SCHEMA()
cdef t_rpc_arg_schema FLOAT_SCHEMA = DOUBLE_ARG_SCHEMA()
cdef t_rpc_arg_schema BOOL_SCHEMA = BOOL_ARG_SCHEMA()
cdef t_rpc_arg_schema NIL_SCHEMA = NIL_ARG_SCHEMA()

cdef dict[str, PinGlueService] __services = {}

def _gen_random_string(size: int) -> str:
    """ Generate a random string of the given size
    Paramers:
        size (int): The size of the string. The size must be greater than 0

    Raises:
        TypeError: size is not an int
        ValueError: size <= 0

    Returns:
        str: A random string
    """
    if size is not None and not isinstance(size, int):
        raise TypeError("size must be of type int")
    elif size is None or size <= 0:
        raise ValueError("size must be > 0")

    return ''.join([choice(string.ascii_uppercase + string.digits) for _ in range(size)])


class PinGlueErrorCode(Enum):
    Success = <int>E_pin_glue_ret.E_PG_Success
    InvalidArg = <int>E_pin_glue_ret.E_PG_InvalidArg
    UnexpectedMsg = <int>E_pin_glue_ret.E_PG_UnexpectedMsg
    Internal = <int>E_pin_glue_ret.E_PG_Internal
    OutOfMemory = <int>E_pin_glue_ret.E_PG_OutOfMemory
    NoPin = <int>E_pin_glue_ret.E_PG_NoPin
    Timeout = <int>E_pin_glue_ret.E_PG_Timeout
    PinExit = <int>E_pin_glue_ret.E_PG_PinExit
    Unknown = 8


class PinGlueError(Exception):
    def __init__(self, eCode: int):
        errorMessage = ""
        try:
            cErrorCode = PinGlueErrorCode(eCode)
        except ValueError:
            cErrorCode = PinGlueErrorCode.Unknown
        if PinGlueErrorCode.Success == cErrorCode:
                errorMessage = "Success"
        elif PinGlueErrorCode.InvalidArg == cErrorCode:
                errorMessage = "Invalid Argument"
        elif PinGlueErrorCode.UnexpectedMsg == cErrorCode:
            errorMessage = "Unexpected Message"
        elif PinGlueErrorCode.Internal == cErrorCode:
            errorMessage = "Internal Error"
        elif PinGlueErrorCode.OutOfMemory == cErrorCode:
            errorMessage = "Out of Memory"
        elif PinGlueErrorCode.NoPin == cErrorCode:
            errorMessage = "Pin not found"
        elif PinGlueErrorCode.Timeout == cErrorCode:
            errorMessage = "Timeout"
        elif PinGlueErrorCode.PinExit == cErrorCode:
            errorMessage = "Pin Terminated"
        else:
            errorMessage = "Unknown Error"
        self._errorCode = cErrorCode
        super().__init__(errorMessage)
        
    def error_code(self) -> PinGlueError:
        return self._errorCode

cdef class CArgs:
    """A wrapper around the Args structure used to pass arguments to pinglue_open
       This wrapper makes sure the memory allocated for the knobs is valid for the entire
       lifetime of CArgs
    
       This wrapper will take the arguments from a list[str]
    """
    cdef t_pg_args _c_args
    cdef list _utfArgs

    def __cinit__(self, args: list[str] | None):
        self._c_args.argc = 0
        self._c_args.argv = NULL
        if args is not None:
            self._c_args.argc = len(args)
            if 0 != self._c_args.argc:
                self._utfArgs = []
                self._c_args.argv = <const char**>stdlib.malloc(self._c_args.argc*sizeof(char*))
                if not self._c_args.argv:
                    raise MemoryError()
                for ia, arg in enumerate(args):
                    uArg = arg.encode('UTF-8')
                    self._utfArgs.append(uArg)
                    self._c_args.argv[ia] = uArg

    def __dealloc__(self):
        stdlib.free(self._c_args.argv)                

    cdef t_pg_args* the_c_args(self):
        return &self._c_args

cdef class PinGlueScriptFragment:
    """Wraps a Pin glue script fragment
    """
    cdef t_pg_script_fragment* _fragment
    def __cinit__(self):
        pass

    def __init__(self):
        raise TypeError("This class cannot be instantiated directly.")
    
    def __dealloc__(self):
        if self._fragment:
            pinglue_script_fragment_free(self._fragment)
    
    @staticmethod
    cdef PinGlueScriptFragment from_ptr(t_pg_script_fragment* fragment):
        cdef PinGlueScriptFragment ret = PinGlueScriptFragment.__new__(PinGlueScriptFragment)
        ret._fragment = fragment
        return ret

cdef class PinGlueInstance:
    """Wraps a pin glue instance
    """

    cdef t_pin_glue* _pg
    cdef bint _started

    def __cinit__(self, pin_args: list[str] | None = None, glue_args: list[str] | None = None,
                 program: str | None = None, program_args: list[str] | None = None):
        """Open a Pin Glue instance

        Raises:
            TypeError
            ValueError
        """
        self._pg = NULL
        self._started = False
        if program is None or 0 == len(program):
            raise ValueError("Invalid program")

        pinArgs = CArgs(pin_args)
        glueArgs = CArgs(glue_args)
        programArgs = CArgs(program_args)

        uProgram = program.encode('UTF-8')
        cdef int eCode = pinglue_open(&self._pg, 
                                      pinArgs.the_c_args(), 
                                      glueArgs.the_c_args(), 
                                      uProgram, 
                                      programArgs.the_c_args())
        if 0 != eCode:
            raise PinGlueError(eCode)


    def __dealloc__(self):
        if self._pg:
            pinglue_close(self._pg)

    def start_program(self):
        if self._started:
            raise RuntimeError("Program already started")
        cdef int eCode = pinglue_start_program(self._pg)
        if 0 != eCode:
            raise PinGlueError(eCode)
        self._started = True
    
    def wait_until_done(self):
        if not self._started:
            raise RuntimeError("Program not started")
        cdef int eCode = 0
        cdef uint32_t timeoutUS = -1 
        with nogil:
            eCode = pinglue_finish(self._pg, timeoutUS)
        if 0 != eCode:
            raise PinGlueError(eCode)

cdef py_args_from_rpc_args(t_pg_remote_request* request): 
    args = list()

    # Build function args
    cdef const t_rpc_arg* arg
    cdef E_rpc_arg_type type
    cdef double tmp_double # cdef statement not allowed inside if

    for i in range(request.argsSize):
        arg = &(request.args[i])
        type = ARG_SCHEMA_TYPE(arg.argSchema)
        if type == E_rpc_arg_type.RpcBuffer:
            args.append((<char*>arg.argData).decode('UTF-8'))
        elif type == E_rpc_arg_type.RpcBoolean:
            args.append(bool(arg.argData))
        elif type == E_rpc_arg_type.RpcFloat:
            memcpy(&tmp_double, &arg.argData, sizeof(double))
            args.append(tmp_double)
        elif type == E_rpc_arg_type.RpcInt:
            args.append(<int64_t>arg.argData)
        elif type == E_rpc_arg_type.RpcUInt:
            args.append(<uint64_t>arg.argData)
        elif type == E_rpc_arg_type.RpcNil:
            args.append(None)
        else:
            raise TypeError(f"type not supported: {arg.argSchema.argType}")

    return args

cdef void free_rpc_arg_str(t_rpc_arg* arg, void* state) noexcept with gil:
    stdlib.free(<char*>arg.argData)    

cdef add_response_ret_from_py_ret(py_arg, t_pg_remote_response* response):
    """ Convert python return arg to rpc_arg and add it to response ret,
        in case of str it also allocates the response ret and assign to response.deleter the deleter of ret
    Args:
        py_arg (str): A python return argument
        response (t_pg_remote_response*): the response to handle it's ret
    
    Raises:
        TypeError: the type of py_arg is not supported yet for conversion
    """

    if py_arg is None:
        response.ret = MAKE_RPC_ARG(NIL_SCHEMA, 0, 0, RpcArgFlagsNone)
        return

    cdef t_rpc_arg* ret = &response.ret
    ret.flags = RpcArgFlagsNone
    ret.deleter = NULL

    argType = type(py_arg)

    cdef const char* uPyArg
    if argType is str:
        uPyArg = PyUnicode_AsUTF8(py_arg)
        size = 1 + len(uPyArg)
        ret.argSchema = BUFFER_SCHEMA
        ret.argData = <uint64_t>stdlib.malloc(size)
        ret.argDataSize = size
        memcpy(<char*>ret.argData, uPyArg, size)
        response.deleter = free_rpc_arg_str
        return

    cdef t_rpc_arg_schema schema

    # For negative values direct casting of a Python integer to uint64_t can raise an OverflowError,
    # so the following handle it by storing it first in long type
    cdef long val = py_arg
    ret.argData = <uint64_t>val # the default assigment for all types besides double

    cdef double temp_double # cdef statement not allowed inside if

    if argType is bool:
        schema = BOOL_SCHEMA
    elif argType is float:
        schema = FLOAT_SCHEMA
        temp_double = py_arg
        memcpy(&ret.argData, &temp_double, sizeof(double))
    elif argType is int:
        schema = INT_SCHEMA
    else:
        raise TypeError(f"type {argType} is not supported")

    ret.argSchema = schema
    ret.argDataSize = ARG_SCHEMA_SIZE(schema)


cdef void remote_request_cb(t_pg_remote_func* func, t_pg_remote_response* response) noexcept with gil:
    func_name = func.functionName.decode('UTF-8')
    unique_name = func.name.decode('UTF-8')
    response.handled = True
    response.cancel = False
    response.deleter = NULL

    service = __services.get(unique_name)
    if service is None:
        response.handled = False
        print(f"Service not found for {unique_name}, sendind to server to cancel the service")
        response.cancel = True
        return
    try:
        args = py_args_from_rpc_args(&func.request)
        ret = service.callback(func_name, *args)
        add_response_ret_from_py_ret(ret, response)
    except Exception as e:
        print(f"An exception occurred in remote_request_cb: {e}, sending reject response")
        # Will send reject response
        response.handled = False

cdef class PinGlueService:
    """A wrapper around a pinglue service object
    """

    cdef t_pg_service* _service
    cdef str _unique_name
    cdef object _callback

    def __cinit__(self, unique_name: str):
        global __services
        __services[unique_name] = self
        self._unique_name = unique_name
        pass
    
    def __init__(self):
        raise TypeError("This class cannot be instantiated directly.")

    def __dealloc__(self):
        global __services
        del __services[self._unique_name]
        if self._service:
            pinglue_service_close(self._service)

    def start(self):
        eCode = pinglue_service_start(self._service)
        if 0 != eCode:
            raise PinGlueError(eCode)

    @staticmethod
    cdef PinGlueService create(str unique_name, t_pin_glue* pg, const t_pg_service_request* request):
        cdef PinGlueService service = PinGlueService.__new__(PinGlueService, unique_name)
        cdef t_pg_service* temp = NULL
        eCode =  pinglue_service_open(&temp, pg, request)
        if 0 != eCode:
            raise PinGlueError(eCode)
        service._service = temp
        return service

    def wait_for_result(self, callback) -> str:
        """ Wait for service to finish processing any callbacks that occurs while waiting

        Args:
            callback (callable): A callbale object to call when a service calls a remote function
        
        Raises:
        """
        if not callable(callback):
            raise TypeError("callback must me callbale")
                
        self._callback = callback

        cdef E_pin_glue_ret eCode = E_pin_glue_ret.E_PG_Success
        cdef t_pg_service_result* result = NULL
        with nogil:
            while True:
                eCode = <E_pin_glue_ret>pinglue_service_wait(self._service, -1, remote_request_cb, &result)
                if E_pin_glue_ret.E_PG_Success == eCode:
                    if result is not NULL:
                        break
                    continue
                
                raise PinGlueError(eCode)

        return result.result.decode('UTF-8') # return the json string

    @property
    def callback(self):
        return self._callback

cdef class PinGlueServiceRequest:
    """A wrapper around a Pin Glue service request object
    """

    cdef t_pg_service_request _request
    cdef CArgs _knobs
    cdef bytes _service_name
    cdef list _fragments
    cdef list _script_remote_names

    cdef void _initialize_request_knobs(self, knobs):
        """Properly initialize request knobs making sure
           the memory is valid through out the lifetime of
           the request
        """
        self._knobs = CArgs(knobs)
        
        self._request.knobs = self._knobs._c_args.argv
        self._request.knobsSize = self._knobs._c_args.argc

    def __cinit__(self, service_name: str, knobs: list[str]):
        """Initialize a pinglue service request.
        The same request can be used to create request instances for multiple pinglue instances.

        Args:
            service_name (str): The service name. Can be None (in the future)
            knobs (list[str]): Knobs to pass to the service - may be None

        Raises:
            TypeError: service_name is not str or knobs is not list[str]
            ValueError: For now - if service_name is None or empty string
        """
        if service_name is None or 0 == len(service_name):
            # For now raise an exception - we will relax this in the future
            raise ValueError("Invalid service name")
        
        try:
            self._initialize_request_knobs(knobs)
        except TypeError:
            raise TypeError("knobs must be of type list[str]")
        
        self._service_name = service_name.encode('UTF-8')
        self._request.serviceName = self._service_name

        self._request.resultChannel = NULL  # Make sure pinglue creates a new channel for this service
        
        # The fields below will be filled just for the actual request that will be used
        # to create the service object
        self._request.callbackName = NULL
        self._request.scripts = NULL
        self._request.scriptsSize = 0
        memset(<void*>&self._request.name, 0, sizeof(self._request.name))

        self._fragments = [] # Empty list for tracking script fragments
        self._script_remote_names = [] # Empty list for tracking registered func names for remote function calls

    def __dealloc__(self):
        pass

    def add_server_script(self, script: str):
        """Create a service fragment from the given script code.
           This method does not validate the contents of the script

        Args:
            script (str): A string containig a server side code
        
        Raises:
            PinGlueError: script fragment cannot be created from script
            TypeError: script type is not str
            ValueError: script is None or empty string
        """
        if script is None or 0 == len(script):
            raise ValueError("Invalid script")

        uScript = script.encode('UTF-8')
        cdef t_pg_script_fragment* fragment = GLUE_server_script(uScript, len(uScript))
        if fragment is NULL:
            raise PinGlueError(int(PinGlueErrorCode.Internal))
        self._fragments.append(PinGlueScriptFragment.from_ptr(fragment))

    def add_remote_function_registration_script(self, script_func_name: str, remote_name: str, noblock: bool):
        """ Create and add script fragment to register a remote function call.

            The term remote may be misleasing. It is remote from the POV of the Pin glue server.
            This means that remote_name is actually the local name by which we identify the function
            on the client side (our side). script_name is the name that the service script will use
            on the server side to call the function.

            Args:
                script_func_name (str): The name by which se server side service script calls the function
                remote_name (str): The name given to the function by the user of this class to identify the
                                   function locally
                noblock (bool): Whether pinglue can avoid waiting for function to return/response

            Raises:
                 PinGlueError: script fragment cannot be created
                 TypeError: script_func_name or remote_name are not of type str
                 ValueError: script_func_name or remote_name are None or empty strings.
                             script_func_name already registered for remote function.
        """
        if script_func_name is None or 0 == len(script_func_name):
            raise ValueError("Invalid script_func_name")

        if remote_name is None or 0 == len(remote_name):
            raise ValueError("Invalid remote_name")

        if script_func_name in self._script_remote_names:
            raise ValueError(f"'{script_func_name}' already registered")
        
        uFuncName = script_func_name.encode('UTF-8')
        uRemoteName = remote_name.encode('UTF-8')
        cdef t_pg_script_fragment* fragment = GLUE_register_remote_function(NULL, uFuncName, uRemoteName, noblock)
        if fragment is NULL:
            raise PinGlueError(int(PinGlueErrorCode.Internal))
        self._fragments.append(PinGlueScriptFragment.from_ptr(fragment))
        self._script_remote_names.append(script_func_name)


    def create_service(self, PinGlueInstance pinglue) -> PinGlueService:
        """Create a service object from the request.
        The service object will be bound to the specific pinglue instance

        Args:
            pinglue (PinGlueInstance): The pinglue instance
        
        Raises:
            MemoryError: Not able to allocate script fragment array
        """

        cdef t_pg_service_request actualRequest
        memcpy(&actualRequest, &(self._request), sizeof(t_pg_service_request))

        randomName = _gen_random_string(5)
        while randomName in __services:
            randomName = _gen_random_string(5)

        callbackName = "cb_" + randomName

        uCallbackName = callbackName.encode('UTF-8')
        uRandomName = randomName.encode('UTF-8')

        actualRequest.callbackName = uCallbackName
        actualRequest.name[:5] = uRandomName

        cdef int scriptCount = len(self._fragments)
        cdef PinGlueScriptFragment temp
        if 0 != scriptCount:
            actualRequest.scripts = <const t_pg_script_fragment**>stdlib.malloc(scriptCount*sizeof(t_pg_script_fragment*))
            if not actualRequest.scripts:
                raise MemoryError()
            
            for idx, fragment in enumerate(self._fragments):
                temp = fragment # This is unfortunate but required because of cython C/Python interop rules
                actualRequest.scripts[idx] = temp._fragment

            actualRequest.scriptsSize = scriptCount

        try:
            return PinGlueService.create(randomName, pinglue._pg, &actualRequest)
        finally:
            stdlib.free(actualRequest.scripts)
