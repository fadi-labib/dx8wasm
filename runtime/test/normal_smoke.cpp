// SPDX-License-Identifier: GPL-3.0-only
// Verifies the lit path transforms the vertex normal by the world matrix (so a
// rotated unit lights correctly). Object normal is +X and the light points +X.
//   rotated scene   — world = 90° about Z rotates the normal to +Y (⊥ light) so
//                     N·L = 0: only ambient shows -> [64,64,64]. Without the world
//                     transform the object normal stays ∥ the light -> full white.
//   identity scene  — world = identity, N·L = 1 -> white (asserted inline)
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z, nx, ny, nz; };   // FVF: XYZ | NORMAL, stride 24

static IDirect3DDevice8* g_dev = nullptr;

static void quad() {   // full-viewport quad, object normal +X on every vertex
  V v[4] = {
    {-1, -1, 0, 1, 0, 0}, {1, -1, 0, 1, 0, 0}, {1, 1, 0, 1, 0, 0}, {-1, 1, 0, 1, 0, 0},
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
  g_dev->SetTransform(D3DTS_VIEW, &id); g_dev->SetTransform(D3DTS_PROJECTION, &id);
  g_dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_NORMAL);
  g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);   // rotated quad may flip winding

  D3DMATERIAL8 mat{};
  mat.Diffuse = {1, 1, 1, 1}; mat.Ambient = {1, 1, 1, 1}; mat.Emissive = {0, 0, 0, 0};
  g_dev->SetMaterial(&mat);

  D3DLIGHT8 light{};
  light.Type = D3DLIGHT_DIRECTIONAL;
  light.Diffuse = {1, 1, 1, 1};
  light.Ambient = {0, 0, 0, 0};
  light.Direction = {-1, 0, 0};   // vector to light = +X; N·L = 1 only if the normal is +X in world space
  g_dev->SetLight(0, &light);
  g_dev->LightEnable(0, 1);
  g_dev->SetRenderState(D3DRS_LIGHTING, 1);
  g_dev->SetRenderState(D3DRS_AMBIENT, 0x00404040u);   // 0.25 global ambient -> shows through when N·L=0

  unsigned char px[4];

  // Identity scene: object normal +X stays +X, parallel to the light -> full white.
  g_dev->SetTransform(D3DTS_WORLD, &id);
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  quad();
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  if (!near3(px, 255, 255, 255)) { report_error("normal: identity-world diffuse should be full white"); return 1; }

  // Rotated scene: world = RotZ(90°) (D3D row-major) rotates the +X normal to +Y.
  // N·L = 0, so only the 0.25 ambient shows -> [64,64,64]. Before the fix the object
  // normal stayed +X (∥ light) and this came out full white.
  D3DMATRIX rot{};
  rot.m[0][0] = 0;  rot.m[0][1] = 1;  // row0 = (0,1,0): n_obj(1,0,0) * M -> (0,1,0)
  rot.m[1][0] = -1; rot.m[1][1] = 0;  // row1 = (-1,0,0)
  rot.m[2][2] = 1;  rot.m[3][3] = 1;
  g_dev->SetTransform(D3DTS_WORLD, &rot);
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  quad();
  g_dev->Present(nullptr, nullptr, nullptr, nullptr);
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [64,64,64,255]

  g_dev->Release(); d3d->Release();
  return 0;
}
