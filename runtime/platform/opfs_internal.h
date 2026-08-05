// SPDX-License-Identifier: GPL-3.0-only
//
// Wire format shared between opfs_bridge.cpp (this side of the handshake) and
// web/opfs-io-worker.js in the game repo (the other side). The JS mirrors these slot
// numbers by hand — there is no generator — so any change here is a change there, and
// the smoke in runtime/test/opfs_smoke.cpp plus the game's opfs-worker-test.mjs are
// what keep the two honest.
//
// Also exposed here (rather than in the public header) is the test seam: a responder
// callback that stands in for the I/O worker, so the protocol can be tested without a
// worker, without shared memory and without threads.
#ifndef DX8WASM_OPFS_INTERNAL_H
#define DX8WASM_OPFS_INTERNAL_H

#include <stdint.h>

// Header slots. Int32 each, in this order, at the start of the control block.
enum {
  GX_OPFS_STATE  = 0,   // 0 idle, 1 request pending, 2 done, 3 error
  GX_OPFS_OP     = 1,   // GX_OPFS_OP_*
  GX_OPFS_IDX    = 2,   // archive index
  GX_OPFS_OFFSET = 3,   // byte offset within the archive
  GX_OPFS_LENGTH = 4,   // bytes requested
  GX_OPFS_RESULT = 5,   // bytes actually written into the window
  GX_OPFS_ERR    = 6,   // GX_OPFS_ERR_*
  GX_OPFS_EPOCH  = 7,   // bumped by the worker on every reply; a stalled epoch means a dead worker
};

enum { GX_OPFS_OP_READ = 1, GX_OPFS_OP_LIST = 2 };
enum { GX_OPFS_ERR_NONE = 0, GX_OPFS_ERR_BAD_INDEX = 1, GX_OPFS_ERR_RANGE = 2, GX_OPFS_ERR_READ = 3 };

#define GX_OPFS_HDR_INTS   8
#define GX_OPFS_WINDOW     (4 * 1024 * 1024)
#define GX_OPFS_TIMEOUT_MS 5000
#define GX_OPFS_MAX        64    // registered archives; the BYO set is 22 today
#define GX_OPFS_NAME_MAX   64    // including NUL

// The control block: header, then GX_OPFS_WINDOW bytes of data window immediately
// after it. One allocation, so the page needs one address for both.
typedef struct GxOpfsBlock {
  int32_t hdr[GX_OPFS_HDR_INTS];
} GxOpfsBlock;

typedef void (*GxOpfsResponder)(GxOpfsBlock*);

#ifdef __cplusplus
extern "C" {
#endif

GxOpfsBlock*   gx_opfs_block(void);
unsigned char* gx_opfs_window(GxOpfsBlock* b);

// Test seam: when set, this is called instead of waking a worker and blocking. Used by
// runtime/test/opfs_smoke.cpp; in a real build nothing sets it and it stays null.
void gx_opfs_set_test_responder(GxOpfsResponder r);

// List wire format, in the data window: for each archive, the NUL-terminated name
// followed by the NUL-terminated size in decimal. Chosen over a binary struct because
// the producer is hand-written JS and a mismatch in a text format is legible in a hex
// dump; the list is exchanged once per session, so its cost is irrelevant.
// encode returns bytes written (0 if the window is too small); decode returns entries.
int32_t gx_opfs_encode_list(GxOpfsBlock* b, const char* const* names, const int32_t* sizes, int n);
int     gx_opfs_decode_list(GxOpfsBlock* b, char names[][GX_OPFS_NAME_MAX], int32_t* sizes, int max);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // DX8WASM_OPFS_INTERNAL_H
