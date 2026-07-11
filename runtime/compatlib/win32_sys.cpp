// SPDX-License-Identifier: GPL-3.0-only
// compatlib Tier 2 — modules + threads. Everything is statically linked and the
// default build is single-threaded, so these are honest stubs: no DLLs to load,
// and CreateThread runs the body synchronously. Both log loudly the first time.
#include "compatlib/win32.h"
#include <cstdio>
#include <cstring>

extern "C" {

// Modules: no dynamic linking. LoadLibrary hands back a non-null token so callers
// proceed; GetProcAddress can't resolve anything and says so.
HMODULE LoadLibraryA(const char*) { return (HMODULE)(uintptr_t)1; }
void* GetProcAddress(HMODULE, const char* name) {
  std::fprintf(stderr, "[compat] GetProcAddress(%s): no dynamic linking — null\n", name ? name : "?");
  return nullptr;
}
BOOL FreeLibrary(HMODULE) { return 1; }
DWORD GetModuleFileNameA(HMODULE, char* buf, DWORD size) {
  if (!buf || size == 0) return 0;
  std::strncpy(buf, "/game.exe", size - 1);
  buf[size - 1] = 0;
  return (DWORD)std::strlen(buf);
}

// Threads: single-threaded build. CreateThread runs the body inline and returns
// an already-complete pseudo-handle. ponytail: a game needing true concurrency
// (a worker that must run *alongside* the caller) needs emscripten pthreads —
// revisit if one appears. The warning makes reliance on it visible.
HANDLE CreateThread(void*, uint32_t, LPTHREAD_START_ROUTINE start, void* param, DWORD, DWORD* threadId) {
  static int warned = 0;
  if (!warned) { std::fprintf(stderr, "[compat] CreateThread: running synchronously (single-threaded)\n"); warned = 1; }
  if (threadId) *threadId = 1;
  if (start) start(param);
  return (HANDLE)(uintptr_t)1;
}
DWORD GetCurrentThreadId(void) { return 1; }
BOOL TerminateThread(HANDLE, DWORD) { return 1; }

}
