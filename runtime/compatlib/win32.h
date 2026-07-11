// SPDX-License-Identifier: GPL-3.0-only
// compatlib Tier 0 — the Win32 bedrock a DX8 game references on its first frame:
// core types, timing, and debug output. Clean-room from the public Win32 API
// semantics; maps onto emscripten/POSIX. Grows tier by tier (see docs/COMPATLIB.md).
//
// A game includes this where it would include <windows.h>. The `using` aliases
// intentionally match d3d8.h's (redefining an alias to the same type is legal),
// so both headers can be included together.
#ifndef DX8WASM_COMPAT_WIN32_H
#define DX8WASM_COMPAT_WIN32_H
#include <cstdint>

using DWORD = uint32_t;
using UINT = uint32_t;
using BYTE = uint8_t;
using BOOL = int32_t;
using LONG = int32_t;
using LONGLONG = int64_t;
using HANDLE = void*;
using HMODULE = void*;

union LARGE_INTEGER {
  struct { DWORD LowPart; LONG HighPart; } u;
  LONGLONG QuadPart;
};

#ifdef __cplusplus
extern "C" {
#endif

// --- Timing (winmm / kernel32) ----------------------------------------------
DWORD timeGetTime(void);                             // ms since an arbitrary epoch
DWORD GetTickCount(void);                            // same clock
BOOL  QueryPerformanceCounter(LARGE_INTEGER* count); // microsecond ticks
BOOL  QueryPerformanceFrequency(LARGE_INTEGER* freq);// fixed 1e6 (ticks/sec)
void  Sleep(DWORD milliseconds);                     // no-op in the single-threaded loop
DWORD timeBeginPeriod(UINT period);                  // no-op, returns 0

// --- Debug ------------------------------------------------------------------
void  OutputDebugStringA(const char* str);

#ifdef __cplusplus
}
#endif
#endif  // DX8WASM_COMPAT_WIN32_H
