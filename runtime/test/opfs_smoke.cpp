// SPDX-License-Identifier: GPL-3.0-only
// Exercises the OPFS on-demand read protocol without a browser worker and without
// threads: a test responder fills the reply in place, exactly as web/opfs-io-worker.js
// would. The protocol (chunking across the 4 MiB window, contiguity across chunks,
// short reads, bad indices) is the risky part of the feature and is the part that a
// full-stack harness reports least clearly, so it is pinned here.
//
// Why this is an Emscripten smoke rather than a host binary: this repo has no host
// build at all (CMakeLists.txt hard-fails unless EMSCRIPTEN), so "the unit test" here
// means a wasm module run through the same headless harness as every other smoke. It
// is built WITHOUT -pthread, like the rest of the SDK's targets, which is exactly why
// the real Atomics.wait path is compiled out (see opfs_bridge.cpp) — that path needs
// shared memory and a non-main thread, neither of which exists in this target.
// Reports [1,0,0,255] on pass.
#include "dx8wasm/opfs.h"
#include "../platform/opfs_internal.h"
#include <emscripten.h>
#include <cstdio>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { report_error(msg); g_fail = 1; return 1; } } while (0)

// Stand-in for the I/O worker. Serves a synthetic archive whose byte i is (i & 0xff),
// so a wrong offset, a dropped chunk or a doubled chunk all show up as a value
// mismatch rather than as a plausible-looking buffer.
static const char* const kNames[2] = { "INIZH.big", "W3DZH.big" };
static const int32_t     kSizes[2] = { GX_OPFS_WINDOW + 4096, 2000 };

static void fakeWorker(GxOpfsBlock* b) {
  if (b->hdr[GX_OPFS_STATE] != 1) return;
  b->hdr[GX_OPFS_ERR] = 0;
  if (b->hdr[GX_OPFS_OP] == GX_OPFS_OP_LIST) {
    b->hdr[GX_OPFS_RESULT] = gx_opfs_encode_list(b, kNames, kSizes, 2);
  } else {
    int idx = b->hdr[GX_OPFS_IDX];
    if (idx < 0 || idx >= 2) {                       // the worker validates too
      b->hdr[GX_OPFS_ERR] = GX_OPFS_ERR_BAD_INDEX;
      b->hdr[GX_OPFS_RESULT] = 0;
    } else {
      uint32_t off = (uint32_t)b->hdr[GX_OPFS_OFFSET];
      uint32_t len = (uint32_t)b->hdr[GX_OPFS_LENGTH];
      uint32_t end = (uint32_t)kSizes[idx];
      if (off >= end) len = 0;
      else if (off + len > end) len = end - off;     // short read at EOF, not an error
      unsigned char* w = gx_opfs_window(b);
      for (uint32_t i = 0; i < len; i++) w[i] = (unsigned char)((off + i) & 0xff);
      b->hdr[GX_OPFS_RESULT] = (int32_t)len;
    }
  }
  b->hdr[GX_OPFS_EPOCH]++;
  b->hdr[GX_OPFS_STATE] = 2;
}

static unsigned char g_big[GX_OPFS_WINDOW + 1024];

int main() {
  CHECK(dx8wasm_opfs_init() == 1, "opfs_init did not allocate the control block");
  CHECK(dx8wasm_opfs_init() == 0, "a second opfs_init must be a no-op, not a re-allocation");
  CHECK(dx8wasm_opfs_control_addr() != 0, "control_addr is 0 after init");
  GxOpfsBlock* b = gx_opfs_block();
  CHECK(b != nullptr, "gx_opfs_block returned null after init");
  gx_opfs_set_test_responder(fakeWorker);

  // --- the list handshake -----------------------------------------------------
  CHECK(dx8wasm_opfs_ready() == 1, "not ready after the list exchange");
  CHECK(dx8wasm_opfs_count() == 2, "expected 2 registered archives");
  CHECK(dx8wasm_opfs_index_of("INIZH.big") == 0, "INIZH.big is not index 0");
  CHECK(dx8wasm_opfs_index_of("W3DZH.big") == 1, "W3DZH.big is not index 1");
  CHECK(dx8wasm_opfs_index_of("nope.big") == -1, "an unregistered name must not resolve");
  CHECK(dx8wasm_opfs_size_of(1) == 2000, "wrong size decoded for index 1");
  CHECK(dx8wasm_opfs_size_of(99) == -1, "a bad index must report size -1");

  // --- a read smaller than the window ----------------------------------------
  unsigned char buf[300];
  CHECK(dx8wasm_opfs_read(0, 7, buf, sizeof(buf)) == (int)sizeof(buf), "short read: wrong byte count");
  for (size_t i = 0; i < sizeof(buf); i++) {
    CHECK(buf[i] == (unsigned char)((7 + i) & 0xff), "short read: wrong bytes (offset not honoured?)");
  }

  // --- a read LARGER than the window: must be chunked and still contiguous ----
  int n = dx8wasm_opfs_read(0, 0, g_big, (uint32_t)sizeof(g_big));
  CHECK(n == (int)sizeof(g_big), "chunked read: wrong total byte count");
  for (size_t i = 0; i < sizeof(g_big); i++) {
    CHECK(g_big[i] == (unsigned char)(i & 0xff), "chunked read: discontinuity at a window boundary");
  }

  // --- a read straddling EOF returns a SHORT count, not an error --------------
  int tail = dx8wasm_opfs_read(1, 1900, buf, 300);
  CHECK(tail == 100, "read past EOF must return the available bytes, not an error");

  // --- entirely past EOF is 0 bytes, still not an error ----------------------
  CHECK(dx8wasm_opfs_read(1, 5000, buf, 16) == 0, "read wholly past EOF must return 0");

  // --- a bad index fails, and does not wedge the block ----------------------
  CHECK(dx8wasm_opfs_read(99, 0, buf, 16) == -1, "a bad index must return -1");
  CHECK(b->hdr[GX_OPFS_STATE] == 0, "the block must be left idle after a failed read");
  CHECK(dx8wasm_opfs_read(0, 0, buf, 16) == 16, "a good read after a failed one must still work");

  // --- a null destination is not a crash -----------------------------------
  CHECK(dx8wasm_opfs_read(0, 0, nullptr, 16) == -1, "a null destination must be refused");

  printf("opfs_smoke: PASS\n");
  report_pixel(1, 0, 0, 255);
  return 0;
}
