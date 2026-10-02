#!/bin/bash
# Build the Renaissance boundary-drain plugin (ptjdrain.jar) of the Java whole-program Fast arm.
# It compiles against renaissance.jar's plugin API only; the jar runs in EVERY Java arm (see PtjDrain.java).
#   JDK_HOME / JAVA_HOME  JDK with bin/javac (default <artifact root>/suites/java/jdk17)
#   REN                   directory holding renaissance.jar (default <artifact root>/suites/java/renaissance)
set -e
cd "$(dirname "$0")"
ROOT=$(realpath -m "$(pwd)/../../../../..")
JDK=${JDK_HOME:-${JAVA_HOME:-$ROOT/suites/java/jdk17}}
REN=${REN:-$ROOT/suites/java/renaissance}
[ -x "$JDK/bin/javac" ] || { echo "no bin/javac under $JDK (set JDK_HOME)" >&2; exit 2; }
[ -f "$REN/renaissance.jar" ] || { echo "no renaissance.jar under $REN (run suites/reassemble.sh, or set REN)" >&2; exit 2; }
T=cls   # the class files (a few KB) stay next to the source
"$JDK/bin/javac" --release 11 -cp "$REN/renaissance.jar" -d "$T" PtjDrain.java
"$JDK/bin/jar" cf ptjdrain.jar -C "$T" .
echo "built $(pwd)/ptjdrain.jar"
