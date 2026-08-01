// SPDX-License-Identifier: GPL-3.0-only
// The fog coverage counter only fires on a non-LINEAR/NONE mode, so a zero could never
// distinguish "this game relies on linear fog" from "this game never touches fog"
// (docs/CONFORMANCE.md, zero-hit findings). This emits a positive-usage telemetry counter on
// every fog-mode TRANSITION — not a coverage counter: nothing here is unimplemented and nothing
// falls back. Asserts the record appears for LINEAR, is not re-emitted for an unchanged value,
// and that coverage counters stay untouched throughout. Reports [1,1,1,1].
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include "dx8wasm/telemetry.h"
#include <cstring>
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

static int count_occurrences(const char* haystack, const char* needle) {
  int n = 0;
  const size_t len = strlen(needle);
  for (const char* p = strstr(haystack, needle); p; p = strstr(p + len, needle)) ++n;
  return n;
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  dx8wasm_coverage before{};
  dx8wasm_get_coverage(&before);

  // One transition to LINEAR, then two redundant writes of the same value: a transition counter
  // must record the first and ignore the rest, or an engine that rewrites fog state per pass
  // would flood the ring with a value that never changed.
  dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
  dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
  dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);

  dx8wasm_coverage after{};
  dx8wasm_get_coverage(&after);   // also flushes the telemetry tally
  const int coverageUntouched =
      (after.unhandled_render_states == before.unhandled_render_states &&
       after.fallbacks_taken == before.fallbacks_taken) ? 1 : 0;
  if (!coverageUntouched) { report_error("a handled fog mode bumped a coverage counter"); return 1; }

  char tel[4096];
  dx8wasm_tel_drain(tel, sizeof tel);
  const char* const kKey = "\"n\":\"d3d8.fogmode.table.00000003\"";   // D3DFOG_LINEAR == 3
  const int occurrences = count_occurrences(tel, kKey);
  if (occurrences != 1) { report_error("linear fog was not recorded exactly once per transition"); return 1; }
  const int deltaIsOne = strstr(tel, "\"n\":\"d3d8.fogmode.table.00000003\",\"v\":1}") != nullptr ? 1 : 0;
  if (!deltaIsOne) { report_error("three identical writes were recorded as more than one transition"); return 1; }

  dev->Release(); d3d->Release();
  report_pixel(1, occurrences, deltaIsOne, coverageUntouched);
  return 0;
}
