// SPDX-License-Identifier: GPL-3.0-only
#include "coverage/coverage.h"
#include "dx8wasm/contract.h"
#include <cstdio>
#include <set>

namespace {
dx8wasm_coverage g_counts{};
dx8wasm_unhandled_cb g_cb = nullptr;
void* g_user = nullptr;
std::set<uint64_t> g_seen;   // distinct (family, value) already reported

enum Family { RS = 0, TOP = 1, FMT = 2 };

// Every unhandled token is a fallback (we ignore/substitute and keep rendering),
// so bump both the category counter and fallbacks_taken. Log + callback fire
// only the first time each distinct (family, value) is seen.
void note(Family fam, const char* kind, uint32_t value, uint32_t& counter) {
  ++counter;
  ++g_counts.fallbacks_taken;
  if (g_seen.insert(((uint64_t)fam << 32) | value).second) {
    std::fprintf(stderr, "[dx8wasm] unhandled %s 0x%x — falling back\n", kind, value);
    if (g_cb) g_cb(kind, value, g_user);
  }
}
} // namespace

namespace coverage {
void unhandled_render_state(uint32_t s) { note(RS,  "D3DRS",  s, g_counts.unhandled_render_states); }
void unhandled_texture_op(uint32_t o)   { note(TOP, "D3DTOP", o, g_counts.unhandled_texture_stage_ops); }
void unhandled_format(uint32_t f)       { note(FMT, "D3DFMT", f, g_counts.unhandled_formats); }
} // namespace coverage

extern "C" {
void dx8wasm_get_coverage(dx8wasm_coverage* out) { if (out) *out = g_counts; }
void dx8wasm_set_unhandled_callback(dx8wasm_unhandled_cb cb, void* user) { g_cb = cb; g_user = user; }
}
