# vendored: brotli-wasm (web build)

Browser brotli decoder — the browser has no native brotli. Vendored from the
`brotli-wasm` npm package (v3.0.1), `pkg.web/` wasm-bindgen web target.

- Upstream: https://github.com/httptoolkit/brotli-wasm
- License: Apache-2.0 (see LICENSE). GPL-3.0-compatible.

Usage: `import init, { decompress } from './brotli_wasm.js'; await init();`
`init()` fetches `brotli_wasm_bg.wasm` next to the module. `decompress(Uint8Array)
=> Uint8Array`. We only use the decoder.
