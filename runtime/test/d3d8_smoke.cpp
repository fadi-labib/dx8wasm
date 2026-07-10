// SPDX-License-Identifier: GPL-3.0-only
// Drives the D3D8 path end-to-end and reads back the cleared pixel.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu /* A=FF R=33 G=66 B=CC */, 1.0f, 0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);
  unsigned char px[4] = {0,0,0,0};
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);
  dev->Release(); d3d->Release();
  return 0;
}
