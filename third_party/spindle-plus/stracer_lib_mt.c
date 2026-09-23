/*
 * S-Tracer runtime library -- SPINDLE-PLUS (thread-safe variant of stracer_lib.c).
 *
 * The base Spindle S-Tracer runtime uses ONE global trace buffer, which races and
 * corrupts under multiple threads. memcached is multithreaded (worker threads +
 * event loop), so Spindle-Plus gives every thread its OWN private buffer via
 * thread-local storage. There is NO per-access lock -- the per-access cost stays
 * the same single store as the single-threaded version (it works on threads without
 * changing the algorithm or its performance characteristics). A mutex is taken
 * only on the rare bulk flush (every 64K
 * entries) when writing the shared output fd, and on thread-buffer registration.
 *
 * Same 16-byte entry format and same instrumentation entry points as the base
 * runtime (__strace_addr / __strace_base / __strace_loop_enter / __strace_loop_exit,
 * __init_main / __fini_main). Output -> $STRACE_OUT (default /dev/null = no-I/O metric).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

typedef struct { uint64_t addr; uint64_t meta; } STraceEntry;

#define ST_TAG_ACCESS     0ULL
#define ST_TAG_LOOP_ENTER 1ULL
#define ST_TAG_LOOP_EXIT  2ULL
#define ST_TAG_BASE       3ULL
#define ST_TAG_SHIFT      56
#define ST_META(w, s, t) (((uint64_t)(w) & 1ULL) | (((uint64_t)(s) & 0xFFFFULL) << 1) | ((t) << ST_TAG_SHIFT))

#define BUF_ENTRIES (1u << 16) /* 64K entries = 1 MiB per-thread buffer */

typedef struct ThreadBuf {
  STraceEntry *buf;
  uint32_t pos;
  unsigned long long count;
  struct ThreadBuf *next; /* registry link */
} ThreadBuf;

static int g_fd = -1;
static pthread_mutex_t g_wlock = PTHREAD_MUTEX_INITIALIZER;   /* serializes writes to g_fd */
static pthread_mutex_t g_reglock = PTHREAD_MUTEX_INITIALIZER; /* protects g_registry */
static ThreadBuf *g_registry = NULL;
static pthread_key_t g_key;
static int g_key_ready = 0;
static __thread ThreadBuf *tl = NULL; /* this thread's buffer (fast path) */

static void tb_flush(ThreadBuf *t) {
  if (t->pos == 0) return;
  if (g_fd >= 0) {
    pthread_mutex_lock(&g_wlock);
    ssize_t n = write(g_fd, t->buf, (size_t)t->pos * sizeof(STraceEntry));
    (void)n;
    pthread_mutex_unlock(&g_wlock);
  }
  t->pos = 0;
}

/* pthread_key destructor: flush a worker thread's buffer when it exits. The
 * ThreadBuf stays in the registry (so __fini_main can still sum counts). */
static void tb_destructor(void *p) {
  if (p) tb_flush((ThreadBuf *)p);
}

static ThreadBuf *tb_get(void) {
  if (tl) return tl;
  ThreadBuf *t = (ThreadBuf *)calloc(1, sizeof(ThreadBuf));
  if (!t) { perror("stracer-mt: calloc"); _exit(1); }
  t->buf = (STraceEntry *)malloc((size_t)BUF_ENTRIES * sizeof(STraceEntry));
  if (!t->buf) { perror("stracer-mt: malloc"); _exit(1); }
  pthread_mutex_lock(&g_reglock);
  t->next = g_registry;
  g_registry = t;
  pthread_mutex_unlock(&g_reglock);
  tl = t;
  if (g_key_ready) pthread_setspecific(g_key, t);
  return t;
}

static inline void st_emit(uint64_t addr, uint64_t meta) {
  ThreadBuf *t = tb_get();
  t->buf[t->pos].addr = addr;
  t->buf[t->pos].meta = meta;
  ++t->pos;
  ++t->count;
  if (t->pos == BUF_ENTRIES) tb_flush(t);
}

void __init_main(void) {
  const char *out = getenv("STRACE_OUT");
  if (!out) out = "/dev/null";
  g_fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (g_fd < 0) { perror("stracer-mt: open STRACE_OUT"); _exit(1); }
  if (pthread_key_create(&g_key, tb_destructor) == 0) g_key_ready = 1;
}

void __fini_main(void) {
  unsigned long long total = 0;
  unsigned nthreads = 0;
  pthread_mutex_lock(&g_reglock);
  for (ThreadBuf *t = g_registry; t; t = t->next) {
    tb_flush(t);
    total += t->count;
    ++nthreads;
  }
  pthread_mutex_unlock(&g_reglock);
  if (g_fd >= 0) close(g_fd);
  fprintf(stderr, "[stracer-mt] total dynamic trace entries: %llu (%.2f MiB) across %u threads\n",
          total, (double)total * sizeof(STraceEntry) / (1024.0 * 1024.0), nthreads);
}

void __strace_addr(void *p, uint64_t meta) { st_emit((uint64_t)p, meta); }
void __strace_base(void *p, uint64_t sz) { st_emit((uint64_t)p, ST_META(0, sz & 0xFFFF, ST_TAG_BASE)); }
void __strace_loop_enter(uint64_t start) { st_emit(start, ST_META(0, 0, ST_TAG_LOOP_ENTER)); }
void __strace_loop_exit(uint64_t end) { st_emit(end, ST_META(0, 0, ST_TAG_LOOP_EXIT)); }
