// SPDX-License-Identifier: GPL-3.0-only
// Reduce dx8wasm telemetry NDJSON into a summary and into OpenTelemetry spans.
//
// The span exporter is deliberately file-based and dependency-light: a capture run
// should produce a readable trace with no collector running. This mirrors
// tools/mcp-general/telemetry.mjs in the generals-dx8wasm MCP work — same
// FileSpanExporter idea, one span per interesting record. Point a real OTLP
// collector at the output later if you want Jaeger/Tempo.
import { appendFileSync, writeFileSync } from 'node:fs';
import { randomBytes } from 'node:crypto';

// Fold NDJSON lines into { spans, counters, logs, gauges, malformed, summary,
// gaugeSummary }.
//
// `lines` must be in producer claim order — which is what the ring emits and what a
// capture harness appends. `gaugeSummary[name].decreases` is the only statistic here
// that depends on that ordering; every other one is order-independent.
export function reduce(lines) {
  const spans = [], logs = [], counters = {}, gauges = [];
  let malformed = 0;
  for (const line of lines) {
    const text = String(line).trim();
    if (!text) continue;
    let rec;
    try { rec = JSON.parse(text); } catch { malformed++; continue; }
    // A record that claims a kind but lacks (or misdefines) that kind's required
    // field is structurally-valid JSON but semantically broken — it must count as
    // malformed rather than flow through and quietly poison an aggregate (a NaN
    // `ms` would otherwise turn a whole span-key's summary into NaN with no trace
    // of why). `0` is a legitimate counter delta and is accepted, not defaulted —
    // only a missing/non-numeric `v` is treated as malformed.
    if (rec.k === 'counter') {
      if (typeof rec.v !== 'number' || Number.isNaN(rec.v)) { malformed++; continue; }
      counters[rec.n] = (counters[rec.n] || 0) + rec.v;
    } else if (rec.k === 'span') {
      if (typeof rec.ms !== 'number' || Number.isNaN(rec.ms)) { malformed++; continue; }
      spans.push({ name: rec.n, ms: rec.ms, seq: rec.seq });
    } else if (rec.k === 'gauge') {
      // Same rule as counters and spans: a gauge whose `v` is missing or non-numeric
      // is malformed, not a zero. `0` is a legitimate sample (frame 0 exists).
      if (typeof rec.v !== 'number' || Number.isNaN(rec.v)) { malformed++; continue; }
      gauges.push({ name: rec.n, v: rec.v, seq: rec.seq });
    } else if (rec.k === 'log') {
      logs.push({ name: rec.n, detail: rec.d ?? '' });
    } else malformed++; // parsed fine, but an unrecognized `k` means producer/reducer disagree
  }
  // Per-name span statistics — this is what the perf work reads. Field names are a
  // contract: count/total/max/mean, do not rename.
  const summary = {};
  for (const s of spans) {
    const e = summary[s.name] || (summary[s.name] = { count: 0, total: 0, max: 0 });
    e.count++; e.total += s.ms; e.max = Math.max(e.max, s.ms);
  }
  for (const e of Object.values(summary)) e.mean = e.total / e.count;

  // Per-name gauge statistics. `decreases` is the interesting one and the reason the
  // gauge kind exists: for a value the engine only ever advances — a simulation frame
  // number — a single decrease is proof that something reset it, and a saved-game
  // restore is the only thing in normal play that does. count/total/max/mean (the span
  // statistics) cannot express that, and neither can a summed counter.
  //
  // `first`/`last` are recorded rather than reconstructed from min/max because the
  // series is not assumed monotonic: after a restore, `min` is not the first sample.
  const gaugeSummary = {};
  for (const g of gauges) {
    const e = gaugeSummary[g.name] || (gaugeSummary[g.name] = {
      count: 0, first: g.v, last: g.v, min: g.v, max: g.v, decreases: 0, maxDecrease: 0,
    });
    if (e.count > 0 && g.v < e.last) {
      e.decreases++;
      e.maxDecrease = Math.max(e.maxDecrease, e.last - g.v);
    }
    e.count++; e.last = g.v;
    e.min = Math.min(e.min, g.v);
    e.max = Math.max(e.max, g.v);
  }
  return { spans, counters, logs, gauges, malformed, summary, gaugeSummary };
}

// Write one NDJSON OTel-shaped span per timed record, under a single trace.
//
// Synthetic timestamps: the ring records carry a duration (`ms`) but no wall-clock
// time, so this accumulates durations into a running clock (`t`) to fabricate
// start/end times. The result is a trace whose spans always appear back-to-back and
// in file order — a future reader must not mistake that layout for real wall-clock
// ordering; it is an artifact of this fabrication, not evidence the recorded spans
// were actually sequential or contiguous.
export async function writeOtel(lines, filePath) {
  const { spans } = reduce(lines);
  const traceId = randomBytes(16).toString('hex');
  writeFileSync(filePath, '');
  let t = 0;
  for (const s of spans) {
    const start = t;
    t += s.ms;
    appendFileSync(filePath, JSON.stringify({
      name: s.name,
      traceId,
      spanId: randomBytes(8).toString('hex'),
      parentSpanId: null,
      startTimeUnixNano: Math.round(start * 1e6),
      endTimeUnixNano: Math.round(t * 1e6),
      attributes: { 'dx8wasm.seq': s.seq },
      status: { code: 0 },
    }) + '\n');
  }
  return spans.length;
}
