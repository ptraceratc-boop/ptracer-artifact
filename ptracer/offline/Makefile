# PTracer offline stage: ptrecon (Intel PT decode + VEX skeleton interpretation), pt_capture2
# (perf_event Intel PT capture with a sideband), ptpktscan (packet counts of a saved AUX file).
#
# Dependencies: libipt (system, -lipt), Zydis (static library from the E9Patch tree) and libpyvex
# (from the Python environment that has pyvex installed).  Every path below can be overridden on
# the command line or in the environment:
#   PY     python interpreter whose `pyvex' package supplies libpyvex (default: python3)
#   PYVEX  the pyvex package directory (default: asked from $(PY))
#   ZYDIS  the Zydis source tree with libZydis.a built (default: ../../third_party/e9patch/contrib/zydis)
PY    ?= python3
PYVEX ?= $(shell $(PY) -c 'import pyvex,os;print(os.path.dirname(pyvex.__file__))')
ZYDIS ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../third_party/e9patch/contrib/zydis)
CXX   ?= g++
CC    ?= gcc
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wno-unused-function -I$(PYVEX)/include -I$(ZYDIS)/include -I$(ZYDIS)/dependencies/zycore/include -DZYDIS_STATIC_BUILD -DZYCORE_STATIC_BUILD
LDFLAGS  := -L$(PYVEX)/lib -lpyvex -Wl,-rpath,$(PYVEX)/lib $(ZYDIS)/libZydis.a -lipt

all: ptrecon pt_capture2 ptpktscan tests/vextest tests/gtstream_test

HDRS := vexlift.h vexinterp.h ptdecode.h recon.h e9phase.h json.h trace_format.h gtstream.h ../runtime/rt/ptgt_format.h

ptrecon: main.o recon.o recon_par.o ptdecode.o e9phase.o vexinterp.o vexlift.o
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# pt_capture2: raw perf_event_open Intel PT capture of one command (per-task, or per-CPU with
# --cpu), with the draining AUX writer, the /proc/PID/maps sideband and the spare-TCB hand-off
# to new threads of a buffer-sink runtime (../runtime/rt/ptlog_abi.h).
pt_capture2: pt_capture2.c ../runtime/rt/ptlog_abi.h
	$(CC) -O2 -Wall -Wno-stringop-truncation -o $@ $< -lipt -lpthread

# ptpktscan: count PT packet types in an AUX file that is already on disk.  A capture taken with
# --no-decode writes the AUX and no packet summary, and a capture can have lost = 0 /
# truncated = 0 (the drain kept up) and still be full of on-chip OVF packets (the packet
# generator did not).  OVF is trace loss, so it has to be counted.
ptpktscan: ptpktscan.c
	$(CC) -O2 -Wall -o $@ $< -lipt

# Hardware-free unit tests: the VEX lifter/interpreter on hand-assembled bytes, and the
# ground-truth stream reader on synthetic records.
tests/vextest: tests/vextest.cpp vexinterp.o vexlift.o
	$(CXX) $(CXXFLAGS) -o $@ $< vexinterp.o vexlift.o $(LDFLAGS)

tests/gtstream_test: tests/gtstream_test.cpp gtstream.h ../runtime/rt/ptgt_format.h
	$(CXX) -std=c++17 -O2 -Wall -Wextra -Werror -o $@ $<

test: tests/vextest tests/gtstream_test
	tests/gtstream_test && tests/vextest

clean:
	rm -f *.o ptrecon pt_capture2 ptpktscan tests/vextest tests/gtstream_test
.PHONY: all clean test
