#!/bin/bash
# Build the HotSpot front end (ptjava.so, a JVMTI agent) and, when a JDK with javac and
# renaissance.jar are present, the boundary-drain plugin ptjdrain/ptjdrain.jar.
#   ZYDIS_DIR            as in ../build.sh
#   JDK_HOME / JAVA_HOME JDK with include/jvmti.h (default <artifact root>/suites/java/jdk17,
#                        else the JDK of `javac` on PATH, else /usr/lib/jvm/java-17-openjdk-amd64)
set -e
cd "$(dirname "$0")"
ROOT=$(realpath -m "$(pwd)/../../../..")
ZY=${ZYDIS_DIR:-$ROOT/third_party/e9patch/contrib/zydis}
JDK=${JDK_HOME:-${JAVA_HOME:-}}
if [ -z "$JDK" ] && [ -f "$ROOT/suites/java/jdk17/include/jvmti.h" ]; then JDK=$ROOT/suites/java/jdk17; fi
if [ -z "$JDK" ] && command -v javac >/dev/null 2>&1; then
  JDK=$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")
fi
JDK=${JDK:-/usr/lib/jvm/java-17-openjdk-amd64}
[ -f "$ZY/libZydis.a" ] || { echo "no libZydis.a under $ZY (set ZYDIS_DIR)" >&2; exit 2; }
[ -f "$JDK/include/jvmti.h" ] || { echo "no include/jvmti.h under $JDK (set JDK_HOME)" >&2; exit 2; }
g++ -O2 -std=c++17 -fPIC -shared -fno-exceptions -fno-rtti \
  -I"$JDK/include" -I"$JDK/include/linux" \
  -I"$ZY/include" -I"$ZY/dependencies/zycore/include" -DZYAN_NO_LIBC=0 \
  -o ptjava.so jvmtiagent.cc "$ZY/libZydis.a" -lpthread
echo "built $(pwd)/ptjava.so"
# the Renaissance boundary-drain plugin of the whole-program Fast arm (loaded in every Java arm)
if [ -x "$JDK/bin/javac" ] && [ -f "${REN:-$ROOT/suites/java/renaissance}/renaissance.jar" ]; then
  JDK_HOME="$JDK" bash ptjdrain/build.sh
else
  echo "ptjdrain.jar not built (needs $JDK/bin/javac and renaissance.jar)" >&2
fi
