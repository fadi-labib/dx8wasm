# Roadmap

Phased **clean-room re-derivation** into a decoupled, Linux-CI'd SDK. The working reference (`~/projects/personal/Generals-WebAssembly`) and GeneralsX/EA are read-only sources we study, not extract from (see `docs/LICENSING.md`). Each phase ends with a runnable/verifiable artifact.

**What is in flight right now:** not all phases below are active. The live work is telemetry and measured-gap verification against the Generals integration — the current evidence base is the "Measured against a real target" section of [`CONFORMANCE.md`](CONFORMANCE.md#measured-against-a-real-target-not-empirically-probed) and [`measured-gap.json`](measured-gap.json). Phases 5 and 6 are parked (see below); everything else is either done or the Phase 3 measured-gap tail.

## Phase 0 — Spec & scaffold  ✅ (this)
- `SPEC.md`, `README.md`, `docs/PORTING_METHOD.md`, `docs/LICENSING.md`
- `runtime/include/dx8wasm/contract.h` — integration ABI
- `tools/serve-https.py` — dev server (HTTPS + COOP/COEP + Range)
- `cmake/` toolchain notes

## Phase 1 — Asset pipeline & web harness (cleanest, game-agnostic)
- ✅ `asset-tools/pack.py` — GAXD v2 segmented-Brotli packer, re-derived clean (Linux-correct `python3`), round-trip self-test.
- ✅ `web-runtime/gaxd.js` — pure GAXD decoder, cross-validated against the packer in Node (byte-exact, full + streamed).
- ✅ `web-runtime/loader.js` (Range streaming + OPFS cache, bounded memory), `coi-serviceworker.js`, `index.html`, vendored brotli-wasm — verified headlessly end-to-end in real Chromium (Playwright): byte-exact OPFS, crossOriginIsolated, cache-hit on reload. Multiplayer (lobby/signaling/netbridge) is out of scope here — it belongs to a consuming game.
- ✅ **Deliverable met:** pack an arbitrary directory + serve it; loader streams + caches in OPFS. No game needed.

## Phase 2 — Runtime translation layer
- **Detailed plan: [`PHASE2_PLAN.md`](PHASE2_PLAN.md)** (milestones 2.0–2.6, verification rig, decisions, risks). Target: Generals.
- ✅ **Slice 2.0–2.2 done** (spec+plan in `superpowers/`): Emscripten toolchain, SDL3→WebGL2 `platform` seam, clean-room `d3d8.h`, `d3d8webgl` device — `Direct3DCreate8`→`CreateDevice`→`Clear`/`Present` verified by headless pixel readback (gl/platform/d3d8 smokes). Deferred: pin emsdk (SDL3 port experimental), drop interface virtual-dtors before real-ABI compat, single-device/global-context constraint to revisit in 2.3.
- ✅ **2.3 done:** vertex/index buffers (`Lock`/`Unlock` → GL buffer upload), `SetStreamSource`/`SetIndices`, `SetVertexShader(FVF)`, `SetTransform` (world/view/proj), `DrawIndexedPrimitive`. New `graphics-ff` module generates + caches a fixed-function GLSL program from the FVF (`XYZ|DIFFUSE` today). Verified by `draw_smoke`: full-viewport colored quad, corner+center pixel readback. Deferred: only the `XYZ|DIFFUSE` FVF and a hardcoded attribute layout exist — grow per-FVF in 2.4; non-identity transform convention unverified until a real projection is needed.
- ✅ **2.4 done:** `CreateTexture` + `LockRect`/`UnlockRect` (A8R8G8B8, level 0), `SetTexture`, `SetTextureStageState` (`D3DTSS_COLOROP`). `graphics-ff` gained a TEX1 sampler variant + `MODULATE`/`SELECTARG1` combiners, keyed by `(FVF, colorOp)`; D3D texel [B,G,R,A] recovered via the same `.bgra` shader swizzle as diffuse. Verified by `draw_tex_smoke`: modulated quad, non-identity diffuse × texel readback `[128,128,64,255]`. Deferred: level-0 only (no mips), nearest filtering, texture-stage args assumed canonical (`MODULATE` = diffuse×texture, `SELECTARG1` = texture).
- ✅ **2.5 done:** coverage/fallback layer wired via the `contract.h` hooks. New `runtime/coverage/` implements `dx8wasm_get_coverage` + `dx8wasm_set_unhandled_callback`. `SetRenderState` (unhandled), unsupported `D3DTSS_COLOROP` (falls back to `MODULATE`), and non-A8R8G8B8 `CreateTexture` (falls back to RGBA) each bump a counter + `fallbacks_taken` and fire the callback once per distinct token. Verified by `coverage_smoke`: counters `[1,1,1]`, callback fired 3×, rendering continues after the fallback.
- ✅ **2.6 done:** common render-state subset via `SetRenderState` — depth test/write (`ZENABLE`/`ZWRITEENABLE`, `LEQUAL`), alpha blend (`ALPHABLENDENABLE` + `SRCBLEND`/`DESTBLEND`), backface cull (`CULLMODE`), and in-shader alpha test (`ALPHATESTENABLE`/`ALPHAFUNC`/`ALPHAREF` emulated via `discard`, since GLES has no fixed-function alpha test). Platform context now requests a 24-bit depth buffer. Verified by `render_state_smoke`: four sub-scenes assert depth rejection, CCW cull, alpha-test discard, and a 50%-alpha blend `[153,51,102,191]`. Deferred: cull winding assumes NDC == GL winding (no D3D Y-flip projection yet); default cull is `NONE` not D3D's `CCW`.

**Phase 2 slices 2.0–2.6 complete.** The d3d8webgl layer covers device/clear/present, FVF buffers + indexed draws, textures + one combiner, the coverage/fallback layer, and the common render-state subset — all verified by headless pixel-readback smokes on Linux CI. A two-agent adversarial self-review hardened it: `Set*` now AddRefs bound resources (COM contract; `draw_smoke` exercises release-while-bound), `Clear` forces the depth-write mask (D3D `Clear` ignores `ZWRITEENABLE`), `Lock` null-checks its out-param, and buffer/texture destructors guard GL deletes on context liveness. Accepted limitations to revisit in Phase 3: `BaseVertexIndex` ignored (no GLES base-vertex draw), default render states diverge from D3D (cull `NONE` not `CCW`, depth test off until set), single device / global context.
- Re-derive `runtime/d3d8webgl/` (~3.4k LOC surface, ref: Lolendor) behind the `contract.h` boundary.
- Re-derive `runtime/compatlib/` (Win32→POSIX shims, ref: Lolendor CompatLib).
- Build `runtime/platform/` (SDL3 window/input + OpenAL) as a thin new layer.
- Deliverable: a headless smoke test — create device, upload an FVF quad, `DrawIndexedPrimitive`, read back pixels, assert (cf. the WebGPU PoC pattern).

## Phase 3 — Coverage & fixed-function hardening
- ✅ **Capability/fallback layer done in 2.5** (wired early per feedback): unhandled `D3DRS_*`/`D3DTSS_*`/`D3DFMT_*` log loudly, count, and fall back via the `contract.h` hooks.
- ✅ **3.1 done:** fixed-function **directional lighting**. FVF `NORMAL`, `SetLight`/`LightEnable` (light 0), `SetMaterial`, `D3DRS_LIGHTING`/`D3DRS_AMBIENT`. `graphics-ff` gained a per-vertex (Gouraud) lit shader variant keyed by a `lit` bit: `emissive + matAmbient*(globalAmbient + lightAmbient) + matDiffuse*lightDiffuse*clamp(N·L,0,1)`, saturated — the formula cross-checked against DXVK `d3d9_fixed_function.cpp` (directional: L = normalize(−Direction), atten 1), never pasted. Verified by `light_smoke`: diffuse term `[153,102,51,255]` + an ambient/back-face-clamp sub-scene. Deferred: single light 0 only, material-source is always the material (not vertex color), object-space normals (no inverse-transpose normal matrix until non-identity world), no specular/spot/point/attenuation/fog yet.
- ✅ **3.2 done:** multi-light accumulation. `graphics-ff` lit shader now loops over up to `MAX_LIGHTS` (8) enabled lights (uniform-bounded loop, `uLightCount`); the device compacts enabled **directional** lights into uniform arrays and sums their diffuse/ambient contributions. Point/spot lights are logged and skipped at `SetLight` (deferred — the coverage principle: flag, don't mis-render). Verified by `multilight_smoke`: two lights `(0.5,0.1,0)`+`(0.1,0.3,0.4)` → `[153,102,102,255]`; single-light `light_smoke` still green (count=1).
- ✅ **3.3 done:** point lights + distance attenuation. The lit shader now computes per-vertex world position and branches per light type: directional (`hitDir = -Direction`, atten 1) vs point (`hitDir` toward `Position`, `atten = 1/(a0+a1·d+a2·d²)`, zeroed past `Range`; ambient also scaled by atten). Device uploads type/position/attenuation/range arrays. Spot still deferred (logged at `SetLight`). Verified by `point_light_smoke`: on-axis point light, `hitDot·atten = 0.889` → `[227,227,227,255]` (a value that distinguishes attenuation-on from -ignored).
- ✅ **3.4 done:** spot lights — completes the D3DLIGHT trilogy. The lit shader adds a cone term for type-2 lights: `rho = dot(-hitDir, aim)`, full inside the inner cone (`rho ≥ cos(θ/2)`), zero outside the outer (`rho ≤ cos(φ/2)`), `pow((rho−cosφ)/(cosθ−cosφ), falloff)` between, multiplied into the distance attenuation (per DXVK). Device uploads aim direction + `(cosθ/2, cosφ/2, falloff)`. Verified by `spot_light_smoke`: same quad/light, aimed away → black, aimed at → `[144,96,48,255]`.
- ✅ **3.5 done:** linear fog. New `fog` program-key bit adds an eye-space-depth fog blend to every shader variant: `factor = clamp((end − z)/(end − start), 0, 1)`, `rgb = mix(fogColor, rgb, factor)` (alpha untouched). Device handles `D3DRS_FOGENABLE`/`FOGCOLOR`/`FOGSTART`/`FOGEND` (start/end are floats bit-packed in the DWORD); `FOGTABLEMODE`/`FOGVERTEXMODE` set to EXP/EXP2 are flagged via coverage (only linear implemented). `coverage_smoke` probe moved from `FOGENABLE` (now handled) to `FILLMODE`. Verified by `fog_smoke`: red quad at depth 0.5, fog `[0,1]` → `mix(blue,red,0.5)` = `[128,0,128,255]`.
- ✅ **3.6 done:** Blinn specular — completes the fixed-function lighting equation. The lit shader accumulates a per-light specular term `lightSpecular * pow(max(N·H, 0), matPower)` (half-vector `H = normalize(L + V)`, infinite viewer `V = +Z`), gated by `N·L > 0`, then adds `matSpecular · Σspec` to the vertex colour. Uniform-gated by `D3DRS_SPECULARENABLE` (no new shader variant). Device uploads per-light specular + material specular/power. Verified by `specular_smoke`: head-on light, `N·H = 1`, diffuse zeroed → `[204,204,204,255]`. Deferred: local-viewer mode (uses infinite viewer).
- ✅ **3.7 done:** pre-transformed vertices (`D3DFVF_XYZRHW`) — the UI/HUD/2D path. The vertex shader maps screen-pixel coords straight to clip space (D3D top-left origin → Y-flip, `z*2−1` for depth), bypassing world/view/proj; uses a `uViewport` uniform from the device's backbuffer size. Attribute 0 becomes a `vec4`; `lit`/rhw are mutually exclusive. `rhw`=1 assumed (perspective 2D deferred). Verified by `rhw_smoke`: a screen-space quad over the right half lights the right pixel green and leaves the left black.
- ✅ **3.8 done:** all primitive types. `DrawIndexedPrimitive` maps every `D3DPT_*` (point/line list+strip, triangle list/strip/fan) to its GL mode and derives the index count from the primitive count (`*3`, `+2`, `*2`, `+1`, etc.) instead of hard-rejecting non-`TRIANGLELIST`. Verified by `strip_smoke`: a 4-index triangle strip (primCount 2) fills the quad.
- ✅ **3.9 done:** texture-stage combiners — `MODULATE`, `MODULATE2X/4X`, `ADD`, `ADDSIGNED`, `SELECTARG1/2` over the default args (arg1=texture, arg2=diffuse); D3D saturation via the framebuffer's UNORM clamp. Verified by `combiner_smoke` (`ADD`); coverage-probe texture ops now 6/6. `coverage_smoke` op probe moved to `D3DTOP_SUBTRACT`.
- ✅ **Measured-gap tail closed (2026-08-01)** — every token in
  [`measured-gap.json`](measured-gap.json) is now actioned, per
  [`superpowers/plans/2026-08-01-close-the-remaining-docs-items.md`](superpowers/plans/2026-08-01-close-the-remaining-docs-items.md).
  Implemented: `D3DRS_FILLMODE` (value-sensitive — `SOLID` is exact, `WIREFRAME`/`POINT` keep
  reporting because GLES3 has no `glPolygonMode`), `D3DTSS_MAXANISOTROPY`
  (`EXT_texture_filter_anisotropic`, clamped to the device limit),
  `D3DRS_SPECULARMATERIALSOURCE` (`MATERIAL`/`COLOR1`; `COLOR2` still reports, since
  `D3DFVF_SPECULAR` is not uploaded as an attribute). Accepted-and-ignored with a reason at each
  call site: `D3DRS_PATCHSEGMENTS`, `D3DRS_SOFTWAREVERTEXPROCESSING`, `D3DRS_RANGEFOGENABLE`, and
  the six `D3DTSS_BUMPENV*` states — `D3DTOP_BUMPENVMAP` stays unimplemented and reported, so the
  prerequisite op remains the signal. All verified by `accepted_states_smoke`.
- ✅ **Both instrumentation blind spots closed:** vertex blending (`D3DFVF_XYZB1-5`) now has a
  coverage counter of its own (`vertexblend_smoke`) — it previously had none, so its absence from
  a capture proved nothing — and fog-mode *transitions* now emit telemetry regardless of value
  (`fogmode_smoke`), so "fog unused" is falsifiable rather than merely unobserved.
- ✅ **Capstone done:** [`CONFORMANCE.md`](CONFORMANCE.md) — a conformance matrix generated by `scripts/conformance.mjs`. The `conformance` probe program exercises each D3D8 token against a live device and reads the coverage counters to classify handled vs fallback (empirical, can't drift); the feature table is curated and paired with each verifying smoke. Current: render states 17/20, texture ops 3/6, formats 2/5, all fixed-function lighting + linear fog.
- Initial coverage target = Generals' *measured* D3D8 surface: ~80 `D3DRS_*`, the `D3DTSS_*` combiners, `D3DTS_WORLD/VIEW`, `D3DLIGHT` fixed-function lighting, FVF formats, and only 8 real `.vso/.pso` SM1.x shaders.
- Deliverable: a conformance matrix (which D3D8 states/ops are covered) built against that target.

## Phase 4 — CI & tests
- ✅ **CI harness done:** `scripts/check.sh` (mechanical guardrails: SPDX headers, commit-authorship, no saturating 32-bit casts of `emscripten_get_now()`) + `scripts/ci.sh` (guardrails + pinned-toolchain check + the full test suite, 35 smokes across the d3d8webgl/compatlib/telemetry surface — every `CMakeLists.txt` executable target except `conformance`, `minigame`, and `spin_demo`) + `.github/workflows/ci.yml`, which just invokes `ci.sh`. Runs locally today; the workflow goes live once this repo has a remote.
- ✅ **Emscripten pinned:** `.emscripten-version` (6.0.2) is checked by `ci.sh` against the live toolchain; the `wasm-opt`/`-g` DWARF workaround is documented in `cmake/`.
- ✅ **Determinism harness done:** `determinism_smoke` digests one fixed render sequence twice
  in-process (catches state left dirty by the first pass) and `scripts/determinism.mjs` compares
  the digest across fresh browser contexts (catches uninitialised memory and iteration-order-
  dependent shader-cache keys). Both run in `ci.sh`. A game with replays extends the same seam by
  digesting its own per-tick simulation state. `runtime/test/frame_digest.h` is the reusable
  FNV-1a-over-`glReadPixels` helper.

**Phase 4 complete.** Every non-parked phase is now closed; the open list is exactly the two
parked phases plus compatlib's grow-on-demand tiers.

## ⏸️ Parked — Phase 5 — WebGPU backend
Parked: not started, and not currently justified. The WebGL2 backend already runs a full
commercial RTS (Generals) at 60 FPS on a real GPU, so a second graphics backend would add
real risk and ongoing maintenance against no current user need. **Unpark when** a target
needs compute shaders WebGL2 can't express, or if browser WebGL2 support meaningfully
degrades.
- Second graphics backend behind the same interface: fixed-function → SPIR-V (DXVK-modeled) → WGSL (Tint/Naga); SM1.x shaders → WGSL. Share the FF core with WebGL2 via SPIRV-Cross (→GLSL).
- Deliverable (if unparked): the headless smoke test passes on both backends.

## ⏸️ Parked — Phase 6 — Second-game validation (prove generality)
Parked: not started. Generality is still the goal, but the cheap version of that proof
already exists — [`examples/minigame/`](../examples/minigame/) is a working integration
using only init + D3D8 + pump, with no Generals code in it. Wiring up a second *full* game
is the expensive version of the same claim, and nothing today says which parts of the SDK
still need it. **Unpark after** the measured-gap work (see "what is in flight" above) has
shown which parts of the SDK are Generals-shaped versus genuinely general.
- Pick a *different* fit: a **DirectDraw-2D** classic (simpler graphics path) or an **OSS reimplementation** (openage/OpenRA). Wire it up; measure how much is drop-in vs gap-fill.
- Deliverable (if unparked): a second game running → the SDK is proven reusable, not Generals-shaped.

## Phase A — full D3D8 COM ABI ✅ (Generals integration prerequisite)
The clean-room `d3d8.h` is now the **complete standard D3D8 interface** in canonical
vtable order (slot 0 = `QueryInterface`), not the old minimal subset — so a game
compiled against any standard D3D8 header dispatches to the right slot. `IDirect3D8`,
`IDirect3DDevice8` (~94 methods), and the resource hierarchy (`IDirect3DResource8`/
`BaseTexture8`/`Texture8`/`VertexBuffer8`/`IndexBuffer8`/`Surface8`) are all present;
the supported subset does real work, the rest are honest stubs (`warn_once` /
coverage / sensible defaults). Verified by `abi_smoke` (low/mid/high-slot dispatch)
plus all 20 existing smokes still green through the expanded vtable. This retires
the roadmap's "real-ABI compat" deferral and unblocks linking against Generals
(see `../generals-dx8wasm/PLAN.md`). Next Phase A: bring-up harness + CMake seam.

## Game-integration foundation ✅
The seam a real game plugs into, all via the public surface (see [`docs/INTEGRATION.md`](INTEGRATION.md)):
- `dx8wasm_init`/`shutdown`/`has_cap` implemented; `log_unimplemented` gates coverage logging.
- Input: `dx8wasm_pump` + `dx8wasm_input` (SDL events → key/mouse/wheel/quit). No Win32 message pump — this is the raw-input seam a game maps from.
- Device owns the GL viewport (games drive only D3D8, never GL).
- [`examples/minigame/`](../examples/minigame/): a keyboard-controlled sprite using only init + D3D8 + pump — the integration template. `node scripts/minigame.mjs`.
- **compatlib** (Win32→POSIX, `docs/COMPATLIB.md` maps the full surface Generals needs, tiered):
  - ✅ **Tier 0** — types umbrella + timing (`timeGetTime`/`GetTickCount`/`QueryPerformance*`/`Sleep`) + `OutputDebugStringA`, all on the one emscripten clock (`compat_smoke`).
  - ✅ **Tier 1** — file I/O (`CreateFile`/`ReadFile`/`WriteFile`/`CloseHandle`/`SetFilePointer`/`GetFileSize`/`GetFileAttributes`), directory enumeration (`FindFirstFile`/`NextFile`/`Close`, `CreateDirectory`, `Get`/`SetCurrentDirectory`), shell-folder stub, and the `GlobalAlloc` family — over POSIX with Windows-path (`\\`→`/`) normalization and `<tchar.h>`-style generic-name macros (`compat_file_smoke`).
  - ✅ **Tier 2** — modules (`LoadLibrary`/`GetProcAddress`/`FreeLibrary`/`GetModuleFileName`, static-link stubs), threads (`CreateThread` runs synchronously in the single-threaded build; `GetCurrentThreadId`/`TerminateThread`), and registry (`Reg*` over an in-memory key/value store; missing values return NOT_FOUND so the game uses its defaults). `CloseHandle` guards low-integer pseudo-handles. `compat_sys_smoke`.
  - ✅ **Tier 3 (math)** — D3DX helper math: `D3DXMatrix` Identity/Multiply/Transpose/Inverse/Translation/Scaling/RotationZ, `D3DXVec3/4Transform`, `D3DXGetFVFVertexSize` — pure math over `D3DMATRIX` (row-major, row-vector). `compat_d3dx_smoke` (M·M⁻¹=I, composition, FVF sizing).
  - Remaining: Tier 3 D3DX **textures/shaders** (image decode + SM1.x — a graphics slice), Tier 4 (sockets/COM/VFW) — grow against the real build.
- **Still game-side (grow on demand):** the higher compatlib tiers, non-blocking main loop conversion, asset wiring, audio.

## Cross-cutting, ongoing
- **dx8wasm is the home for future work.** The Generals repos (`Generals-WebAssembly`, `Generals-Mac-iOS-iPad`) are **read-only reference sources** we study for behavior — we don't sync from them or push back to Lolendor (low-trust, AI-generated; see `docs/LICENSING.md`).
- Document every gap-fill as a lessons entry (per `PORTING_METHOD.md` §6). SDK-side lessons library:
  [`RESULTS-2026-08-01-close-the-docs-items.md`](RESULTS-2026-08-01-close-the-docs-items.md)
  (`CDI-1`..`CDI-27` — documentation drift, plans written without compiling them, coverage
  instrumentation as a work list, failure modes the suite cannot see, parallel worktree execution).
