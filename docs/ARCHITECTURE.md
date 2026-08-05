# Architecture

How dx8wasm is put together, for agents extending it. For the callable surface see
[`SDK_REFERENCE.md`](SDK_REFERENCE.md); for conventions and the contribution
workflow see [`AGENTS.md`](../AGENTS.md).

## Layers

```
        game code (compiled with emcc)
        │  #include <d3d8.h>, <windows.h>, dx8wasm/contract.h
        ▼
┌───────────────────────────────────────────────────────────┐
│ runtime/d3d8/d3d8.h        clean-room D3D8 API subset       │  ← the game-facing header
├───────────────────────────────────────────────────────────┤
│ runtime/d3d8webgl/         Device8 — translates D3D8→GLES3  │  lib: dx8_d3d8webgl
│   device.cpp   state, buffers, textures, DrawIndexedPrimitive
│   d3d8.cpp     the factory (Direct3DCreate8, CreateDevice)  │
│ runtime/graphics-ff/       FVF/state → cached GLSL program  │
│ runtime/coverage/          fallback layer + contract hooks  │
│ runtime/runtime.cpp        dx8wasm_init/shutdown/has_cap    │
├───────────────────────────────────────────────────────────┤
│ runtime/platform/          SDL3 → WebGL2 context + input    │  lib: dx8_platform
│   opfs_bridge.cpp   blocking ranged reads from OPFS (opt-in) │  (built into dx8_d3d8webgl)
├───────────────────────────────────────────────────────────┤
│ runtime/compatlib/         Win32 → POSIX/emscripten shims   │  lib: dx8_compat (independent)
└───────────────────────────────────────────────────────────┘
        ▼
   Emscripten → WebGL2 / GLES3 + MEMFS + browser APIs
```

Three static libraries, built by the top-level `CMakeLists.txt` against the
`emscripten` CMake preset:
- **`dx8_platform`** — the only thing that touches SDL3. Window/GL-context
  lifecycle (`create_gl_context`/`present`/`destroy`/`gl_context_alive`) and the
  input pump (`dx8wasm_pump`).
- **`dx8_d3d8webgl`** — the translation layer; depends on `dx8_platform`. Contains
  the device, the shader generator, the coverage layer, the runtime init, the
  telemetry ring, and `runtime/platform/opfs_bridge.cpp`. That last one lives under
  `runtime/platform/` because it is a platform service, but it is compiled into *this*
  library because it calls the telemetry producers: putting it in `dx8_platform` would
  make the two archives mutually dependent, and wasm-ld resolves a static archive only
  against archives listed after it.
- **`dx8_compat`** — the Win32 shims; standalone (only depends on libc + the
  emscripten FS). A game links all three.

## The draw call — data flow

`DrawIndexedPrimitive` is the hot path where everything converges:

1. **Topology** → `prim_info()` maps `D3DPRIMITIVETYPE` to a GL mode + derives the
   index count from the primitive count.
2. **Program selection** → `ff::program_for(fvf, colorOp, alphaFunc, lit, fog)`
   returns a cached GLSL program, keyed by that tuple. On a miss it string-builds
   the vertex + fragment shaders for exactly that feature combination and compiles
   once. New fixed-function features are usually **one conditional block in
   `build()` plus one key bit** — the key is why the program set stays small.
3. **Viewport** → the device sets `glViewport` from the backbuffer size (the game
   never calls GL).
4. **Attributes** → walked in FVF order (`XYZ[RHW]`, `NORMAL`, `DIFFUSE`, `TEX1`)
   at fixed locations; absent ones are disabled.
5. **Uniforms** → matrices (uploaded transposed), lighting arrays (compacted
   enabled lights), fog, alpha-ref, viewport — only those the program has.
6. **Draw** → `glDrawElements(mode, count, GL_UNSIGNED_SHORT, …)`.

State setters (`SetRenderState`, `SetTextureStageState`, `SetLight`, …) just record
device state; nothing touches GL until the draw. Unhandled tokens route to
`coverage::` at set time.

## The shader generator (`graphics-ff`)

One compiled program per `(fvf, colorOp, alphaFunc, lit, fog)` tuple, cached in a
map. Feature axes that vary at runtime (light count, light type, specular on/off)
are **uniform data, not shader variants**, so the cache stays bounded. The program
key packs the tuple into a `uint64`. Lighting is per-vertex Gouraud, looping over a
uniform-bounded light array; the whole FF lighting equation lives in one loop.

## The coverage layer (`coverage`)

Implements the `contract.h` introspection. `coverage::unhandled_*` bumps a counter,
increments `fallbacks_taken`, and fires the registered callback **once per distinct
token**. This is what makes gaps discoverable as data, and what powers the
empirical `conformance` probe. `dx8wasm_init(log_unimplemented)` gates the stderr log.

## Test harness

- **`web-runtime/test/phase2.gpu.test.mjs`** — the suite. Builds all wasm targets,
  then loads each smoke in headless Chromium (SwiftShader, via Playwright) and
  asserts the value it reported to `window.__gpu`. Graphics smokes read back a
  pixel; compat smokes report a `[1,0,0,255]` sentinel. **Canvas is 4×4** — small
  enough that a wrong viewport still fills a 1-pixel readback, so viewport/
  full-canvas bugs need a real-sized target (`demo`/`minigame`) to surface.
- **`scripts/conformance.mjs` + `runtime/test/conformance.cpp`** — probes each
  token against a live device and classifies handled/fallback from the coverage
  counters → `docs/CONFORMANCE.md` (empirical, can't drift).
- **`scripts/ci.sh`** — mechanical guardrails (`check.sh`: SPDX/authorship/vendored)
  + toolchain pin + packer selftest + web-runtime suite + the GPU/compat smokes.

## Adding a feature — the mechanical path

Enum in `d3d8.h` → handle in `device.cpp` (route the unhandled case to `coverage::`)
→ if it shades, extend `ff_shader.cpp` (a `build()` block, maybe a key bit) → a
smoke in `runtime/test/` wired into the harness → regenerate `CONFORMANCE.md`. Keep
the diff minimal; mark shortcuts with `ponytail:`. See `AGENTS.md` for the full
convention list and the gotchas (color byte order, matrix transpose, handle
encodings, headless-canvas limitation).
