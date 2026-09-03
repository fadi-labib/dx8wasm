# Results — the pre-publish review and v0.1.0 (2026-09-03)

The repository was private and about to be published. A full read of the runtime and of the
2026-08-28..30 commits, backed by two delegated reviews (runtime C++; licensing/docs/tooling) whose
findings were each red-checked against the code before anything was changed, found real defects in
the device, a self-contradicting licensing narrative, README claims the code did not back, and a path
traversal in every ad-hoc dev server. Everything was fixed test-first, `scripts/ci.sh` was green
locally and on GitHub, the repo was made public and tagged **`v0.1.0`**.

Range: `a4383c5..303f4db` — 6 commits: tooling, device, licensing story, docs hygiene, Node-24 CI
actions. Deployed the same day with the Generals integration (`generals-dx8wasm` `v0.1.0`, engine
`web-v0.1.0`); the integration's record is `generals-dx8wasm/docs/SESSION-RECORD-2026-09.md`.

## What the review found, and what was done

| Finding | Where | Fix | Pinned by |
|---|---|---|---|
| `DrawIndexedPrimitive` hard-coded `GL_UNSIGNED_SHORT`; a `D3DFMT_INDEX32` buffer drew as u16 pairs — degenerate triangles, nothing counted, while caps advertise `MaxVertexIndex 0xFFFFF` | `device.cpp` | element type from the buffer's format; `CreateIndexBuffer` refuses non-index formats | `index32_smoke` |
| `CopyRects` checked neither format equality nor bounds; the new DXT branch overflowed the staging vector on a DXT5→DXT1 copy or an off-grid rect | `device.cpp` | formats must match; rects must fit both surfaces and sit on the 4×4 grid for DXT; validated before the first byte | `resource_contract_smoke` |
| `GLBuffer::Lock` let `off == length` with size 0 through (one-past-the-end pointer); both `LockRect` paths handed out pointers for rects past the level; `Texture8::LockRect` ignored its rect | `device.cpp` | refuse; honour the rect | `resource_contract_smoke` |
| `SetTexture(stage ≥ 2)` and `SetStreamSource(stream ≠ 0)` returned `D3D_OK` and dropped the object with no trace | `device.cpp`, `coverage.*`, `contract.h` | counted: new `unhandled_slots` field, telemetry kinds `stage`/`stream`; `MaxStreams = 1` | `resource_contract_smoke` |
| `GetIndices` always returned base 0 | `device.cpp` | returns the stored base | `resource_contract_smoke` |
| Comments contradicting the code: "Lock does no bounds check", "counters are always exact regardless of caller thread" | `device.cpp`, `contract.h` | corrected; the half-pixel translation's backbuffer denominator documented as exact only for full viewports | — |
| Seven ad-hoc static servers mapped `join(root, decodeURIComponent(url))`; `GET /../../.git/config` served the repo's git config | `scripts/*.mjs`, `web-runtime/test/*.mjs` | `scripts/lib/static-path.mjs` confines or 404s | `static-path.test.mjs` |
| `serve-https.py`: suffix ranges read as `0..N`, garbage ranges a traceback, `_range` leaked across keep-alive after a HEAD | `tools/serve-https.py` | `parse_range()` | `--selftest`, in `ci.sh` |
| README/SPEC/EA terms said "extracted from EA via the Lolendor fork"; AGENTS/llms.txt/LICENSING said clean-room; the third-party file disowned the fork it derived through | six files | one story: independent reimplementation, no code copied, GPL-3.0 by choice, EA §7 reproduced for the games built on it | — |
| README claimed OpenAL, WebRTC, a shell UI, a cmake template, "WebGPU planned", "36 smokes"; six libraries listed as vendored were never pulled in | `README.md`, `THIRD_PARTY_LICENSES.md`, `docs/LICENSING.md` | claims removed; inventory lists what is linked, and what was once planned as "not used" | link check, path grep |
| Home-directory paths, links into the private integration repo, a 1,939-line machine-specific plan, a roadmap calling GitHub CI untested (green since 08-07), `llms-full.txt` 17 lines behind | `AGENTS.md`, `docs/*` | rewritten as prose; plan retired; regenerated | `linkcheck` |

Verified clean: no secrets, no tracked build artifacts, SPDX on every source file, `package.json`
private and GPL, the COI service worker original, the packer free of path writes, the licence text
canonical.

## Lessons (PPR)

**PPR-1 — The repo's own words are admissions.** A public reader, or a lawyer, treats README and the
EA-terms preamble as statements of fact. The tree (no upstream copyright header anywhere) said the code
was original; the prose said it was extracted. Before publishing, grep every provenance claim and make
them agree with the code, then say the same thing in every file.

**PPR-2 — "Clean-room" is a term of art.** It means the implementers never saw the original.
Studying GPL sources for behaviour and reimplementing is legitimate and is an *independent
reimplementation*; AGENTS.md permitted studying GeneralsX, so "clean-room" was an overclaim. Use the
accurate term everywhere, including code headers.

**PPR-3 — A count in prose is a liability the day after it is written.** "36 headless smokes" had
drifted to 43 (CDI-16 said this once already for the conformance table). Prose now points at the
generated tables and says "one smoke per feature"; the harness gained a name filter so a single fixture
can be run without the count ever mattering.

**PPR-4 — Inventory what is linked, not what was planned.** Six libraries sat in the third-party
inventory as "vendored / linked" because a roadmap once intended them. A public inventory that lists
phantom dependencies is misleading in the direction that matters (readers assume more licences bind
them). The file now has "linked or vendored today" and "not used".

**PPR-5 — Two code paths for one operation diverge.** `DrawIndexedPrimitiveUP` grew format handling;
`DrawIndexedPrimitive` never did, so 32-bit indices silently drew garbage on the buffer path only. When
one path learns something, look for its twin.

**PPR-6 — "Coverage, not silence" gaps hide where a non-null object is dropped with `D3D_OK`.** Stage
≥ 2 textures and stream ≠ 0 buffers were returned as success and discarded; a clear with `nullptr` (the
engine's blanket reset) must stay free, a real object must count. The rule's audit question is: *where
does this return `D3D_OK` and throw the argument away?*

**PPR-7 — `path.join` normalises, it does not confine.** Seven servers each carried the same
`join(root, decodeURIComponent(url))`; one shared helper with a test (`resolve`, then require the
`root + sep` prefix; a malformed escape is a 404) fixed all of them and cannot be forgotten by the
eighth.

**PPR-8 — Delegated reviews are hypotheses until red-checked.** Every subagent finding was verified
against the code before being acted on. All held; one (`GetTexture(stage ≥ 2)` returning `D3D_OK`
with null) was judged harmless and left. The red-check is what makes a review a result rather than an
opinion — the same rule that closed the lost-mouse bug on the game side the same day (`GT-31`).
