// SPDX-License-Identifier: GPL-3.0-only
// Ring-buffer telemetry (see runtime/include/dx8wasm/telemetry.h for the why).
//
// Single-consumer, multi-producer. Producers claim a slot with one relaxed
// fetch_add on the write cursor; the consumer walks from the read cursor to the
// write cursor. A producer that laps the reader loses its record and bumps
// g_dropped — bounded memory is worth more here than completeness, because the
// alternative (grow, or block the engine thread) perturbs the very frame times
// this exists to measure.
#include "dx8wasm/telemetry.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/threading.h>
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

Record                g_ring[DX8WASM_TEL_CAPACITY];
std::atomic<uint32_t> g_write{0};          // next slot to claim (monotonic)
uint32_t              g_read = 0;          // consumer cursor (single thread)
std::atomic<uint32_t> g_dropped{0};
std::atomic<uint32_t> g_seq{0};
uint32_t              g_lastFlushMs = 0;

void copy_field(char* dst, uint32_t cap, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

// Claim a slot and fill the common fields. Returns nullptr if the ring is full.
Record* claim(uint8_t kind, const char* name) {
    const uint32_t slot = g_write.fetch_add(1, std::memory_order_relaxed);
    if (slot - g_read >= DX8WASM_TEL_CAPACITY) {   // unsigned wrap-safe distance
        g_dropped.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    Record* r = &g_ring[slot % DX8WASM_TEL_CAPACITY];
    r->kind = kind;
    r->value = 0;
    r->ms = 0.0;
    copy_field(r->name, DX8WASM_TEL_NAME_MAX, name);
    r->detail[0] = '\0';
    return r;
}

uint32_t now_ms() {
#ifdef __EMSCRIPTEN__
    return (uint32_t)emscripten_get_now();
#else
    return 0;
#endif
}

}  // namespace

extern "C" {

void dx8wasm_tel_log(const char* name, const char* detail) {
    if (Record* r = claim(KLOG, name)) copy_field(r->detail, DX8WASM_TEL_DETAIL_MAX, detail);
}

void dx8wasm_tel_counter(const char* name, uint32_t delta) {
    if (Record* r = claim(KCOUNTER, name)) r->value = delta;
}

void dx8wasm_tel_span(const char* name, double ms) {
    if (Record* r = claim(KSPAN, name)) r->ms = ms;
}

uint32_t dx8wasm_tel_dropped(void) { return g_dropped.load(std::memory_order_relaxed); }

uint32_t dx8wasm_tel_drain(char* out, uint32_t cap) {
    if (!out || cap == 0) return 0;
    uint32_t used = 0;
    out[0] = '\0';
    const uint32_t write = g_write.load(std::memory_order_relaxed);
    while (g_read != write) {
        const Record& r = g_ring[g_read % DX8WASM_TEL_CAPACITY];
        char line[320];
        int n = 0;
        const uint32_t seq = g_seq.fetch_add(1, std::memory_order_relaxed);
        if (r.kind == KLOG) {
            n = std::snprintf(line, sizeof line,
                              "{\"seq\":%u,\"k\":\"log\",\"n\":\"%s\",\"d\":\"%s\"}\n",
                              seq, r.name, r.detail);
        } else if (r.kind == KCOUNTER) {
            n = std::snprintf(line, sizeof line,
                              "{\"seq\":%u,\"k\":\"counter\",\"n\":\"%s\",\"v\":%u}\n",
                              seq, r.name, r.value);
        } else {
            n = std::snprintf(line, sizeof line,
                              "{\"seq\":%u,\"k\":\"span\",\"n\":\"%s\",\"ms\":%g}\n",
                              seq, r.name, r.ms);
        }
        if (n <= 0) { g_read++; continue; }
        if (used + (uint32_t)n + 1 > cap) break;   // leave it queued for next drain
        std::memcpy(out + used, line, (uint32_t)n);
        used += (uint32_t)n;
        out[used] = '\0';
        g_read++;
    }
    return used;
}

void dx8wasm_tel_pump(void) {
    const uint32_t t = now_ms();
    if (g_lastFlushMs != 0 && t - g_lastFlushMs < DX8WASM_TEL_FLUSH_MS) return;
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
