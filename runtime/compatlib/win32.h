// SPDX-License-Identifier: GPL-3.0-only
// compatlib Tier 0 — the Win32 bedrock a DX8 game references on its first frame:
// core types, timing, and debug output. Written independently from the public Win32 API
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

// --- Tier 1 types + constants (kernel32 file/dir/mem) -----------------------
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES ((DWORD)-1)
#define GENERIC_READ  0x80000000u
#define GENERIC_WRITE 0x40000000u
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define GMEM_FIXED 0x0000
#define GMEM_ZEROINIT 0x0040

using HGLOBAL = void*;
using HKEY = void*;
using REGSAM = DWORD;
typedef DWORD (*LPTHREAD_START_ROUTINE)(void*);

// --- Tier 2 constants (kernel32 / advapi32) ---------------------------------
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_MORE_DATA 234
#define REG_SZ 1
#define REG_BINARY 3
#define REG_DWORD 4
#define HKEY_CLASSES_ROOT  ((HKEY)(uintptr_t)0x80000000)
#define HKEY_CURRENT_USER  ((HKEY)(uintptr_t)0x80000001)
#define HKEY_LOCAL_MACHINE ((HKEY)(uintptr_t)0x80000002)
#define HKEY_USERS         ((HKEY)(uintptr_t)0x80000003)

#define LoadLibrary LoadLibraryA
#define GetModuleFileName GetModuleFileNameA
#define RegOpenKeyEx RegOpenKeyExA
#define RegCreateKeyEx RegCreateKeyExA
#define RegQueryValueEx RegQueryValueExA
#define RegSetValueEx RegSetValueExA
struct FILETIME { DWORD dwLowDateTime, dwHighDateTime; };
struct WIN32_FIND_DATAA {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
  DWORD nFileSizeHigh, nFileSizeLow;
  DWORD dwReserved0, dwReserved1;
  char  cFileName[MAX_PATH];
  char  cAlternateFileName[14];
};
struct MEMORYSTATUS {
  DWORD dwLength, dwMemoryLoad;
  DWORD dwTotalPhys, dwAvailPhys, dwTotalPageFile, dwAvailPageFile, dwTotalVirtual, dwAvailVirtual;
};

// Non-UNICODE generic-name mapping, matching Win32's <tchar.h> behaviour.
#define CreateFile CreateFileA
#define GetFileAttributes GetFileAttributesA
#define FindFirstFile FindFirstFileA
#define FindNextFile FindNextFileA
#define CreateDirectory CreateDirectoryA
#define GetCurrentDirectory GetCurrentDirectoryA
#define SetCurrentDirectory SetCurrentDirectoryA

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

// --- Tier 1: file I/O -------------------------------------------------------
HANDLE CreateFileA(const char* name, DWORD access, DWORD share, void* sec,
                   DWORD disposition, DWORD flags, HANDLE templ);
BOOL  ReadFile(HANDLE h, void* buf, DWORD count, DWORD* read, void* overlapped);
BOOL  WriteFile(HANDLE h, const void* buf, DWORD count, DWORD* written, void* overlapped);
BOOL  CloseHandle(HANDLE h);
DWORD SetFilePointer(HANDLE h, LONG dist, LONG* distHigh, DWORD method);
DWORD GetFileSize(HANDLE h, DWORD* high);
DWORD GetFileAttributesA(const char* name);

// --- Tier 1: directories ----------------------------------------------------
HANDLE FindFirstFileA(const char* pattern, WIN32_FIND_DATAA* data);
BOOL   FindNextFileA(HANDLE h, WIN32_FIND_DATAA* data);
BOOL   FindClose(HANDLE h);
BOOL   CreateDirectoryA(const char* name, void* sec);
DWORD  GetCurrentDirectoryA(DWORD len, char* buf);
BOOL   SetCurrentDirectoryA(const char* name);

// --- Tier 1: memory ---------------------------------------------------------
HGLOBAL GlobalAlloc(UINT flags, uint32_t bytes);
HGLOBAL GlobalFree(HGLOBAL h);
uint32_t GlobalSize(HGLOBAL h);
void    GlobalMemoryStatus(MEMORYSTATUS* status);

// --- Tier 1: shell folders (shell32) — stubbed to a fixed virtual home ------
typedef void* LPITEMIDLIST;
int  SHGetSpecialFolderLocation(void* hwnd, int csidl, LPITEMIDLIST* ppidl);
BOOL SHGetPathFromIDListA(LPITEMIDLIST pidl, char* path);
#define SHGetPathFromIDList SHGetPathFromIDListA

// --- Tier 2: modules (static link — stubs) ----------------------------------
HMODULE LoadLibraryA(const char* name);
void*   GetProcAddress(HMODULE mod, const char* name);
BOOL    FreeLibrary(HMODULE mod);
DWORD   GetModuleFileNameA(HMODULE mod, char* buf, DWORD size);

// --- Tier 2: threads (single-threaded: CreateThread runs synchronously) ------
HANDLE CreateThread(void* sec, uint32_t stack, LPTHREAD_START_ROUTINE start,
                    void* param, DWORD flags, DWORD* threadId);
DWORD  GetCurrentThreadId(void);
BOOL   TerminateThread(HANDLE thread, DWORD exitCode);

// --- Tier 2: registry (in-memory key/value store) ---------------------------
LONG RegOpenKeyExA(HKEY key, const char* subkey, DWORD opts, REGSAM sam, HKEY* result);
LONG RegCreateKeyExA(HKEY key, const char* subkey, DWORD reserved, char* cls, DWORD opts,
                     REGSAM sam, void* sec, HKEY* result, DWORD* disposition);
LONG RegQueryValueExA(HKEY key, const char* name, DWORD* reserved, DWORD* type,
                      BYTE* data, DWORD* size);
LONG RegSetValueExA(HKEY key, const char* name, DWORD reserved, DWORD type,
                    const BYTE* data, DWORD size);
LONG RegCloseKey(HKEY key);

#ifdef __cplusplus
}
#endif
#endif  // DX8WASM_COMPAT_WIN32_H
