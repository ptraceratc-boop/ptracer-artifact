#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

from __future__ import annotations

from inspect import isfunction
from typing import Any


class DirectCallError(Exception):
    """This exception is raised if a GlueCallback is called directly"""
    pass


class GlueCallbackMismatchError(Exception):
    """This exception is raised if a server script GlueCallback is mixed with other types of Glue_callbacks"""
    pass


class GlueCallback:
    """Base class for Pin glue callbacks
    
    """

    def __init__(self,
                 func,
                 remote_name: str = None,
                 noblock: bool = None,
                 server_script: bool = None):
        """Initialize a GlueCallback instance
        
        Args:
            func (Callable): A function to be called for the callback. May be None for Server Side functions
            remote_name (str): The name of the remote callback
            noblock (bool): Whether pinglue can avoid waiting for function to return
            server_script(bool): Whether it's a server script

        Raises:
            TypeError
            ValueError

        """
        if func is None:
            raise ValueError("func must have a value")
        if not callable(func):
            raise TypeError("func must me callbale")

        self.__server_script = server_script

        if isinstance(func, GlueCallback):
            if server_script != func.__server_script:
                raise GlueCallbackMismatchError()
            self.__func = func.__func
            self.__remote_name = func.__remote_name if remote_name is None else remote_name
            self.__noblock = func.__noblock if noblock is None else noblock
        else:
            self.__func = func
            self.__remote_name = remote_name
            self.__noblock = noblock

    def __call__(self, *args, **kwargs):
        raise DirectCallError("{self.__remote_name} is a remote callback")

    @property
    def __name__(self) -> str:
        return self.remote_name
    
    @property
    def function(self):
        return self.__func

    @property
    def local_name(self) -> str:
        return function.__name__

    @property
    def remote_name(self) -> str:
        if self.__remote_name is None:
            return self.__func.__name__
        return self.__remote_name

    @property
    def noblock(self) -> bool:
        if self.__noblock is None:
            return False
        return self.__noblock

    @property
    def server_script(self) -> bool:
        if self.__server_script is None:
            return False
        return self.__server_script


class GlueCallbackManager:
    """Holds GlueCallbacks
       and executes them or queues them for execution
    """

    def __init__(self) -> None:
        """"""
        self._callbacks = {}

    def __call__(self, func_name: str, *args: Any, **kwds: Any) -> Any:
        callback = self._callbacks.get(func_name)
        if callback is None:
            raise RuntimeError(f"Callback {func_name} not found")
        
        # For now we support only in *args
        return callback.function(*args)
        

    def register_callback(self, callback_name_key: str,
                          callback: GlueCallback):
        """_summary_

        Args:
            callback_name_key (str): _description_
            callback (GlueCallback): _description_

        Raises:
            TypeError: _description_
            TypeError: _description_
        """
        if not isinstance(callback_name_key, str):
            raise TypeError("callback_name_key must be of type str")
        if not isinstance(callback, GlueCallback):
            raise TypeError("callback must be a GlueCallback")
        self._callbacks[callback_name_key] = callback


def pinglue_callback(f) -> GlueCallback:
    """A decorator to mark a function as a callback target for pinglue server messages

    The underlying object name will be used as the remote name

    Raises:
        TypeError
        ValueError
        GlueCallbackMismatchError

    """
    return GlueCallback(f)


def pinglue_function(*args, remote_name: str = None) -> GlueCallback:
    """A decorator to mark a function as a callback target for pinglue server messages, optionally passing a remote name

    If remote_name is not specified then the name of the underlying function name will be used

    Args:
        remote_name (str): Optional remote (server side) function name

    Raises:
        TypeError
        ValueError
        GlueCallbackMismatchError
    """

    def inner(f):
        return GlueCallback(f, remote_name=remote_name)

    if 1 == len(args):
        return GlueCallback(args[0])

    return inner


def pinglue_noblock(f) -> GlueCallback:
    """A decorator to mark a function as a callback target for pinglue server messages.
       This decorator also marks that the server should not wait for the function to return.

    Raises:
        TypeError
        ValueError
        GlueCallbackMismatchError

    """
    # TODO PINT-6033
    return GlueCallback(f, noblock=True)


def pinglue_server_script(*args, remote_name: str = None) -> GlueCallback:
    """A decorator to mark a function as a server script.
       This decorator cannot be combined with other callback decorators.
    
    If remote_name is not specified then the name of the underlying function name will be used
    
    Args:
    remote_name (str): Optional remote (server side) function name

    Raises:
        TypeError
        ValueError
        GlueCallbackMismatchError

    """

    def inner(f):
        if not isfunction(f):
            raise TypeError(
                "For server scripts the underlying function must be a simple Python function"
            )
        return GlueCallback(f, remote_name=remote_name, server_script=True)

    if 1 == len(args):
        if not isfunction(args[0]):
            raise TypeError(
                "For server scripts the underlying function must be a simple Python functions"
            )
        return GlueCallback(args[0], args[0].__name__, server_script=True)

    return inner
