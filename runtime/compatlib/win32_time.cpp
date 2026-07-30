// SPDX-License-Identifier: GPL-3.0-only
// compatlib Tier 0 implementation. All timers derive from the one monotonic
// browser clock (emscripten_get_now, milliseconds), so they stay consistent with
// each other — QueryPerformanceCounter is that same time expressed in the fixed
// 1e6 tick/sec unit QueryPerformanceFrequency reports.
#include "compatlib/win32.h"
#include <emscripten.h>
#include <cstdio>

extern "C" {

// Both of these are 32-bit tick counts that the real Win32 API wraps every ~49.7
// days, and callers are written for that. Getting there needs the double widened to
// a 64-bit integer FIRST and only then truncated: casting the double straight to
// DWORD is a float->i32 conversion, and under -pthread emscripten_get_now() is
// performance.timeOrigin + performance.now() (~1.7e12 ms), far outside uint32_t's
// range. wasm's non-trapping fptoui saturates rather than wrapping, so the direct
// cast returned a constant 0xFFFFFFFF — a frozen clock, which silently breaks every
// caller that paces or times out on it. Widen, then wrap. (Same defect as the one
// fixed in telemetry.cpp/coverage.cpp; scripts/check.sh guards the pattern now.)
DWORD timeGetTime(void) { return (DWORD)(uint64_t)emscripten_get_now(); }
DWORD GetTickCount(void) { return (DWORD)(uint64_t)emscripten_get_now(); }

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
