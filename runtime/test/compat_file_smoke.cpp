// SPDX-License-Identifier: GPL-3.0-only
// Validates compatlib Tier 1 on the emscripten filesystem: a file write/read
// round-trip (incl. GetFileSize + GetFileAttributes), backslash-path
// normalization, directory enumeration (FindFirstFile), and the GlobalAlloc
// family. Uses the generic (non-A) names so the <tchar.h>-style macros are
// exercised too. Reports [1,0,0,255] on pass.
#include "compatlib/win32.h"
#include <emscripten.h>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

#define CHECK(c, msg) do { if (!(c)) { report_error(msg); return 1; } } while (0)

int main() {
  // Write via a Windows-style backslash path (normalized to '/').
  HANDLE w = CreateFile("\\test.bin", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  CHECK(w != INVALID_HANDLE_VALUE, "CreateFile(write) failed");
  DWORD written = 0;
  CHECK(WriteFile(w, "hello", 5, &written, nullptr) && written == 5, "WriteFile wrong count");
  CloseHandle(w);

  DWORD attr = GetFileAttributes("/test.bin");
  CHECK(attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY), "GetFileAttributes wrong");

  HANDLE r = CreateFile("/test.bin", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(r != INVALID_HANDLE_VALUE, "CreateFile(read) failed");
  CHECK(GetFileSize(r, nullptr) == 5, "GetFileSize != 5");
  char buf[8] = {0}; DWORD got = 0;
  CHECK(ReadFile(r, buf, 5, &got, nullptr) && got == 5 && std::memcmp(buf, "hello", 5) == 0, "ReadFile mismatch");
  CloseHandle(r);

  // Directory enumeration.
  CreateDirectory("/dir", nullptr);
  HANDLE dw = CreateFile("/dir/a.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
  CHECK(dw != INVALID_HANDLE_VALUE, "CreateFile in /dir failed");
  WriteFile(dw, "x", 1, &written, nullptr); CloseHandle(dw);
  WIN32_FIND_DATAA fd;
  HANDLE fh = FindFirstFile("/dir/*.txt", &fd);
  CHECK(fh != INVALID_HANDLE_VALUE && std::strcmp(fd.cFileName, "a.txt") == 0, "FindFirstFile miss");
  FindClose(fh);

  // Memory.
  HGLOBAL g = GlobalAlloc(GMEM_ZEROINIT, 32);
  CHECK(g && GlobalSize(g) == 32 && ((char*)g)[0] == 0, "GlobalAlloc/Size wrong");
  ((char*)g)[0] = 42;
  GlobalFree(g);

  report_pixel(1, 0, 0, 255);
  return 0;
}
