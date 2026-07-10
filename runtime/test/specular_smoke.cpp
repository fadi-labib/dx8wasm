// SPDX-License-Identifier: GPL-3.0-only
// Drives the 3.6 Blinn specular path in isolation. A head-on directional light
// (L = +Z) over a +Z-facing quad makes the half-vector H = normalize(L + V) with
// V = +Z equal the normal, so N·H = 1 and pow(1, power) = 1. Diffuse is zeroed
// (light + material diffuse both 0), leaving pure specular:
//   matSpecular(1) * lightSpecular(0.8) = 0.8 -> [204,204,204,255].
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
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
  mat.Diffuse = {0, 0, 0, 1};   // no diffuse; alpha 1
  mat.Specular = {1, 1, 1, 1};
  mat.Power = 10.0f;
  dev->SetMaterial(&mat);

  D3DLIGHT8 light{};
  light.Type = D3DLIGHT_DIRECTIONAL;
  light.Diffuse = {0, 0, 0, 0};
  light.Specular = {0.8f, 0.8f, 0.8f, 1.0f};
  light.Direction = {0, 0, -1};   // L = +Z, head-on
  dev->SetLight(0, &light);
  dev->LightEnable(0, 1);
  dev->SetRenderState(D3DRS_LIGHTING, 1);
  dev->SetRenderState(D3DRS_SPECULARENABLE, 1);

  V v[4] = {
    {-1, -1, 0, 0, 0, 1}, {1, -1, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 1}, {-1, 1, 0, 0, 0, 1},
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

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  unsigned char px[4] = {0};
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [204,204,204,255]

  ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
