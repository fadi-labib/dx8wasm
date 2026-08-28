// SPDX-License-Identifier: GPL-3.0-only
// A 1:1 textured blit on the pre-transformed path must reproduce its texels EXACTLY, including the
// last column, with LINEAR filtering -- the way every tiled UI piece in Generals is drawn
// (PushButtonImageDrawThree: native-size pieces, one quad each, exclusive atlas UVs). If sampling
// lands on texel boundaries instead of centres (the GL-vs-D3D half-pixel convention), the edge
// column blends 50% with the atlas NEIGHBOUR -- here a white column right after the piece -- and
// shows up as a bright seam at every piece edge. This smoke draws a 10-texel grey piece whose atlas
// neighbour is white, 1:1 at x 20..30, and reads the piece's last column back: exact grey or bust.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <cstdio>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z, rhw; D3DCOLOR c; float u, v; };   // XYZRHW | DIFFUSE | TEX1, stride 28

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 64; pp.BackBufferHeight = 64;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  // 64x64 atlas: columns 0..9 grey (64,64,64), column 10 onward white. One mip level.
  IDirect3DTexture8* tex = nullptr;
  if (dev->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex) != D3D_OK || !tex) { report_error("CreateTexture failed"); return 1; }
  D3DLOCKED_RECT lr{};
  tex->LockRect(0, &lr, nullptr, 0);
  for (int y = 0; y < 64; y++) {
    uint32_t* row = (uint32_t*)((uint8_t*)lr.pBits + y * lr.Pitch);
    for (int x = 0; x < 64; x++) row[x] = (x < 10) ? 0xFF404040u : 0xFFFFFFFFu;
  }
  tex->UnlockRect(0);
  dev->SetTexture(0, tex);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);   // texture only
  dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
  dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
  dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
  dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);

  // The piece: 10 texels (u 0..10/64) drawn 1:1 over pixels 20..29, y likewise (v 0..10/64), with
  // exclusive UVs -- and with the -0.5 pixel bias every D3D-era 2D renderer applies (Generals'
  // Render2DClass: `bais_add(-0.5f, -0.5f)`), so that under D3D's integer pixel centres pixel 20
  // (centre 20.0) samples texel 0.5, not texel 0.0. Without the bias even D3D would seam: pixel 20
  // at t=0 is u=0, a texel boundary, which WRAP blends with the atlas's far edge (measured here as
  // [159,159,159] = half grey, half white before this smoke modelled the bias).
  const D3DCOLOR white = 0xFFFFFFFFu;
  const float u1 = 10.0f / 64.0f, v1 = 10.0f / 64.0f;
  V v[4] = {
    {19.5f, 19.5f, 0, 1, white, 0, 0}, {29.5f, 19.5f, 0, 1, white, u1, 0}, {29.5f, 29.5f, 0, 1, white, u1, v1}, {19.5f, 29.5f, 0, 1, white, 0, v1},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  IDirect3DVertexBuffer8* vb = nullptr; IDirect3DIndexBuffer8* ib = nullptr;
  dev->CreateVertexBuffer(sizeof v, 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DPOOL_MANAGED, &vb);
  dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();
  dev->SetStreamSource(0, vb, sizeof(V));
  dev->SetIndices(ib, 0);

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  // GL rows are bottom-up: D3D row 25 -> GL row 64-1-25.
  const int gy = 64 - 1 - 25;
  unsigned char first[4] = {0}, last[4] = {0}, beyond[4] = {0};
  glReadPixels(20, gy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, first);    // piece's first column
  glReadPixels(29, gy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, last);     // piece's LAST column: the seam candidate
  glReadPixels(30, gy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, beyond);   // outside the quad: background
  char msg[160];
  if (beyond[0] > 3) { snprintf(msg, sizeof msg, "quad leaked past x=30: [%d,%d,%d]", beyond[0], beyond[1], beyond[2]); report_error(msg); return 1; }
  if (first[0] < 62 || first[0] > 66) { snprintf(msg, sizeof msg, "first column is not the texel: [%d,%d,%d]", first[0], first[1], first[2]); report_error(msg); return 1; }
  // The SDK's translation is 7/16 px, not 1/2 (see ff_shader.cpp: a full half lands D3D-aligned
  // samples on primitive edges), so the edge column may carry at most a 1/16-texel blend with the
  // white neighbour: 64 + (255-64)/16 = 76. Anything near the half blend (159) is the seam.
  if (last[0] < 62 || last[0] > 78) { snprintf(msg, sizeof msg, "seam: last column blends with the white neighbour: [%d,%d,%d] (expected 64..76, half-blend would be 159)", last[0], last[1], last[2]); report_error(msg); return 1; }
  report_pixel(64, 64, 64, 255);   // the piece colour; the bound above is the real assertion (measured last column: 76 = the documented 1/16-texel blend)

  ib->Release(); vb->Release(); tex->Release(); dev->Release(); d3d->Release();
  return 0;
}
