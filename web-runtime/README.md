# web-runtime/

Game-agnostic browser harness: decode a GAXD archive, stream it in, cache it in
OPFS, under cross-origin isolation. (Multiplayer/lobby/signaling from the
reference is out of scope here — that belongs to a consuming game.)

Nothing in this directory may depend on a specific game. Everything here runs standalone
with `npm test` and touches no path outside this repo. Game-coupled test harnesses (ones that
`spawn('node', ['scripts/serve-game.mjs'])` or otherwise reach into a consuming game's build
output) belong in that game's own repo, not here — see e.g.
[`generals-dx8wasm/web-runtime/`](../../generals-dx8wasm/web-runtime/README.md), which adopted
this repo's Generals-specific harnesses on 2026-07-31 for exactly that reason.

## Status

| Piece | State |
|---|---|
| `gaxd.js` — pure GAXD v2 decoder (browser + Node) | ✅ done, cross-validated |
| `loader.js` — Range streaming + OPFS cache (bounded memory) | ✅ done, headlessly verified |
| `onboard.js` + `onboard.html` — client-side asset import (own-the-assets ports) | ✅ done, headlessly verified |
| `coi-serviceworker.js` — cross-origin isolation for static hosts | ✅ done |
| `index.html` — load-and-list harness | ✅ done |
| `vendor/brotli/` — brotli-wasm web decoder (Apache-2.0) | ✅ vendored |

## Two ways assets reach OPFS

- **`loader.js`** — stream a **pre-packed GAXD archive from a URL** (for freely
  distributable data). Server hosts the archive.
- **`onboard.js`** — the user points at **their own installed game folder**
  (File System Access API) and we copy the needed files into OPFS **locally**.
  Copyrighted game data never touches a server — this is how an *own-the-assets*
  port (Generals, etc.) ships legally and still plays for a stranger. Chromium/
  Edge only (multi-GB folders can't use the in-memory `<input>` fallback).

## Tests

```bash
cd web-runtime && npm install && npm test
```

- `test/gaxd.test.mjs` — cross-language contract check: packs a fixture with
  `asset-tools/pack.py`, decodes with `gaxd.js` via Node's built-in brotli,
  asserts byte-exact on both full-decode and per-segment-streamed paths.
- `test/loader.browser.test.mjs` — end-to-end in real Chromium (Playwright):
  serves a packed archive with Range + COOP/COEP, runs `loader.js`, asserts
  every file lands in OPFS byte-exact (SHA-256), the page is crossOriginIsolated,
  and a reload hits the OPFS cache.
- `test/onboard.browser.test.mjs` — drives `onboard.js` in Chromium against a
  stand-in install folder: asserts validation, filtered recursive import into
  OPFS byte-exact, junk excluded, and the completion marker / `isImported()`.

## Design note

The browser has no native brotli (`DecompressionStream` is gzip/deflate only),
so `gaxd.js` takes an **injected** `decompress(Uint8Array) => Uint8Array`. Node
tests inject `zlib.brotliDecompressSync`; the browser injects a wasm brotli
decoder (vendored later, not yet in-tree). Keeping decompression out of the
decoder is what makes the format logic testable without a browser.
