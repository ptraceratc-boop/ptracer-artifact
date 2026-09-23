#
# Copyright (C) 2024-2026 Intel Corporation.
# SPDX-License-Identifier: MIT
#

PIP_GLOBAL_ARGS ?=

SO_SUFFIX := .so

PINRTDIR := $(PINKIT)/$(TARGET)/pinrt
PYPINGLUE := $(PINKIT)/bindings/python/pynbin/pypinglue${SO_SUFFIX}

CFLAGS := -fPIC -O2
CPPFLAGS :=

LDFLAGS := -fPIC -Wl,--exclude-libs=ALL

ifeq ($(TARGET),ia32)
	CFLAGS += -m32
	LDFLAGS += -m32
else
	CFLAGS += -m64
	LDFLAGS += -m64
endif

PYTHON3DIR := $(realpath $(dir $(PYTHON3)))

INCLUDES := \
	-I$(PINKIT)/bindings/glue/libpinglue/include \
	-I$(PINKIT)/bindings/glue/include \
	-I$(PINRTDIR)/include/adaptor \
	-I$(PINRTDIR)/include/pinos \
	 $(shell $(PYTHON3DIR)/python3-config --includes)

LIBDIRS := \
	-L$(PINKIT)/bindings/glue/libpinglue/lib-$(TARGET)

LIBS :=  -lpinglue \
		 $(PINRTDIR)/lib/librscproto.a \
		 $(PINRTDIR)/lib/libpinos.a \
		 -lpthread -lrt -lstdc++ \
		 $(shell $(PYTHON3DIR)/python3-config --libs)

PYX_SOURCES := pypinglue.pyx
SOURCES := $(patsubst %.pyx,%.c,$(PYX_SOURCES))
OBJECTS := $(patsubst %.c,%.o,$(SOURCES))

export VIRTUAL_ENV := .venv
USER_SITE_PACKAGES := $(shell $(PYTHON3) -m site --user-site)
ifneq ($(wildcard $(USER_SITE_PACKAGES)/pip ), )
  $(info Found site packages at $(USER_SITE_PACKAGES))
  SITE_PACKAGES := $(USER_SITE_PACKAGES)
else
  PIP_CANDIDATES := $(shell $(PYTHON3) -c 'import site; print(*site.getsitepackages())')
  VALID_PIP_CANDIDATES := $(foreach candidate,$(PIP_CANDIDATES),$(wildcard $(candidate)/pip))
  PIP_PATH := $(firstword $(VALID_PIP_CANDIDATES))
  ifneq ($(PIP_PATH),)
	SITE_PACKAGES := $(dir $(PIP_PATH))
    $(info Found site packages at $(SITE_PACKAGES))
  else
    $(error Cannot find python site packages)
  endif
endif

ENV_SITE_PACKAGES := $(VIRTUAL_ENV)/$(shell echo $(SITE_PACKAGES) | sed 's|.*/lib\(.*\)|lib\1|')

.PHONY: clean cleanall all

all: pypinglue.build

clean:
	rm -f *.o $(PYPINGLUE)

cleanall: clean
	rm -f *.c
	rm -rf $(VIRTUAL_ENV) pyenv

# Always link
pypinglue.build: | $(OBJECTS)
	$(CC) -shared $(LIBDIRS) $(LDFLAGS) -o $(PYPINGLUE) $(OBJECTS) $(LIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.c: %.pyx | pyenv
	$(VIRTUAL_ENV)/bin/python3 -m cython -3 -o $@ $<

# We always start with a clean environment
pyenv:
	rm -rf $(VIRTUAL_ENV)
	mkdir -p $(VIRTUAL_ENV)/bin
	mkdir -p $(ENV_SITE_PACKAGES)
	cp -R $(SITE_PACKAGES)/pip $(ENV_SITE_PACKAGES)/
	ln -s $(PYTHON3) $(VIRTUAL_ENV)/bin/python3
	ln -s lib $(VIRTUAL_ENV)/lib64
	echo home = $(dir $(PYTHON3)) > $(VIRTUAL_ENV)/pvyenv.cfg
	echo include-system-site-packages = false >> $(VIRTUAL_ENV)/pyvenv.cfg
	echo $(shell $(PYTHON3) -V | sed 's/Python /version = /') >> $(VIRTUAL_ENV)/pyvenv.cfg
	$(VIRTUAL_ENV)/bin/python3 -m pip -v $(PIP_GLOBAL_ARGS) install --upgrade pip
	$(VIRTUAL_ENV)/bin/python3 -m pip -v $(PIP_GLOBAL_ARGS) install --upgrade cython
	touch $@
