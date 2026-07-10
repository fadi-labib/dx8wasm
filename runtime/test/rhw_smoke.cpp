// SPDX-License-Identifier: GPL-3.0-only
// Drives the 3.7 pre-transformed (D3DFVF_XYZRHW) path — screen-space vertices,
// no transform pipeline, as UI/HUD uses. On a 4x4 viewport the quad covers screen
// x in [2,4] (the right half), full height. Verifies the screen->clip mapping:
// the right-half pixel is the quad colour, the left-half pixel stays background.
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
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }
  dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

  // Screen-space quad: right half (x 2..4), full height (y 0..4), green.
  const D3DCOLOR green = 0xFF00FF00u;
  V v[4] = {
    {2, 0, 0, 1, green}, {4, 0, 0, 1, green}, {4, 4, 0, 1, green}, {2, 4, 0, 1, green},
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

  unsigned char inR[4] = {0}, outL[4] = {0};
  glReadPixels(3, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, inR);    // right half -> green
  glReadPixels(0, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, outL);   // left half -> background
  if (outL[0] > 3 || outL[1] > 3 || outL[2] > 3) { report_error("rhw: quad leaked into the left half"); return 1; }
  report_pixel(inR[0], inR[1], inR[2], inR[3]);   // expect [0,255,0,255]

  ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
