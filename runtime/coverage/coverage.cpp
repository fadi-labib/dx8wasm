// SPDX-License-Identifier: GPL-3.0-only
#include "coverage/coverage.h"
#include "dx8wasm/contract.h"
#include "dx8wasm/telemetry.h"
#include <atomic>
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
// The prefix is a single macro, shared by the budget arithmetic below and the
// snprintf that actually builds the key (in emit_direct()), so the two cannot
// silently drift apart the way a hardcoded "15" and a hardcoded literal could.
#define DX8WASM_UNHANDLED_PREFIX "d3d8.unhandled."

// Per-kind character budget, derived from DX8WASM_TEL_NAME_MAX (32, incl. NUL):
//   31 usable chars - sizeof(DX8WASM_UNHANDLED_PREFIX)-1 (15) - 1 (separating dot)
//   - 8 ("%08x") = 7 characters available for <kind>. Checked per kind below, not
//   as one number for "the longest kind" — a single blanket number is exactly the
//   kind of mistake this budget exists to catch (texture_op, at 10 chars, used to
//   be miscounted as fitting a 6-digit decimal budget when render_st, at 9, was
//   the one actually checked against it).
constexpr int kTelKindMaxLen =
    (DX8WASM_TEL_NAME_MAX - 1) - (int)(sizeof(DX8WASM_UNHANDLED_PREFIX) - 1) - 1 - 8;
static_assert(kTelKindMaxLen == 7, "recompute the comment above if DX8WASM_TEL_NAME_MAX or the prefix changes");
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
// The table never *drops* a count from hitting capacity: reaching it flushes
// immediately (freeing every slot) before the new distinct token is inserted, so
// a full table costs only a slightly earlier flush, never a lost occurrence.
//
// What it does NOT bound is the *tail*: the occurrences accumulated since the
// last flush are only guaranteed to reach telemetry once something flushes them
// — the time trigger below (checked only inside tally_record(), i.e. only when
// note() is called again), the table-full trigger, or a dx8wasm_get_coverage()
// call. If token traffic stops for good with a partial tally still pending and
// nobody ever calls dx8wasm_get_coverage() again, that tail simply never reaches
// telemetry. Nothing is fabricated or double-counted in that case, but nothing is
// accounted as a drop either, because it was never decided to be dropped — it is
// asleep, not lost. dx8wasm_tel_pump() does not know about this tally and cannot
// wake it: it only drains records the tally has already flushed into the ring.
// This is not merely theoretical: a real capture harness (the game-integration
// repo's coverage-dump-test.mjs) does NOT call dx8wasm_get_coverage() — it only
// reads [gxotel] NDJSON off the console — so the tail described above is real in
// practice, not just on paper. What actually bounds this in that harness is the
// time-based flush in tally_record() above (DX8WASM_TEL_FLUSH_MS) and the
// table-full flush; nothing calls dx8wasm_get_coverage() to add a third bound.
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
uint64_t g_lastFlushMs = 0;   // only meaningful once g_clockSeeded; see now_ms()
bool g_clockSeeded = false;   // not "g_lastFlushMs == 0": 0 is a legal timestamp
                              // (a thread's first sub-millisecond, and always on
                              // the non-Emscripten path), so overloading it as
                              // "never stamped" is the latent trap telemetry.cpp's
                              // g_everFlushed removed. Same defect class, so the
                              // same hardening here rather than only there.

// Guards g_tally/g_tallyUsed/g_lastFlushMs against the one cross-thread edge this
// file has: note() (and therefore tally_record()) only ever runs on the D3D8
// producer thread, but dx8wasm_get_coverage() is documented (contract.h) as
// callable from any thread — on an Emscripten build with -sPROXY_TO_PTHREAD=1,
// a main-thread ccall genuinely runs concurrently with the engine's worker
// thread, so without this, a get_coverage()-triggered flush could either emit
// the same tally entry the producer is mid-updating twice (a fabricated
// over-count — the one thing the telemetry honesty contract forbids), or reset
// g_tallyUsed out from under an in-flight insert (a silent, unaccounted loss).
// A plain std::atomic_flag test-and-set is enough: both sides use a *non-blocking*
// try-acquire, so the producer is never stalled by a reader (and vice versa) —
// this is deliberately not a mutex. Losing the race just means "try again on the
// next call"; see emit_direct()'s call sites for what each side does when it
// loses. g_counts and g_seen are untouched by this — they were already accepted
// as single-producer-thread state before this file added a tally, and adding
// synchronisation to them is out of scope for this change.
std::atomic_flag g_tallyLock = ATOMIC_FLAG_INIT;

// The defect fixed here was the *truncation*, not the function. `(uint32_t)
// emscripten_get_now()` saturated to 0xFFFFFFFF (under -pthread that call is
// performance.timeOrigin + performance.now(), ~1.7e12 ms, and wasm's non-trapping
// fptoui saturates rather than wraps), pinning this clock to a constant and
// disabling the time-based tally flush below forever — the same failure as
// telemetry.cpp's pump. So: keep emscripten_get_now(), widen the result.
//
// Deliberately NOT telemetry.cpp's fix (emscripten_performance_now()), because
// this clock is not single-threaded the way the pump is. g_lastFlushMs is stamped
// by flush_tally_locked(), which dx8wasm_get_coverage() reaches from ANY thread
// (contract.h; and see the g_tallyLock note above — a main-thread ccall really
// does run concurrently with the engine worker under PROXY_TO_PTHREAD), while the
// comparison happens on the producer thread. performance.now() has a per-thread
// origin, so mixing the two bases in one cursor would make a producer compare its
// own smaller value against a main-thread stamp, underflow the unsigned subtract
// to a huge number, and force a flush on the very next token. Not a correctness
// bug — it cannot fabricate or lose counts, and it self-heals after one
// producer-side flush — but it would defeat coalescing precisely when a reader
// polls, adding the ring pressure the tally exists to avoid. timeOrigin +
// performance.now() is the same base on every thread, which is exactly what a
// cursor shared across threads needs.
uint64_t now_ms() {
#ifdef __EMSCRIPTEN__
  return (uint64_t)emscripten_get_now();
#else
  // This SDK's CMakeLists.txt always configures the Emscripten toolchain (see
  // repo root), so this branch is never exercised in a shipped build. Kept, like
  // telemetry.cpp's own now_ms(), so the file stays self-contained rather than
  // hard-depending on <emscripten.h>. It returns a constant, so the time trigger
  // in tally_record() can structurally never fire on this path — inert dead code,
  // not a half-implemented feature, precisely because a non-Emscripten build of
  // this SDK does not exist. The tally still flushes on capacity, so nothing is
  // lost even here.
  return 0;
#endif
}

// Emits one coalesced counter record. Called from two places: the flush path
// (below, holding g_tallyLock, one call per distinct tallied token) and
// tally_record()'s own non-blocking fallback (not holding it, count always 1)
// when it loses the race for g_tallyLock — an extra single-occurrence record
// under that rare contention is still honest telemetry, just less coalesced;
// it is never a fabricated over-count because it corresponds to exactly the one
// occurrence tally_record() was asked to record and could not safely tally.
void emit_direct(Family fam, uint32_t value, uint32_t count) {
  char key[DX8WASM_TEL_NAME_MAX];
  std::snprintf(key, sizeof key, DX8WASM_UNHANDLED_PREFIX "%s.%08x", kTelKind[fam], value);
  dx8wasm_tel_counter(key, count);
}

// Caller must already hold g_tallyLock. Drains the tally into one emit_direct()
// call per distinct token, then clears it.
void flush_tally_locked() {
  for (int i = 0; i < g_tallyUsed; ++i) {
    const uint32_t fam = (uint32_t)(g_tally[i].key >> 32);
    const uint32_t value = (uint32_t)g_tally[i].key;
    emit_direct((Family)fam, value, g_tally[i].count);
  }
  g_tallyUsed = 0;
  g_lastFlushMs = now_ms();
  g_clockSeeded = true;   // this IS a stamp; without it a get_coverage()-driven flush
                          // that lands before the first tally_record() would be thrown
                          // away by that call's seeding branch.
}

// Non-blocking, callable from any thread: tries to acquire g_tallyLock and
// flush if it gets it, otherwise does nothing — the tally is left exactly as it
// was for the next successful attempt (a skip, never a loss). This is
// dx8wasm_get_coverage()'s flush; see its declaration in contract.h for the
// thread-affinity contract this backs.
void flush_tally() {
  if (g_tallyLock.test_and_set(std::memory_order_acquire)) return;   // contended right now; try again later
  flush_tally_locked();
  g_tallyLock.clear(std::memory_order_release);
}

// Producer path — called only from note(), i.e. only from the D3D8 thread.
// Never blocks: if a concurrent dx8wasm_get_coverage() call from another thread
// currently holds g_tallyLock, this falls back to emit_direct() for just this
// occurrence instead of touching g_tally while a reader might be iterating or
// clearing it. That fallback is what makes the guard closeable with a single
// non-blocking primitive on both sides rather than needing the producer to ever
// wait on the reader.
void tally_record(Family fam, uint32_t value) {
  if (g_tallyLock.test_and_set(std::memory_order_acquire)) {
    emit_direct(fam, value, 1);
    return;
  }

  const uint64_t key = ((uint64_t)fam << 32) | value;
  bool found = false;
  for (int i = 0; i < g_tallyUsed; ++i) {
    if (g_tally[i].key == key) { ++g_tally[i].count; found = true; break; }
  }
  if (!found) {
    if (g_tallyUsed == kTallyCapacity) flush_tally_locked();   // make room; never a drop, see the tally comment above
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
  const uint64_t t = now_ms();
  if (!g_clockSeeded) {
    g_clockSeeded = true;
    g_lastFlushMs = t;
  } else if (t - g_lastFlushMs >= DX8WASM_TEL_FLUSH_MS) {
    flush_tally_locked();
  }

  g_tallyLock.clear(std::memory_order_release);
}

// Every unhandled token is a fallback (we ignore/substitute and keep rendering),
// so bump both the category counter and fallbacks_taken on every call. Log +
// callback fire only the first time each distinct (family, value) is seen —
// that dedup is unchanged by the telemetry tally above, which counts every
// occurrence (coalesced, not deduped) so a reducer can sum how many times a
// real playthrough hit each exact token. g_counts and g_seen stay single-
// producer-thread state, exactly as before this file added a tally.
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
  flush_tally();   // best-effort, non-blocking; see contract.h's thread-affinity note.
  if (out) *out = g_counts;
}
void dx8wasm_set_unhandled_callback(dx8wasm_unhandled_cb cb, void* user) { g_cb = cb; g_user = user; }
}
