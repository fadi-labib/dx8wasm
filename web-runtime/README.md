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
| `onboard.js` + `onboard.html` — client-side asset import (own-the-assets ports) | ✅ done, headlessly verified — but **no longer the path Generals runs**, see below |
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

### `onboard.js` is a reference implementation, not Generals' live path (2026-08-06)

Read this before improving it. The Generals port **does not use `onboard.js`**. It grew its own
onboarding into `generals-dx8wasm/web/byo-*.js`: an eight-step wizard, an install manifest, size-based
verification on every boot, and targeted repair of evicted files. That decision is recorded as D4 in
`generals-dx8wasm/docs/superpowers/specs/2026-08-06-byo-onboarding-wizard-design.md`, along with its
stated cost — **two onboarding implementations now exist and will drift.**

This one was deliberately kept rather than deleted: it is generic over games via a profile object,
it is covered by `test/onboard.browser.test.mjs` in this repo's `npm test`, and it is the thing a
*second* own-the-assets port would start from. But it is now the simpler of the two, and a fix made
here does not reach Generals.

Two things worth stealing from the game-repo version if you extend this one, both learned the
expensive way: a folder handle persisted in IndexedDB is what makes a targeted repair possible
instead of a multi-gigabyte re-import, and a completion marker alone is not an install oracle — it
survives storage eviction, so the game boots into missing data with nothing to explain it. If the
SDK component gains no second consumer, delete it then rather than maintaining a divergent twin.

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
