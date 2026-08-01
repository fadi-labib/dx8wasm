# Results — closing the remaining `docs/` items (2026-08-01)

What began as "check every remaining item in `docs/` and make a plan" became an audit that reversed
several of its own premises, a four-tier plan, and a parallel execution in which **three of ten
tasks found a defect in the plan's own code**. This records what shipped, what the docs claimed
before they were checked, what was deliberately left undone, and the lessons worth citing later.

Plan: [`superpowers/plans/2026-08-01-close-the-remaining-docs-items.md`](superpowers/plans/2026-08-01-close-the-remaining-docs-items.md).
Range: `bc7ae37..a71e6f3` — 22 non-merge commits, of which 13 are the ten planned tasks across four
merged branches and the rest are setup plus the final-review fix wave described at the end.

## The audit, and what it overturned

The docs' own account of what was open was wrong in both directions.

- **Two plans that looked unstarted were fully implemented.**
  [`2026-07-29-honest-stubs.md`](superpowers/plans/2026-07-29-honest-stubs.md) (8 tasks) and
  [`2026-07-12-texture-surfaces.md`](superpowers/plans/2026-07-12-texture-surfaces.md) (4 tasks) had
  every checkbox unticked and every deliverable present: `format_support.h`,
  `caps_query_smoke.cpp`, `honest_stubs_smoke.cpp`, `surface_smoke.cpp`, a stencil-reporting
  `dx8wasm_has_cap`, the "Stubs fail loudly" contract in `SDK_REFERENCE.md`,
  `CopyRects`/`UpdateTexture`. Anyone resuming from those documents would have redone landed work.
- **A generated document had drifted anyway.** `CONFORMANCE.md` is half machine-probed, half
  hand-curated — and the curated half is a JS array *inside the generator*, so regenerating faithfully
  re-emitted stale rows. It declared the second texture stage missing (implemented: stage-1 slot in
  `device.cpp`, combiner chaining in `ff_shader.cpp`) and textures "level 0, nearest, clamp"
  (bilinear/trilinear over a full mip chain with per-stage filter and address state). Eleven of the
  then-31 smokes verified features with no row in the matrix at all.
- **The roadmap's guidance on its own remaining work was wrong.** It said implementing
  `D3DRS_FILLMODE` would force `coverage_smoke`'s probe to move. GLES3/WebGL2 has no
  `glPolygonMode`, so the honest change is value-sensitive rather than wholesale — and the probe,
  which uses `WIREFRAME`, needed no change at all.

## What shipped

Ten tasks in four tiers, executed as four parallel tracks on separate git worktrees. Every task
ended with `scripts/ci.sh` printing `ALL GREEN`; every runtime change landed with a headless pixel
smoke. The suite grew from 31 to **35** smokes.

### Tier 1 — make the docs describe the code

| Change | Commit |
|---|---|
| Corrected the curated feature table in `scripts/conformance.mjs`: two wrong rows fixed, seven rows added for previously-unrepresented smokes, `FVF: SPECULAR` split into an honest `partial` plus a `yes`; regenerated `CONFORMANCE.md` | `3b810eb` |
| Stamped both landed plans COMPLETE with the evidence behind the word "verified"; pointed the roadmap's measured-gap bullet at the new plan | `996de7e` |

Both reviews re-derived the underlying evidence rather than accepting the implementer's word — for
Tier 1 that is the entire deliverable, and an over-claiming matrix would have been worse than the
drift it replaced.

### Tier 2 — close the Phase 3 measured-gap tail

Every token in [`measured-gap.json`](measured-gap.json) is now actioned: three implemented, nine
accepted-and-ignored with the reason at the call site.

| Change | Commit |
|---|---|
| `D3DRS_FILLMODE`, value-sensitive: `SOLID` accepted exactly (it is what the backend draws), `WIREFRAME`/`POINT` still reported because GLES3 cannot express them. New `accepted_states_smoke` asserting both halves | `af300dc` |
| `D3DTSS_MAXANISOTROPY` via `EXT_texture_filter_anisotropic`, clamped to the device limit, cached, zero GL cost when absent; `coverage_smoke`'s stage-state probe re-pointed to `D3DTSS_MAXMIPLEVEL` | `b2f06f0` |
| `D3DRS_SPECULARMATERIALSOURCE`: `MATERIAL`/`COLOR1` handled; `COLOR2` still reported, because `D3DFVF_SPECULAR` is not uploaded as an attribute | `89d4296` |
| The nine documented no-ops: `PATCHSEGMENTS`, `SOFTWAREVERTEXPROCESSING`, `RANGEFOGENABLE`, six `D3DTSS_BUMPENV*` | `83cbddb` |

### Tier 3 — instrument the two questions the measurement could not answer

| Change | Commit |
|---|---|
| Vertex blending (`D3DFVF_XYZB1-5`) gains a coverage family of its own, keyed on the position mask so the key space stays at five values. **Still unimplemented — now measurable** | `c5caa08` |
| Strengthened `vertexblend_smoke` to assert distinct-token *attribution*, not just occurrence count | `58c775a` |
| Fog-mode *transitions* emit `d3d8.fogmode.{table,vertex}.<hex>` telemetry regardless of value, so "fog unused" stops being unfalsifiable. Not a coverage counter — nothing here falls back | `5421c6c` |

### Tier 4 — determinism harness (Phase 4's last item)

| Change | Commit |
|---|---|
| `frame_digest.h` (chained FNV-1a over a full `glReadPixels`), `determinism_smoke` (same sequence twice in-process), `scripts/determinism.mjs` (fresh browser *context* per run), wired into `ci.sh` | `187f9ab` |
| Corrected the smoke's comment to describe what it actually digests | `a36ec6c` |

Digest stable at `70f71745` across three fresh contexts, and unchanged after Tier 2 altered
render-state handling — correct, since the harness only clears, presents and reads back.

### Capstone

| Change | Commit |
|---|---|
| Regenerated `CONFORMANCE.md` and `llms-full.txt`; closed Phase 3's tail and Phase 4 in `ROADMAP.md`; documented the accept-without-acting rule and the determinism seam in `SDK_REFERENCE.md`; added a disposition note to `measured-gap.json` without touching a single measurement; fixed the ROADMAP's stale "31 smokes" to a counted 35 | `f26f203` |

**Phase 4 is complete.** The only remaining non-parked item in the roadmap is compatlib's
grow-on-demand Tier 3 (D3DX textures/shaders) and Tier 4 (sockets/COM/VFW).

## What this plan deliberately did not do

Stated so none of it reads as finished:

1. **`D3DFVF_SPECULAR` attribute upload.** The render state is handled but the specular vertex
   colour is still dropped; `D3DMCS_COLOR2` reports rather than works. Needs a new shader variant
   and a smoke that can distinguish a specular vertex colour from a material one.
2. **Vertex blending itself.** Instrumented, not implemented.
3. **A re-capture against the changed SDK.** Every disposition in `measured-gap.json` was actioned,
   so a fresh three-scenario capture would show a different, smaller gap. The numbers in that file
   are the 2026-07-31 measurement and were deliberately left untouched.
4. **The skirmish scenario's provenance asterisk.** Its numbers were transcribed from a prior run's
   stdout rather than captured live — a provenance caveat, not a wrong number.
5. **The parked and grow-on-demand items.** Phase 5 (WebGPU), Phase 6 (second game), compatlib
   Tiers 3-4, plus local-viewer lighting, >8 lights, EXP/EXP2 fog, and cube/volume/render-target
   surfaces. Each phase's unpark condition is in `ROADMAP.md`.

## Lessons

Stable IDs for citing from later sessions. `CDI` = close-the-docs-items.

### Documentation that lies

**CDI-1 — "Generated" is not the same as "cannot drift".** `CONFORMANCE.md` is produced by a script,
which bought false confidence: half its content is a hand-written array *inside* that script, so
regenerating re-emitted stale rows faithfully. Separate probed from curated content visibly in the
artifact — this one does say which tables are probed, and that is what made the drift findable — and
re-verify the curated half against code rather than regenerating it.

**CDI-2 — Checkbox state is the least reliable progress signal in a repo.** Two plans were 100%
unchecked and 100% implemented. Verify completion by artifact — the file exists, the symbol is
present, the smoke is in the CI list — never by checkbox. When writing the word "verified", put the
evidence in the same commit.

**CDI-3 — A comment that contradicts the code is a defect, and the bar applies to code you just
wrote.** Three findings this session were comments falsified by the very change that introduced
them: `coverage_smoke`'s "solid" label on `D3DFILL_POINT`, `determinism_smoke`'s claim to digest
"depth state, blend state, and a transform" when it digests four clears, and
`SetTextureStageState`'s "Anisotropy and LOD bias arrive here" after anisotropy got its own branch.
All three were fixed rather than deferred — deferring them in a plan whose thesis is that docs must
not overstate the runtime would have been self-refuting.

**CDI-4 — A number that has drifted once will drift again.** `ROADMAP.md` claimed "31 smokes"; the
true figure was 35. The git log shows `f9a1781 docs: fix smoke count in ROADMAP.md CI item (31, not
~30)` — already fixed once, deliberately. Counted quantities in prose are liabilities; either
generate them or expect to re-check them every time the thing they count changes.

**CDI-5 — Auditing docs means confirming what is right as carefully as correcting what is wrong.**
The same ROADMAP sentence that had the wrong smoke count also claimed the suite covers "every
executable target except `conformance`, `minigame`, and `spin_demo`" — still exactly true, verified
by enumerating all 38 targets. Fixing the number and leaving the clause alone required checking both.

### Plans written without compiling them

**CDI-6 — Plan code that has never been compiled is a hypothesis, not a specification.** Three of
ten tasks hit a defect in the plan's own code: `TRUE`/`FALSE` used where this SDK's `d3d8.h` does not
define them; `import { chromium } from 'playwright'` from a directory with no resolvable
`node_modules` (the repo's own scripts use `createRequire` against `web-runtime/package.json`); and a
test assertion that was arithmetically unsatisfiable. Write plans with complete code anyway — each
defect surfaced in seconds as a compile error or a red test instead of as a design argument — but
expect the implementer to act as the compiler, and say explicitly that stopping to ask beats forcing
a pass.

**CDI-7 — A caveat aimed at the wrong dependency direction is worse than no caveat.** The plan's
Task 6 carried a note about exactly the arithmetic that broke: it reassured that the *earlier*
assertions were unaffected by the new block (true) while missing that the *new* block was affected by
them (the bug). A caveat reads as "this hazard was already considered" and suppresses the scrutiny
that would have caught it.

**CDI-8 — Cumulative test state is fine within one task and treacherous across several.** Four
independently-authored blocks accumulated assertions against a single snapshot taken at the top of
the file; the fourth inherited a counter that had already moved twice. Each block should take its own
reading. The same brief got this right one paragraph later with a local `beforeOp` snapshot — the bug
was reusing the global.

**CDI-9 — Line numbers in a plan are stale the moment the first task lands.** Three tasks found the
brief's cited lines had drifted as earlier commits grew the same files. Each matched on content
instead, which is the right response; plans should cite anchors (a symbol, a neighbouring `case`)
rather than line numbers wherever possible.

### Coverage instrumentation as a work list

**CDI-10 — A counter that mixes "unimplemented" with "deliberately ignored" cannot be ranked.**
`D3DRS_PATCHSEGMENTS` — a float bit-pattern the engine smuggles through a render state, not a
rendering request at all — was the most-hit unhandled token in every scenario of a real capture at
40,138 hits, outranking every genuine finding. Silencing decisions, with the reason at the call site,
is what makes frequency data mean anything.

**CDI-11 — Silencing a token is only safe if the signal survives elsewhere.** The six
`D3DTSS_BUMPENV*` states went quiet, but `D3DTOP_BUMPENVMAP` — their only consumer, and the thing
that would make them live — stays unimplemented and still reported. The smoke asserts this with its
own fresh snapshot. Had the states been silenced *and* the op quietly become "supported", the change
would have destroyed the very signal it claimed to preserve.

**CDI-12 — Keep usage signals out of gap counters.** Fog-mode transitions emit plain telemetry, never
a coverage counter, and never touch `fallbacks_taken` — because nothing about them is unimplemented.
Four tasks were spent sharpening what a coverage counter means; routing a usage signal through one
would have undone that in a fifth.

**CDI-13 — Some gaps are unmeasurable, and "no hits" then proves nothing.** Vertex blending rides the
`D3DFVF_XYZB1-5` position bits, not any token the coverage layer watches, so its absence from three
real captures was not evidence of anything. Before reading a zero as a negative result, confirm an
instrument exists that could have produced a non-zero.

**CDI-14 — Do not overload a legal value as a "never set" sentinel.** The fog-transition mirror uses
`0xFFFFFFFF` for "never written", because `0` is a legal `D3DFOGMODE`; overloading it would make a
genuine first write of mode 0 invisible. The telemetry code had already been bitten by this exact
shape once — a timestamp cursor where `0` meant both "never stamped" and "the first millisecond" —
and the fix pattern propagated correctly to new code, which is what a well-documented past defect
buys you.

### Failure modes the test suite cannot see

**CDI-15 — A shader-cache key field that is not in the hash does not exist.** Adding
`specFromVertex` to the key struct without adding it to the packing expression would have collided
the material-sourced and vertex-sourced programs on one cache entry, serving whichever compiled first
to both. The symptom — "the render state does nothing" — is indistinguishable from never implementing
it, and `specular_smoke` only walks the material path, so the suite stays green either way. Only a
reading review catches this, which is why the step was flagged load-bearing in the plan and the
reviewer was told to enumerate the taken bits itself rather than trust the claim that one was free.

**CDI-16 — A test fixture can stop testing without failing.** `coverage_smoke` deliberately probes
tokens that are unimplemented, so implementing one forces the probe to move. The dangerous outcome is
not a red suite — it is a probe that has become handled *with its expected value adjusted to match*:
green, and proving nothing. Re-pointing required proving the new token is genuinely unhandled and is
not in any documented-no-op group, so it stays that way.

**CDI-17 — A counter cannot express attribution; pair it with something that can.** The vertex-blend
smoke first asserted only an occurrence count, which increments per call regardless of which
telemetry key the occurrence lands under — so it passed identically whether three calls produced two
distinct keys (correct, keyed on the position mask) or three (wrong, keyed on the whole FVF).
Bounding that key space is the instrument's whole purpose. The fix asserts both: occurrences via the
counter, distinct tokens via the unhandled callback, which fires once per distinct token.

**CDI-18 — Mutation-test the assertion, then verify the mutation was reverted.** The implementer
temporarily made the device key on the full FVF, watched the strengthened smoke fail with its own
error message, and reverted — answering a question a passing test never can. The re-review's job then
included confirming the revert by diffing the implementation files across the fix range. A
half-applied mutation experiment would ship the bug next to the test that detects it.

**CDI-19 — When a fix is provably inert, prove the inertness instead of re-measuring.** The
`determinism_smoke` comment fix was verified as six insertions inside one comment block with no
executable line touched, so the digest was unaffected *by construction*. Stronger than a passing
re-run, and cheaper.

**CDI-20 — An idempotent generator is verifiable by re-running it.** The capstone review confirmed
`CONFORMANCE.md` was regenerated rather than hand-edited by running `node scripts/conformance.mjs`
against the committed tree and observing a zero diff. Any generated artifact should support that
check; it converts "was this hand-edited?" from a judgement into a command.

### Parallel execution across worktrees

**CDI-21 — Whole-tree guardrails and worktrees are natural enemies.** Creating the worktrees
immediately broke `scripts/check.sh`: guardrail 2 asserts every `*.wasm` under a `vendor/` directory
is tracked, and `git ls-files` run from the main checkout cannot match a path inside a *linked*
worktree, so each worktree's vendored brotli decoder read as untracked. Fixed by pruning
`.worktrees` from the scan (`60ccefc`).

**CDI-22 — Worktrees do not inherit gitignored state.** No `build/` (each track pays a cold
Emscripten build) and no `web-runtime/node_modules`, which means no `playwright` and therefore no GPU
suite at all. Install per worktree up front rather than letting each agent rediscover it.

**CDI-23 — A self-reported recovery is a claim like any other.** One agent edited the *main* checkout
instead of its worktree, caught itself, and reverted. Verified independently rather than trusted:
clean tree, zero diff against HEAD, neither target file carrying the new text, roadmap untouched. It
had in fact recovered completely — the check is what makes that a fact rather than a hope.

**CDI-24 — Forecast merges read-only when you are not the only writer.**
`git merge-tree --write-tree` computes a merge result without touching the working tree or index,
which is the right tool while other agents hold live checkouts sharing one object store. A
`git merge --no-commit` plus `--abort` mutates the index for the duration.

**CDI-25 — Serialise tracks that edit inside the same syntactic construct, even when git would merge
them cleanly.** Two tracks both needed to edit one `switch` in `device.cpp` — one adding five `case`s,
the other rewriting an existing one. Git resolves that textually; the failure mode is a duplicated or
dropped `case` label, which compiles and which no smoke would catch. Deferring the second track until
the first merged cost about two tasks of wall-clock and one cold build, and removed the risk. The
tracks that *did* run in parallel shared only append-at-the-end files, where conflicts are trivial
and visible — and in the event `merge-tree` predicted zero conflicts and all four merges were clean.

### The runtime itself

**CDI-26 — "Implement this token" is sometimes the wrong frame; ask which *values* are expressible.**
GLES3 dropped `glPolygonMode`, so `D3DRS_FILLMODE` splits: `SOLID` is exactly what the backend already
draws and can be accepted precisely, while `WIREFRAME`/`POINT` cannot be expressed and must keep
reporting. The conformance matrix now reports fill mode by value, mirroring how fog already reported
`FOGTABLEMODE(LINEAR)` handled beside `(EXP)` fallback. The roadmap's own instruction to "implement
`D3DRS_FILLMODE`" and move the probe was wrong on both counts.

**CDI-27 — WebGL extensions must be enabled on the context before their tokens do anything.** A bare
`glTexParameterf(GL_TEXTURE_2D, 0x84FE, …)` silently no-ops without
`emscripten_webgl_enable_extension`. The absent-extension case must also be distinguishable from "1×
was requested" — `aniso_limit()` returns 0 for "not possible", never 1 — and the result must be
cached, since `apply_sampler` runs per draw per texture stage.

### What the final whole-branch review caught — and why it matters most

Every one of the ten tasks passed its own scoped review. The whole-branch review then returned
**NEEDS WORK**: 1 Critical, 5 Important, 9 Minor. The runtime changes were ruled sound; **every
defect was in what the branch claimed** — the one thing this plan said it would not get wrong.
Fixed in `ac739b9`, `8283fed`, `23eed3f`, `bd4d2bc`, `2b1c61a`, `a71e6f3`; the scoped re-review then
verdicted all fifteen ADDRESSED with no test weakened.

The Critical one is the most useful thing in this document. Three places — `ROADMAP.md`,
`determinism_smoke.cpp`'s header, and `determinism.mjs`'s header, all verbatim from the plan — claimed
the fresh-context runs catch "iteration-order-dependent shader-cache keys". They cannot: the digested
sequence has no draw call, so `ff::build`/`hash_key` is never reached and no program is ever cached.
The file even contradicted itself, the Task 9 comment fix correctly stating "does not exercise… any
draw call" seven lines below the header making the claim. A maintainer could have added an
order-dependent key field — CDI-15's exact failure mode — seen the determinism stage green, and
trusted a sentence that was never true.

Three of the Important findings were this document's own lessons recurring inside the branch that
wrote them, which is why the next two lessons exist.

**CDI-28 — Writing a lesson down does not apply it; sweep the class, not the instance.** CDI-4 (a
counted quantity in prose is a liability) was fixed in `ROADMAP.md` and missed in `README.md` ("16
smokes") and `llms.txt` ("~20"), leaving the repo carrying three different figures for 35. CDI-16 (a
fixture can stop testing without failing) recurred inside the conformance program itself: after every
measured token was implemented, the `textureOps` and `formats` probe tables contained no unimplemented
token at all, so neither could ever emit a ⚠️ row again while displaying "6/6" and "5/5" — two of
three probed tables lost their falsifiability *by succeeding*. CDI-17 (a counter cannot express what
it does not include) recurred as cross-task drift: `total()` claims to sum "every coverage counter",
and a later task appended `unhandled_vertex_formats` without adding it, so a leak into that counter
would pass every "must not move" assertion in the file. Each is invisible from inside the task that
caused it; only a whole-branch pass finds them.

**CDI-29 — A sentence can be entirely true and still mislead; test the reading, not the clauses.**
The anisotropy conformance row read "a textured draw with the state set is pixel-verified" and was
graded ✅ yes. Every clause is true. But the pixel assertion validates the MODULATE colour product,
not anisotropic sampling — and under SwiftShader `aniso_limit()` may return 0 and skip the
`glTexParameterf` entirely while producing byte-identical pixels, so the test cannot distinguish
"programmed correctly" from "not programmed at all". It survived a fix wave and a re-review as an
out-of-scope note precisely because nothing in it was incorrect. Demoted to `partial` in `a71e6f3`
with the limits stated. The check that catches this class is not "is each clause accurate?" but
"what will a reader conclude, and could the cited test detect that conclusion being false?"

## Repo state at close

Branch `main`, clean, `scripts/ci.sh` → `ALL GREEN`: guardrails, pinned Emscripten 6.0.2, packer
self-test, web-runtime suite, **35** headless GPU smokes, and the determinism harness (digest
`70f71745` stable across three fresh browser contexts). Conformance: render states 25/29, texture
ops 6/7, formats 5/6 — the sub-totals are deliberate, since each probed table keeps at least one
genuinely-unimplemented token so it remains able to report a failure (see CDI-28).

A specific commit hash is *not* recorded here on purpose: it was, and went stale within the hour as
the final-review fixes landed — the same class of defect as the "31 smokes" this branch fixed
(CDI-4). `git log` is the authority for where the branch head is.
