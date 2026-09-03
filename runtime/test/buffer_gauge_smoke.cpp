// SPDX-License-Identifier: GPL-3.0-only
// Covers the buffer-upload GAUGES themselves -- gl.buf_waste_ratio and the two defect
// counters -- which buffer_range_smoke deliberately does not touch: the gauges emit once per
// second from Present, and a one-frame smoke would assert across an emission that never
// happened (its header says so). This smoke pays the seconds instead: it spans two emission
// windows and drains the telemetry ring in-process via dx8wasm_tel_drain, the same pattern
// telemetry_smoke uses to busy-wait across the flush cadence.
//
// Why this exists: the Stage A gauges were validated in situ (93% of uploads carried a lock
// flag, oob/unmatched both zero, the ratio landing on exactly 1.000 after the fix) but
// validation is not a test, and nothing in ci.sh would catch the gauges regressing. The
// perf-guard in generals-dx8wasm gates the ratio in a real capture, so a gauge that silently
// went wrong would make THAT gate a liar too. This is the synthetic oracle under it.
//
// THE ORACLE, and why a wrong state cannot produce it. Two emission windows, two exact values:
//
//   Window 1 -- buffer A: whole-buffer Lock(0,0) then sub-range Lock(off, n/2), both correct
//   paths. uploaded = n + n/2, locked = n + n/2, ratio EXACTLY 1.0.
//     * If Unlock regressed to respecifying the whole buffer, uploaded = 2n against locked
//       3n/2 and the window reads 4/3 -- the mutation test drives exactly this.
//     * If the Lock range plumbing broke (size recorded as 0), locked collapses to 2n or 0
//       and the ratio reads 3/4 or 0.0. Either way, not 1.0.
//
//   Window 2 -- buffer B, fresh, whose FIRST lock is the sub-range. Unlock must upload the
//   WHOLE buffer regardless (glBufferSubData cannot create storage, and the bytes outside the
//   lock must become the staging zeros -- AB-13, a lesson ID from the Generals integration repo's
//   results docs: that is correctness, not optimisation), while
//   only n/2 bytes were locked. ratio EXACTLY 2.0.
//     * This is the state where the gauge MUST report waste, produced by behaviour the code is
//       REQUIRED to have -- so the "can it read anything but 1.0" half of the oracle can never
//       rot into vacuity if the upload strategy changes.
//
// Both windows also assert buf_lock_oob == 0 and buf_unlock_unmatched == 0: either defect
// silently corrupts the ratio's accounting, which is why perf-guard watches them too.
#include "d3d8/d3d8.h"
#include "dx8wasm/telemetry.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct Vertex { float x, y, z; D3DCOLOR c; };   // FVF: XYZ | DIFFUSE, stride 16

// Busy-wait on the same clock device.cpp's emit gate reads. The gauges are rate-limited to
// one emission per second, so the smoke costs ~2.2 s of wall time; that is the price of
// testing the emission path itself rather than a test hook that would duplicate its math.
static void wait_ms(double ms) {
  const double t0 = emscripten_performance_now();
  while (emscripten_performance_now() - t0 < ms) {}
}

// Pull every "v" for gauge `name` out of drained NDJSON, in emission order.
// Records look like: {"k":"gauge","n":"gl.buf_waste_ratio","v":1.5}
static int find_gauge(const char* ndjson, const char* name, double* out, int cap) {
  char needle[96];
  std::snprintf(needle, sizeof needle, "\"n\":\"%s\"", name);
  int found = 0;
  const char* p = ndjson;
  while ((p = std::strstr(p, needle)) != nullptr && found < cap) {
    const char* v = std::strstr(p, "\"v\":");
    if (!v) break;
    out[found++] = std::strtod(v + 4, nullptr);
    p = v;
  }
  return found;
}

static bool near_eq(double a, double b) { return std::fabs(a - b) < 1e-9; }

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 8; pp.BackBufferHeight = 8;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  const UINT n = 12 * sizeof(Vertex);            // 192 bytes; n/2 = 96
  BYTE* dst = nullptr;

  // --- Window 1: buffer A, whole lock then sub-range lock. Expected ratio: exactly 1.0. ---
  IDirect3DVertexBuffer8* a = nullptr;
  if (dev->CreateVertexBuffer(n, 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &a) != D3D_OK) {
    report_error("CreateVertexBuffer A failed"); return 1;
  }
  if (a->Lock(0, 0, &dst, 0) != D3D_OK || !dst) { report_error("A whole Lock failed"); return 1; }
  std::memset(dst, 0x11, n);
  a->Unlock();                                   // first upload: whole buffer. up n, locked n
  if (a->Lock(n / 2, n / 2, &dst, D3DLOCK_NOOVERWRITE) != D3D_OK || !dst) {
    report_error("A sub-range Lock failed"); return 1;
  }
  std::memset(dst, 0x22, n / 2);
  a->Unlock();                                   // sub-range upload. up n/2, locked n/2
  a->Release();

  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);   // arms the emit clock, no emission yet
  wait_ms(1050.0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);   // >=1 s elapsed: emits window 1

  // --- Window 2: buffer B, FIRST lock is the sub-range. Expected ratio: exactly 2.0. ---
  IDirect3DVertexBuffer8* b = nullptr;
  if (dev->CreateVertexBuffer(n, 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &b) != D3D_OK) {
    report_error("CreateVertexBuffer B failed"); return 1;
  }
  if (b->Lock(0, n / 2, &dst, 0) != D3D_OK || !dst) { report_error("B first Lock failed"); return 1; }
  std::memset(dst, 0x33, n / 2);
  b->Unlock();                                   // first upload MUST be whole: up n, locked n/2
  b->Release();

  wait_ms(1050.0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);   // emits window 2

  // --- Drain the ring in-process and check the two windows. ---
  static char ndjson[32768];
  uint32_t total = 0, got;
  while ((got = dx8wasm_tel_drain(ndjson + total, (uint32_t)(sizeof ndjson - total))) != 0) {
    total += got;
    if (total >= sizeof ndjson - 1024) { report_error("drain buffer too small"); return 1; }
  }
  ndjson[total] = '\0';

  double ratio[4] = {0}, oob[4] = {0}, unmatched[4] = {0};
  const int nr = find_gauge(ndjson, "gl.buf_waste_ratio", ratio, 4);
  const int no = find_gauge(ndjson, "gl.buf_lock_oob", oob, 4);
  const int nu = find_gauge(ndjson, "gl.buf_unlock_unmatched", unmatched, 4);
  if (nr < 2) {
    char msg[128];
    std::snprintf(msg, sizeof msg, "expected 2 waste_ratio emissions, saw %d -- emit path broken", nr);
    report_error(msg); return 1;
  }
  char msg[192];
  if (!near_eq(ratio[0], 1.0)) {
    std::snprintf(msg, sizeof msg,
                  "window 1 waste_ratio %.6f != 1.0 -- correct locks mis-accounted", ratio[0]);
    report_error(msg); return 1;
  }
  if (!near_eq(ratio[1], 2.0)) {
    std::snprintf(msg, sizeof msg,
                  "window 2 waste_ratio %.6f != 2.0 -- first-upload waste not visible to the gauge", ratio[1]);
    report_error(msg); return 1;
  }
  for (int i = 0; i < no; i++) if (!near_eq(oob[i], 0.0)) { report_error("buf_lock_oob != 0"); return 1; }
  for (int i = 0; i < nu; i++) if (!near_eq(unmatched[i], 0.0)) { report_error("buf_unlock_unmatched != 0"); return 1; }

  report_pixel(1, 0, 0, 255);                    // internally-checked smoke: pass sentinel
  dev->Release(); d3d->Release();
  return 0;
}
