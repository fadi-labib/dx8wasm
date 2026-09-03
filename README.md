# dx8wasm

**A reusable SDK for porting classic DirectX-8-era Windows games to the browser (WebAssembly + WebGL2).**

Old fixed-function Windows games (DirectX 8 / DirectDraw, ~1998–2005) are small, often open-source or already reimplemented, and — unlike modern AAA — genuinely portable to the web. dx8wasm packages the hard 80% (D3D8→WebGL2 translation, Win32 shims, browser asset streaming, build glue) so each new game is **"wire it up + fill the gaps it exposes"** instead of a months-long bespoke port.

Proven: a full 500k-LOC engine (the GPLv3 *Command & Conquer: Generals — Zero Hour* source, via the GeneralsX port) runs in the browser on this SDK. dx8wasm is an independent reimplementation of the D3D8 and Win32 subset that engine needs; it contains no code from the game.

## What it is
- `runtime/d3d8webgl` — implements the D3D8 COM API on **WebGL2** (a `d3d8.dll` for the browser).
- `runtime/graphics-ff` — the fixed-function pipeline as generated GLSL (transforms, lights, fog, texture combiners).
- `runtime/compatlib` — Win32 → POSIX/Emscripten shims, tiered (`docs/COMPATLIB.md`).
- `runtime/platform` — SDL3 windowing/input seam, plus an opt-in OPFS bridge for ranged asset reads. Audio stays game-side.
- `runtime/coverage`, `runtime/telemetry` — every unimplemented D3D8 token is flagged, counted and fallen back from, never silently wrong; an NDJSON telemetry sink measures a real run.
- `web-runtime` — browser harness: streaming loader, OPFS cache, Brotli unpack, COOP/COEP service worker, and a reference asset-onboarding page.
- `asset-tools` — offline segmented-Brotli asset packer (streams into OPFS).
- `tools/serve-https.py` — dev server: HTTPS (secure context for SharedArrayBuffer) + COOP/COEP + Range.

## What it is NOT
Not an emulator, not a magic button, not for modern DX11/12 AAA. No audio layer, no networking, and no WebGPU backend (parked, see `docs/ROADMAP.md`). See `SPEC.md` §2–3, §12 for the fit criteria and the "which strategy for which game" decision framework.

## Status
**v0.1.0 (2026-09-03) is the first public release**; the review that preceded it is
[`docs/RESULTS-2026-09-03-pre-publish-review.md`](docs/RESULTS-2026-09-03-pre-publish-review.md).
**Phases 0–4 done; game-integration foundation in place.** The D3D8→WebGL2
fixed-function pipeline is broadly feature-complete (transforms, all light types,
ambient/diffuse/specular, fog, texture combiners, render states, pre-transformed
2D, every primitive type) — verified by one headless pixel/self-test smoke per feature on
Linux CI (GitHub Actions) and catalogued in [`docs/CONFORMANCE.md`](docs/CONFORMANCE.md). The runtime contract
(init + input pump + coverage introspection) is implemented, and a game plugs in
through the public surface alone.

- **Wiring a game:** [`docs/INTEGRATION.md`](docs/INTEGRATION.md) — the step-by-step guide.
- **Working template:** [`examples/minigame/`](examples/minigame/) — a keyboard-controlled sprite using only `dx8wasm_init` + D3D8 + `dx8wasm_pump`.
- Design: `SPEC.md`; plan: `docs/ROADMAP.md`; methodology: `docs/PORTING_METHOD.md`.
- **For AI agents:** [`llms.txt`](llms.txt) (index) · [`AGENTS.md`](AGENTS.md) (contributing) · [`docs/SDK_REFERENCE.md`](docs/SDK_REFERENCE.md) (building against) · [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md). Single-file context: [`llms-full.txt`](llms-full.txt) (`bash scripts/gen-llms-full.sh` to regenerate).

## Quickstart
```bash
# build every wasm target (SDK + smokes + examples)
cmake --preset emscripten && cmake --build build/emscripten
# see the pipeline live in a browser
node scripts/demo.mjs        # http://127.0.0.1:8080  (spinning textured + lit quads)
node scripts/minigame.mjs    # http://127.0.0.1:8081  (arrow-key sprite — the integration template)
# run the headless pixel-smoke suite
node web-runtime/test/phase2.gpu.test.mjs
```

## License
**GPL-3.0-only**, by choice, across the whole SDK. dx8wasm is an independent
reimplementation: no code from EA's GPLv3 Generals release, from GeneralsX, or from
any other fork was copied — every file is an SPDX-only original, and
`docs/LICENSING.md` has the provenance rules. EA's GPL §7 additional terms are
reproduced in `EA_ADDITIONAL_TERMS.md` because any game built from EA's source on
this SDK must carry them. See also `THIRD_PARTY_LICENSES.md`.
