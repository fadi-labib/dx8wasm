// SPDX-License-Identifier: GPL-3.0-only
// See runtime/include/dx8wasm/opfs.h for why this exists and why it is not a
// filesystem backend, and runtime/platform/opfs_internal.h for the wire format.
#include "dx8wasm/opfs.h"
#include "dx8wasm/telemetry.h"
#include "opfs_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
// The real handshake needs shared memory (memory.atomic.wait32 is only valid against a
// shared memory, and only off the main thread). __EMSCRIPTEN_PTHREADS__ is defined only
// when the translation unit is compiled with -pthread — true for the engine's
// dx8wasm_backend target, false for this SDK's own smoke targets. Keying on it rather
// than on __EMSCRIPTEN__ is what lets the protocol be unit-tested at all: without it the
// atomics path would not even compile into the non-pthread smoke.
#if defined(__EMSCRIPTEN_PTHREADS__)
#include <emscripten/atomic.h>
#include <emscripten/threading.h>
#endif

static GxOpfsBlock*    g_block = nullptr;
static GxOpfsResponder g_testResponder = nullptr;
static char            g_names[GX_OPFS_MAX][GX_OPFS_NAME_MAX];
static int32_t         g_sizes[GX_OPFS_MAX];
static int             g_count = 0;
static bool            g_listed = false;
static bool            g_listFailed = false;
static uint32_t        g_pendCount = 0;    // reads not yet reported to telemetry (see below)
static uint32_t        g_pendBytes = 0;
#define GX_OPFS_TEL_BATCH 64

extern "C" {

GxOpfsBlock* gx_opfs_block(void) { return g_block; }

unsigned char* gx_opfs_window(GxOpfsBlock* b) {
  return reinterpret_cast<unsigned char*>(b) + sizeof(int32_t) * GX_OPFS_HDR_INTS;
}

void gx_opfs_set_test_responder(GxOpfsResponder r) { g_testResponder = r; }

int dx8wasm_opfs_init(void) {
  if (g_block) return 0;
  // One allocation: header then window, contiguous. It lives in the wasm heap, which is
  // shared memory under -pthread, so the I/O worker can view it as
  // new Int32Array(wasmMemory.buffer, addr, GX_OPFS_HDR_INTS) with no copy and no
  // separate SharedArrayBuffer. calloc's alignment is at least 8, which satisfies the
  // 4-byte alignment an Int32Array view requires.
  g_block = static_cast<GxOpfsBlock*>(calloc(1, sizeof(int32_t) * GX_OPFS_HDR_INTS + GX_OPFS_WINDOW));
  return g_block ? 1 : 0;
}

uintptr_t dx8wasm_opfs_control_addr(void) { return reinterpret_cast<uintptr_t>(g_block); }

}  // extern "C"

// Publish the request already staged in the header and block until the worker replies.
// Returns true on a reply, false on timeout (or if no responder exists in a test build).
static bool exchange(void) {
  GxOpfsBlock* b = g_block;
  if (!b) return false;
  if (g_testResponder) {          // unit-test path: no worker, no threads, no atomics
    b->hdr[GX_OPFS_STATE] = 1;
    g_testResponder(b);
    return b->hdr[GX_OPFS_STATE] == 2;
  }
#if defined(__EMSCRIPTEN_PTHREADS__)
  const int32_t epoch = __atomic_load_n(&b->hdr[GX_OPFS_EPOCH], __ATOMIC_SEQ_CST);
  __atomic_store_n(&b->hdr[GX_OPFS_STATE], 1, __ATOMIC_SEQ_CST);
  emscripten_atomic_notify(&b->hdr[GX_OPFS_STATE], 1);
  // Blocking here is legal ONLY because this runs on the engine thread, which is a Web
  // Worker. The wait is sliced at 50 ms so the deadline is enforced by our own clock
  // rather than trusted to the instruction's timeout, and so a missed notify (the worker
  // storing 'done' between our load and our wait) costs 50 ms instead of 5 s.
  const double deadline = emscripten_get_now() + GX_OPFS_TIMEOUT_MS;
  for (;;) {
    const int32_t state = __atomic_load_n(&b->hdr[GX_OPFS_STATE], __ATOMIC_SEQ_CST);
    if (state != 1) return state == 2;
    emscripten_atomic_wait_u32(&b->hdr[GX_OPFS_STATE], 1, 50ll * 1000 * 1000);
    if (emscripten_get_now() > deadline) {
      // Loud, not a retry loop and not a silent stall: this project already lost a day
      // to a quiet main-thread hang (the .scb stall), so a wedged worker must be
      // visible in telemetry and must return an error to the caller.
      if (__atomic_load_n(&b->hdr[GX_OPFS_EPOCH], __ATOMIC_SEQ_CST) == epoch) {
        dx8wasm_tel_counter("opfs.read.timeout", 1);
        dx8wasm_tel_log("opfs.worker_stalled", "no reply within 5000 ms");
      }
      __atomic_store_n(&b->hdr[GX_OPFS_STATE], 0, __ATOMIC_SEQ_CST);
      return false;
    }
  }
#else
  return false;   // no worker and no responder: the feature is simply off
#endif
}

static void ensureList(void) {
  if (g_listed || g_listFailed || !g_block) return;
  g_block->hdr[GX_OPFS_OP] = GX_OPFS_OP_LIST;
  if (!exchange()) { g_listFailed = true; return; }
  if (g_block->hdr[GX_OPFS_ERR] == GX_OPFS_ERR_NONE) {
    g_count = gx_opfs_decode_list(g_block, g_names, g_sizes, GX_OPFS_MAX);
    g_listed = g_count > 0;
  }
  if (!g_listed) g_listFailed = true;
  g_block->hdr[GX_OPFS_STATE] = 0;
}

extern "C" {

int dx8wasm_opfs_ready(void) { ensureList(); return g_listed ? 1 : 0; }
int dx8wasm_opfs_count(void) { ensureList(); return g_count; }

int dx8wasm_opfs_index_of(const char* name) {
  ensureList();
  if (!name) return -1;
  for (int i = 0; i < g_count; i++) {
    if (strcmp(g_names[i], name) == 0) return i;
  }
  return -1;
}

int dx8wasm_opfs_size_of(int idx) {
  ensureList();
  return (idx >= 0 && idx < g_count) ? g_sizes[idx] : -1;
}

int dx8wasm_opfs_read(int idx, uint32_t offset, void* dst, uint32_t len) {
  ensureList();
  if (!g_listed || idx < 0 || idx >= g_count || !dst) return -1;
  uint32_t done = 0;
  while (done < len) {
    uint32_t chunk = len - done;
    if (chunk > GX_OPFS_WINDOW) chunk = GX_OPFS_WINDOW;
    g_block->hdr[GX_OPFS_OP]     = GX_OPFS_OP_READ;
    g_block->hdr[GX_OPFS_IDX]    = idx;
    g_block->hdr[GX_OPFS_OFFSET] = static_cast<int32_t>(offset + done);
    g_block->hdr[GX_OPFS_LENGTH] = static_cast<int32_t>(chunk);
    const bool ok = exchange();
    const int32_t err = g_block->hdr[GX_OPFS_ERR];
    const int32_t got = g_block->hdr[GX_OPFS_RESULT];
    g_block->hdr[GX_OPFS_STATE] = 0;      // leave the block idle whatever happened
    if (!ok || err != GX_OPFS_ERR_NONE) return -1;
    if (got <= 0) break;                  // at or past end of archive
    memcpy(static_cast<unsigned char*>(dst) + done, gx_opfs_window(g_block), static_cast<size_t>(got));
    done += static_cast<uint32_t>(got);
    // Batch the counters. One record per read floods the 1024-entry telemetry ring, and a full
    // ring drops records and reports "tel.dropped" — which by this SDK's own contract invalidates
    // every measurement in that window, not just these two. This is not hypothetical: the game
    // engine's archive-table parse issues reads by the hundred thousand (it reads each archived
    // filename one byte at a time), and the first full-stack run of this feature reported 845
    // reads for what was certainly far more. The reducer sums counter deltas, so a batched delta
    // is exactly as accurate as N separate ones; the only cost is that up to GX_OPFS_TEL_BATCH-1
    // reads go unreported if the run ends mid-batch.
    g_pendCount++;
    g_pendBytes += static_cast<uint32_t>(got);
    if (g_pendCount >= GX_OPFS_TEL_BATCH || g_pendBytes >= 1024u * 1024u) {
      dx8wasm_tel_counter("opfs.read.count", g_pendCount);
      dx8wasm_tel_counter("opfs.read.bytes", g_pendBytes);
      g_pendCount = 0;
      g_pendBytes = 0;
    }
    if (static_cast<uint32_t>(got) < chunk) break;   // short read: end of archive
  }
  return static_cast<int>(done);
}

// --- list wire format -------------------------------------------------------------

int32_t gx_opfs_encode_list(GxOpfsBlock* b, const char* const* names, const int32_t* sizes, int n) {
  if (!b || !names || !sizes) return 0;
  unsigned char* w = gx_opfs_window(b);
  int32_t at = 0;
  for (int i = 0; i < n; i++) {
    char num[16];
    const int nameLen = static_cast<int>(strlen(names[i]));
    const int numLen = snprintf(num, sizeof(num), "%d", sizes[i]);
    if (at + nameLen + numLen + 2 > GX_OPFS_WINDOW) return 0;
    memcpy(w + at, names[i], static_cast<size_t>(nameLen) + 1);
    at += nameLen + 1;
    memcpy(w + at, num, static_cast<size_t>(numLen) + 1);
    at += numLen + 1;
  }
  return at;
}

int gx_opfs_decode_list(GxOpfsBlock* b, char names[][GX_OPFS_NAME_MAX], int32_t* sizes, int max) {
  if (!b || !names || !sizes) return 0;
  const unsigned char* w = gx_opfs_window(b);
  int32_t total = b->hdr[GX_OPFS_RESULT];
  if (total <= 0 || total > GX_OPFS_WINDOW) return 0;
  int32_t at = 0, n = 0;
  while (at < total && n < max) {
    const char* name = reinterpret_cast<const char*>(w + at);
    const int32_t nameLen = static_cast<int32_t>(strnlen(name, static_cast<size_t>(total - at)));
    if (at + nameLen >= total) return n;             // unterminated: stop, keep what parsed
    at += nameLen + 1;
    const char* num = reinterpret_cast<const char*>(w + at);
    const int32_t numLen = static_cast<int32_t>(strnlen(num, static_cast<size_t>(total - at)));
    if (at + numLen >= total) return n;
    at += numLen + 1;
    if (nameLen == 0 || nameLen >= GX_OPFS_NAME_MAX) continue;   // skip, do not truncate silently
    memcpy(names[n], name, static_cast<size_t>(nameLen) + 1);
    sizes[n] = static_cast<int32_t>(strtol(num, nullptr, 10));
    n++;
  }
  return n;
}

}  // extern "C"
