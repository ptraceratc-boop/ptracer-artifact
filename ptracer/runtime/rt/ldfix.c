/*
 * ldfix.c -- PTracer Stage-2 runtime shim that makes a REWRITTEN
 * `ld-linux-x86-64.so.2` still recognise itself as the program being run.
 *
 * WHY
 *   E9Patch instruments a binary by pointing the ELF `e_entry` at its own
 *   loader stub, which runs, maps the trampolines, and jumps to the real entry.
 *   glibc's dynamic loader decides whether it is "the program interpreter" or
 *   "the program itself" by comparing the kernel's `AT_ENTRY` auxiliary vector
 *   entry with the address of its own `_start` (`elf/rtld.c`:
 *   `if (*user_entry == (ElfW(Addr)) ENTRY_POINT)`).  After E9Patch those two
 *   differ, so an explicitly invoked `./ld.so.e9 prog` takes the *interpreter*
 *   path, builds a main map out of its own program headers, and -- ld.so having
 *   no PT_PHDR -- ends up with `l_addr == 0` and dereferences a link-time
 *   address:
 *
 *      SIGSEGV in _dl_process_pt_gnu_property (dl-load.c:885), si_addr = 0x2a8
 *      #1 rtld_setup_main_map (rtld.c:1260)  #2 dl_main (rtld.c:1668)
 *
 * WHAT
 *   This E9Patch `init` routine runs inside the rewritten process, immediately
 *   after the loader mapped the trampolines and before the real entry point,
 *   and puts `AT_ENTRY` back to the ORIGINAL entry point (which the E9Patch
 *   config records).  The comparison then succeeds and ld.so behaves exactly as
 *   the unrewritten one.  For any other target the write is a no-op in effect:
 *   AT_ENTRY is restored to the value the kernel would have passed for the
 *   un-rewritten binary, which is what every consumer of AT_ENTRY expects.
 *
 * BUILD
 *   cd <e9patch> && ./e9compile.sh <path>/runtime/rt/ldfix.c
 *   -> the PIE object `ldfix` (rt/ldfix.e9rt, built by make -C
 *      runtime/e9plugin); runtime/e9plugin/ptlog.cpp injects it with
 *      sendELFFileMessage() when rewrite.py passes `--ldfix=FILE`
 *      (rewrite.py's `--fix-at-entry`, implied by `--ld-so`).
 */

#include "stdlib.c"

#define AT_NULL     0
#define AT_ENTRY    9
#define AT_BASE     7

/* The prefix of E9Patch's `struct e9_config_s' (src/e9patch/e9loader.h) that
 * this shim needs.  `base' is the loader's link-time address, so the image's
 * runtime base is `(char *)config - base'; `entry' is the ORIGINAL e_entry,
 * either a link-time address or, with the E9_ABS_ADDR bit set, absolute. */
struct e9cfg
{
    char          magic[8];
    char          version[16];
    unsigned      flags;
    unsigned      size;
    long          base;
    long          entry;
};

#define E9_ABS_ADDR (1L << 62)

void init(int argc, char **argv, char **envp, const void *dynamic,
    const void *cfg)
{
    (void)argc; (void)argv; (void)dynamic;
    const struct e9cfg *c = (const struct e9cfg *)cfg;
    if (c == 0 || envp == 0)
        return;
    if (c->magic[0] != 'E' || c->magic[1] != '9' || c->magic[2] != 'P')
        return;
    unsigned long entry;
    if ((c->entry & E9_ABS_ADDR) != 0)
        entry = (unsigned long)(c->entry & ~E9_ABS_ADDR);
    else
        entry = (unsigned long)((const char *)cfg - c->base) +
                (unsigned long)c->entry;

    /* auxv sits just past the NULL that terminates envp on the initial stack. */
    char **p = envp;
    while (*p != 0)
        p++;
    unsigned long *av = (unsigned long *)(p + 1);
    /* A rewritten ld.so may also be the PROGRAM INTERPRETER (the program's
     * PT_INTERP names it).  Then the kernel's AT_ENTRY
     * is the program's entry and AT_BASE is this image's own load base, and
     * AT_ENTRY must stay untouched, or ld.so would take itself for the
     * program.  Explicitly invoked, ld.so has no interpreter and AT_BASE = 0. */
    unsigned long self = (unsigned long)((const char *)cfg - c->base);
    for (unsigned long *q = av; q[0] != AT_NULL; q += 2)
        if (q[0] == AT_BASE && q[1] == self)
            return;
    for (; av[0] != AT_NULL; av += 2)
        if (av[0] == AT_ENTRY)
            av[1] = entry;
}
