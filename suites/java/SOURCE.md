# suites/java — Temurin JDK 17 + Renaissance

| item | content |
|---|---|
| `jdk17/` | Eclipse Temurin 17.0.13+11 (HotSpot, x86_64 Linux; `jdk17/release` has the full build record). `lib/src.zip`, `jmods/` and `man/` are not included; everything the runtime and the JVMTI agent build need is (`include/jvmti.h`, `lib/server/libjvm.so`, `lib/modules`, the CDS archive). `lib/modules` is stored as `modules.part-*`; `../reassemble.sh` restores it. |
| `renaissance/renaissance.jar` | Renaissance 0.15.0 (github.com/renaissance-benchmarks/renaissance, git 87358d94a06920c1f66e08dca3869bf96004d115), the released fat jar. Stored as `renaissance.jar.part-*`. |

This JDK — not a distribution package — is the one every HotSpot cell was measured with; the
HiFi JVMTI agent was compiled against its `include/`.  Cells: `scrabble` (12 iterations, 6
warm-up dropped) and `philosophers` (8 iterations, 4 dropped) in the vanilla configuration;
the instrumented configurations use the driver's own iteration counts.  The JVM is pinned to
four cores:

```
jdk17/bin/java -jar renaissance/renaissance.jar -r 12 scrabble
```

The JVMTI agent (`ptracer/runtime/jit/java/jvmtiagent.cc`) is built with

```
g++ -O2 -std=c++17 -fPIC -shared -I suites/java/jdk17/include -I suites/java/jdk17/include/linux \
    -I third_party/e9patch/contrib/zydis/include \
    -I third_party/e9patch/contrib/zydis/dependencies/zycore/include -DZYAN_NO_LIBC=0 \
    -o ptjava.so jvmtiagent.cc third_party/e9patch/contrib/zydis/libZydis.a -lpthread
```

and passed as `-agentpath:ptjava.so=<options>`.  Pin 4.4 runs the JVM only with
`-pin_memory_range A:B -enforce_pin_range_allocations 1` (see the drivers).

```
3d664eb8f9a7ab7d0b9962a263b075a0da53af49bd2a142c0344ca66b55b0355  jdk17/bin/java
fe5b9dd8f3e1b470084f17073dc0a25b28ebc650bc5d3b06b07ab91561bd0803  jdk17/lib/server/libjvm.so
bf8da421e038568d81c360a2d0b25b6fd09babc97c66042a7bb9a99d5aff41ba  jdk17/lib/modules
d19b4dc4944c70aa9f9ea944fe624fedffa1dd5511a9b41dd6eeb420411bbb4d  renaissance/renaissance.jar
```
