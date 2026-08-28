// SPDX-License-Identifier: GPL-3.0-only
// Pins the D3D pixel-centre convention on the pre-transformed (D3DFVF_XYZRHW) path.
//
// In D3D8/9 a pixel's centre is at integer screen coordinates: pixel 10 spans x in [9.5, 10.5).
// A one-pixel quad drawn from 9.5 to 10.5 therefore covers pixel 10 and nothing else. In GL the
// centre of pixel i is i+0.5, so the same vertices handed to GL unchanged cover pixel 9 instead
// (its centre, 9.5, is the quad's inclusive left edge) and leave pixel 10 dark. The fixed-function
// RHW vertex shader translates by +0.5 px to restore D3D's mapping; this smoke fails without it.
//
// Why it matters beyond coverage: Generals' Render2DClass pre-offsets all 2D geometry by
// (-0.5, -0.5) for exactly this convention, so texel centres land on pixel centres. Without the
// translation every UI sample sits on a texel BOUNDARY -- linear filtering blurs, and each edge of
// a tiled atlas piece bleeds its neighbour: regular hairlines across every menu button.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z, rhw; D3DCOLOR c; };   // FVF: XYZRHW | DIFFUSE, stride 20

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 16; pp.BackBufferHeight = 16;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }
  dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

  // One D3D pixel: x and y in [9.5, 10.5) -> pixel (10, 10) under D3D's convention.
  const D3DCOLOR green = 0xFF00FF00u;
  V v[4] = {
    {9.5f, 9.5f, 0, 1, green}, {10.5f, 9.5f, 0, 1, green}, {10.5f, 10.5f, 0, 1, green}, {9.5f, 10.5f, 0, 1, green},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  dev->CreateVertexBuffer(sizeof v, 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb);
  dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();
  dev->SetStreamSource(0, vb, sizeof(V));
  dev->SetIndices(ib, 0);

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  // glReadPixels is bottom-left origin: D3D row y maps to GL row (H-1-y).
  const int H = 16;
  unsigned char hit[4] = {0}, left[4] = {0}, right[4] = {0}, up[4] = {0}, down[4] = {0};
  glReadPixels(10, H - 1 - 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, hit);     // the D3D pixel -> green
  glReadPixels(9,  H - 1 - 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);    // GL-convention victim -> black
  glReadPixels(11, H - 1 - 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
  glReadPixels(10, H - 1 - 9,  1, 1, GL_RGBA, GL_UNSIGNED_BYTE, up);      // D3D row 9 -> black
  glReadPixels(10, H - 1 - 11, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, down);
  const unsigned char* dark[4] = { left, right, up, down };
  const char* names[4] = { "pixel 9,10 (GL-convention victim)", "pixel 11,10", "pixel 10,9", "pixel 10,11" };
  for (int i = 0; i < 4; i++)
    if (dark[i][1] > 3) { report_error(names[i]); return 1; }
  report_pixel(hit[0], hit[1], hit[2], hit[3]);   // expect [0,255,0,255]

  ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
