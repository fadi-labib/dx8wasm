// SPDX-License-Identifier: GPL-3.0-only
// Drives the 3.4 spot-light cone. Same quad (±0.5, normal +Z) and light position
// (0,0,2) in both scenes; only the spot aim changes:
//   aimed away  — quad is outside the outer cone -> spotAtten 0 -> black
//   aimed at it — quad is inside the inner cone  -> spotAtten 1 -> lit
// Inner Theta=1.0, outer Phi=2.0 rad; on-axis rho=0.9428 > cos(0.5)=0.878, so the
// quad sits fully inside the inner cone. Lit value = diffuse * hitDot(0.9428).
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z, nx, ny, nz; };   // FVF: XYZ | NORMAL

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
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_NORMAL);

  D3DMATERIAL8 mat{};
  mat.Diffuse = {1, 1, 1, 1}; mat.Ambient = {0, 0, 0, 0}; mat.Emissive = {0, 0, 0, 0};
  dev->SetMaterial(&mat);

  D3DLIGHT8 light{};
  light.Type = D3DLIGHT_SPOT;
  light.Diffuse = {0.6f, 0.4f, 0.2f, 1.0f};
  light.Position = {0, 0, 2};
  light.Range = 100.0f;
  light.Attenuation0 = 1.0f;   // no distance falloff (atten_dist = 1)
  light.Theta = 1.0f; light.Phi = 2.0f; light.Falloff = 1.0f;
  dev->LightEnable(0, 1);
  dev->SetRenderState(D3DRS_LIGHTING, 1);

  V v[4] = {
    {-0.5f, -0.5f, 0, 0, 0, 1}, {0.5f, -0.5f, 0, 0, 0, 1}, {0.5f, 0.5f, 0, 0, 0, 1}, {-0.5f, 0.5f, 0, 0, 0, 1},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  dev->CreateVertexBuffer(sizeof v, 0, D3DFVF_XYZ | D3DFVF_NORMAL, D3DPOOL_MANAGED, &vb);
  dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();
  dev->SetStreamSource(0, vb, sizeof(V));
  dev->SetIndices(ib, 0);
  unsigned char px[4];

  // Scene 1 — aim the spot away (-Y): quad is outside the cone -> dark.
  light.Direction = {0, -1, 0};
  dev->SetLight(0, &light);
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  if (px[0] > 3 || px[1] > 3 || px[2] > 3) { report_error("spot: outside-cone quad was lit"); return 1; }

  // Scene 2 — aim the spot at the quad (-Z): inside the inner cone -> lit.
  light.Direction = {0, 0, -1};
  dev->SetLight(0, &light);
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  dev->Present(nullptr, nullptr, nullptr, nullptr);
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect diffuse*0.9428 = [144,96,48,255]

  ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
