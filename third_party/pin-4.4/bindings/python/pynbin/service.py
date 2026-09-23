#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

from __future__ import annotations
from typing import Any
from inspect import getsourcelines

from .request import Request, RequestType
from .callbacks import GlueCallback, GlueCallbackManager
from .pynbin import Pin
from .pypinglue import PinGlueServiceRequest


class Service(Request):
    """Represents a service request"""

    def __init__(self, name: str, knobs: list[str] = []):
        """Initialize a service request

        Args:
            knobs (list[str]): List of knobs to pass to the service
            name (str): The name of the service
        """
        self._service = None
        self._service_request = PinGlueServiceRequest(name, knobs)
        self._callbackManager = GlueCallbackManager()

    def __call__(self, func_name: str, *args: Any, **kwds: Any) -> Any:
        return self._callbackManager(func_name, *args, kwds = kwds)

    def request_type(self) -> RequestType:
        return RequestType.SERVICE
    
    @staticmethod
    def get_undecorated_function_src(func: function, func_name: str, remote_name: str | None = None) -> str:
        """Returns a string with function definition without the decorator, 
           in case remote_name is not None it means also to replace the func_name with remote_name 

        Args:
            func (function): The function (with decorator or without)
            func_name (str): The original name of the function
            remote_name (str or None): The remote name.

        Returns:
            str: result string
        """
        lines, lnum = getsourcelines(func)
        defIdx = -1
        
        for i, line in enumerate(lines): 
            if line.startswith("def "):
                defIdx = i
                if remote_name is not None:
                    lines[i] = line.replace(func_name, remote_name, 1)
                
                break
            
        if -1 == defIdx:
           raise ValueError("Invalid remote function source")
       
        return ''.join(lines[defIdx:])

    def with_function_target(self,
                             callback: GlueCallback,
                             *,
                             remote_name: str | None = None) -> Service:
        """Return the service object after adding a remote callback to it
        
        Args:
        callback (GlueCallback): The callback object.
        remote_name (str): The remote callback name. If remote_name is None then the name
                     will be taken from the callback object. If the remote_name in the callback
                     object is also None then the name of the wrapped function will be used

        
        Raises:
            ValueError: callback is None or "def " not found in callback.function
            TypeError: callback is not a GlueCallback or remote_name is not str
        """

        if not isinstance(callback, GlueCallback):
            raise TypeError("callback must be a valid GlueCallback")

        if remote_name is not None and not isinstance(remote_name, str):
            raise TypeError("remote_name must be a str or None")

        if callback is None:
            raise ValueError("callback must be a local function")
        
        if remote_name is None:
            remote_name = callback.remote_name

        if callback.server_script:
            serverScript = Service.get_undecorated_function_src(callback.function, callback.function.__name__, remote_name)
            self._service_request.add_server_script(serverScript)
        else:
            if remote_name is None:
                raise ValueError(
                    "callback must have a remote name or one should be passed to with_function_target"
                )
            
            clientName = callback.function.__name__
            self._service_request.add_remote_function_registration_script(
                remote_name, clientName, callback.noblock)
            self._callbackManager.register_callback(
                clientName, callback)

        return self

    def send(self, pin: Pin):
        """_summary_

        Args:
            pin (Pin): _description_
        """
        self._service = self._service_request.create_service(pin.pin)
        self._service.start()

    def wait_for_result(self) -> str:
        """_summary_

        Raises:
            RuntimeError: _description_

        Returns:
            str: JSON result string
        """
        if self._service is None:
            raise RuntimeError()
        return self._service.wait_for_result(self)
