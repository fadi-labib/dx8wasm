// SPDX-License-Identifier: GPL-3.0-only
// Texture upload via the D3D8 SURFACE path — the route GeneralsX's TextureClass
// uses (dx8wasm's own Texture8::LockRect is never called by the engine). Exercises
// CreateImageSurface -> Surface8::LockRect/UnlockRect -> Texture8::GetSurfaceLevel
// -> CopyRects -> GL upload. A white vertex color MODULATE'd with a red texel via
// the surface path yields red: [255,0,0,255].
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z; D3DCOLOR diffuse; float u, v; };   // XYZ | DIFFUSE | TEX1, stride 24

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
  dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex);
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();

  // Fill a standalone image surface with red, then CopyRects it into the texture's
  // level-0 surface (which uploads to GL). This is the surface upload path.
  const D3DCOLOR red = 0xFFFF0000u;   // A=FF R=FF G=00 B=00
  IDirect3DSurface8* img = nullptr;
  if (dev->CreateImageSurface(2, 2, D3DFMT_A8R8G8B8, &img) != D3D_OK || !img) { report_error("CreateImageSurface failed"); return 1; }
  D3DLOCKED_RECT lr{};
  if (img->LockRect(&lr, nullptr, 0) != D3D_OK) { report_error("Surface LockRect failed"); return 1; }
  for (int i = 0; i < 4; i++) std::memcpy((BYTE*)lr.pBits + i * 4, &red, 4);
  img->UnlockRect();

  IDirect3DSurface8* texSurf = nullptr;
  if (tex->GetSurfaceLevel(0, &texSurf) != D3D_OK || !texSurf) { report_error("GetSurfaceLevel failed"); return 1; }
  D3DSURFACE_DESC desc{};
  if (texSurf->GetDesc(&desc) != D3D_OK || desc.Width != 2 || desc.Height != 2) { report_error("surface GetDesc wrong"); return 1; }
  if (dev->CopyRects(img, nullptr, 0, texSurf, nullptr) != D3D_OK) { report_error("CopyRects failed"); return 1; }
  texSurf->Release(); img->Release();

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
