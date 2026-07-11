// SPDX-License-Identifier: GPL-3.0-only
// User-pointer draw (DrawPrimitiveUP) — inline vertex data, no vertex/index
// buffer bound. A full-screen magenta quad drawn as a 4-vertex triangle strip
// straight from a stack array. Proves the scratch-buffer UP path. -> [255,0,255,255].
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z; D3DCOLOR c; };   // XYZ | DIFFUSE

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }
  D3DMATRIX id{}; id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
  dev->SetTransform(D3DTS_WORLD, &id); dev->SetTransform(D3DTS_VIEW, &id); dev->SetTransform(D3DTS_PROJECTION, &id);
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);

  const D3DCOLOR m = 0xFFFF00FFu;   // magenta -> [255,0,255]
  V v[4] = { {-1, -1, 0, m}, {1, -1, 0, m}, {-1, 1, 0, m}, {1, 1, 0, m} };   // strip order

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  if (dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(V)) != D3D_OK) { report_error("DrawPrimitiveUP failed"); return 1; }
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  unsigned char px[4] = {0};
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [255,0,255,255]

  dev->Release(); d3d->Release();
  return 0;
}
