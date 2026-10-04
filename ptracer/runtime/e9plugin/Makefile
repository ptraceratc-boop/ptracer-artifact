# PTracer Stage-2 build.  `make` builds the e9tool plugin, both flavours of
# the injected buffer-sink runtime, the LD_PRELOAD flavour (used by the JIT
# front ends), the pthread shim and the AT_ENTRY shim.
#
# E9 is the E9Patch checkout (e9tool, e9patch, e9compile.sh, src/e9tool/e9plugin.h):
# <root>/third_party/e9patch by default, relative to this Makefile.  e9compile.sh
# must run with the checkout as its working directory and writes its output
# there (moved out again below), so the checkout has to be writable.
E9      ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../third_party/e9patch)
CXXFLAGS = -std=c++17 -fPIC -shared -O2 -Wall -I $(E9)/src/e9tool

all: ptlog.so ../rt/ptlogrt.e9rt ../rt/ptlogrt_nofini.e9rt ../rt/ptlogrt.so ../rt/ptlogmt.so ../rt/ptlogmt_sync.so ../rt/ldfix.e9rt

ptlog.so: ptlog.cpp ../rt/ptlog_abi.h ../rt/ptgt_format.h
	g++ $(CXXFLAGS) -o $@ $<

../rt/ptlogrt.e9rt: ../rt/ptlogrt.c ../rt/ptlog_abi.h ../rt/ptgt_format.h
	cd $(E9) && NO_SIMD_CHECK=1 ./e9compile.sh $(abspath $<) -DPTLOG_E9RT
	mv $(E9)/ptlogrt $@

# libc.so.6 has no DT_FINI/DT_FINI_ARRAY for E9Patch to hook, so it gets a
# flavour of the runtime that exports no fini() at all.
# Both e9compile invocations use the SAME ptlogrt.o/ptlogrt filenames, so they
# are serialised (the dependency on ptlogrt.e9rt) even under make -j.
../rt/ptlogrt_nofini.e9rt: ../rt/ptlogrt.c ../rt/ptlog_abi.h ../rt/ptgt_format.h ../rt/ptlogrt.e9rt
	cd $(E9) && NO_SIMD_CHECK=1 ./e9compile.sh $(abspath $<) -DPTLOG_E9RT -DPTLOG_NO_FINI
	mv $(E9)/ptlogrt $@

../rt/ptlogrt.so: ../rt/ptlogrt.c ../rt/ptlog_abi.h ../rt/ptgt_format.h
	gcc -shared -fPIC -O2 -Wall -o $@ $<

../rt/ptlogmt.so: ../rt/ptlogmt.c ../rt/ptlog_abi.h
	gcc -shared -fPIC -O2 -Wall -o $@ $< -ldl

# Optional variant: the same shim plus synchronization-event logging (ptsync.S); preloaded instead of ptlogmt.so.
../rt/ptlogmt_sync.so: ../rt/ptlogmt.c ../rt/ptsync.S ../rt/ptlog_abi.h
	gcc -shared -fPIC -O2 -Wall -DPTLOG_SYNC=1 -o $@ ../rt/ptlogmt.c ../rt/ptsync.S -ldl

../rt/ldfix.e9rt: ../rt/ldfix.c
	cd $(E9) && NO_SIMD_CHECK=1 ./e9compile.sh $(abspath $<)
	mv $(E9)/ldfix $@

clean:
	rm -f ptlog.so ../rt/ptlogrt.e9rt ../rt/ptlogrt_nofini.e9rt ../rt/ptlogrt.so ../rt/ptlogmt.so ../rt/ptlogmt_sync.so ../rt/ldfix.e9rt

.PHONY: all clean
