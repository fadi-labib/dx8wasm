# Third-Party Licenses

dx8wasm is GPL-3.0-only by choice (see `LICENSE` and `docs/LICENSING.md`). This
file inventories the external code the SDK actually links or vendors, and the
projects it studied for behaviour without copying code. Keep it current whenever
a dependency is added.

## Linked or vendored today

| Library | License | Where | GPLv3-compatible? |
|---|---|---|---|
| SDL3 | zlib | Emscripten port, pulled by the toolchain at build time (`CMakeLists.txt`); not in tree | yes |
| brotli-wasm 3.0.1 (web build) | Apache-2.0 | vendored at `web-runtime/vendor/brotli/`, LICENSE alongside | yes |

## Reference sources: behaviour studied, no code copied

| Project | License | How it was used |
|---|---|---|
| EA, *C&C Generals / Zero Hour* source | GPL-3.0 + EA §7 terms | The kind of game this SDK runs. Studied to learn which D3D8 / Win32 surface a DX8-era engine needs. No file was copied. `EA_ADDITIONAL_TERMS.md` is reproduced for the games built on this SDK, not because the SDK derives from EA's code. |
| fbraz3/GeneralsX | GPL-3.0 | Cross-platform port of the above. Studied for behaviour, cited in comments as `GeneralsX <file>:<line>`. No code copied. |
| Lolendor/Generals-WebAssembly | GPL-3.0 (AI-generated fork) | Low-trust reference for the WebGL2 approach. Behaviour only. |
| DXVK, `d3d9_fixed_function.cpp` | zlib | Behavioural model for `runtime/graphics-ff/`. If a line is ever copied, add the zlib notice to that file. |
| Wine `wined3d` | LGPL-2.1 | Cross-check only. Never paste: it would pull LGPL text into a GPL file. |

None of these appears in the tree, and no file carries an upstream copyright
header, because nothing was extracted. Should a file ever be extracted from
EA or GeneralsX, it keeps its upstream header verbatim and EA's §7 terms attach
to it (`docs/LICENSING.md`, "Does this bind the SDK?").

## Not used

Earlier revisions of this file listed OpenAL Soft, FFmpeg, SPIRV-Cross, glslang,
Naga and Tint as anticipated dependencies (audio, video, a WebGPU shader path).
None is linked or vendored: audio and video stay game-side, and the WebGPU
backend is parked (`docs/ROADMAP.md`). Add a row above the day one is pulled in,
and drop its full LICENSE text under `web-runtime/vendor/<name>/` if it is
vendored.
