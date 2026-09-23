// preload.js -- installs the V8 JIT hook as early as possible: `node -r ./preload.js`
// PTracer v2 runtime/jit (D9).  Config from the environment:
//   PTJIT_MODE     0 off | 1 count | 2 +code dump | 3 +entry-only patch | 4 +full site set
//   PTJIT_PTW      1 = trampolines contain `ptwrite` (default), 0 = bare detour (A/B control)
//   PTJIT_ENUM     1 = also enumerate the code objects that exist when the hook is installed
//   PTJIT_DUMP     jitdump path      PTJIT_SITEMAP  site-map path     PTJIT_MAPS  /proc/self/maps copy
//   PTJIT_STATS    path for the JSON stats line (also printed to stderr as "PTJIT {...}")
//   PTJIT_SOCK / PTJIT_CACHE / PTJIT_PY / PTJIT_ANALYZE / PTJIT_NOSPAWN / PTJIT_SPACE / PTJIT_SPARKPLUG
//   PTJIT_KEYFRAME K = resync-keyframe period (default 1024, 0 = off).  A JIT function is
//                  entered by OSR at a loop header, so without these the object's entry
//                  anchor never runs (defect D-J1, the design notes
//   PTJIT_KFCTR    countdown cells to reserve (default 65536)
//   PTJIT_KF_FLAGS_LIVE  1 = also emit a keyframe where EFLAGS are live (default 0 = drop)
//   PTJIT_GT       1 = same-run ground truth: every memory access of a patched object also
//                  logs its effective address to gt.<pid>.<pid>.bin (PTJIT_GT_DIR,
//                  PTJIT_GT_MB window, default 4096).  A measurement build (defect D-J2).
//   PTJIT_ADDON    load a different addon build (the pre-fix one, for the reliability table)
//   PTJIT_FAST     0 (default) conservative value objective; 1 Fast location objective.
//                  Both use this runtime-assisted substrate, not Pin JIT. Cache keys differ.
//   PTJIT_STACK_ANCHOR  0 unchanged (default), 1 rsp, 2 rsp+rbp at each object entry.
//                  Native MODE=4 only; post-cache augmentation, no selected values removed.
//   PTJIT_PIN      1 = publish analyzed JIT plans to hifitool -jitbridge 1 instead of
//                  patching application bytes. Requires MODE=4 and KEYFRAME=0.
//                  Pin performs instrumentation; native images still need ELF PLANs.
// The legacy JITPOC_* names from eval/jit-poc are still accepted.
'use strict';
const E = process.env;
const g = (a, b, d) => (E[a] !== undefined ? E[a] : (E[b] !== undefined ? E[b] : d));
const mode = parseInt(g('PTJIT_MODE', 'JITPOC_MODE', '0'), 10);
if (mode > 0) {
  const hook = require(E.PTJIT_ADDON || (__dirname + '/jithook.node'));
  hook.enable(mode,
    parseInt(g('PTJIT_ARENA_MB', 'JITPOC_ARENA_MB', '1024'), 10),
    parseInt(g('PTJIT_VERBOSE', 'JITPOC_VERBOSE', '0'), 10),
    parseInt(g('PTJIT_PTW', 'JITPOC_PTW', '1'), 10),
    parseInt(g('PTJIT_MAXPATCH', 'JITPOC_MAXPATCH', '-1'), 10),
    parseInt(g('PTJIT_ENUM', 'JITPOC_ENUM', '1'), 10),
    parseInt(g('PTJIT_CNT', 'JITPOC_CNT', '0'), 10),
    parseInt(g('PTJIT_NOIMM64', 'JITPOC_NOIMM64', '0'), 10),
    parseInt(g('PTJIT_MINPATCH', 'JITPOC_MINPATCH', '0'), 10));
  process.on('exit', () => {
    const st = hook.stats();
    if (mode >= 3) st.verify = hook.verify();
    const cnt = g('PTJIT_COUNTERS', 'JITPOC_COUNTERS', null);
    if (cnt) {
      const c = hook.counters();
      st.executed = c.length;
      st.hits = c.reduce((a, x) => a + x.count, 0);
      require('fs').writeFileSync(cnt, JSON.stringify(c));
    }
    const dump = g('PTJIT_DUMP', 'JITPOC_DUMP', null);
    if (dump) st.flushed = hook.flush(dump);
    const sm = g('PTJIT_SITEMAP', null, null);
    if (sm) st.sitemap = hook.sitemap(sm);
    const maps = g('PTJIT_MAPS', 'JITPOC_MAPS', null);
    if (maps) {
      try { require('fs').writeFileSync(maps, require('fs').readFileSync('/proc/self/maps')); }
      catch (e) {}
    }
    const lat = st.analysis_ms; delete st.analysis_ms;
    const sp = E.PTJIT_STATS;
    if (sp) { try { require('fs').writeFileSync(sp, JSON.stringify(Object.assign({}, st, {analysis_ms: lat}))); } catch (e) {} }
    process._rawDebug('PTJIT ' + JSON.stringify(st));
  });
}
