// SPDX-License-Identifier: GPL-3.0-only
// compatlib Tier 1 — file + directory + shell-path shims over POSIX (the
// emscripten filesystem). Written independently from Win32 semantics. Windows paths use
// backslashes; every entry point normalizes them to '/'. 64-bit file offsets
// are not handled (emscripten `long` is 32-bit) — fine for individual assets.
#include "compatlib/win32.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <unistd.h>

namespace {
std::string norm(const char* p) {   // Windows path -> POSIX path
  std::string s(p ? p : "");
  for (char& c : s) if (c == '\\') c = '/';
  return s;
}
// Directory-enumeration context behind a FindFirstFile HANDLE.
struct Find {
  DIR* dir;
  std::string base;      // directory being listed (with trailing '/')
  std::string glob;      // pattern to match entries against
};
bool fill_next(Find* f, WIN32_FIND_DATAA* out) {
  for (struct dirent* e; (e = readdir(f->dir)) != nullptr; ) {
    if (fnmatch(f->glob.c_str(), e->d_name, 0) != 0) continue;
    std::memset(out, 0, sizeof *out);
    struct stat st;
    out->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    if (stat((f->base + e->d_name).c_str(), &st) == 0) {
      if (S_ISDIR(st.st_mode)) out->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
      out->nFileSizeLow = (DWORD)st.st_size;
    }
    std::strncpy(out->cFileName, e->d_name, MAX_PATH - 1);
    return true;
  }
  return false;
}
} // namespace

extern "C" {

HANDLE CreateFileA(const char* name, DWORD access, DWORD, void*, DWORD disposition, DWORD, HANDLE) {
  if (!name) return INVALID_HANDLE_VALUE;
  std::string path = norm(name);
  const bool wr = (access & GENERIC_WRITE) != 0;
  const char* mode = "rb";
  if (wr) {
    mode = (disposition == CREATE_ALWAYS || disposition == CREATE_NEW ||
            disposition == TRUNCATE_EXISTING) ? "wb" : "r+b";
  }
  FILE* fp = std::fopen(path.c_str(), mode);
  if (!fp && wr && disposition == OPEN_ALWAYS) fp = std::fopen(path.c_str(), "w+b");
  return fp ? (HANDLE)fp : INVALID_HANDLE_VALUE;
}

BOOL ReadFile(HANDLE h, void* buf, DWORD count, DWORD* read, void*) {
  if (h == INVALID_HANDLE_VALUE || !h) return 0;
  size_t n = std::fread(buf, 1, count, (FILE*)h);   // TRUE at EOF too (n==0), Win32-style
  if (read) *read = (DWORD)n;
  return 1;
}
BOOL WriteFile(HANDLE h, const void* buf, DWORD count, DWORD* written, void*) {
  if (h == INVALID_HANDLE_VALUE || !h) return 0;
  size_t n = std::fwrite(buf, 1, count, (FILE*)h);
  if (written) *written = (DWORD)n;
  return n == count;
}
BOOL CloseHandle(HANDLE h) {   // file handles (Find* use FindClose)
  if (h == INVALID_HANDLE_VALUE || !h) return 0;
  if ((uintptr_t)h <= 0xffff) return 1;   // Tier 2 pseudo-handle (thread/module) — no fclose
  return std::fclose((FILE*)h) == 0 ? 1 : 0;
}
DWORD SetFilePointer(HANDLE h, LONG dist, LONG* distHigh, DWORD method) {
  if (h == INVALID_HANDLE_VALUE || !h) return (DWORD)-1;
  int whence = method == FILE_BEGIN ? SEEK_SET : method == FILE_END ? SEEK_END : SEEK_CUR;
  if (std::fseek((FILE*)h, dist, whence) != 0) return (DWORD)-1;
  if (distHigh) *distHigh = 0;
  return (DWORD)std::ftell((FILE*)h);
}
DWORD GetFileSize(HANDLE h, DWORD* high) {
  if (h == INVALID_HANDLE_VALUE || !h) return (DWORD)-1;
  FILE* fp = (FILE*)h;
  long cur = std::ftell(fp);
  std::fseek(fp, 0, SEEK_END);
  long sz = std::ftell(fp);
  std::fseek(fp, cur, SEEK_SET);
  if (high) *high = 0;
  return (DWORD)sz;
}
DWORD GetFileAttributesA(const char* name) {
  struct stat st;
  if (!name || stat(norm(name).c_str(), &st) != 0) return INVALID_FILE_ATTRIBUTES;
  return S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

HANDLE FindFirstFileA(const char* pattern, WIN32_FIND_DATAA* data) {
  if (!pattern || !data) return INVALID_HANDLE_VALUE;
  std::string p = norm(pattern);
  size_t slash = p.find_last_of('/');
  std::string dir = slash == std::string::npos ? "." : p.substr(0, slash);
  std::string glob = slash == std::string::npos ? p : p.substr(slash + 1);
  DIR* d = opendir(dir.c_str());
  if (!d) return INVALID_HANDLE_VALUE;
  Find* f = new Find{d, dir + "/", glob};
  if (!fill_next(f, data)) { closedir(d); delete f; return INVALID_HANDLE_VALUE; }
  return (HANDLE)f;
}
BOOL FindNextFileA(HANDLE h, WIN32_FIND_DATAA* data) {
  if (h == INVALID_HANDLE_VALUE || !h || !data) return 0;
  return fill_next((Find*)h, data) ? 1 : 0;
}
BOOL FindClose(HANDLE h) {
  if (h == INVALID_HANDLE_VALUE || !h) return 0;
  Find* f = (Find*)h;
  closedir(f->dir);
  delete f;
  return 1;
}

BOOL CreateDirectoryA(const char* name, void*) {
  return (name && mkdir(norm(name).c_str(), 0777) == 0) ? 1 : 0;
}
DWORD GetCurrentDirectoryA(DWORD len, char* buf) {
  char tmp[MAX_PATH];
  if (!getcwd(tmp, sizeof tmp)) return 0;
  DWORD need = (DWORD)std::strlen(tmp);
  if (buf && len > need) std::strcpy(buf, tmp);
  return need;
}
BOOL SetCurrentDirectoryA(const char* name) {
  return (name && chdir(norm(name).c_str()) == 0) ? 1 : 0;
}

// Shell folders: no real "My Documents" in the browser — hand back a fixed
// virtual home the game can create and write into.
int SHGetSpecialFolderLocation(void*, int csidl, LPITEMIDLIST* ppidl) {
  if (ppidl) *ppidl = (LPITEMIDLIST)(intptr_t)(csidl + 1);   // non-null token
  return 0;   // S_OK
}
BOOL SHGetPathFromIDListA(LPITEMIDLIST, char* path) {
  if (!path) return 0;
  std::strcpy(path, "/userdata");
  return 1;
}

}
