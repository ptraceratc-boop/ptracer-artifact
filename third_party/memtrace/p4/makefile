# Build the SAME memtrace source against an explicit Pin 4.x kit, without
# replacing the Pin 3.20 build in ../obj-intel64 (baselines_slice.md, 2026-09-22).
ifndef PIN_ROOT
$(error PIN_ROOT must name the installed Pin 4.x kit)
endif
CONFIG_ROOT := $(PIN_ROOT)/source/tools/Config
include $(CONFIG_ROOT)/makefile.config
include makefile.rules
include $(TOOLS_ROOT)/Config/makefile.default.rules
