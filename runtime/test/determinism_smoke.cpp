// SPDX-License-Identifier: GPL-3.0-only
// Determinism harness (Phase 4). A replay desyncs when identical inputs stop producing identical
// state, so this renders one fixed sequence of sub-scenes TWICE through the same device and
// asserts the two framebuffer digests match — that catches state the first pass left dirty.
// scripts/determinism.mjs then loads this smoke in several fresh contexts and compares the
// published digest across them — a fresh context (not just a fresh page) is what will catch
// uninitialised memory and iteration-order-dependent shader-cache keys that an in-process repeat
// cannot see, once the rendered sequence includes a draw call. Today's sequence below is clears
// only (no draw, so no GL program is ever compiled or cached), so what this actually proves right
// now is bit-for-bit stability of the clear/present/readback path across fresh processes.
//
// The digest is asserted here and published on window.__det, NOT reported through the pixel
// tuple: phase2.gpu.test.mjs compares pixels with a +/-2 tolerance, which would happily accept a
// digest that changed by one. Reports the sentinel [1,0,0,255].
#include "d3d8/d3d8.h"
#include "frame_digest.h"
#include <cstdio>
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });
EM_JS(void, report_digest, (const char* d), { window.__det = { digest: UTF8ToString(d) }; });

static IDirect3DDevice8* g_dev = nullptr;

// One fixed sequence: four clears with different colours, each presented and read back, chained
// into one digest (four passes x 16 px = 64 px of signal per repeat). This proves bit-for-bit
// stability of the clear/present/readback path only — it does not currently exercise transforms,
// lighting, blending, or any draw call (D3DRS_ZENABLE/D3DRS_ALPHABLENDENABLE are set identically
// on every pass and so contribute no distinguishing signal). A consumer that needs those covered
// extends this sequence with its own draws.
static uint32_t render_sequence() {
  uint32_t d = digest::kSeed;
  const uint32_t colors[] = {0xFF3366CCu, 0xFF33CC66u, 0xFFCC6633u, 0xFF663399u};
  for (uint32_t c : colors) {
    g_dev->SetRenderState(D3DRS_ZENABLE, 1);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, c, 1.0f, 0);
    g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    d = digest::fnv1a_framebuffer(d, 4, 4);
  }
  return d;
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev) != D3D_OK || !g_dev) {
    report_error("CreateDevice failed"); return 1;
  }

  const uint32_t first = render_sequence();
  const uint32_t second = render_sequence();
  if (first != second) { report_error("the same sequence digested differently on a repeat"); return 1; }

  // A digest of zero would also compare equal to itself, so a readback that silently returned
  // nothing would pass the check above. Reject the degenerate value explicitly.
  if (first == digest::kSeed) { report_error("digest never absorbed any pixels"); return 1; }

  char hex[16];
  std::snprintf(hex, sizeof hex, "%08x", first);
  report_digest(hex);

  g_dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
