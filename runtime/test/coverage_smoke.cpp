// SPDX-License-Identifier: GPL-3.0-only
// Drives the 2.5 coverage/fallback layer: feed the device three unhandled tokens
// (a render state, hit 3x to exercise telemetry coalescing, a texture-stage op,
// and a texture format), then assert the contract counters incremented, the
// callback fired once per distinct item, rendering still works, and the
// telemetry ring received exactly one coalesced counter record per distinct
// token (not one record per occurrence). Reports [rsTopTss, formats, cbCount,
// sawCoalescedTelemetry] (0-based slots 0-3) — see the report_pixel() call below
// for why the slots are laid out that way.
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include "dx8wasm/telemetry.h"
#include <cstring>
#include <emscripten.h>
#include <GLES3/gl3.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

static int g_cbCount = 0;
static void on_unhandled(const char*, uint32_t, void*) { g_cbCount++; }

// Counts non-overlapping occurrences of `needle` in `haystack`. Used to assert
// *uniqueness*, not just presence: a buggy implementation that emits both an
// uncoalesced "v":1 record and a coalesced "v":3 record for the same key would
// pass a plain strstr() presence check on "v":3 alone, since that substring
// would still be found. Requiring the key name itself to appear exactly once
// closes that gap.
static int count_occurrences(const char* haystack, const char* needle) {
  int n = 0;
  const size_t len = strlen(needle);
  for (const char* p = strstr(haystack, needle); p; p = strstr(p + len, needle)) ++n;
  return n;
}

int main() {
  dx8wasm_set_unhandled_callback(on_unhandled, nullptr);

  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  // Three distinct unhandled tokens. D3DRS_FILLMODE stays unimplemented, so this
  // smoke is stable as later slices implement other render states. It is hit 3
  // times (same token, D3DRS_FILLMODE = 0x00000008) to exercise coalescing: the
  // per-occurrence counters below must see 3 calls, but the telemetry ring must
  // still only carry one counter record for it, with delta 3.
  dev->SetRenderState(D3DRS_FILLMODE, 2 /* wireframe */);
  dev->SetRenderState(D3DRS_FILLMODE, 1 /* solid — still the same unhandled token */);
  dev->SetRenderState(D3DRS_FILLMODE, 2 /* wireframe again */);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, 25 /* D3DTOP_MULTIPLYADD, unimplemented */);   // -> fallback
  IDirect3DTexture8* tex = nullptr;
  dev->CreateTexture(2, 2, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, &tex);   // unsupported format
  // A stage state with no implementation (D3DTSS_MAXANISOTROPY = 21) must be COUNTED, not
  // silently dropped — the render-state path reported its gaps while this one swallowed them.
  dev->SetTextureStageState(0, (D3DTEXTURESTAGESTATETYPE)21, 4);

  // dx8wasm_get_coverage() flushes the telemetry tally as one of its side effects
  // (see coverage.cpp), so the drain just below is guaranteed to see this batch's
  // records rather than whatever was still sitting in the tally table.
  dx8wasm_coverage cov{};
  dx8wasm_get_coverage(&cov);
  if (cov.unhandled_render_states != 3 || cov.unhandled_texture_stage_ops != 1 ||
      cov.unhandled_formats != 1 || cov.unhandled_texture_stage_states != 1 ||
      cov.fallbacks_taken != 6 || g_cbCount != 4) {
    report_error("coverage counters wrong"); return 1;
  }

  // The coverage layer must emit telemetry coalesced per distinct token, not one
  // record per occurrence: 3 calls on the same D3DRS_FILLMODE token must drain to
  // exactly one counter record carrying delta 3, or a real playthrough's ring
  // would overrun on the very first busy frame. A pre-coalescing implementation
  // would instead show three separate records each with "v":1 and no "v":3 line.
  // Checking for exactly one occurrence of the key name (not just presence of
  // the delta-3 substring) also catches an implementation that emits an extra
  // uncoalesced delta-1 record alongside a correct delta-3 one — presence of
  // the delta-3 substring alone would not catch that, since it would still be
  // found regardless of what else is in the ring.
  char tel[4096];
  dx8wasm_tel_drain(tel, sizeof tel);
  const char* const kKey = "\"n\":\"d3d8.unhandled.rstate.00000008\"";
  const int keyOccurrences = count_occurrences(tel, kKey);
  const int sawCoalescedTelemetry =
      (keyOccurrences == 1 && strstr(tel, "\"n\":\"d3d8.unhandled.rstate.00000008\",\"v\":3}") != nullptr) ? 1 : 0;
  if (!sawCoalescedTelemetry) { report_error("coverage telemetry was not coalesced per token"); return 1; }

  // Rendering must continue despite the unhandled state — clear and read back.
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);
  unsigned char px[4] = {0};
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  if (px[0] != 51 || px[1] != 102 || px[2] != 204) { report_error("rendering stopped after fallback"); return 1; }

  // report_pixel() takes exactly four ints but there are five facts to check, so
  // the render-state (3), texture-op (1) and stage-state (1) counters — each
  // already asserted individually just above — are folded into slot 0 as a sum
  // rather than reported separately; that frees a slot for the coalescing check
  // to be independently readable rather than smuggled into an existing slot
  // where it could hide a false pass.
  // Slots (0-based): [0] rsTopTss = 3 (render-state) + 1 (texture-op) + 1
  // (stage-state) = 5; [1] formats = 1; [2] cbCount = 4; [3] sawCoalescedTelemetry
  // = 1, independently asserted above (with its own report_error + early return)
  // so a false "was coalesced" report cannot hide behind an unrelated slot value
  // or the harness's ±2 pixel-comparison tolerance.
  report_pixel(cov.unhandled_render_states + cov.unhandled_texture_stage_ops + cov.unhandled_texture_stage_states,
               cov.unhandled_formats, g_cbCount, sawCoalescedTelemetry);
  if (tex) tex->Release();
  dev->Release(); d3d->Release();
  return 0;
}
