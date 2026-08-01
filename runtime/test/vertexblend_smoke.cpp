// SPDX-License-Identifier: GPL-3.0-only
// Vertex blending (D3DFVF_XYZB1-5) had no instrument at all: it rides the FVF position bits,
// not a D3DRS_*/D3DTSS_*/D3DTOP_*/D3DFMT_* token, so its absence from a capture proved nothing
// either way (docs/measured-gap.json says exactly this). Worse, bind_pipeline would bind such a
// position as 3 floats and silently mis-read every vertex.
//
// This proves two separate things, and deliberately keeps them separate:
//   1. Occurrence counting: `unhandled_vertex_formats` bumps once per SetVertexShader call
//      with a blended position, and not at all for the two ordinary position types.
//   2. Distinct-token attribution: the unhandled callback (which coverage.cpp's note() fires
//      once per distinct (family, value) pair via its g_seen dedup) must fire exactly twice for
//      the three blended calls below, because XYZB1|DIFFUSE and XYZB1|NORMAL|TEX1 share one
//      position-mask key (0x6) while XYZB3 is a second (0xa). If the instrument ever regressed
//      to keying on the whole FVF DWORD instead of the position mask, this would report 3
//      distinct tokens instead of 2 — the occurrence count alone (which would still read 3
//      either way) cannot catch that regression, which is exactly why both assertions are here
//      rather than just one standing in for the other.
// Reports [1,0,0,255].
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <cstring>
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

// Counts only D3DFVF_* callback firings, so an unrelated token firing earlier/later in the
// process (or in a future revision of this smoke) can never be mistaken for one of these.
static int g_fvfCbCount = 0;
static void on_unhandled(const char* kind, uint32_t, void*) {
  if (std::strcmp(kind, "D3DFVF") == 0) ++g_fvfCbCount;
}

static uint32_t blends() {
  dx8wasm_coverage c{};
  dx8wasm_get_coverage(&c);
  return c.unhandled_vertex_formats;
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

  // The two position types this backend implements must not count, and must not be reported
  // as a distinct unhandled token either.
  const uint32_t base = blends();
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
  dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  if (blends() != base) { report_error("an implemented position type was counted as blended"); return 1; }
  if (g_fvfCbCount != 0) { report_error("an implemented position type fired the unhandled callback"); return 1; }

  // Each blended width must count as an occurrence. Keyed on the position mask, so XYZB1 with
  // and without a normal is one token, not two — five possible keys total, not one per FVF the
  // engine builds.
  dev->SetVertexShader(D3DFVF_XYZB1 | D3DFVF_DIFFUSE);
  dev->SetVertexShader(D3DFVF_XYZB1 | D3DFVF_NORMAL | D3DFVF_TEX1);
  dev->SetVertexShader(D3DFVF_XYZB3 | D3DFVF_DIFFUSE);
  if (blends() != base + 3) { report_error("blended FVFs were not counted once per occurrence"); return 1; }

  // Distinct-token attribution: three calls, but only two distinct position-mask keys (0x6 for
  // both XYZB1 variants, 0xa for XYZB3). A full-FVF key would instead fire the callback 3 times
  // here, since XYZB1|DIFFUSE and XYZB1|NORMAL|TEX1 are different DWORDs.
  if (g_fvfCbCount != 2) { report_error("blended FVFs were not attributed to the position-mask key"); return 1; }

  dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
