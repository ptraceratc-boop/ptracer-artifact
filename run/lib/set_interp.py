#!/usr/bin/env python3
"""set_interp.py -- point an ELF executable's PT_INTERP at another loader, IN PLACE.

    set_interp.py ELF NEW_INTERP        # rewrite the PT_INTERP string
    set_interp.py ELF                   # print it

Used for the whole-process Fast arm: the rewritten main image names the
REWRITTEN ld.so (`rewrite.py --ld-so --under-ld-so`, built with the AT_BASE-aware ldfix) as its program
interpreter, so the kernel maps the instrumented loader and the program is started exactly as before
(`./prog args`, no explicit `ld.so prog` invocation, argv/auxv/`/proc/self/exe` unchanged).

Only the bytes of the interpreter string change: the new path (plus NUL padding) must fit into the
existing PT_INTERP p_filesz, so the ELF layout (and every E9Patch trampoline, site map address and
PT_LOAD) is untouched.  The kernel requires the last byte of the segment to be NUL and opens the
string up to its first NUL.
"""
import struct, sys


def interp(data):
    if data[:4] != b"\x7fELF" or data[4] != 2:
        raise SystemExit("not a 64-bit ELF")
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    for i in range(phnum):
        o = phoff + i * phentsize
        p_type, = struct.unpack_from("<I", data, o)
        if p_type == 3:                                   # PT_INTERP
            p_offset, = struct.unpack_from("<Q", data, o + 8)
            p_filesz, = struct.unpack_from("<Q", data, o + 32)
            return p_offset, p_filesz
    raise SystemExit("no PT_INTERP (static or shared object?)")


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()
    off, sz = interp(data)
    cur = data[off:off + sz].split(b"\0", 1)[0].decode()
    if len(sys.argv) == 2:
        print(cur)
        return
    new = sys.argv[2].encode()
    if not new.startswith(b"/"):
        raise SystemExit("the interpreter path must be absolute")
    if len(new) + 1 > sz:
        raise SystemExit("%s: new interpreter %r (%d bytes + NUL) does not fit PT_INTERP p_filesz %d"
                         % (path, sys.argv[2], len(new), sz))
    with open(path, "r+b") as f:
        f.seek(off)
        f.write(new + b"\0" * (sz - len(new)))
    print("%s: PT_INTERP %s -> %s" % (path, cur, sys.argv[2]))


if __name__ == "__main__":
    main()
