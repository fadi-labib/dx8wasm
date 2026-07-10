// SPDX-License-Identifier: GPL-3.0-only
// Drives the 2.6 render-state subset across four sub-scenes in one context:
//   depth  — a far quad must NOT overwrite a nearer one (Z test + write)
//   cull   — a front-facing triangle disappears under D3DCULL_CCW
//   alpha  — a sub-ref-alpha quad is discarded by the in-shader alpha test
//   blend  — a 50%-alpha quad over an opaque background blends (reported pixel)
// Each internal check reports an error on failure; the blend result is the
// harness-asserted pixel [153,51,102,191].
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z; D3DCOLOR c; };   // FVF: XYZ | DIFFUSE

static IDirect3DDevice8* g_dev = nullptr;

// Build ephemeral buffers, bind, draw. Buffers are released after the draw; a
// smoke doesn't need buffer reuse and this keeps each scene self-contained.
static void draw(V* verts, int nv, uint16_t* idx, int ni) {
  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  g_dev->CreateVertexBuffer(nv * sizeof(V), 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb);
  g_dev->CreateIndexBuffer(ni * sizeof(uint16_t), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib);
  BYTE* dst = nullptr;
  vb->Lock(0, nv * sizeof(V), &dst, 0); std::memcpy(dst, verts, nv * sizeof(V)); vb->Unlock();
  ib->Lock(0, ni * sizeof(uint16_t), &dst, 0); std::memcpy(dst, idx, ni * sizeof(uint16_t)); ib->Unlock();
  g_dev->SetStreamSource(0, vb, sizeof(V));
  g_dev->SetIndices(ib, 0);
  g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, nv, 0, ni / 3);
  ib->Release(); vb->Release();
}

static void quad(float z, D3DCOLOR c) {
  V v[4] = {{-1, -1, z, c}, {1, -1, z, c}, {1, 1, z, c}, {-1, 1, z, c}};
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  draw(v, 4, idx, 6);
}

static void center(unsigned char* px) { glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px); }
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
  g_dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
  unsigned char px[4];

  // Scene 1 — depth: near red first, then far green must be rejected.
  g_dev->SetRenderState(D3DRS_ZENABLE, 1);
  g_dev->SetRenderState(D3DRS_ZWRITEENABLE, 1);
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000u, 1.0f, 0);
  quad(-0.5f, 0xFFFF0000u);   // near, red
  quad(0.5f, 0xFF00FF00u);    // far, green — depth test (LEQUAL) rejects it
  center(px);
  if (!near3(px, 255, 0, 0)) { report_error("depth: far quad overwrote near"); return 1; }

  // Scene 2 — cull: a front-facing (CCW) triangle vanishes under D3DCULL_CCW.
  g_dev->SetRenderState(D3DRS_ZENABLE, 0);
  g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  { V t[3] = {{-1, -1, 0, 0xFFFF0000u}, {1, -1, 0, 0xFFFF0000u}, {0, 1, 0, 0xFFFF0000u}};
    uint16_t ti[3] = {0, 1, 2}; draw(t, 3, ti, 3); }
  center(px);
  if (!near3(px, 0, 0, 0)) { report_error("cull: CCW triangle was not culled"); return 1; }

  // Scene 3 — alpha test: alpha 64 < ref 128 with GREATER -> discarded.
  g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, 1);
  g_dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
  g_dev->SetRenderState(D3DRS_ALPHAREF, 128);
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  quad(0.0f, 0x40FF0000u);   // alpha 0x40 = 64 -> fails GREATER 128 -> discard
  center(px);
  if (!near3(px, 0, 0, 0)) { report_error("alpha test: sub-ref quad was not discarded"); return 1; }

  // Scene 4 — alpha blend: 50% red over opaque blue background.
  g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
  g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, 1);
  g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
  g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);   // (51,102,204,255)
  quad(0.0f, 0x80FF0000u);   // (255,0,0) at alpha 0.502
  g_dev->Present(nullptr, nullptr, nullptr, nullptr);
  center(px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [153,51,102,191]

  g_dev->Release(); d3d->Release();
  return 0;
}
