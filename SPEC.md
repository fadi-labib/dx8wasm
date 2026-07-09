# dx8wasm — Specification

**A reusable SDK for porting classic DirectX-8-era Windows games to WebAssembly (browser).**

Status: **draft / scaffolding.** This document is the design contract; the code is being extracted and generalized from a working reference port (see §11).

---

## 1. Mission

Turn "port a DX8 game to the browser" from a months-long bespoke effort into **"wire it up + fill the gaps it exposes."** Provide the batteries-included layers a classic Windows C++ game needs to run as WASM+WebGL2 (later +WebGPU), so each new game reuses the hard 80% instead of rebuilding it.

Proven feasible: a full 500k-LOC engine (C&C Generals Zero Hour) already runs in-browser via this exact stack (the `Lolendor/Generals-WebAssembly` reference; see §11). dx8wasm extracts that stack, decouples it from any one game, hardens it for Linux CI, and documents the integration contract.

## 2. Non-goals

- **Not** an emulator. We compile the game's own C++ to WASM (Emscripten); we do not emulate x86/Windows. (For DOS-era games, use js-dos/DOSBox-wasm instead — see §12.)
- **Not** a magic button. It is a framework + methodology; each game needs integration work and per-game gap-filling (§7).
- **Not** for modern (DX11/12) AAA engines — those exceed WebGPU's feature subset, WASM32 memory, and are usually closed-source (see §12).
- **Not** a shader *cross-compiler* (those exist: SPIRV-Cross/Tint/Naga). We *generate* fixed-function shaders and *use* those tools downstream (§6.3).

## 3. Scope — which games this fits

A game is a **good fit** when all hold:
1. **C++ source is available** — original (rare, e.g. EA's GPL Generals/C&C) **or** an OSS clean-room reimplementation exists (openage/AoE2, OpenRA/C&C, OpenMW, devilutionX…). Port the reimplementation when the original is closed.
2. **Graphics is DirectDraw-2D or D3D8/9 fixed-function** (or light shader-model-1.x). Modern shader-heavy games are out of scope.
3. **Fits WASM32 memory** (~2–4 GB working set; assets stream). ~2003-era games qualify.
4. Dependencies are shimmable (DirectInput/Sound/Show/Play → SDL3/OpenAL/FFmpeg/WebRTC).

**Poor fit:** closed-source modern games, DX11/12 engines, kernel anti-cheat, >4 GB working set.

## 4. Architecture

A game sits on top; dx8wasm supplies every layer beneath it. Strategy per layer follows the **translate / shim / swap / stub** model (§7).

```
        ┌─────────────────────────────────────────────┐
        │  GAME (its own C++, compiled to WASM)         │  ← unchanged; keeps calling Win32/D3D8
        └───────────────┬───────────────┬──────────────┘
        Direct3DCreate8  │  SDL3 / OpenAL │  Win32 calls
        ┌───────────────▼───┐ ┌─────────▼───┐ ┌────────▼────────┐
        │ runtime/d3d8webgl │ │ platform    │ │ runtime/compatlib│
        │ D3D8 COM → WebGL2 │ │ SDL3 window │ │ Win32 → POSIX/   │
        │  (+WebGPU later)  │ │ input+audio │ │ Emscripten shims │
        │  └ graphics-ff:   │ └─────────────┘ └─────────────────┘
        │    fixed-func →   │
        │    shader gen     │
        └───────────────────┘
        ┌─────────────────────────────────────────────┐
        │ web-runtime: shell UI, streaming loader,      │  ← game-agnostic browser harness
        │ OPFS cache, Brotli unpack, COOP/COEP SW,      │
        │ WebRTC signaling/UDP bridge                    │
        └─────────────────────────────────────────────┘
        ┌─────────────────────────────────────────────┐
        │ asset-tools: GAXD segmented-Brotli packer     │  ← offline, game-agnostic
        └─────────────────────────────────────────────┘
        ┌─────────────────────────────────────────────┐
        │ cmake: Emscripten toolchain preset + template │
        └─────────────────────────────────────────────┘
```

## 5. Components

All components are **GPL-3.0-only** (see §13 and `docs/LICENSING.md`). The
"Origin" column notes where the code comes from; permissive origins are modeled
for behavior, not license.

| Component | Path | Origin | Extraction status |
|---|---|---|---|
| **d3d8webgl** — D3D8 COM object model → WebGL2 runtime | `runtime/d3d8webgl/` | re-derived (~3.4k LOC surface), ref: Lolendor | to build + generalize |
| **graphics-ff** — fixed-function → shader generator | `runtime/graphics-ff/` | new; models DXVK `d3d9_fixed_function.cpp` (zlib), no code copied | to build |
| **compatlib** — Win32 → POSIX/Emscripten shims | `runtime/compatlib/` | re-derived, ref: Lolendor CompatLib (~28 headers) | to build |
| **platform** — SDL3 windowing/input + OpenAL audio glue | `runtime/platform/` | thin, new + SDL3 | to build |
| **web-runtime** — shell, loader, OPFS, Brotli, COI SW | `web-runtime/` | re-derived, ref: Lolendor `web/shell` | gaxd decoder ✅; loader/SW next |
| **asset-tools** — GAXD segmented-Brotli packer | `asset-tools/` | re-derived, ref: Lolendor `packer.py` | ✅ present |
| **cmake** — Emscripten toolchain + template CMakeLists | `cmake/` | new, ref: Lolendor preset | in progress |
| **tools/serve-https.py** — dev HTTPS+COI+Range server | `tools/` | ours (this session) | ✅ present |
| **contract** — integration ABI a game targets | `runtime/include/dx8wasm/` | new | ✅ present |

## 6. Graphics translation (the crown jewel)

### 6.1 Runtime, not compiler
`d3d8webgl` is a **runtime library** that implements the D3D8 COM interfaces (`IDirect3D8`, `IDirect3DDevice8`, textures, vertex/index buffers, surfaces, swapchain) backed by WebGL2. Most of it is resource/state/draw plumbing; the fixed-function *shader generation* is one module. A game calling `Direct3DCreate8` transparently gets the WebGL2 device.

### 6.2 Coverage generalization
The reference `d3d8webgl` implements the **subset one game used**. dx8wasm adds:
- A **capability/coverage layer**: unimplemented render states, texture-stage ops, or formats **log + fall back** loudly (never silently render wrong), so a new game's gaps are visible.
- Fixed-function behavior modeled on **DXVK `d3d9_fixed_function.cpp`** (permissive, 2.6k LOC, covers T&L, lighting, fog, `D3DTOP_*` combiners, alpha test, vertex-blend/skinning) and cross-checked against **Wine `wined3d`** — the two exhaustive OSS references.

### 6.3 Dual target (WebGL2 now, WebGPU later)
Two backends behind one interface:
- **WebGL2** (default, max reach): fixed-function → **GLSL ES** directly (the reference approach).
- **WebGPU** (opt-in, future): fixed-function → **SPIR-V** (DXVK-modeled) → **WGSL** via Tint/Naga; existing SM1.x shaders → SPIR-V → WGSL. Also enables SPIR-V → GLSL via SPIRV-Cross to share one FF core across both.

## 7. Per-subsystem strategy (translate / shim / swap / stub)

The core methodology (see `docs/PORTING_METHOD.md`): choose per subsystem by API-surface size + determinism risk.

| Subsystem | Strategy | dx8wasm provides |
|---|---|---|
| Graphics | **Translate** | `d3d8webgl` (keep D3D8 API, WebGL2 under it) |
| OS plumbing | **Shim** | `compatlib` (Win32→POSIX, included first, whole-function wraps) |
| Window/input/audio | **Swap** | `platform` on SDL3 + OpenAL (find the engine's Device/Manager seam) |
| Video | **Swap** | FFmpeg-wasm player behind the game's video seam |
| Networking | **Shim/Translate** | UDP-over-WebRTC bridge + WebSocket/MQTT signaling |
| Assets/FS | **Shim + harness** | WASMFS+OPFS + GAXD streaming |
| Periphery | **Stub** | whole-function stubs for screenshots/dialogs/telemetry |

## 8. Portability bug taxonomy (proactive checklist)

Grep/audit these on day one of any port — each is a *silent* corruption class:
- **Integer width** in on-disk structs (`long` = 4/8 bytes) → fixed-width types + `static_assert(sizeof)`.
- **`wchar_t` width** (2 vs 4) → hard-code disk format (UTF-16LE), convert at I/O.
- **Exit semantics** (`ExitProcess` skips dtors) → `_exit()` after explicit cleanup.
- **Case sensitivity** & **path separators** → normalize in the VFS layer, not at call sites.
- **MSVC builtins** (`__max/__int64/__forceinline`) → one compat-types header.
- **High-DPI** points≠pixels → decide per API call; verify input scaling with a corner tap.

## 9. Verification & CI

- **Determinism as master gate:** if the game has replays/lockstep, run recorded games headless in CI and assert frame-exact outcomes. Catches "harmless" changes no eyeball pass would.
- **Linux CI is mandatory** (the reference port was macOS-only and shipped a hardcoded `/opt/homebrew` python path — exactly the class of bug an SDK must not have).
- **Artifact verification over exit codes** — check what's *in* the wasm (`strings`/`nm`), not that the build returned 0.
- **Headless render tests** where possible (offscreen framebuffer → pixel readback; cf. the WebGPU PoC pattern).
- **Behavioral acceptance per phase** ("menu renders", "skirmish loads", "10-min stability").

## 10. Integration contract (how a game plugs in)

A consuming game must:
1. Build its C++ against `compatlib` (keeps Win32 signatures — game code stays diffable against upstream).
2. Call `Direct3DCreate8()` (from `d3d8webgl`) — transparently gets the WebGL2 device.
3. Use `platform` (SDL3) for window/input, OpenAL for sound (or shim its DirectInput/DirectSound behind these).
4. Provide a small **entry shim** (`WebMain`-style) and declare its asset roots.
5. Run its game files through `asset-tools` → streamable bundle.
6. Configure the `cmake` template (targets, feature flags).

Formal ABI: `runtime/include/dx8wasm/contract.h`.

## 11. Provenance & the reference port

Copyright-bearing upstreams (both GPL-3.0-only, both preserve EA's §7 terms): **EA** (GPLv3 *Generals/Zero Hour* source, the root) and **fbraz3/GeneralsX** (the human-authored cross-platform port — our authoritative derivative upstream). **Lolendor/Generals-WebAssembly** (EA → GeneralsX → Mac/iOS → Lolendor) is an **AI-generated** web fork: we use it only as a low-trust *reference* for the WebGL2 approach, not an authoritative upstream or attribution target. AI generation doesn't strip GPL — it derives from EA's engine, so anything drawn from it stays GPL-3.0-only. We **prefer re-deriving** the web-agnostic layers against GeneralsX + EA + the D3D8 spec over copying Lolendor, plus DXVK (permissive) for the fixed-function core. See `docs/LICENSING.md` for provenance rules.

## 12. Alternatives & the decision framework (know when NOT to use dx8wasm)

```
1. OSS reimplementation exists? (openage, OpenRA, OpenMW, devilutionX, OpenRCT2, OpenTTD)
     → port THAT with dx8wasm (often already SDL-based).
2. Original source available?  no → reimpl or emulate.
3. Graphics API?  DirectDraw-2D (easy) < D3D fixed-function (dx8wasm sweet spot)
                  < D3D shader (cross-compile) < DX11/12 modern (out of scope).
4. DOS-era / tiny?  → js-dos / DOSBox-wasm / v86 (zero porting).
5. Built on Unity/Godot?  → use the engine's native web export.
```

## 13. Licensing

**GPL-3.0-only, uniformly.** The core (`d3d8webgl`, `compatlib`, `web-runtime`, `asset-tools`) is extracted from EA's GPLv3 C&C Generals/Zero Hour release (via the Lolendor reference) and cannot be relicensed. Rather than a fragile mixed matrix, the whole SDK — including our own new files — is GPL-3.0-only. EA's GPL §7 additional terms (no trademark use, notice must propagate, mark modifications, indemnify EA) apply to the derived code and therefore to the whole build. `graphics-ff` models DXVK (zlib) behaviorally without copying code. Consuming games are always GPLv3 derivatives (they compile EA's source) and must comply. The **SDK's own** license, though, is contingent: EA/§7 bind it only if we *extract* EA/GeneralsX code — if we stay clean-room (reference + reimplement, the preferred stance), the SDK isn't EA-derived and could go permissive. We ship GPLv3-uniform by default for simplicity; permissive stays open per component until something EA-derived is extracted. Authoritative details and the decision table: `docs/LICENSING.md` (§"Does this bind the SDK?"), plus `LICENSE`, `EA_ADDITIONAL_TERMS.md`, `THIRD_PARTY_LICENSES.md`.

## 14. Roadmap

See `docs/ROADMAP.md`. Phase 0 (this): spec + scaffold + methodology + contract + dev server. Phase 1: extract asset-tools + web-runtime (game-agnostic, cleanest). Phase 2: extract + de-couple d3d8webgl + compatlib. Phase 3: coverage/fallback layer + DXVK-modeled FF hardening. Phase 4: Linux CI + headless tests. Phase 5: WebGPU backend. Phase 6: second-game validation (pick a DirectDraw-2D or reimplemented title to prove generality).
