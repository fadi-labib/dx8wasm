// SPDX-License-Identifier: GPL-3.0-only
#include "coverage/coverage.h"
#include "dx8wasm/contract.h"
#include "dx8wasm/telemetry.h"
#include <cstdio>
#include <set>

namespace {
dx8wasm_coverage g_counts{};
dx8wasm_unhandled_cb g_cb = nullptr;
void* g_user = nullptr;
bool g_logging = true;       // gated by dx8wasm_init(log_unimplemented)
std::set<uint64_t> g_seen;   // distinct (family, value) already reported

enum Family { RS = 0, TOP = 1, FMT = 2, TSS = 3 };

// Telemetry key kinds, one per Family above. Kept to <=10 chars so that
// "d3d8.unhandled." (15) + kind + "." + a 6-digit value (worst case we budget
// for) + NUL still fits DX8WASM_TEL_NAME_MAX (32): 15 + 9 + 1 + 6 + 1 = 32 for
// the longest of these ("render_st"). The brief's suggested "render_state" (12)
// and "stage_state" (11) do not fit and would silently truncate into colliding
// keys, so they are shortened here instead of shortening the shared prefix.
const char* const kTelKind[] = {"render_st", "texture_op", "format", "stage_st"};

// Every unhandled token is a fallback (we ignore/substitute and keep rendering),
// so bump both the category counter and fallbacks_taken. Log + callback fire
// only the first time each distinct (family, value) is seen. Telemetry fires on
// every occurrence (not just the first) with the token value in the key, so a
// reducer can sum how many times a real playthrough hit each exact token.
void note(Family fam, const char* kind, uint32_t value, uint32_t& counter) {
  ++counter;
  ++g_counts.fallbacks_taken;

  // Mirror every unhandled token into telemetry. The token value goes in the key so a
  // reducer can list exactly which states a real playthrough hit — the difference
  // between a curated conformance matrix and a measured one.
  char key[DX8WASM_TEL_NAME_MAX];
  std::snprintf(key, sizeof key, "d3d8.unhandled.%s.%u", kTelKind[fam], value);
  dx8wasm_tel_counter(key, 1);

  if (g_seen.insert(((uint64_t)fam << 32) | value).second) {
    if (g_logging) std::fprintf(stderr, "[dx8wasm] unhandled %s 0x%x — falling back\n", kind, value);
    if (g_cb) g_cb(kind, value, g_user);
  }
}
} // namespace

namespace coverage {
void unhandled_render_state(uint32_t s) { note(RS,  "D3DRS",  s, g_counts.unhandled_render_states); }
void unhandled_texture_op(uint32_t o)   { note(TOP, "D3DTOP", o, g_counts.unhandled_texture_stage_ops); }
void unhandled_format(uint32_t f)       { note(FMT, "D3DFMT", f, g_counts.unhandled_formats); }
void unhandled_stage_state(uint32_t t)  { note(TSS, "D3DTSS", t, g_counts.unhandled_texture_stage_states); }
void set_logging(bool on) { g_logging = on; }
} // namespace coverage

extern "C" {
void dx8wasm_get_coverage(dx8wasm_coverage* out) { if (out) *out = g_counts; }
void dx8wasm_set_unhandled_callback(dx8wasm_unhandled_cb cb, void* user) { g_cb = cb; g_user = user; }
}
