// SPDX-License-Identifier: GPL-3.0-only
// compatlib Tier 0 implementation. All timers derive from the one monotonic
// browser clock (emscripten_get_now, milliseconds), so they stay consistent with
// each other — QueryPerformanceCounter is that same time expressed in the fixed
// 1e6 tick/sec unit QueryPerformanceFrequency reports.
#include "compatlib/win32.h"
#include <emscripten.h>
#include <cstdio>

extern "C" {

DWORD timeGetTime(void) { return (DWORD)emscripten_get_now(); }
DWORD GetTickCount(void) { return (DWORD)emscripten_get_now(); }

BOOL QueryPerformanceFrequency(LARGE_INTEGER* freq) {
  if (!freq) return 0;
  freq->QuadPart = 1000000;   // 1 tick = 1 microsecond
  return 1;
}
BOOL QueryPerformanceCounter(LARGE_INTEGER* count) {
  if (!count) return 0;
  count->QuadPart = (LONGLONG)(emscripten_get_now() * 1000.0);   // ms -> microseconds
  return 1;
}

void Sleep(DWORD) {}              // single-threaded browser loop: nothing to sleep
DWORD timeBeginPeriod(UINT) { return 0; }

void OutputDebugStringA(const char* str) { if (str) std::fprintf(stderr, "%s", str); }

}
