# Phase 2 Plan — Runtime translation layer

The hard core: make a D3D8 game's own C++ render calls work in the browser on
WebGL2. Clean-room re-derived (ref: Lolendor; behavior ref: DXVK — see
`LICENSING.md`). **Target: C&C Generals/Zero Hour** — coverage tracks its
*measured* D3D8 surface so we always know the gap to a real menu render.

Exit criterion: a synthetic headless-Chromium GPU suite (clear → colored quad →
textured quad → blended scene → coverage-callback) is green and CI-able, and the
coverage counters are mapped onto Generals' surface.

## What Phase 2 must produce

| Layer | Path | Job |
|---|---|---|
| D3D8 API headers | `runtime/d3d8/` | the COM interface declarations the game compiles against |
| `d3d8webgl` | `runtime/d3d8webgl/` | implement `IDirect3D8` / `IDirect3DDevice8` + resources on WebGL2 |
| `graphics-ff` | `runtime/graphics-ff/` | fixed-function state → generated GLSL ES (+ shader cache) |
| `compatlib` | `runtime/compatlib/` | the minimal Win32 shims the device path needs |
| `platform` | `runtime/platform/` | SDL3 → a live WebGL2 context on a canvas |
| test harness | `runtime/test/` + `web-runtime/test/` | C++→wasm GPU smoke, driven by Playwright, pixel-readback asserts |

## Milestones (each ends in a runnable check)

**2.0 — Toolchain + test rig** (no D3D8 yet)
- Pin Emscripten in a `cmake` preset; document the `-g0` wasm-opt DWARF workaround.
- Trivial C++→wasm that gets a WebGL2 context and `glClear`s a known color.
- ✔️ Check: extend the Phase 1 Playwright rig to **pixel-readback** — assert the
  cleared color in headless Chromium. Proves the rig before any translation code.

**2.1 — COM skeleton**
- Source the D3D8 interface headers (decision below); `Direct3DCreate8` →
  `IDirect3D8` → `CreateDevice` returns a device owning the `platform` GL context.
- ✔️ Check: device creation succeeds; `GetDeviceCaps`/adapter queries return sane
  values. No drawing yet.

**2.2 — Clear + Present via the D3D8 API**
- `Clear()` → `glClear`; `Present()` → commit/swap.
- ✔️ Check: same readback as 2.0 but driven through the **D3D8 API**, not raw GL.

**2.3 — Buffers + FVF + first fixed-function draw**
- `CreateVertexBuffer`/`CreateIndexBuffer` + `Lock`/`Unlock`, `SetStreamSource`,
  `SetIndices`, `SetVertexShader(FVF)`, `DrawIndexedPrimitive`.
- `graphics-ff` generates a minimal transform + diffuse GLSL from FVF + world/
  view/proj matrices; cached by state key.
- ✔️ Check: draw an untextured colored quad; readback asserts corner + center px.

**2.4 — Textures + one texture-stage combiner**
- `CreateTexture` + `LockRect`/`UnlockRect` upload, `SetTexture`,
  `SetTextureStageState` (`D3DTOP_MODULATE` first).
- ✔️ Check: textured quad; readback asserts the sampled texel color.

**2.5 — Coverage / fallback layer (wire early, per feedback)**
- Every unhandled `D3DRS_*` / `D3DTSS_*` / `D3DFMT_*` **logs loudly, counts, and
  falls back** — never silently wrong. Drive the `contract.h` hooks
  (`dx8wasm_get_coverage`, `dx8wasm_set_unhandled_callback`).
- ✔️ Check: set an unimplemented state → the unhandled callback fires, counters
  increment, rendering continues.

**2.6 — Common render-state subset**
- Depth test/write, alpha blend, backface cull, alpha test (emulated in-shader —
  GLES has no fixed-function alpha test). The subset Generals' menu/skirmish hits.
- ✔️ Check: a mixed opaque + alpha-blended multi-primitive scene; readback asserts.

## Verification approach

Reuse Phase 1's Playwright + headless-Chromium harness. A C++ `gpu_smoke`
program compiles to wasm, runs a named test sequence against `d3d8webgl`, and
exposes the framebuffer (via `glReadPixels`) to JS; Playwright asserts pixel
samples within a tolerance. WebGL2 in headless Chromium is provided by
SwiftShader — deterministic enough for sampled-pixel asserts (confirm exact
flags in 2.0; likely `--use-gl=angle --use-angle=swiftshader`).

Performance is explicitly **not** a Phase 2 goal — correctness first.

## Key decisions to settle at 2.0/2.1

1. **D3D8 header source.** The interface *declarations* are Microsoft's API
   (arguably uncopyrightable, cf. Google v. Oracle). Options: (a) clean-room
   declare the subset we implement, or (b) vendor MinGW-w64's permissive
   `d3d8.h`. Prefer (a) for a minimal, owned surface; fall back to (b) if the
   COM/vtable details get fiddly. **Never** use Wine's headers (LGPL).
2. **COM ABI.** The game and `d3d8webgl` are both built with `emcc`, so a C++
   class with virtual methods gives an Itanium-ABI vtable that matches what the
   game's `IDirect3DDevice8*` calls expect — no hand-rolled vtables needed, as
   long as everyone uses the C++ (not C) D3D8 interface form. Verify with a
   one-method call across the boundary in 2.1.
3. **Readback color space.** Assert with tolerance; decide sRGB default-FB
   handling (contract has an `srgb` flag) before writing pixel asserts.

## Risks

- Headless WebGL2 (SwiftShader) determinism for readback — de-risk first in 2.0.
- FVF variety + state-key explosion in the shader cache — start with Generals'
  actual FVFs, grow on demand (thin-slice, not exhaustive).
- Emscripten version drift (the 6.0.2 wasm-opt crash) — pin hard in 2.0.

## Coverage target (Generals' measured D3D8 surface)

Build the Phase 3 conformance matrix against this; Phase 2 only needs the subset
its smoke tests exercise:
- ~80 `D3DRS_*` render states
- `D3DTSS_*` texture-stage combiners
- `D3DTS_WORLD` / `D3DTS_VIEW` / projection transforms + `D3DLIGHT` lighting
- FVF vertex formats
- only 8 real `.vso/.pso` SM1.x shaders (fixed-function dominates)

DXVK `d3d9_fixed_function.cpp` (2,661 lines, zlib) is the behavioral reference:
local copy at `~/projects/personal/Generals-Mac-iOS-iPad/references/fadi-labib-dxvk/`.
Cross-check Wine `wined3d` but never paste (LGPL).
