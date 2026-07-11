// SPDX-License-Identifier: GPL-3.0-only
// Validates compatlib Tier 0 timing. All timers share one clock, so QPC/QPF
// (seconds) scaled to ms must track GetTickCount, and QPF is the fixed 1e6.
// Also exercises the no-op/debug shims for a crash check. Reports [1,0,0,255]
// on pass so it runs through the same headless harness as the GPU smokes.
#include "compatlib/win32.h"
#include <emscripten.h>
#include <cmath>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

int main() {
  LARGE_INTEGER freq{}, count{};
  if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&count)) {
    report_error("QPC/QPF returned false"); return 1;
  }
  if (freq.QuadPart != 1000000) { report_error("QPF is not 1e6"); return 1; }

  DWORD tick = GetTickCount();
  double qpc_ms = (double)count.QuadPart / (double)freq.QuadPart * 1000.0;
  if (std::fabs(qpc_ms - (double)tick) > 100.0) { report_error("QPC and GetTickCount disagree"); return 1; }

  // Exercise the remaining shims for a crash check (no observable effect expected).
  OutputDebugStringA("[compat] tier0 self-test\n");
  Sleep(3);
  timeBeginPeriod(1);
  (void)timeGetTime();

  report_pixel(1, 0, 0, 255);   // pass sentinel
  return 0;
}
