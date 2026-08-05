// SPDX-License-Identifier: GPL-3.0-only
//
// dx8wasm OPFS bridge — ranged reads of large read-only files (game archives) served
// from the Origin Private File System on demand, so they need not be resident in the
// wasm heap for the whole session.
//
// Why this lives in the SDK and not behind the filesystem layer. Under -pthread with
// PROXY_TO_PTHREAD the engine runs on a Web Worker, and a *worker* may hold an OPFS
// sync access handle and may block — measured at 1385 MB/s while blocked inside wasm.
// A read() syscall's JS handler, however, runs on the MAIN browser thread (measured;
// see generals-dx8wasm scripts/opfs-probe/fsthread-probe.cpp), where
// createSyncAccessHandle does not exist and blocking is forbidden. So this cannot be an
// Emscripten filesystem backend or an FS.registerDevice handler: the interception has
// to sit one layer up, at the caller, which IS the engine thread. That is what this API
// is — the caller blocks here, on the engine thread, and never anywhere else.
//
// Mechanics: one control block (an int32 header followed by a data window) is allocated
// in the wasm heap, which under -pthread is already a SharedArrayBuffer, so a dedicated
// I/O worker reaches it from a (wasmMemory, offset) pair with no separate SAB. The
// caller writes a request, notifies, and sleeps on the header; the I/O worker — the only
// thread that touches OPFS handles — fills the window and stores 'done'.
//
// Failure contract, in the spirit of the rest of this SDK: never lie, never hang. A
// wedged worker times out (GX_OPFS_TIMEOUT_MS), counts "opfs.read.timeout" and returns
// an error to the caller; it does not retry silently and it does not stall forever. A
// caller that gets -1 is expected to fall back to whatever it did before.
#ifndef DX8WASM_OPFS_H
#define DX8WASM_OPFS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Allocate the control block. Returns 1 if this call allocated it, 0 if it already
// existed (or if allocation failed — check dx8wasm_opfs_control_addr() to tell those
// apart). Must be called before the page starts the I/O worker.
int dx8wasm_opfs_init(void);

// Address of the control block, for the page to hand to the I/O worker as
// { memory: wasmMemory.buffer, addr }. 0 until dx8wasm_opfs_init() succeeds.
uintptr_t dx8wasm_opfs_control_addr(void);

// 1 once the worker has answered the archive-list request. This doubles as the feature
// flag: with no worker attached it stays 0 and every caller takes its old path. It is
// cheap after the first call but NOT free on the first — it performs the list exchange,
// so do not call it from a hot loop before boot.
int dx8wasm_opfs_ready(void);

// Number of registered archives, or 0 if the list exchange has not succeeded.
int dx8wasm_opfs_count(void);

// Index of a registered archive by base name (no directory part), or -1.
int dx8wasm_opfs_index_of(const char* name);

// Size of a registered archive in bytes, or -1 for a bad index.
int dx8wasm_opfs_size_of(int idx);

// Ranged read into `dst`. Returns bytes read — which may be less than `len` at end of
// archive — or -1 on a bad index, a null destination, a worker error or a timeout.
// Requests larger than the data window are chunked internally; the result is
// contiguous. Callable only from a thread that may block (i.e. not the main browser
// thread).
int dx8wasm_opfs_read(int idx, uint32_t offset, void* dst, uint32_t len);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // DX8WASM_OPFS_H
