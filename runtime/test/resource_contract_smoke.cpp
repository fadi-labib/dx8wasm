// SPDX-License-Identifier: GPL-3.0-only
// Three resource-contract fixes the review of 2026-08-29 flagged as untested, each of which was a
// real gap in device.cpp:
//   * a DXT texture-level surface (GetSurfaceLevel -> Surface8::LockRect) reports a BLOCK-row pitch,
//     not width*4, and its buffer is block-sized -- so a sub-rect/CopyRects DXT consumer can't
//     overflow (the engine's own DDS loader only did full-surface locks, which is why this was
//     latent, but any new consumer would have corrupted the heap);
//   * an out-of-range vertex/index-buffer Lock refuses with D3DERR_INVALIDCALL instead of handing
//     back a pointer past the staging vector;
//   * GetTexture honours its Stage argument (was ignored, always returned stage 0).
// Reports the sentinel [1,0,0,255] when all three hold.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <cstring>
#include <cstdio>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  // ---- 1. DXT surface pitch is block-based, not width*4 ----
  IDirect3DTexture8* dxt = nullptr;
  if (dev->CreateTexture(64, 64, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &dxt) != D3D_OK || !dxt) {
    report_error("CreateTexture(DXT1) failed"); return 1;
  }
  IDirect3DSurface8* surf = nullptr;
  if (dxt->GetSurfaceLevel(0, &surf) != D3D_OK || !surf) { report_error("GetSurfaceLevel failed"); return 1; }
  D3DLOCKED_RECT lr{};
  if (surf->LockRect(&lr, nullptr, 0) != D3D_OK) { report_error("Surface LockRect failed"); return 1; }
  const int expectPitch = ((64 + 3) / 4) * 8;   // DXT1: 16 blocks/row * 8 bytes = 128, NOT 64*4=256
  if (lr.Pitch != expectPitch) {
    char m[96]; std::snprintf(m, sizeof m, "DXT surface pitch %d, expected %d (block-based)", lr.Pitch, expectPitch);
    report_error(m); return 1;
  }
  surf->UnlockRect();
  surf->Release();
  dxt->Release();

  // ---- 2. Out-of-range buffer Lock refuses ----
  IDirect3DVertexBuffer8* vb = nullptr;
  if (dev->CreateVertexBuffer(256, 0, 0, D3DPOOL_MANAGED, &vb) != D3D_OK || !vb) { report_error("CreateVertexBuffer failed"); return 1; }
  BYTE* ptr = nullptr;
  if (vb->Lock(300, 4, &ptr, 0) == D3D_OK) { report_error("out-of-range Lock (off past end) claimed success"); return 1; }
  if (vb->Lock(200, 100, &ptr, 0) == D3D_OK) { report_error("out-of-range Lock (off+size past end) claimed success"); return 1; }
  if (vb->Lock(0, 256, &ptr, 0) != D3D_OK || !ptr) { report_error("in-range full Lock was wrongly refused"); return 1; }
  vb->Unlock();
  vb->Release();

  // ---- 3. GetTexture honours Stage ----
  IDirect3DTexture8 *t0 = nullptr, *t1 = nullptr;
  dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t0);
  dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t1);
  dev->SetTexture(0, t0);
  dev->SetTexture(1, t1);
  IDirect3DBaseTexture8* got = nullptr;
  dev->GetTexture(1, &got);
  bool stageOk = (got == t1);
  if (got) got->Release();
  dev->SetTexture(0, nullptr); dev->SetTexture(1, nullptr);
  t0->Release(); t1->Release();
  if (!stageOk) { report_error("GetTexture(1) returned stage 0's texture"); return 1; }

  report_pixel(1, 0, 0, 255);
  dev->Release(); d3d->Release();
  return 0;
}
