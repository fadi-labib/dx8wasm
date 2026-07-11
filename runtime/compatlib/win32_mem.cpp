// SPDX-License-Identifier: GPL-3.0-only
// compatlib Tier 1 — GlobalAlloc family over malloc. An 8-byte size header is
// stored before the returned block so GlobalSize can report it; the returned
// pointer stays 8-byte aligned.
#include "compatlib/win32.h"
#include <cstdlib>
#include <cstring>

extern "C" {

HGLOBAL GlobalAlloc(UINT flags, uint32_t bytes) {
  auto* p = (uint8_t*)std::malloc(bytes + 8);
  if (!p) return nullptr;
  *(uint32_t*)p = bytes;
  if (flags & GMEM_ZEROINIT) std::memset(p + 8, 0, bytes);
  return p + 8;
}
HGLOBAL GlobalFree(HGLOBAL h) {
  if (h) std::free((uint8_t*)h - 8);
  return nullptr;
}
uint32_t GlobalSize(HGLOBAL h) {
  return h ? *(uint32_t*)((uint8_t*)h - 8) : 0;
}
void GlobalMemoryStatus(MEMORYSTATUS* s) {
  if (!s) return;
  std::memset(s, 0, sizeof *s);
  s->dwLength = sizeof *s;
  s->dwMemoryLoad = 50;                       // plausible fixed values
  s->dwTotalPhys = 512u * 1024 * 1024;
  s->dwAvailPhys = 256u * 1024 * 1024;
  s->dwTotalVirtual = 2048u * 1024 * 1024;
  s->dwAvailVirtual = 1024u * 1024 * 1024;
}

}
