# suites/sysroot — the system images the analyses are keyed to

Byte-exact copies of the Ubuntu 24.04 shared libraries that the whole-program analyses
(pyperformance, Memcached, Rust) were run against.  Every spec, site map and rewritten
library in this repository is keyed to these exact files; a different glibc build has
different bytes and needs a fresh analysis.

| file | package | sha256 |
|---|---|---|
| lib/x86_64-linux-gnu/libc.so.6 | libc6 2.39-0ubuntu8.8 (Build ID 328820b9…) | 8db37cf3f2169f59a0f07ef1fea308c35656668c64c8ff294e1860f4121eb161 |
| lib/x86_64-linux-gnu/libm.so.6 | libc6 2.39-0ubuntu8.8 | e9c4b28d340e415b8137480ec442662f981e1399386c5931dae0e886e3639e91 |
| lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 | libc6 2.39-0ubuntu8.8 | cd4df4f3c7b83673d61189bf2eaebd33ca4f2853ab9772b8a25e025ef99b1e81 |
| lib/x86_64-linux-gnu/libz.so.1 | zlib1g 1:1.3.dfsg-3.1ubuntu2.2 | 86200da370f20476a2507e9097a789b5ef97269b4ca8d5e164ad82dab9d99892 |
| lib/x86_64-linux-gnu/libevent-2.1.so.7 | libevent-2.1-7t64 2.1.12-stable-9ubuntu2.1 | 5c5f033f065466b7c62aa2ea0aa83eb9086e61b532a26b203b72bcef6ff25dd2 |
| lib/x86_64-linux-gnu/libgcc_s.so.1 | libgcc-s1 14.2.0-4ubuntu2~24.04.1 | d93224d2b0dab4247598be683adca02f5cf00586f99c187579cd7e92058fb7cb |
| lib/x86_64-linux-gnu/libstdc++.so.6 | libstdc++6 14.2.0-4ubuntu2~24.04.1 | 1fd75fe70354a416d75aef22bcae68c47bd25d20e2d0568c30b1a9838cf62f11 |

The Docker image is built from the same distribution so that `/lib/x86_64-linux-gnu/*`
inside the container matches these files; `sha256sum -c` against this table is the check.
Whole-program rewrites (libc/libm/libz for CPython, libc/libevent for Memcached, libc/libgcc_s
for Rust) take these files as input and are loaded through `LD_LIBRARY_PATH`; the loader
itself is analyzed but never rewritten.
