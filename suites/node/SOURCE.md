# suites/node — Node.js v22 + Web Tooling Benchmark

| item | content |
|---|---|
| `bin/node` | Node.js v22.23.2 (nodesource build for Ubuntu 24.04, package `nodejs` 22.23.2-1nodesource1). Stored as `node.part-*`; `../reassemble.sh` restores it. Links libstdc++/libgcc_s/libc from `../sysroot`. |
| `include/node/` | the matching Node headers (`/usr/include/node` of that package): the V8 and N-API headers the `jithook` addon is compiled against |
| `web-tooling-benchmark/` | Web Tooling Benchmark 0.5.3 (github.com/v8/web-tooling-benchmark, git 4a12828c6a1eed02a70c011bd080445dd319a05f) with its `node_modules` as installed; the `dist/` bundle is not needed and not included |
| `wtb.js` | the harness: runs one payload N times and prints `{ms, checksum}` |

Cells: `acorn` and `babel` (10 iterations each) are the two Web-Tooling cells of the overhead
slice; `babylon` and `typescript` are the other payloads the JIT accuracy runs exercise.  Run
with `web-tooling-benchmark/` as the working directory:

```
cd web-tooling-benchmark && ../bin/node ../wtb.js acorn 10
```

The HiFi (Pin) configuration adds `-r <ptracer>/runtime/jit/preload.js`, which loads the
`jithook` addon built from `ptracer/runtime/jit/jithook.cc` with

```
g++ -O2 -std=c++17 -fPIC -shared -fno-exceptions -fno-rtti \
    -I suites/node/include/node -I third_party/e9patch/contrib/zydis/include \
    -I third_party/e9patch/contrib/zydis/dependencies/zycore/include -DZYAN_NO_LIBC=0 \
    -o jithook.node jithook.cc third_party/e9patch/contrib/zydis/libZydis.a \
    -Wl,--unresolved-symbols=ignore-all
```

(no node-gyp: the node binary exports both the N-API entry points and the V8 C++ ABI).

```
3517c2df0b2f8cd7f422b4b8450ef81c6889f08eb03e281d6de9079b15e6a327  bin/node
```
