# Licensing

**Default: dx8wasm is GPL-3.0-only, uniformly, across every component.**

This default is a deliberate choice for simplicity and safety — but whether it's
*legally required* depends on one thing: **do we copy EA/GeneralsX code, or only
reference it?**

- **If we extract EA/GeneralsX source** (copy code into our tree), those files are
  GPLv3 derivatives. GPLv3 is strong copyleft — they can only be conveyed under
  GPLv3, can't be relicensed (we don't hold the copyright), and EA's §7 terms
  attach. A build linking them is GPLv3, full stop.
- **If we stay clean-room** (read/reference GeneralsX/Lolendor, reimplement against
  the D3D8 API — which is Microsoft's, not EA's — and copy zero lines), the SDK is
  **not EA-derived** and we're free to license it however we want, including
  permissively. Copyright covers expression, not behavior or APIs.

We just adopted a **prefer-re-deriving** stance (see Provenance), so the clean-room
path is live. We still ship the whole SDK as GPL-3.0-only *by choice* — one license,
no mixed-matrix footguns — until/unless we deliberately go permissive. See
[Does this bind the SDK?](#does-this-bind-the-sdk) for the decision.

- Full license text: [`LICENSE`](../LICENSE) (GNU GPL v3, 29 June 2007).
- EA's additional terms: [`EA_ADDITIONAL_TERMS.md`](../EA_ADDITIONAL_TERMS.md).

## Does this bind the SDK?

The trigger for GPL + EA §7 is **copying/deriving code**, not reading it.

| What we do with EA/GeneralsX | SDK is EA-derived? | SDK license |
|---|---|---|
| **Reference** — learn, reimplement clean-room, copy nothing | No | Our choice (permissive possible) |
| **Extract** — copy any EA/GeneralsX source | Yes | GPL-3.0-only + §7, non-negotiable |

**Always true regardless:** a *consuming game* compiles EA's actual GPLv3 game
source, so the **shipped game is always EA-derived** — GPLv3 + §7 bind it even if
our SDK is 100% clean-room. dx8wasm exists to run that game, so §7 is always in
play downstream; it only reaches *the SDK itself* if we extract.

## SPDX header convention

Every source file carries, at the top:

```
SPDX-License-Identifier: GPL-3.0-only
```

Extracted files **also keep their upstream copyright headers verbatim** and add
a dx8wasm provenance annotation (`// dx8wasm @extract/@fix <author> <date>`, see
`PORTING_METHOD.md` §6) so changes stay upstream-mergeable.

## EA Section 7 additional terms (must propagate)

EA attached further terms under GPL §7. They travel with any **EA-derived** code —
so they bind the SDK *if we extract*, and they **always** bind a consuming game.
In short — you must:

- **not** use "Command & Conquer" or any EA trademark, or imply EA affiliation;
- include the copyright notice **and** these terms on any propagation/conveyance;
- mark modified versions as modified and not misrepresent their origin;
- indemnify EA if you assume contractual liability to downstream recipients.

Full wording in [`EA_ADDITIONAL_TERMS.md`](../EA_ADDITIONAL_TERMS.md).

## Practical consequences

- **We ship the SDK as GPL-3.0-only by default.** A game shipped on it is a GPLv3
  derivative (it compiles EA's source) and must comply: convey complete
  corresponding source, license the combined work GPLv3-compatibly, keep all
  notices, and carry EA's §7 terms.
- **Game assets are never covered.** They remain the game owner's / publisher's
  copyright. dx8wasm never bundles or redistributes assets — users bring their
  own, as with the reference port.
- **Permissive is on the table — if we stay clean-room.** As long as we copy no
  EA/GeneralsX code, the SDK isn't EA-derived and we could relicense some or all
  of it permissively (`contract.h`, `platform`, asset-format spec, methodology are
  natural candidates). We keep the GPLv3-uniform default for now to avoid a
  mixed-license matrix; going permissive is a deliberate future decision, gated on
  verifying nothing EA-derived was extracted. The moment we extract, that door
  closes for the affected components.

## Provenance

Two upstreams matter for copyright and attribution, both **GPL-3.0-only** and
both carrying EA's §7 terms verbatim:

- **EA** — original *Generals / Zero Hour* source (GPLv3, Feb 2025). The root.
- **fbraz3/GeneralsX** — the human-authored cross-platform port that preserves
  EA's GPLv3 + §7 notices. This is our authoritative derivative upstream; keep
  its copyright on any file extracted from it and mirror fixes back where useful.

**Lolendor/Generals-WebAssembly** is an **AI-generated** web fork further down
the chain (EA → GeneralsX → Mac/iOS → Lolendor). We treat it as a low-trust
**reference implementation** for the web/WebGL2 approach — *not* an authoritative
upstream and *not* an attribution target. AI generation does **not** strip GPL:
it derives from EA's engine, so anything taken from it is still GPL-3.0-only.
Because its provenance and header hygiene are unreliable, **prefer re-deriving
the web layer against GeneralsX + EA + the D3D8 spec over copy-pasting Lolendor**;
where we do follow its approach, the result is our own GPLv3 code, not a Lolendor
attribution.

Before extracting any file, confirm it carries EA's original GPL notice (and
GeneralsX's, if from that tree). If a downstream fork stripped it, restore it
from EA/GeneralsX upstream rather than extracting a header-less copy.

## Third-party dependencies

We vendor / link these. All are GPL-3.0-compatible; two need care:

| Dependency | License | Note |
|---|---|---|
| DXVK (`d3d9_fixed_function.cpp`) | zlib | We model *behavior*, don't copy. If any line is copied verbatim, that file also carries the zlib notice. |
| Wine `wined3d` | **LGPL-2.1** | Behavioral cross-check **only**. Never paste wined3d code — pasting pulls LGPL text into a file. Read for understanding, reimplement. |
| SPIRV-Cross / glslang | Apache-2.0 / BSD | compatible |
| Naga / Tint | MIT-Apache / BSD | compatible |
| SDL3 | zlib | compatible |
| OpenAL Soft | **LGPL-2.1** | WASM links statically → LGPL's dynamic-link escape doesn't apply. Subsumed by the SDK being GPLv3, but prefer SDL3 audio if avoidable. |
| FFmpeg | LGPL-2.1 / GPL (`--enable-gpl`) | Either is fine under a GPLv3 SDK. |

A machine-readable inventory lives in [`THIRD_PARTY_LICENSES.md`](../THIRD_PARTY_LICENSES.md).
