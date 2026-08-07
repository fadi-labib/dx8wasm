// SPDX-License-Identifier: GPL-3.0-only
// The d3d8webgl device. Implements the FULL D3D8 COM vtable (so a game links and
// dispatches correctly); the supported subset does real work, the rest are honest
// stubs (log-once / coverage / sensible defaults) pending Phase C.
#include "d3d8/d3d8.h"
#include "caps_fill.h"   // shared fill_caps() — device caps must match IDirect3D8's
#include "format_support.h"   // shared format predicates — CheckDeviceFormat answers from these
#include "platform/platform.h"
#include "graphics-ff/ff_shader.h"
#include "coverage/coverage.h"
#include "dx8wasm/telemetry.h"
#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

// Debug counters (integration bring-up): draw submissions, bind-pipeline failures,
// and clears. Exported so the platform seam's frame probe can report them.
long g_dx8_draws = 0, g_dx8_bindfail = 0, g_dx8_clears = 0;
extern "C" void dx8wasm_debug_counts(long* draws, long* bindfail, long* clears) {
  if (draws) *draws = g_dx8_draws; if (bindfail) *bindfail = g_dx8_bindfail; if (clears) *clears = g_dx8_clears;
}

namespace {

// EXT_texture_filter_anisotropic, resolved once. WebGL requires an extension to be *enabled*
// on the context before its tokens do anything, which is why this goes through
// emscripten_webgl_enable_extension rather than just calling glTexParameterf and hoping.
// Returns the device's max anisotropy, or 0 when the extension is unavailable — 0 reads as
// "no anisotropy possible", never as "1x was requested".
constexpr GLenum kTextureMaxAnisotropyExt = 0x84FE;
constexpr GLenum kMaxTextureMaxAnisotropyExt = 0x84FF;
float aniso_limit() {
  static float limit = -1.0f;
  if (limit >= 0.0f) return limit;
  limit = 0.0f;
  if (emscripten_webgl_enable_extension(emscripten_webgl_get_current_context(),
                                        "EXT_texture_filter_anisotropic")) {
    GLfloat max = 0.0f;
    glGetFloatv(kMaxTextureMaxAnisotropyExt, &max);
    if (max > 1.0f) limit = max;
  }
  return limit;
}

void warn_once(const char* what) {   // one line per distinct unimplemented method
  static const char* seen[64]; static int n = 0;
  for (int i = 0; i < n; i++) if (seen[i] == what) return;
  if (n < 64) seen[n++] = what;
  std::fprintf(stderr, "[dx8wasm] %s: stubbed (Phase C)\n", what);
}

// --- per-frame cost accounting for paths OUTSIDE the draw calls ---------------------------
// Round 1 of this instrumentation showed draws are ~3% of the frame (264/frame, 0.79 ms) and the
// swap 0.01 ms, while the frame differs by 24 ms between ANGLE backends. So the cost is in the
// calls that were COUNTED but not TIMED. These accumulate it:
//   gl.state_ms   the ~750 SetRenderState/SetTexture/SetTextureStageState/SetTransform per frame
//   gl.upload_ms  texture uploads (UnlockRect -> glTexImage2D) and static buffer uploads
//                 (GLBuffer::Unlock -> glBufferData). The DrawPrimitiveUP paths also upload, but
//                 those are inside the draw functions and already counted in gl.draw_ms.
//
// File-scope because GLBuffer and the texture class have no Device8 to hang a member on. Safe
// because every GL call in this backend happens on the one engine thread (PROXY_TO_PTHREAD).
// Doubles throughout, never cast to a 32-bit integer -- that saturating fptoui is what silently
// killed the telemetry pump once (runtime/telemetry/telemetry.cpp, now_ms()).
double   g_frameStateMs = 0.0;
double   g_frameUploadMs = 0.0;
uint32_t g_frameUploads = 0;

// --- emission rate -------------------------------------------------------------------------
// These gauges are emitted ONCE PER SECOND as per-frame averages, not once per frame.
//
// Why: the telemetry ring is DX8WASM_TEL_CAPACITY (1024) records drained every
// DX8WASM_TEL_FLUSH_MS (1000 ms). The engine already emits ~3 records/frame (frame.client,
// frame.logic, logic.frame). Adding 9 more per frame is 12 x fps per flush -- 720 at 60 fps,
// which fits, and 1560 at 130 fps, which does NOT. It overflowed the ring and
// options-roundtrip-test failed on "telemetry ring dropped 24693 record(s)", correctly refusing
// to trust counts it knew were incomplete.
//
// The irony is the useful part: the GL_DYNAMIC_DRAW fix made the game fast enough that the
// instrumentation measuring it broke the telemetry it reports through. Instrumentation has to
// budget for the best case, not the case that motivated it.
//
// Averaging loses per-frame percentiles. That is an acceptable default: a measurement run that
// wants p95 can raise DX8WASM_TEL_CAPACITY or shorten the window, while every ordinary run now
// carries its own frame breakdown for ~9 records/second instead of ~1500.
double   g_accFrames = 0.0;
double   g_accDrawMs = 0.0, g_accStateMs = 0.0, g_accTexMs = 0.0, g_accBufMs = 0.0, g_accPresentMs = 0.0;
double   g_accDraws = 0.0, g_accStateCalls = 0.0, g_accTexUploads = 0.0, g_accBufUploads = 0.0;
double   g_lastEmitMs = 0.0;
// Split out, because "uploads are 92% of the frame" is a diagnosis and "which kind" is a fix.
// tex = UnlockRect -> glTexImage2D; buf = GLBuffer::Unlock -> glBufferData (the whole staging
// vector, every Unlock, with a STATIC_DRAW hint -- a prime suspect for a per-frame dynamic VB).
double   g_frameTexUploadMs = 0.0;
double   g_frameBufUploadMs = 0.0;
uint32_t g_frameTexUploads = 0;
uint32_t g_frameBufUploads = 0;

// --- Stage A: what does the engine actually LOCK? -------------------------------------------
// "Uploads are 63% of the frame" is a diagnosis; "and 90% of every upload was not even written
// to" would be a fix. Nothing has measured the second half, so the pending change to honour the
// locked range rests on a belief. This measures it before anything acts on it.
//
// Two numbers decide it, and BOTH are required:
//   1. waste_ratio = bytes_uploaded / bytes_locked. Predicted saving is
//      buf_upload_ms * (1 - 1/waste_ratio); the bar is >= 1.5 ms before a change that can alter
//      what is drawn is worth making.
//   2. Whether the cost is per-BYTE or per-CALL at all. AB-06 established the 461 us upload was a
//      pipeline STALL, and stall cost does not scale with bytes. GL_DYNAMIC_DRAW already attacked
//      that. If per-upload time stays flat across scenes while bytes_uploaded swings, cutting
//      bytes buys nothing and the change is dead regardless of how large the waste ratio is.
//
// Condition 2 is the one a waste-ratio-only instrument walks straight past -- it would report a
// large waste, justify the fix, and save nothing. AB-05 is what that costs.
//
// waste_ratio is a ratio of SUMS, not a mean of per-upload ratios, so a 200 KB buffer with a 2 KB
// lock is not averaged out by a dozen small fully-written ones.
double   g_accBufBytesUploaded = 0.0, g_accBufBytesLocked = 0.0;
double   g_accBufWholeLocks = 0.0, g_accBufDiscard = 0.0, g_accBufNoOverwrite = 0.0;
// Defect counters, NOT per-frame averages -- see the emit site.
double   g_accBufLockOob = 0.0, g_accBufUnlockUnmatched = 0.0;

// D3DLOCK_* now live in runtime/d3d8/d3d8.h. They were briefly defined here, on the reasoning that
// the SDK's public surface should not grow for the sake of an instrument -- but once Unlock started
// acting on the locked range they became part of the contract a caller must be able to express, and
// runtime/test/buffer_range_smoke.cpp needs them to drive the sub-range path.

// --- GPU frame time: what is the 7.56 ms OUTSIDE the engine? --------------------------------
// Even uncapped, client + logic + present is 3.81 ms of an 11.36 ms frame (AB-24); the other
// two thirds is not engine code. Every gl.* gauge above times the API *call*, not the GPU work
// it queues, so GPU execution necessarily lands in that gap -- along with canvas compositing
// and rAF scheduling, and nothing has ever separated the three.
//
// EXT_disjoint_timer_query_webgl2 brackets the frame's whole GL command stream: begin right
// after present N, end right after present N+1, so every command of frame N+1 including its
// swap is inside. gl.gpu_ms is therefore GPU execution of OUR commands only -- compositing is
// the browser drawing the canvas into the page and is deliberately outside the bracket. That
// is exactly the split the number exists to make. The extension was probed available AND
// functional on this rig (ANGLE/GL and ANGLE/Vulkan, RTX 4080: a 200-draw workload returned
// 52-56 us, result available within 10 ms) BEFORE this was designed around it, the way
// glMapBufferRange was checked (AB-18). NOTE the emscripten trap found in the same probe: the
// gl*EXT query entry points (glGenQueriesEXT et al.) call createQueryEXT()/beginQueryEXT(),
// which exist only on the WebGL1 extension object -- on a WebGL2 context the CORE ES3 query
// API must be used with the EXT enum, and only the 64-bit result read goes through the EXT
// entry point (whose WebGL2 branch correctly calls getQueryParameter).
//
// Decision rule, fixed BEFORE the first capture (IM-03), against the uncapped menu frame
// (11.36 ms period, 7.56 ms unexplained):
//   gpu_ms >= 4.5 ms (>=60% of the gap) -> the port is GPU-bound uncapped; the question closes
//                                          as "GPU execution", and any future uncapped-fps work
//                                          targets GPU load, not engine CPU.
//   gpu_ms <= 1.5 ms (<=20% of the gap) -> the gap is compositing/scheduling, unaddressable
//                                          from inside the engine; the question closes as
//                                          "browser overhead" and strengthens the IM-16
//                                          demotion of client-side scene work.
//   in between                          -> report the split and STOP; neither branch justifies
//                                          further work at 60 Hz.
// Every branch CLOSES the item (IM-02): what this instrument can kill is any future plan to
// buy uncapped fps from engine CPU work, and equally any untested "it's the browser" claim.
//
// Caveat carried from 2026-08-06: the 4080 idled at P8/210 MHz mid-skirmish when the CPU was
// the bottleneck. The timer measures at whatever clock the GPU actually ran, so a downclocked
// GPU reports honestly larger times -- that is the reality being measured, not an error term.
// Clock changes can surface as GPU_DISJOINT events; those samples are dropped and counted.
//
// Validation counter (IM-05): gl.gpu_frames is the harvested-sample denominator and reads
// EXACTLY ZERO if any of the plumbing is broken, so a capture where gpu_ms looks plausible but
// gpu_frames is 0 or tiny is the instrument failing, not the GPU being fast.
#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif
// Provided by emscripten's libwebgl; the WebGL2 branch reads via getQueryParameter and writes
// an i53 into the out pointer. Declared here because GLES3/gl3.h carries neither the EXT enums
// nor this entry point, and including GLES2/gl2ext.h alongside gl3.h buys those two lines at
// the cost of a header-compatibility question this file does not need to answer.
extern "C" void glGetQueryObjectui64vEXT(GLuint id, GLenum pname, GLuint64* params);

constexpr int GPU_TQ_RING = 8;   // results arrive ~1 frame later; 8 absorbs a slow harvest
GLuint  g_gpuTq[GPU_TQ_RING] = {};
bool    g_gpuTqInFlight[GPU_TQ_RING] = {};
int     g_gpuTqHead = 0;         // next slot to begin a query in
int     g_gpuTqTail = 0;         // oldest in-flight slot, harvested in order
int     g_gpuTqActive = -1;      // slot whose query is currently open, -1 if none
int     g_gpuTqState = 0;        // 0 unprobed, 1 supported, -1 unsupported (SwiftShader)
int     g_gpuTqTainted = 0;      // in-flight results to DROP after a disjoint event
double  g_accGpuMs = 0.0;        // sum of harvested GPU-elapsed time, ms
double  g_accGpuFrames = 0.0;    // harvested samples -- the denominator (IM-12)
double  g_accGpuDisjoint = 0.0;  // defect-style raw counts, not divided by n (see emit site)
double  g_accGpuUnmeasured = 0.0;

// Called once per Present, AFTER platform::present(), so the open query brackets everything
// the next frame issues. Cost when supported: one begin/end, one getParameter, one
// availability poll per frame -- noise against the 7.56 ms being attributed.
static void gpu_frame_tick() {
  if (g_gpuTqState < 0) return;
  if (g_gpuTqState == 0) {
    // Probe on the live context rather than trusting the availability check done on this rig:
    // SwiftShader runs the same binary, and an unsupported target must disable this path
    // permanently instead of raising GL errors every frame. Drain stale errors first so the
    // probe reads its own verdict, not a leftover from boot.
    while (glGetError() != GL_NO_ERROR) {}
    GLuint probe = 0;
    glGenQueries(1, &probe);
    glBeginQuery(GL_TIME_ELAPSED_EXT, probe);
    if (glGetError() != GL_NO_ERROR) {
      glDeleteQueries(1, &probe);
      g_gpuTqState = -1;
      return;
    }
    glEndQuery(GL_TIME_ELAPSED_EXT);
    glDeleteQueries(1, &probe);
    glGenQueries(GPU_TQ_RING, g_gpuTq);
    g_gpuTqState = 1;
  }

  // Close the bracket around the frame just presented.
  if (g_gpuTqActive >= 0) {
    glEndQuery(GL_TIME_ELAPSED_EXT);
    g_gpuTqInFlight[g_gpuTqActive] = true;
    g_gpuTqActive = -1;
  }

  // Reading GPU_DISJOINT_EXT also RESETS it, so once per frame is the right cadence. A
  // disjoint event (clock change, preemption) invalidates every result not yet harvested.
  GLint disjoint = 0;
  glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
  if (disjoint) {
    g_accGpuDisjoint += 1.0;
    g_gpuTqTainted = 0;
    for (int i = 0; i < GPU_TQ_RING; ++i) g_gpuTqTainted += g_gpuTqInFlight[i] ? 1 : 0;
  }

  // Harvest every result that is ready, oldest first. Never blocks: an unavailable result
  // stays in flight and is retried next frame.
  while (g_gpuTqInFlight[g_gpuTqTail]) {
    GLuint avail = 0;
    glGetQueryObjectuiv(g_gpuTq[g_gpuTqTail], GL_QUERY_RESULT_AVAILABLE, &avail);
    if (!avail) break;
    GLuint64 ns = 0;
    glGetQueryObjectui64vEXT(g_gpuTq[g_gpuTqTail], GL_QUERY_RESULT, &ns);
    g_gpuTqInFlight[g_gpuTqTail] = false;
    g_gpuTqTail = (g_gpuTqTail + 1) % GPU_TQ_RING;
    if (g_gpuTqTainted > 0) {
      --g_gpuTqTainted;   // measured across a disjoint event; the value is garbage
    } else {
      g_accGpuMs     += (double)ns / 1e6;
      g_accGpuFrames += 1.0;
    }
  }

  // Open the bracket for the coming frame, unless the ring is saturated -- then skip the
  // frame and say so, rather than stalling the engine thread to wait for a slot.
  if (!g_gpuTqInFlight[g_gpuTqHead]) {
    glBeginQuery(GL_TIME_ELAPSED_EXT, g_gpuTq[g_gpuTqHead]);
    g_gpuTqActive = g_gpuTqHead;
    g_gpuTqHead = (g_gpuTqHead + 1) % GPU_TQ_RING;
  } else {
    g_accGpuUnmeasured += 1.0;
  }
}

// Adds its lifetime to `acc`. Two clock reads per call, so the totals are upper bounds on the
// real cost -- documented rather than corrected for, since we are looking for a 24 ms gap.
struct ScopedMs {
  double& acc; double t0;
  explicit ScopedMs(double& a) : acc(a), t0(emscripten_performance_now()) {}
  ~ScopedMs() { acc += emscripten_performance_now() - t0; }
};

// A GPU buffer backed by a CPU staging vector. Lock hands out a pointer into
// the staging bytes; Unlock uploads them to the GL buffer object.
struct GLBuffer {
  GLenum target;
  std::vector<BYTE> cpu;
  GLuint glbuf = 0;
  // The usage hint this buffer is respecified with. Starts STATIC and is promoted on the SECOND
  // unlock -- see Unlock().
  GLenum hint = GL_STATIC_DRAW;
  // The range of the CURRENT lock. Lock and Unlock are separated in time, so Unlock has no way to
  // know what changed -- which is precisely why it re-uploads the whole staging vector. Recording
  // it here is what makes both the measurement and any future partial upload possible.
  // `locked` distinguishes "locked the entire buffer" from "never locked at all".
  UINT  lock_off = 0, lock_size = 0;
  DWORD lock_flags = 0;
  bool  locked = false;
  // Whether the GL buffer has storage yet. glBufferSubData cannot create it, only patch it.
  bool  sized = false;
  explicit GLBuffer(GLenum t, UINT length) : target(t), cpu(length) {}
  HRESULT Lock(UINT off, UINT size, BYTE** pp, DWORD flags) {
    if (!pp) return D3DERR_INVALIDCALL;
    lock_off = off; lock_size = size; lock_flags = flags; locked = true;
    *pp = cpu.data() + off; return D3D_OK;
  }
  HRESULT Unlock() {
    ScopedMs _u(g_frameUploadMs); ScopedMs _b(g_frameBufUploadMs);
    ++g_frameUploads; ++g_frameBufUploads;

    // --- resolve the range that actually changed ----------------------------------------------
    // MEASURED 2026-08-07 before this was acted on: waste ratio 28.1x on ANGLE/OpenGL (NVIDIA
    // RTX 4080 SUPER) -- 671 KB uploaded per Unlock for 24 KB actually locked -- and the cost is
    // bytes-correlated, not fixed per call (Pearson r = 0.77 over 64 windows; bytes x1.31 ->
    // time x1.65). Both were required before touching this: a large waste ratio alone would not
    // have justified it, because AB-06 showed the cost was once a pipeline STALL, and stall cost
    // does not scale with bytes. See docs/RESULTS-2026-08-07-buffer-upload-ranges.md.
    UINT up_off = 0, up_size = (UINT)cpu.size();
    bool upload_all = true;
    {
      double locked_bytes = (double)cpu.size();
      if (!locked) {
        // Unlock without a matching Lock. Conservative: upload everything, and charge it as a
        // whole-buffer write so the ratio errs AGAINST the hypothesis rather than flattering it.
        g_accBufUnlockUnmatched += 1.0;
      } else if (lock_off > cpu.size()) {
        // Lock does no bounds check -- `cpu.data() + off` hands out a pointer past the staging
        // vector for an out-of-range offset. That is a latent defect independent of this work;
        // count it and upload conservatively rather than deriving a range from a garbage offset.
        g_accBufLockOob += 1.0;
        locked_bytes = 0.0;
      } else {
        const UINT avail = (UINT)cpu.size() - lock_off;
        // D3D8: Lock(0, 0, ...) means the ENTIRE buffer, not a zero-byte lock.
        UINT eff = lock_size ? lock_size : avail;
        if (eff > avail) { g_accBufLockOob += 1.0; eff = avail; }
        locked_bytes = (double)eff;
        if (eff == cpu.size()) {
          g_accBufWholeLocks += 1.0;
        } else {
          // A genuine sub-range. Respecifying the whole buffer is still correct for a whole-buffer
          // lock and lets the driver orphan, so only sub-ranges take the glBufferSubData path.
          up_off = lock_off; up_size = eff; upload_all = false;
        }
      }
      if (locked && (lock_flags & D3DLOCK_DISCARD))     g_accBufDiscard     += 1.0;
      if (locked && (lock_flags & D3DLOCK_NOOVERWRITE)) g_accBufNoOverwrite += 1.0;
      g_accBufBytesLocked += locked_bytes;
      locked = false;   // cleared so a stale range cannot be counted twice
    }
    // -------------------------------------------------------------------------------------------

    if (!glbuf) glGenBuffers(1, &glbuf);
    glBindBuffer(target, glbuf);
    // Measured 2026-08-07: this call was 88% of the whole frame -- 60 buffer uploads per frame at
    // 461 us each on ANGLE/OpenGL (docs RESULTS-2026-08-06-angle-backend.md, AB-06). 461 us is not
    // transfer cost for a buffer this size; it is a PIPELINE STALL. GL_STATIC_DRAW promises "written
    // once, drawn many times", so the driver places the buffer in device-local memory and, when the
    // same buffer is rewritten mid-frame, has to wait for the GPU to finish reading it first.
    //
    // A buffer that is unlocked more than once is being rewritten, whatever it was created as, so
    // promote it after the first upload. The bytes uploaded are IDENTICAL -- this changes only the
    // hint, so there is no semantic risk, which is why it is the first thing tried. (Honouring the
    // locked range with glBufferSubData is the other half and is a separate change: it would stop
    // uploading bytes outside the lock, which is correct per the D3D8 contract but is a real
    // behaviour change rather than a hint.)
    // MEASURED, both halves. The hint alone is a 1.5x win on ANGLE/Vulkan (131 -> 77 us per
    // upload, frame 11.23 -> 7.48 ms) and does nothing on NVIDIA's GL driver (461 -> 462 us).
    //
    // Explicit orphaning -- glBufferData(size, NULL, hint) then glBufferSubData -- is the textbook
    // fix for this stall and was tried here. It made things WORSE: 726 us per upload on GL (from
    // 462, frame 31.6 -> 48.7 ms) and slightly worse on Vulkan (77 -> 82 us). Two calls plus a
    // discard signal cost more than the single respecify that ANGLE already turns into whatever it
    // prefers. It is not in the code; this comment is the record so nobody re-derives it from first
    // principles and re-lands it. Note this is NOT the same as the sub-range upload below: that
    // one sends FEWER bytes, where orphaning sent the same bytes plus a discard.
    //
    // ORPHANING IS NOW ALSO UNSAFE HERE, which is a stronger statement than "it was slower".
    // Orphaning discards the buffer's whole contents. The sub-range path below depends on the
    // bytes OUTSIDE the lock still being on the GPU from an earlier upload -- orphaning throws away
    // exactly what it relies on. It is only legal on D3DLOCK_DISCARD, where the app has promised it
    // no longer needs those bytes, and DISCARD is ~11% of uploads here (measured: ~6 of ~55 per
    // frame; the other ~49 are NOOVERWRITE). So re-landing orphaning unconditionally would corrupt
    // geometry, not merely cost time.
    //
    // And the remedy that WOULD address the remaining cost is unavailable. Patching in place makes
    // the driver conservatively synchronise -- measured as per-draw-call cost rising 2.2 -> 4.0 us
    // on GL and 2.4 -> 4.2 us on Vulkan, which is where about a third of the upload saving
    // reappeared (AB-14). D3DLOCK_NOOVERWRITE is precisely the app promising that sync is
    // unnecessary, but GL has no way to pass that promise on: WebGL2 has no buffer mapping, and
    // emscripten's glMapBufferRange REJECTS GL_MAP_UNSYNCHRONIZED_BIT outright and emulates the
    // mapping with malloc + copy (emsdk src/lib/libwebgl.js). There is no unsynchronised write
    // path in the browser to reach for.
    //
    // THE FIRST UPLOAD IS ALWAYS WHOLE-BUFFER, and that is a correctness requirement, not an
    // optimisation. glBufferSubData can only patch storage that already exists, and until the
    // first glBufferData the GL buffer has no size at all. Uploading everything once also makes
    // the GL buffer an exact copy of the staging vector, so every later divergence is confined to
    // bytes the app wrote inside a lock -- which is what makes patching only the locked range
    // safe. Without it, bytes never covered by any lock (a partially-filled buffer's tail) would
    // be undefined on the GPU instead of the zeros the staging vector holds.
    if (upload_all || !sized) {
      glBufferData(target, (GLsizeiptr)cpu.size(), cpu.data(), hint);
      sized = true;
      g_accBufBytesUploaded += (double)cpu.size();
    } else {
      glBufferSubData(target, (GLintptr)up_off, (GLsizeiptr)up_size, cpu.data() + up_off);
      g_accBufBytesUploaded += (double)up_size;
    }
    hint = GL_DYNAMIC_DRAW;
    return D3D_OK;
  }
  ~GLBuffer() { if (glbuf && platform::gl_context_alive()) glDeleteBuffers(1, &glbuf); }
};

// Shared IUnknown + IDirect3DResource8 stub prefix, mixed into the resource
// objects. QueryInterface returns self (+AddRef) — permissive but adequate.
#define D3D_RESOURCE_STUBS(RTYPE)                                                     \
  HRESULT QueryInterface(REFIID, void** o) override { if (o) { *o = this; ++refs; } return D3D_OK; } \
  HRESULT GetDevice(IDirect3DDevice8**) override { return D3DERR_INVALIDCALL; }       \
  HRESULT SetPrivateData(REFIID, const void*, DWORD, DWORD) override { return D3D_OK; } \
  HRESULT GetPrivateData(REFIID, void*, DWORD*) override { return D3DERR_INVALIDCALL; } \
  HRESULT FreePrivateData(REFIID) override { return D3D_OK; }                          \
  DWORD SetPriority(DWORD) override { return 0; }                                      \
  DWORD GetPriority() override { return 0; }                                           \
  void PreLoad() override {}                                                           \
  D3DRESOURCETYPE GetType() override { return RTYPE; }

struct VertexBuffer8 : IDirect3DVertexBuffer8 {
  GLBuffer b; ULONG refs = 1; UINT length; DWORD fvf;
  VertexBuffer8(UINT len, DWORD f) : b(GL_ARRAY_BUFFER, len), length(len), fvf(f) {}
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
  D3D_RESOURCE_STUBS(D3DRTYPE_VERTEXBUFFER)
  HRESULT Lock(UINT o, UINT s, BYTE** pp, DWORD f) override { return b.Lock(o, s, pp, f); }
  HRESULT Unlock() override { return b.Unlock(); }
  HRESULT GetDesc(D3DVERTEXBUFFER_DESC* d) override {
    if (d) { std::memset(d, 0, sizeof *d); d->Type = D3DRTYPE_VERTEXBUFFER; d->Pool = D3DPOOL_MANAGED; d->Size = length; d->FVF = fvf; }
    return D3D_OK;
  }
};

struct IndexBuffer8 : IDirect3DIndexBuffer8 {
  GLBuffer b; ULONG refs = 1; UINT length; D3DFORMAT fmt;
  IndexBuffer8(UINT len, D3DFORMAT f) : b(GL_ELEMENT_ARRAY_BUFFER, len), length(len), fmt(f) {}
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
  D3D_RESOURCE_STUBS(D3DRTYPE_INDEXBUFFER)
  HRESULT Lock(UINT o, UINT s, BYTE** pp, DWORD f) override { return b.Lock(o, s, pp, f); }
  HRESULT Unlock() override { return b.Unlock(); }
  HRESULT GetDesc(D3DINDEXBUFFER_DESC* d) override {
    if (d) { std::memset(d, 0, sizeof *d); d->Type = D3DRTYPE_INDEXBUFFER; d->Pool = D3DPOOL_MANAGED; d->Size = length; d->Format = fmt; }
    return D3D_OK;
  }
};

// Level-0-only A8R8G8B8 texture. CPU staging holds the D3D [B,G,R,A] bytes;
// UnlockRect uploads them verbatim as GL_RGBA (the .bgra shader swizzle fixes
// channel order). Nearest + clamp — no mips/filtering until a target needs them.
struct Surface8;  // fwd: texture mip levels are handed out as surfaces

// DXT/S3TC block decompression (Generals' terrain/unit textures are DXT1/3/5).
// Decoded to [R,G,B,A] byte order (uploaded as GL_RGBA, sampled plain — matching
// Leondore's d3d8webgl, which converts all textures to RGBA at upload). CPU decode
// keeps it portable (no reliance on the WEBGL_compressed_texture_s3tc extension).
namespace dxt {
// is_dxt lives in format_support.h so the factory's CheckDeviceFormat answers from the same
// predicate. Pulled in here because this dxt sits inside an anonymous namespace and would
// otherwise shadow the global one.
using ::dxt::is_dxt;
inline UINT block_bytes(D3DFORMAT f) { return f == D3DFMT_DXT1 ? 8u : 16u; }
inline size_t data_size(UINT w, UINT h, D3DFORMAT f) { return (size_t)((w + 3) / 4) * ((h + 3) / 4) * block_bytes(f); }
inline void rgb565(uint16_t c, int& r, int& g, int& b) {
  r = (((c >> 11) & 0x1f) * 255 + 15) / 31; g = (((c >> 5) & 0x3f) * 255 + 31) / 63; b = ((c & 0x1f) * 255 + 15) / 31;
}
// Decode the 8-byte color half of a block (shared by DXT1/3/5) into dst[BGRA].
// alpha16 supplies per-pixel alpha for DXT3/5; null => DXT1 (1-bit punch-through).
inline void color_block(const BYTE* b, BYTE* dst, UINT texW, UINT texH, UINT bx, UINT by, const BYTE* alpha16) {
  uint16_t c0 = (uint16_t)(b[0] | (b[1] << 8)), c1 = (uint16_t)(b[2] | (b[3] << 8));
  int r[4], g[4], bl[4]; rgb565(c0, r[0], g[0], bl[0]); rgb565(c1, r[1], g[1], bl[1]);
  bool punch = !alpha16 && c0 <= c1;   // DXT1 with 1-bit alpha
  if (!punch) { r[2] = (2*r[0]+r[1])/3; g[2] = (2*g[0]+g[1])/3; bl[2] = (2*bl[0]+bl[1])/3;
                r[3] = (r[0]+2*r[1])/3; g[3] = (g[0]+2*g[1])/3; bl[3] = (bl[0]+2*bl[1])/3; }
  else        { r[2] = (r[0]+r[1])/2;   g[2] = (g[0]+g[1])/2;   bl[2] = (bl[0]+bl[1])/2;
                r[3] = 0; g[3] = 0; bl[3] = 0; }
  uint32_t idx = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
  for (int py = 0; py < 4; py++) for (int px = 0; px < 4; px++) {
    UINT x = bx*4+px, y = by*4+py; if (x >= texW || y >= texH) continue;
    int i = (idx >> (2*(py*4+px))) & 3;
    int a = alpha16 ? alpha16[py*4+px] : (punch && i == 3 ? 0 : 255);
    BYTE* d = dst + ((size_t)y*texW + x)*4;
    d[0] = (BYTE)r[i]; d[1] = (BYTE)g[i]; d[2] = (BYTE)bl[i]; d[3] = (BYTE)a;   // R,G,B,A
  }
}
inline void dxt5_alpha(const BYTE* b, BYTE* out16) {
  int a0 = b[0], a1 = b[1], al[8]; al[0] = a0; al[1] = a1;
  if (a0 > a1) for (int i = 1; i < 7; i++) al[i+1] = ((7-i)*a0 + i*a1) / 7;
  else { for (int i = 1; i < 5; i++) al[i+1] = ((5-i)*a0 + i*a1) / 5; al[6] = 0; al[7] = 255; }
  uint64_t bits = 0; for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2+i] << (8*i);
  for (int i = 0; i < 16; i++) out16[i] = (BYTE)al[(bits >> (3*i)) & 7];
}
inline void decode(const BYTE* src, UINT w, UINT h, D3DFORMAT f, BYTE* dst /* w*h*4 */) {
  UINT bw = (w + 3) / 4, bh = (h + 3) / 4, bb = block_bytes(f);
  for (UINT by = 0; by < bh; by++) for (UINT bx = 0; bx < bw; bx++) {
    const BYTE* blk = src + ((size_t)by*bw + bx) * bb;
    if (f == D3DFMT_DXT1) { color_block(blk, dst, w, h, bx, by, nullptr); continue; }
    BYTE a16[16];
    if (f == D3DFMT_DXT5) dxt5_alpha(blk, a16);
    else for (int i = 0; i < 16; i++) a16[i] = (BYTE)(((blk[i/2] >> ((i&1)*4)) & 0xf) * 17);  // DXT3 explicit
    color_block(blk + 8, dst, w, h, bx, by, a16);
  }
}
} // namespace dxt

// Uncompressed texture formats. The engine (WW3D textureloader) loads many
// textures as 16-bit (A4R4G4B4/R5G6B5/A1R5G5B5) or 24-bit (R8G8B8), not just
// 32-bit A8R8G8B8 — Get_Valid_Texture_Format hands us whatever the caps allow.
// We stage at the source's true bytes-per-pixel (so LockRect pitch matches what
// the engine writes) and expand to the same [B,G,R,A] byte order the 32-bit
// path uses, so the shader's .bgra swizzle recovers correct color. Without this,
// 16-bit rows were read as 32-bit → the terrain rainbow-noise.
namespace texfmt {
inline UINT bpp(D3DFORMAT f) {
  switch (f) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: return 4;
    case D3DFMT_R8G8B8:   return 3;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4: case D3DFMT_A8L8: return 2;
    case D3DFMT_A8: case D3DFMT_L8: return 1;
    default: return 4;   // unknown: treat as 32-bit (verbatim), matches old behavior
  }
}
// supported() lives in format_support.h — shared with the factory's CheckDeviceFormat. Pulled
// in here because this texfmt sits inside an anonymous namespace and would otherwise shadow it.
using ::texfmt::supported;
// One level prepared for glTexImage2D, matching Leondore's d3d8webgl prepareLevelUpload:
// 32-bit is converted BGRA->RGBA; 16-bit uses the native GL packed type (no CPU expand);
// L8/A8/A8L8 use the GL luminance/alpha formats. `conv` holds any reordered bytes.
struct Upload { GLenum internalFormat, format, type; const BYTE* pixels; std::vector<BYTE> conv; };
inline bool prepare(D3DFORMAT f, UINT w, UINT h, const BYTE* src, Upload& u) {
  const size_t n = (size_t)w * h;
  switch (f) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8: {                    // BGRA bytes -> RGBA; X8 forces opaque alpha
      const bool opaque = (f == D3DFMT_X8R8G8B8);
      u.conv.resize(n * 4);
      for (size_t i = 0; i < n; i++) {
        u.conv[i*4+0] = src[i*4+2]; u.conv[i*4+1] = src[i*4+1];
        u.conv[i*4+2] = src[i*4+0]; u.conv[i*4+3] = opaque ? 255 : src[i*4+3];
      }
      u.internalFormat = GL_RGBA; u.format = GL_RGBA; u.type = GL_UNSIGNED_BYTE; u.pixels = u.conv.data();
      return true;
    }
    case D3DFMT_R8G8B8: {                       // 24-bit BGR -> RGBA
      u.conv.resize(n * 4);
      for (size_t i = 0; i < n; i++) {
        u.conv[i*4+0] = src[i*3+2]; u.conv[i*4+1] = src[i*3+1];
        u.conv[i*4+2] = src[i*3+0]; u.conv[i*4+3] = 255;
      }
      u.internalFormat = GL_RGBA; u.format = GL_RGBA; u.type = GL_UNSIGNED_BYTE; u.pixels = u.conv.data();
      return true;
    }
    case D3DFMT_R5G6B5:                          // native 5_6_5 (RGB order already)
      u.internalFormat = GL_RGB565; u.format = GL_RGB; u.type = GL_UNSIGNED_SHORT_5_6_5; u.pixels = src;
      return true;
    case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4: {                       // ARGB4444 -> RGBA4444
      const bool opaque = (f == D3DFMT_X4R4G4B4);
      u.conv.resize(n * 2);
      const uint16_t* s = (const uint16_t*)src; uint16_t* d = (uint16_t*)u.conv.data();
      for (size_t i = 0; i < n; i++) { uint16_t v = s[i];
        uint16_t a = opaque ? 0xF : ((v>>12)&0xF), r = (v>>8)&0xF, g = (v>>4)&0xF, b = v&0xF;
        d[i] = (uint16_t)((r<<12)|(g<<8)|(b<<4)|a); }
      u.internalFormat = GL_RGBA4; u.format = GL_RGBA; u.type = GL_UNSIGNED_SHORT_4_4_4_4; u.pixels = u.conv.data();
      return true;
    }
    case D3DFMT_A1R5G5B5:
    case D3DFMT_X1R5G5B5: {                        // ARGB1555 -> RGBA5551
      const bool opaque = (f == D3DFMT_X1R5G5B5);
      u.conv.resize(n * 2);
      const uint16_t* s = (const uint16_t*)src; uint16_t* d = (uint16_t*)u.conv.data();
      for (size_t i = 0; i < n; i++) { uint16_t v = s[i];
        uint16_t a = opaque ? 1 : ((v>>15)&0x1), r = (v>>10)&0x1F, g = (v>>5)&0x1F, b = v&0x1F;
        d[i] = (uint16_t)((r<<11)|(g<<6)|(b<<1)|a); }
      u.internalFormat = GL_RGB5_A1; u.format = GL_RGBA; u.type = GL_UNSIGNED_SHORT_5_5_5_1; u.pixels = u.conv.data();
      return true;
    }
    case D3DFMT_L8:
      u.internalFormat = GL_LUMINANCE; u.format = GL_LUMINANCE; u.type = GL_UNSIGNED_BYTE; u.pixels = src; return true;
    case D3DFMT_A8:
      u.internalFormat = GL_ALPHA; u.format = GL_ALPHA; u.type = GL_UNSIGNED_BYTE; u.pixels = src; return true;
    case D3DFMT_A8L8:
      u.internalFormat = GL_LUMINANCE_ALPHA; u.format = GL_LUMINANCE_ALPHA; u.type = GL_UNSIGNED_BYTE; u.pixels = src; return true;
    default: return false;
  }
}
} // namespace texfmt

struct Texture8 : IDirect3DTexture8 {
  ULONG refs = 1;
  struct Level { UINT w, h; std::vector<BYTE> px; };
  std::vector<Level> levels;   // mip chain; levels[0] is the base
  D3DFORMAT fmt;
  GLuint tex = 0;
  int maxLevel = 0;            // highest mip level actually uploaded (0 => base only)
  // mips==0 => full chain down to 1x1. w() / h() below expose the base level so
  // the single-level callers (and existing smokes) read the same values as before.
  Texture8(UINT width, UINT height, UINT mips = 1, D3DFORMAT format = D3DFMT_A8R8G8B8) : fmt(format) {
    UINT lw = width ? width : 1, lh = height ? height : 1;
    UINT count = mips ? mips : 0xffffu;
    const bool compressed = dxt::is_dxt(format);
    for (UINT i = 0; i < count; ++i) {
      size_t bytes = compressed ? dxt::data_size(lw, lh, format) : (size_t)lw * lh * texfmt::bpp(format);
      levels.push_back({lw, lh, std::vector<BYTE>(bytes)});
      if (lw == 1 && lh == 1) break;
      lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1;
    }
    if (levels.empty()) levels.push_back({1, 1, std::vector<BYTE>(4)});
  }
  UINT w() const { return levels[0].w; }
  UINT h() const { return levels[0].h; }
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
  D3D_RESOURCE_STUBS(D3DRTYPE_TEXTURE)
  DWORD SetLOD(DWORD) override { return 0; }
  DWORD GetLOD() override { return 0; }
  DWORD GetLevelCount() override { return (DWORD)levels.size(); }
  HRESULT GetLevelDesc(UINT l, D3DSURFACE_DESC* d) override {
    if (!d || l >= levels.size()) return D3DERR_INVALIDCALL;
    std::memset(d, 0, sizeof *d); d->Format = fmt; d->Type = D3DRTYPE_TEXTURE;
    d->Pool = D3DPOOL_MANAGED; d->Width = levels[l].w; d->Height = levels[l].h;
    return D3D_OK;
  }
  HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface8** ppSurfaceLevel) override;  // out-of-line (needs Surface8)
  HRESULT LockRect(UINT l, D3DLOCKED_RECT* lr, const RECT*, DWORD) override {
    if (!lr || l >= levels.size()) return D3DERR_INVALIDCALL;
    // DXT pitch is bytes per ROW OF BLOCKS; uncompressed is bytes per pixel row.
    lr->Pitch = dxt::is_dxt(fmt) ? (int32_t)(((levels[l].w + 3) / 4) * dxt::block_bytes(fmt))
                                 : (int32_t)(levels[l].w * texfmt::bpp(fmt));
    lr->pBits = levels[l].px.data(); return D3D_OK;
  }
  HRESULT UnlockRect(UINT l) override { upload_level(l); return D3D_OK; }
  // Upload one mip level to GL. Filter/wrap kept NEAREST/CLAMP (unchanged from the
  // single-level impl) so existing pixel smokes stay bit-identical; real sampler
  // state is applied elsewhere.
  void upload_level(UINT l) {
    ScopedMs _u(g_frameUploadMs); ScopedMs _t(g_frameTexUploadMs);
    ++g_frameUploads; ++g_frameTexUploads;
    if (l >= levels.size()) return;
    // Reference-aligned mip handling (Leondore d3d8webgl): for a MULTI-LEVEL texture the
    // BASE level is authoritative -- upload level 0 and GPU-generate the whole chain,
    // and IGNORE the engine's uploads to levels 1+. This removes two mip-garbage sources
    // that show up as a shared shimmer/tiling pattern on MINIFIED alpha surfaces (trees,
    // shoreline, water, projected light pools):
    //   (a) the engine declares a chain (e.g. MIP_LEVELS_3) but leaves upper levels empty
    //       -> minification samples transparent-black/garbage from the unfilled levels;
    //   (b) a later empty upper-level UnlockRect clobbering the freshly generated mips.
    // Applies to DXT too: we CPU-decode DXT to RGBA, so glGenerateMipmap is valid and the
    // never-filled DXT upper levels (previously uploaded as black -> tree/foliage moire)
    // can no longer leak. Single-level textures are unchanged (no mip chain generated).
    const bool multiLevel = levels.size() > 1;
    if (multiLevel && l != 0) return;                 // base level drives the whole chain
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    const Level& L = levels[l];
    if (dxt::is_dxt(fmt)) {
      std::vector<BYTE> rgba((size_t)L.w * L.h * 4);   // CPU-decompress DXT -> RGBA (portable; no S3TC ext)
      dxt::decode(L.px.data(), L.w, L.h, fmt, rgba.data());
      glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA, (GLsizei)L.w, (GLsizei)L.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    } else {
      texfmt::Upload u;
      if (texfmt::prepare(fmt, L.w, L.h, L.px.data(), u))
        glTexImage2D(GL_TEXTURE_2D, (GLint)l, u.internalFormat, (GLsizei)L.w, (GLsizei)L.h, 0, u.format, u.type, u.pixels);
      else {                                            // unknown format -> magenta (visible, not crashy)
        std::vector<BYTE> mag((size_t)L.w * L.h * 4);
        for (size_t i = 0; i < mag.size(); i += 4) { mag[i]=255; mag[i+1]=0; mag[i+2]=255; mag[i+3]=255; }
        glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA, (GLsizei)L.w, (GLsizei)L.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, mag.data());
      }
    }
    if (multiLevel) {
      glGenerateMipmap(GL_TEXTURE_2D);                 // l==0 here; regenerate the full chain from the base
      maxLevel = (int)levels.size() - 1;
    } else if ((int)l > maxLevel) {
      maxLevel = (int)l;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, maxLevel);
    // Default to bilinear + wrap (the retail game samples smooth, not blocky).
    // Real per-stage filter/address is applied at bind time (apply_sampler).
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  HRESULT AddDirtyRect(const RECT*) override { return D3D_OK; }
  ~Texture8() { if (tex && platform::gl_context_alive()) glDeleteTextures(1, &tex); }
};

// A surface is either a view onto a Texture8 mip level (parent != null; UnlockRect
// re-uploads that level) or a standalone CPU image (CreateImageSurface; owns its
// buffer). The engine's TextureClass loads pixels through this path, and D3DX's
// LoadSurfaceFromSurface (engine-side CompatLib) just needs LockRect to work.
struct Surface8 : IDirect3DSurface8 {
  ULONG refs = 1;
  D3DFORMAT fmt;
  UINT w, h;
  Texture8* parent;        // non-null => texture-level surface
  UINT level;
  std::vector<BYTE> own;   // used only when parent == nullptr
  Surface8(Texture8* p, UINT lvl) : fmt(p->fmt), w(p->levels[lvl].w), h(p->levels[lvl].h), parent(p), level(lvl) { p->AddRef(); }
  Surface8(UINT width, UINT height, D3DFORMAT format) : fmt(format), w(width), h(height), parent(nullptr), level(0), own((size_t)width * height * texfmt::bpp(format)) {}
  ~Surface8() { if (parent) parent->Release(); }
  BYTE* base() { return parent ? parent->levels[level].px.data() : own.data(); }
  HRESULT QueryInterface(REFIID, void** o) override { if (o) *o = this; return D3D_OK; }
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
  HRESULT GetDevice(IDirect3DDevice8**) override { return D3DERR_INVALIDCALL; }
  HRESULT SetPrivateData(REFIID, const void*, DWORD, DWORD) override { return D3D_OK; }
  HRESULT GetPrivateData(REFIID, void*, DWORD*) override { return D3DERR_INVALIDCALL; }
  HRESULT FreePrivateData(REFIID) override { return D3D_OK; }
  HRESULT GetContainer(REFIID, void** o) override { if (o) *o = parent; return parent ? D3D_OK : D3DERR_INVALIDCALL; }
  HRESULT GetDesc(D3DSURFACE_DESC* d) override {
    if (!d) return D3DERR_INVALIDCALL;
    std::memset(d, 0, sizeof *d); d->Format = fmt; d->Type = D3DRTYPE_SURFACE;
    d->Pool = D3DPOOL_MANAGED; d->Width = w; d->Height = h; return D3D_OK;
  }
  HRESULT LockRect(D3DLOCKED_RECT* lr, const RECT* r, DWORD) override {
    if (!lr) return D3DERR_INVALIDCALL;
    UINT top = r ? (UINT)r->top : 0, left = r ? (UINT)r->left : 0;
    const UINT bp = texfmt::bpp(fmt);
    lr->Pitch = (int32_t)(w * bp);
    lr->pBits = base() + (size_t)top * (w * bp) + (size_t)left * bp;
    return D3D_OK;
  }
  HRESULT UnlockRect() override { if (parent) parent->upload_level(level); return D3D_OK; }
};

HRESULT Texture8::GetSurfaceLevel(UINT Level, IDirect3DSurface8** ppSurfaceLevel) {
  if (!ppSurfaceLevel || Level >= levels.size()) return D3DERR_INVALIDCALL;
  *ppSurfaceLevel = new Surface8(this, Level);
  return D3D_OK;
}

void set_identity(float* m) {
  std::memset(m, 0, 16 * sizeof(float));
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}
float as_float(DWORD v) { float f; std::memcpy(&f, &v, sizeof f); return f; }   // D3DRS float-in-DWORD

bool prim_info(D3DPRIMITIVETYPE t, UINT pc, GLenum& mode, GLsizei& n) {
  switch (t) {
    case D3DPT_POINTLIST:     mode = GL_POINTS;         n = (GLsizei)pc;       return true;
    case D3DPT_LINELIST:      mode = GL_LINES;          n = (GLsizei)(pc * 2); return true;
    case D3DPT_LINESTRIP:     mode = GL_LINE_STRIP;     n = (GLsizei)(pc + 1); return true;
    case D3DPT_TRIANGLELIST:  mode = GL_TRIANGLES;      n = (GLsizei)(pc * 3); return true;
    case D3DPT_TRIANGLESTRIP: mode = GL_TRIANGLE_STRIP; n = (GLsizei)(pc + 2); return true;
    case D3DPT_TRIANGLEFAN:   mode = GL_TRIANGLE_FAN;   n = (GLsizei)(pc + 2); return true;
  }
  return false;
}
GLenum gl_cmpfunc(DWORD f) {
  switch (f) {
    case D3DCMP_NEVER: return GL_NEVER;   case D3DCMP_LESS: return GL_LESS;
    case D3DCMP_EQUAL: return GL_EQUAL;   case D3DCMP_LESSEQUAL: return GL_LEQUAL;
    case D3DCMP_GREATER: return GL_GREATER; case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
    case D3DCMP_GREATEREQUAL: return GL_GEQUAL; default: return GL_ALWAYS;
  }
}
GLenum gl_blend(DWORD b) {
  switch (b) {
    case D3DBLEND_ZERO: return GL_ZERO;             case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;   case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;   case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    default: return GL_ONE;
  }
}
// D3D sampler filter/address -> GL. Only explicit POINT -> nearest; everything else
// (incl. LINEAR/ANISOTROPIC) -> linear. D3DTEXF_NONE (0) is invalid for min/mag in real
// D3D8 — the runtime rejects it and the effective filter stays LINEAR — so we must NOT
// treat a NONE min/mag as nearest. (The engine leaves textures at an uninitialized
// FILTER_TYPE_DEFAULT that resolves to 0/NONE; the shroud relied on that meaning linear,
// and mapping it to nearest turned its one-texel-per-cell projection into hard squares.)
// NONE remains meaningful only for the *mip* filter (no mipmapping), handled below.
// WRAP is the default; CLAMP/MIRROR honored. gl_tex_filter is used for MAG (never
// mipmapped); MIN goes through gl_min_filter which fuses D3D's separate min+mip knobs
// into GL's single enum, but only when a real mip chain was uploaded (hasMips).
inline GLenum gl_tex_filter(uint32_t f) { return (f == D3DTEXF_POINT) ? GL_NEAREST : GL_LINEAR; }
inline GLenum gl_min_filter(uint32_t minF, uint32_t mipF, bool hasMips) {
  const bool linMin = (minF != D3DTEXF_POINT);
  if (!hasMips || mipF == D3DTEXF_NONE) return linMin ? GL_LINEAR : GL_NEAREST;
  const bool linMip = (mipF == D3DTEXF_LINEAR);   // else POINT: nearest mip
  if (linMin) return linMip ? GL_LINEAR_MIPMAP_LINEAR  : GL_LINEAR_MIPMAP_NEAREST;
  return           linMip ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
}
inline GLenum gl_tex_wrap(uint32_t a) {
  switch (a) {
    case D3DTADDRESS_CLAMP:
    // D3DTADDRESS_BORDER: GLES3 has no GL_CLAMP_TO_BORDER, so clamp to the edge texel —
    // matching the reference d3d8webgl port. Without this case, BORDER fell through to
    // GL_REPEAT, which tiles a single sprite and can wrap the opposite edge in at the quad
    // boundary. (Correctness fix for BORDER-addressed content; the game's smoke/particle
    // billboards observed so far use WRAP/CLAMP, not BORDER.)
    case D3DTADDRESS_BORDER: return GL_CLAMP_TO_EDGE;
    case D3DTADDRESS_MIRROR: return GL_MIRRORED_REPEAT;
    default:                 return GL_REPEAT;   // WRAP
  }
}

struct Device8 : IDirect3DDevice8 {
  ULONG refs = 1;
  VertexBuffer8* stream = nullptr;
  IndexBuffer8* indices = nullptr;
  UINT baseVertexIndex = 0;   // D3D8 SetIndices base: added to every index at draw time
  UINT stride = 0;
  uint32_t fvf = 0;
  Texture8* texture = nullptr;               // stage 0 texture
  Texture8* texture1 = nullptr;              // stage 1 texture (terrain multitexture)
  // Full per-stage combiner + texcoord state (D3DTSS_*), initialized to the D3D8
  // defaults: stage 0 modulates the texel with the diffuse/current color and
  // selects the texel alpha; stage 1 is disabled. SetTextureStageState overrides.
  struct StageState {
    uint32_t colorOp, colorArg1, colorArg2, alphaOp, alphaArg1, alphaArg2;
    uint32_t tci;      // low 16 bits of D3DTSS_TEXCOORDINDEX: which vertex uv set feeds the stage
    uint32_t texgen;   // high bits (>>16): 0 none, else a D3DTSS_TCI_* texgen mode
    uint32_t ttff;     // D3DTSS_TEXTURETRANSFORMFLAGS (COUNTn enables the stage matrix)
    // Sampler state. Default to LINEAR + WRAP (fidelity: the retail game samples
    // bilinear/trilinear with wrapping; D3D's own POINT/WRAP default would look
    // blocky). The engine overrides per stage (e.g. terrain sets CLAMP).
    uint32_t minFilter, magFilter, mipFilter, addressU, addressV;
    uint32_t maxAniso;   // D3DTSS_MAXANISOTROPY; 1 = isotropic, D3D8's own default
  } stageState[2] = {
    { D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, 0, 0,
      D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP, 1 },
    { D3DTOP_DISABLE,  D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_DISABLE,    D3DTA_TEXTURE, D3DTA_CURRENT, 1, 0, 0,
      D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP, 1 },
  };
  float texMat[2][16];                       // D3DTS_TEXTURE0 / D3DTS_TEXTURE0+1 (row-major, uploaded as-is)
  float texFactor[4] = {0, 0, 0, 0};         // D3DRS_TEXTUREFACTOR as RGBA floats
  GLenum srcBlend = GL_ONE, dstBlend = GL_ZERO;
  bool alphaBlendEnable = false;   // D3DRS_ALPHABLENDENABLE — tracked so the draw path re-asserts it
  bool alphaTestEnable = false, zWrite = true, zTest = true;   // zTest = engine's D3DRS_ZENABLE intent
  uint32_t alphaFunc = D3DCMP_ALWAYS;
  DWORD alphaRef = 0;
  float world[16], view[16], proj[16];
  bool lighting = false, specularEnable = false;
  // Material color sources (D3DRS_*MATERIALSOURCE). D3D8 defaults: COLORVERTEX on,
  // diffuse from vertex COLOR1, ambient/emissive from the material. Generals bakes
  // scene lighting into the vertex diffuse and leaves material diffuse white, so
  // honoring COLOR1 here is what stops lit geometry blowing out to full white.
  bool colorVertex = true;
  uint32_t diffuseSource = D3DMCS_COLOR1, ambientSource = D3DMCS_MATERIAL, emissiveSource = D3DMCS_MATERIAL;
  // D3D8's own default is COLOR2, but this backend does not upload D3DFVF_SPECULAR as an
  // attribute, so MATERIAL is the honest default: it is what the shader actually reads.
  uint32_t specularSource = D3DMCS_MATERIAL;
  float globalAmbient[4] = {0, 0, 0, 0};
  D3DLIGHT8 lights[ff::MAX_LIGHTS]{};
  bool lightOn[ff::MAX_LIGHTS] = {false};
  D3DMATERIAL8 material{ {1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0 };
  bool fogEnable = false;
  float fogColor[3] = {0, 0, 0}, fogStart = 0.0f, fogEnd = 1.0f;
  // Last fog mode written, per D3D8 state. Sentinel 0xFFFFFFFF = "never written", so the first
  // write is a transition even when it selects mode 0.
  uint32_t lastFogTableMode = 0xFFFFFFFFu, lastFogVertexMode = 0xFFFFFFFFu;
  // Color write mask (D3DRS_COLORWRITEENABLE). Default = write all (0xF). Zero is a real
  // value: stencil-shadow volumes render color-write-off; mapping 0 -> "write all" painted
  // every shadow volume as a solid black silhouette over the scene.
  DWORD colorWrite = 0xF;
  // Stencil state (applied together at draw time, since glStencilFunc/Op take grouped args).
  bool  stencilEnable = false;
  DWORD stencilFail = D3DSTENCILOP_KEEP, stencilZFail = D3DSTENCILOP_KEEP, stencilPass = D3DSTENCILOP_KEEP;
  DWORD stencilFunc = D3DCMP_ALWAYS, stencilRef = 0, stencilMask = 0xFFFFFFFF, stencilWriteMask = 0xFFFFFFFF;
  // Mirror of every SetRenderState value, so GetRenderState can answer truthfully.
  // D3DRS_* tops out well under this in the D3D8 subset (runtime/d3d8/d3d8.h).
  static constexpr unsigned kRenderStateCount = 256;
  DWORD rsCache[kRenderStateCount]{};
  // Stage-state mirror, same contract as rsCache: every Set is recorded so Get can answer.
  static constexpr unsigned kStageCount = 8, kStageStateCount = 32;
  DWORD tssCache[kStageCount][kStageStateCount]{};
  float vpW, vpH;
  D3DVIEWPORT8 viewport;
  GLuint scratchVB = 0, scratchIB = 0;   // reused for DrawPrimitiveUP (user-pointer) draws

  Device8(int w, int h) : vpW((float)w), vpH((float)h) {
    set_identity(world); set_identity(view); set_identity(proj);
    set_identity(texMat[0]); set_identity(texMat[1]);
    viewport = {0, 0, (DWORD)w, (DWORD)h, 0.0f, 1.0f};
    glDepthFunc(GL_LEQUAL);
    // Seed the render-state mirror with the state this device actually starts in, so a
    // GetRenderState before any SetRenderState reports the truth rather than zero.
    rsCache[D3DRS_COLORWRITEENABLE] = 0xF;
    rsCache[D3DRS_ZFUNC]            = D3DCMP_LESSEQUAL;
    rsCache[D3DRS_ALPHAFUNC]        = D3DCMP_ALWAYS;
    rsCache[D3DRS_SRCBLEND]         = D3DBLEND_ONE;
    rsCache[D3DRS_DESTBLEND]        = D3DBLEND_ZERO;
    rsCache[D3DRS_STENCILFUNC]      = D3DCMP_ALWAYS;
    rsCache[D3DRS_STENCILMASK]      = 0xFFFFFFFFu;
    rsCache[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFFu;
    rsCache[D3DRS_TEXTUREFACTOR]    = 0xFFFFFFFFu;
    // Same reasoning, per-stage: D3D8's own default for D3DTSS_MAXANISOTROPY is 1 (isotropic),
    // but tssCache defaults to 0 like every other slot — so an unset GetTextureStageState would
    // answer 0, not the truth. Harmless for rendering (StageState::maxAniso already starts at 1
    // and that is what actually drives apply_sampler), but a mirror should not lie either.
    tssCache[0][D3DTSS_MAXANISOTROPY] = 1;
    tssCache[1][D3DTSS_MAXANISOTROPY] = 1;
  }

  HRESULT QueryInterface(REFIID, void** o) override { if (o) { *o = this; ++refs; } return D3D_OK; }
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override {
    ULONG r = --refs;
    if (!r) {
      if (stream) stream->Release();
      if (indices) indices->Release();
      if (texture) texture->Release();
      platform::destroy_gl_context();
      delete this;
    }
    return r;
  }

  HRESULT Clear(DWORD, const D3DRECT*, DWORD Flags, D3DCOLOR c, float Z, DWORD Stencil) override {
    g_dx8_clears++;
    GLbitfield mask = 0;
    if (Flags & D3DCLEAR_TARGET) {
      // D3D Clear ignores COLORWRITEENABLE; force all channels on so a shadow pass that
      // left color-write off doesn't mask the clear. The next draw restores the mask.
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glClearColor(((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f,
                   (c & 0xff) / 255.0f, ((c >> 24) & 0xff) / 255.0f);
      mask |= GL_COLOR_BUFFER_BIT;
    }
    if (Flags & D3DCLEAR_ZBUFFER) { glDepthMask(GL_TRUE); glClearDepthf(Z); mask |= GL_DEPTH_BUFFER_BIT; }
    if (Flags & D3DCLEAR_STENCIL) { glStencilMask(0xFFFFFFFF); glClearStencil((GLint)Stencil); mask |= GL_STENCIL_BUFFER_BIT; }
    glClear(mask);
    if (Flags & D3DCLEAR_ZBUFFER) glDepthMask(zWrite ? GL_TRUE : GL_FALSE);
    return D3D_OK;
  }
  // --- per-frame draw-call instrumentation -------------------------------------------------
  // Measured 2026-08-06: this translation layer is CPU-bound in PER-DRAW-CALL cost, not
  // GPU-bound. An integrated AMD GPU beat an RTX 4080 on the same build until Chrome's ANGLE
  // backend was moved off NVIDIA's OpenGL driver onto Vulkan (2.3x, 30 -> 71 FPS), and the 4080
  // sat at idle clocks (P8, 210 MHz, 10 W of 320) mid-skirmish because commands were not
  // reaching it fast enough to wake it. See
  // generals-dx8wasm/docs/RESULTS-2026-08-06-angle-backend.md.
  //
  // "Reduce the number of draw calls" is the obvious response to that, and until these two
  // gauges existed it was an ASSUMPTION: nothing measured how many calls a frame issues or what
  // share of the frame they account for. gl.draws answers the first, gl.draw_ms the second.
  //
  // Held as double, never cast to a 32-bit integer. Deltas of emscripten_performance_now() are
  // sub-millisecond doubles; the saturating float->i32 fptoui is exactly what silently killed
  // the telemetry pump once already (runtime/telemetry/telemetry.cpp, now_ms()). The clock is
  // thread-relative, which is fine here because only deltas within one thread are ever taken.
  //
  // Cost of measuring: two clock reads per draw call. At a few thousand calls a frame that is
  // well under the ~30 ms being investigated, but it is not free -- treat gl.draw_ms as an
  // upper bound on the real submission cost rather than an exact figure.
  uint32_t frameDraws = 0;
  double   frameDrawMs = 0.0;
  // Draw calls turned out to be ~2% of the frame (203/frame, 0.58 ms), so the cost is NOT in
  // drawing. The two remaining candidates on this side of the boundary are the volume of STATE
  // changes -- ANGLE-over-GL validates each one, and a fixed-function engine emits far more of
  // them than draws -- and the SWAP, where a GL driver does its deferred work. Count the first,
  // time the second.
  uint32_t frameStateCalls = 0;
  struct DrawTimer {
    Device8* d; double t0;
    explicit DrawTimer(Device8* dev) : d(dev), t0(emscripten_performance_now()) {}
    ~DrawTimer() { d->frameDrawMs += emscripten_performance_now() - t0; ++d->frameDraws; }
  };

  HRESULT Present(const RECT*, const RECT*, HWND, const RGNDATA*) override {
    // Emitted at the frame boundary as GAUGES, not counters: both are absolute values for the
    // frame just finished, and the reducer sums counters by key (which would turn a per-frame
    // count into a meaningless running total -- see telemetry.h on why a sampled value fed to a
    // counter produces triangular numbers).
    g_accFrames      += 1.0;
    g_accDraws       += (double)frameDraws;
    g_accDrawMs      += frameDrawMs;
    g_accStateCalls  += (double)frameStateCalls;
    g_accStateMs     += g_frameStateMs;
    g_accTexUploads  += (double)g_frameTexUploads;
    g_accTexMs       += g_frameTexUploadMs;
    g_accBufUploads  += (double)g_frameBufUploads;
    g_accBufMs       += g_frameBufUploadMs;
    g_frameStateMs = 0.0; g_frameUploadMs = 0.0; g_frameUploads = 0;
    g_frameTexUploadMs = 0.0; g_frameBufUploadMs = 0.0;
    g_frameTexUploads = 0; g_frameBufUploads = 0;
    frameDraws = 0;
    frameDrawMs = 0.0;
    frameStateCalls = 0;
    // One clock pair per frame, so this costs nothing measurable. If a GL driver is doing its
    // deferred work at swap time, it lands here and nowhere else.
    const double p0 = emscripten_performance_now();
    platform::present();
    const double now = emscripten_performance_now();
    g_accPresentMs += now - p0;

    // After present, so the query it opens brackets the whole of the NEXT frame's GL stream
    // and the one it closes covered this frame's, swap included.
    gpu_frame_tick();

    // Once per second, emit per-frame AVERAGES and reset. 9 records/second, not 9/frame.
    if (g_lastEmitMs == 0.0) g_lastEmitMs = now;
    if (now - g_lastEmitMs >= 1000.0 && g_accFrames > 0.0) {
      const double n = g_accFrames;
      dx8wasm_tel_gauge("gl.draws",         g_accDraws       / n);
      dx8wasm_tel_gauge("gl.draw_ms",       g_accDrawMs      / n);
      dx8wasm_tel_gauge("gl.state_calls",   g_accStateCalls  / n);
      dx8wasm_tel_gauge("gl.state_ms",      g_accStateMs     / n);
      dx8wasm_tel_gauge("gl.tex_uploads",   g_accTexUploads  / n);
      dx8wasm_tel_gauge("gl.tex_upload_ms", g_accTexMs       / n);
      dx8wasm_tel_gauge("gl.buf_uploads",   g_accBufUploads  / n);
      dx8wasm_tel_gauge("gl.buf_upload_ms", g_accBufMs       / n);
      dx8wasm_tel_gauge("gl.present_ms",    g_accPresentMs   / n);
      // Stage A. Ring budget, because this is a documented failure mode: the engine emits ~3
      // records/frame (390/s at 130 fps), the gauges above are 9/s, these are 8/s -- ~407/s
      // against a DX8WASM_TEL_CAPACITY of 1024. Fits. The comment at the top of this file is the
      // record of the run where that arithmetic was NOT done.
      dx8wasm_tel_gauge("gl.buf_bytes_uploaded", g_accBufBytesUploaded / n);
      dx8wasm_tel_gauge("gl.buf_bytes_locked",   g_accBufBytesLocked   / n);
      // buf_bytes_uploaded counts what is ACTUALLY sent, so once Unlock honours the locked range
      // this ratio collapses toward 1.0 and STAYS there. That makes it a permanent regression
      // detector rather than a one-off diagnosis: anything that goes back to respecifying whole
      // buffers shows up here as the ratio climbing again, in every ordinary capture.
      // Ratio of sums, so one 200 KB buffer with a 2 KB lock is not averaged away by a dozen
      // small fully-written ones. 0.0 means "nothing was locked this window", not "no waste".
      dx8wasm_tel_gauge("gl.buf_waste_ratio",
                        g_accBufBytesLocked > 0.0 ? g_accBufBytesUploaded / g_accBufBytesLocked : 0.0);
      dx8wasm_tel_gauge("gl.buf_whole_locks",    g_accBufWholeLocks    / n);
      dx8wasm_tel_gauge("gl.buf_discard",        g_accBufDiscard       / n);
      dx8wasm_tel_gauge("gl.buf_nooverwrite",    g_accBufNoOverwrite   / n);
      // NOT divided by n. These are defects, and a defect rate of 0.02/frame reads as zero;
      // a raw count of 1 in a window is unmistakable.
      dx8wasm_tel_gauge("gl.buf_lock_oob",         g_accBufLockOob);
      dx8wasm_tel_gauge("gl.buf_unlock_unmatched", g_accBufUnlockUnmatched);
      // GPU frame time (see gpu_frame_tick above). gpu_ms divides by ITS OWN denominator, not
      // n: harvested samples can lag frames by the ring depth, and dividing a sum of K samples
      // by n frames would understate the GPU time by exactly the lag (IM-14's cousin -- never
      // mix denominators). gpu_frames is emitted so the divisor is checkable (IM-12); it reads
      // 0 if the query plumbing is broken (IM-05). Emitted only when the extension probed
      // supported, so absence in a capture means "unsupported context", not "zero GPU cost".
      // disjoint/unmeasured are defect-style raw counts, like the two lines above.
      // Ring budget (IM-07): +4 records/s on top of the ~407/s counted above -> ~411/s
      // against DX8WASM_TEL_CAPACITY 1024. Still fits at 130 fps.
      if (g_gpuTqState > 0) {
        dx8wasm_tel_gauge("gl.gpu_ms",         g_accGpuFrames > 0.0 ? g_accGpuMs / g_accGpuFrames : 0.0);
        dx8wasm_tel_gauge("gl.gpu_frames",     g_accGpuFrames);
        dx8wasm_tel_gauge("gl.gpu_disjoint",   g_accGpuDisjoint);
        dx8wasm_tel_gauge("gl.gpu_unmeasured", g_accGpuUnmeasured);
      }
      g_accFrames = g_accDraws = g_accDrawMs = g_accStateCalls = g_accStateMs = 0.0;
      g_accTexUploads = g_accTexMs = g_accBufUploads = g_accBufMs = g_accPresentMs = 0.0;
      g_accBufBytesUploaded = g_accBufBytesLocked = g_accBufWholeLocks = 0.0;
      g_accBufDiscard = g_accBufNoOverwrite = g_accBufLockOob = g_accBufUnlockUnmatched = 0.0;
      g_accGpuMs = g_accGpuFrames = g_accGpuDisjoint = g_accGpuUnmeasured = 0.0;
      g_lastEmitMs = now;
    }
    return D3D_OK;
  }
  HRESULT BeginScene() override { return D3D_OK; }
  HRESULT EndScene() override { return D3D_OK; }

  HRESULT CreateVertexBuffer(UINT Length, DWORD, DWORD FVF, D3DPOOL, IDirect3DVertexBuffer8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    *out = new VertexBuffer8(Length, FVF); return D3D_OK;
  }
  HRESULT CreateIndexBuffer(UINT Length, DWORD, D3DFORMAT Format, D3DPOOL, IDirect3DIndexBuffer8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    *out = new IndexBuffer8(Length, Format); return D3D_OK;
  }
  HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD, D3DFORMAT Format, D3DPOOL, IDirect3DTexture8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    if (!dxt::is_dxt(Format) && !texfmt::supported(Format)) coverage::unhandled_format(Format);
    *out = new Texture8(Width, Height, Levels, Format); return D3D_OK;  // Levels==0 => full mip chain; DXT/16-bit decoded on upload
  }
  HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* vb, UINT Stride) override {
    // Single-stream fixed-function pipeline: only stream 0 is used. The engine's
    // Apply_Render_State_Changes clears streams 1..N with SetStreamSource(i,null);
    // those must NOT clobber stream 0 (they did when the stream index was ignored).
    if (StreamNumber != 0) return D3D_OK;
    auto* n = static_cast<VertexBuffer8*>(vb);
    if (n) n->AddRef(); if (stream) stream->Release();
    stream = n; stride = Stride; return D3D_OK;
  }
  HRESULT SetIndices(IDirect3DIndexBuffer8* ib, UINT BaseVertexIndex) override {
    auto* n = static_cast<IndexBuffer8*>(ib);
    if (n) n->AddRef(); if (indices) indices->Release();
    indices = n; baseVertexIndex = BaseVertexIndex; return D3D_OK;
  }
  HRESULT SetVertexShader(DWORD Handle) override {
    // bind_pipeline binds position as 3 floats (XYZ) or 4 (XYZRHW). A blended position carries
    // 1-5 extra blend weights, so binding it either way mis-reads every vertex — and until this
    // report existed there was no instrument for it anywhere, which is why the Generals
    // measurement could not say whether the engine uses it. Keyed on the position mask, so the
    // key space is the five blend widths rather than one key per FVF combination.
    // This treats every Handle as an FVF combination, so a real (non-FVF) vertex-shader handle
    // with a non-zero position field would also report here. That is fine only because
    // CreateVertexShader() always refuses (D3DERR_INVALIDCALL) below, so no real shader handle
    // can ever reach this function today — if that changes, this needs to branch on whether
    // Handle is an FVF token before reading it as one.
    const DWORD pos = Handle & D3DFVF_POSITION_MASK;
    if (pos != D3DFVF_XYZ && pos != D3DFVF_XYZRHW && pos != 0)
      coverage::unhandled_vertex_format(pos);
    fvf = Handle;
    return D3D_OK;
  }
  HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture8* t) override {
    ++frameStateCalls; ScopedMs _s(g_frameStateMs);
    auto* n = static_cast<Texture8*>(t);
    Texture8** slot = Stage == 0 ? &texture : (Stage == 1 ? &texture1 : nullptr);
    if (!slot) return D3D_OK;   // only 2 stages
    if (n) n->AddRef(); if (*slot) (*slot)->Release();
    *slot = n; return D3D_OK;
  }
  // Ops the multi-stage combiner (graphics-ff) can emit. Anything else is stored
  // (the shader falls back to MODULATE) but reported to the coverage layer.
  static bool combiner_op_supported(DWORD op) {
    switch (op) {
      case D3DTOP_DISABLE: case D3DTOP_SELECTARG1: case D3DTOP_SELECTARG2:
      case D3DTOP_MODULATE: case D3DTOP_MODULATE2X: case D3DTOP_MODULATE4X:
      case D3DTOP_ADD: case D3DTOP_ADDSIGNED: case D3DTOP_ADDSIGNED2X:
      case D3DTOP_SUBTRACT: case D3DTOP_ADDSMOOTH: case D3DTOP_BLENDTEXTUREALPHA:
      case D3DTOP_BLENDDIFFUSEALPHA: case D3DTOP_BLENDCURRENTALPHA:
      case D3DTOP_BLENDFACTORALPHA: case D3DTOP_DOTPRODUCT3: return true;
      default: return false;
    }
  }
  HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override {
    ++frameStateCalls; ScopedMs _s(g_frameStateMs);
    // Record before the stage cutoff below: D3D's Get returns whatever was Set, whether or not
    // the driver acts on it, and a save/restore bracket depends on exactly that.
    if (Stage < kStageCount && Type < kStageStateCount) tssCache[Stage][Type] = Value;
    if (Stage > 1) return D3D_OK;   // only 2 stages are wired into the combiner (see caps)
    StageState& s = stageState[Stage];
    switch (Type) {
      case D3DTSS_COLOROP:   if (!combiner_op_supported(Value)) coverage::unhandled_texture_op(Value); s.colorOp = Value; break;
      case D3DTSS_ALPHAOP:   if (!combiner_op_supported(Value)) coverage::unhandled_texture_op(Value); s.alphaOp = Value; break;
      case D3DTSS_COLORARG1: s.colorArg1 = Value; break;
      case D3DTSS_COLORARG2: s.colorArg2 = Value; break;
      case D3DTSS_ALPHAARG1: s.alphaArg1 = Value; break;
      case D3DTSS_ALPHAARG2: s.alphaArg2 = Value; break;
      case D3DTSS_TEXCOORDINDEX:          s.tci = Value & 0xffff; s.texgen = (Value >> 16) & 0xffff; break;
      case D3DTSS_TEXTURETRANSFORMFLAGS:  s.ttff = Value; break;
      case D3DTSS_MINFILTER: s.minFilter = Value; break;
      case D3DTSS_MAGFILTER: s.magFilter = Value; break;
      case D3DTSS_MIPFILTER: s.mipFilter = Value; break;
      case D3DTSS_ADDRESSU:  s.addressU  = Value; break;
      case D3DTSS_ADDRESSV:  s.addressV  = Value; break;
      case D3DTSS_MAXANISOTROPY: s.maxAniso = Value ? Value : 1; break;
      // Bump-environment matrix + luminance scale/offset, written as part of the engine's
      // blanket stage-state reset at device init. Inert without D3DTOP_BUMPENVMAP or
      // D3DTOP_BUMPENVMAPLUMINANCE to consume them, and neither op appears anywhere in the
      // measurement (docs/measured-gap.json zero-hit findings) — the game never asked for bump
      // mapping itself. Implementing the matrix without the op would be dead code. The op stays
      // unimplemented and therefore still reported, so this stays discoverable if it ever lands.
      case D3DTSS_BUMPENVMAT00: case D3DTSS_BUMPENVMAT01:
      case D3DTSS_BUMPENVMAT10: case D3DTSS_BUMPENVMAT11:
      case D3DTSS_BUMPENVLSCALE: case D3DTSS_BUMPENVLOFFSET:
        break;
      // Report rather than swallow, matching SetRenderState. MAXMIPLEVEL and LOD bias arrive
      // here and would otherwise vanish without ever showing up in the conformance matrix.
      default: coverage::unhandled_stage_state(Type); break;
    }
    return D3D_OK;
  }
  HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override {
    ++frameStateCalls; ScopedMs _s(g_frameStateMs);
    // Record every state, handled or not, so GetRenderState can report it back. Engines
    // bracket passes with Get(X,&old)/Set(X,temp)/Set(X,old); a Get that reports 0 turns
    // the restore into "disable", which is invisible until the bracketed state matters.
    if (State < kRenderStateCount) rsCache[State] = Value;
    switch (State) {
      case D3DRS_ZENABLE:          zTest = Value != 0; Value ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST); break;
      case D3DRS_ZWRITEENABLE:     zWrite = Value != 0; glDepthMask(zWrite ? GL_TRUE : GL_FALSE); break;
      case D3DRS_ZFUNC:            glDepthFunc(gl_cmpfunc(Value)); break;
      case D3DRS_DITHERENABLE:     Value ? glEnable(GL_DITHER) : glDisable(GL_DITHER); break;
      case D3DRS_ZBIAS:            // legacy 0..16 depth-bias level -> polygon offset
        if (Value) { glPolygonOffset(-(float)Value, -(float)Value); glEnable(GL_POLYGON_OFFSET_FILL); }
        else glDisable(GL_POLYGON_OFFSET_FILL); break;
      case D3DRS_SHADEMODE:        break;   // GOURAUD (our default); FLAT unsupported
      case D3DRS_FILLMODE:
        // Value-sensitive on purpose. SOLID is exactly what this backend draws, so accepting it
        // is not a fallback and must not count — it was 19,392 hits of pure noise in the
        // Generals capture (docs/measured-gap.json). WIREFRAME/POINT genuinely cannot be
        // expressed: GLES3 dropped glPolygonMode, so they keep reporting rather than pretending.
        if (Value != D3DFILL_SOLID) coverage::unhandled_render_state(State);
        break;
      case D3DRS_ALPHABLENDENABLE: alphaBlendEnable = Value != 0; alphaBlendEnable ? glEnable(GL_BLEND) : glDisable(GL_BLEND); break;
      case D3DRS_SRCBLEND:         srcBlend = gl_blend(Value); glBlendFunc(srcBlend, dstBlend); break;
      case D3DRS_DESTBLEND:        dstBlend = gl_blend(Value); glBlendFunc(srcBlend, dstBlend); break;
      case D3DRS_CULLMODE:         apply_cull(Value); break;
      case D3DRS_ALPHATESTENABLE:  alphaTestEnable = Value != 0; break;
      case D3DRS_ALPHAREF:         alphaRef = Value; break;
      case D3DRS_ALPHAFUNC:        alphaFunc = Value; break;
      case D3DRS_LIGHTING:         lighting = Value != 0; break;
      case D3DRS_COLORVERTEX:      colorVertex = Value != 0; break;
      case D3DRS_DIFFUSEMATERIALSOURCE:  diffuseSource = Value; break;
      case D3DRS_AMBIENTMATERIALSOURCE:  ambientSource = Value; break;
      case D3DRS_EMISSIVEMATERIALSOURCE: emissiveSource = Value; break;
      case D3DRS_SPECULARMATERIALSOURCE:
        // COLOR2 would read the specular vertex colour, which is not an attribute here (see the
        // D3DFVF_SPECULAR skip in bind_pipeline) — report that value rather than pretend, and
        // keep sourcing from the material so specular stays correct instead of going black.
        if (Value == D3DMCS_COLOR2) coverage::unhandled_render_state(State);
        else specularSource = Value;
        break;
      case D3DRS_SPECULARENABLE:   specularEnable = Value != 0; break;
      case D3DRS_FOGENABLE:        fogEnable = Value != 0; break;
      case D3DRS_FOGSTART:         fogStart = as_float(Value); break;
      case D3DRS_FOGEND:           fogEnd = as_float(Value); break;
      case D3DRS_FOGCOLOR:
        fogColor[0] = ((Value >> 16) & 0xff) / 255.0f; fogColor[1] = ((Value >> 8) & 0xff) / 255.0f;
        fogColor[2] = (Value & 0xff) / 255.0f; break;
      case D3DRS_FOGTABLEMODE: case D3DRS_FOGVERTEXMODE: {
        // Positive-usage telemetry, deliberately NOT a coverage counter: LINEAR is implemented,
        // so nothing here falls back and fallbacks_taken must not move. It exists because the
        // coverage counter alone could never distinguish "relies on linear fog" from "never
        // touches fog" (docs/CONFORMANCE.md zero-hit findings). Emitted on transitions only —
        // an engine may rewrite this per pass, and a per-occurrence record would crowd the ring
        // for a value that never changed. So a count here is a TRANSITION count, not an
        // occurrence count; do not read it as "how often fog was set".
        uint32_t& last = State == D3DRS_FOGTABLEMODE ? lastFogTableMode : lastFogVertexMode;
        if (last != Value) {
          last = Value;
          char key[DX8WASM_TEL_NAME_MAX];
          // Unlike coverage.cpp's per-kind budget (static_assert against DX8WASM_TEL_NAME_MAX),
          // this key had no compile-time check — a rename here could silently truncate and merge
          // two distinct measurements. "vertex" (6 chars) is the longer of the two %s values.
          static_assert(sizeof("d3d8.fogmode.vertex.") - 1 + 8 < DX8WASM_TEL_NAME_MAX,
                        "fog telemetry key may exceed DX8WASM_TEL_NAME_MAX and get truncated");
          std::snprintf(key, sizeof key, "d3d8.fogmode.%s.%08x",
                        State == D3DRS_FOGTABLEMODE ? "table" : "vertex", (unsigned)Value);
          dx8wasm_tel_counter(key, 1);
        }
        if (Value != D3DFOG_LINEAR && Value != D3DFOG_NONE) coverage::unhandled_render_state(State);
        break;
      }
      case D3DRS_AMBIENT:
        globalAmbient[0] = ((Value >> 16) & 0xff) / 255.0f; globalAmbient[1] = ((Value >> 8) & 0xff) / 255.0f;
        globalAmbient[2] = (Value & 0xff) / 255.0f; globalAmbient[3] = ((Value >> 24) & 0xff) / 255.0f; break;
      case D3DRS_TEXTUREFACTOR:   // ARGB -> RGBA floats for the combiner's TFACTOR arg
        texFactor[0] = ((Value >> 16) & 0xff) / 255.0f; texFactor[1] = ((Value >> 8) & 0xff) / 255.0f;
        texFactor[2] = (Value & 0xff) / 255.0f; texFactor[3] = ((Value >> 24) & 0xff) / 255.0f; break;
      // Color write + stencil: stored, applied together at draw time (apply_raster_masks).
      case D3DRS_COLORWRITEENABLE: colorWrite = Value; break;
      case D3DRS_STENCILENABLE:    stencilEnable = Value != 0; break;
      case D3DRS_STENCILFAIL:      stencilFail = Value; break;
      case D3DRS_STENCILZFAIL:     stencilZFail = Value; break;
      case D3DRS_STENCILPASS:      stencilPass = Value; break;
      case D3DRS_STENCILFUNC:      stencilFunc = Value; break;
      case D3DRS_STENCILREF:       stencilRef = Value; break;
      case D3DRS_STENCILMASK:      stencilMask = Value; break;
      case D3DRS_STENCILWRITEMASK: stencilWriteMask = Value; break;
      // --- Accepted and ignored. Each of these is a decision, not a gap: routing them to the
      // coverage layer would rank them against real missing features, and PATCHSEGMENTS alone
      // (40,138 hits in the Generals capture) would top that ranking forever.
      case D3DRS_PATCHSEGMENTS:
        // Not a render state as far as the engine is concerned: W3D smuggles a float
        // bit-pattern through it as an N-patch tessellation hint
        // (engine/GeneralsX/.../WW3D2/shader.cpp:1036). WebGL2 has no tessellation stage, so
        // there is no implementation to have — only a side channel to ignore.
        break;
      case D3DRS_SOFTWAREVERTEXPROCESSING:
        // A device-pipeline mode switch. This backend has exactly one vertex path (GLSL on the
        // GPU) and no software fallback to switch to, so there is nothing to select.
        break;
      case D3DRS_RANGEFOGENABLE:
        // Set twice at init and never again — almost certainly the engine writing D3D8's own
        // FALSE default during a blanket state reset (the coverage layer records the token, not
        // the value, so this is inferred). If a capture ever shows range fog genuinely enabled,
        // it IS expressible via the existing shader-emulated fog: move it to a real handler then.
        break;
      default: coverage::unhandled_render_state(State); break;
    }
    return D3D_OK;
  }
  HRESULT SetLight(DWORD Index, const D3DLIGHT8* p) override {
    if (!p || Index >= ff::MAX_LIGHTS) return D3DERR_INVALIDCALL;
    lights[Index] = *p; return D3D_OK;
  }
  HRESULT LightEnable(DWORD Index, BOOL Enable) override {
    if (Index < ff::MAX_LIGHTS) lightOn[Index] = Enable != 0; return D3D_OK;
  }
  HRESULT SetMaterial(const D3DMATERIAL8* p) override { if (!p) return D3DERR_INVALIDCALL; material = *p; return D3D_OK; }
  HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override {
    ++frameStateCalls; ScopedMs _s(g_frameStateMs);
    if (!pMatrix) return D3DERR_INVALIDCALL;
    float* dst = State == D3DTS_WORLD ? world : State == D3DTS_VIEW ? view : State == D3DTS_PROJECTION ? proj : nullptr;
    // Stage texture matrices (D3DTS_TEXTURE0 = 16, stage 1 = 17). The terrain
    // macro/cloud passes drive these together with camera-space texgen.
    if (!dst && (State == D3DTS_TEXTURE0 || State == D3DTS_TEXTURE0 + 1))
      dst = texMat[State - D3DTS_TEXTURE0];
    if (!dst) return D3D_OK;   // other transforms ignored for now
    std::memcpy(dst, pMatrix->m, 16 * sizeof(float)); return D3D_OK;
  }
  HRESULT SetViewport(const D3DVIEWPORT8* v) override { if (v) viewport = *v; return D3D_OK; }
  HRESULT GetViewport(D3DVIEWPORT8* v) override { if (v) *v = viewport; return D3D_OK; }

  void apply_cull(DWORD mode) {
    if (mode == D3DCULL_NONE) { glDisable(GL_CULL_FACE); return; }
    glEnable(GL_CULL_FACE); glFrontFace(GL_CCW); glCullFace(mode == D3DCULL_CCW ? GL_FRONT : GL_BACK);
  }
  static GLenum gl_stencilop(DWORD op) {
    switch (op) {
      case D3DSTENCILOP_KEEP:    return GL_KEEP;
      case D3DSTENCILOP_ZERO:    return GL_ZERO;
      case D3DSTENCILOP_REPLACE: return GL_REPLACE;
      case D3DSTENCILOP_INCRSAT: return GL_INCR;
      case D3DSTENCILOP_DECRSAT: return GL_DECR;
      case D3DSTENCILOP_INVERT:  return GL_INVERT;
      case D3DSTENCILOP_INCR:    return GL_INCR_WRAP;
      case D3DSTENCILOP_DECR:    return GL_DECR_WRAP;
      default:                   return GL_KEEP;
    }
  }
  // Color-write mask + stencil, applied per-draw from stored state (Leondore's d3d8webgl
  // model). Deferring to draw time keeps Clear (which must force color-write on) correct.
  void apply_raster_masks() {
    glColorMask((colorWrite & 1) != 0, (colorWrite & 2) != 0, (colorWrite & 4) != 0, (colorWrite & 8) != 0);
    if (stencilEnable) {
      glEnable(GL_STENCIL_TEST);
      glStencilFunc(gl_cmpfunc(stencilFunc ? stencilFunc : D3DCMP_ALWAYS), (GLint)stencilRef,
                    stencilMask ? stencilMask : 0xFFFFFFFF);
      glStencilOp(gl_stencilop(stencilFail), gl_stencilop(stencilZFail), gl_stencilop(stencilPass));
      glStencilMask(stencilWriteMask ? stencilWriteMask : 0xFFFFFFFF);
    } else {
      glDisable(GL_STENCIL_TEST);
    }
  }
  void set_light_uniforms(const ff::Program* p) {
    int type[ff::MAX_LIGHTS];
    float dir[ff::MAX_LIGHTS * 3], pos[ff::MAX_LIGHTS * 3], atten[ff::MAX_LIGHTS * 3];
    float spotDir[ff::MAX_LIGHTS * 3], spotParams[ff::MAX_LIGHTS * 3];
    float range[ff::MAX_LIGHTS], diff[ff::MAX_LIGHTS * 4], amb[ff::MAX_LIGHTS * 4], spec[ff::MAX_LIGHTS * 4];
    int count = 0;
    for (int i = 0; i < ff::MAX_LIGHTS; i++) {
      const D3DLIGHT8& L = lights[i];
      if (!lightOn[i] || (L.Type != D3DLIGHT_DIRECTIONAL && L.Type != D3DLIGHT_POINT && L.Type != D3DLIGHT_SPOT)) continue;
      type[count] = (L.Type == D3DLIGHT_SPOT) ? 2 : (L.Type == D3DLIGHT_POINT) ? 1 : 0;
      float dx = -L.Direction.x, dy = -L.Direction.y, dz = -L.Direction.z;
      float len = std::sqrt(dx * dx + dy * dy + dz * dz); if (len < 1e-6f) len = 1.0f;
      dir[count * 3 + 0] = dx / len; dir[count * 3 + 1] = dy / len; dir[count * 3 + 2] = dz / len;
      pos[count * 3 + 0] = L.Position.x; pos[count * 3 + 1] = L.Position.y; pos[count * 3 + 2] = L.Position.z;
      atten[count * 3 + 0] = L.Attenuation0; atten[count * 3 + 1] = L.Attenuation1; atten[count * 3 + 2] = L.Attenuation2;
      range[count] = L.Range;
      spotDir[count * 3 + 0] = -dir[count * 3 + 0]; spotDir[count * 3 + 1] = -dir[count * 3 + 1]; spotDir[count * 3 + 2] = -dir[count * 3 + 2];
      spotParams[count * 3 + 0] = std::cos(L.Theta * 0.5f); spotParams[count * 3 + 1] = std::cos(L.Phi * 0.5f); spotParams[count * 3 + 2] = L.Falloff;
      std::memcpy(&diff[count * 4], &L.Diffuse.r, 4 * sizeof(float));
      std::memcpy(&amb[count * 4], &L.Ambient.r, 4 * sizeof(float));
      std::memcpy(&spec[count * 4], &L.Specular.r, 4 * sizeof(float));
      count++;
    }
    glUniform1i(p->uLightCount, count);
    if (count) {
      glUniform1iv(p->uLightType, count, type); glUniform3fv(p->uLightDir, count, dir);
      glUniform3fv(p->uLightPos, count, pos); glUniform3fv(p->uLightAtten, count, atten);
      glUniform1fv(p->uLightRange, count, range); glUniform3fv(p->uSpotDir, count, spotDir);
      glUniform3fv(p->uSpotParams, count, spotParams); glUniform4fv(p->uLightDiffuse, count, diff);
      glUniform4fv(p->uLightAmbient, count, amb); glUniform4fv(p->uLightSpecular, count, spec);
    }
    glUniform1i(p->uSpecularEnable, specularEnable ? 1 : 0);
    glUniform1f(p->uMatPower, material.Power);
    glUniform4fv(p->uGlobalAmbient, 1, globalAmbient); glUniform4fv(p->uMatDiffuse, 1, &material.Diffuse.r);
    glUniform4fv(p->uMatAmbient, 1, &material.Ambient.r); glUniform4fv(p->uMatEmissive, 1, &material.Emissive.r);
    glUniform4fv(p->uMatSpecular, 1, &material.Specular.r);
  }
  // Select the FF program, upload uniforms, and bind vertex attributes from the
  // currently-bound GL_ARRAY_BUFFER at the given stride. Shared by the buffer and
  // user-pointer draw paths. Returns false if no program supports the state.
  // Translate a stored D3DTSS_TEXCOORDINDEX texgen mode (high bits, already >>16)
  // into the shader's texgen code. Only camera-space position (the terrain macro/
  // cloud pass) is generated; reflection/normal fall back to the uv set (warned).
  uint32_t texgen_code(uint32_t mode) {
    if (mode == 0) return 0;
    if (mode == (D3DTSS_TCI_CAMERASPACEPOSITION >> 16)) return 1;
    warn_once("texgen mode (reflection/normal) unsupported");
    return 0;
  }
  // Apply a stage's sampler filter/address to the currently-bound GL_TEXTURE_2D.
  void apply_sampler(const StageState& s, const Texture8* t) {
    const bool hasMips = t && t->maxLevel > 0;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_min_filter(s.minFilter, s.mipFilter, hasMips));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_tex_filter(s.magFilter));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_tex_wrap(s.addressU));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_tex_wrap(s.addressV));
    // Clamp to the device limit, not to the request: asking for 16x on hardware that offers 4x
    // is not an error in D3D8, it is a request the driver narrows. limit == 0 means the
    // extension is absent, and then there is nothing to program at all.
    // aniso_limit() caches its query result for the lifetime of the GL context: if that context
    // is lost and recreated, the cached value survives but the extension is not re-queried on the
    // new context, so this can silently no-op after a context loss/recreate. This backend has no
    // context-loss handling at all today, so that is accepted rather than built out here.
    if (const float limit = aniso_limit(); limit > 0.0f && s.maxAniso > 1) {
      // maxAniso == 1 is D3D8's own default (isotropic) and the GL default already matches it,
      // so skip the call rather than reprogram the no-op default on every draw of every stage.
      const float want = (float)s.maxAniso;
      glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyExt, want > limit ? limit : want);
    }
  }
  // vbase: byte offset added to every vertex-attribute pointer. Used to honor D3D8's
  // SetIndices BaseVertexIndex on GLES3 (no glDrawElementsBaseVertex) — the dynamic
  // vertex-buffer ring (render2d/2D UI, dynamesh, particles) writes each batch at a
  // running offset and relies on BaseVertexIndex to point the draw at it.
  bool bind_pipeline(GLsizei vstride, GLintptr vbase = 0) {
    g_dx8_draws++;
    // D3D viewport Y is measured from the TOP; GL's framebuffer is bottom-up. Flip Y so a
    // partial viewport (the in-game 3D view sits above the command bar, i.e. height < the
    // full backbuffer) lands in the correct half instead of the bottom -> otherwise the
    // scene renders shifted down and mouse picking is offset vertically by the same amount.
    // Full-screen viewports (Y=0, Height=backbuffer) are unaffected: vpH-0-vpH == 0.
    glViewport((GLint)viewport.X, (GLint)((int)vpH - (int)viewport.Y - (int)viewport.Height),
               (GLsizei)viewport.Width, (GLsizei)viewport.Height);
    apply_raster_masks();   // color-write mask + stencil, from stored render state
    // FVF texcoord count is (fvf>>8)&0xf sets (D3DFVF_TEX1=0x100, TEX2=0x200, ...),
    // NOT a bitmask. The engine's 2D UI uses TEX2 (0x200); a `& D3DFVF_TEX1` test
    // wrongly reads that as untextured. Treat any texcoord set as "has UVs".
    const int texcoords = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    const bool rhw = fvf & D3DFVF_XYZRHW;
    const bool lit = lighting && (fvf & D3DFVF_NORMAL);
    // RHW vertices are pre-transformed screen-space overlays (2D UI, HUD). They must
    // never be depth-tested against the 3D scene, or the terrain's depth buffer
    // rejects the whole in-game HUD. Force depth off for RHW; restore the engine's
    // D3DRS_ZENABLE intent for 3D draws.
    if (rhw) glDisable(GL_DEPTH_TEST);
    else     zTest ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST);
    // Re-assert blend + depth-write on every draw, like the reference d3d8webgl port's
    // per-draw applyFixedState. Applying these only eagerly in SetRenderState lets a stale
    // GL blend/depth-mask from a prior draw leak in — e.g. 3D smoke particles ending up
    // opaque with depth-write enabled, so each billboard renders as a hard SQUARE instead
    // of soft alpha (2D UI, which sets its own state right before drawing, was unaffected).
    alphaBlendEnable ? glEnable(GL_BLEND) : glDisable(GL_BLEND);
    glBlendFunc(srcBlend, dstBlend);
    glDepthMask(zWrite ? GL_TRUE : GL_FALSE);

    // Build the full program key from the per-stage state. A stage with no texture
    // collapses (stage 0 -> select the diffuse/current color, stage 1 -> disable)
    // so an op sourcing TEXTURE never samples an unbound unit.
    ff::Key key;
    key.fvf = fvf;
    key.alphaFunc = alphaTestEnable ? alphaFunc : 0;
    key.lit = lit;
    key.fog = fogEnable;
    // COLOR1 material sources only apply when lit and the vertex actually carries a
    // diffuse color; otherwise fall back to the material uniform (skinned meshes ship
    // diffuse=0, so gating on FVF diffuse avoids blacking them out).
    const bool cvOn = colorVertex && (fvf & D3DFVF_DIFFUSE);
    key.diffFromVertex = lit && cvOn && diffuseSource == D3DMCS_COLOR1;
    key.ambFromVertex  = lit && cvOn && ambientSource == D3DMCS_COLOR1;
    key.emisFromVertex = lit && cvOn && emissiveSource == D3DMCS_COLOR1;
    // COLOR1 is the only vertex source available (there is no specular attribute), so this is
    // the one non-material case the shader can express.
    key.specFromVertex = lit && cvOn && specularSource == D3DMCS_COLOR1;
    for (int s = 0; s < 2; s++) {
      const StageState& ss = stageState[s];
      Texture8* stex = s == 0 ? texture : texture1;
      ff::Stage& ks = key.stage[s];
      ks.colorOp = ss.colorOp; ks.colorArg1 = ss.colorArg1; ks.colorArg2 = ss.colorArg2;
      ks.alphaOp = ss.alphaOp; ks.alphaArg1 = ss.alphaArg1; ks.alphaArg2 = ss.alphaArg2;
      ks.tci = ss.tci & 1;                       // only vertex uv sets 0/1 are wired
      ks.texgen = texgen_code(ss.texgen);
      ks.xform = (ss.ttff & 0xff) != 0;          // COUNT1..4 -> apply the stage matrix
      ks.hasTex = stex != nullptr && texcoords > 0;
      if (!ks.hasTex) {
        if (s == 0) { ks.colorOp = D3DTOP_SELECTARG2; ks.colorArg2 = D3DTA_DIFFUSE;
                      ks.alphaOp = D3DTOP_SELECTARG2; ks.alphaArg2 = D3DTA_DIFFUSE; }
        else        { ks.colorOp = D3DTOP_DISABLE;    ks.alphaOp = D3DTOP_DISABLE; }
      }
    }
    const ff::Program* p = ff::program_for(key);
    if (!p) { g_dx8_bindfail++; return false; }
    glUseProgram(p->prog);
    glUniformMatrix4fv(p->uWorld, 1, GL_FALSE, world);
    glUniformMatrix4fv(p->uView, 1, GL_FALSE, view);
    glUniformMatrix4fv(p->uProj, 1, GL_FALSE, proj);
    if (p->uTexMat0 >= 0) glUniformMatrix4fv(p->uTexMat0, 1, GL_FALSE, texMat[0]);
    if (p->uTexMat1 >= 0) glUniformMatrix4fv(p->uTexMat1, 1, GL_FALSE, texMat[1]);
    if (p->uTFactor >= 0) glUniform4fv(p->uTFactor, 1, texFactor);

    // Vertex attributes. Memory layout: pos, [normal], [diffuse], [specular],
    // then the texcoord sets (2 floats each). Locations: 0 pos, 1 diffuse,
    // 2 uv-set0, 3 normal, 4 uv-set1 (matches the shader's `layout(location=)`).
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, rhw ? 4 : 3, GL_FLOAT, GL_FALSE, vstride, (void*)(uintptr_t)vbase);
    GLuint off = rhw ? 16 : 12;
    if (fvf & D3DFVF_NORMAL) { glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, vstride, (void*)(uintptr_t)(vbase + off)); off += 12; }
    else glDisableVertexAttribArray(3);
    if (fvf & D3DFVF_DIFFUSE) { glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, vstride, (void*)(uintptr_t)(vbase + off)); off += 4; }
    else glDisableVertexAttribArray(1);
    if (fvf & D3DFVF_SPECULAR) off += 4;         // present in some passes; skipped, keeps uv offsets right
    const GLuint uvBase = off;
    if (texcoords > 0) { glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, vstride, (void*)(uintptr_t)(vbase + uvBase)); }
    else glDisableVertexAttribArray(2);
    if (texcoords > 1) { glEnableVertexAttribArray(4); glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, vstride, (void*)(uintptr_t)(vbase + uvBase + 2 * sizeof(float))); }
    else glDisableVertexAttribArray(4);

    // Bind both texture stages (stage 0 -> unit 0, stage 1 -> unit 1).
    if (p->uTex >= 0) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture ? texture->tex : 0); glUniform1i(p->uTex, 0);
      if (texture) apply_sampler(stageState[0], texture); }
    if (p->uTex1 >= 0) { glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texture1 ? texture1->tex : 0); glUniform1i(p->uTex1, 1);
      if (texture1) apply_sampler(stageState[1], texture1); }

    if (p->uAlphaRef >= 0) glUniform1f(p->uAlphaRef, alphaRef / 255.0f);
    if (rhw) glUniform2f(p->uViewport, vpW, vpH);
    if (lit) set_light_uniforms(p);
    if (fogEnable) { glUniform3fv(p->uFogColor, 1, fogColor); glUniform1f(p->uFogStart, fogStart); glUniform1f(p->uFogEnd, fogEnd); }
    return true;
  }
  HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, UINT, UINT, UINT StartIndex, UINT PrimitiveCount) override {
    DrawTimer _dt(this);
    GLenum mode; GLsizei icount;
    if (!stream || !indices || !prim_info(Type, PrimitiveCount, mode, icount)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ARRAY_BUFFER, stream->b.glbuf);
    // Honor D3D8 SetIndices BaseVertexIndex by offsetting the attribute pointers
    // (GLES3 has no glDrawElementsBaseVertex). Indices stay 0-based as the engine wrote them.
    if (!bind_pipeline((GLsizei)stride, (GLintptr)baseVertexIndex * stride)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->b.glbuf);
    glDrawElements(mode, icount, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(StartIndex * sizeof(uint16_t)));
    return D3D_OK;
  }

  // --- ABI-complete stubs (log-once / sensible defaults; Phase C) --------------
  HRESULT TestCooperativeLevel() override { return D3D_OK; }
  // No texture-memory accounting exists, and WebGL exposes none. A made-up figure is not
  // harmless: engines feed it into quality heuristics (Generals' GameLOD switches behaviour at
  // exactly 256 MB). 0 reads as "unknown" and cannot masquerade as a real budget.
  UINT GetAvailableTextureMem() override { warn_once("GetAvailableTextureMem"); return 0; }
  HRESULT ResourceManagerDiscardBytes(DWORD) override { return D3D_OK; }
  HRESULT GetDirect3D(IDirect3D8** o) override { if (o) *o = nullptr; warn_once("GetDirect3D"); return D3DERR_INVALIDCALL; }
  HRESULT GetDeviceCaps(D3DCAPS8* c) override {
    if (!c) return D3DERR_INVALIDCALL;
    // Report the SAME full cap set as IDirect3D8::GetDeviceCaps (shared caps_fill.h).
    // The engine's runtime filter/feature selection (DX8Caps::Init_Caps) queries THIS
    // device object; the old near-empty caps here (TextureFilterCaps=0) made it think
    // the GPU had no bilinear filtering, downgrading every texture to nearest -> blocky.
    fill_caps(c);
    return D3D_OK;
  }
  HRESULT GetDisplayMode(D3DDISPLAYMODE* m) override {
    if (m) { m->Width = (UINT)vpW; m->Height = (UINT)vpH; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8; }
    return D3D_OK;
  }
  HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) override {
    if (p) { std::memset(p, 0, sizeof *p); p->DeviceType = D3DDEVTYPE_HAL; p->BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING; }
    return D3D_OK;
  }
  HRESULT SetCursorProperties(UINT, UINT, IDirect3DSurface8*) override { return D3D_OK; }
  void SetCursorPosition(UINT, UINT, DWORD) override {}
  BOOL ShowCursor(BOOL) override { return 1; }
  HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain8**) override { warn_once("CreateAdditionalSwapChain"); return D3DERR_INVALIDCALL; }
  HRESULT Reset(D3DPRESENT_PARAMETERS* pp) override {
    if (pp && pp->BackBufferWidth) { vpW = (float)pp->BackBufferWidth; vpH = (float)pp->BackBufferHeight;
      viewport = {0, 0, pp->BackBufferWidth, pp->BackBufferHeight, 0.0f, 1.0f}; }
    return D3D_OK;
  }
  // Backbuffer as a standalone Surface8 sized to the framebuffer. Enough for the
  // engine to query its description; pixel readback (glReadPixels) is a later
  // refinement (the smudge/distortion effects that copy from it).
  HRESULT GetBackBuffer(UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface8** o) override {
    if (!o) return D3DERR_INVALIDCALL;
    UINT bw = viewport.Width ? viewport.Width : 1, bh = viewport.Height ? viewport.Height : 1;
    *o = new Surface8(bw, bh, D3DFMT_X8R8G8B8);
    return D3D_OK;
  }
  HRESULT GetRasterStatus(D3DRASTER_STATUS* s) override { if (s) { s->InVBlank = 0; s->ScanLine = 0; } return D3D_OK; }
  void SetGammaRamp(DWORD, const D3DGAMMARAMP*) override {}
  void GetGammaRamp(D3DGAMMARAMP*) override {}
  HRESULT CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, void** o) override { if (o) *o = nullptr; warn_once("CreateVolumeTexture"); return D3DERR_INVALIDCALL; }
  HRESULT CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, void** o) override { if (o) *o = nullptr; warn_once("CreateCubeTexture"); return D3DERR_INVALIDCALL; }
  HRESULT CreateRenderTarget(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, BOOL, IDirect3DSurface8** o) override { if (o) *o = nullptr; warn_once("CreateRenderTarget"); return D3DERR_INVALIDCALL; }
  HRESULT CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, IDirect3DSurface8** o) override { if (o) *o = nullptr; warn_once("CreateDepthStencilSurface"); return D3DERR_INVALIDCALL; }
  HRESULT CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8** o) override {
    if (!o) return D3DERR_INVALIDCALL;
    if (!texfmt::supported(Format)) coverage::unhandled_format(Format);
    *o = new Surface8(Width, Height, Format); return D3D_OK;
  }
  // Row-copy src surface region into dst (both 32-bit); re-upload if dst is a
  // texture level. Rects null => whole surface. dstPoints null => same origin.
  HRESULT CopyRects(IDirect3DSurface8* src, const RECT* srcRects, UINT n,
                    IDirect3DSurface8* dst, const POINT* dstPoints) override {
    auto* s = static_cast<Surface8*>(src); auto* d = static_cast<Surface8*>(dst);
    if (!s || !d) return D3DERR_INVALIDCALL;
    // Bytes per pixel comes from the surface format, NOT a hardcoded 4. The shroud
    // (fog of war) copies an R5G6B5 (2 bpp) src into its R5G6B5 dst texture every
    // frame; using 4 here doubled every stride/offset and filled the dst with
    // misaligned garbage that projected onto terrain as cyan/green/black tiles.
    const UINT bpp = texfmt::bpp(s->fmt);
    UINT count = n ? n : 1;
    for (UINT i = 0; i < count; ++i) {
      RECT r = srcRects ? srcRects[i] : RECT{0, 0, (LONG)s->w, (LONG)s->h};
      POINT p = dstPoints ? dstPoints[i] : POINT{r.left, r.top};
      UINT rw = (UINT)(r.right - r.left), rh = (UINT)(r.bottom - r.top);
      for (UINT y = 0; y < rh; ++y) {
        const BYTE* sp = s->base() + (size_t)(r.top + y) * (s->w * bpp) + (size_t)r.left * bpp;
        BYTE* dp = d->base() + (size_t)(p.y + y) * (d->w * bpp) + (size_t)p.x * bpp;
        std::memcpy(dp, sp, (size_t)rw * bpp);
      }
    }
    if (d->parent) d->parent->upload_level(d->level);
    return D3D_OK;
  }
  // Copy every matching mip level src->dst (CPU) and re-upload each.
  HRESULT UpdateTexture(IDirect3DBaseTexture8* src, IDirect3DBaseTexture8* dst) override {
    auto* s = static_cast<Texture8*>(src); auto* d = static_cast<Texture8*>(dst);
    if (!s || !d) return D3DERR_INVALIDCALL;
    size_t n = s->levels.size() < d->levels.size() ? s->levels.size() : d->levels.size();
    for (size_t l = 0; l < n; ++l) {
      if (s->levels[l].w == d->levels[l].w && s->levels[l].h == d->levels[l].h)
        d->levels[l].px = s->levels[l].px;
      d->upload_level((UINT)l);
    }
    return D3D_OK;
  }
  HRESULT GetFrontBuffer(IDirect3DSurface8*) override { warn_once("GetFrontBuffer"); return D3DERR_INVALIDCALL; }
  // Only the backbuffer exists (CreateRenderTarget and CreateDepthStencilSurface both refuse),
  // so a request to render elsewhere must be refused rather than silently drawn to the screen.
  // A null target means "restore the default", which is where we already are.
  HRESULT SetRenderTarget(IDirect3DSurface8* target, IDirect3DSurface8*) override {
    if (!target) return D3D_OK;
    warn_once("SetRenderTarget"); return D3DERR_INVALIDCALL;
  }
  HRESULT GetRenderTarget(IDirect3DSurface8** o) override { if (o) *o = nullptr; return D3DERR_INVALIDCALL; }
  HRESULT GetDepthStencilSurface(IDirect3DSurface8** o) override { if (o) *o = nullptr; return D3DERR_INVALIDCALL; }
  HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* m) override {
    if (!m) return D3DERR_INVALIDCALL;
    const float* s = State == D3DTS_WORLD ? world : State == D3DTS_VIEW ? view : State == D3DTS_PROJECTION ? proj : nullptr;
    if (s) std::memcpy(m->m, s, 16 * sizeof(float)); else std::memset(m, 0, sizeof *m);
    return D3D_OK;
  }
  HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) override { warn_once("MultiplyTransform"); return D3D_OK; }
  HRESULT GetMaterial(D3DMATERIAL8* m) override { if (m) *m = material; return D3D_OK; }
  HRESULT GetLight(DWORD i, D3DLIGHT8* l) override { if (l && i < ff::MAX_LIGHTS) *l = lights[i]; return D3D_OK; }
  HRESULT GetLightEnable(DWORD i, BOOL* e) override { if (e) *e = (i < ff::MAX_LIGHTS && lightOn[i]) ? 1 : 0; return D3D_OK; }
  // No user clip planes are implemented (caps advertise MaxUserClipPlanes = 0 to match), and
  // GetClipPlane would otherwise leave the caller's buffer untouched while reporting success —
  // an unwritten buffer is worse than a zeroed one because the garbage is nondeterministic.
  HRESULT SetClipPlane(DWORD, const float*) override { warn_once("SetClipPlane"); return D3DERR_INVALIDCALL; }
  HRESULT GetClipPlane(DWORD, float*) override { warn_once("GetClipPlane"); return D3DERR_INVALIDCALL; }
  HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD* v) override {
    if (!v) return D3DERR_INVALIDCALL;
    *v = State < kRenderStateCount ? rsCache[State] : 0;
    return D3D_OK;
  }
  // State blocks record nothing. Reporting success would make "restore" a silent no-op — the
  // same class of failure as a GetRenderState that always answers zero, just via another API.
  HRESULT BeginStateBlock() override { warn_once("BeginStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT EndStateBlock(DWORD* t) override { if (t) *t = 0; warn_once("EndStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT ApplyStateBlock(DWORD) override { warn_once("ApplyStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT CaptureStateBlock(DWORD) override { warn_once("CaptureStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT DeleteStateBlock(DWORD) override { return D3D_OK; }   // deleting nothing is honest
  HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE, DWORD* t) override { if (t) *t = 0; warn_once("CreateStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT SetClipStatus(const D3DCLIPSTATUS8*) override { return D3D_OK; }
  HRESULT GetClipStatus(D3DCLIPSTATUS8* s) override { if (s) { s->ClipUnion = 0; s->ClipIntersection = 0xffffffff; } return D3D_OK; }
  HRESULT GetTexture(DWORD, IDirect3DBaseTexture8** o) override { if (o) { *o = texture; if (texture) texture->AddRef(); } return D3D_OK; }
  HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* v) override {
    if (!v) return D3DERR_INVALIDCALL;
    if (Stage >= kStageCount || Type >= kStageStateCount) return D3DERR_INVALIDCALL;
    *v = tssCache[Stage][Type];
    return D3D_OK;
  }
  HRESULT ValidateDevice(DWORD* n) override { if (n) *n = 1; return D3D_OK; }
  HRESULT GetInfo(DWORD, void*, DWORD) override { return D3DERR_INVALIDCALL; }
  // No palettized path exists; GetPaletteEntries would report success without writing a single
  // entry, leaving the caller to read whatever was already in its buffer.
  HRESULT SetPaletteEntries(UINT, const PALETTEENTRY*) override { warn_once("SetPaletteEntries"); return D3DERR_INVALIDCALL; }
  HRESULT GetPaletteEntries(UINT, PALETTEENTRY*) override { warn_once("GetPaletteEntries"); return D3DERR_INVALIDCALL; }
  HRESULT SetCurrentTexturePalette(UINT) override { return D3D_OK; }
  HRESULT GetCurrentTexturePalette(UINT* n) override { if (n) *n = 0; return D3D_OK; }
  HRESULT DrawPrimitive(D3DPRIMITIVETYPE Type, UINT StartVertex, UINT PrimitiveCount) override {
    DrawTimer _dt(this);
    GLenum mode; GLsizei vcount;
    if (!stream || !prim_info(Type, PrimitiveCount, mode, vcount)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ARRAY_BUFFER, stream->b.glbuf);
    if (!bind_pipeline((GLsizei)stride)) return D3DERR_INVALIDCALL;
    glDrawArrays(mode, (GLint)StartVertex, vcount);
    return D3D_OK;
  }
  // User-pointer draws: vertex/index data is inline (no D3D buffer). Stream it
  // through reused scratch GL buffers. Common for UI/particles/dynamic geometry.
  HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE Type, UINT PrimitiveCount, const void* pVertexData, UINT VertexStride) override {
    DrawTimer _dt(this);
    GLenum mode; GLsizei vcount;
    if (!pVertexData || !prim_info(Type, PrimitiveCount, mode, vcount)) return D3DERR_INVALIDCALL;
    if (!scratchVB) glGenBuffers(1, &scratchVB);
    glBindBuffer(GL_ARRAY_BUFFER, scratchVB);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vcount * VertexStride, pVertexData, GL_STREAM_DRAW);
    if (!bind_pipeline((GLsizei)VertexStride)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDrawArrays(mode, 0, vcount);
    return D3D_OK;
  }
  HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE Type, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount,
                                 const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexData, UINT VertexStride) override {
    DrawTimer _dt(this);
    GLenum mode; GLsizei icount;
    if (!pIndexData || !pVertexData || !prim_info(Type, PrimitiveCount, mode, icount)) return D3DERR_INVALIDCALL;
    const bool i32 = IndexDataFormat == D3DFMT_INDEX32;
    const GLenum itype = i32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    if (!scratchVB) glGenBuffers(1, &scratchVB);
    if (!scratchIB) glGenBuffers(1, &scratchIB);
    glBindBuffer(GL_ARRAY_BUFFER, scratchVB);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MinVertexIndex + NumVertices) * VertexStride, pVertexData, GL_STREAM_DRAW);
    if (!bind_pipeline((GLsizei)VertexStride)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scratchIB);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)icount * (i32 ? 4 : 2), pIndexData, GL_STREAM_DRAW);
    glDrawElements(mode, icount, itype, (void*)0);
    return D3D_OK;
  }
  HRESULT ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer8*, DWORD) override { warn_once("ProcessVertices"); return D3DERR_INVALIDCALL; }
  // Nothing is compiled, so creation must fail and the caller must take its fixed-function
  // fallback path rather than believing it holds a shader.
  HRESULT CreateVertexShader(const DWORD*, const DWORD*, DWORD* h, DWORD) override { if (h) *h = 0; warn_once("CreateVertexShader"); return D3DERR_INVALIDCALL; }
  HRESULT GetVertexShader(DWORD* h) override { if (h) *h = fvf; return D3D_OK; }
  HRESULT DeleteVertexShader(DWORD) override { return D3D_OK; }
  HRESULT SetVertexShaderConstant(DWORD, const void*, DWORD) override { return D3D_OK; }
  // No constant store exists — reporting success without writing leaves the caller reading
  // uninitialised memory and calling it shader state.
  HRESULT GetVertexShaderConstant(DWORD, void*, DWORD) override { warn_once("GetVertexShaderConstant"); return D3DERR_INVALIDCALL; }
  HRESULT GetVertexShaderDeclaration(DWORD, void*, DWORD*) override { return D3DERR_INVALIDCALL; }
  HRESULT GetVertexShaderFunction(DWORD, void*, DWORD*) override { return D3DERR_INVALIDCALL; }
  HRESULT GetStreamSource(UINT, IDirect3DVertexBuffer8** o, UINT* s) override { if (o) { *o = stream; if (stream) stream->AddRef(); } if (s) *s = stride; return D3D_OK; }
  HRESULT GetIndices(IDirect3DIndexBuffer8** o, UINT* base) override { if (o) { *o = indices; if (indices) indices->AddRef(); } if (base) *base = 0; return D3D_OK; }
  HRESULT SetPixelShader(DWORD) override { warn_once("SetPixelShader"); return D3D_OK; }
  HRESULT GetPixelShader(DWORD* h) override { if (h) *h = 0; return D3D_OK; }
  HRESULT CreatePixelShader(const DWORD*, DWORD* h) override { if (h) *h = 0; warn_once("CreatePixelShader"); return D3DERR_INVALIDCALL; }
  HRESULT DeletePixelShader(DWORD) override { return D3D_OK; }
  HRESULT SetPixelShaderConstant(DWORD, const void*, DWORD) override { return D3D_OK; }
  HRESULT GetPixelShaderConstant(DWORD, void*, DWORD) override { warn_once("GetPixelShaderConstant"); return D3DERR_INVALIDCALL; }
  HRESULT GetPixelShaderFunction(DWORD, void*, DWORD*) override { return D3DERR_INVALIDCALL; }
  HRESULT DrawRectPatch(UINT, const float*, const D3DRECTPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
  HRESULT DrawTriPatch(UINT, const float*, const D3DTRIPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
  HRESULT DeletePatch(UINT) override { return D3D_OK; }
};
} // namespace

IDirect3DDevice8* dx8_create_device(int w, int h) {
  if (!platform::create_gl_context(w, h)) return nullptr;
  return new Device8(w, h);
}
