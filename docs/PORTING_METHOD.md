# Porting Method — how to bring any classic Windows game to WASM

The reusable *methodology* behind dx8wasm. The SDK gives you code; this gives you the sequence and the judgment. Distilled from a real 500k-LOC port and generalized.

## 0. The one big idea: strategy per subsystem, not per project

A port is **one decision per subsystem**, chosen by API-surface size and determinism risk:

| Strategy | Use when | Example |
|---|---|---|
| **Translate** — keep the legacy API, swap the implementation under it | huge embedded API surface; behavior must be bit-faithful | D3D8 kept, WebGL2 underneath (`d3d8webgl`) |
| **Shim** — reimplement a thin API on new primitives | many small scattered calls, simple semantics | Win32 → POSIX (`compatlib`) |
| **Swap behind an interface** — add a backend to an existing seam | the engine already has a Device/Manager abstraction | `SDL3GameEngine` beside `Win32GameEngine`; OpenAL beside Miles |
| **Stub** — return benign values | non-gameplay-critical | screenshots, dialogs, telemetry |
| **Rewrite** — last resort | no translator and no thin shim possible | budget 10× the estimate; usually the abandoned path |

Rule of thumb: **translate** the renderer (fidelity/determinism), **swap** audio/video (natural manager seams), **shim** OS plumbing, **stub** periphery.

## 1. The universal sequence

0. **Ecosystem sweep + analysis (pure research, no code).** Does an OSS reimplementation exist (openage, OpenRA, OpenMW…)? Port that if the original is closed. Find every partial reference — a translation layer's sample app, a similar engine's SDL backend. Adapt, never copy-paste. Write an architecture-decision log (which strategy per subsystem) before coding.
1. **Compile-with-stubs.** Get a linking binary with every Windows-only path stubbed whole-function. Fix the ABI taxonomy (§3) proactively. Goal: it links, not runs.
2. **Graphics + windowing + input.** The longest pole; everything else is only testable once you can see the game. "Menu renders" is the project's true halfway point.
3. **Audio, then video.** Natural manager seams, lower risk.
4. **Polish/hardening** — paths, lifecycle, packaging, CI.
5. **New-architecture pass** (64-bit/ARM/WASM) — only after the same-arch port works, so failures isolate.

Acceptance gates are **behavioral** ("skirmish loads", "10-min stability"), never "it compiles".

## 2. The compat-shim pattern, done right

- **One shim layer, included first.** Inline stubs collide with third-party headers defining the same symbols. Centralize.
- **Wrap whole functions, not lines.** >50% Windows API in a function → `#ifdef` the entire function.
- **Never wrap gameplay logic** in platform conditionals — that's how determinism dies invisibly.
- **Keep Win32 signatures** so game code is unchanged and stays diffable against upstream.
- Budget it as a real subsystem: dozens of headers — time, files, threads, sockets, types, text encoding, COM stubs.

## 3. Portability bug taxonomy (grep for these on day one)

Each is a *silent* corruption class:

| Class | Bug | Prevention |
|---|---|---|
| Integer width | `long` in on-disk struct = 4 (Win32) vs 8 (LP64) → binary parse breaks | fixed-width types + `static_assert(sizeof)` |
| `wchar_t` width | 2 vs 4 bytes → Unicode disk fields desync | hard-code disk format (UTF-16LE), convert at I/O |
| Exit semantics | `ExitProcess` skips C++ dtors; POSIX runs them → exit crashes | `_exit()` after explicit cleanup |
| Case sensitivity | NTFS paths fail on case-sensitive FS | case-normalizing VFS or lowercase-on-load |
| Path separators | `\` literals in data-driven paths | normalize in the FS layer |
| MSVC builtins | `__max/__int64/__forceinline` | one compat-types header |
| High-DPI | points ≠ drawable pixels → input/render mislocated | decide per API call; verify with a corner tap |

## 4. Verification: determinism is the master gate

- If the game has **replays/lockstep**, run recorded games headless in CI and assert frame-exact outcomes. Catches "harmless" math/iteration/state changes nothing else would.
- **Build every target every session** — "one platform works, the other broken" is still a failure; same-day regressions are cheap.
- **Verify artifacts, not exit codes** — check what's in the binary (`strings`/`nm`), not that the command returned 0. Silent dependency fallbacks pass green.

## 5. Translation-layer integration craft

When adopting DXVK/ANGLE/Wine pieces:
- **Pin to an immutable commit** of your own fork; integrate via submodule/ExternalProject. You *will* accumulate patches.
- **Layers stack, mismatches compound** — each boundary (D3D8→Vulkan→Metal) has capability gaps; budget to read *both* layers' source on a corruption.
- **Ship the layer's config** — defaults are tuned for desktop Wine, not your target.
- **Learn the layer's conventions before patching** (e.g. how it loads SDL) — matching its style avoids self-inflicted breakage.

## 6. Process patterns that make months-long ports survivable

- **Session handoff docs** (blockers+evidence, achievements+paths, explicit next task).
- **A lessons library with stable IDs** — every painful debug becomes a citable artifact.
- **Source-annotation convention** on every change (`// project @bugfix author date desc`) — keeps a huge port diffable and upstream-mergeable.
- **Minimal-diff discipline** — one commit per category; never mix platform code with logic. (A whole-port mega-PR gets rejected as unreviewable; the same work sliced lands.)
- **Reproducible build env** (Docker/pinned toolchain) so "works on my machine" can't burn a session — and so an SDK never ships a host-specific path.

## 7. Calibration (what effort to expect)

- Full references available (SDK + prior port) → **days** (deliver an already-portable engine).
- No prior port but translation layers exist → **months, tiny team** (the reference port).
- No translator, true renderer rewrite → the path everyone abandons.

The whole point of dx8wasm is to move new games into the first row.
