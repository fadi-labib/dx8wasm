# Roadmap

Phased **clean-room re-derivation** into a decoupled, Linux-CI'd SDK. The working reference (`~/projects/personal/Generals-WebAssembly`) and GeneralsX/EA are read-only sources we study, not extract from (see `docs/LICENSING.md`). Each phase ends with a runnable/verifiable artifact.

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
- Continue hardening `graphics-ff` against **DXVK `d3d9_fixed_function.cpp`** (2,661 lines, zlib/permissive — local copy at `Generals-Mac-iOS-iPad/references/fadi-labib-dxvk/`) for remaining T&L, multi-light, fog, more `D3DTOP_*` combiners, vertex-blend. Cross-check behavior against Wine `wined3d` but **never paste** (LGPL).
- Initial coverage target = Generals' *measured* D3D8 surface: ~80 `D3DRS_*`, the `D3DTSS_*` combiners, `D3DTS_WORLD/VIEW`, `D3DLIGHT` fixed-function lighting, FVF formats, and only 8 real `.vso/.pso` SM1.x shaders.
- Deliverable: a conformance matrix (which D3D8 states/ops are covered) built against that target.

## Phase 4 — CI & tests
- **Linux CI** (GitHub Actions): configure + build + headless render tests + the pack pipeline. This is the anti-regression the reference lacked.
- Pin Emscripten: 6.0.2's `wasm-opt` crashes on `-g` DWARF (`Assertion !endMap.contains(span.end)`); workaround is re-linking with `-g0`. Lock the version and document both in `cmake/`.
- Determinism harness stub (for games with replays).

## Phase 5 — WebGPU backend
- Second graphics backend behind the same interface: fixed-function → SPIR-V (DXVK-modeled) → WGSL (Tint/Naga); SM1.x shaders → WGSL. Share the FF core with WebGL2 via SPIRV-Cross (→GLSL).
- Deliverable: the headless smoke test passes on both backends.

## Phase 6 — Second-game validation (prove generality)
- Pick a *different* fit: a **DirectDraw-2D** classic (simpler graphics path) or an **OSS reimplementation** (openage/OpenRA). Wire it up; measure how much is drop-in vs gap-fill.
- Deliverable: a second game running → the SDK is proven reusable, not Generals-shaped.

## Cross-cutting, ongoing
- **dx8wasm is the home for future work.** The Generals repos (`Generals-WebAssembly`, `Generals-Mac-iOS-iPad`) are **read-only reference sources** we study for behavior — we don't sync from them or push back to Lolendor (low-trust, AI-generated; see `docs/LICENSING.md`).
- Document every gap-fill as a lessons entry (per `PORTING_METHOD.md` §6).
