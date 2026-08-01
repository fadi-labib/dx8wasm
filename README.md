# dx8wasm

**A reusable SDK for porting classic DirectX-8-era Windows games to the browser (WebAssembly + WebGL2).**

Old fixed-function Windows games (DirectX 8 / DirectDraw, ~1998–2005) are small, often open-source or already reimplemented, and — unlike modern AAA — genuinely portable to the web. dx8wasm packages the hard 80% (D3D8→WebGL2 translation, Win32 shims, browser asset streaming, build glue) so each new game is **"wire it up + fill the gaps it exposes"** instead of a months-long bespoke port.

Proven: a full 500k-LOC engine (C&C Generals Zero Hour) already runs in-browser on this stack. dx8wasm extracts and generalizes it.

## What it is
- `runtime/d3d8webgl` — implements the D3D8 COM API on **WebGL2** (a `d3d8.dll` for the browser). WebGPU backend planned.
- `runtime/compatlib` — Win32 → POSIX/Emscripten shims.
- `runtime/platform` — SDL3 windowing/input + OpenAL audio.
- `web-runtime` — browser harness: shell UI, streaming loader, OPFS cache, Brotli unpack, COOP/COEP service worker, WebRTC.
- `asset-tools` — offline segmented-Brotli asset packer (streams into OPFS).
- `cmake` — Emscripten toolchain + a template a game plugs into.
- `tools/serve-https.py` — dev server: HTTPS (secure context for SharedArrayBuffer) + COOP/COEP + Range.

## What it is NOT
Not an emulator, not a magic button, not for modern DX11/12 AAA. See `SPEC.md` §2–3, §12 for the fit criteria and the "which strategy for which game" decision framework.

## Status
**Phases 0–4 done; game-integration foundation in place.** The D3D8→WebGL2
fixed-function pipeline is broadly feature-complete (transforms, all light types,
ambient/diffuse/specular, fog, texture combiners, render states, pre-transformed
2D, every primitive type) — verified by 35 headless pixel/self-test smokes on Linux CI and
catalogued in [`docs/CONFORMANCE.md`](docs/CONFORMANCE.md). The runtime contract
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
**GPL-3.0-only**, uniformly — the core is extracted from EA's GPLv3 C&C Generals
release and can't be relicensed, so the whole SDK is GPLv3. EA's GPL §7 additional
terms apply and must propagate. See `LICENSE`, `EA_ADDITIONAL_TERMS.md`, and
`docs/LICENSING.md`.
