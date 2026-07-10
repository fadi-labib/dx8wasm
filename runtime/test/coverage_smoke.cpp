// SPDX-License-Identifier: GPL-3.0-only
// Drives the 2.5 coverage/fallback layer: feed the device three unhandled tokens
// (a render state, a texture-stage op, a texture format), then assert the
// contract counters incremented, the callback fired once per distinct item, and
// rendering still works. Reports [renderStates, textureOps, formats, cbCount].
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <emscripten.h>
#include <GLES3/gl3.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

static int g_cbCount = 0;
static void on_unhandled(const char*, uint32_t, void*) { g_cbCount++; }

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

  // Three distinct unhandled tokens. D3DRS_FOGENABLE stays unimplemented past
  // 2.6, so this smoke is stable as later slices implement other states.
  dev->SetRenderState(D3DRS_FOGENABLE, 1);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_ADD);   // unsupported op -> fallback
  IDirect3DTexture8* tex = nullptr;
  dev->CreateTexture(2, 2, 1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, &tex);   // unsupported format

  dx8wasm_coverage cov{};
  dx8wasm_get_coverage(&cov);
  if (cov.unhandled_render_states != 1 || cov.unhandled_texture_stage_ops != 1 ||
      cov.unhandled_formats != 1 || cov.fallbacks_taken != 3 || g_cbCount != 3) {
    report_error("coverage counters wrong"); return 1;
  }

  // Rendering must continue despite the unhandled state — clear and read back.
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);
  unsigned char px[4] = {0};
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  if (px[0] != 51 || px[1] != 102 || px[2] != 204) { report_error("rendering stopped after fallback"); return 1; }

  report_pixel(cov.unhandled_render_states, cov.unhandled_texture_stage_ops, cov.unhandled_formats, g_cbCount);
  if (tex) tex->Release();
  dev->Release(); d3d->Release();
  return 0;
}
