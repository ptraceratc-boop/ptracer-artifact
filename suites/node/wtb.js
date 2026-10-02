// wtb.js <benchmark> <warmup> <iters> -- run one Web Tooling Benchmark payload: <warmup>
// untimed in-process iterations (JIT compilation and, under PTracer, the analysis of the
// compiled code happen here), then <iters> timed iterations. Prints the timed milliseconds,
// the number of analyzer calls that fell INSIDE the timed window (0 = steady state) and a
// checksum of the result so instrumented runs can be compared against vanilla ones.
// cwd must be web-tooling-benchmark/ (the payloads resolve their inputs relatively).
'use strict';
const path = require('path').join(__dirname, 'web-tooling-benchmark', 'src') + '/';
const name = process.argv[2] || 'acorn';
const warm = parseInt(process.argv[3] || '5', 10);
const iters = parseInt(process.argv[4] || '10', 10);
const b = require(path + name + '-benchmark.js');
const fn = b.fn || b.run;
const E = process.env;
const hook = (parseInt(E.PTJIT_MODE || '0', 10) > 0 && E.PTJIT_ADDON) ? require(E.PTJIT_ADDON) : null;
const calls = () => (hook ? hook.stats().analysis_calls : 0);
let last = null;
for (let i = 0; i < warm; i++) last = fn();
const before = calls();
const t0 = process.hrtime.bigint();
for (let i = 0; i < iters; i++) last = fn();
const t1 = process.hrtime.bigint();
const after = calls();
const crypto = require('crypto');
const ck = crypto.createHash('sha256').update(JSON.stringify(last) || 'null')
  .digest('hex').slice(0, 16);
console.log(JSON.stringify({wtb: name, warmup: warm, iters, ms: Number(t1 - t0) / 1e6,
  analysis_calls: after - before, checksum: ck}));
