# web-runtime/

Game-agnostic browser harness: decode a GAXD archive, stream it in, cache it in
OPFS, under cross-origin isolation. (Multiplayer/lobby/signaling from the
reference is out of scope here — that belongs to a consuming game.)

## Status

| Piece | State |
|---|---|
| `gaxd.js` — pure GAXD v2 decoder (browser + Node) | ✅ done, cross-validated |
| `test/gaxd.test.mjs` — packer↔loader byte-exact check | ✅ passing |
| `loader.js` — streaming + OPFS + brotli-wasm wiring | ⏳ next (needs browser to verify) |
| `coi-serviceworker.js` — cross-origin isolation | ⏳ next |
| `index.html` — minimal load-and-list harness | ⏳ next |

## Test

```bash
node web-runtime/test/gaxd.test.mjs
```

Packs a fixture with `asset-tools/pack.py`, decodes it with `gaxd.js` using
Node's built-in brotli, and asserts every file round-trips — both full decode
and per-segment streamed (the path the browser loader takes). This is the
cross-language contract check for `GAXD_FORMAT.md`.

## Design note

The browser has no native brotli (`DecompressionStream` is gzip/deflate only),
so `gaxd.js` takes an **injected** `decompress(Uint8Array) => Uint8Array`. Node
tests inject `zlib.brotliDecompressSync`; the browser injects a wasm brotli
decoder (vendored later, not yet in-tree). Keeping decompression out of the
decoder is what makes the format logic testable without a browser.
