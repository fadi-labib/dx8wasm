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

// Fold NDJSON lines into { spans, counters, logs, malformed, summary }.
export function reduce(lines) {
  const spans = [], logs = [], counters = {};
  let malformed = 0;
  for (const line of lines) {
    const text = String(line).trim();
    if (!text) continue;
    let rec;
    try { rec = JSON.parse(text); } catch { malformed++; continue; }
    if (rec.k === 'counter') counters[rec.n] = (counters[rec.n] || 0) + (rec.v || 0);
    else if (rec.k === 'span') spans.push({ name: rec.n, ms: rec.ms, seq: rec.seq });
    else if (rec.k === 'log') logs.push({ name: rec.n, detail: rec.d ?? '' });
    else malformed++; // parsed fine, but an unrecognized `k` means producer/reducer disagree
  }
  // Per-name span statistics — this is what the perf work reads. Field names are a
  // contract: count/total/max/mean, do not rename.
  const summary = {};
  for (const s of spans) {
    const e = summary[s.name] || (summary[s.name] = { count: 0, total: 0, max: 0 });
    e.count++; e.total += s.ms; e.max = Math.max(e.max, s.ms);
  }
  for (const e of Object.values(summary)) e.mean = e.total / e.count;
  return { spans, counters, logs, malformed, summary };
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
