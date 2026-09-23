#
# Copyright (C) 2024-2026 Intel Corporation.
# SPDX-License-Identifier: MIT
#

AR_SUFFIX := .a

ifndef HOST_ARCH
  HST := $(shell  uname -m)
  ifeq ($(HST),x86)
    HOST_ARCH := ia32
  endif
  ifeq ($(HST),i386)
    HOST_ARCH := ia32
  endif
  ifeq ($(HST),i686)
    HOST_ARCH := ia32
  endif
  ifeq ($(HST),x86_64)
    HOST_ARCH := intel64
  endif
  ifeq ($(HST),amd64)
    HOST_ARCH := intel64
  endif
  # Verify known host
  ifndef HOST_ARCH
    $(error Could not detect the host architecture. Please define HOST_ARCH from the command line.)
  endif
endif

# Define the architecture of the target
TARGET ?= $(HOST_ARCH)

# Tool flags
# Extra arguments forwarded to pip (for example: --proxy).
PIP_GLOBAL_ARGS ?=

# NATIVE_CXX Generally should not be overridden directly. Instead, set CXX to the desired
# compiler for libpinglue and pynbin components, which will also be used for pin tools
# unless CXX_FOR_PIN is explicitly set.
# If CXX is set without CXX_FOR_PIN, the same compiler will be used for pin tools (pinglueserver) as well.
## The C++ compiler. Honors environment/command-line CXX when provided.
CXX ?= g++
# Native host C++ compiler used as the default toolchain.
NATIVE_CXX ?= $(CXX)
# Compiler used for Pin Tool side components (pinglueserver). Defaults to the resolved CXX value.
CXX_FOR_PIN ?= $(CXX)
# Check to see that we have python3 and pip3 installed

export PYTHON3 ?= $(shell which python3)
export PINKIT := $(PINKIT)

.PHONY: all clean cleanall pinglueserver pinglueserver-full pinglueserver-install \
        libpinglue pypinglue test \
			  pinglueserver.test libpinglue.test pypinglue.test pynbin.test \
			  pinglueserver-full.test libpinglue-full.test pypinglue-full.test pynbin-full.test \
			  pinglueserver.setup libpinglue.setup pypinglue.setup \
			  pinglueserver-full.setup libpinglue-full.setup pypinglue-full.setup

all: pinglueserver libpinglue pypinglue

pinglueserver pinglueserver-full:
	$(MAKE) -C $(PINKIT)/bindings/glue/pinglueserver -f makefile TARGET=$(TARGET) pinglueserver.build CXX=$(CXX_FOR_PIN) APP_CXX=$(NATIVE_CXX)

pinglueserver-install pinglueserver.setup pinglueserver-full.setup:
	$(MAKE) -C $(PINKIT)/bindings/glue/pinglueserver -f makefile TARGET=$(TARGET) install CXX=$(CXX_FOR_PIN)  APP_CXX=$(NATIVE_CXX)

libpinglue libpinglue.setup libpinglue-full.setup:
	$(MAKE) -C $(PINKIT)/bindings/glue/libpinglue -f makefile TARGET=$(HOST_ARCH) CXX=$(NATIVE_CXX)

pypinglue pypinglue.setup pypinglue-full.setup: libpinglue
	$(MAKE) -C $(PINKIT)/bindings/python/pynbin/cymod/pinglue -f makefile TARGET=$(HOST_ARCH) PIP_GLOBAL_ARGS="$(PIP_GLOBAL_ARGS)" CXX=$(NATIVE_CXX)

clean:
	$(MAKE) -C $(PINKIT)/bindings/glue/pinglueserver -f makefile TARGET=ia32 clean
	$(MAKE) -C $(PINKIT)/bindings/glue/pinglueserver -f makefile TARGET=intel64 clean
	$(MAKE) -C $(PINKIT)/bindings/glue/libpinglue -f makefile TARGET=$(TARGET) clean
	$(MAKE) -C $(PINKIT)/bindings/python/pynbin/cymod/pinglue -f makefile TARGET=$(TARGET) clean

cleanall:
	$(MAKE) -C $(PINKIT)/bindings/glue/pinglueserver -f makefile TARGET=ia32 clean
	$(MAKE) -C $(PINKIT)/bindings/glue/pinglueserver -f makefile TARGET=intel64 clean
	$(MAKE) -C $(PINKIT)/bindings/glue/libpinglue -f makefile TARGET=$(TARGET) cleanall
	$(MAKE) -C $(PINKIT)/bindings/python/pynbin/cymod/pinglue -f makefile TARGET=$(TARGET) cleanall

test: pinglueserver-full.test libpinglue-full.test pynbin-full.test

pinglueserver.test: pinglueserver
	$(MAKE) -C $(PINKIT)/bindings/glue/pinglueserver -f makefile TARGET=$(TARGET) sanity CXX=$(CXX_FOR_PIN)  APP_CXX=$(NATIVE_CXX)

libpinglue.test: pinglueserver-install libpinglue
	$(MAKE) -C $(PINKIT)/bindings/glue/libpinglue -f makefile TARGET=$(TARGET) test CXX=$(NATIVE_CXX)

pypinglue.test pynbin.test: pinglueserver-install pypinglue
	$(MAKE) -C $(PINKIT)/bindings/python/tests -f makefile TARGET=$(TARGET) sanity CXX=$(NATIVE_CXX)

pinglueserver-full.test: pinglueserver
	$(MAKE) -C $(PINKIT)/bindings/glue/pinglueserver -f makefile TARGET=$(TARGET) test CXX=$(CXX_FOR_PIN)  APP_CXX=$(NATIVE_CXX)

libpinglue-full.test: pinglueserver-install libpinglue
	$(MAKE) -C $(PINKIT)/bindings/glue/libpinglue -f makefile TARGET=$(TARGET) test CXX=$(NATIVE_CXX)

pypinglue-full.test pynbin-full.test: pinglueserver-install pypinglue
	$(MAKE) -C $(PINKIT)/bindings/python/tests -f makefile TARGET=$(TARGET) test CXX=$(NATIVE_CXX)
