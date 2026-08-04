// SPDX-License-Identifier: GPL-3.0-only
// The NDJSON -> OpenTelemetry reducer. Fixture in, expected spans/counters out — no
// browser, no engine, so this stays a fast unit test.
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { reduce, writeOtel } from '../otel/ndjson-to-otel.mjs';

const fixture = [
  '{"seq":0,"k":"log","n":"boot","d":"engine up"}',
  '{"seq":1,"k":"counter","n":"d3d8.unhandled.render_state.36","v":1}',
  '{"seq":2,"k":"counter","n":"d3d8.unhandled.render_state.36","v":2}',
  '{"seq":3,"k":"span","n":"frame.logic","ms":2.5}',
  '{"seq":4,"k":"span","n":"frame.logic","ms":6.5}',
  'not json at all',
  '{"seq":5,"k":"bogus","n":"whatever"}',
  '{"seq":6,"k":"span","n":"frame.logic"}',
  '{"seq":7,"k":"span","n":"frame.logic","ms":"not-a-number"}',
  '{"seq":8,"k":"counter","n":"d3d8.unhandled.render_state.36"}',
  '{"seq":9,"k":"counter","n":"d3d8.zero_delta_probe","v":0}',
  // A gauge series that advances, then jumps backwards once (what a saved-game
  // restore looks like), then advances again. Frame 0 is a legitimate sample, and
  // one gauge record is deliberately missing its `v`.
  '{"seq":10,"k":"gauge","n":"logic.frame","v":0}',
  '{"seq":11,"k":"gauge","n":"logic.frame","v":1200}',
  '{"seq":12,"k":"gauge","n":"logic.frame","v":1201}',
  '{"seq":13,"k":"gauge","n":"logic.frame","v":300}',
  '{"seq":14,"k":"gauge","n":"logic.frame","v":301}',
  '{"seq":15,"k":"gauge","n":"logic.frame"}',
];

const r = reduce(fixture);
assert.equal(r.counters['d3d8.unhandled.render_state.36'], 3, 'counters sum');
assert.equal(r.spans.length, 2, 'two spans');
assert.equal(r.logs[0].detail, 'engine up', 'log detail preserved');
assert.equal(r.malformed, 6, 'unparseable JSON, unknown k, missing ms, non-numeric ms, missing counter v, and missing gauge v all count as malformed');
assert.equal(r.summary['frame.logic'].max, 6.5, 'span max');
assert.equal(r.summary['frame.logic'].mean, 4.5, 'span mean');
assert.equal(Number.isNaN(r.summary['frame.logic'].total), false, 'a bad span record must not poison the summary with NaN');
// A `0` delta is a legitimate counter value (the producer really did report no
// increment on that call) and must be recorded, not treated as "missing" the way
// `rec.v || 0` would.
assert.equal(r.counters['d3d8.zero_delta_probe'], 0, 'a genuine 0 delta counter is recorded, not dropped as malformed');

// --- gauges: a sampled series, not an aggregate ------------------------------
const gs = r.gaugeSummary['logic.frame'];
assert.equal(r.gauges.length, 5, 'five well-formed gauge samples; the one missing `v` is malformed');
assert.equal(gs.count, 5, 'gauge count');
assert.equal(gs.first, 0, 'a 0 sample is a real sample, not a missing one');
assert.equal(gs.last, 301, 'last sample is the last in claim order, not the largest');
assert.equal(gs.max, 1201, 'max is the high-water mark, not the last value');
assert.equal(gs.min, 0, 'min spans the whole series');
// The point of the whole kind: one backwards step is visible and quantified. A
// summed counter would report 3003 and a span summary would report a mean; neither
// can distinguish this series from one that only ever advanced.
assert.equal(gs.decreases, 1, 'exactly one backwards step in the series');
assert.equal(gs.maxDecrease, 901, 'the size of the backwards step, 1201 -> 300');
// A monotonic series must report zero decreases — otherwise "decreases > 0" would be
// evidence of nothing.
assert.equal(reduce([
  '{"seq":0,"k":"gauge","n":"m","v":1}',
  '{"seq":1,"k":"gauge","n":"m","v":1}',
  '{"seq":2,"k":"gauge","n":"m","v":2}',
]).gaugeSummary['m'].decreases, 0, 'a non-decreasing series (including a repeat) reports no decreases');
// Gauges are not spans: they must not leak into the span summary, whose fields the
// perf work reads as a contract.
assert.equal(r.summary['logic.frame'], undefined, 'a gauge does not appear in the span summary');

const dir = mkdtempSync(join(tmpdir(), 'otel-'));
const outFile = join(dir, 'trace.ndjson');
await writeOtel(fixture, outFile);
const written = readFileSync(outFile, 'utf8').trim().split('\n').map(JSON.parse);
assert.ok(written.length >= 2, 'one OTel span per timed record');
assert.ok(written.every(s => s.traceId && s.spanId), 'every span carries trace ids');
assert.ok(written.some(s => s.name === 'frame.logic'), 'span name preserved');
// The synthetic clock must be monotonic non-decreasing across the written spans
// (start <= end for each span, and each span's start >= the previous span's end),
// even though — per the module's doc comment — this ordering is fabricated, not a
// claim about real wall-clock causality.
for (const s of written) assert.ok(s.startTimeUnixNano <= s.endTimeUnixNano, 'span start <= end');
for (let i = 1; i < written.length; i++) {
  assert.ok(written[i].startTimeUnixNano >= written[i - 1].endTimeUnixNano, 'spans do not overlap in the synthetic clock');
}
assert.deepEqual(written.map(s => s.attributes['dx8wasm.seq']), [3, 4], 'seq passthrough into attributes, in order');

console.log('otel.test.mjs: PASS');
