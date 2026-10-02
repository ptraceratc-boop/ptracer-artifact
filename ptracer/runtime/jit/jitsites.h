// jitsites.h -- PTracer v2 runtime/jit: analyzer-service client, content-hash cache,
// and the JSON reply parser.  Included by jithook.cc (V8) and reusable by a JVMTI front end.
#ifndef PTJIT_SITES_H
#define PTJIT_SITES_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <spawn.h>
#include <time.h>

// ------------------------------------------------------------------ sites ---
// Registers: 0..15 = rax..r15 (SysV numbering below), 16..31 = xmm0..15, 255 = unsupported.
#include "jitsite_format.h"

static const char *PTJ_REGNAME[16] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi",
                                      "r8","r9","r10","r11","r12","r13","r14","r15"};

static int ptj_regnum(const char *s, size_t n) {
  char b[8];
  if (n >= sizeof(b)) return 255;
  memcpy(b, s, n); b[n] = 0;
  for (int i = 0; i < 16; i++) if (!strcmp(b, PTJ_REGNAME[i])) return i;
  if (b[0]=='x' && b[1]=='m' && b[2]=='m') { int v = atoi(b+3); if (v>=0 && v<16) return 16+v; }
  return 255;   // fs_base / gs_base / anything else
}

// ------------------------------------------------------------- JSON scan ----
// A minimal scanner for the analyzer's reply.  It never allocates: it walks the
// buffer, finds "sites":[ ... ] and pulls the known keys out of each object.
struct PtjJson {
  const char *p, *e;
};
static void ptj_ws(PtjJson *j) { while (j->p < j->e && (*j->p==' '||*j->p=='\t'||*j->p=='\n'||*j->p=='\r')) j->p++; }
static int ptj_skip_string(PtjJson *j) {          // at the opening quote
  if (j->p >= j->e || *j->p != '"') return 0;
  j->p++;
  while (j->p < j->e) {
    if (*j->p == '\\') { j->p += 2; continue; }
    if (*j->p == '"') { j->p++; return 1; }
    j->p++;
  }
  return 0;
}
static int ptj_skip_value(PtjJson *j) {
  ptj_ws(j);
  if (j->p >= j->e) return 0;
  char c = *j->p;
  if (c == '"') return ptj_skip_string(j);
  if (c == '{' || c == '[') {
    char open = c, close = (c=='{') ? '}' : ']';
    int depth = 0;
    while (j->p < j->e) {
      if (*j->p == '"') { if (!ptj_skip_string(j)) return 0; continue; }
      if (*j->p == open) depth++;
      else if (*j->p == close) { depth--; j->p++; if (!depth) return 1; continue; }
      j->p++;
    }
    return 0;
  }
  while (j->p < j->e && *j->p != ',' && *j->p != '}' && *j->p != ']') j->p++;
  return 1;
}

// Parse one site object starting at '{'.  Advances j past the object.
static int ptj_parse_site(PtjJson *j, uint64_t base, PtjSite *out) {
  ptj_ws(j);
  if (j->p >= j->e || *j->p != '{') return 0;
  j->p++;
  memset(out, 0, sizeof(*out));
  out->size = 8;
  uint64_t addr = 0; int have_addr = 0;
  for (;;) {
    ptj_ws(j);
    if (j->p < j->e && *j->p == '}') { j->p++; break; }
    if (j->p < j->e && *j->p == ',') { j->p++; continue; }
    if (j->p >= j->e || *j->p != '"') return 0;
    const char *k = j->p + 1;
    if (!ptj_skip_string(j)) return 0;
    size_t klen = (size_t)(j->p - 1 - k);
    ptj_ws(j);
    if (j->p >= j->e || *j->p != ':') return 0;
    j->p++;
    ptj_ws(j);
    const char *vs = j->p;
    if (!ptj_skip_value(j)) return 0;
    size_t vlen = (size_t)(j->p - vs);
    if (klen == 4 && !memcmp(k, "addr", 4)) { addr = strtoull(vs, nullptr, 10); have_addr = 1; }
    else if (klen == 2 && !memcmp(k, "id", 2)) out->id = (uint16_t)strtoul(vs, nullptr, 10);
    else if (klen == 4 && !memcmp(k, "when", 4)) out->when = (vlen >= 6 && vs[1]=='a') ? PTJ_WHEN_AFTER : PTJ_WHEN_BEFORE;
    else if (klen == 4 && !memcmp(k, "kind", 4))
      out->kind = (vs[1]=='r') ? PTJ_KIND_REG : (vs[1]=='l') ? PTJ_KIND_LOAD : PTJ_KIND_MEMOP;
    else if (klen == 4 && !memcmp(k, "size", 4)) out->size = (uint8_t)strtoul(vs, nullptr, 10);
    else if (klen == 10 && !memcmp(k, "flags_dead", 10)) out->flags_dead = (vs[0]=='t');
    else if (klen == 6 && !memcmp(k, "resync", 6)) out->resync = (vs[0]=='t');
    else if (klen == 8 && !memcmp(k, "keyframe", 8)) out->kf = (uint32_t)strtoul(vs, nullptr, 10);
    else if (klen == 3 && !memcmp(k, "reg", 3)) {
      if (vs[0]=='"' && out->nregs < PTJ_MAXREG) out->regs[out->nregs++] = (uint8_t)ptj_regnum(vs+1, vlen-2);
    } else if (klen == 4 && !memcmp(k, "regs", 4)) {
      const char *q = vs, *qe = vs + vlen;
      while (q < qe) {
        if (*q == '"') {
          const char *s = q + 1; const char *t = s;
          while (t < qe && *t != '"') t++;
          if (out->nregs < PTJ_MAXREG) out->regs[out->nregs++] = (uint8_t)ptj_regnum(s, (size_t)(t - s));
          q = t + 1;
        } else q++;
      }
    }
  }
  if (!have_addr || addr < base) return 0;
  out->off = (uint32_t)(addr - base);
  return 1;
}

// Parse a whole reply.  Returns the number of sites written, or -1 on error.
static int ptj_parse_reply(const char *buf, size_t len, uint64_t base, PtjSite *out, int maxs) {
  const char *s = (const char *)memmem(buf, len, "\"sites\":", 8);
  if (!s) return -1;
  if (memmem(buf, len < 40 ? len : 40, "\"ok\": false", 11) ||
      memmem(buf, len < 40 ? len : 40, "\"ok\":false", 10)) return -1;
  PtjJson j = {s + 8, buf + len};
  ptj_ws(&j);
  if (j.p >= j.e || *j.p != '[') return -1;
  j.p++;
  int n = 0;
  for (;;) {
    ptj_ws(&j);
    if (j.p >= j.e) return -1;
    if (*j.p == ']') break;
    if (*j.p == ',') { j.p++; continue; }
    PtjSite st;
    if (!ptj_parse_site(&j, base, &st)) return -1;
    if (n < maxs) out[n++] = st;
  }
  return n;
}

// `"restart_roots": [abs addr, ...]` -- the analyzer v2.21 reply carries, per function,
// the addresses at which its recursive descent RESTARTED after the descent from the entry ran
// out (an optimised code object is entered in the middle by OSR / deopt / an exception handler,
// so the reachable-from-offset-0 part of `JS:*array` is 39 of 1 888 bytes).  The patcher must
// seed ITS OWN sweep with them, or every site the analyzer found beyond the restart is dropped
// as `no_insn_boundary` and the object is instrumented exactly as badly as before.
// Returns the number of offsets written.  A reply may contain several functions, so every
// occurrence of the key is collected.
static int ptj_parse_restart_roots(const char *buf, size_t len, uint64_t base,
                                   uint32_t *out, int maxr) {
  int n = 0;
  const char *p = buf, *e = buf + len;
  static const char KEY[] = "\"restart_roots\":";
  for (;;) {
    const char *k = (const char *)memmem(p, (size_t)(e - p), KEY, sizeof(KEY) - 1);
    if (!k) break;
    p = k + sizeof(KEY) - 1;
    while (p < e && (*p == ' ' || *p == '\t')) p++;
    if (p >= e || *p != '[') continue;
    p++;
    while (p < e && *p != ']') {
      while (p < e && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
      if (p >= e || *p == ']') break;
      char *endp = nullptr;
      unsigned long long v = strtoull(p, &endp, 10);
      if (endp == p) { p++; continue; }
      p = endp;
      if (v >= base && n < maxr) out[n++] = (uint32_t)(v - base);
    }
  }
  return n;
}

// ------------------------------------------------------------- SHA-256 ------
struct PtjSha { uint32_t h[8]; uint64_t n; uint8_t b[64]; size_t bl; };
static const uint32_t PTJ_K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static inline uint32_t ptj_ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
static void ptj_sha_block(PtjSha *c, const uint8_t *p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|p[i*4+3];
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = ptj_ror(w[i-15],7) ^ ptj_ror(w[i-15],18) ^ (w[i-15] >> 3);
    uint32_t s1 = ptj_ror(w[i-2],17) ^ ptj_ror(w[i-2],19) ^ (w[i-2] >> 10);
    w[i] = w[i-16] + s0 + w[i-7] + s1;
  }
  uint32_t a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],h=c->h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t S1 = ptj_ror(e,6)^ptj_ror(e,11)^ptj_ror(e,25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + S1 + ch + PTJ_K[i] + w[i];
    uint32_t S0 = ptj_ror(a,2)^ptj_ror(a,13)^ptj_ror(a,22);
    uint32_t mj = (a & b) ^ (a & cc) ^ (b & cc);
    uint32_t t2 = S0 + mj;
    h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
  }
  c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=h;
}
static void ptj_sha_init(PtjSha *c) {
  static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                 0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  memcpy(c->h, iv, sizeof(iv)); c->n = 0; c->bl = 0;
}
static void ptj_sha_update(PtjSha *c, const void *data, size_t len) {
  const uint8_t *p = (const uint8_t *)data;
  c->n += len;
  while (len) {
    size_t k = 64 - c->bl; if (k > len) k = len;
    memcpy(c->b + c->bl, p, k); c->bl += k; p += k; len -= k;
    if (c->bl == 64) { ptj_sha_block(c, c->b); c->bl = 0; }
  }
}
static void ptj_sha_hex(PtjSha *c, char out[65]) {
  uint64_t bits = c->n * 8;
  uint8_t pad = 0x80;
  ptj_sha_update(c, &pad, 1);
  uint8_t z = 0;
  while (c->bl != 56) ptj_sha_update(c, &z, 1);
  uint8_t l[8];
  for (int i = 0; i < 8; i++) l[i] = (uint8_t)(bits >> (56 - 8*i));
  ptj_sha_update(c, l, 8);
  static const char hx[] = "0123456789abcdef";
  for (int i = 0; i < 8; i++)
    for (int b = 0; b < 4; b++) {
      uint8_t v = (uint8_t)(c->h[i] >> (24 - 8*b));
      out[i*8 + b*2] = hx[v >> 4]; out[i*8 + b*2 + 1] = hx[v & 15];
    }
  out[64] = 0;
}

// ------------------------------------------------------- analyzer client ----
struct PtjClient {
  int fd;
  pid_t svc_pid;      // the `analyze.py --serve` we spawned (0 if we attached to one)
  char sock[256];
  char cache[256];
  char py[256], script[256];
  char ver[32];       // analyzer version tag: part of the on-disk cache key (see below)
  int fast;           // ask for `"mode":"fast"' (coalesced sites) instead of "hifi".
                      // MUST be reflected in `ver`, or a fast answer is served for a hifi
                      // question out of the on-disk cache.
  uint32_t kf;        // `"keyframe": K' to ask the analyzer for (0 = no resync sites).
                      // K is ALSO part of the on-disk cache key: a k0 site list and a k1024
                      // one describe the same bytes, and serving one for the other is exactly
                      // the stale-cache bug that would silently undo the keyframe build.
  // stats
  uint64_t calls, call_ns, mem_hits, disk_hits, disk_writes, errors;
  uint64_t reconnects, unserved;   // service died and was re-dialled / no service at all
  uint64_t lat_ns[4096]; uint32_t nlat;
  char *rbuf; size_t rcap, rlen;
  char *wbuf; size_t wcap;
};

static int ptj_connect(PtjClient *c) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  struct sockaddr_un a; memset(&a, 0, sizeof(a));
  a.sun_family = AF_UNIX;
  snprintf(a.sun_path, sizeof(a.sun_path), "%s", c->sock);
  if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { close(fd); return -1; }
  return fd;
}

// Start `analyze.py --serve SOCK` (one per process) and wait for it to listen.
static int ptj_start_service(PtjClient *c, int timeout_ms) {
  c->fd = ptj_connect(c);
  if (c->fd >= 0) return 0;                       // somebody already serves it
  unlink(c->sock);
  char *argv[5] = {(char *)c->py, (char *)c->script, (char *)"--serve", (char *)c->sock, nullptr};
  extern char **environ;
  pid_t pid = 0;
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
  if (posix_spawn(&pid, c->py, &fa, nullptr, argv, environ) != 0) return -1;
  posix_spawn_file_actions_destroy(&fa);
  c->svc_pid = pid;   // so the caller can reap it: a service that outlives the process
                      // keeps the inherited stderr pipe open and wedges any harness that
                      // reads the child's output to EOF
  for (int i = 0; i < timeout_ms / 5; i++) {
    c->fd = ptj_connect(c);
    if (c->fd >= 0) return 0;
    struct timespec ts = {0, 5000000};
    nanosleep(&ts, nullptr);
  }
  return -1;
}

static inline uint64_t ptj_ns(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Synchronous request/response ("pause and analyze").
// `avoid` (analyzer v2.18) lists absolute addresses whose sites the rewriter could not
// patch.  The solver then treats those addresses as unavailable and returns an ALTERNATIVE
// covering set for the same values, instead of the caller simply losing them.  Callers
// drive this as a loop: request -> patch -> collect what would not go -> re-request with
// the union of the failures -> patch again.  With navoid == 0 the request is byte-for-byte
// what it was, so the V8 front end is unaffected.
// `roots` are extra entry offsets the VM knows about (HotSpot's jvmtiAddrLocationMap);
// `data_from` is the offset of the object's first inline-data byte, which clips the analyzer's
// restart fill.  `rroots`/`nrroots` receive the reply's `restart_roots` as OFFSETS -- the
// patcher must decode from them too.  All four are optional; with them absent the request is
// byte-for-byte what it was before v2.21.
static int ptj_analyze(PtjClient *c, const uint8_t *code, size_t len, uint64_t base,
                       const char *name, PtjSite *out, int maxs,
                       const uint64_t *avoid = nullptr, int navoid = 0,
                       const uint32_t *roots = nullptr, int nroots = 0,
                       uint32_t data_from = 0,
                       uint32_t *rroots = nullptr, int maxrroots = 0, int *nrroots = nullptr) {
  // A `--serve` process can die under memory pressure.  Without this the client kept a
  // dead fd (or none) and every later object silently got NO site list at all: the run
  // still succeeded, it was simply not instrumented, and no counter said so.  Re-dial
  // once per call and count it.
  if (c->fd < 0 && c->sock[0]) {
    c->fd = ptj_connect(c);
    if (c->fd >= 0) c->reconnects++;
  }
  if (c->fd < 0) { c->unserved++; return -1; }
  if (nrroots) *nrroots = 0;
  size_t need = len * 2 + 256 + (name ? strlen(name) : 0) +
                (size_t)navoid * 24 + (size_t)nroots * 24;
  if (need > c->wcap) {
    free(c->wbuf); c->wcap = need * 2; c->wbuf = (char *)malloc(c->wcap);
    if (!c->wbuf) { c->wcap = 0; return -1; }
  }
  static const char hx[] = "0123456789abcdef";
  char *w = c->wbuf;
  w += sprintf(w, "{\"base\":%llu,\"mode\":\"%s\",\"keyframe\":%u,\"name\":\"",
               (unsigned long long)base, c->fast ? "fast" : "hifi", (unsigned)c->kf);
  for (const char *s = name; s && *s; s++)
    if (*s != '"' && *s != '\\' && (unsigned char)*s >= 0x20 && (unsigned char)*s < 0x7f) *w++ = *s;
  w += sprintf(w, "\",\"hex\":\"");
  for (size_t i = 0; i < len; i++) { *w++ = hx[code[i] >> 4]; *w++ = hx[code[i] & 15]; }
  *w++ = '"';
  if (navoid > 0) {
    w += sprintf(w, ",\"avoid\":[");
    for (int i = 0; i < navoid; i++)
      w += sprintf(w, "%s%llu", i ? "," : "", (unsigned long long)avoid[i]);
    *w++ = ']';
  }
  if (nroots > 0) {
    w += sprintf(w, ",\"roots\":[");
    for (int i = 0; i < nroots; i++)
      w += sprintf(w, "%s%llu", i ? "," : "", (unsigned long long)(base + roots[i]));
    *w++ = ']';
  }
  if (data_from && data_from < len)
    w += sprintf(w, ",\"data_from\":%llu", (unsigned long long)(base + data_from));
  *w++ = '}'; *w++ = '\n';
  size_t wlen = (size_t)(w - c->wbuf);
  uint64_t t0 = ptj_ns();
  size_t off = 0;
  while (off < wlen) {
    ssize_t k = write(c->fd, c->wbuf + off, wlen - off);
    if (k <= 0) { c->errors++; close(c->fd); c->fd = -1; return -1; }
    off += (size_t)k;
  }
  // read one line
  c->rlen = 0;
  for (;;) {
    if (c->rlen + 65536 > c->rcap) {
      size_t nc = (c->rcap ? c->rcap * 2 : 1 << 20);
      while (nc < c->rlen + 65536) nc *= 2;
      char *nb = (char *)realloc(c->rbuf, nc);
      if (!nb) { c->errors++; return -1; }
      c->rbuf = nb; c->rcap = nc;
    }
    ssize_t k = read(c->fd, c->rbuf + c->rlen, c->rcap - c->rlen - 1);
    if (k <= 0) { c->errors++; close(c->fd); c->fd = -1; return -1; }
    c->rlen += (size_t)k;
    if (memchr(c->rbuf + c->rlen - (size_t)k, '\n', (size_t)k)) break;
  }
  uint64_t dt = ptj_ns() - t0;
  c->calls++; c->call_ns += dt;
  if (c->nlat < 4096) c->lat_ns[c->nlat++] = dt;
  int n = ptj_parse_reply(c->rbuf, c->rlen, base, out, maxs);
  if (n < 0) c->errors++;
  else if (rroots && maxrroots > 0 && nrroots)
    *nrroots = ptj_parse_restart_roots(c->rbuf, c->rlen, base, rroots, maxrroots);
  return n;
}

// --------------------------------------------------------- on-disk cache ----
// runtime/jit/cache/<masked-sha256>.<analyzer version>.sites : a compact text form
// of the site list, keyed by the code bytes with every relocatable field masked out
// (D7), so the same JS function recompiled at a different address in a later run
// hits.  The **analyzer version** is part of the file name: analyze.py's own cache
// key contains ANALYZER_VERSION, and a version-blind runtime cache would keep
// feeding pre-fix site lists to the patcher forever (PTJIT_CACHE_VER).
// runtime/jit/cache/<masked-sha256>.<ver>[.k<K>].sites -- the `.k<K>' component is present
// only for a keyframe build, so the (large) warm caches of the no-keyframe A/B control keep
// hitting while a k1024 build can never be served a k0 site list.
static void ptj_cache_path(PtjClient *c, const char *key, char *buf, size_t n) {
  if (c->kf) snprintf(buf, n, "%s/%s.%s.k%u.sites", c->cache, key, c->ver[0] ? c->ver : "v0", (unsigned)c->kf);
  else       snprintf(buf, n, "%s/%s.%s.sites", c->cache, key, c->ver[0] ? c->ver : "v0");
}

// PTJITC1: off id when kind size nregs [regs...]          (pre-keyframe, still read)
// PTJITC2: off id when kind size nregs flags_dead resync kf [regs...]
// PTJITC3: header carries the number of `restart_roots' and one line of offsets before
//          the sites.  A cache entry without them would hand the patcher a site list it cannot
//          place: the sites beyond the first restart are at boundaries the patcher's own sweep
//          never reaches, and every one of them is dropped as `no_insn_boundary'.
static int ptj_cache_load(PtjClient *c, const char *key, PtjSite *out, int maxs,
                          uint32_t *rroots = nullptr, int maxrroots = 0, int *nrroots = nullptr) {
  if (nrroots) *nrroots = 0;
  if (!c->cache[0]) return -1;
  char path[640];
  ptj_cache_path(c, key, path, sizeof path);
  FILE *f = fopen(path, "r");
  if (!f) return -1;
  int n = 0, ns = 0, ver = 0, nrr = 0;
  if (fscanf(f, "PTJITC%d %d", &ver, &ns) != 2 || ver < 1 || ver > 3) { fclose(f); return -1; }
  if (ver >= 3) {
    if (fscanf(f, " %d", &nrr) != 1) { fclose(f); return -1; }
    for (int i = 0; i < nrr; i++) {
      unsigned v;
      if (fscanf(f, " %u", &v) != 1) { fclose(f); return -1; }
      if (rroots && nrroots && *nrroots < maxrroots) rroots[(*nrroots)++] = v;
    }
  }
  for (int i = 0; i < ns; i++) {
    unsigned off, id, when, kind, size, nregs, fd = 0, rs = 0, kf = 0;
    if (fscanf(f, "%u %u %u %u %u %u", &off, &id, &when, &kind, &size, &nregs) != 6) break;
    if (ver >= 2 && fscanf(f, "%u %u %u", &fd, &rs, &kf) != 3) { fclose(f); return -1; }
    PtjSite s; memset(&s, 0, sizeof(s));
    s.off = off; s.id = (uint16_t)id; s.when = (uint8_t)when; s.kind = (uint8_t)kind;
    s.size = (uint8_t)size; s.nregs = (uint8_t)(nregs > PTJ_MAXREG ? PTJ_MAXREG : nregs);
    s.flags_dead = (uint8_t)fd; s.resync = (uint8_t)rs; s.kf = kf;
    for (unsigned r = 0; r < nregs; r++) {
      unsigned v; if (fscanf(f, "%u", &v) != 1) { fclose(f); return -1; }
      if (r < PTJ_MAXREG) s.regs[r] = (uint8_t)v;
    }
    if (n < maxs) out[n++] = s;
  }
  fclose(f);
  c->disk_hits++;
  return n;
}

static void ptj_cache_store(PtjClient *c, const char *key, const PtjSite *s, int n,
                            const uint32_t *rroots = nullptr, int nrroots = 0) {
  if (!c->cache[0]) return;
  char path[640], tmp[700];
  ptj_cache_path(c, key, path, sizeof path);
  snprintf(tmp, sizeof(tmp), "%s.%d.tmp", path, (int)getpid());
  FILE *f = fopen(tmp, "w");
  if (!f) return;
  fprintf(f, "PTJITC3 %d %d", n, nrroots);
  for (int i = 0; i < nrroots; i++) fprintf(f, " %u", rroots[i]);
  fputc('\n', f);
  for (int i = 0; i < n; i++) {
    fprintf(f, "%u %u %u %u %u %u %u %u %u", s[i].off, s[i].id, s[i].when, s[i].kind,
            s[i].size, s[i].nregs, s[i].flags_dead, s[i].resync, s[i].kf);
    for (int r = 0; r < s[i].nregs; r++) fprintf(f, " %u", s[i].regs[r]);
    fputc('\n', f);
  }
  fclose(f);
  rename(tmp, path);
  c->disk_writes++;
}
#endif
