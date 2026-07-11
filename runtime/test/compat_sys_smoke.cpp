// SPDX-License-Identifier: GPL-3.0-only
// Validates compatlib Tier 2: module stubs, synchronous CreateThread (the body
// runs before it returns), CloseHandle on a thread pseudo-handle (must not
// fclose), and a registry write/read round-trip incl. the missing-value ->
// NOT_FOUND fallback. Uses generic names to exercise the macros. Pass = [1,0,0,255].
#include "compatlib/win32.h"
#include <emscripten.h>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

#define CHECK(c, msg) do { if (!(c)) { report_error(msg); return 1; } } while (0)

static DWORD thread_body(void* p) { *(int*)p = 7; return 0; }

int main() {
  // Modules.
  HMODULE m = LoadLibrary("whatever.dll");
  CHECK(m != nullptr, "LoadLibrary null");
  FreeLibrary(m);
  char exe[64] = {0};
  CHECK(GetModuleFileName(nullptr, exe, sizeof exe) > 0 && exe[0] == '/', "GetModuleFileName bad");

  // Threads run synchronously: the body has executed by the time CreateThread returns.
  int flag = 0; DWORD tid = 0;
  HANDLE t = CreateThread(nullptr, 0, thread_body, &flag, 0, &tid);
  CHECK(t != nullptr && flag == 7 && tid != 0, "CreateThread didn't run synchronously");
  CloseHandle(t);   // pseudo-handle — must be a no-op, not an fclose
  CHECK(GetCurrentThreadId() != 0, "GetCurrentThreadId 0");

  // Registry round-trip.
  HKEY k = nullptr;
  CHECK(RegCreateKeyEx(HKEY_LOCAL_MACHINE, "Software\\dx8wasm", 0, nullptr, 0, 0, nullptr, &k, nullptr) == ERROR_SUCCESS && k,
        "RegCreateKeyEx failed");
  DWORD in = 1234;
  CHECK(RegSetValueEx(k, "answer", 0, REG_DWORD, (const BYTE*)&in, sizeof in) == ERROR_SUCCESS, "RegSetValueEx failed");
  DWORD out = 0, type = 0, size = sizeof out;
  CHECK(RegQueryValueEx(k, "answer", nullptr, &type, (BYTE*)&out, &size) == ERROR_SUCCESS && out == 1234 && type == REG_DWORD,
        "RegQueryValueEx wrong");
  DWORD size2 = sizeof out;
  CHECK(RegQueryValueEx(k, "missing", nullptr, nullptr, (BYTE*)&out, &size2) == ERROR_FILE_NOT_FOUND, "missing not NOT_FOUND");
  RegCloseKey(k);

  report_pixel(1, 0, 0, 255);
  return 0;
}
