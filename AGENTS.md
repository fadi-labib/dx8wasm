# AGENTS.md — working in the dx8wasm repo

Instructions for AI coding agents contributing to **dx8wasm**, a clean-room
DirectX-8 → WebAssembly (WebGL2) translation SDK for porting DX8-era games to the
browser. If you are *consuming* the SDK to build a game/app, read
[`docs/SDK_REFERENCE.md`](docs/SDK_REFERENCE.md) and
[`docs/INTEGRATION.md`](docs/INTEGRATION.md) instead.

## Orientation

- **Build** (all wasm targets): `cmake --preset emscripten && cmake --build build/emscripten`
  (or `bash scripts/build-wasm.sh`). Requires an activated Emscripten SDK; the
  project pins **6.0.2** (`.emscripten-version`).
- **Full CI** (mechanical checks + toolchain pin + every test suite): `bash scripts/ci.sh`. Must print `ALL GREEN`.
- **Headless GPU + compat smokes**: `node web-runtime/test/phase2.gpu.test.mjs` (Playwright + headless Chromium/SwiftShader).
- **See it live**: `node scripts/demo.mjs` (pipeline demo) · `node scripts/minigame.mjs` (integration example).
- **Regenerate the coverage matrix**: `node scripts/conformance.mjs` → `docs/CONFORMANCE.md`.

## Repo layout

| Path | What |
|------|------|
| `runtime/d3d8/d3d8.h` | Clean-room D3D8 API subset (the game-facing header) |
| `runtime/d3d8webgl/` | The device — translates D3D8 calls to WebGL2 |
| `runtime/graphics-ff/` | Fixed-function shader generator (FVF/state → cached GLSL program) |
| `runtime/coverage/` | Coverage/fallback layer + `contract.h` introspection |
| `runtime/platform/` | SDL3 → WebGL2 window/context/input seam, plus `opfs_bridge.cpp` (opt-in on-demand OPFS reads; built into `dx8_d3d8webgl`, see `docs/ARCHITECTURE.md`) |
| `runtime/compatlib/` | Win32 → POSIX shims (tiered; see `docs/COMPATLIB.md`) |
| `runtime/include/dx8wasm/contract.h` | The dx8wasm-specific ABI (init, input, coverage) |
| `runtime/include/dx8wasm/opfs.h` | Optional: ranged reads from OPFS instead of resident archives (`docs/SDK_REFERENCE.md` §4) |
| `runtime/test/` | Headless pixel/self-test smokes (one per feature) |
| `web-runtime/` | Phase 1 asset loader + the JS test harness |
| `asset-tools/` | GAXD asset packer |
| `examples/minigame/` | End-to-end integration template |
| `scripts/` | build, ci, check (guardrails), demo, minigame, conformance |
| `docs/` | ROADMAP, SPEC, INTEGRATION, COMPATLIB, CONFORMANCE, SDK_REFERENCE, ARCHITECTURE |

## Non-negotiable conventions

1. **Clean-room.** Re-derive from behavior and public API specs. You **may study**
   the DXVK reference (`~/projects/personal/Generals-Mac-iOS-iPad/references/fadi-labib-dxvk/`,
   zlib) and the reference port's compat *surface* (which symbols) — but **never
   paste** code, and **never** read Wine `wined3d` implementation (LGPL). Cite the
   behavioral source in the commit message.
2. **Coverage, not silence.** Any D3D8 token / Win32 case you don't implement must
   **log loudly + fall back + count** (via `runtime/coverage/` or an `fprintf`),
   never render/return silently wrong. This is the SDK's core discipline.
3. **TDD with a runnable check.** Every feature ships a headless smoke in
   `runtime/test/` wired into `web-runtime/test/phase2.gpu.test.mjs`, asserting an
   exact pixel (graphics) or a `[1,0,0,255]` sentinel (compat). Choose test values
   that **distinguish correct from almost-correct** (non-identity operands).
4. **Minimal (ponytail).** Smallest change that works. No speculative abstraction.
   Mark deliberate shortcuts with a `ponytail:` comment naming the ceiling.
5. **SPDX header** on every source file: `// SPDX-License-Identifier: GPL-3.0-only`.
6. **Commits**: author `Fadi Labib <github@fadilabib.com>`; **never** add an AI
   co-author line. `scripts/check.sh` enforces SPDX + authorship + vendored-binary
   tracking (also runs as a PostToolUse hook).

## How to add a feature (the pattern)

A new **render state / texture op / format**: add the enum to `d3d8.h` → handle it
in `device.cpp` (`SetRenderState`/`SetTextureStageState`/`CreateTexture`), routing
the unhandled case to `coverage::` → if it affects shading, extend
`graphics-ff/ff_shader.cpp` (usually one conditional block + a program-key bit) →
add a smoke → regenerate `CONFORMANCE.md`. If you handle a token that a
`coverage_smoke` used as its "unhandled" probe, move that probe to a still-unhandled one.

A **compatlib shim**: add the declaration to `runtime/compatlib/win32.h` (+ any
`<tchar.h>`-style generic macro), implement over POSIX/emscripten in the matching
`win32_*.cpp`, add a `compat_*_smoke`. See `docs/COMPATLIB.md` for the tier map.

## Gotchas that have already bitten (see docs/ARCHITECTURE.md for detail)

- **Color byte order**: D3DCOLOR `0xAARRGGBB` is `[B,G,R,A]` in memory → recover RGBA with a `.bgra` GLSL swizzle (diffuse, texels, material). No CPU byte-shuffling.
- **Matrix convention**: D3D matrices are row-major/row-vector; upload with `glUniformMatrix4fv(..., GL_FALSE, ...)` (GL reads the transpose) and multiply `proj*view*world` in the shader.
- **Viewport**: the device sets `glViewport` itself every draw — a consuming game drives only D3D8, never GL. SDL's emscripten canvas can resize after context creation.
- **Handles**: file = `FILE*`; thread/module = the integer `1` (`CloseHandle` skips `≤0xffff`); registry `HKEY` = predefined root (high bit) or heap `std::string*`.
- **Headless tests use a 4×4 canvas** — they cannot catch viewport/full-canvas bugs. Drive a real-sized target (demo/minigame) for those.
