// jitdump.h -- PTracer v2 runtime/jit: the two files the offline stage consumes, shared by
// the V8 front end (jithook.cc) and the HotSpot front end (java/jvmtiagent.cc).
//
//   1. the **jitdump**  -- a flat stream of 8-byte-aligned `JTR1` records that lets Stage 3
//      reconstruct the process's executable image at any point in time (every record is
//      rdtscp-stamped, because a JIT address is only valid for a time interval).
//   2. the **site map** -- docs/SPEC_FORMAT.md section 2 with the JIT profile: absolute
//      addresses (`pie: false`), plus `obj`/`tsc` on every record.
//
// Both formats are specified in the design notes sections 4 and 5; `check_sitemap.py`
// is the executable check that a run's two files agree.
#ifndef PTJIT_DUMP_H
#define PTJIT_DUMP_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/falloc.h>
#include "jitpatch.h"

static const uint32_t PTJ_REC_MAGIC = 0x4a545231;   // "JTR1"

// evt: 0 CODE_ADDED, 1 CODE_MOVED, 2 CODE_REMOVED, 3 window patched, 4 trampoline, 5 slab
struct PtjRecHdr {
  uint32_t magic;
  uint32_t len;         // total record length incl. header, 8-aligned
  uint64_t addr;        // code start (evt 4: the trampoline's address)
  uint64_t new_addr;    // evt 1: the destination; evt 4: the patched code address
  uint64_t tsc;         // rdtscp at the event
  uint32_t code_len;
  uint8_t code_type;    // 0 BYTE_CODE, 1 JIT_CODE, 2 WASM_CODE, 3 VM stub / dynamic code
  uint8_t evt;
  uint16_t name_len;
};

static inline uint64_t ptj_rdtscp(void) {
  uint32_t lo, hi, aux;
  __asm__ volatile("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux));
  return ((uint64_t)hi << 32) | lo;
}

struct PtjArena {
  uint8_t *p; size_t cap, used;
  uint64_t bytes;        // code bytes recorded
  uint64_t truncated;    // records dropped because the arena is full
};

static void ptj_arena_init(PtjArena *a, size_t cap) {
  if (a->p) return;
  a->p = (uint8_t *)mmap(nullptr, cap, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (a->p == MAP_FAILED) { a->p = nullptr; return; }
  a->cap = cap; a->used = 0;
}

// `src` is where the bytes are read from when with_code; for evt 0/3 that is `addr` itself,
// for evt 4/5 it is the trampoline/slab.  Passing them separately keeps the caller honest
// about which address goes in the header.
static void ptj_arena_record(PtjArena *a, uint8_t evt, uint8_t code_type, const void *addr,
                             const void *new_addr, const void *src, size_t code_len,
                             const char *name, size_t name_len, int with_code) {
  if (!a->p) return;
  if (name_len > 255) name_len = 255;
  size_t body = name_len + (with_code ? code_len : 0);
  size_t total = (sizeof(PtjRecHdr) + body + 7) & ~(size_t)7;
  if (a->used + total > a->cap) { a->truncated++; return; }
  PtjRecHdr *h = (PtjRecHdr *)(a->p + a->used);
  h->magic = PTJ_REC_MAGIC;
  h->len = (uint32_t)total;
  h->addr = (uint64_t)addr;
  h->new_addr = (uint64_t)new_addr;
  h->tsc = ptj_rdtscp();
  h->code_len = (uint32_t)code_len;
  h->code_type = code_type;
  h->evt = evt;
  h->name_len = (uint16_t)name_len;
  uint8_t *p = (uint8_t *)(h + 1);
  if (name_len) memcpy(p, name, name_len);
  p += name_len;
  if (with_code && code_len) memcpy(p, src, code_len);
  a->used += total;
  a->bytes += code_len;
}

static int64_t ptj_arena_flush(PtjArena *a, const char *path) {
  if (!a->p || !a->used || !path || !*path) return 0;
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) return 0;
  size_t off = 0;
  while (off < a->used) {
    ssize_t w = write(fd, a->p + off, a->used - off);
    if (w <= 0) break;
    off += (size_t)w;
  }
  close(fd);
  return (int64_t)off;
}

// ------------------------------------------------- ground-truth ring ------------
// The same-run oracle's output stream (docs/SPEC_FORMAT.md section 3): a 32-byte "PTGT"
// header and 16-byte {effective address, original ip} records in program order.
//
// Unlike the ELF runtime (runtime/rt/ptlogrt.c), which walks a 256 MiB window of the file
// and remaps it from a SIGSEGV handler, this ring is pre-mapped as ONE window followed by a
// PROT_NONE guard page and never faults in normal operation.  A JIT front end lives inside a
// VM that owns SIGSEGV for its own purposes -- HotSpot uses it for implicit null checks and
// for safepoint polls, and chaining a handler in front of it is a much larger hazard than
// reserving a few GB of address space.  The file is sparse: only the pages actually written
// ever reach the disk, and it is truncated to the bytes used at exit.
//
// One ring per PROCESS, bumped with `lock xadd', so records from several threads interleave
// but never overwrite each other.  Stage 3 walks the reconstruction of ONE thread against
// this stream in lockstep, so the comparison is only sound when the JIT code of the run is
// executed by one thread (m4.md section 9: multi-thread reconstruction does not exist yet).
// `cur' is a POINTER to the cursor cell, not the cell: the trampolines reach it with a
// rel32, so it has to live next to the trampoline slab (ptj_slab_data_init below), not in
// the front end's own data segment -- which on a first attempt was >2GB away and made every
// single gt sequence unencodable (`gt_refused' = all of them, 0 records written).
struct PtjGtRing { uint64_t *cur; uint8_t *base; size_t bytes; int fd; char path[512]; };

static int ptj_gt_open(PtjGtRing *r, const char *dir, int pid, size_t bytes) {
  memset(r, 0, sizeof *r); r->fd = -1;
  snprintf(r->path, sizeof r->path, "%s/gt.%d.%d.bin", dir && dir[0] ? dir : ".", pid, pid);
  int fd = open(r->path, O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) { fprintf(stderr, "PTJIT gt: cannot create %s\n", r->path); return 0; }
  bytes = (bytes + 0xfffull) & ~(size_t)0xfff;
  if (ftruncate(fd, (off_t)bytes) != 0) { close(fd); return 0; }
  // Place the (multi-GB) reservation FAR from where a JIT VM keeps its code space, or it
  // eats the +-2GB neighbourhood the trampoline slab needs and every patch is then skipped
  // with `out_of_range' -- which is exactly what a first attempt did.  MAP_FIXED_NOREPLACE
  // so a hint that is already taken fails instead of silently landing somewhere useful.
  static const uintptr_t HINTS[] = {0x200000000000ull, 0x300000000000ull,
                                    0x400000000000ull, 0x500000000000ull, 0};
  void *res = MAP_FAILED;
  for (int h = 0; res == MAP_FAILED && HINTS[h]; h++)
    res = mmap((void *)HINTS[h], bytes + 4096, PROT_NONE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
  if (res == MAP_FAILED)
    res = mmap(nullptr, bytes + 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (res == MAP_FAILED) { close(fd); return 0; }
  void *m = mmap(res, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0);
  if (m == MAP_FAILED) { munmap(res, bytes + 4096); close(fd); return 0; }
  r->base = (uint8_t *)m; r->bytes = bytes; r->fd = fd;
  uint32_t *h = (uint32_t *)m;
  h[0] = 0x54475450u; h[1] = 1; h[2] = (uint32_t)pid; h[3] = 16;
  memset(m + 16, 0, 16);
  return 1;
}
// Point the ring at its cursor cell (allocated next to the slab) and start it past the header.
static void ptj_gt_arm(PtjGtRing *r, uint64_t *cell) {
  if (!r->base || !cell || r->cur) return;
  *cell = (uint64_t)(uintptr_t)(r->base + 32);
  r->cur = cell;
}
static uint64_t ptj_gt_used(const PtjGtRing *r) {
  if (!r->base) return 0;
  return r->cur ? *r->cur - (uint64_t)(uintptr_t)r->base : 32;
}
// Finish the ring WITHOUT unmapping it and WITHOUT truncating the file.  Both would be
// fatal: a VM keeps running JIT code after the front end's exit hook (node tears the isolate
// down afterwards, and it segfaulted on the very first attempt), and any record written then
// would hit an unmapped window (SIGSEGV) or a shrunk file (SIGBUS).  Instead the byte count
// goes into the header's reserved word and the unused tail is punched out, so the file costs
// no disk; `runtime/jit/gt_finish.py' truncates it to that count once the process is gone.
// A process that is KILLED leaves count = 0 and the consumer falls back to trimming records
// whose ip is zero, exactly as SPEC_FORMAT section 3 says.
static void ptj_gt_close(PtjGtRing *r) {
  if (!r->base) return;
  uint64_t used = ptj_gt_used(r);
  if (used > r->bytes) used = r->bytes;
  ((uint64_t *)r->base)[2] = used;                       // header bytes 16..23
  msync(r->base, (size_t)used, MS_SYNC);
#ifdef FALLOC_FL_PUNCH_HOLE
  if (r->fd >= 0 && used < r->bytes)
    if (fallocate(r->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                  (off_t)used, (off_t)(r->bytes - used)) != 0) {}
#endif
}

// ------------------------------------------------- keyframe counter arena -------
// Mapped immediately after the trampoline slab, so every `dec CNT(%rip)' in the slab reaches
// it with a rel32 (the slab is tens of MB).  A hint the kernel cannot honour is not fatal:
// ptj_kf_open() checks the reach per guard and the site is dropped as `keyframe_no_counter'.
// Bytes at the START of the trampoline slab reserved for the rip-relative data a trampoline
// touches: the ground-truth ring cursor in the first cache line, then ONE CACHE LINE per
// keyframe countdown cell (so two hot sites, possibly on two threads, never share a line).
//
// It lives INSIDE the slab because that is the only mapping guaranteed to be within +-2GB of
// every trampoline.  A separate mmap "next to" the slab is placed by the kernel wherever it
// likes: a first attempt landed 2.3 GB away and silently dropped EVERY keyframe guard and
// EVERY gt record (`keyframe_no_counter' / `gt_refused' equal to the whole population), which
// is why both emitters check the rel32 reach rather than assuming it.
static inline size_t ptj_slab_data_bytes(uint32_t ncounters) {
  return ((size_t)(ncounters + 1) * PTJ_KFCTR_STRIDE * 8 + 0xfff) & ~(size_t)0xfff;
}
static void ptj_slab_data_init(PtjPatchCtx *cx, uint8_t *slab, uint32_t n) {
  if (!slab || cx->gt_cur) return;
  memset(slab, 0, ptj_slab_data_bytes(n));
  cx->gt_cur = (uint64_t *)slab;
  if (n) { cx->kfctr = (uint64_t *)slab + PTJ_KFCTR_STRIDE; cx->kfctr_used = 0; cx->kfctr_max = n; }
}

// ------------------------------------------------------------------ site map ---
// One row per code object, in the order they were patched.
struct PtjObjRec { uint64_t addr, tsc; uint32_t len; uint32_t nsites, npatched; };

// docs/SPEC_FORMAT.md section 2, JIT profile.  `vm` is "v8" or "hotspot"; `image` is
// `jit:<pid>` in both cases (there is no ELF to match against /proc/PID/maps).
static int64_t ptj_write_sitemap(const char *path, const char *vm, int pid, PtjPatchCtx *cx,
                                 const PtjObjRec *objs, uint32_t nobjs,
                                 const void *slab, uint64_t slab_cap, uint64_t slab_used,
                                 int use_ptwrite, uint32_t space, uint32_t kf_period = 0) {
  FILE *f = fopen(path, "w");
  if (!f) return 0;
  fprintf(f, "{\"version\": 2, \"jit\": true, \"vm\": \"%s\", \"pid\": %d,\n", vm, pid);
  fprintf(f, " \"image\": \"jit:%d\", \"orig_image\": \"jit:%d\",\n", pid, pid);
  // BUFFER SINK the design notes: the map-level `sink' and `sync' are exactly
  // the fields `runtime/rewrite.py' writes for an E9Patch buffer build, so `ptrecon' takes the
  // positional cv path and the sync-marker realignment with no JIT-specific code at all.
  fprintf(f, " \"sink\": \"%s\", \"pie\": false, \"sync\": %u, \"space\": %u,\n",
          cx->sink_buffer ? "buffer" : use_ptwrite ? "ptwrite" : "none",
          cx->sink_buffer ? cx->sync : 0u, space);
  fprintf(f, " \"slab\": {\"base\": %llu, \"size\": %llu, \"used\": %llu},\n",
          (unsigned long long)(uintptr_t)slab, (unsigned long long)slab_cap,
          (unsigned long long)slab_used);
  fprintf(f, " \"objects\": [\n");
  for (uint32_t i = 0; i < nobjs && objs; i++)
    fprintf(f, "  {\"i\": %u, \"addr\": %llu, \"len\": %u, \"tsc\": %llu, \"sites\": %u,"
               " \"patched\": %u}%s\n",
            i, (unsigned long long)objs[i].addr, objs[i].len,
            (unsigned long long)objs[i].tsc, objs[i].nsites, objs[i].npatched,
            (i + 1 < nobjs) ? "," : "");
  fprintf(f, " ],\n \"entries\": [\n");
  for (uint32_t i = 0; i < cx->nentries; i++) {
    PtjMapEntry *m = &cx->entries[i];
    const char *kind = m->kind == PTJ_KIND_REG ? "reg" : m->kind == PTJ_KIND_LOAD ? "load" : "memop";
    fprintf(f, "  {\"tramp_addr\": %llu, \"orig_addr\": %llu, \"site\": %u, \"obj\": %u,"
               " \"tsc\": %llu, \"kind\": \"%s\", \"when\": \"%s\", \"payload_bits\": %u",
            (unsigned long long)m->tramp_addr, (unsigned long long)m->orig_addr,
            m->site, m->obj, (unsigned long long)m->tsc, kind,
            m->when == PTJ_WHEN_AFTER ? "after" : "before", m->payload_bits);
    // A per-entry `sink' overrides the map's own (SPEC_FORMAT section 2): with the buffer
    // sink every critical value is a store into the ring, and the only PTWRITEs left in the
    // slab are the 1-in-`sync' markers, which are not entries at all.
    if (m->buffer) fprintf(f, ", \"role\": \"cv\", \"sink\": \"buffer\"");
    if (m->kind == PTJ_KIND_MEMOP) fprintf(f, ", \"size\": %u", m->size);
    else if (m->reg < 16) fprintf(f, ", \"reg\": \"%s\"", PTJ_REGNAME[m->reg]);
    else if (m->reg < 32) fprintf(f, ", \"reg\": \"xmm%u\", \"half\": \"%s\"",
                                  m->reg - 16, m->half == 1 ? "lo" : "hi");
    // SPEC_FORMAT section 2: a keyframed value carries the guard's counter cell, the branch
    // and its join label.  Stage 3 keys on `kf_branch_addr' and decides from the next
    // instruction's ip whether the logging path ran.
    if (m->kf_period)
      fprintf(f, ", \"keyframe\": %u, \"resync\": %s, \"counter\": %llu,"
                 " \"kf_branch_addr\": %llu, \"kf_join_addr\": %llu",
              m->kf_period, m->resync ? "true" : "false",
              (unsigned long long)m->counter, (unsigned long long)m->kf_branch,
              (unsigned long long)m->kf_join);
    fprintf(f, "}%s\n", (i + 1 < cx->nentries || cx->ngtents) ? "," : "");
  }
  // SAME-RUN GROUND TRUTH the design notes section 4): `role: "gt"' rows are
  // NOT critical values.  `offline/recon.cpp' drops them at load time, so the store is
  // ordinary instrumentation to the reconstruction -- no record, no state change, no payload
  // consumed -- and only the gt comparison reads the stream they write.
  for (uint32_t i = 0; i < cx->ngtents; i++) {
    PtjGtEnt *g = &cx->gtents[i];
    fprintf(f, "  {\"tramp_addr\": %llu, \"orig_addr\": %llu, \"obj\": %u, \"tsc\": %llu,"
               " \"role\": \"gt\", \"kind\": \"memop\", \"when\": \"before\", \"size\": %u,"
               " \"op\": %u, \"key_addr\": %llu}%s\n",
            (unsigned long long)g->tramp_addr, (unsigned long long)g->orig_addr,
            g->obj, (unsigned long long)g->tsc, g->size, g->op,
            (unsigned long long)g->key_addr,
            (i + 1 < cx->ngtents) ? "," : "");
  }
  fprintf(f, " ],\n");
  // The buffer sink's SYNC MARKERS (SPEC_FORMAT section 3): `ptwrite %gs:16' of the thread's
  // running value count, emitted 1-in-`sync' values.  `ptrecon' uses them to realign its
  // POSITIONAL cv cursor after a PT state loss; the format is the ELF one byte for byte.
  fprintf(f, " \"sync_markers\": [");
  for (uint32_t i = 0; i < cx->nsyncs; i++)
    fprintf(f, "%s{\"tramp_addr\": %llu, \"orig_addr\": %llu}",
            i ? ", " : "", (unsigned long long)cx->syncs[i].tramp_addr,
            (unsigned long long)cx->syncs[i].orig_addr);
  fprintf(f, "],\n \"gt_site_addrs\": [");
  for (uint32_t i = 0; i < cx->ngtents; i++)
    fprintf(f, "%s%llu", i ? ", " : "", (unsigned long long)cx->gtents[i].orig_addr);
  // ... and WHEN each became one.  A JIT code object is executed before it is patched (V8
  // enumerates code that is already running; HotSpot patches an nmethod seconds after
  // CODE_ADDED), so an address is a gt site only from this rdtscp on -- without it the
  // reconstruction's pre-patch records desynchronise the lockstep walk (jit_accuracy.md).
  fprintf(f, "],\n \"gt_site_tsc\": [");
  for (uint32_t i = 0; i < cx->ngtents; i++)
    fprintf(f, "%s%llu", i ? ", " : "", (unsigned long long)cx->gtents[i].tsc);
  // ... and the LOCKSTEP KEY each gt record is written with (defect D-J9): the address of
  // the relocated copy of the instruction inside this trampoline.  An ip is not a dynamic
  // instance for JIT code (addresses are reused across code-object versions, and inside a
  // loop the next record with the same ip is another iteration); a trampoline address is
  // unique for the life of the process, and it is what `ptrecon' is executing when it
  // produces the record.  Absent (or 0) => `ptrecon' falls back to matching on the ip,
  // which is the ELF `--gt-all' behaviour.
  fprintf(f, "],\n \"gt_site_tramps\": [");
  for (uint32_t i = 0; i < cx->ngtents; i++)
    fprintf(f, "%s%llu", i ? ", " : "", (unsigned long long)cx->gtents[i].key_addr);
  fprintf(f, "],\n \"gt_static\": {\"sites\": %u, \"refused\": %llu},\n"
             " \"keyframe\": {\"period\": %u, \"requested\": %llu, \"sites\": %llu, \"values\": %llu,"
             " \"counters\": %u, \"dropped_flags_live\": %llu, \"dropped_no_counter\": %llu},\n",
          cx->ngtents, (unsigned long long)cx->gt_refused,
          kf_period, (unsigned long long)cx->kf_requested,
          (unsigned long long)cx->kf_sites, (unsigned long long)cx->kf_values,
          cx->kfctr_used, (unsigned long long)cx->drops[PTJ_DROP_KF_FLAGS],
          (unsigned long long)cx->drops[PTJ_DROP_KF_NOCTR]);
  fprintf(f, " \"relocated\": [\n");
  for (uint32_t i = 0; i < cx->nrelocs; i++) {
    PtjReloc *r = &cx->relocs[i];
    fprintf(f, "  {\"tramp_addr\": %llu, \"orig_addr\": %llu, \"len\": %u,"
               " \"tramp_len\": %u, \"obj\": %u}%s\n",
            (unsigned long long)r->tramp_addr, (unsigned long long)r->orig_addr,
            r->len, r->tramp_len, r->obj, (i + 1 < cx->nrelocs) ? "," : "");
  }
  fprintf(f, " ],\n \"trampolines\": [");
  for (uint32_t i = 0; i < cx->ntramps; i++)
    fprintf(f, "%s%llu", i ? ", " : "", (unsigned long long)cx->tramps[i]);
  fprintf(f, "]\n}\n");
  int64_t written = ftell(f);
  fclose(f);
  return written;
}
#endif
