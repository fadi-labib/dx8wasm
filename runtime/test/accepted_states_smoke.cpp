// SPDX-License-Identifier: GPL-3.0-only
// States this backend deliberately ACCEPTS WITHOUT ACTING must not bump a coverage counter.
// The counters mean "unimplemented, fell back"; a decision to no-op is not a gap, and letting
// the two share a counter is how 40k/frame of D3DRS_PATCHSEGMENTS came to dominate a capture
// that was supposed to rank real work. The mirror assertion matters just as much: a state the
// backend truly cannot express must STILL count, or this smoke would pass by silencing
// everything. Reports [1,0,0,255] when both halves hold.
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

// Sum of every coverage counter. Any single token leaking into any counter moves this.
static uint32_t total() {
  dx8wasm_coverage c{};
  dx8wasm_get_coverage(&c);
  return c.unhandled_render_states + c.unhandled_texture_stage_ops +
         c.unhandled_formats + c.unhandled_texture_stage_states;
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

  // --- Accepted without acting: no counter may move. ---
  const uint32_t before = total();
  // D3DFILL_SOLID is what this backend already draws, so accepting it is exact, not a fallback.
  dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
  if (total() != before) { report_error("D3DFILL_SOLID was counted as unhandled"); return 1; }

  // Anisotropy is a sampler parameter with a real GL mapping (EXT_texture_filter_anisotropic),
  // clamped to 1 when the extension is absent — either way it is handled, never a fallback.
  dev->SetTextureStageState(0, D3DTSS_MAXANISOTROPY, 4);
  if (total() != before) { report_error("D3DTSS_MAXANISOTROPY was counted as unhandled"); return 1; }

  // --- Genuinely unimplemented: the counter MUST move. ---
  // GLES3 has no glPolygonMode, so wireframe cannot be expressed and must keep reporting.
  dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
  if (total() != before + 1) { report_error("D3DFILL_WIREFRAME stopped being reported"); return 1; }

  // Rendering must still work after both.
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
