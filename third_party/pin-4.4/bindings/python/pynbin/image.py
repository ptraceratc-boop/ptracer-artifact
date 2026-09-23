#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

from __future__ import annotations

from .request import Instrumentation, InstrumentationType
from .callbacks import GlueCallback

class Img(Instrumentation):
    """This class is used to specify image instrumentation conditions
       and operations.

    """

    def instrumentation_type(self) -> InstrumentationType:
        """ Override instrumentation_type.
        
        Returns:
             InstrumentationType.IMG
        """
        return InstrumentationType.IMG
