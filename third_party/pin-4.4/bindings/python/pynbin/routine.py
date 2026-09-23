#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

from __future__ import annotations

from .request import Instrumentation, InstrumentationType
from .callbacks import GlueCallback
from .image import Img

class Rtn(Instrumentation):
    """This class is used to specify routine instrumentation conditions
       and operations.

    """

    def instrumentation_type(self) -> InstrumentationType:
        """ Override instrumentation_type.
        
        Returns:
             InstrumentationType.RTN
        """
        return InstrumentationType.RTN

    def in_image(self, img: Img | str) -> Rtn:
        """_summary_

        Args:
            img (Img): The image instrumentation object to filter by.
                       If a string is passed we construct the filter as:
                       img = Img().name(img)

        Raises:
            TypeError: img is not of type Img or not of type str
    
        Returns:
            The Rtn object (self)
        """
        raise NotImplementedError()

        return self
