// SPDX-License-Identifier: GPL-3.0-only
// Verifies D3D8 material color sources (D3DRS_*MATERIALSOURCE) in the lit path.
// The material diffuse is WHITE and the vertex diffuse is RED, lit by a white
// directional light at N·L = 1:
//   COLOR1 (default) scene — diffuse must come from the vertex -> RED [255,0,0]
//                            (the over-bright bug rendered this white)
//   MATERIAL scene         — diffuse must come from the material -> WHITE, asserted
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z, nx, ny, nz; DWORD color; };   // FVF: XYZ | NORMAL | DIFFUSE, stride 28

static IDirect3DDevice8* g_dev = nullptr;

static void quad(DWORD color) {   // front-facing full-viewport quad (normal +Z), given vertex color
  V v[4] = {
    {-1, -1, 0, 0, 0, 1, color}, {1, -1, 0, 0, 0, 1, color},
    { 1,  1, 0, 0, 0, 1, color}, {-1, 1, 0, 0, 0, 1, color},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  g_dev->CreateVertexBuffer(sizeof v, 0, D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb);
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
  g_dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE);

  // Material diffuse WHITE (as Generals leaves it); vertex diffuse drives shading.
  D3DMATERIAL8 mat{};
  mat.Diffuse = {1, 1, 1, 1}; mat.Ambient = {0, 0, 0, 0}; mat.Emissive = {0, 0, 0, 0};
  g_dev->SetMaterial(&mat);

  D3DLIGHT8 light{};
  light.Type = D3DLIGHT_DIRECTIONAL;
  light.Diffuse = {1, 1, 1, 1};
  light.Ambient = {0, 0, 0, 0};
  light.Direction = {0, 0, -1};   // travels -Z; vector to light is +Z, so N·L = 1 for the +Z quad
  g_dev->SetLight(0, &light);
  g_dev->LightEnable(0, 1);
  g_dev->SetRenderState(D3DRS_LIGHTING, 1);
  g_dev->SetRenderState(D3DRS_AMBIENT, 0x00000000u);

  unsigned char px[4];
  const DWORD RED = 0xFFFF0000u;   // A=FF R=FF G=00 B=00

  // MATERIAL scene: force diffuse from the material -> white * white light = white.
  g_dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  quad(RED);
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  if (!near3(px, 255, 255, 255)) { report_error("matsource: MATERIAL diffuse should be white, not vertex"); return 1; }

  // COLOR1 scene (D3D8 default): diffuse from the vertex -> red. This is the fix;
  // before it, the lit path multiplied by the white material and returned white.
  g_dev->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  quad(RED);
  g_dev->Present(nullptr, nullptr, nullptr, nullptr);
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [255,0,0,255]

  g_dev->Release(); d3d->Release();
  return 0;
}
