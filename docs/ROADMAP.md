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
- Re-derive `runtime/d3d8webgl/` (~3.4k LOC surface, ref: Lolendor) behind the `contract.h` boundary.
- Re-derive `runtime/compatlib/` (Win32→POSIX shims, ref: Lolendor CompatLib).
- Build `runtime/platform/` (SDL3 window/input + OpenAL) as a thin new layer.
- Deliverable: a headless smoke test — create device, upload an FVF quad, `DrawIndexedPrimitive`, read back pixels, assert (cf. the WebGPU PoC pattern).

## Phase 3 — Coverage & fixed-function hardening
- Add the **capability/fallback layer** (non-negotiable for generality): every unimplemented render state / texture-stage op / format **logs loudly + falls back**, never silently renders wrong. Wire the `contract.h` introspection hooks (`dx8wasm_get_coverage`, `dx8wasm_set_unhandled_callback`) in **early**.
- Harden `graphics-ff` against **DXVK `d3d9_fixed_function.cpp`** (2,661 lines, zlib/permissive — local copy at `Generals-Mac-iOS-iPad/references/fadi-labib-dxvk/`) for T&L, lighting, fog, `D3DTOP_*` combiners, alpha-test, vertex-blend. Cross-check behavior against Wine `wined3d` but **never paste** (LGPL).
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
