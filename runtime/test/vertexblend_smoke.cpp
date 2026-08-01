// SPDX-License-Identifier: GPL-3.0-only
// Vertex blending (D3DFVF_XYZB1-5) had no instrument at all: it rides the FVF position bits,
// not a D3DRS_*/D3DTSS_*/D3DTOP_*/D3DFMT_* token, so its absence from a capture proved nothing
// either way (docs/measured-gap.json says exactly this). Worse, bind_pipeline would bind such a
// position as 3 floats and silently mis-read every vertex. Assert the blended widths are
// counted and the two ordinary position types are not. Reports [1,0,0,255].
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

static uint32_t blends() {
  dx8wasm_coverage c{};
  dx8wasm_get_coverage(&c);
  return c.unhandled_vertex_formats;
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

  // The two position types this backend implements must not count.
  const uint32_t base = blends();
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
  dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  if (blends() != base) { report_error("an implemented position type was counted as blended"); return 1; }

  // Each blended width must count. Keyed on the position mask, so XYZB1 with and without a
  // normal is one token, not two — five possible keys total, not one per FVF the engine builds.
  dev->SetVertexShader(D3DFVF_XYZB1 | D3DFVF_DIFFUSE);
  dev->SetVertexShader(D3DFVF_XYZB1 | D3DFVF_NORMAL | D3DFVF_TEX1);
  dev->SetVertexShader(D3DFVF_XYZB3 | D3DFVF_DIFFUSE);
  if (blends() != base + 3) { report_error("blended FVFs were not counted once per occurrence"); return 1; }

  dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
