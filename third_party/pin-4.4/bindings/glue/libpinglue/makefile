#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

OBJ_SUFFIX := .o
AR_SUFFIX := .a
EXE_SUFFIX := .exe

PINRTDIR := $(PINKIT)/$(TARGET)/pinrt
OBJDIR := obj-$(TARGET)
LIBDIR := lib-$(TARGET)

CFLAGS = -pthread -fPIC
CXXFLAGS = -std=c++17 -fPIC -pthread
CPPFLAGS = 
LDFLAGS := -Wl,--exclude-libs=ALL -pie
LIBS :=  \
		-lpinglue \
		 $(PINRTDIR)/lib/librscproto.a \
		 $(PINRTDIR)/lib/libpinos.a \
		 -lpthread
		 


ifeq ($(DEBUG),1)
	CPPFLAGS += -DDEBUG=1
	OPTFLAGS := -O0
	DBGFLAGS := -g
else
	OPTFLAGS := -O2
	DBGFLAGS :=
endif


ifeq ($(TARGET),ia32)
	CFLAGS += -m32
	CXXFLAGS += -m32
	LDFLAGS += -m32
else
	CFLAGS += -m64
	CXXFLAGS += -m64
	LDFLAGS += -m64
endif

INCLUDES := \
	-I$(PINKIT)/bindings/glue/libpinglue/include \
	-I$(PINKIT)/bindings/glue/include \
	-I$(PINRTDIR)/include/adaptor \
	-I$(PINRTDIR)/include/pinos

SOURCES := pinglue.cpp
OBJECTS := $(patsubst %.cpp,$(OBJDIR)/%$(OBJ_SUFFIX),$(SOURCES))

TEST_OBJECTS := test_libpinglue.o

CXXFLAGS := $(CXXFLAGS) $(OPTFLAGS) $(DBGFLAGS)

.PHONY: all clean cleanall test test_libpinglue

all: libpinglue.build

clean:
	rm -rf $(OBJDIR)

cleanall: clean
	rm -rf $(LIBDIR)

$(OBJDIR):
	mkdir -p $@

$(LIBDIR):
	mkdir -p $@

libpinglue.build: $(LIBDIR)/libpinglue$(AR_SUFFIX)

test: export GLUE_PIN_KIT := $(PINKIT)
test: test_libpinglue
	$(OBJDIR)/test_libpinglue$(EXE_SUFFIX) $(TARGET)

test_libpinglue: $(OBJDIR)/test_libpinglue$(EXE_SUFFIX)

$(OBJDIR)/test_libpinglue$(EXE_SUFFIX): $(LIBDIR)/libpinglue$(AR_SUFFIX)
$(OBJDIR)/test_libpinglue$(EXE_SUFFIX): $(TEST_OBJECTS) | $(OBJDIR)
	$(CXX) -g $(LDFLAGS) -L$(LIBDIR) -o $@ $(OBJDIR)/$^ $(LIBS)

$(TEST_OBJECTS): %.o: test/%.c | $(OBJDIR)
	$(CC) -g $(CFLAGS) $(INCLUDES) -c -o $(OBJDIR)/$@ $<

$(LIBDIR)/libpinglue$(AR_SUFFIX): $(OBJECTS) | $(LIBDIR)
	$(AR) rcs $@ $^

$(OBJECTS):  $(SOURCES) | $(OBJDIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

