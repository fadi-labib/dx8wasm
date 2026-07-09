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
**Phase 0 — spec + scaffold.** See `SPEC.md` for the design, `docs/ROADMAP.md` for the plan, `docs/PORTING_METHOD.md` for the methodology that generalizes to any classic-game port.

## Quickstart (once a game is wired — target shape)
```bash
# build the game to wasm against the SDK
cmake --preset emscripten && cmake --build build/emscripten
# pack the user's game assets
asset-tools/pack.sh <build-name>
# serve with a secure context (SharedArrayBuffer needs HTTPS or localhost)
python3 tools/serve-https.py 8443     # https://<host>:8443
```

## License
**GPL-3.0-only**, uniformly — the core is extracted from EA's GPLv3 C&C Generals
release and can't be relicensed, so the whole SDK is GPLv3. EA's GPL §7 additional
terms apply and must propagate. See `LICENSE`, `EA_ADDITIONAL_TERMS.md`, and
`docs/LICENSING.md`.
