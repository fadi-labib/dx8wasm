// SPDX-License-Identifier: GPL-3.0-only
// A D3DFMT_INDEX32 index buffer drawn through DrawIndexedPrimitive must be read as 32-bit
// indices. The buffer path hard-coded GL_UNSIGNED_SHORT (only the user-pointer path,
// DrawIndexedPrimitiveUP, honoured the format), so every 32-bit index was read as two 16-bit
// ones: {0,1,2,0,2,3} became {0,0,1,0,2,0} -- degenerate triangles, nothing drawn, and no
// coverage count. This backend's caps advertise MaxVertexIndex 0xFFFFF, so a consumer is entitled
// to use 32-bit indices. A full-canvas green quad indexed with them must light the centre pixel.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <cstdint>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct V { float x, y, z, rhw; D3DCOLOR c; };   // XYZRHW | DIFFUSE, stride 20

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }
  dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

  const D3DCOLOR green = 0xFF00FF00u;
  V v[4] = { {0, 0, 0, 1, green}, {4, 0, 0, 1, green}, {4, 4, 0, 1, green}, {0, 4, 0, 1, green} };
  uint32_t idx[6] = {0, 1, 2, 0, 2, 3};   // read as u16 pairs this is {0,0,1,0,2,0}: two degenerate triangles
  IDirect3DVertexBuffer8* vb = nullptr;
  IDirect3DIndexBuffer8* ib = nullptr;
  if (dev->CreateVertexBuffer(sizeof v, 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb) != D3D_OK || !vb) { report_error("CreateVertexBuffer failed"); return 1; }
  if (dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX32, D3DPOOL_MANAGED, &ib) != D3D_OK || !ib) { report_error("CreateIndexBuffer(INDEX32) failed"); return 1; }
  BYTE* dst = nullptr;
  vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); vb->Unlock();
  ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); ib->Unlock();
  dev->SetStreamSource(0, vb, sizeof(V));
  dev->SetIndices(ib, 0);

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF000000u, 1.0f, 0);
  if (dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2) != D3D_OK) { report_error("DrawIndexedPrimitive with INDEX32 refused"); return 1; }
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  unsigned char px[4] = {0};
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);   // expect [0,255,0,255]; the 16-bit misread leaves the clear colour

  ib->Release(); vb->Release(); dev->Release(); d3d->Release();
  return 0;
}
