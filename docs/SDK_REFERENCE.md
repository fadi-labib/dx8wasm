# dx8wasm SDK reference

The complete public surface for building a game or app against dx8wasm. Three
parts: the **D3D8 API** (stock DirectX 8, what the game calls), the **runtime
contract** (dx8wasm-specific: init, input, coverage), and the **compatlib**
(Win32→POSIX). Plus the load-bearing **conventions** you must respect.

Everything here is C/C++ against an Emscripten toolchain (WebGL2 / GLES3). See
[`INTEGRATION.md`](INTEGRATION.md) for build/link and [`CONFORMANCE.md`](CONFORMANCE.md)
for exactly which tokens are implemented vs fall back.

---

## Conventions (read first — these are load-bearing)

- **Color**: `D3DCOLOR` is `0xAARRGGBB`. In little-endian memory that's bytes
  `[B,G,R,A]`. dx8wasm recovers RGBA with a `.bgra` shader swizzle, so pass colors
  as normal `0xAARRGGBB` — do not pre-swizzle.
- **Matrices**: `D3DMATRIX` is row-major, applied as row vectors (`v' = v·M`), the
  standard D3D convention. Set `D3DTS_WORLD/VIEW/PROJECTION`; the device handles
  the GL transpose internally.
- **Coordinates**: transformed vertices are in the usual D3D clip space. For
  pre-transformed `D3DFVF_XYZRHW` vertices, positions are in **screen pixels**
  (top-left origin), `z` in `[0,1]`, `rhw = 1`.
- **The device owns the GL viewport** — you drive only D3D8, never GL directly.
- **No blocking loop**: the browser can't block. Convert the game loop to an
  `emscripten_set_main_loop` callback.

### Stubs fail loudly

dx8wasm implements a subset of D3D8. Where it stops, it says so — it never returns a
plausible-looking value it made up. Three rules hold across the whole surface:

1. **A `Get*` for state the device tracks answers from that state.** `GetRenderState`,
   `GetTextureStageState`, `GetTransform`, `GetMaterial`, `GetLight` all round-trip what you
   set, because engines bracket passes with `Get(X,&old)` / `Set(X,temp)` / `Set(X,old)`.
2. **Anything unimplemented returns `D3DERR_INVALIDCALL` or `D3DERR_NOTAVAILABLE`** — never
   `D3D_OK`. That includes state blocks, shader creation, clip planes, palettes and
   render-target switching. Expect failures and take your fallback path; do not assume success.
3. **Capability queries derive from the same predicate the implementation uses.**
   `CheckDeviceFormat` answers from `runtime/d3d8webgl/format_support.h`, the header the texture
   upload path itself enforces, so caps and behaviour cannot drift apart. `D3DCAPS8` advertises
   only what the device will actually do.

Read the gaps at runtime with `dx8wasm_get_coverage` (§2) — unhandled render states, texture
ops, stage states and formats are each counted and reported once.

**Why this is a rule.** `GetRenderState` used to answer `0` for everything. Generals' stencil
shadow pass does `GetRenderState(D3DRS_COLORWRITEENABLE, &old)` → `Set(…, 0)` → `Set(…, old)`,
so the restore restored *zero* and colour writes stayed off for the rest of the frame. The 3D
scene, drawn before that pass, kept appearing; the entire 2D UI, drawn after it, vanished. It
presented as a missing menu, not as a device error — which is exactly what a stub that reports
success buys you. A stub that fails is debuggable in minutes; one that lies is not.

### Accepted without acting ≠ unimplemented

A coverage counter means one thing: *this backend does not implement the token and fell back*.
Some D3D8 states are deliberately accepted and ignored, and those must **not** count — the
distinction is what keeps `dx8wasm_get_coverage` useful as a work list.

- `D3DRS_FILLMODE(D3DFILL_SOLID)` is exactly what the backend draws, so accepting it is exact.
  `WIREFRAME`/`POINT` still count: GLES3 has no `glPolygonMode`, so they genuinely cannot be
  expressed.
- `D3DRS_PATCHSEGMENTS`, `D3DRS_SOFTWAREVERTEXPROCESSING`, `D3DRS_RANGEFOGENABLE` and the six
  `D3DTSS_BUMPENV*` states are no-ops with the reason written at each call site in
  `runtime/d3d8webgl/device.cpp`.

Why it matters: while these shared a counter with real gaps, `D3DRS_PATCHSEGMENTS` — a float bit
pattern the engine smuggles through a render state, not a rendering request at all — was the
most-hit token in every scenario of a real capture, outranking every genuine finding. Ranking by
hit count is only meaningful once decisions stop being counted as gaps.

When you add a no-op, write the reason at the call site and add it to `accepted_states_smoke`,
which asserts that the accepted set moves no counter *and* that a genuinely-unexpressible token
still does — a smoke that only checked the first half could pass by silencing everything.

### Determinism seam

`runtime/test/frame_digest.h` folds a `glReadPixels` into a chained FNV-1a digest.
`determinism_smoke` uses it to prove one render sequence repeats identically in-process, and
`scripts/determinism.mjs` (in `ci.sh`) proves the digest reproduces across fresh browser
contexts. Today's digested sequence is clears only (no draw call), so this currently proves
bit-for-bit stability of the clear/present/readback path, not yet shader-cache-key stability —
that needs the sequence to include a draw. A game with replays extends the same pattern by
digesting its own per-tick simulation state and comparing across runs — that is the desync check,
and this is the SDK-side half of it.

---

## 1. D3D8 API — `runtime/d3d8/d3d8.h`

### Entry point
```c
IDirect3D8* Direct3DCreate8(UINT SDKVersion);   // pass D3D_SDK_VERSION
```

### IDirect3D8
```c
HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE, HWND hFocusWindow, DWORD BehaviorFlags,
                     D3DPRESENT_PARAMETERS*, IDirect3DDevice8** out);
uint32_t AddRef();  uint32_t Release();
```
`D3DPRESENT_PARAMETERS` fields used: `BackBufferWidth/Height`, `BackBufferFormat`
(`D3DFMT_X8R8G8B8`/`A8R8G8B8`), `SwapEffect` (`D3DSWAPEFFECT_DISCARD`), `Windowed`.
The backbuffer size sets the GL viewport.

### IDirect3DDevice8
Lifecycle / frame:
```c
HRESULT Clear(uint32_t Count, const D3DRECT*, uint32_t Flags, D3DCOLOR, float Z, uint32_t Stencil);
        // Flags: D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER
HRESULT Present(const D3DRECT* src, const D3DRECT* dst, HWND, const void* dirty);
```
Resources:
```c
HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL, IDirect3DVertexBuffer8** out);
HRESULT CreateIndexBuffer (UINT Length, DWORD Usage, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer8** out);
HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT, D3DPOOL,
                      IDirect3DTexture8** out);
// IDirect3DVertexBuffer8 / IndexBuffer8: Lock(UINT off, UINT size, BYTE** ppData, DWORD flags); Unlock();
// IDirect3DTexture8:                     LockRect(UINT level, D3DLOCKED_RECT*, const D3DRECT*, DWORD); UnlockRect(UINT);
```
Binding + state:
```c
HRESULT SetStreamSource(UINT Stream, IDirect3DVertexBuffer8*, UINT Stride);  // AddRefs the buffer
HRESULT SetIndices(IDirect3DIndexBuffer8*, UINT BaseVertexIndex);            // BaseVertexIndex ignored (assumed 0)
HRESULT SetVertexShader(DWORD Handle);           // pass an FVF code (fixed-function)
HRESULT SetTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX*);   // WORLD / VIEW / PROJECTION
HRESULT SetTexture(DWORD Stage, IDirect3DTexture8*);            // stage 0
HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE, DWORD Value);  // D3DTSS_COLOROP
HRESULT SetRenderState(D3DRENDERSTATETYPE, DWORD Value);
HRESULT SetLight(DWORD Index, const D3DLIGHT8*);   // up to 8
HRESULT LightEnable(DWORD Index, BOOL);
HRESULT SetMaterial(const D3DMATERIAL8*);
```
Draw:
```c
HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE, UINT MinIndex, UINT NumVertices,
                             UINT StartIndex, UINT PrimitiveCount);
```

### FVF codes (vertex layout, in this order)
`D3DFVF_XYZ` (0x002) | `D3DFVF_XYZRHW` (0x004, pre-transformed) | `D3DFVF_NORMAL`
(0x010) | `D3DFVF_DIFFUSE` (0x040) | `D3DFVF_TEX1` (0x100). Attribute order in the
vertex struct: position, normal, diffuse, texcoord. `XYZRHW` excludes lighting.

### ABI note
`IDirect3DDevice8` is the **complete standard vtable** (~94 methods, canonical
order). The supported subset does real work; unimplemented methods are honest
stubs (log-once / coverage / sensible defaults), so a game links and dispatches
correctly. Missing *behavior* surfaces via the coverage layer, never a crash.

### What's supported
All three light types (directional/point/spot), the full ambient+diffuse+specular
equation, **lit + textured** geometry, linear fog, the common depth/blend/cull/
alpha-test/z-func render states, `MODULATE`/`MODULATE2X`/`MODULATE4X`/`ADD`/
`ADDSIGNED`/`SELECTARG1`/`SELECTARG2` stage-0 combiners, `A8R8G8B8`/`X8R8G8B8`
textures, every primitive type, and indexed / non-indexed / **user-pointer**
(`DrawPrimitiveUP`/`DrawIndexedPrimitiveUP`) draws. **Authoritative, current list:
[`CONFORMANCE.md`](CONFORMANCE.md).** Not-yet-supported (grows against the game):
a second texture stage, more formats, `.pso` pixel shaders — anything unlisted
falls back and is counted.

---

## 2. Runtime contract — `runtime/include/dx8wasm/contract.h`

Call `dx8wasm_init` once before `Direct3DCreate8`, then `dx8wasm_pump` each frame.

```c
int  dx8wasm_init(const dx8wasm_init_desc*);   // {abi_version, backend, canvas_selector, srgb, log_unimplemented}; 0 on success
void dx8wasm_shutdown(void);
int  dx8wasm_has_cap(dx8wasm_cap);             // 1 if the active backend supports the cap (advanced caps: currently 0)

// Input — the seam replacing the Win32 message pump.
void dx8wasm_pump(dx8wasm_input* out);         // drains events; fills keys[256] (SDL scancode), mouse_x/y,
                                               // mouse_buttons (bit0 L, bit1 R, bit2 M), wheel, quit

// Coverage — discover what isn't implemented, as data.
void dx8wasm_get_coverage(dx8wasm_coverage*);  // {unhandled_render_states, _texture_stage_ops, _formats, fallbacks_taken}
void dx8wasm_set_unhandled_callback(void (*cb)(const char* kind, uint32_t value, void* user), void* user);
                                               // fires once per distinct unhandled token; kind is "D3DRS_"/"D3DTOP_"/"D3DFMT_"
```

---

## 3. compatlib (Win32→POSIX) — `runtime/compatlib/win32.h`, `d3dx8math.h`

Include `compatlib/win32.h` where you'd include `<windows.h>`. Types align with
`d3d8.h`; `<tchar.h>`-style generic-name macros map `CreateFile`→`CreateFileA`, etc.
Full surface and roadmap: [`COMPATLIB.md`](COMPATLIB.md).

- **Tier 0 — timing/debug**: `timeGetTime`, `GetTickCount`, `QueryPerformanceCounter/Frequency` (QPF = 1e6), `Sleep`, `timeBeginPeriod`, `OutputDebugStringA`. All on one monotonic clock.
- **Tier 1 — files/dirs/memory**: `CreateFile`/`ReadFile`/`WriteFile`/`CloseHandle`/`SetFilePointer`/`GetFileSize`/`GetFileAttributes`; `FindFirstFile`/`FindNextFile`/`FindClose`; `CreateDirectory`/`Get`/`SetCurrentDirectory`; `SHGetSpecialFolderLocation`/`SHGetPathFromIDList` (→ `/userdata`); `GlobalAlloc`/`GlobalFree`/`GlobalSize`/`GlobalMemoryStatus`. Backslash paths auto-normalized. (64-bit file offsets unsupported.)
- **Tier 2 — modules/threads/registry**: `LoadLibrary`/`GetProcAddress`/`FreeLibrary`/`GetModuleFileName` (static-link stubs); `CreateThread` **runs synchronously** (single-threaded), `GetCurrentThreadId`/`TerminateThread`; `Reg{Open,Create}KeyEx`/`Reg{Query,Set}ValueEx`/`RegCloseKey` (in-memory store; missing reads → `ERROR_FILE_NOT_FOUND`).
- **Tier 3 (math)** — `d3dx8math.h`: `D3DXMatrix` Identity/Multiply/Transpose/Inverse/Translation/Scaling/RotationZ, `D3DXVec3/4Transform`, `D3DXGetFVFVertexSize`. Row-major, matches `D3DMATRIX`.

**Not yet built** (grow against your build, per COMPATLIB.md): D3DX texture loading
+ SM1.x shader assembly, sockets/winsock, COM smart pointers (`_com_ptr_t`), video.

---

## 4. On-demand OPFS reads — `runtime/include/dx8wasm/opfs.h`

Optional. For a game whose data is large read-only archives, this serves ranged reads from the
browser's Origin Private File System instead of holding the archives in the wasm heap. Measured in
the Generals port: 1133 MiB of archive resident → 0, whole-browser Pss 1892 → 807 MiB.

```c
int       dx8wasm_opfs_init(void);            // allocate the control block; 1 if this call did it
uintptr_t dx8wasm_opfs_control_addr(void);    // hand to the page: {memory: wasmMemory.buffer, addr}
int       dx8wasm_opfs_ready(void);           // 1 once an I/O worker answered; doubles as the feature flag
int       dx8wasm_opfs_count(void);
int       dx8wasm_opfs_index_of(const char* name);   // by base name, -1 if not registered
int       dx8wasm_opfs_size_of(int idx);
int       dx8wasm_opfs_read(int idx, uint32_t offset, void* dst, uint32_t len);  // bytes read, or -1
```

**Call `dx8wasm_opfs_read` only from a thread that may block** — under `-pthread`/`PROXY_TO_PTHREAD`
that is the engine thread, which is a Web Worker. It publishes a request into a control block that
lives in the wasm heap (already a `SharedArrayBuffer`, so no separate SAB is needed) and sleeps on
`Atomics.wait` until an I/O worker fills a 4 MiB data window. Requests larger than the window are
chunked internally and the result is contiguous.

**Why not an Emscripten filesystem backend or `FS.registerDevice`.** Measured: a `read()` syscall's JS
handler runs on the **main browser thread**, where `createSyncAccessHandle` does not exist and
blocking is forbidden. So the interception must sit one layer *above* the filesystem, where the caller
is the engine thread. This is not a preference — a backend cannot work in this build.

**The page owns the worker.** This SDK provides the blocking primitive and the wire format
(`runtime/platform/opfs_internal.h`: an 8-slot `int32` header then the window); the consumer supplies
the JS side that holds the OPFS handles and services requests. A reference implementation is
`web/opfs-io-worker.js` in the Generals port. Two things that side must get right, both learned the
hard way: sleep on the state's *current* value rather than a fixed 0 (otherwise it spins a core
between reply and reset, forever if the caller dies mid-read), and take file sizes from storage rather
than from the caller's list, since the reader clamps every read against them.

**Failure contract, per this SDK's usual rule.** A wedged worker times out after 5000 ms, counts
`opfs.read.timeout`, logs `opfs.worker_stalled` and returns -1 — never a silent retry loop, never an
indefinite stall. With no worker attached `dx8wasm_opfs_ready()` stays 0, so a consumer that gates on
it keeps whatever it did before. Reads are counted in `opfs.read.count` / `opfs.read.bytes`, batched
64 at a time so they cannot flood the telemetry ring (a flooded ring drops records and invalidates
every counter in the window — see `dx8wasm_tel_dropped`).

⚠️ **Callers should buffer small reads.** The primitive is a cross-thread round trip, so a caller that
reads a byte at a time — which archive-table parsers do — will crawl. Generals' `OpfsFile` serves
anything up to 4 KiB from a lookahead buffer; without it the async-fallback path managed ~370 reads/s
and never finished booting.

Protocol coverage: `runtime/test/opfs_smoke.cpp` exercises window chunking, contiguity across
chunks, short reads at EOF, a bad index, and that a failed read leaves the block idle — with a test
responder standing in for the worker, so no browser worker is involved.

---

## 5. Minimal example

The smallest complete program is [`examples/minigame/minigame.cpp`](../examples/minigame/minigame.cpp):
`dx8wasm_init` → `Direct3DCreate8` → `CreateDevice` → per-frame `dx8wasm_pump` +
`Clear`/`DrawIndexedPrimitive`/`Present`, all under `emscripten_set_main_loop`.
Run it: `node scripts/minigame.mjs`.

## 6. Filling gaps

When you hit an unimplemented D3D8 token, the coverage callback prints it — that's
a feature to add under `runtime/` (see [`AGENTS.md`](../AGENTS.md)). When you hit
an undefined Win32 symbol, [`COMPATLIB.md`](COMPATLIB.md) says which tier it belongs
to and how it maps. Nothing is ever silently wrong: unhandled state falls back and
is counted.
