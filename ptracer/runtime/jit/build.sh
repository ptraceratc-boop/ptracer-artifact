#!/bin/bash
# Build the V8 front end (jithook.node).  No node-gyp: the node binary exports both the
# N-API entry points and the V8 C++ ABI, and the node headers ship the full header set.
#   ZYDIS_DIR     Zydis tree with a built libZydis.a
#                 (default <artifact root>/third_party/e9patch/contrib/zydis)
#   NODE_INCLUDE  node header directory
#                 (default <artifact root>/suites/node/include, else /usr/include/node)
set -e
cd "$(dirname "$0")"
ROOT=$(realpath -m "$(pwd)/../../..")
ZY=${ZYDIS_DIR:-$ROOT/third_party/e9patch/contrib/zydis}
NI=${NODE_INCLUDE:-}
if [ -z "$NI" ]; then
  if [ -f "$ROOT/suites/node/include/node/node_api.h" ]; then NI=$ROOT/suites/node/include/node
  elif [ -f "$ROOT/suites/node/include/node_api.h" ]; then NI=$ROOT/suites/node/include
  else NI=/usr/include/node; fi
fi
[ -f "$ZY/libZydis.a" ] || { echo "no libZydis.a under $ZY (set ZYDIS_DIR)" >&2; exit 2; }
[ -f "$NI/node_api.h" ] || { echo "no node_api.h under $NI (set NODE_INCLUDE)" >&2; exit 2; }
g++ -O2 -std=c++17 -fPIC -shared -fno-exceptions -fno-rtti \
  -I"$NI" -I"$ZY/include" -I"$ZY/dependencies/zycore/include" -DZYAN_NO_LIBC=0 \
  -o jithook.node jithook.cc "$ZY/libZydis.a" \
  -Wl,--unresolved-symbols=ignore-all -pthread
echo "built $(pwd)/jithook.node"
