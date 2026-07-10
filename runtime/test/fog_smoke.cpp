// SPDX-License-Identifier: GPL-3.0-only
// Drives the 3.5 linear-fog path. An unlit red quad sits at eye-depth z = 0.5
// with fog range [start=0, end=1], so the fog factor is (1-0.5)/(1-0) = 0.5 —
// the pixel blends halfway between the red surface and a blue fog colour:
//   mix(blue, red, 0.5) = (0.5, 0, 0.5) -> [128,0,128,255].
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z; D3DCOLOR c; };

static DWORD as_dword(float f) { DWORD d; std::memcpy(&d, &f, sizeof d); return d; }   // float -> D3DRS DWORD

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }
  D3DMATRIX id{}; id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
  dev->SetTransform(D3DTS_WORLD, &id); dev->SetTransform(D3DTS_VIEW, &id); dev->SetTransform(D3DTS_PROJECTION, &id);
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);

  // Red quad at eye-depth 0.5.
  V v[4] = {
    {-1, -1, 0.5f, 0xFFFF0000u}, {1, -1, 0.5f, 0xFFFF0000u},
    { 1,  1, 0.5f, 0xFFFF0000u}, {-1, 1, 0.5f, 0xFFFF0000u},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  dev->CreateVertexBuffer(sizeof v, 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb);
  dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();
  dev->SetStreamSource(0, vb, sizeof(V));
  dev->SetIndices(ib, 0);

  dev->SetRenderState(D3DRS_FOGENABLE, 1);
  dev->SetRenderState(D3DRS_FOGCOLOR, 0x000000FFu);   // blue fog
  dev->SetRenderState(D3DRS_FOGSTART, as_dword(0.0f));
  dev->SetRenderState(D3DRS_FOGEND, as_dword(1.0f));

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  unsigned char px[4] = {0};
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [128,0,128,255]

  ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
