// SPDX-License-Identifier: GPL-3.0-only
// Drives the telemetry ring: produce one of each record kind, drain, and assert the
// NDJSON contains them; then overflow the ring and assert (a) the drop count is
// reported rather than the records vanishing silently, and (b) the post-overflow
// drain emits exactly the records that fit — no burnt write-index causes a stale
// record from a previous lap to be re-serialised (the ring must never fabricate
// data on loss, only ever honestly lose it).
// Also drives dx8wasm_tel_pump()'s "tel.dropped" self-report: after the flood
// above, pumping must hand the page an NDJSON counter record naming the exact
// drop count, in the same flush as the flood's own records; a second pump with
// no further drops must add nothing. Both checks report via report_error (an
// exact string compare in the harness) rather than folding into the pixel
// tuple below: the harness accepts pixel components within +-2 of expected, so
// a 0-vs-1 boolean baked into a tuple slot could pass by that tolerance alone
// without actually exercising the assertion.
// Reports [linesDrained, sawSpanMs, postFloodExactAndDropped, fullyDrainedAfter].
#include "dx8wasm/telemetry.h"
#include <emscripten.h>
#include <cstring>
#include <cstdio>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

// Installs a page-side capture of gxTelemetry so this test can inspect the
// NDJSON that dx8wasm_tel_pump() hands to the page, the same seam a real
// consumer (or the replay-determinism gate) reads.
EM_JS(void, install_tel_capture, (), {
  window.__telCaptured = [];
  window.gxTelemetry = function(text) { window.__telCaptured.push(text); };
});

EM_JS(void, tel_capture_clear, (), { window.__telCaptured = []; });

EM_JS(int, tel_capture_is_empty, (), {
  return window.__telCaptured.length === 0 ? 1 : 0;
});

// True iff the capture contains exactly one "tel.dropped" counter record and
// its value is exactly `value`. Plain substring scan rather than a regex: EM_JS
// bodies pass through the same escape processing as a C string literal, which
// silently eats backslashes it doesn't recognise (\d, \{, ...) -- a regex
// written the normal way here quietly breaks in the emitted JS, and always
// reports "not found" (a false negative that reads exactly like a real
// failure). A literal substring has nothing to escape.
EM_JS(int, tel_capture_has_dropped, (int value), {
  var all = window.__telCaptured.join("");
  var key = "\"n\":\"tel.dropped\",\"v\":";
  var count = 0, last = -1, idx = 0;
  for (;;) {
    idx = all.indexOf(key, idx);
    if (idx < 0) break;
    idx += key.length;
    var j = idx;
    while (j < all.length && all[j] >= '0' && all[j] <= '9') j++;
    last = parseInt(all.slice(idx, j), 10);
    count++;
    idx = j;
  }
  return (count === 1 && last === value) ? 1 : 0;
});

// Busy-waits on the same clock dx8wasm_tel_pump() rate-limits against
// (emscripten_performance_now, via now_ms() inside telemetry.cpp), so a second
// pump call below is a genuine flush attempt rather than one gated shut by the
// one-per-DX8WASM_TEL_FLUSH_MS limiter. That distinction matters: without it,
// "no second record" would prove nothing about the delta bookkeeping, only that
// the limiter works.
EM_JS(void, busy_wait_ms, (double ms), {
  var start = performance.now();
  while (performance.now() - start < ms) { /* spin */ }
});

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

  // --- tel.dropped: emitted with the right value, and only when it changes ---
  install_tel_capture();

  // The ring is empty at this point (fullyDrainedAfter, above) and this is the
  // very first call to dx8wasm_tel_pump() in this process, so g_everFlushed is
  // still false and the rate limit cannot gate this call shut regardless of
  // real elapsed time. The tel.dropped counter this pump appends therefore
  // claims a slot in a ring with room to spare, and is drained in this same
  // flush.
  dx8wasm_tel_pump();
  if (!tel_capture_has_dropped((int)dropped)) {
    report_error("pump did not emit a tel.dropped counter with the flood's drop count");
    return 1;
  }

  // Advance real time past the flush interval, then pump again with no new
  // drops. If the delta bookkeeping is working, this flush computes a zero
  // delta and appends nothing, and the drain finds nothing new either, so
  // dx8wasm_tel_pump() returns before ever calling into the page.
  busy_wait_ms(DX8WASM_TEL_FLUSH_MS + 100);
  tel_capture_clear();
  dx8wasm_tel_pump();
  if (!tel_capture_is_empty()) {
    report_error("pump re-emitted tel.dropped (or something) with no new drops since the last report");
    return 1;
  }

  report_pixel(lines, sawSpanMs, postFloodExactAndDropped, fullyDrainedAfter);
  return 0;
}
