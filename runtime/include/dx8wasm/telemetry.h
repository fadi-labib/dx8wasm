// SPDX-License-Identifier: GPL-3.0-only
//
// dx8wasm telemetry — the supported way for engine and runtime code to get numbers
// and diagnostics out of a wasm build.
//
// Why this exists: under Emscripten with PROXY_TO_PTHREAD the engine runs on a Web
// Worker, and fprintf(stderr) from that worker never reaches the page console. Every
// diagnostic the engine already writes (replay CRC mismatches, map-load failures) is
// therefore invisible in the browser, and the workaround used while debugging —
// MAIN_THREAD_EM_ASM at each call site — costs a cross-thread proxy per line, which
// is unusable for anything per-frame.
//
// So: producers append fixed-size records to a lock-free ring (no allocation, no JS
// call, never blocks); the platform seam drains the ring to the page once a second.
// A full ring drops records and counts the drops — the same honest contract as
// SDL3Mouse's event buffer, and the same rule as the rest of this SDK: never lie
// about what happened (docs/SDK_REFERENCE.md, "Stubs fail loudly").
#ifndef DX8WASM_TELEMETRY_H
#define DX8WASM_TELEMETRY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DX8WASM_TEL_CAPACITY  1024   // records; ~144 KB of static storage
#define DX8WASM_TEL_NAME_MAX  32     // including NUL; longer names are truncated
#define DX8WASM_TEL_DETAIL_MAX 96    // including NUL; longer details are truncated

// --- Producers (any thread; never block, never allocate) ---------------------

// A free-text diagnostic. `name` is a stable dotted key ("map.load_failed"), `detail`
// the variable part. Prefer a counter or span if the thing is countable or timed.
void dx8wasm_tel_log(const char* name, const char* detail);

// Add `delta` to a named counter. Emitted as one record per call; the reducer sums.
void dx8wasm_tel_counter(const char* name, uint32_t delta);

// A completed timed region, in milliseconds.
void dx8wasm_tel_span(const char* name, double ms);

// --- Consumer (single thread — the one that owns the GL context) -------------

// Serialise queued records as NDJSON into `out` (always NUL-terminated) and remove
// them from the ring. Returns bytes written, 0 if nothing was queued. Records that
// do not fit in `cap` stay queued for the next call.
//
// `cap` must be large enough to hold at least one serialised record, or drain makes
// no forward progress: it will keep returning 0 (indistinguishable from "nothing
// queued") while the un-fitting record sits at the head of the ring forever. A log
// record with a maximum-length name and detail, every byte of which needs a 6-byte
// \u00XX escape, serialises to at most ~810 bytes with the current NAME_MAX/
// DETAIL_MAX — pass a `cap` of 1024 or more to stay clear of that edge.
uint32_t dx8wasm_tel_drain(char* out, uint32_t cap);

// Drain and hand the NDJSON to the page: calls window.gxTelemetry(text) on the main
// thread if that function exists, otherwise writes to stdout. Rate-limited
// internally to one flush per DX8WASM_TEL_FLUSH_MS; call it every frame.
void dx8wasm_tel_pump(void);
#define DX8WASM_TEL_FLUSH_MS 1000

// Records dropped because the ring was full, since process start. Non-zero means the
// telemetry itself lost data — treat it as a failed measurement, not a warning.
uint32_t dx8wasm_tel_dropped(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // DX8WASM_TELEMETRY_H
