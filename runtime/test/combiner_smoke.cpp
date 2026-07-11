// SPDX-License-Identifier: GPL-3.0-only
// Drives the 3.9 texture combiner path with D3DTOP_ADD. Texture (0.3,0.2,0.1)
// plus diffuse (0.2,0.3,0.4) sum to (0.5,0.5,0.5) -> [128,128,128]. Under the
// default MODULATE these would multiply to near-black, so the readback proves
// the ADD combiner is actually selected, not the fallback.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z; D3DCOLOR c; float u, v; };   // XYZ | DIFFUSE | TEX1

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  const uint32_t kFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
  const D3DCOLOR kDiffuse = 0xFF334D66u;   // (0.2, 0.3, 0.4)
  const D3DCOLOR kTexel   = 0xFF4D331Au;   // (0.3, 0.2, 0.1)
  V verts[4] = {
    {-1, -1, 0, kDiffuse, 0, 0}, {1, -1, 0, kDiffuse, 1, 0},
    { 1,  1, 0, kDiffuse, 1, 1}, {-1, 1, 0, kDiffuse, 0, 1},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};

  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  IDirect3DTexture8* tex = nullptr;
  dev->CreateVertexBuffer(sizeof verts, 0, kFVF, D3DPOOL_MANAGED, &vb);
  dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
  dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex);
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof verts, &dst, 0); std::memcpy(dst, verts, sizeof verts); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();
  D3DLOCKED_RECT lr{}; tex->LockRect(0, &lr, nullptr, 0);
  for (int i = 0; i < 4; i++) std::memcpy((BYTE*)lr.pBits + i * 4, &kTexel, 4);
  tex->UnlockRect(0);

  dev->SetStreamSource(0, vb, sizeof(V));
  dev->SetIndices(ib, 0);
  dev->SetVertexShader(kFVF);
  dev->SetTexture(0, tex);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_ADD);

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  unsigned char px[4] = {0};
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [128,128,128,255]

  tex->Release(); ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
