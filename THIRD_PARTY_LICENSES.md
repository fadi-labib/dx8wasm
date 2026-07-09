# Third-Party Licenses

dx8wasm itself is GPL-3.0-only (see `LICENSE` + `EA_ADDITIONAL_TERMS.md`). This
file inventories external code we extract, vendor, or link, and its license.
Keep it current whenever a dependency is added or a source is extracted.

## Upstreams we attribute (GPL-3.0-only, carry EA notices + §7 terms)

| Upstream | Role |
|---|---|
| **EA** — C&C Generals / Zero Hour source | Root copyright. GPLv3 + §7 terms. |
| **fbraz3/GeneralsX** | Human-authored cross-platform port; authoritative derivative upstream. Preserve its copyright on extracted files. |

**Lolendor/Generals-WebAssembly** is an AI-generated web fork — a low-trust
*reference* for the WebGL2 approach, **not** an upstream we attribute. Anything
we take from it is still GPL-3.0-only (EA root); prefer re-deriving over copying.

## Extracted components (all GPL-3.0-only)

| Component | Derived from | Note |
|---|---|---|
| `runtime/d3d8webgl` | web layer re-derived vs. EA/GeneralsX + D3D8 spec; Lolendor as reference | keep EA §7 terms |
| `runtime/compatlib`  | EA/GeneralsX | keep EA §7 terms |
| `web-runtime`        | web layer re-derived; Lolendor as reference | keep EA §7 terms |
| `asset-tools`        | web layer re-derived; Lolendor as reference | keep EA §7 terms |

## Referenced for behavior only (no code copied)

| Project | License | Rule |
|---|---|---|
| DXVK `d3d9_fixed_function.cpp` | zlib | Model logic; if any line copied, add zlib notice to that file. |
| Wine `wined3d` | LGPL-2.1 | Read only. Never paste — pasting brings LGPL into the file. |

## Vendored / linked libraries

| Library | License | GPLv3-compatible? |
|---|---|---|
| SDL3 | zlib | yes |
| OpenAL Soft | LGPL-2.1 | yes (static link OK under GPLv3; prefer SDL3 audio if avoidable) |
| FFmpeg | LGPL-2.1 or GPL (`--enable-gpl`) | yes |
| SPIRV-Cross | Apache-2.0 | yes |
| glslang | BSD-3-Clause | yes |
| Naga | MIT OR Apache-2.0 | yes |
| Tint (Dawn) | BSD-3-Clause | yes |
| **brotli-wasm** (web build) | **Apache-2.0** | yes — **vendored in-tree** at `web-runtime/vendor/brotli/` (browser has no native brotli). LICENSE included there. |

Whenever a library is actually vendored into the tree, drop its full LICENSE
text under the vendor dir (e.g. `web-runtime/vendor/<name>/LICENSE`) and
reference it here.
