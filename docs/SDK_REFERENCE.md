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

### What's supported
All three light types (directional/point/spot), the full ambient+diffuse+specular
equation, linear fog, depth/blend/cull/alpha-test render states, `MODULATE`/
`MODULATE2X`/`MODULATE4X`/`ADD`/`ADDSIGNED`/`SELECTARG1`/`SELECTARG2` combiners,
`A8R8G8B8`/`X8R8G8B8` textures, every primitive type. **Authoritative, current
list: [`CONFORMANCE.md`](CONFORMANCE.md).** Anything not listed falls back and is
counted — see the coverage API below.

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

## 4. Minimal example

The smallest complete program is [`examples/minigame/minigame.cpp`](../examples/minigame/minigame.cpp):
`dx8wasm_init` → `Direct3DCreate8` → `CreateDevice` → per-frame `dx8wasm_pump` +
`Clear`/`DrawIndexedPrimitive`/`Present`, all under `emscripten_set_main_loop`.
Run it: `node scripts/minigame.mjs`.

## 5. Filling gaps

When you hit an unimplemented D3D8 token, the coverage callback prints it — that's
a feature to add under `runtime/` (see [`AGENTS.md`](../AGENTS.md)). When you hit
an undefined Win32 symbol, [`COMPATLIB.md`](COMPATLIB.md) says which tier it belongs
to and how it maps. Nothing is ever silently wrong: unhandled state falls back and
is counted.
