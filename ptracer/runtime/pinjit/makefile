# Builds the HiFi Pintool (hifitool.so) and the no-op floor (nooptool.so):
#     make PIN_ROOT=<Pin kit root> obj-intel64/nooptool.so obj-intel64/hifitool.so
# Without PIN_ROOT the kit is taken from <artifact root>/third_party/pin-4.4,
# located relative to this file.
ifndef PIN_ROOT
PIN_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../../third_party/pin-4.4)
endif
CONFIG_ROOT := $(PIN_ROOT)/source/tools/Config
include $(CONFIG_ROOT)/makefile.config
include makefile.rules
include $(TOOLS_ROOT)/Config/makefile.default.rules
