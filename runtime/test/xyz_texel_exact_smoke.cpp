// SPDX-License-Identifier: GPL-3.0-only
// The way Generals' Render2DClass actually draws its UI: plain XYZ vertices already in CLIP space
// (identity world/view/projection), positions derived from pixel coordinates with the D3D -0.5 px
// bias (WW3D::Set_Screen_UV_Bias), exclusive atlas UVs, LINEAR filtering. Not XYZRHW. A 10-texel
// piece drawn 1:1 must reproduce its texels exactly -- above all its LAST column, whose atlas
// neighbour here is fully transparent, exactly like the menu buttons' "Buttons-Middle" piece
// (alpha 147 fill, alpha 0 on both sides). Sampling on the texel boundary instead of the centre
// halves that column's alpha and the background shows through: the hairline at every piece edge
// seen on generals.fadilabib.com (2026-08-28). This smoke fails without the SDK's half-pixel
// clip-space translation; rhw_texel_exact_smoke covers the same thing on the RHW path.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <cstdio>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z; D3DCOLOR c; float u, v; };   // XYZ | DIFFUSE | TEX1, stride 24

static const int W = 64, H = 64;
// Render2DClass::Convert_Vert: pixel -> clip, with the -0.5 bias.
static float cx(float px) { return (px - 0.5f) * (2.0f / W) - 1.0f; }
static float cy(float py) { return 1.0f - (py - 0.5f) * (2.0f / H); }

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = W; pp.BackBufferHeight = H;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  // 64x64 atlas: columns 0..9 opaque grey (64,64,64), everything else the same grey with ALPHA 0
  // -- the Buttons-Middle situation. Alpha blending on, so a half-alpha seam shows the black clear.
  IDirect3DTexture8* tex = nullptr;
  if (dev->CreateTexture(64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex) != D3D_OK || !tex) { report_error("CreateTexture failed"); return 1; }
  D3DLOCKED_RECT lr{};
  tex->LockRect(0, &lr, nullptr, 0);
  for (int y = 0; y < 64; y++) {
    uint32_t* row = (uint32_t*)((uint8_t*)lr.pBits + y * lr.Pitch);
    for (int x = 0; x < 64; x++) row[x] = (x < 10) ? 0xFF404040u : 0x00404040u;
  }
  tex->UnlockRect(0);
  dev->SetTexture(0, tex);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
  dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
  dev->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
  dev->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
  dev->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
  dev->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
  dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  dev->SetRenderState(D3DRS_LIGHTING, 0);
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
  D3DMATRIX id{}; id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
  dev->SetTransform(D3DTS_WORLD, &id); dev->SetTransform(D3DTS_VIEW, &id); dev->SetTransform(D3DTS_PROJECTION, &id);

  // The piece: pixels 20..29 in x and y, exclusive UVs 0..10/64.
  const D3DCOLOR white = 0xFFFFFFFFu;
  const float u1 = 10.0f / 64.0f, v1 = 10.0f / 64.0f;
  V v[4] = {
    {cx(20), cy(20), 0.5f, white, 0, 0}, {cx(30), cy(20), 0.5f, white, u1, 0},
    {cx(30), cy(30), 0.5f, white, u1, v1}, {cx(20), cy(30), 0.5f, white, 0, v1},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  IDirect3DVertexBuffer8* vb = nullptr; IDirect3DIndexBuffer8* ib = nullptr;
  dev->CreateVertexBuffer(sizeof v, 0, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DPOOL_MANAGED, &vb);
  dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();
  dev->SetStreamSource(0, vb, sizeof(V));
  dev->SetIndices(ib, 0);

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  const int gy = H - 1 - 25;   // D3D row 25
  unsigned char first[4] = {0}, last[4] = {0}, beyond[4] = {0};
  glReadPixels(20, gy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, first);
  glReadPixels(29, gy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, last);     // the seam candidate
  glReadPixels(30, gy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, beyond);
  char msg[160];
  if (beyond[0] > 3) { snprintf(msg, sizeof msg, "quad leaked past x=30: [%d,%d,%d]", beyond[0], beyond[1], beyond[2]); report_error(msg); return 1; }
  if (first[0] < 62 || first[0] > 66) { snprintf(msg, sizeof msg, "first column is not the texel: [%d,%d,%d]", first[0], first[1], first[2]); report_error(msg); return 1; }
  // 7/16 px translation (ff_shader.cpp), so the edge column may lose at most 1/16 of its alpha to
  // the transparent neighbour: 64 * 15/16 = 60 over the black clear. The seam is the half-alpha
  // 32, or -- without any translation -- the column not being covered at all (0).
  if (last[0] < 58 || last[0] > 66) { snprintf(msg, sizeof msg, "seam: last column half-alpha over the clear: [%d,%d,%d] (expected 60..64; half-alpha would be 32)", last[0], last[1], last[2]); report_error(msg); return 1; }
  report_pixel(64, 64, 64, 255);   // the piece colour; the bound above is the real assertion

  ib->Release(); vb->Release(); tex->Release(); dev->Release(); d3d->Release();
  return 0;
}
