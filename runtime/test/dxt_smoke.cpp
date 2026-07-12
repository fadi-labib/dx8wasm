// SPDX-License-Identifier: GPL-3.0-only
// DXT1 (S3TC) compressed texture decode. Generals' terrain/unit textures are
// DXT-compressed. A 4x4 DXT1 block encoding solid red is uploaded via LockRect,
// decoded on Unlock, and MODULATE'd with a white vertex color -> red [255,0,0,255].
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <cstdint>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z; D3DCOLOR diffuse; float u, v; };   // XYZ | DIFFUSE | TEX1

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }
  const uint32_t kFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
  D3DMATRIX id{}; id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
  dev->SetTransform(D3DTS_WORLD, &id); dev->SetTransform(D3DTS_VIEW, &id); dev->SetTransform(D3DTS_PROJECTION, &id);
  dev->SetVertexShader(kFVF);

  V v[4] = {
    {-1, -1, 0, 0xFFFFFFFFu, 0, 0}, {1, -1, 0, 0xFFFFFFFFu, 1, 0},
    { 1,  1, 0, 0xFFFFFFFFu, 1, 1}, {-1, 1, 0, 0xFFFFFFFFu, 0, 1},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  IDirect3DVertexBuffer8* vb = nullptr; IDirect3DIndexBuffer8* ib = nullptr; IDirect3DTexture8* tex = nullptr;
  dev->CreateVertexBuffer(sizeof v, 0, kFVF, D3DPOOL_MANAGED, &vb);
  dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
  if (dev->CreateTexture(4, 4, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &tex) != D3D_OK || !tex) { report_error("CreateTexture DXT1 failed"); return 1; }
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();

  // 8-byte DXT1 block, solid red: c0=0xF800 (RGB565 red) > c1=0x0000 -> 4-color
  // opaque mode; all 16 index bits 0 -> every texel is color0 (red).
  const BYTE block[8] = { 0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
  D3DLOCKED_RECT lr{}; tex->LockRect(0, &lr, nullptr, 0);
  std::memcpy(lr.pBits, block, sizeof block);
  tex->UnlockRect(0);

  dev->SetStreamSource(0, vb, sizeof(V));
  dev->SetIndices(ib, 0);
  dev->SetTexture(0, tex);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  unsigned char px[4] = {0};
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [255,0,0,255]

  tex->Release(); ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
