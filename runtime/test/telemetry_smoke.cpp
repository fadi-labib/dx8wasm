// SPDX-License-Identifier: GPL-3.0-only
// Drives the telemetry ring: produce one of each record kind, drain, and assert the
// NDJSON contains them; then overflow the ring and assert the drop count is reported
// rather than the records vanishing silently.
// Reports [linesDrained, sawSpanMs, droppedAfterOverflow>0, secondDrainEmpty].
#include "dx8wasm/telemetry.h"
#include <emscripten.h>
#include <cstring>
#include <cstdio>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });

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

  // A drained ring is empty — no record is delivered twice.
  char again[64];
  int secondDrainEmpty = dx8wasm_tel_drain(again, sizeof again) == 0 ? 1 : 0;

  // Overflow: the ring holds DX8WASM_TEL_CAPACITY records; push twice that.
  for (int i = 0; i < DX8WASM_TEL_CAPACITY * 2; i++) dx8wasm_tel_counter("flood", 1);
  int dropped = dx8wasm_tel_dropped() > 0 ? 1 : 0;

  report_pixel(lines, sawSpanMs, dropped, secondDrainEmpty);
  return 0;
}
