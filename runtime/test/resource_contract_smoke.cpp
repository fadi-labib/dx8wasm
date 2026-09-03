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
// Extended 2026-09-03 with the resource-contract gaps the pre-publish review found:
//   * Lock at exactly the buffer's length (size 0 = "to the end") refuses instead of returning a
//     one-past-the-end pointer;
//   * CopyRects refuses mismatched formats, rects past either surface, and DXT rects off the
//     4-pixel block grid -- each of which used to memcpy past a staging vector;
//   * Texture8::LockRect honours its rect (it returned the level origin for any rect);
//   * GetIndices returns the base SetIndices stored (it returned 0);
//   * CreateIndexBuffer refuses a non-index format;
//   * a texture bound to stage >= 2 or a buffer bound to stream != 0 is a fallback (the backend
//     chains 2 stages / 1 stream) and must be counted -- not swallowed -- while clearing those
//     slots with nullptr, which the engine's blanket state reset does, stays free.
// Reports the sentinel [1,0,0,255] when everything holds.
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <emscripten.h>
#include <cstring>
#include <cstdio>
#include <cstdint>

static int g_cbCount = 0;
static void on_unhandled(const char*, uint32_t, void*) { g_cbCount++; }

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

int main() {
  dx8wasm_set_unhandled_callback(on_unhandled, nullptr);
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

  // ---- 4. Lock at exactly the buffer's length refuses ----
  // D3D8 reads size 0 as "to the end of the buffer"; from offset == length that is a zero-byte
  // range whose pointer is one past the staging vector. Real D3D8 refuses; so must this.
  {
    IDirect3DVertexBuffer8* vb2 = nullptr;
    if (dev->CreateVertexBuffer(256, 0, 0, D3DPOOL_MANAGED, &vb2) != D3D_OK || !vb2) { report_error("CreateVertexBuffer failed"); return 1; }
    BYTE* p2 = nullptr;
    if (vb2->Lock(256, 0, &p2, 0) == D3D_OK) { report_error("Lock(off == length, size 0) claimed success: a one-past-the-end pointer"); return 1; }
    vb2->Release();
  }

  // ---- 5. CopyRects validates formats and bounds; a valid copy still lands ----
  {
    IDirect3DSurface8 *sArgb = nullptr, *sDxt1 = nullptr, *sDxt5 = nullptr, *dArgb = nullptr, *dDxt1 = nullptr;
    dev->CreateImageSurface(8, 8, D3DFMT_A8R8G8B8, &sArgb);
    dev->CreateImageSurface(8, 8, D3DFMT_DXT1, &sDxt1);
    dev->CreateImageSurface(8, 8, D3DFMT_DXT5, &sDxt5);
    dev->CreateImageSurface(16, 16, D3DFMT_A8R8G8B8, &dArgb);
    dev->CreateImageSurface(16, 16, D3DFMT_DXT1, &dDxt1);
    if (!sArgb || !sDxt1 || !sDxt5 || !dArgb || !dDxt1) { report_error("CreateImageSurface failed"); return 1; }
    if (dev->CopyRects(sDxt5, nullptr, 0, sDxt1, nullptr) == D3D_OK) { report_error("CopyRects DXT5 -> DXT1 (16- vs 8-byte blocks) claimed success"); return 1; }
    if (dev->CopyRects(sArgb, nullptr, 0, sDxt1, nullptr) == D3D_OK) { report_error("CopyRects A8R8G8B8 -> DXT1 claimed success"); return 1; }
    RECT tooWide{0, 0, 12, 4};                       // past the 8x8 source
    if (dev->CopyRects(sArgb, &tooWide, 1, dArgb, nullptr) == D3D_OK) { report_error("CopyRects with a source rect past the surface claimed success"); return 1; }
    RECT quad{0, 0, 4, 4}; POINT farOff{14, 14};     // 4x4 landing at (14,14) in 16x16: two pixels over
    if (dev->CopyRects(sArgb, &quad, 1, dArgb, &farOff) == D3D_OK) { report_error("CopyRects with a destination point past the surface claimed success"); return 1; }
    RECT offGrid{2, 2, 6, 6};                        // DXT rects live on the 4x4 block grid
    if (dev->CopyRects(sDxt1, &offGrid, 1, dDxt1, nullptr) == D3D_OK) { report_error("CopyRects with a DXT rect off the 4-pixel block grid claimed success"); return 1; }
    // Positive control: the checks above must not break the copy real DDS/shroud consumers do.
    D3DLOCKED_RECT lr2{};
    sArgb->LockRect(&lr2, nullptr, 0);
    for (int y = 0; y < 8; y++) { uint32_t* row = (uint32_t*)((uint8_t*)lr2.pBits + y * lr2.Pitch); for (int x = 0; x < 8; x++) row[x] = 0xFF336699u; }
    sArgb->UnlockRect();
    POINT at{4, 4};
    if (dev->CopyRects(sArgb, nullptr, 0, dArgb, &at) != D3D_OK) { report_error("in-range CopyRects was wrongly refused"); return 1; }
    dArgb->LockRect(&lr2, nullptr, 0);
    const uint32_t inside  = *(const uint32_t*)((const uint8_t*)lr2.pBits + 4 * lr2.Pitch + 4 * 4);
    const uint32_t outside = *(const uint32_t*)lr2.pBits;
    dArgb->UnlockRect();
    if (inside != 0xFF336699u || outside == 0xFF336699u) { report_error("CopyRects positive control: the pixel did not land at the destination point"); return 1; }
    sArgb->Release(); sDxt1->Release(); sDxt5->Release(); dArgb->Release(); dDxt1->Release();
  }

  // ---- 6. Texture8::LockRect honours its rect ----
  {
    IDirect3DTexture8* t = nullptr;
    if (dev->CreateTexture(8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t) != D3D_OK || !t) { report_error("CreateTexture failed"); return 1; }
    RECT sub{2, 3, 3, 4};   // the single texel (2,3)
    D3DLOCKED_RECT lr3{};
    if (t->LockRect(0, &lr3, &sub, 0) != D3D_OK) { report_error("Texture LockRect(rect) refused"); return 1; }
    *(uint32_t*)lr3.pBits = 0xFF112233u;
    t->UnlockRect(0);
    t->LockRect(0, &lr3, nullptr, 0);
    const uint32_t at23 = *(const uint32_t*)((const uint8_t*)lr3.pBits + 3 * lr3.Pitch + 2 * 4);
    const uint32_t at00 = *(const uint32_t*)lr3.pBits;
    t->UnlockRect(0);
    t->Release();
    if (at23 != 0xFF112233u || at00 == 0xFF112233u) { report_error("Texture LockRect(rect) wrote at the level origin, not at the rect"); return 1; }
    // ...and a rect past the level refuses instead of handing out a pointer past the staging bytes.
    IDirect3DTexture8* t8 = nullptr;
    dev->CreateTexture(8, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t8);
    RECT past{6, 6, 10, 10};
    if (t8->LockRect(0, &lr3, &past, 0) == D3D_OK) { report_error("Texture LockRect with a rect past the level claimed success"); return 1; }
    IDirect3DSurface8* s8 = nullptr;
    t8->GetSurfaceLevel(0, &s8);
    if (s8->LockRect(&lr3, &past, 0) == D3D_OK) { report_error("Surface LockRect with a rect past the surface claimed success"); return 1; }
    s8->Release(); t8->Release();
  }

  // ---- 7. GetIndices returns the base SetIndices stored ----
  {
    IDirect3DIndexBuffer8* ib = nullptr;
    if (dev->CreateIndexBuffer(12, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib) != D3D_OK || !ib) { report_error("CreateIndexBuffer failed"); return 1; }
    dev->SetIndices(ib, 7);
    IDirect3DIndexBuffer8* gotIb = nullptr; UINT base = 0;
    dev->GetIndices(&gotIb, &base);
    if (gotIb) gotIb->Release();
    dev->SetIndices(nullptr, 0);
    ib->Release();
    if (base != 7) { report_error("GetIndices returned base 0 for a SetIndices base of 7"); return 1; }
  }

  // ---- 8. CreateIndexBuffer refuses a non-index format ----
  {
    IDirect3DIndexBuffer8* bad = nullptr;
    if (dev->CreateIndexBuffer(12, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &bad) == D3D_OK) { report_error("CreateIndexBuffer accepted D3DFMT_A8R8G8B8 as an index format"); return 1; }
  }

  // ---- 9. Binding beyond the backend's slots is counted, not swallowed ----
  {
    dx8wasm_coverage before{}, after{};
    dx8wasm_get_coverage(&before);
    const int cbBefore = g_cbCount;
    IDirect3DTexture8* t2 = nullptr;      dev->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t2);
    IDirect3DVertexBuffer8* vb3 = nullptr; dev->CreateVertexBuffer(64, 0, 0, D3DPOOL_MANAGED, &vb3);
    if (!t2 || !vb3) { report_error("resource creation failed"); return 1; }
    dev->SetTexture(2, nullptr); dev->SetStreamSource(1, nullptr, 0);   // the blanket clear: free
    dx8wasm_get_coverage(&after);
    if (after.fallbacks_taken != before.fallbacks_taken) { report_error("clearing stage 2 / stream 1 with nullptr was counted as a fallback"); return 1; }
    dev->SetTexture(2, t2); dev->SetTexture(2, t2);                      // same distinct slot, hit twice
    dev->SetStreamSource(1, vb3, 16);
    dx8wasm_get_coverage(&after);
    dev->SetTexture(2, nullptr); dev->SetStreamSource(1, nullptr, 0);
    t2->Release(); vb3->Release();
    if (after.fallbacks_taken - before.fallbacks_taken != 3) {
      char m[128]; std::snprintf(m, sizeof m, "stage-2 texture x2 + stream-1 buffer should count 3 fallbacks, counted %u", after.fallbacks_taken - before.fallbacks_taken);
      report_error(m); return 1;
    }
    if (g_cbCount - cbBefore != 2) { report_error("unhandled callback should fire once per distinct slot (stage 2, stream 1)"); return 1; }
  }

  report_pixel(1, 0, 0, 255);
  dev->Release(); d3d->Release();
  return 0;
}
