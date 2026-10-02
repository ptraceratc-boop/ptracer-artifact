# JIT-suite toolchain build (whole-program Fast images of Node/Java).  Same recipes as
# runtime/e9plugin/Makefile, applied to the sources of THIS directory (provenance: ../SOURCES.md5), so the outputs land under
# runtime/jit_toolchain/ and nothing the non-JIT Fast images use is touched:
#   e9plugin/ptlog.so, rt/ptlogrt.e9rt, rt/ptlogrt_nofini.e9rt   (used by jit_toolchain/rewrite.py)
#   rt/ptlogrt.so, rt/ptlogmt.so                                   (LD_PRELOAD of an e9fast JIT run)
# e9compile.sh writes <checkout>/ptlogrt; the non-JIT Makefile uses the same scratch name, so ptracer/build.sh runs
# the two makes one after the other (never in parallel).
E9      ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../../third_party/e9patch)
CXXFLAGS = -std=c++17 -fPIC -shared -O2 -Wall -I $(E9)/src/e9tool

all: ptlog.so ../rt/ptlogrt.e9rt ../rt/ptlogrt_nofini.e9rt ../rt/ptlogrt.so ../rt/ptlogmt.so

ptlog.so: ptlog.cpp ../rt/ptlog_abi.h ../rt/ptgt_format.h
	g++ $(CXXFLAGS) -o $@ $<

../rt/ptlogrt.e9rt: ../rt/ptlogrt.c ../rt/ptlog_abi.h ../rt/ptgt_format.h
	cd $(E9) && NO_SIMD_CHECK=1 ./e9compile.sh $(abspath $<) -DPTLOG_E9RT
	mv $(E9)/ptlogrt $@

# libc.so.6 has no DT_FINI/DT_FINI_ARRAY: it gets the flavour without fini().  Serialised after ptlogrt.e9rt
# (same e9compile output name).
../rt/ptlogrt_nofini.e9rt: ../rt/ptlogrt.c ../rt/ptlog_abi.h ../rt/ptgt_format.h ../rt/ptlogrt.e9rt
	cd $(E9) && NO_SIMD_CHECK=1 ./e9compile.sh $(abspath $<) -DPTLOG_E9RT -DPTLOG_NO_FINI
	mv $(E9)/ptlogrt $@

../rt/ptlogrt.so: ../rt/ptlogrt.c ../rt/ptlog_abi.h ../rt/ptgt_format.h
	gcc -shared -fPIC -O2 -Wall -o $@ $<

../rt/ptlogmt.so: ../rt/ptlogmt.c ../rt/ptlog_abi.h
	gcc -shared -fPIC -O2 -Wall -o $@ $< -ldl

clean:
	rm -f ptlog.so ../rt/ptlogrt.e9rt ../rt/ptlogrt_nofini.e9rt ../rt/ptlogrt.so ../rt/ptlogmt.so

.PHONY: all clean
