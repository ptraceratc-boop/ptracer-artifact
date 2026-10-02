// wtb.js <benchmark> <warmup> <iters> -- the Web Tooling harness of the JIT Fast sweep (both arms).
// Same interface and output as suites/node/wtb.js: <warmup> untimed in-process iterations, then <iters>
// timed ones; prints the timed milliseconds, the analyzer calls that fell INSIDE the timed window and a
// checksum of the last result (compare it with the vanilla arm at the same iteration count).  In addition:
//   WTB_DRAIN_MS=T (default 0 = off): when the PTracer JIT hook is loaded (preload.js publishes it as
//     process[Symbol.for('ptjit.hook')]) and analyses asynchronously, wait up to T ms at the warm-up/timed
//     boundary -- untimed, before the window opens -- until every queued analysis has been installed.
//     Reported as `drain' {ms, pending_after}.  A no-op in the vanilla arm (no hook).
//   js_analysis_calls: analyses the JS thread itself ran inside the window (the async hook's off-thread
//     analyses do not stop the timed thread; they are in analysis_calls).  window_hook: the hook's counters
//     at the window's start and end.
// cwd must be web-tooling-benchmark/ (the payloads resolve their inputs relatively); WTB_DIR overrides it.
'use strict';
const src = require('path').join(process.env.WTB_DIR || process.cwd(), 'src') + '/';
const name = process.argv[2] || 'acorn';
const warm = parseInt(process.argv[3] || '5', 10);
const iters = parseInt(process.argv[4] || '10', 10);
const b = require(src + name + '-benchmark.js');
const fn = b.fn || b.run;
const E = process.env;
let hook = process[Symbol.for('ptjit.hook')] || null;
if (!hook && parseInt(E.PTJIT_MODE || '0', 10) > 0 && E.PTJIT_ADDON) hook = require(E.PTJIT_ADDON);
const hs = () => {
  if (!hook) return null;
  const s = hook.stats();
  return {analysis_calls: s.analysis_calls || 0, js_analysis_calls: s.js_analysis_calls || 0,
          aj_pending: s.aj_pending, aj_queued: s.aj_queued, aj_installed: s.aj_installed,
          patched: s.patched, objs_analyzed: s.objs_analyzed, async: s.async};
};
let last = null, drain = null;
for (let i = 0; i < warm; i++) last = fn();
const drainMs = parseInt(E.WTB_DRAIN_MS || '0', 10);
if (hook && hook.drain && drainMs > 0) {
  const d0 = process.hrtime.bigint();
  const left = hook.drain(drainMs);
  drain = {ms: Number(process.hrtime.bigint() - d0) / 1e6, pending_after: left};
}
const h0 = hs();
const t0 = process.hrtime.bigint();
for (let i = 0; i < iters; i++) last = fn();
const t1 = process.hrtime.bigint();
const h1 = hs();
const crypto = require('crypto');
const ck = crypto.createHash('sha256').update(JSON.stringify(last) || 'null')
  .digest('hex').slice(0, 16);
const r = {wtb: name, warmup: warm, iters, ms: Number(t1 - t0) / 1e6,
  analysis_calls: h0 ? h1.analysis_calls - h0.analysis_calls : 0, checksum: ck};
if (h0) { r.js_analysis_calls = h1.js_analysis_calls - h0.js_analysis_calls; r.window_hook = {start: h0, end: h1}; }
if (drain) r.drain = drain;
console.log(JSON.stringify(r));
