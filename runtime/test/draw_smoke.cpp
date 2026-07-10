// SPDX-License-Identifier: GPL-3.0-only
// Drives the 2.3 draw path: upload an FVF quad + indices, DrawIndexedPrimitive,
// read back. Asserts the quad color at both a corner and the center (proves the
// triangles actually fill the viewport, not just that a clear happened).
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct Vertex { float x, y, z; D3DCOLOR c; };   // FVF: XYZ | DIFFUSE, stride 16

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  // Full-viewport quad in NDC, flat green 0xFF33CC66 -> RGBA [51,204,102,255].
  const D3DCOLOR kGreen = 0xFF33CC66u;
  Vertex verts[4] = {
    {-1, -1, 0, kGreen}, {1, -1, 0, kGreen}, {1, 1, 0, kGreen}, {-1, 1, 0, kGreen},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};

  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  if (dev->CreateVertexBuffer(sizeof verts, 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb) != D3D_OK ||
      dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib) != D3D_OK) {
    report_error("buffer creation failed"); return 1;
  }
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof verts, &dst, 0); std::memcpy(dst, verts, sizeof verts); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0);   std::memcpy(dst, idx, sizeof idx);     ib->Unlock();

  D3DMATRIX id{}; id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
  dev->SetTransform(D3DTS_WORLD, &id);
  dev->SetTransform(D3DTS_VIEW, &id);
  dev->SetTransform(D3DTS_PROJECTION, &id);
  dev->SetStreamSource(0, vb, sizeof(Vertex));
  dev->SetIndices(ib, 0);
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu /* distinct blue background */, 1.0f, 0);
  if (dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2) != D3D_OK) {
    report_error("DrawIndexedPrimitive failed"); return 1;
  }
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  unsigned char center[4] = {0}, corner[4] = {0};
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, center);
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
  for (int i = 0; i < 4; i++) {
    if (std::abs(center[i] - corner[i]) > 2) { report_error("corner/center mismatch — quad did not fill"); return 1; }
  }
  report_pixel(center[0], center[1], center[2], center[3]);

  ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
