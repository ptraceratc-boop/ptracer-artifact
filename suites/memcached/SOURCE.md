# suites/memcached — memcached 1.6.21 server + memaslap client

| item | content |
|---|---|
| `bin/memcached_sym` | memcached 1.6.21 built from `memcached-1.6.21/` with the default `./configure` (gcc 13.3.0, `-g -O2 -fno-omit-frame-pointer -pthread`), unstripped |
| `memcached-1.6.21/` | the configured source tree (objects and generated Makefiles removed; `config.h` kept so the exact feature set — EXTSTORE, no TLS — is visible). `./configure && make` rebuilds it. |
| `bin/memcaslap` | the load generator (`memaslap`, from Ubuntu 24.04 `libmemcached-tools` 1.1.4-1.1build3) |
| `lib/libmemcached.so.11`, `lib/libhashkit.so.2` | its non-base libraries (libsasl2/libcrypto/libevent come from the distribution) |
| `mcslap.cfg` | the memaslap configuration: 16-byte keys, 64-byte values, 50/50 get/set |

The paper's metric for this suite is the server's user CPU time for a fixed number of
operations.  Server: `memcached_sym -p <port> -t 4 -m 256 -U 0 -c 1024` pinned to four cores;
client: `memcaslap -s 127.0.0.1:<port> -F mcslap.cfg -x <ops> -c <conc> -T 4` on other cores.

Whole-program rewrite images: `memcached_sym` + `../sysroot/lib/x86_64-linux-gnu/libc.so.6` +
`libevent-2.1.so.7`.

```
bda2492f99ca9f8599f92db212c2f8d051ded08b3f5d5ef43f4b11df0d57405b  bin/memcached_sym
5a3f2cad59b815d67f19eeb5332b8acb2e0c04372ea3871caf03776636c90c03  bin/memcaslap
33a0f2862c12cb6da46895313124c0045c32bec64b6f85f8919e6dd5136dca3e  lib/libmemcached.so.11
b412b08c3ca75e40ea9c6d28ae628979baca6e7c913b4e6535785763d422a566  lib/libhashkit.so.2
```
