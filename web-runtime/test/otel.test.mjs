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
];

const r = reduce(fixture);
assert.equal(r.counters['d3d8.unhandled.render_state.36'], 3, 'counters sum');
assert.equal(r.spans.length, 2, 'two spans');
assert.equal(r.logs[0].detail, 'engine up', 'log detail preserved');
assert.equal(r.malformed, 1, 'malformed lines are counted, not ignored');
assert.equal(r.summary['frame.logic'].max, 6.5, 'span max');
assert.equal(r.summary['frame.logic'].mean, 4.5, 'span mean');

const dir = mkdtempSync(join(tmpdir(), 'otel-'));
const outFile = join(dir, 'trace.ndjson');
await writeOtel(fixture, outFile);
const written = readFileSync(outFile, 'utf8').trim().split('\n').map(JSON.parse);
assert.ok(written.length >= 2, 'one OTel span per timed record');
assert.ok(written.every(s => s.traceId && s.spanId), 'every span carries trace ids');
assert.ok(written.some(s => s.name === 'frame.logic'), 'span name preserved');

console.log('otel.test.mjs: PASS');
