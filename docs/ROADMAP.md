# Roadmap

Phased extraction from the working reference (`~/projects/personal/Generals-WebAssembly`) into a decoupled, Linux-CI'd SDK. Each phase ends with a runnable/verifiable artifact.

## Phase 0 — Spec & scaffold  ✅ (this)
- `SPEC.md`, `README.md`, `docs/PORTING_METHOD.md`, `docs/LICENSING.md`
- `runtime/include/dx8wasm/contract.h` — integration ABI
- `tools/serve-https.py` — dev server (HTTPS + COOP/COEP + Range)
- `cmake/` toolchain notes

## Phase 1 — Asset pipeline & web harness (cleanest, game-agnostic)
- Extract `asset-tools/` (GAXD segmented-Brotli packer) — **fix the macOS-hardcoded python path** (`/opt/homebrew/bin/python3.11` → `python3`); this is a known Linux-blocking bug in the reference.
- Extract `web-runtime/` (shell, loader, OPFS/IndexedDB cache, brotli-wasm unpack, `coi-serviceworker`, WebRTC signaling) — de-brand from "GeneralsX".
- Deliverable: pack an arbitrary directory + serve it; loader streams + caches in OPFS. No game needed.

## Phase 2 — Runtime translation layer
- Extract `runtime/d3d8webgl/` (~3.4k LOC, near-zero coupling) behind the `contract.h` boundary.
- Extract `runtime/compatlib/` (Win32→POSIX shims).
- Build `runtime/platform/` (SDL3 window/input + OpenAL) as a thin new layer.
- Deliverable: a headless smoke test — create device, upload an FVF quad, `DrawIndexedPrimitive`, read back pixels, assert (cf. the WebGPU PoC pattern).

## Phase 3 — Coverage & fixed-function hardening
- Add the **capability/fallback layer**: unimplemented render states / texture-stage ops / formats log loudly and fall back (never silently wrong).
- Harden `graphics-ff` against **DXVK `d3d9_fixed_function.cpp`** (permissive reference) + Wine `wined3d`: fill T&L, lighting, fog, `D3DTOP_*`, alpha-test, vertex-blend coverage beyond the one-game subset.
- Deliverable: a conformance matrix (which D3D8 states/ops are covered).

## Phase 4 — CI & tests
- **Linux CI** (GitHub Actions): configure + build + headless render tests + the pack pipeline. This is the anti-regression the reference lacked.
- Pin Emscripten (the `wasm-opt` DWARF crash is version drift — lock it; document the `-g0` link workaround).
- Determinism harness stub (for games with replays).

## Phase 5 — WebGPU backend
- Second graphics backend behind the same interface: fixed-function → SPIR-V (DXVK-modeled) → WGSL (Tint/Naga); SM1.x shaders → WGSL. Share the FF core with WebGL2 via SPIRV-Cross (→GLSL).
- Deliverable: the headless smoke test passes on both backends.

## Phase 6 — Second-game validation (prove generality)
- Pick a *different* fit: a **DirectDraw-2D** classic (simpler graphics path) or an **OSS reimplementation** (openage/OpenRA). Wire it up; measure how much is drop-in vs gap-fill.
- Deliverable: a second game running → the SDK is proven reusable, not Generals-shaped.

## Cross-cutting, ongoing
- Keep the reference (`Generals-WebAssembly`) as the upstream we sync from; contribute fixes back (the Linux python-path fix, `serve-https.py`, the Emscripten-6 DWARF note are ready PRs).
- Document every gap-fill as a lessons entry (per `PORTING_METHOD.md` §6).
