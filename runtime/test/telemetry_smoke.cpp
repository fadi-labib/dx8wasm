// SPDX-License-Identifier: GPL-3.0-only
// Drives the telemetry ring: produce one of each record kind, drain, and assert the
// NDJSON contains them; then overflow the ring and assert (a) the drop count is
// reported rather than the records vanishing silently, and (b) the post-overflow
// drain emits exactly the records that fit — no burnt write-index causes a stale
// record from a previous lap to be re-serialised (the ring must never fabricate
// data on loss, only ever honestly lose it).
// Reports [linesDrained, sawSpanMs, postFloodExactAndDropped, fullyDrainedAfter].
#include "dx8wasm/telemetry.h"
#include <emscripten.h>
#include <cstring>
#include <cstdio>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

int main() {
  char buf[8192];

  dx8wasm_tel_log("boot", "engine up");
  dx8wasm_tel_counter("d3d8.unhandled_render_state", 3);
  dx8wasm_tel_span("frame.logic", 2.5);

  uint32_t n = dx8wasm_tel_drain(buf, sizeof buf);
  int lines = 0;
  for (uint32_t i = 0; i < n; i++) if (buf[i] == '\n') lines++;

  // The span must serialise its duration, not round it away.
  int sawSpanMs = strstr(buf, "\"n\":\"frame.logic\"") && strstr(buf, "\"ms\":2.5") ? 1 : 0;

  // A drained ring is empty — no record is delivered twice. The buffer here is
  // comfortably larger than any single record, so "empty" cannot be confused with
  // "the one queued record didn't fit".
  char again[256];
  if (dx8wasm_tel_drain(again, sizeof again) != 0) {
    report_error("ring not empty immediately after a full drain");
    return 1;
  }

  // Overflow: the ring holds DX8WASM_TEL_CAPACITY records; push twice that while
  // nothing is draining, so exactly DX8WASM_TEL_CAPACITY claims succeed and the
  // rest are dropped.
  for (int i = 0; i < DX8WASM_TEL_CAPACITY * 2; i++) dx8wasm_tel_counter("flood", 1);
  uint32_t dropped = dx8wasm_tel_dropped();

  // Drain the flood. A ring that burns a write index on a drop (rather than
  // rejecting the claim outright) makes the consumer walk past the burnt index and
  // re-serialise whatever stale record from a previous lap happens to sit there —
  // the drain would then report more lines than actually fit, or repeat a name/seq
  // pair already emitted above. Asserting the exact count is the clean way to catch
  // that without having to diff record contents.
  static char flood[128 * 1024];
  uint32_t fn = dx8wasm_tel_drain(flood, sizeof flood);
  int floodLines = 0;
  for (uint32_t i = 0; i < fn; i++) if (flood[i] == '\n') floodLines++;
  int postFloodExactAndDropped =
      (dropped > 0 && floodLines == DX8WASM_TEL_CAPACITY) ? 1 : 0;

  // And the ring must be fully drained now — no leftover from either lap, and no
  // record double-delivered across the two drains.
  char onceMore[256];
  int fullyDrainedAfter = dx8wasm_tel_drain(onceMore, sizeof onceMore) == 0 ? 1 : 0;

  report_pixel(lines, sawSpanMs, postFloodExactAndDropped, fullyDrainedAfter);
  return 0;
}
