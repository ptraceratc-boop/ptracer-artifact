#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#
from __future__ import annotations

from .pypinglue import PinGlueInstance
from .request import Request
import argparse

class Pin:
    """ A class that represents a Pin + pinglueserver + instrumented program instance 
    """

    def __init__(self,
                 program: str,
                 program_args: list[str] | None = None,
                 pin_args: list[str] | None = None,
                 glue_args: list[str] | None = None):
        """
        Parameters
        ----------
        program (str):
               The path to the program to instrument
        program_args (list[str]):
               The arguments to pass to the program
        pin_args (list[str]):
               The arguments to pass to Pin
        pin_args (list[str]):
               The arguments to pass to pinglueserver
        
        """
        self._program = program
        self._program_args = program_args
        self._pin_args = pin_args
        self._glue_args = glue_args
        self.__pin = PinGlueInstance(pin_args, glue_args, program,
                                     program_args)

    @staticmethod
    def parse_args(args: list[str]|None = None, *,services_path: str|None = None) -> Pin:
        """Parse command line arguments and create a Pin instance
        To run with cmd args, run:
        <python_main.py> --pinargs="arg1 arg2 ..." --glueargs="arg1 arg2 ...>" -- <program> <prog_arg1> <prog_arg2> ...
        When -- <program> MUST be passed at the end, and the following are OPTIONAL:
        OPTIONAL: --pinargs="arg1 arg2 ..."
        OPTIONAL: --glueargs ="arg1 arg2 ..."
        OPTIONAL after -- <program>: <prog_arg1> <prog_arg2> ... 
        
        Args:
            args (list[str]|None): Optional list of arguments to parse
            services_path (str|None): Optional path to the services directory.
                                      The value passed here will only be used if `-services-path` is
                                      not passed as part of glueargs

        Raises
        ------
        ValueError

        Returns:
            A new Pin instance
        """
        
        parser = argparse.ArgumentParser(description="Pin Glue options")
        
        parser.add_argument("--pinargs",
                            type=str,
                            help="Optional: List of pin arguments enclosed in quotes, separated by space")
        parser.add_argument("--glueargs",
                            type=str,
                            help="Optional: List of glue arguments enclosed in quotes, separated by space")
        parser.add_argument("program", nargs="+")
        
        args = parser.parse_args(args = args)

        extra_glue_args = []
        if services_path:
            extra_glue_args.extend(["-services-path", services_path])

        # Convert string args to list     
        if args.pinargs:
            args.pinargs = args.pinargs.strip('"').split()    
        if args.glueargs:
            args.glueargs = args.glueargs.strip('"').split()
            if "-services-path" not in args.glueargs:
                args.glueargs.extend(extra_glue_args)
        else:
            args.glueargs = extra_glue_args

        return Pin(program=args.program[0], 
			       program_args = args.program[1:],
                   pin_args = args.pinargs,
                   glue_args = args.glueargs)

    def instrument(self, request0: Request, *request_objs):
        """Send instrumentation requests for the passed instrumentation objects

        Args:
            request0 (Request): The instrumentation object to send a request for
            *request_objs: Optional other instrumentation objects to create requests for

        Raises:
            TypeError
            ValueError

        """
        request0.send(self)
        if request_objs:
            for request in request_objs:
                request.send(self)

    def start(self, wait_done: bool = True):
        """Start the instrumented program. This call blocks until Pin exits.

        """
        self._done = False
        self.__pin.start_program()
        if wait_done:
            self.wait_until_done()
    
    def wait_until_done(self):
        """ Wait until pin finishes execution
        """
        if not self._done:
            self.__pin.wait_until_done()
            self._done = True

    @property
    def pin(self):
        """Get the pinglue instance

        Returns:
            PinGlueInstance:
        """
        return self.__pin
    
    @property
    def program(self):
        """Get the path for the instrumented program

        Returns:
            str: The path to the instrumented program as passed to init()
        """
        return self._program

    @property
    def program_args(self):
        """Get instrumented program arguments list

        Returns:
            list[str]: Instrumented program argument list as passed to init()
        """
        return self._program_args

    @property
    def pin_args(self):
        """Get Pin argument list

        Returns:
            list[str]: Pin argument list as passed to init()
        """
        return self._pin_args
    
    @property
    def glue_args(self):
        """Get Pin Glue argument list

        Returns:
            _list[str]: Pin Glue argument list as passed to init()
        """
        return self._glue_args
