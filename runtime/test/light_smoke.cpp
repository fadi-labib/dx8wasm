// SPDX-License-Identifier: GPL-3.0-only
// Drives the 3.1 fixed-function lighting path with a single directional light.
//   ambient scene — a back-facing quad (N·L clamps to 0) shows only global
//                    ambient * material ambient; verifies the clamp + ambient term
//   diffuse scene — a front-facing quad (N·L = 1) shows material diffuse * light
//                    diffuse; the harness-asserted pixel [153,102,51,255]
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z, nx, ny, nz; };   // FVF: XYZ | NORMAL, stride 24

static IDirect3DDevice8* g_dev = nullptr;

static void quad(float nz) {   // full-viewport quad with a uniform normal (0,0,nz)
  V v[4] = {
    {-1, -1, 0, 0, 0, nz}, {1, -1, 0, 0, 0, nz}, {1, 1, 0, 0, 0, nz}, {-1, 1, 0, 0, 0, nz},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  g_dev->CreateVertexBuffer(sizeof v, 0, D3DFVF_XYZ | D3DFVF_NORMAL, D3DPOOL_MANAGED, &vb);
  g_dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();
  g_dev->SetStreamSource(0, vb, sizeof(V));
  g_dev->SetIndices(ib, 0);
  g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  ib->Release(); vb->Release();
}

static bool near3(const unsigned char* px, int r, int g, int b) {
  return std::abs(px[0] - r) <= 3 && std::abs(px[1] - g) <= 3 && std::abs(px[2] - b) <= 3;
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev) != D3D_OK || !g_dev) {
    report_error("CreateDevice failed"); return 1;
  }
  D3DMATRIX id{}; id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
  g_dev->SetTransform(D3DTS_WORLD, &id); g_dev->SetTransform(D3DTS_VIEW, &id); g_dev->SetTransform(D3DTS_PROJECTION, &id);
  g_dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_NORMAL);

  D3DMATERIAL8 mat{};
  mat.Diffuse = {1, 1, 1, 1}; mat.Ambient = {1, 1, 1, 1}; mat.Emissive = {0, 0, 0, 0};
  g_dev->SetMaterial(&mat);

  D3DLIGHT8 light{};
  light.Type = D3DLIGHT_DIRECTIONAL;
  light.Diffuse = {0.6f, 0.4f, 0.2f, 1.0f};
  light.Ambient = {0, 0, 0, 0};
  light.Direction = {0, 0, -1};   // travels -Z; vector to light is +Z
  g_dev->SetLight(0, &light);
  g_dev->LightEnable(0, 1);
  g_dev->SetRenderState(D3DRS_LIGHTING, 1);

  unsigned char px[4];

  // Ambient scene: back-facing normal -> N·L = clamp(dot((0,0,-1),(0,0,1)),0,1) = 0.
  g_dev->SetRenderState(D3DRS_AMBIENT, 0x00333333u);   // 0.2 global ambient
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  quad(-1.0f);
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  if (!near3(px, 51, 51, 51)) { report_error("lighting: ambient/back-face N.L wrong"); return 1; }

  // Diffuse scene: front-facing normal -> N·L = 1; no global ambient.
  g_dev->SetRenderState(D3DRS_AMBIENT, 0x00000000u);
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  quad(1.0f);
  g_dev->Present(nullptr, nullptr, nullptr, nullptr);
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [153,102,51,255]

  g_dev->Release(); d3d->Release();
  return 0;
}
