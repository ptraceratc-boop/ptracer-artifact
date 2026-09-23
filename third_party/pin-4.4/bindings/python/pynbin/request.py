#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

from enum import Enum



class RequestType(Enum):
    INSTRUMENTATION = 0
    SERVICE = 1
    PROGRAM_EVENT = 2


class InstrumentationType(Enum):
    INS = 0
    RTN = 1
    TRACE = 2
    IMG = 3
    BBL = 4

class PredicateOperation(Enum):
    And = "and"
    Or = "or"

class PinGlueScriptFragment:
    """Represents a server side script fragment
    """

class Request:
    """This class is the base class for PinGlue requests"""

    def request_type(self) -> RequestType:
        """Return the request type.

        This method should be implemented by derived classes

        Returns:
            RequestType
        """
        raise NotImplementedError()

    def send(self, pin):
        """Send the request
        
        """
        raise NotImplementedError()


class Instrumentation(Request):
    """This class is the base class for all Instrumentation objects
    
    """

    def __predicate__(itype: InstrumentationType) -> PinGlueScriptFragment:
        """Return the PinGlueScriptFragment corresponding to the give instrumentation type
        
        Args:
            itype (InstrumentationType): The instrumentation type for which to return the predicate
                                         script fragment. If the Instrumentation object
                                         does not hold a predicate for the given type None shall be returned.

        Returns:
            PinGlueScriptFragment: The script fragment for the given instrumentation type or None
        """
        raise NotImplementedError()

    def request_type(self) -> RequestType:
        return RequestType.INSTRUMENTATION

    def instrumentation_type(self) -> InstrumentationType:
        """Return the instrumentation type for the object
        
        This method should be implemented by derived classes

        Returns:
            InstrumentationType
        """
        raise NotImplementedError()

    def send(self, pin):
        from .pynbin import Pin

        if not isinstance(pin, Pin):
            raise TypeError()

        raise NotImplementedError()