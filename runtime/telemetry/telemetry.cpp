// SPDX-License-Identifier: GPL-3.0-only
// Ring-buffer telemetry (see runtime/include/dx8wasm/telemetry.h for the why).
//
// Single-consumer, multi-producer. A producer claims a slot with a CAS loop on the
// write cursor that rechecks fullness on every attempt: this is deliberate — a
// dropped record must never consume a write index, or the consumer's "walk to the
// write cursor" would run over an index nobody wrote this lap and re-serialise a
// stale record from a previous one (fabricated data, not just lost data). Once a
// slot is claimed it belongs exclusively to that producer, which fills the fields
// and then release-publishes a per-slot ready flag; the consumer only reads a slot
// after acquiring that flag, so it can never observe a partially-written record.
// The handshake is symmetric on the way back: recycling a slot (clearing its ready
// flag and advancing g_read) is also a release, paired with an acquire load of
// g_read in claim() — a producer must not overwrite a slot's fields until it can
// see that the consumer has finished reading them, or the same race reappears on
// the other edge.
// A full ring drops the record and bumps g_dropped — bounded memory is worth more
// here than completeness, because the alternative (grow, or block the engine
// thread) perturbs the very frame times this exists to measure.
#include "dx8wasm/telemetry.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>   // emscripten_performance_now — see now_ms()
#endif

namespace {

enum Kind : uint8_t { KLOG = 0, KCOUNTER = 1, KSPAN = 2 };

struct Record {
    uint8_t  kind;
    uint32_t value;                        // counter delta
    double   ms;                           // span duration
    char     name[DX8WASM_TEL_NAME_MAX];
    char     detail[DX8WASM_TEL_DETAIL_MAX];
};

// Zero-initialised by the static-storage-duration zero-init phase (no initialiser
// needed, and none is given — same reliance as the plain-old g_ring array below).
Record                g_ring[DX8WASM_TEL_CAPACITY];
std::atomic<uint8_t>  g_ready[DX8WASM_TEL_CAPACITY];   // release/acquire commit flag per slot

std::atomic<uint32_t> g_write{0};          // next slot to claim; only advances on a successful claim
std::atomic<uint32_t> g_read{0};           // consumer cursor; only the consumer stores, producers only load
std::atomic<uint32_t> g_dropped{0};
uint32_t              g_seq = 0;           // single-consumer only (drain), so plain uint32_t suffices
uint64_t              g_lastFlushMs = 0;   // only meaningful once g_everFlushed; widened, see now_ms()
bool                  g_everFlushed = false;  // not "g_lastFlushMs != 0": 0 is a legal timestamp

void copy_field(char* dst, uint32_t cap, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

// Escapes `src` for embedding as a JSON string body (no surrounding quotes) into
// `dst`. `name`/`detail` are free text (paths, asset names, error strings) and are
// not guaranteed to be free of '"', '\', or control characters, so this is not
// optional: an unescaped quote or backslash breaks the NDJSON line for every
// downstream parser. `dst` must be sized for the worst case by the caller (every
// source byte becoming a 6-byte \u00XX escape); this truncates defensively if not.
void json_escape(char* dst, size_t dstCap, const char* src) {
    size_t o = 0;
    if (!src) src = "";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(src); *p && o + 7 <= dstCap; ++p) {
        const unsigned char c = *p;
        switch (c) {
            case '"':  dst[o++] = '\\'; dst[o++] = '"';  break;
            case '\\': dst[o++] = '\\'; dst[o++] = '\\'; break;
            case '\n': dst[o++] = '\\'; dst[o++] = 'n';  break;
            case '\r': dst[o++] = '\\'; dst[o++] = 'r';  break;
            case '\t': dst[o++] = '\\'; dst[o++] = 't';  break;
            default:
                if (c < 0x20) {
                    o += (size_t)std::snprintf(dst + o, dstCap - o, "\\u%04x", c);
                } else {
                    dst[o++] = (char)c;
                }
        }
    }
    dst[o] = '\0';
}

// Claim a slot and fill the common fields. Returns nullptr if the ring is full, in
// which case `g_write` is left untouched — the whole point of the CAS loop over a
// fetch_add is that a drop must not burn a write index (see file header).
Record* claim(uint8_t kind, const char* name, uint32_t* outSlot) {
    uint32_t write = g_write.load(std::memory_order_relaxed);
    for (;;) {
        // Acquire: paired with the release store on the consumer's recycle edge
        // (dx8wasm_tel_drain, both the success and the n<=0 drop path). Without this,
        // a producer reusing a recycled slot could race the consumer's just-finished
        // (non-atomic) read of that same slot's previous contents — the same class of
        // bug as an unpublished write, just on the other edge of the handshake.
        const uint32_t read = g_read.load(std::memory_order_acquire);
        if (write - read >= DX8WASM_TEL_CAPACITY) {   // unsigned wrap-safe distance
            g_dropped.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        if (g_write.compare_exchange_weak(write, write + 1,
                                           std::memory_order_relaxed, std::memory_order_relaxed)) {
            break;   // slot `write` is now exclusively ours
        }
        // CAS failed: `write` was refreshed to the current value; loop retries with it.
    }
    *outSlot = write;
    Record* r = &g_ring[write % DX8WASM_TEL_CAPACITY];
    r->kind = kind;
    r->value = 0;
    r->ms = 0.0;
    copy_field(r->name, DX8WASM_TEL_NAME_MAX, name);
    r->detail[0] = '\0';
    return r;
}

// Publish a claimed slot to the consumer. Release, paired with the acquire load in
// dx8wasm_tel_drain: everything claim()'s caller wrote to the record happens-before
// the consumer's read of it.
void commit(uint32_t slot) {
    g_ready[slot % DX8WASM_TEL_CAPACITY].store(1, std::memory_order_release);
}

// Deliberately NOT emscripten_get_now(). Under -pthread (which every build that
// uses this ring is, because PROXY_TO_PTHREAD is why the ring exists) emscripten
// defines emscripten_get_now() as `performance.timeOrigin + performance.now()` —
// a Unix-epoch millisecond value, ~1.7e12, so pthreads share one time base. That
// does not fit in uint32_t, and wasm's non-trapping fptoui *saturates* rather
// than wrapping: the cast returned 0xFFFFFFFF on every call, forever. The pump's
// rate limit then latched shut after its first call (t - g_lastFlushMs == 0 <
// FLUSH_MS on every subsequent frame) and the ring was never flushed again — the
// whole pipeline silently delivered nothing while looking healthy.
// performance.now() is thread-relative and stays in range for ~49 days as a
// double-precision millisecond count, but ~49 days is still a deadline, not a
// fix — the same saturating float->i32 fptoui that caused this whole task would
// fire again once thread uptime crossed it, just with a much longer fuse before
// the pump latched shut permanently. So the cast target is uint64_t here, not
// uint32_t: casting a double that stays well under 2^63 to a 64-bit integer
// never approaches the range where wasm's non-trapping fptoui saturates, closing
// the class rather than deferring it. This does not change the threading story:
// the value is still a per-thread origin, only acceptable because the pump is
// single-consumer (see the thread-affinity requirement on dx8wasm_tel_pump() in
// telemetry.h). Call the pump from two threads and this clock's two bases
// interleave, which makes the rate limit meaningless regardless of width.
// (coverage.cpp's own flush clock is read from more than one thread and for
// that reason deliberately does NOT use this function — it keeps
// emscripten_get_now() and holds it in a uint64_t of its own.)
uint64_t now_ms() {
#ifdef __EMSCRIPTEN__
    return (uint64_t)emscripten_performance_now();
#else
    // No browser clock on this path. Return a value that always advances by a full
    // interval so the rate limit degrades to "flush on every call" — which is what
    // this path has always done, stdout being its sink. It must not be a constant:
    // gating on a constant clock is the exact latch this function was fixed for,
    // and reintroducing it here would rot the portability the #else exists for.
    // This is a rate-limiter input, not a timestamp; nothing is measured with it.
    static uint64_t tick = 0;
    return tick += DX8WASM_TEL_FLUSH_MS;
#endif
}

}  // namespace

extern "C" {

void dx8wasm_tel_log(const char* name, const char* detail) {
    uint32_t slot;
    if (Record* r = claim(KLOG, name, &slot)) {
        copy_field(r->detail, DX8WASM_TEL_DETAIL_MAX, detail);
        commit(slot);
    }
}

void dx8wasm_tel_counter(const char* name, uint32_t delta) {
    uint32_t slot;
    if (Record* r = claim(KCOUNTER, name, &slot)) {
        r->value = delta;
        commit(slot);
    }
}

void dx8wasm_tel_span(const char* name, double ms) {
    uint32_t slot;
    if (Record* r = claim(KSPAN, name, &slot)) {
        r->ms = ms;
        commit(slot);
    }
}

uint32_t dx8wasm_tel_dropped(void) { return g_dropped.load(std::memory_order_relaxed); }

uint32_t dx8wasm_tel_drain(char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    uint32_t used = 0;
    out[0] = '\0';
    for (;;) {
        const uint32_t read = g_read.load(std::memory_order_relaxed);
        const uint32_t write = g_write.load(std::memory_order_relaxed);
        if (read == write) break;   // nothing claimed beyond here

        const uint32_t idx = read % DX8WASM_TEL_CAPACITY;
        // Acquire: a producer may have claimed this slot (advanced g_write) but not
        // finished writing its fields yet. Stop rather than read it mid-write, and
        // do not skip ahead to a later slot that happens to be ready already — that
        // would reorder output relative to claim order.
        if (g_ready[idx].load(std::memory_order_acquire) == 0) break;

        const Record& r = g_ring[idx];
        char nameEsc[6 * (DX8WASM_TEL_NAME_MAX - 1) + 1];
        json_escape(nameEsc, sizeof nameEsc, r.name);

        char line[900];
        int n = 0;
        if (r.kind == KLOG) {
            char detailEsc[6 * (DX8WASM_TEL_DETAIL_MAX - 1) + 1];
            json_escape(detailEsc, sizeof detailEsc, r.detail);
            n = std::snprintf(line, sizeof line,
                              "{\"seq\":%u,\"k\":\"log\",\"n\":\"%s\",\"d\":\"%s\"}\n",
                              g_seq, nameEsc, detailEsc);
        } else if (r.kind == KCOUNTER) {
            n = std::snprintf(line, sizeof line,
                              "{\"seq\":%u,\"k\":\"counter\",\"n\":\"%s\",\"v\":%u}\n",
                              g_seq, nameEsc, r.value);
        } else {
            n = std::snprintf(line, sizeof line,
                              "{\"seq\":%u,\"k\":\"span\",\"n\":\"%s\",\"ms\":%g}\n",
                              g_seq, nameEsc, r.ms);
        }

        if (n <= 0) {
            // Formatting failure: don't fabricate a line and don't spin on it forever,
            // but do count it as lost — every other loss path bumps g_dropped, so this
            // one must too, or an overflow could look smaller than it really was.
            g_dropped.fetch_add(1, std::memory_order_relaxed);
            g_ready[idx].store(0, std::memory_order_relaxed);
            // Release: paired with the acquire load in claim(). Recycling a slot is a
            // handshake just like publishing one — a producer must not reuse it until
            // it can see that the consumer is done reading it.
            g_read.store(read + 1, std::memory_order_release);
            continue;
        }
        if (used + (uint32_t)n + 1 > cap) break;   // doesn't fit — leave it queued, seq NOT spent

        std::memcpy(out + used, line, (uint32_t)n);
        used += (uint32_t)n;
        out[used] = '\0';
        g_seq++;   // only now, once the record is actually in `out`, is the seq spent
        g_ready[idx].store(0, std::memory_order_relaxed);
        // Release: same recycle handshake as the drop path above — a producer that
        // later reclaims this slot must see this store (and everything before it,
        // including this drain's own reads of g_ring[idx]) before it can overwrite
        // the slot's fields.
        g_read.store(read + 1, std::memory_order_release);
    }
    return used;
}

void dx8wasm_tel_pump(void) {
    const uint64_t t = now_ms();
    if (g_everFlushed && t - g_lastFlushMs < DX8WASM_TEL_FLUSH_MS) return;
    g_everFlushed = true;
    g_lastFlushMs = t;

    static char buf[16384];
    const uint32_t n = dx8wasm_tel_drain(buf, sizeof buf);
    if (n == 0) return;
#ifdef __EMSCRIPTEN__
    // One proxied call per flush, not per record — the whole point of the ring.
    MAIN_THREAD_EM_ASM({
        var text = UTF8ToString($0);
        if (typeof gxTelemetry === 'function') gxTelemetry(text);
        else console.log(text);
    }, buf);
#else
    std::fputs(buf, stdout);
#endif
}

}  // extern "C"
