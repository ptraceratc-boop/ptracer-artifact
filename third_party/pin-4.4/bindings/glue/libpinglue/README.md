<!---
Copyright (C) 2024-2024 Intel Corporation.
SPDX-License-Identifier: MIT
-->

# Setup

The recommended way to setup `libpinglue` is to use the `setup` script of the `bindings` package.
See [setup](../../README.md). The steps below are for building `libpinglue` separately.

## Dependencies

- Linux kernel version >= 4.12.14
- GCC  Version  range[9.1, 14] (Requires `<filesystem>`)
- Pin 4.x
- Matching pinglueserver (For testing or actually using the library)

## Building

```bash
make PINKIT=<pinkit> TARGET=<intel64|ia32>
```

## Run Tests

```bash
make PINKIT=<pinkit> TARGET=<intel64|ia32> test
```

> **_NOTE:_** pinglueserver should be built and present at <pinkit>/bindings/glue/pinglueserver

# Using The Library

> **_NOTE:_** The documentation for this section is incomplete.
