#!/bin/bash
# Runs INSIDE ptracer-artifact (gcc 13.3.0, the interpreter's toolchain). /src = CPython 3.12.13+ source+build
# tree whose pyconfig.h is byte-identical to cpython-cg's; /f5py/extbuild/root = headers from the
# Ubuntu -dev debs matching the image's runtime libs. Flags = the Makefile's PY_STDMODULE_CFLAGS + CCSHARED.
set -e
S=/src; R=/f5py/extbuild/root/usr/include; O=/f5py/extmods; T=/f5py/extbuild/obj; mkdir -p $T $O
CF="-fno-strict-overflow -Wsign-compare -DNDEBUG -g -O3 -Wall -O2 -std=c11 -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wstrict-prototypes -Werror=implicit-function-declaration -fvisibility=hidden -I$S/Include/internal -I$S -I$S/Include -fPIC"
EXT=cpython-312-x86_64-linux-gnu.so
# _sqlite3
objs=""
for c in blob connection cursor microprotocols module prepare_protocol row statement util; do
  gcc $CF -I$S/Modules/_sqlite -I$R -DPY_SQLITE_HAVE_SERIALIZE=1 -DPY_SQLITE_ENABLE_LOAD_EXTENSION=1 -c $S/Modules/_sqlite/$c.c -o $T/sq_$c.o; objs="$objs $T/sq_$c.o"; done
gcc -shared $objs /usr/lib/x86_64-linux-gnu/libsqlite3.so.0 -o $O/_sqlite3.$EXT
# _lzma
gcc $CF -I$R -c $S/Modules/_lzmamodule.c -o $T/lzma.o; gcc -shared $T/lzma.o /usr/lib/x86_64-linux-gnu/liblzma.so.5 -o $O/_lzma.$EXT
# _bz2
gcc $CF -I$R -c $S/Modules/_bz2module.c -o $T/bz2.o; gcc -shared $T/bz2.o -lbz2 -o $O/_bz2.$EXT
# _uuid
gcc $CF -I$R -DHAVE_UUID_UUID_H=1 -DHAVE_UUID_GENERATE_TIME_SAFE=1 -c $S/Modules/_uuidmodule.c -o $T/uuid.o; gcc -shared $T/uuid.o /usr/lib/x86_64-linux-gnu/libuuid.so.1 -o $O/_uuid.$EXT
ls -la $O
