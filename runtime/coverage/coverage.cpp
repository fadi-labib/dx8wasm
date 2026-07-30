// SPDX-License-Identifier: GPL-3.0-only
#include "coverage/coverage.h"
#include "dx8wasm/contract.h"
#include "dx8wasm/telemetry.h"
#include <cstdio>
#include <set>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {
dx8wasm_coverage g_counts{};
dx8wasm_unhandled_cb g_cb = nullptr;
void* g_user = nullptr;
bool g_logging = true;       // gated by dx8wasm_init(log_unimplemented)
std::set<uint64_t> g_seen;   // distinct (family, value) already reported

enum Family { RS = 0, TOP = 1, FMT = 2, TSS = 3 };

// Telemetry key kinds, one per Family above, tied together with the array below by
// a static_assert rather than by convention — a fifth Family with no matching
// string would otherwise index kTelKind out of bounds.
#define DX8WASM_KIND_RSTATE  "rstate"
#define DX8WASM_KIND_TEXOP   "texop"
#define DX8WASM_KIND_FORMAT  "format"
#define DX8WASM_KIND_TSSTATE "tsstate"
const char* const kTelKind[] = {DX8WASM_KIND_RSTATE, DX8WASM_KIND_TEXOP, DX8WASM_KIND_FORMAT, DX8WASM_KIND_TSSTATE};
static_assert(sizeof kTelKind / sizeof *kTelKind == TSS + 1,
              "kTelKind must have exactly one entry per Family enumerator");

// Key format is "d3d8.unhandled.<kind>.<value>", value written as fixed-width hex
// ("%08x") rather than decimal. D3DFMT_* includes FourCC-encoded formats (e.g.
// D3DFMT_UYVY = 0x59565955 = 1,498,831,189 decimal, 10 digits) that decimal "%u"
// cannot bound to a fixed digit count without truncating for large values —
// "%08x" is always exactly 8 characters for any uint32_t, so this cannot
// truncate regardless of which Family or value comes through. (Hex also matches
// how D3D8 tokens are conventionally written, and the fprintf just below already
// uses "0x%x".)
//
// Per-kind character budget, derived from DX8WASM_TEL_NAME_MAX (32, incl. NUL):
//   31 usable chars - 15 ("d3d8.unhandled.") - 1 (separating dot) - 8 ("%08x")
//   = 7 characters available for <kind>. Checked per kind below, not as one
//   number for "the longest kind" — a single blanket number is exactly the kind
//   of mistake this budget exists to catch (texture_op, at 10 chars, used to be
//   miscounted as fitting a 6-digit decimal budget when render_st, at 9, was the
//   one actually checked against it).
constexpr int kTelKindMaxLen = (DX8WASM_TEL_NAME_MAX - 1) - 15 - 1 - 8;
static_assert(kTelKindMaxLen == 7, "recompute the comment above if DX8WASM_TEL_NAME_MAX changes");
static_assert(sizeof(DX8WASM_KIND_RSTATE)  - 1 <= kTelKindMaxLen, DX8WASM_KIND_RSTATE  " exceeds the per-kind budget");
static_assert(sizeof(DX8WASM_KIND_TEXOP)   - 1 <= kTelKindMaxLen, DX8WASM_KIND_TEXOP   " exceeds the per-kind budget");
static_assert(sizeof(DX8WASM_KIND_FORMAT)  - 1 <= kTelKindMaxLen, DX8WASM_KIND_FORMAT  " exceeds the per-kind budget");
static_assert(sizeof(DX8WASM_KIND_TSSTATE) - 1 <= kTelKindMaxLen, DX8WASM_KIND_TSSTATE " exceeds the per-kind budget");

// --- Per-token telemetry tally --------------------------------------------------
//
// note() is on the hot path: an unhandled D3DRS/D3DTSS/D3DTOP/D3DFMT token fires
// once per SetRenderState/SetTextureStageState/CreateTexture call, which can be
// thousands of calls per frame in a real game. Emitting one dx8wasm_tel_counter()
// record per *occurrence* would overrun the 1024-record ring many times over
// between the platform seam's 1 Hz drains, crowding out other subsystems'
// records (frame spans, engine logs) and silently under-counting this one's own
// totals (the ring counts drops, but a drop is still a lost measurement).
//
// So: tally occurrences per distinct (family, value) in a small fixed-size table
// and flush the whole table into one dx8wasm_tel_counter(key, count) call per
// distinct token — coalescing, not sampling, so the reducer's sum is still exact.
// The table never *drops* a count: reaching capacity flushes immediately (freeing
// every slot) before the new distinct token is inserted, so a full table costs
// only a slightly earlier flush, never a lost occurrence.
constexpr int kTallyCapacity = 64;   // A real playthrough's distinct unhandled tokens
                                      // are a small subset of the ~150 D3DRS + ~25
                                      // D3DTOP + ~30 D3DTSS + a handful of D3DFMT
                                      // values that exist at all; 64 is comfortable
                                      // headroom over what one flush window (a
                                      // second, or one dx8wasm_get_coverage() call)
                                      // plausibly discovers for the first time. The
                                      // number is a batching tradeoff, not a
                                      // correctness one, precisely because hitting
                                      // it flushes rather than drops.
struct TallyEntry {
  uint64_t key;     // (family << 32) | value
  uint32_t count;
};
TallyEntry g_tally[kTallyCapacity];
int g_tallyUsed = 0;
uint32_t g_lastFlushMs = 0;

uint32_t now_ms() {
#ifdef __EMSCRIPTEN__
  return (uint32_t)emscripten_get_now();
#else
  return 0;
#endif
}

// Drains the tally into one dx8wasm_tel_counter() call per distinct token, then
// clears it. Called from three places: (a) tally_record(), when the table is
// full and a new distinct token needs a slot — see the capacity comment above
// for why this never loses a count; (b) tally_record(), once
// DX8WASM_TEL_FLUSH_MS has elapsed since the last flush, so the ring receives
// coalesced records at roughly the cadence the platform seam drains it at; and
// (c) dx8wasm_get_coverage(), so telemetry is exact — not up to a second stale —
// at the moment a reader (Task 9's capture reducer) samples coverage, rather
// than whatever was still sitting in the tally table when it happened to ask.
void flush_tally() {
  for (int i = 0; i < g_tallyUsed; ++i) {
    const uint32_t fam = (uint32_t)(g_tally[i].key >> 32);
    const uint32_t value = (uint32_t)g_tally[i].key;
    char key[DX8WASM_TEL_NAME_MAX];
    std::snprintf(key, sizeof key, "d3d8.unhandled.%s.%08x", kTelKind[fam], value);
    dx8wasm_tel_counter(key, g_tally[i].count);
  }
  g_tallyUsed = 0;
  g_lastFlushMs = now_ms();
}

void tally_record(Family fam, uint32_t value) {
  const uint64_t key = ((uint64_t)fam << 32) | value;
  bool found = false;
  for (int i = 0; i < g_tallyUsed; ++i) {
    if (g_tally[i].key == key) { ++g_tally[i].count; found = true; break; }
  }
  if (!found) {
    if (g_tallyUsed == kTallyCapacity) flush_tally();   // make room; never a drop, see above
    g_tally[g_tallyUsed].key = key;
    g_tally[g_tallyUsed].count = 1;
    ++g_tallyUsed;
  }

  // Time-based flush. Unlike telemetry.cpp's dx8wasm_tel_pump (which is fine to
  // flush on its very first call, since it is driven by an external tick that
  // already waited), the tally's first-ever call is also the first token this
  // process has seen — flushing immediately would split a burst that starts at
  // process start into many single-occurrence records, defeating coalescing for
  // exactly the case that matters most. So the first call only seeds the clock.
  const uint32_t t = now_ms();
  if (g_lastFlushMs == 0) { g_lastFlushMs = t; return; }
  if (t - g_lastFlushMs >= DX8WASM_TEL_FLUSH_MS) flush_tally();
}

// Every unhandled token is a fallback (we ignore/substitute and keep rendering),
// so bump both the category counter and fallbacks_taken on every call. Log +
// callback fire only the first time each distinct (family, value) is seen —
// that dedup is unchanged by the telemetry tally above, which counts every
// occurrence (coalesced, not deduped) so a reducer can sum how many times a
// real playthrough hit each exact token.
void note(Family fam, const char* kind, uint32_t value, uint32_t& counter) {
  ++counter;
  ++g_counts.fallbacks_taken;

  tally_record(fam, value);

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
void dx8wasm_get_coverage(dx8wasm_coverage* out) {
  flush_tally();   // exact at read time — see flush_tally()'s comment, reason (c).
  if (out) *out = g_counts;
}
void dx8wasm_set_unhandled_callback(dx8wasm_unhandled_cb cb, void* user) { g_cb = cb; g_user = user; }
}
