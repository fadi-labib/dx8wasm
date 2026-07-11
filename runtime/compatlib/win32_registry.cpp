// SPDX-License-Identifier: GPL-3.0-only
// compatlib Tier 2 — registry over an in-memory key/value store. Enough for a
// game that reads a handful of settings (missing values return NOT_FOUND so the
// game falls back to its defaults) and writes them back. Not persisted across
// reloads yet — a later pass can back it with OPFS/localStorage.
//
// HKEY encoding: predefined roots have the high bit set (0x8000xxxx); an opened
// key is a heap std::string* holding its full path (wasm32 heap pointers never
// have the high bit set, so the two are unambiguous).
#include "compatlib/win32.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
struct Val { DWORD type; std::vector<uint8_t> data; };
std::map<std::string, Val>& store() { static std::map<std::string, Val> m; return m; }

std::string key_path(HKEY k) {
  uintptr_t v = (uintptr_t)k;
  if (v & 0x80000000u) {
    switch (v) {
      case 0x80000000u: return "HKCR"; case 0x80000001u: return "HKCU";
      case 0x80000002u: return "HKLM"; default: return "HKU";
    }
  }
  return v ? *(std::string*)k : std::string();
}
} // namespace

extern "C" {

LONG RegOpenKeyExA(HKEY key, const char* subkey, DWORD, REGSAM, HKEY* result) {
  if (!result) return ERROR_FILE_NOT_FOUND;
  std::string path = key_path(key);
  if (subkey && *subkey) path += "\\" + std::string(subkey);
  *result = new std::string(path);
  return ERROR_SUCCESS;
}
LONG RegCreateKeyExA(HKEY key, const char* subkey, DWORD, char*, DWORD, REGSAM, void*, HKEY* result, DWORD* disp) {
  if (disp) *disp = 1;   // REG_CREATED_NEW_KEY
  return RegOpenKeyExA(key, subkey, 0, 0, result);
}
LONG RegQueryValueExA(HKEY key, const char* name, DWORD*, DWORD* type, BYTE* data, DWORD* size) {
  auto it = store().find(key_path(key) + "\\" + (name ? name : ""));
  if (it == store().end()) return ERROR_FILE_NOT_FOUND;
  const Val& v = it->second;
  if (type) *type = v.type;
  if (data) {
    if (!size || *size < v.data.size()) { if (size) *size = (DWORD)v.data.size(); return ERROR_MORE_DATA; }
    std::memcpy(data, v.data.data(), v.data.size());
  }
  if (size) *size = (DWORD)v.data.size();
  return ERROR_SUCCESS;
}
LONG RegSetValueExA(HKEY key, const char* name, DWORD, DWORD type, const BYTE* data, DWORD size) {
  Val& v = store()[key_path(key) + "\\" + (name ? name : "")];
  v.type = type;
  v.data.assign(data, data + size);
  return ERROR_SUCCESS;
}
LONG RegCloseKey(HKEY key) {
  uintptr_t v = (uintptr_t)key;
  if (v && !(v & 0x80000000u)) delete (std::string*)key;   // opened key; roots aren't heap
  return ERROR_SUCCESS;
}

}
