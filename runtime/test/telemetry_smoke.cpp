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
// no further drops must add nothing. Also constructs the case where the ring
// is still completely full at the exact moment the tel.dropped report itself
// tries to append (its own append is then dropped too) and asserts the true
// total eventually surfaces rather than being permanently swallowed by the
// self-drop. All three checks report via report_error (an exact string
// compare in the harness) rather than folding into the pixel tuple below: the
// harness accepts pixel components within +-2 of expected, so a small-integer
// or 0-vs-1 value baked into a tuple slot could pass by that tolerance alone
// without actually exercising the assertion.
// Gauge exactness is asserted the same way and for the same reason.
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
  // 1234567 is past %g's 6 significant digits deliberately: rendered with the span's
  // format this gauge would serialise as "1.23457e+06" and parse back as 1234570.
  // A simulation frame number corrupted like that destroys the only property gauges
  // exist to expose (how consecutive samples compare), so the exactness is asserted.
  dx8wasm_tel_gauge("logic.frame", 1234567.0);

  uint32_t n = dx8wasm_tel_drain(buf, sizeof buf);
  int lines = 0;
  for (uint32_t i = 0; i < n; i++) if (buf[i] == '\n') lines++;

  // The span must serialise its duration, not round it away.
  int sawSpanMs = strstr(buf, "\"n\":\"frame.logic\"") && strstr(buf, "\"ms\":2.5") ? 1 : 0;

  // Reported via report_error (an exact string compare in the harness) rather than a
  // pixel slot: the harness accepts pixel components within +-2, so a 0-vs-1 flag
  // baked into a tuple could pass on tolerance alone without the assertion running.
  // The full record shape is matched, not just the number, so a gauge emitted under
  // the wrong `k` cannot satisfy it.
  if (!strstr(buf, "\"k\":\"gauge\",\"n\":\"logic.frame\",\"v\":1234567}")) {
    report_error("gauge did not serialise as an exact k=gauge record (check %.17g, not %g)");
    return 1;
  }

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

  // --- tel.dropped's self-drop path: the ring is full at the exact moment the
  // report itself tries to append. This is the case the whole feature exists
  // to cover, and it needs its own deterministic setup: the append inside
  // dx8wasm_tel_pump() must find zero free slots.
  //
  // Guarantee an empty ring first (independent of anything above).
  {
    char drainAll[512];
    while (dx8wasm_tel_drain(drainAll, sizeof drainAll) != 0) {}
  }

  // Fill the ring to exactly capacity, then push one more: that one claim fails
  // (ring full), bumping g_dropped by 1. At this instant nothing has been
  // drained, so the ring is still completely full of "filler" records.
  for (int i = 0; i < DX8WASM_TEL_CAPACITY; i++) dx8wasm_tel_counter("filler", 1);
  dx8wasm_tel_counter("trigger_drop", 1);   // ring full: this claim fails, g_dropped += 1

  // Pump while the ring is still full. dx8wasm_tel_pump()'s own tel.dropped
  // append attempt claims a slot in that same full ring, so it is itself
  // dropped (g_dropped grows by 1 more here) and nothing is queued for this
  // flush. The drain that follows inside this same pump call empties part of
  // the backlog (buf is smaller than the full backlog), freeing room for next
  // flush's append to succeed.
  busy_wait_ms(DX8WASM_TEL_FLUSH_MS + 100);
  tel_capture_clear();
  dx8wasm_tel_pump();

  // The drain above only freed part of the "filler" backlog (its buffer is
  // smaller than 1024 records' worth of NDJSON); fully empty what remains here
  // via direct drain calls, outside of pump's own rate-limited flush, so the
  // *next* pump's tel.dropped append lands in an empty ring and gets drained
  // immediately — otherwise it would queue behind however many filler records
  // are still ahead of it and might not fit in the next flush's buffer either,
  // which would make this test flaky on backlog size rather than a clean
  // assertion about the bookkeeping.
  {
    char drainRest[512];
    while (dx8wasm_tel_drain(drainRest, sizeof drainRest) != 0) {}
  }

  // Now pump again. If the bookmark was wrongly advanced before the failed
  // append above, this flush computes delta = 1 (only the self-drop's own
  // increment) and the original drop is gone forever. If the bookmark was
  // correctly left unadvanced, this flush recomputes delta = 2 (the original
  // trigger_drop plus the self-drop) against the same unmoved watermark, and
  // — the ring having freed room via the drain above — the append succeeds
  // and reports the true total.
  busy_wait_ms(DX8WASM_TEL_FLUSH_MS + 100);
  tel_capture_clear();
  dx8wasm_tel_pump();
  // Exactly 2 drops happened since the ring was last emptied above:
  // "trigger_drop" (the real one this test is trying to report) and the
  // pump's own self-drop (its append attempt against the still-full ring).
  // A correct implementation reports both, eventually, without losing either.
  if (!tel_capture_has_dropped(2)) {
    report_error("pump under-reported drops when its own tel.dropped append hit a full ring");
    return 1;
  }

  report_pixel(lines, sawSpanMs, postFloodExactAndDropped, fullyDrainedAfter);
  return 0;
}
