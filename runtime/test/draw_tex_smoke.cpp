// SPDX-License-Identifier: GPL-3.0-only
// Drives the 2.4 texture path: upload an A8R8G8B8 texture, bind it, draw a
// TEX1 quad with D3DTOP_MODULATE. Diffuse and texel are both non-identity in
// some channel, so a correct readback proves BOTH sampling and the multiply.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct Vertex { float x, y, z; D3DCOLOR c; float u, v; };   // XYZ|DIFFUSE|TEX1, stride 24

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
  const D3DCOLOR kDiffuse = 0xFF80FFFFu;   // RGBA (128,255,255,255) -> (0.502,1,1,1)
  const D3DCOLOR kTexel   = 0xFFFF8040u;   // RGBA (255,128,64,255)  -> (1,0.502,0.251,1)
  // MODULATE product -> (128,128,64,255).
  Vertex verts[4] = {
    {-1, -1, 0, kDiffuse, 0, 0}, {1, -1, 0, kDiffuse, 1, 0},
    { 1,  1, 0, kDiffuse, 1, 1}, {-1, 1, 0, kDiffuse, 0, 1},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};

  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  IDirect3DTexture8* tex = nullptr;
  if (dev->CreateVertexBuffer(sizeof verts, 0, kFVF, D3DPOOL_MANAGED, &vb) != D3D_OK ||
      dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib) != D3D_OK ||
      dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex) != D3D_OK) {
    report_error("resource creation failed"); return 1;
  }
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof verts, &dst, 0); std::memcpy(dst, verts, sizeof verts); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0);   std::memcpy(dst, idx, sizeof idx);     ib->Unlock();
  // Fill the 2x2 texture with a single solid texel (proves upload for w>1; the
  // readback stays deterministic regardless of which texel is sampled).
  D3DLOCKED_RECT lr{};
  tex->LockRect(0, &lr, nullptr, 0);
  for (int i = 0; i < 4; i++) std::memcpy((BYTE*)lr.pBits + i * 4, &kTexel, 4);
  tex->UnlockRect(0);

  D3DMATRIX id{}; id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
  dev->SetTransform(D3DTS_WORLD, &id);
  dev->SetTransform(D3DTS_VIEW, &id);
  dev->SetTransform(D3DTS_PROJECTION, &id);
  dev->SetStreamSource(0, vb, sizeof(Vertex));
  dev->SetIndices(ib, 0);
  dev->SetVertexShader(kFVF);
  dev->SetTexture(0, tex);
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);
  if (dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2) != D3D_OK) {
    report_error("DrawIndexedPrimitive failed"); return 1;
  }
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  unsigned char px[4] = {0};
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);

  tex->Release(); ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
