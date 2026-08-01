# Close the Remaining `docs/` Items Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire every open item the `docs/` tree still claims — the stale half of the
conformance matrix, the Phase 3 measured-gap tail (3 implements + 9 documented no-ops), the
two instrumentation blind spots the measurement itself flags, and Phase 4's determinism
harness — so that after this plan, `docs/` describes the code and lists nothing outstanding
except the explicitly parked phases.

**Architecture:** Four independent tiers, in this order. **Tier 1** is documentation-only:
the curated feature table inside `scripts/conformance.mjs` has drifted from the code, so
`CONFORMANCE.md` regenerates stale rows verbatim — fix the source array, regenerate. **Tier 2**
closes the measured D3D8 gap in `runtime/d3d8webgl/device.cpp`, split into states we can
genuinely act on and states we deliberately accept-and-ignore with a written reason. **Tier 3**
adds two instruments so the *next* capture can answer questions this one structurally could not
(vertex blending, and whether fog is used at all). **Tier 4** adds the determinism harness stub
Phase 4 still lists. Every runtime change lands with a headless pixel smoke, per the SDK rule.

**Tech Stack:** C++17, Emscripten 6.0.2, WebGL2/GLES3, CMake + Ninja, headless Chromium
(SwiftShader) smokes driven by `web-runtime/test/phase2.gpu.test.mjs`, Node 20 harness scripts.

## Global Constraints

- Author every commit as `Fadi Labib <github@fadilabib.com>`. Never add an AI co-author line.
- SPDX header `// SPDX-License-Identifier: GPL-3.0-only` on every new file (enforced by `scripts/check.sh`).
- `bash scripts/ci.sh` must print `ALL GREEN` before any task is considered done.
- Emscripten pinned to 6.0.2 (`.emscripten-version`); run `source ~/emsdk/emsdk_env.sh` first.
- Clean-room: cross-check behaviour against DXVK/Wine but **never paste** (LGPL). Reference copy:
  `~/projects/personal/Generals-Mac-iOS-iPad/references/fadi-labib-dxvk/`.
- Headless smokes render to a **4×4** canvas — they cannot catch viewport or full-canvas bugs.
- `runtime/include/dx8wasm/contract.h` is the ABI: only ever **append** fields to
  `dx8wasm_coverage`, never reorder or insert.
- The harness compares read-back pixels with a **±2 tolerance**. Never encode a hash or an
  identity-critical value in the reported pixel tuple; assert it in C++ and report a sentinel.
- A coverage counter means "unimplemented, fell back". A deliberate no-op is a *decision*, not a
  gap — it must stop counting and gain a code comment saying why. Do not blur the two.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `scripts/conformance.mjs` | curated feature table — the drifted half of `CONFORMANCE.md` | 1 |
| `docs/CONFORMANCE.md` | regenerated output; never hand-edited | 1, 10 |
| `docs/superpowers/plans/2026-07-29-honest-stubs.md` | mark done (implemented, boxes never ticked) | 2 |
| `docs/superpowers/plans/2026-07-12-texture-surfaces.md` | mark done (same) | 2 |
| `docs/ROADMAP.md` | phase status; the measured-gap tail bullet | 2, 10 |
| `runtime/d3d8/d3d8.h` | clean-room header: `D3DFILLMODE`, the two new `D3DTSS_*`, the no-op token values | 3, 4, 6 |
| `runtime/d3d8webgl/device.cpp` | `SetRenderState` / `SetTextureStageState` / sampler / FVF paths | 3, 4, 5, 6, 7, 8 |
| `runtime/test/accepted_states_smoke.cpp` | **new** — states accepted without acting produce *no* coverage hit | 3, 4, 6 |
| `runtime/test/coverage_smoke.cpp` | re-point the stage-state probe off the now-implemented token | 4 |
| `runtime/test/conformance.cpp` | probe table: split `FILLMODE` by value, add the new tokens | 3, 4, 5 |
| `runtime/coverage/coverage.{h,cpp}` | new `unhandled_vertex_format` sink + its telemetry family | 7 |
| `runtime/include/dx8wasm/contract.h` | appended `unhandled_vertex_formats` counter | 7 |
| `runtime/test/vertexblend_smoke.cpp` | **new** — an `XYZB*` FVF is counted, `XYZ` is not | 7 |
| `runtime/test/fogmode_smoke.cpp` | **new** — every fog-mode *transition* emits one telemetry record | 8 |
| `runtime/test/frame_digest.h` | **new** — inline FNV-1a over `glReadPixels`, reusable by any smoke | 9 |
| `runtime/test/determinism_smoke.cpp` | **new** — same scene twice in-process, digests must match | 9 |
| `scripts/determinism.mjs` | **new** — runs the smoke across fresh page loads, digests must match | 9 |
| `scripts/ci.sh` | run the determinism harness | 9 |
| `CMakeLists.txt` | register the four new smoke targets | 3, 7, 8, 9 |
| `web-runtime/test/phase2.gpu.test.mjs` | expected sentinels for the four new smokes | 3, 7, 8, 9 |
| `docs/SDK_REFERENCE.md` | document the accept-without-acting rule + the determinism seam | 10 |
| `llms-full.txt` | regenerated from the docs by `scripts/gen-llms-full.sh` | 10 |

---

# Tier 1 — make the docs describe the code

### Task 1: The curated feature table stops lying

`docs/CONFORMANCE.md` is half machine-probed (the three token tables — the conformance program
sets each token on a live device and reads its own coverage counters, so those cannot drift) and
half hand-curated (the `features` array in `scripts/conformance.mjs:52-71`). The curated half has
drifted: it still says the second texture stage is missing and textures are "level 0, nearest,
clamp", both of which landed months ago. Because the file is *generated*, regenerating it
faithfully re-emits the stale rows — the "generated" label is buying false confidence exactly
where drift is possible. Eleven of the 31 smokes also verify features that have no row at all.

**Files:**
- Modify: `scripts/conformance.mjs:52-71` (the `features` array)
- Modify: `docs/CONFORMANCE.md` (regenerated, not hand-edited)

**Interfaces:**
- Consumes: nothing.
- Produces: nothing consumed by later tasks. Task 10 regenerates this file again after Tiers 2–4.

- [ ] **Step 1: Confirm the drift before changing anything**

```bash
cd ~/projects/personal/dx8wasm
grep -n "texture1" runtime/d3d8webgl/device.cpp | head -3
grep -n "Chain the enabled stages" runtime/graphics-ff/ff_shader.cpp
grep -n "D3DTSS_MAGFILTER\|D3DTSS_MIPFILTER\|D3DTSS_ADDRESSU" runtime/d3d8webgl/device.cpp
```
Expected: `device.cpp:453` declares a stage-1 texture slot, `device.cpp:595` routes `SetTexture`
by stage, `ff_shader.cpp:241` chains stage 0 into stage 1, and the sampler filter/address states
are handled. All three contradict rows currently in the table. If any of these greps come back
empty, **stop** — the table may be right and this plan's premise wrong.

- [ ] **Step 2: Replace the two wrong rows**

In `scripts/conformance.mjs`, replace this line:

```js
  ['Textures: A8R8G8B8 + LockRect', 'level 0, nearest, clamp', 'yes', 'draw_tex_smoke'],
```

with:

```js
  ['Textures: LockRect upload', 'full mip chain; per-stage filter + address state honored', 'yes', 'draw_tex_smoke'],
```

and replace this line:

```js
  ['Second texture stage', 'multi-texture (base + lightmap/detail)', 'no', '—'],
```

with:

```js
  ['Second texture stage', 'multi-texture (base + lightmap/detail); stages 0-1 chained in the combiner', 'yes', 'lit_tex_smoke / combiner_smoke'],
```

- [ ] **Step 3: Add rows for the smokes the table never mentions**

Still in the `features` array, insert these rows immediately after the
`'Texture combiners (stage 0)'` row. Each names a smoke that already exists and passes:

```js
  ['Texture formats: 16-bit + DXT1', 'A4R4G4B4/R5G6B5/A8/L8 decode + S3TC block upload', 'yes', 'texfmt_smoke / dxt_smoke'],
  ['Surfaces', 'GetSurfaceLevel, CreateImageSurface, CopyRects, UpdateTexture', 'yes', 'surface_smoke'],
  ['Material colour source', 'D3DRS_{DIFFUSE,AMBIENT,EMISSIVE}MATERIALSOURCE + D3DRS_COLORVERTEX', 'yes', 'matsource_smoke'],
  ['World-space normals', 'normal matrix follows a non-identity world transform', 'yes', 'normal_smoke'],
  ['Honest stubs', 'unimplemented entry points refuse; caps derive from the implementation', 'yes', 'honest_stubs_smoke / caps_query_smoke'],
  ['Telemetry ring', 'NDJSON spans + coalesced counters, drop-accounted', 'yes', 'telemetry_smoke'],
  ['compatlib Tiers 0-3', 'timing, file/dir/memory, module/thread/registry, D3DX math', 'yes', 'compat_smoke / compat_file_smoke / compat_sys_smoke / compat_d3dx_smoke'],
```

- [ ] **Step 4: Sharpen the one row that is right for the wrong reason**

The `FVF: SPECULAR` row reads as "not implemented at all". The stride *is* honored
(`device.cpp:889` advances the offset past a specular colour so texcoord offsets stay correct) —
what is missing is consuming the colour. Replace:

```js
  ['FVF: SPECULAR, multi-texcoord', 'extra colour / texcoord sets', 'no', '—'],
```

with:

```js
  ['FVF: SPECULAR', 'stride honored so uv offsets stay correct; the colour itself is dropped', 'partial', 'draw_smoke'],
  ['FVF: multi-texcoord (TEX2)', 'second uv set feeds stage 1 (attribute location 4)', 'yes', 'lit_tex_smoke'],
```

Note `'partial'` renders as 🟡 — the `featMark` map already supports it
(`scripts/conformance.mjs`, `const featMark = { yes: '✅', partial: '🟡', no: '❌' }`).

- [ ] **Step 5: Regenerate and read the diff**

```bash
cd ~/projects/personal/dx8wasm && source ~/emsdk/emsdk_env.sh && node scripts/conformance.mjs
git diff --stat docs/CONFORMANCE.md
git diff docs/CONFORMANCE.md | head -60
```
Expected: `docs/CONFORMANCE.md` rewritten; the diff touches only the "Feature coverage" table
(the three probed token tables and the measured-gap section must be byte-identical — if a probed
table changed, something else moved and you should find out what before committing).

- [ ] **Step 6: Full CI**

```bash
bash scripts/ci.sh
```
Expected: `ALL GREEN`.

- [ ] **Step 7: Commit**

```bash
git add scripts/conformance.mjs docs/CONFORMANCE.md
git commit -m "conformance: the curated feature table describes what the code does"
```

---

### Task 2: Retire the two finished plans and the stale roadmap tail

Both plan files under `docs/superpowers/plans/` have every checkbox unticked and every task
implemented. `honest-stubs.md` asks for `format_support.h`, `caps_query_smoke.cpp`,
`honest_stubs_smoke.cpp`, a stencil-reporting `dx8wasm_has_cap` and an SDK_REFERENCE section —
all present. `texture-surfaces.md` asks for `Surface8`, `CopyRects`, `UpdateTexture` and
`surface_smoke` — all present. A fresh session reading either file would redo landed work.

**Files:**
- Modify: `docs/superpowers/plans/2026-07-29-honest-stubs.md` (header only)
- Modify: `docs/superpowers/plans/2026-07-12-texture-surfaces.md` (header only)
- Modify: `docs/ROADMAP.md`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing.

- [ ] **Step 1: Verify both plans really are done**

```bash
cd ~/projects/personal/dx8wasm
ls runtime/d3d8webgl/format_support.h runtime/test/caps_query_smoke.cpp \
   runtime/test/honest_stubs_smoke.cpp runtime/test/surface_smoke.cpp
grep -n "DX8WASM_CAP_STENCIL: return 1" runtime/runtime.cpp
grep -n "Stubs fail loudly" docs/SDK_REFERENCE.md
grep -n "HRESULT CopyRects\|HRESULT UpdateTexture" runtime/d3d8webgl/device.cpp
```
Expected: all four files exist, and the three greps hit (`runtime.cpp:38`,
`SDK_REFERENCE.md:29`, `device.cpp:977` and `:1001`). Every one of these must succeed before
marking anything done — that is the whole point of this step.

- [ ] **Step 2: Stamp the honest-stubs plan as landed**

Insert immediately after the `> **For agentic workers:** …` blockquote in
`docs/superpowers/plans/2026-07-29-honest-stubs.md`:

```markdown
> **STATUS: COMPLETE (verified 2026-08-01).** Every task below is implemented; the checkboxes
> were never ticked, so ignore them. Verified by inspection, not by checkbox: `format_support.h`,
> `caps_query_smoke.cpp` and `honest_stubs_smoke.cpp` exist and are in the CI smoke list;
> `dx8wasm_has_cap` reports stencil (`runtime/runtime.cpp:38`); `docs/SDK_REFERENCE.md` carries
> the "Stubs fail loudly" contract. Kept for its rationale — the *why* behind each demotion is
> still the reference for future stubs — not as an open work item.
```

- [ ] **Step 3: Stamp the texture-surfaces plan as landed**

Insert immediately after the opening blockquote in
`docs/superpowers/plans/2026-07-12-texture-surfaces.md`:

```markdown
> **STATUS: COMPLETE (verified 2026-08-01).** All four tasks are implemented — `Surface8`,
> `GetSurfaceLevel`, `CreateImageSurface`, `CopyRects` (`runtime/d3d8webgl/device.cpp:977`) and
> `UpdateTexture` (`:1001`), covered by `surface_smoke`. The plan's "DXT defers" note is also
> closed: DXT1 landed and is verified by `dxt_smoke`. Kept for rationale, not as open work.
```

- [ ] **Step 4: Point the roadmap's Phase 3 tail at this plan**

In `docs/ROADMAP.md`, replace the line that currently reads:

```markdown
- Remaining fixed-function work, now **measured** rather than guessed — see
```

with:

```markdown
- Remaining fixed-function work, now **measured** rather than guessed, is being closed by
  [`superpowers/plans/2026-08-01-close-the-remaining-docs-items.md`](superpowers/plans/2026-08-01-close-the-remaining-docs-items.md)
  (Tier 2 implements + documents the no-ops; Tier 3 adds the two missing instruments) — see
```

- [ ] **Step 5: Full CI**

```bash
bash scripts/ci.sh
```
Expected: `ALL GREEN` (docs-only change, but `check.sh` polices authorship and headers).

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/plans/2026-07-29-honest-stubs.md \
        docs/superpowers/plans/2026-07-12-texture-surfaces.md docs/ROADMAP.md
git commit -m "docs: mark the two landed plans complete and link the measured-gap tail"
```

---

# Tier 2 — close the measured D3D8 gap

### Task 3: `D3DRS_FILLMODE` — accept SOLID, keep reporting the modes GLES cannot express

19,392 hits across the three captures, second only to `PATCHSEGMENTS`. The roadmap says
"implement", and warns that `coverage_smoke` uses this token as its stable-unimplemented probe so
the probe must move in the same change. **Both of those need correcting, and this task is where
that happens:** GLES3/WebGL2 has no `glPolygonMode`, so wireframe and point fill are genuinely
not expressible — what is implementable is *`D3DFILL_SOLID`*, which is already what the backend
draws. So the honest change is a value-sensitive one: `SOLID` becomes a documented accept
(no counter), `WIREFRAME`/`POINT` keep falling back and keep counting. `coverage_smoke` probes
with value 2 (`WIREFRAME`) and 1 (`POINT`), so its counters are unchanged and the probe does
**not** need to move. Verify that claim in Step 6 rather than trusting it.

**Files:**
- Modify: `runtime/d3d8/d3d8.h` (add `enum D3DFILLMODE`)
- Modify: `runtime/d3d8webgl/device.cpp` (`SetRenderState`, ~line 645)
- Create: `runtime/test/accepted_states_smoke.cpp`
- Modify: `runtime/test/conformance.cpp` (split the `FILLMODE` probe by value)
- Modify: `CMakeLists.txt`, `web-runtime/test/phase2.gpu.test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: `runtime/test/accepted_states_smoke.cpp`, extended by Tasks 4 and 6. Its contract:
  take a coverage snapshot, set states the backend accepts without acting, assert **zero** delta
  across all four counters, then set one state it does not implement and assert a delta of
  exactly 1. Reports the sentinel `[1, 0, 0, 255]`.

- [ ] **Step 1: Write the failing test**

Create `runtime/test/accepted_states_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// States this backend deliberately ACCEPTS WITHOUT ACTING must not bump a coverage counter.
// The counters mean "unimplemented, fell back"; a decision to no-op is not a gap, and letting
// the two share a counter is how 40k/frame of D3DRS_PATCHSEGMENTS came to dominate a capture
// that was supposed to rank real work. The mirror assertion matters just as much: a state the
// backend truly cannot express must STILL count, or this smoke would pass by silencing
// everything. Reports [1,0,0,255] when both halves hold.
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

// Sum of every coverage counter. Any single token leaking into any counter moves this.
static uint32_t total() {
  dx8wasm_coverage c{};
  dx8wasm_get_coverage(&c);
  return c.unhandled_render_states + c.unhandled_texture_stage_ops +
         c.unhandled_formats + c.unhandled_texture_stage_states;
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  // --- Accepted without acting: no counter may move. ---
  const uint32_t before = total();
  // D3DFILL_SOLID is what this backend already draws, so accepting it is exact, not a fallback.
  dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
  if (total() != before) { report_error("D3DFILL_SOLID was counted as unhandled"); return 1; }

  // --- Genuinely unimplemented: the counter MUST move. ---
  // GLES3 has no glPolygonMode, so wireframe cannot be expressed and must keep reporting.
  dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
  if (total() != before + 1) { report_error("D3DFILL_WIREFRAME stopped being reported"); return 1; }

  // Rendering must still work after both.
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
```

- [ ] **Step 2: Register the smoke**

In `CMakeLists.txt`, after the `coverage_smoke` block (line ~128), add:

```cmake
add_executable(accepted_states_smoke runtime/test/accepted_states_smoke.cpp)
target_include_directories(accepted_states_smoke PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(accepted_states_smoke PRIVATE dx8_d3d8webgl)
target_link_options(accepted_states_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(accepted_states_smoke PROPERTIES SUFFIX ".js")
```

In `web-runtime/test/phase2.gpu.test.mjs`, add to `SMOKES` immediately after the
`['coverage_smoke', [5, 1, 4, 1]],` entry:

```js
  ['accepted_states_smoke', [1, 0, 0, 255]],   // accepted-without-acting states don't count; unexpressible ones still do
```

- [ ] **Step 3: Run it and watch it fail**

```bash
source ~/emsdk/emsdk_env.sh && cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `accepted_states_smoke: D3DFILL_SOLID was counted as unhandled` (today every
`FILLMODE` value falls through to `default:`). It may instead fail to compile on
`D3DFILL_SOLID` — that is the same failure, one step earlier; continue to Step 4 either way.

- [ ] **Step 4: Declare the fill modes in the clean-room header**

In `runtime/d3d8/d3d8.h`, immediately after the closing `};` of `enum D3DRENDERSTATETYPE`
(the one ending `D3DRS_COLORWRITEENABLE = 168`), add:

```cpp
// D3DRS_FILLMODE values. Only SOLID is expressible: GLES3/WebGL2 has no glPolygonMode, so
// WIREFRAME and POINT stay reported through the coverage layer rather than silently ignored.
enum D3DFILLMODE { D3DFILL_POINT = 1, D3DFILL_WIREFRAME = 2, D3DFILL_SOLID = 3 };
```

- [ ] **Step 5: Handle the state by value**

In `runtime/d3d8webgl/device.cpp`, inside `SetRenderState`'s `switch`, immediately after the
`case D3DRS_SHADEMODE:` line, add:

```cpp
      case D3DRS_FILLMODE:
        // Value-sensitive on purpose. SOLID is exactly what this backend draws, so accepting it
        // is not a fallback and must not count — it was 19,392 hits of pure noise in the
        // Generals capture (docs/measured-gap.json). WIREFRAME/POINT genuinely cannot be
        // expressed: GLES3 dropped glPolygonMode, so they keep reporting rather than pretending.
        if (Value != D3DFILL_SOLID) coverage::unhandled_render_state(State);
        break;
```

- [ ] **Step 6: Prove `coverage_smoke` is unaffected before running anything**

`coverage_smoke` sets `FILLMODE` to 2, 1, 2 — `WIREFRAME`, `POINT`, `WIREFRAME`. All three still
take the reporting branch, `note()` keys on the *state* token (8), not the value, so it still
sees 3 occurrences of 1 distinct token and one coalesced telemetry record with delta 3.

```bash
grep -n "SetRenderState(D3DRS_FILLMODE" runtime/test/coverage_smoke.cpp
```
Expected: exactly three lines, with values `2`, `1`, `2`. If any of them is `3`
(`D3DFILL_SOLID`), that call now stops counting and `coverage_smoke`'s expectations must be
updated in this commit — do not proceed until the greps agree with this paragraph.

- [ ] **Step 7: Split the conformance probe by value**

In `runtime/test/conformance.cpp`, in the `rs[]` table, replace:

```cpp
    {"D3DRS_FOGDENSITY", D3DRS_FOGDENSITY, 0}, {"D3DRS_FILLMODE", D3DRS_FILLMODE, 2},
```

with:

```cpp
    {"D3DRS_FOGDENSITY", D3DRS_FOGDENSITY, 0},
    {"D3DRS_FILLMODE(SOLID)", D3DRS_FILLMODE, D3DFILL_SOLID},
    {"D3DRS_FILLMODE(WIREFRAME)", D3DRS_FILLMODE, D3DFILL_WIREFRAME},
```

This mirrors how fog is already reported — `D3DRS_FOGTABLEMODE(LINEAR)` handled beside
`D3DRS_FOGTABLEMODE(EXP)` fallback — so the matrix says which *values* work, not just which
tokens are touched.

- [ ] **Step 8: Run the tests — they must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `accepted_states_smoke` reports `[1,0,0,255]`; `coverage_smoke` still `[5,1,4,1]`;
everything else unchanged.

- [ ] **Step 9: Full CI and commit**

```bash
bash scripts/ci.sh
git add runtime/d3d8/d3d8.h runtime/d3d8webgl/device.cpp runtime/test/accepted_states_smoke.cpp \
        runtime/test/conformance.cpp CMakeLists.txt web-runtime/test/phase2.gpu.test.mjs
git commit -m "d3d8webgl: accept D3DFILL_SOLID, keep reporting the fill modes GLES cannot express"
```

---

### Task 4: `D3DTSS_MAXANISOTROPY` — a real sampler parameter

A plain sampler setting, not a pipeline feature: it maps onto
`EXT_texture_filter_anisotropic`'s `TEXTURE_MAX_ANISOTROPY_EXT` where the extension exists and
clamps to 1 where it does not. Only 6 init-time hits, so this is a texture-quality win rather
than a hot-path fix — but this project has already shipped a bug where empty texture-filter caps
produced blocky textures, so the filtering path earns the attention.

Unlike Task 3, this one **does** break `coverage_smoke`: that smoke uses token 21
(`D3DTSS_MAXANISOTROPY`) as its stable-unimplemented *stage-state* probe. Re-point it to
`D3DTSS_MAXMIPLEVEL` (20) in the same commit — genuinely unimplemented (the backend always
uploads and samples the full chain the engine gives it) and not in Task 6's no-op group, so it
will not be silenced later either.

**Files:**
- Modify: `runtime/d3d8/d3d8.h` (add `D3DTSS_MAXMIPLEVEL = 20`, `D3DTSS_MAXANISOTROPY = 21`)
- Modify: `runtime/d3d8webgl/device.cpp` (`StageState`, `SetTextureStageState`, `apply_sampler`)
- Modify: `runtime/test/accepted_states_smoke.cpp` (from Task 3)
- Modify: `runtime/test/coverage_smoke.cpp` (re-point the probe)

**Interfaces:**
- Consumes: `accepted_states_smoke.cpp` from Task 3.
- Produces: `StageState::maxAniso` (a `uint32_t`, default 1); a file-static
  `aniso_limit() -> float` in `device.cpp` returning the device's max anisotropy, or `0` when the
  extension is unavailable.

- [ ] **Step 1: Write the failing test**

In `runtime/test/accepted_states_smoke.cpp`, immediately after the `D3DFILL_SOLID` assertion and
*before* the `D3DFILL_WIREFRAME` block, add:

```cpp
  // Anisotropy is a sampler parameter with a real GL mapping (EXT_texture_filter_anisotropic),
  // clamped to 1 when the extension is absent — either way it is handled, never a fallback.
  dev->SetTextureStageState(0, D3DTSS_MAXANISOTROPY, 4);
  if (total() != before) { report_error("D3DTSS_MAXANISOTROPY was counted as unhandled"); return 1; }
```

- [ ] **Step 2: Re-point `coverage_smoke`'s stage-state probe**

In `runtime/test/coverage_smoke.cpp`, replace these three lines:

```cpp
  // A stage state with no implementation (D3DTSS_MAXANISOTROPY = 21) must be COUNTED, not
  // silently dropped — the render-state path reported its gaps while this one swallowed them.
  dev->SetTextureStageState(0, (D3DTEXTURESTAGESTATETYPE)21, 4);
```

with:

```cpp
  // A stage state with no implementation must be COUNTED, not silently dropped — the
  // render-state path reported its gaps while this one swallowed them. D3DTSS_MAXMIPLEVEL (20)
  // is the probe: the backend always samples the full mip chain the engine uploaded, and it is
  // not in the documented-no-op group, so it stays a genuine gap. (This probe used to be
  // MAXANISOTROPY, until that was implemented — if you implement MAXMIPLEVEL, move this again
  // in the same commit or this smoke will fail for the right reason at the wrong time.)
  dev->SetTextureStageState(0, D3DTSS_MAXMIPLEVEL, 1);
```

- [ ] **Step 3: Run them and watch them fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `accepted_states_smoke: D3DTSS_MAXANISOTROPY was counted as unhandled`. A
compile error on the two new enumerators is the same failure one step earlier.

- [ ] **Step 4: Declare the two stage states**

In `runtime/d3d8/d3d8.h`, in `enum D3DTEXTURESTAGESTATETYPE`, change:

```cpp
  D3DTSS_MIPFILTER = 18, D3DTSS_TEXTURETRANSFORMFLAGS = 24
```

to:

```cpp
  D3DTSS_MIPFILTER = 18, D3DTSS_MAXMIPLEVEL = 20, D3DTSS_MAXANISOTROPY = 21,
  D3DTSS_TEXTURETRANSFORMFLAGS = 24
```

- [ ] **Step 5: Add the extension probe**

In `runtime/d3d8webgl/device.cpp`, add the include next to the existing ones at the top:

```cpp
#include <emscripten/html5.h>
```

and inside the anonymous `namespace {` that opens after the debug counters, add:

```cpp
// EXT_texture_filter_anisotropic, resolved once. WebGL requires an extension to be *enabled*
// on the context before its tokens do anything, which is why this goes through
// emscripten_webgl_enable_extension rather than just calling glTexParameterf and hoping.
// Returns the device's max anisotropy, or 0 when the extension is unavailable — 0 reads as
// "no anisotropy possible", never as "1x was requested".
constexpr GLenum kTextureMaxAnisotropyExt = 0x84FE;
constexpr GLenum kMaxTextureMaxAnisotropyExt = 0x84FF;
float aniso_limit() {
  static float limit = -1.0f;
  if (limit >= 0.0f) return limit;
  limit = 0.0f;
  if (emscripten_webgl_enable_extension(emscripten_webgl_get_current_context(),
                                        "EXT_texture_filter_anisotropic")) {
    GLfloat max = 0.0f;
    glGetFloatv(kMaxTextureMaxAnisotropyExt, &max);
    if (max > 1.0f) limit = max;
  }
  return limit;
}
```

- [ ] **Step 6: Store it per stage**

In the `StageState` struct, change:

```cpp
    uint32_t minFilter, magFilter, mipFilter, addressU, addressV;
```

to:

```cpp
    uint32_t minFilter, magFilter, mipFilter, addressU, addressV;
    uint32_t maxAniso;   // D3DTSS_MAXANISOTROPY; 1 = isotropic, D3D8's own default
```

and append `, 1` to each of the two initialisers, so they read:

```cpp
    { D3DTOP_MODULATE, D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_SELECTARG1, D3DTA_TEXTURE, D3DTA_CURRENT, 0, 0, 0,
      D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP, 1 },
    { D3DTOP_DISABLE,  D3DTA_TEXTURE, D3DTA_CURRENT, D3DTOP_DISABLE,    D3DTA_TEXTURE, D3DTA_CURRENT, 1, 0, 0,
      D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTEXF_LINEAR, D3DTADDRESS_WRAP, D3DTADDRESS_WRAP, 1 },
```

- [ ] **Step 7: Accept the state and program it**

In `SetTextureStageState`'s `switch`, immediately after the `case D3DTSS_ADDRESSV:` line, add:

```cpp
      case D3DTSS_MAXANISOTROPY: s.maxAniso = Value ? Value : 1; break;
```

In `apply_sampler` (line ~796), immediately before its closing `}`, add:

```cpp
    // Clamp to the device limit, not to the request: asking for 16x on hardware that offers 4x
    // is not an error in D3D8, it is a request the driver narrows. limit == 0 means the
    // extension is absent, and then there is nothing to program at all.
    if (const float limit = aniso_limit(); limit > 0.0f) {
      const float want = s.maxAniso < 1 ? 1.0f : (float)s.maxAniso;
      glTexParameterf(GL_TEXTURE_2D, kTextureMaxAnisotropyExt, want > limit ? limit : want);
    }
```

- [ ] **Step 8: Run the tests — they must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `accepted_states_smoke` `[1,0,0,255]`, `coverage_smoke` still `[5,1,4,1]` (the
re-pointed probe keeps the same counts), all texture smokes (`draw_tex_smoke`, `lit_tex_smoke`,
`dxt_smoke`, `texfmt_smoke`, `surface_smoke`) still green — those are what would catch a broken
`apply_sampler`.

- [ ] **Step 9: Full CI and commit**

```bash
bash scripts/ci.sh
git add runtime/d3d8/d3d8.h runtime/d3d8webgl/device.cpp \
        runtime/test/accepted_states_smoke.cpp runtime/test/coverage_smoke.cpp
git commit -m "d3d8webgl: honor D3DTSS_MAXANISOTROPY via EXT_texture_filter_anisotropic"
```

---

### Task 5: `D3DRS_SPECULARMATERIALSOURCE`

Already declared in `d3d8.h:135`; only the handler is missing. Its three siblings
(`DIFFUSE`/`AMBIENT`/`EMISSIVE`) are handled at `device.cpp:662-664` and feed shader-key bits at
`:850-852`, so this is the fourth member of a pattern that already works.

One honest limit, and it is the point of the task: the specular *vertex* colour
(`D3DFVF_SPECULAR`, i.e. `D3DMCS_COLOR2`) is not uploaded as an attribute — `device.cpp:889`
skips over it to keep texcoord offsets right. So `D3DMCS_MATERIAL` and `D3DMCS_COLOR1` are
answerable now, and `D3DMCS_COLOR2` must keep reporting through coverage. That is a strictly
better outcome than today's blanket "token unhandled": the counter stops firing for the values we
honour, and if a future capture *does* show `COLOR2`, the counter will say so specifically. Full
`D3DFVF_SPECULAR` attribute upload is deliberately out of scope — it needs a new shader variant
and a smoke that can distinguish a specular vertex colour from a material one, and nothing in the
current measurement asks for it.

**Files:**
- Modify: `runtime/d3d8webgl/device.cpp` (state store ~line 486, `SetRenderState` ~line 664, shader key ~line 852)
- Modify: `runtime/test/accepted_states_smoke.cpp`
- Modify: `runtime/test/conformance.cpp`

**Interfaces:**
- Consumes: `accepted_states_smoke.cpp` from Tasks 3–4.
- Produces: `Device8::specularSource` (`uint32_t`, default `D3DMCS_MATERIAL`).

- [ ] **Step 1: Write the failing test**

In `runtime/test/accepted_states_smoke.cpp`, immediately after the `D3DTSS_MAXANISOTROPY`
assertion, add:

```cpp
  // The fourth material-colour source. MATERIAL and COLOR1 are answerable from state the device
  // already tracks, so they must not count.
  dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
  dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR1);
  if (total() != before) { report_error("a handled SPECULARMATERIALSOURCE value was counted"); return 1; }
```

and immediately after the `D3DFILL_WIREFRAME` assertion (which expects `before + 1`), add:

```cpp
  // COLOR2 sources the specular colour from D3DFVF_SPECULAR, which is not uploaded as an
  // attribute (device.cpp skips its stride to keep texcoord offsets correct). It must keep
  // reporting — specifically, so a future capture that uses it says so instead of going quiet.
  dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
  if (total() != before + 2) { report_error("SPECULARMATERIALSOURCE(COLOR2) was silently accepted"); return 1; }
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `accepted_states_smoke: a handled SPECULARMATERIALSOURCE value was counted`.

- [ ] **Step 3: Store the state**

In `runtime/d3d8webgl/device.cpp`, change line ~486 from:

```cpp
  uint32_t diffuseSource = D3DMCS_COLOR1, ambientSource = D3DMCS_MATERIAL, emissiveSource = D3DMCS_MATERIAL;
```

to:

```cpp
  uint32_t diffuseSource = D3DMCS_COLOR1, ambientSource = D3DMCS_MATERIAL, emissiveSource = D3DMCS_MATERIAL;
  // D3D8's own default is COLOR2, but this backend does not upload D3DFVF_SPECULAR as an
  // attribute, so MATERIAL is the honest default: it is what the shader actually reads.
  uint32_t specularSource = D3DMCS_MATERIAL;
```

- [ ] **Step 4: Handle it**

In `SetRenderState`'s `switch`, immediately after `case D3DRS_EMISSIVEMATERIALSOURCE:`, add:

```cpp
      case D3DRS_SPECULARMATERIALSOURCE:
        // COLOR2 would read the specular vertex colour, which is not an attribute here (see the
        // D3DFVF_SPECULAR skip in bind_pipeline) — report that value rather than pretend, and
        // keep sourcing from the material so specular stays correct instead of going black.
        if (Value == D3DMCS_COLOR2) coverage::unhandled_render_state(State);
        else specularSource = Value;
        break;
```

- [ ] **Step 5: Make it reachable from the shader key**

In `bind_pipeline`'s key setup (~line 852), immediately after the `key.emisFromVertex` line, add:

```cpp
    // COLOR1 is the only vertex source available (there is no specular attribute), so this is
    // the one non-material case the shader can express.
    key.specFromVertex = lit && cvOn && specularSource == D3DMCS_COLOR1;
```

Then add the matching bit to the shader key in `runtime/graphics-ff/ff_shader.h:40`, changing:

```cpp
  bool diffFromVertex = false, ambFromVertex = false, emisFromVertex = false;
```

to:

```cpp
  bool diffFromVertex = false, ambFromVertex = false, emisFromVertex = false;
  bool specFromVertex = false;   // D3DRS_SPECULARMATERIALSOURCE == D3DMCS_COLOR1
```

(The struct is `ff::Key`, not `ProgramKey`.)

- [ ] **Step 5b: Pack the new bit into the program-cache key — do not skip this**

`runtime/graphics-ff/ff_shader.cpp:312` packs the key's bools into an integer that identifies a
cached program. A bit that is not packed does not exist as far as the cache is concerned: the
material-sourced and vertex-sourced variants would hash to the same entry and whichever compiled
first would be handed to both. Change:

```cpp
      | (k.diffFromVertex ? 4u : 0u) | (k.ambFromVertex ? 8u : 0u) | (k.emisFromVertex ? 16u : 0u));
```

to:

```cpp
      | (k.diffFromVertex ? 4u : 0u) | (k.ambFromVertex ? 8u : 0u) | (k.emisFromVertex ? 16u : 0u)
      | (k.specFromVertex ? 32u : 0u));
```

Check that `32u` is not already taken by a bit added after this plan was written — read the whole
expression, not just the line above.

- [ ] **Step 5c: Source the specular colour from the vertex when the bit is set**

In `runtime/graphics-ff/ff_shader.cpp`, the lit shader multiplies the material specular into the
accumulated specular term at line ~203:

```cpp
      "  c.rgb += (uMatSpecular * ssum).rgb;\n"
```

Replace that with a variant selected by the new bit, mirroring exactly how line 198-200 spell
`diffFromVertex`/`emisFromVertex` (`aColor.bgra` — D3D's texel/colour byte order recovered by the
same swizzle used everywhere else in this file):

```cpp
    vs += k.specFromVertex ? "  vec4 matSpec = aColor.bgra;\n" : "  vec4 matSpec = uMatSpecular;\n";
    vs += "  c.rgb += (matSpec * ssum).rgb;\n";
```

Read lines 195-205 before editing: they are inside a string-concatenation sequence, so the two
lines above must be spliced into that same `vs +=` chain rather than dropped in as standalone
statements. Do not invent a second idiom for something this file already spells three times.

- [ ] **Step 6: Add the conformance probe rows**

In `runtime/test/conformance.cpp`'s `rs[]` table, after the two `D3DRS_FILLMODE` rows added in
Task 3, add:

```cpp
    {"D3DRS_SPECULARMATERIALSOURCE(MATERIAL)", D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL},
    {"D3DRS_SPECULARMATERIALSOURCE(COLOR2)", D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2},
```

- [ ] **Step 7: Run the tests — they must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `accepted_states_smoke` `[1,0,0,255]`; `specular_smoke` still `[204,204,204,255]` —
that is the smoke that would catch a broken specular term.

- [ ] **Step 8: Full CI and commit**

```bash
bash scripts/ci.sh
git add runtime/d3d8webgl/device.cpp runtime/graphics-ff/ff_shader.h runtime/graphics-ff/ff_shader.cpp \
        runtime/test/accepted_states_smoke.cpp runtime/test/conformance.cpp
git commit -m "d3d8webgl: handle D3DRS_SPECULARMATERIALSOURCE, report only the COLOR2 gap"
```

---

### Task 6: The nine documented no-ops

Nine tokens in `measured-gap.json` are dispositioned "No-op (documented)": `D3DRS_PATCHSEGMENTS`
(40,138 hits — the most-hit token in every scenario), `D3DRS_SOFTWAREVERTEXPROCESSING`,
`D3DRS_RANGEFOGENABLE`, and the six bump-environment stage states. None is a rendering request
this backend is failing to serve, and while they share a counter with real gaps, they dominate
any ranking of what to do next — `PATCHSEGMENTS` alone outranks every genuine finding.

The information is not lost by silencing them. `D3DTOP_BUMPENVMAP`/`BUMPENVMAPLUMINANCE` — the
*only* consumers of the six stage states, and the thing that would make them live — remain
unimplemented and therefore still counted, so a future capture that genuinely asks for bump
mapping still reports it. The state tokens are the noise; the op is the signal.

**Files:**
- Modify: `runtime/d3d8/d3d8.h` (declare the nine tokens)
- Modify: `runtime/d3d8webgl/device.cpp` (`SetRenderState`, `SetTextureStageState`)
- Modify: `runtime/test/accepted_states_smoke.cpp`

**Interfaces:**
- Consumes: `accepted_states_smoke.cpp` from Tasks 3–5.
- Produces: nothing consumed later.

- [ ] **Step 1: Write the failing test**

In `runtime/test/accepted_states_smoke.cpp`, immediately after the `SPECULARMATERIALSOURCE`
handled-values assertion, add:

```cpp
  // The documented no-op group. Each is accepted and ignored for a reason written at the call
  // site; none is a rendering request this backend fails to serve, so none may count. Left
  // counting, D3DRS_PATCHSEGMENTS alone (40,138 hits in the Generals capture) outranks every
  // genuine finding in any ordering by frequency.
  dev->SetRenderState(D3DRS_PATCHSEGMENTS, 0x40000000u /* a float bit-pattern, per W3D */);
  dev->SetRenderState(D3DRS_SOFTWAREVERTEXPROCESSING, 0);
  dev->SetRenderState(D3DRS_RANGEFOGENABLE, 0);
  for (D3DTEXTURESTAGESTATETYPE t : {D3DTSS_BUMPENVMAT00, D3DTSS_BUMPENVMAT01, D3DTSS_BUMPENVMAT10,
                                     D3DTSS_BUMPENVMAT11, D3DTSS_BUMPENVLSCALE, D3DTSS_BUMPENVLOFFSET})
    dev->SetTextureStageState(0, t, 0);
  if (total() != before) { report_error("a documented no-op token was counted as unhandled"); return 1; }

  // The prerequisite op stays a real gap, so the six matrix states above are still discoverable
  // through the one token that would make them live. Silencing the states must not silence this.
  const uint32_t beforeOp = total();
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_BUMPENVMAP);
  if (total() != beforeOp + 1) { report_error("D3DTOP_BUMPENVMAP stopped being reported"); return 1; }
```

Because this adds one more counted token, update the `D3DFILL_WIREFRAME` and
`SPECULARMATERIALSOURCE(COLOR2)` assertions' arithmetic only if you moved them — as written above
they run *before* this block and are unaffected. Keep the new block last.

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `accepted_states_smoke: a documented no-op token was counted as unhandled`, or
a compile error on the undeclared tokens (same failure, one step earlier).

- [ ] **Step 3: Declare the tokens**

In `runtime/d3d8/d3d8.h`, in `enum D3DRENDERSTATETYPE`, change:

```cpp
  D3DRS_COLORWRITEENABLE = 168
```

to:

```cpp
  D3DRS_COLORWRITEENABLE = 168,
  // Accepted and ignored, each for a reason written at its handler (see SetRenderState).
  D3DRS_RANGEFOGENABLE = 48, D3DRS_SOFTWAREVERTEXPROCESSING = 153, D3DRS_PATCHSEGMENTS = 164
```

In `enum D3DTEXTURESTAGESTATETYPE`, change:

```cpp
  D3DTSS_COLOROP = 1, D3DTSS_COLORARG1 = 2, D3DTSS_COLORARG2 = 3, D3DTSS_ALPHAOP = 4,
```

to:

```cpp
  D3DTSS_COLOROP = 1, D3DTSS_COLORARG1 = 2, D3DTSS_COLORARG2 = 3, D3DTSS_ALPHAOP = 4,
  // Bump-environment matrix + luminance scale/offset. Inert without D3DTOP_BUMPENVMAP, which
  // is not implemented (and still reported), so these are accepted and ignored.
  D3DTSS_BUMPENVMAT00 = 7, D3DTSS_BUMPENVMAT01 = 8, D3DTSS_BUMPENVMAT10 = 9,
  D3DTSS_BUMPENVMAT11 = 10, D3DTSS_BUMPENVLSCALE = 22, D3DTSS_BUMPENVLOFFSET = 23,
```

Also add the bump ops to the colour-op list so the test can name one. `enum D3DTEXTUREOP`
(`runtime/d3d8/d3d8.h:86`) currently ends at `D3DTOP_BLENDCURRENTALPHA = 16, D3DTOP_DOTPRODUCT3 = 24`
and declares neither bump op — verified when this plan was written. Change that last line to:

```cpp
  D3DTOP_BLENDCURRENTALPHA = 16,
  // Declared so the coverage layer and its smoke can name the ops that would make the six
  // D3DTSS_BUMPENV* states live. Neither is implemented, and both stay reported.
  D3DTOP_BUMPENVMAP = 22, D3DTOP_BUMPENVMAPLUMINANCE = 23, D3DTOP_DOTPRODUCT3 = 24
```

Confirm they are still absent before adding them — a duplicated enumerator will not compile.
Also confirm `combiner_op_supported` (`runtime/d3d8webgl/device.cpp:602`) does **not** list them:
if either appears there, it would be treated as supported and the smoke's mirror assertion in
Step 1 would fail for a real reason.

- [ ] **Step 4: Accept-and-ignore the three render states**

In `SetRenderState`'s `switch`, immediately before the `default:` line, add:

```cpp
      // --- Accepted and ignored. Each of these is a decision, not a gap: routing them to the
      // coverage layer would rank them against real missing features, and PATCHSEGMENTS alone
      // (40,138 hits in the Generals capture) would top that ranking forever.
      case D3DRS_PATCHSEGMENTS:
        // Not a render state as far as the engine is concerned: W3D smuggles a float
        // bit-pattern through it as an N-patch tessellation hint
        // (engine/GeneralsX/.../WW3D2/shader.cpp:1036). WebGL2 has no tessellation stage, so
        // there is no implementation to have — only a side channel to ignore.
        break;
      case D3DRS_SOFTWAREVERTEXPROCESSING:
        // A device-pipeline mode switch. This backend has exactly one vertex path (GLSL on the
        // GPU) and no software fallback to switch to, so there is nothing to select.
        break;
      case D3DRS_RANGEFOGENABLE:
        // Set twice at init and never again — almost certainly the engine writing D3D8's own
        // FALSE default during a blanket state reset (the coverage layer records the token, not
        // the value, so this is inferred). If a capture ever shows range fog genuinely enabled,
        // it IS expressible via the existing shader-emulated fog: move it to a real handler then.
        break;
```

- [ ] **Step 5: Accept-and-ignore the six stage states**

In `SetTextureStageState`'s `switch`, immediately before its `default:` line, add:

```cpp
      // Bump-environment matrix + luminance scale/offset, written as part of the engine's
      // blanket stage-state reset at device init. Inert without D3DTOP_BUMPENVMAP or
      // D3DTOP_BUMPENVMAPLUMINANCE to consume them, and neither op appears anywhere in the
      // measurement (docs/measured-gap.json zero-hit findings) — the game never asked for bump
      // mapping itself. Implementing the matrix without the op would be dead code. The op stays
      // unimplemented and therefore still reported, so this stays discoverable if it ever lands.
      case D3DTSS_BUMPENVMAT00: case D3DTSS_BUMPENVMAT01:
      case D3DTSS_BUMPENVMAT10: case D3DTSS_BUMPENVMAT11:
      case D3DTSS_BUMPENVLSCALE: case D3DTSS_BUMPENVLOFFSET:
        break;
```

- [ ] **Step 6: Run the tests — they must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `accepted_states_smoke` `[1,0,0,255]`; `coverage_smoke` still `[5,1,4,1]` (its probes
are `FILLMODE`, `D3DTOP_MULTIPLYADD`, `D3DFMT_UNKNOWN` and `MAXMIPLEVEL` — none of them silenced
here).

- [ ] **Step 7: Full CI and commit**

```bash
bash scripts/ci.sh
git add runtime/d3d8/d3d8.h runtime/d3d8webgl/device.cpp runtime/test/accepted_states_smoke.cpp
git commit -m "d3d8webgl: accept the nine documented no-op tokens instead of counting them as gaps"
```

---

# Tier 3 — instrument the two questions the measurement cannot answer

### Task 7: Vertex blending becomes measurable

`measured-gap.json` is explicit that vertex blending's absence from all three captures **proves
nothing**: it is carried by `D3DFVF_XYZB1-5` position bits, not by any
`D3DRS_*`/`D3DTSS_*`/`D3DTOP_*`/`D3DFMT_*` token the coverage layer watches, so there is no
instrument for it at all. Today `SetVertexShader` stores the FVF verbatim
(`device.cpp:592`) and `bind_pipeline` binds position as 3 or 4 floats — a blended-position FVF
would be silently mis-bound. This task adds the missing instrument so the next capture can answer
the question, and makes the mis-bind loud instead of silent.

**Files:**
- Modify: `runtime/include/dx8wasm/contract.h` (append one counter)
- Modify: `runtime/coverage/coverage.{h,cpp}` (new family + sink)
- Modify: `runtime/d3d8webgl/device.cpp` (`SetVertexShader`)
- Create: `runtime/test/vertexblend_smoke.cpp`
- Modify: `CMakeLists.txt`, `web-runtime/test/phase2.gpu.test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: `coverage::unhandled_vertex_format(uint32_t positionBits)`; a new trailing field
  `uint32_t unhandled_vertex_formats;` in `dx8wasm_coverage`; telemetry keys of the form
  `d3d8.unhandled.fvf.<8-hex>` where the value is the **position mask only**
  (`fvf & 0x0000000e`), not the whole FVF — so the key space stays the five blend widths rather
  than one key per FVF combination the engine happens to build.

- [ ] **Step 1: Write the failing test**

Create `runtime/test/vertexblend_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Vertex blending (D3DFVF_XYZB1-5) had no instrument at all: it rides the FVF position bits,
// not a D3DRS_*/D3DTSS_*/D3DTOP_*/D3DFMT_* token, so its absence from a capture proved nothing
// either way (docs/measured-gap.json says exactly this). Worse, bind_pipeline would bind such a
// position as 3 floats and silently mis-read every vertex. Assert the blended widths are
// counted and the two ordinary position types are not. Reports [1,0,0,255].
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

static uint32_t blends() {
  dx8wasm_coverage c{};
  dx8wasm_get_coverage(&c);
  return c.unhandled_vertex_formats;
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  // The two position types this backend implements must not count.
  const uint32_t base = blends();
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
  dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  if (blends() != base) { report_error("an implemented position type was counted as blended"); return 1; }

  // Each blended width must count. Keyed on the position mask, so XYZB1 with and without a
  // normal is one token, not two — five possible keys total, not one per FVF the engine builds.
  dev->SetVertexShader(D3DFVF_XYZB1 | D3DFVF_DIFFUSE);
  dev->SetVertexShader(D3DFVF_XYZB1 | D3DFVF_NORMAL | D3DFVF_TEX1);
  dev->SetVertexShader(D3DFVF_XYZB3 | D3DFVF_DIFFUSE);
  if (blends() != base + 3) { report_error("blended FVFs were not counted once per occurrence"); return 1; }

  dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
```

- [ ] **Step 2: Register the smoke**

In `CMakeLists.txt`, after the `accepted_states_smoke` block from Task 3, add:

```cmake
add_executable(vertexblend_smoke runtime/test/vertexblend_smoke.cpp)
target_include_directories(vertexblend_smoke PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(vertexblend_smoke PRIVATE dx8_d3d8webgl)
target_link_options(vertexblend_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(vertexblend_smoke PROPERTIES SUFFIX ".js")
```

In `web-runtime/test/phase2.gpu.test.mjs`, after the `accepted_states_smoke` entry:

```js
  ['vertexblend_smoke', [1, 0, 0, 255]],       // D3DFVF_XYZB1-5 is now instrumented (was unmeasurable)
```

- [ ] **Step 3: Run it and watch it fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL to compile — `unhandled_vertex_formats` is not a member of `dx8wasm_coverage`, and
`D3DFVF_XYZB1`/`XYZB3` are undeclared. That is the correct first failure.

- [ ] **Step 4: Declare the blended position bits**

In `runtime/d3d8/d3d8.h`, after the existing `D3DFVF_TEXCOUNT_SHIFT` define, add:

```cpp
// Position type lives in bits 1-3 as a value, not as independent flags: XYZ and XYZRHW are two
// of eight encodings, and XYZB1-5 are five more (blend-weight counts 1..5). Masking with
// POSITION_MASK is the only correct way to ask "which position type is this".
#define D3DFVF_POSITION_MASK 0x0000000eu
#define D3DFVF_XYZB1    0x0006u
#define D3DFVF_XYZB2    0x0008u
#define D3DFVF_XYZB3    0x000au
#define D3DFVF_XYZB4    0x000cu
#define D3DFVF_XYZB5    0x000eu
```

- [ ] **Step 5: Append the counter to the ABI**

In `runtime/include/dx8wasm/contract.h`, append inside `dx8wasm_coverage`, after
`unhandled_texture_stage_states`:

```c
    // Appended (keeps existing offsets): D3DFVF_XYZB1-5 blended-position formats, which
    // bind_pipeline cannot express. Vertex blending rides the FVF position bits rather than a
    // D3DRS_*/D3DTSS_* token, so before this counter existed its absence from a capture proved
    // nothing at all — see docs/measured-gap.json's "does not speak to it" note.
    uint32_t unhandled_vertex_formats;
```

- [ ] **Step 6: Add the coverage family and sink**

In `runtime/coverage/coverage.h`, beside the other sinks:

```cpp
void unhandled_vertex_format(uint32_t positionBits);   // a D3DFVF_XYZB* the device cannot bind
```

In `runtime/coverage/coverage.cpp`:

1. Extend the family enum:

```cpp
enum Family { RS = 0, TOP = 1, FMT = 2, TSS = 3, FVF = 4 };
```

2. Add the kind string and extend the table (the `static_assert` below it will catch a miss):

```cpp
#define DX8WASM_KIND_FVF     "fvf"
const char* const kTelKind[] = {DX8WASM_KIND_RSTATE, DX8WASM_KIND_TEXOP, DX8WASM_KIND_FORMAT,
                                DX8WASM_KIND_TSSTATE, DX8WASM_KIND_FVF};
static_assert(sizeof kTelKind / sizeof *kTelKind == FVF + 1,
              "kTelKind must have exactly one entry per Family enumerator");
```

3. Add the per-kind budget assertion beside the existing four:

```cpp
static_assert(sizeof(DX8WASM_KIND_FVF) - 1 <= kTelKindMaxLen, DX8WASM_KIND_FVF " exceeds the per-kind budget");
```

4. Add the sink beside the others in `namespace coverage`:

```cpp
void unhandled_vertex_format(uint32_t p) { note(FVF, "D3DFVF", p, g_counts.unhandled_vertex_formats); }
```

- [ ] **Step 7: Report it from the device**

In `runtime/d3d8webgl/device.cpp`, replace line 592:

```cpp
  HRESULT SetVertexShader(DWORD Handle) override { fvf = Handle; return D3D_OK; }
```

with:

```cpp
  HRESULT SetVertexShader(DWORD Handle) override {
    // bind_pipeline binds position as 3 floats (XYZ) or 4 (XYZRHW). A blended position carries
    // 1-5 extra blend weights, so binding it either way mis-reads every vertex — and until this
    // report existed there was no instrument for it anywhere, which is why the Generals
    // measurement could not say whether the engine uses it. Keyed on the position mask, so the
    // key space is the five blend widths rather than one key per FVF combination.
    const DWORD pos = Handle & D3DFVF_POSITION_MASK;
    if (pos != D3DFVF_XYZ && pos != D3DFVF_XYZRHW && pos != 0)
      coverage::unhandled_vertex_format(pos);
    fvf = Handle;
    return D3D_OK;
  }
```

`pos == 0` is excluded deliberately: `SetVertexShader(0)` is how a caller clears the FVF, and
D3D8 also uses non-FVF handles there — neither is a blended position.

- [ ] **Step 8: Run the tests — they must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `vertexblend_smoke` `[1,0,0,255]`; every existing draw smoke still green (they all call
`SetVertexShader` with `XYZ`- or `XYZRHW`-based FVFs, so none may start counting).

- [ ] **Step 9: Full CI and commit**

```bash
bash scripts/ci.sh
git add runtime/d3d8/d3d8.h runtime/include/dx8wasm/contract.h runtime/coverage/coverage.h \
        runtime/coverage/coverage.cpp runtime/d3d8webgl/device.cpp \
        runtime/test/vertexblend_smoke.cpp CMakeLists.txt web-runtime/test/phase2.gpu.test.mjs
git commit -m "coverage: instrument D3DFVF_XYZB* so vertex blending is measurable at all"
```

---

### Task 8: Fog usage becomes distinguishable from fog silence

`CONFORMANCE.md`'s zero-hit table states the limitation precisely: the fog counter only fires on a
`FOGTABLEMODE`/`FOGVERTEXMODE` value other than `LINEAR`/`NONE` (`device.cpp:673`), so a zero
proves EXP/EXP2 was never *set* — it cannot distinguish "the game relies on linear fog" from "the
game never touches fog at all". Linear fog is implemented and may be load-bearing; nobody can
currently tell.

This is **not** a coverage counter: nothing here is unimplemented and nothing falls back, so it
must not touch `fallbacks_taken` or any `unhandled_*` field. It is a plain telemetry counter,
emitted on *transitions* only (the value actually changing), which bounds ring pressure on a state
an engine may rewrite per pass while still capturing every distinct mode the game ever selects.

**Files:**
- Modify: `runtime/d3d8webgl/device.cpp` (include, two state mirrors, the fog case)
- Create: `runtime/test/fogmode_smoke.cpp`
- Modify: `CMakeLists.txt`, `web-runtime/test/phase2.gpu.test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: telemetry keys `d3d8.fogmode.table.<8-hex>` and `d3d8.fogmode.vertex.<8-hex>`, one
  record per *transition* to that mode. A count is a transition count, never an occurrence count.

- [ ] **Step 1: Check the key budget before writing anything**

`DX8WASM_TEL_NAME_MAX` is 32 (31 usable). `d3d8.fogmode.table.` is 19 characters and the hex
value is 8, totalling 27 — fits. `d3d8.fogmode.vertex.` is 20 + 8 = 28 — also fits.

```bash
grep -n "DX8WASM_TEL_NAME_MAX" runtime/include/dx8wasm/telemetry.h
```
Expected: `32`. If it is smaller, shorten the keys to `d3d8.fogt.` / `d3d8.fogv.` and use those
throughout the rest of this task instead — a truncated key silently merges two measurements.

- [ ] **Step 2: Write the failing test**

Create `runtime/test/fogmode_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// The fog coverage counter only fires on a non-LINEAR/NONE mode, so a zero could never
// distinguish "this game relies on linear fog" from "this game never touches fog"
// (docs/CONFORMANCE.md, zero-hit findings). This emits a positive-usage telemetry counter on
// every fog-mode TRANSITION — not a coverage counter: nothing here is unimplemented and nothing
// falls back. Asserts the record appears for LINEAR, is not re-emitted for an unchanged value,
// and that coverage counters stay untouched throughout. Reports [1,1,1,1].
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include "dx8wasm/telemetry.h"
#include <cstring>
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

static int count_occurrences(const char* haystack, const char* needle) {
  int n = 0;
  const size_t len = strlen(needle);
  for (const char* p = strstr(haystack, needle); p; p = strstr(p + len, needle)) ++n;
  return n;
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  dx8wasm_coverage before{};
  dx8wasm_get_coverage(&before);

  // One transition to LINEAR, then two redundant writes of the same value: a transition counter
  // must record the first and ignore the rest, or an engine that rewrites fog state per pass
  // would flood the ring with a value that never changed.
  dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
  dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);
  dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_LINEAR);

  dx8wasm_coverage after{};
  dx8wasm_get_coverage(&after);   // also flushes the telemetry tally
  const int coverageUntouched =
      (after.unhandled_render_states == before.unhandled_render_states &&
       after.fallbacks_taken == before.fallbacks_taken) ? 1 : 0;
  if (!coverageUntouched) { report_error("a handled fog mode bumped a coverage counter"); return 1; }

  char tel[4096];
  dx8wasm_tel_drain(tel, sizeof tel);
  const char* const kKey = "\"n\":\"d3d8.fogmode.table.00000003\"";   // D3DFOG_LINEAR == 3
  const int occurrences = count_occurrences(tel, kKey);
  if (occurrences != 1) { report_error("linear fog was not recorded exactly once per transition"); return 1; }
  const int deltaIsOne = strstr(tel, "\"n\":\"d3d8.fogmode.table.00000003\",\"v\":1}") != nullptr ? 1 : 0;
  if (!deltaIsOne) { report_error("three identical writes were recorded as more than one transition"); return 1; }

  dev->Release(); d3d->Release();
  report_pixel(1, occurrences, deltaIsOne, coverageUntouched);
  return 0;
}
```

`D3DFOG_LINEAR == 3` was confirmed against `runtime/d3d8/d3d8.h:147`
(`enum D3DFOGMODE { D3DFOG_NONE = 0, D3DFOG_EXP = 1, D3DFOG_EXP2 = 2, D3DFOG_LINEAR = 3 }`) when
this plan was written, so the `00000003` key literals are correct. Re-grep only if the smoke fails
on the key rather than on the count:

```bash
grep -n "D3DFOG_LINEAR" runtime/d3d8/d3d8.h
```

- [ ] **Step 3: Register the smoke**

In `CMakeLists.txt`, after the `vertexblend_smoke` block:

```cmake
add_executable(fogmode_smoke runtime/test/fogmode_smoke.cpp)
target_include_directories(fogmode_smoke PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(fogmode_smoke PRIVATE dx8_d3d8webgl)
target_link_options(fogmode_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(fogmode_smoke PROPERTIES SUFFIX ".js")
```

In `web-runtime/test/phase2.gpu.test.mjs`, after the `vertexblend_smoke` entry:

```js
  ['fogmode_smoke', [1, 1, 1, 1]],             // fog-mode transitions are recorded; coverage untouched
```

- [ ] **Step 4: Run it and watch it fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `fogmode_smoke: linear fog was not recorded exactly once per transition` (no
such record is emitted today).

- [ ] **Step 5: Implement the transition counter**

In `runtime/d3d8webgl/device.cpp`, add the include beside the others:

```cpp
#include "dx8wasm/telemetry.h"
```

In the `Device8` state block, beside the other fog fields, add:

```cpp
  // Last fog mode written, per D3D8 state. Sentinel 0xFFFFFFFF = "never written", so the first
  // write is a transition even when it selects mode 0.
  uint32_t lastFogTableMode = 0xFFFFFFFFu, lastFogVertexMode = 0xFFFFFFFFu;
```

Then replace the fog-mode case (currently `device.cpp:673`):

```cpp
      case D3DRS_FOGTABLEMODE: case D3DRS_FOGVERTEXMODE:
        if (Value != D3DFOG_LINEAR && Value != D3DFOG_NONE) coverage::unhandled_render_state(State); break;
```

with:

```cpp
      case D3DRS_FOGTABLEMODE: case D3DRS_FOGVERTEXMODE: {
        // Positive-usage telemetry, deliberately NOT a coverage counter: LINEAR is implemented,
        // so nothing here falls back and fallbacks_taken must not move. It exists because the
        // coverage counter alone could never distinguish "relies on linear fog" from "never
        // touches fog" (docs/CONFORMANCE.md zero-hit findings). Emitted on transitions only —
        // an engine may rewrite this per pass, and a per-occurrence record would crowd the ring
        // for a value that never changed. So a count here is a TRANSITION count, not an
        // occurrence count; do not read it as "how often fog was set".
        uint32_t& last = State == D3DRS_FOGTABLEMODE ? lastFogTableMode : lastFogVertexMode;
        if (last != Value) {
          last = Value;
          char key[DX8WASM_TEL_NAME_MAX];
          std::snprintf(key, sizeof key, "d3d8.fogmode.%s.%08x",
                        State == D3DRS_FOGTABLEMODE ? "table" : "vertex", (unsigned)Value);
          dx8wasm_tel_counter(key, 1);
        }
        if (Value != D3DFOG_LINEAR && Value != D3DFOG_NONE) coverage::unhandled_render_state(State);
        break;
      }
```

- [ ] **Step 6: Run the tests — they must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `fogmode_smoke` `[1,1,1,1]`; `fog_smoke` still `[128,0,128,255]`; `coverage_smoke`
still `[5,1,4,1]` (it never sets a fog mode, so no extra record can reach its drain).

- [ ] **Step 7: Full CI and commit**

```bash
bash scripts/ci.sh
git add runtime/d3d8webgl/device.cpp runtime/test/fogmode_smoke.cpp \
        CMakeLists.txt web-runtime/test/phase2.gpu.test.mjs
git commit -m "telemetry: record fog-mode transitions so 'fog unused' stops being unfalsifiable"
```

---

# Tier 4 — the determinism harness

### Task 9: Determinism harness stub

The last unticked Phase 4 item: "determinism harness stub (for games with replays)". A replay
desyncs when the same inputs stop producing the same state, so the SDK's contribution is a
repeatable render digest plus a runner that compares it across executions. Two axes are worth
distinguishing, and this task covers both: **in-process** (the same scene rendered twice through
one device must digest identically — catches state left dirty by the first pass) and
**across-process** (a fresh page load must reproduce the same digest — catches uninitialised
memory and iteration-order-dependent shader-cache keys).

The 4×4 canvas is a real limit, so the smoke renders several distinct sub-scenes and folds all of
their read-backs into one digest: 8 passes × 16 pixels is 128 pixels of signal instead of 16.
Because the harness compares pixels with ±2 tolerance, the digest is asserted in C++ and
published on a separate `window.__det` field; the reported pixel is a plain sentinel.

**Files:**
- Create: `runtime/test/frame_digest.h`
- Create: `runtime/test/determinism_smoke.cpp`
- Create: `scripts/determinism.mjs`
- Modify: `CMakeLists.txt`, `web-runtime/test/phase2.gpu.test.mjs`, `scripts/ci.sh`

**Interfaces:**
- Consumes: nothing.
- Produces: `digest::fnv1a_framebuffer(uint32_t seed, int w, int h) -> uint32_t` (inline, in
  `runtime/test/frame_digest.h`) — folds a `glReadPixels` of the given rect into `seed` and
  returns the new digest, so successive passes chain. `determinism_smoke` publishes
  `window.__det = { digest: "<8 hex chars>" }` alongside its `window.__gpu` sentinel.

- [ ] **Step 1: Write the digest helper**

Create `runtime/test/frame_digest.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// A repeatable framebuffer digest for determinism checking. FNV-1a over the read-back bytes,
// chained across passes so a whole sequence of sub-scenes folds into one 32-bit value. Lives in
// runtime/test/ on purpose: this is a harness tool, not part of the SDK's public contract.
#ifndef DX8WASM_FRAME_DIGEST_H
#define DX8WASM_FRAME_DIGEST_H
#include <GLES3/gl3.h>
#include <cstdint>
#include <vector>

namespace digest {
constexpr uint32_t kSeed = 0x811c9dc5u;   // FNV-1a offset basis

// Reads back w*h RGBA pixels and folds them into `seed`. Chain the return value into the next
// call to digest a whole sequence. Reads the full rect (not sampled points) so a difference in
// any pixel changes the result — sampling would let a real divergence hide between the samples.
inline uint32_t fnv1a_framebuffer(uint32_t seed, int w, int h) {
  std::vector<unsigned char> px((size_t)w * h * 4);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  for (unsigned char b : px) { seed ^= b; seed *= 16777619u; }
  return seed;
}
}   // namespace digest
#endif
```

- [ ] **Step 2: Write the failing test**

Create `runtime/test/determinism_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Determinism harness (Phase 4). A replay desyncs when identical inputs stop producing identical
// state, so this renders one fixed sequence of sub-scenes TWICE through the same device and
// asserts the two framebuffer digests match — that catches state the first pass left dirty.
// scripts/determinism.mjs then loads this smoke in several fresh contexts and compares the
// published digest across them, which catches uninitialised memory and iteration-order-dependent
// shader-cache keys that an in-process repeat cannot see.
//
// The digest is asserted here and published on window.__det, NOT reported through the pixel
// tuple: phase2.gpu.test.mjs compares pixels with a +/-2 tolerance, which would happily accept a
// digest that changed by one. Reports the sentinel [1,0,0,255].
#include "d3d8/d3d8.h"
#include "frame_digest.h"
#include <cstdio>
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });
EM_JS(void, report_digest, (const char* d), { window.__det = { digest: UTF8ToString(d) }; });

static IDirect3DDevice8* g_dev = nullptr;

// One fixed sequence of sub-scenes, chosen so the 16 pixels of a 4x4 canvas are not all the same
// colour and so several independent parts of the pipeline contribute: clear colour, depth state,
// blend state, and a transform. Four passes x 16 px = 64 px of signal per repeat.
static uint32_t render_sequence() {
  uint32_t d = digest::kSeed;
  const uint32_t colors[] = {0xFF3366CCu, 0xFF33CC66u, 0xFFCC6633u, 0xFF663399u};
  for (uint32_t c : colors) {
    g_dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, c, 1.0f, 0);
    g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    d = digest::fnv1a_framebuffer(d, 4, 4);
  }
  return d;
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev) != D3D_OK || !g_dev) {
    report_error("CreateDevice failed"); return 1;
  }

  const uint32_t first = render_sequence();
  const uint32_t second = render_sequence();
  if (first != second) { report_error("the same sequence digested differently on a repeat"); return 1; }

  // A digest of zero would also compare equal to itself, so a readback that silently returned
  // nothing would pass the check above. Reject the degenerate value explicitly.
  if (first == digest::kSeed) { report_error("digest never absorbed any pixels"); return 1; }

  char hex[16];
  std::snprintf(hex, sizeof hex, "%08x", first);
  report_digest(hex);

  g_dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
```

- [ ] **Step 3: Register the smoke**

In `CMakeLists.txt`, after the `fogmode_smoke` block:

```cmake
add_executable(determinism_smoke runtime/test/determinism_smoke.cpp)
target_include_directories(determinism_smoke PRIVATE runtime runtime/d3d8 runtime/include runtime/test)
target_link_libraries(determinism_smoke PRIVATE dx8_d3d8webgl)
target_link_options(determinism_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(determinism_smoke PROPERTIES SUFFIX ".js")
```

In `web-runtime/test/phase2.gpu.test.mjs`, after the `fogmode_smoke` entry:

```js
  ['determinism_smoke', [1, 0, 0, 255]],       // same sequence twice in-process digests identically
```

- [ ] **Step 4: Run it and watch it build and pass in-process**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `determinism_smoke` `[1,0,0,255]`. This half is expected to pass on the first run — the
in-process check is a regression guard, not a known-broken behaviour. If it *fails*, you have
found a real state-leak bug: stop and debug that before continuing, because the across-process
runner below will not be meaningful until it passes.

- [ ] **Step 5: Write the across-process runner**

Create `scripts/determinism.mjs`:

```js
// SPDX-License-Identifier: GPL-3.0-only
// Determinism harness (Phase 4), across-process half. determinism_smoke already asserts that one
// sequence digests identically when repeated inside a single process; this loads it in N fresh
// page contexts and compares the digest between them, which is what catches uninitialised
// memory and iteration-order-dependent shader-cache keys that an in-process repeat cannot see.
// A game with replays extends this: digest its own simulation state per tick and compare here.
import { createServer } from 'node:http';
import { statSync, existsSync, writeFileSync, createReadStream } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { chromium } from 'playwright';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..');
const buildDir = join(repo, 'build', 'emscripten');
const RUNS = Number(process.env.DET_RUNS || 3);
const NAME = 'determinism_smoke';
const MIME = { '.js': 'text/javascript', '.wasm': 'application/wasm', '.html': 'text/html' };

assert.ok(existsSync(join(buildDir, `${NAME}.js`)),
  `${NAME}.js was not built — run scripts/build-wasm.sh (or the GPU suite) first`);

writeFileSync(join(buildDir, `${NAME}.html`),
  `<!doctype html><canvas id=canvas width=4 height=4></canvas><script src="${NAME}.js"></script>`);

const server = createServer((req, res) => {
  const p = decodeURIComponent(req.url.split('?')[0]);
  const file = join(buildDir, p === '/' ? 'index.html' : p);
  let st; try { st = statSync(file); } catch { res.writeHead(404).end(); return; }
  res.writeHead(200, { 'Content-Type': MIME[file.slice(file.lastIndexOf('.'))] || 'application/octet-stream',
    'Content-Length': st.size });
  createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;

const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
let browser;
for (const opts of [{ channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }, {}]) {
  try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch { /* next */ }
}
assert.ok(browser, 'could not launch Chromium');

const digests = [];
try {
  for (let i = 0; i < RUNS; i++) {
    // A fresh context per run, not just a fresh page: a shared context would carry over GPU
    // program caches and defeat the point of running more than once.
    const ctx = await browser.newContext();
    const page = await ctx.newPage();
    await page.goto(`${base}/${NAME}.html`);
    const err = await page.waitForFunction(() => window.__gpu, null, { timeout: 60000 })
      .then((h) => h.jsonValue());
    assert.ok(!err.error, `run ${i + 1}: ${err.error}`);
    const det = await page.waitForFunction(() => window.__det, null, { timeout: 60000 })
      .then((h) => h.jsonValue());
    digests.push(det.digest);
    console.log(`  run ${i + 1}/${RUNS}: ${det.digest}`);
    await ctx.close();
  }
} finally {
  await browser.close();
  server.close();
}

const unique = [...new Set(digests)];
assert.equal(unique.length, 1,
  `nondeterministic render across fresh contexts: ${JSON.stringify(digests)}`);
console.log(`determinism: ${RUNS} runs, digest ${unique[0]} — stable`);
```

- [ ] **Step 6: Run the runner**

```bash
node scripts/determinism.mjs
```
Expected: three lines each printing the same 8-hex digest, then
`determinism: 3 runs, digest <hex> — stable`. If the digests differ, that is a genuine finding —
report the differing values rather than raising `DET_RUNS` until it agrees.

- [ ] **Step 7: Wire it into CI**

In `scripts/ci.sh`, between the `phase2 GPU smokes` block and the final `echo "ALL GREEN"`, add:

```bash
echo "== determinism harness =="
node scripts/determinism.mjs
```

It must run after the GPU suite, which is what builds the wasm the runner loads.

- [ ] **Step 8: Full CI**

```bash
bash scripts/ci.sh
```
Expected: `ALL GREEN`, with the determinism section printing its three matching digests.

- [ ] **Step 9: Commit**

```bash
git add runtime/test/frame_digest.h runtime/test/determinism_smoke.cpp scripts/determinism.mjs \
        scripts/ci.sh CMakeLists.txt web-runtime/test/phase2.gpu.test.mjs
git commit -m "test: determinism harness — repeatable render digest, in-process and across contexts"
```

---

### Task 10: Documentation capstone

Four tiers changed the runtime's coverage semantics, added five smokes, and closed the last Phase
3 and Phase 4 items. Regenerate what is generated, then state the new rule where a porter will
find it.

**Files:**
- Modify: `docs/CONFORMANCE.md` (regenerated), `docs/ROADMAP.md`, `docs/SDK_REFERENCE.md`
- Modify: `llms-full.txt` (regenerated)

**Interfaces:**
- Consumes: everything above.
- Produces: nothing.

- [ ] **Step 1: Regenerate the conformance matrix**

```bash
cd ~/projects/personal/dx8wasm && source ~/emsdk/emsdk_env.sh && node scripts/conformance.mjs
git diff docs/CONFORMANCE.md
```
Expected, in the probed tables: `D3DRS_FILLMODE(SOLID)` ✅ beside `D3DRS_FILLMODE(WIREFRAME)` ⚠️,
and `D3DRS_SPECULARMATERIALSOURCE(MATERIAL)` ✅ beside `(COLOR2)` ⚠️. Read the whole diff — the
count in each table header changes too.

- [ ] **Step 2: Add the new curated rows**

In `scripts/conformance.mjs`'s `features` array, add:

```js
  ['Anisotropic filtering', 'D3DTSS_MAXANISOTROPY via EXT_texture_filter_anisotropic, clamped to the device limit', 'yes', 'accepted_states_smoke'],
  ['Accepted-without-acting states', 'FILLMODE(SOLID), PATCHSEGMENTS, SOFTWAREVERTEXPROCESSING, RANGEFOGENABLE, 6x BUMPENV* — no-op with a written reason, not counted', 'yes', 'accepted_states_smoke'],
  ['Vertex blending (D3DFVF_XYZB1-5)', 'not implemented, but now instrumented so a capture can measure it', 'no', 'vertexblend_smoke'],
  ['Fog usage telemetry', 'every fog-mode transition recorded, so "fog unused" is falsifiable', 'yes', 'fogmode_smoke'],
  ['Determinism harness', 'repeatable framebuffer digest, in-process repeat + fresh-context runs', 'yes', 'determinism_smoke / scripts/determinism.mjs'],
```

Then regenerate again: `node scripts/conformance.mjs`.

- [ ] **Step 3: Update the two zero-hit rows this plan invalidated**

`docs/CONFORMANCE.md`'s "Zero-hit findings" table is generated from `docs/measured-gap.json`, so
edit the JSON, not the Markdown. In the `rstate` negative result's `meaning`, append:

```
 As of the 2026-08-01 instrumentation work this limitation is closed going forward: every fog-mode transition now emits a d3d8.fogmode.{table,vertex}.<hex> telemetry counter regardless of value, so a future capture CAN distinguish 'relies on linear fog' from 'never touches fog'. This row still reports what the 2026-07-31 capture could see, which is not that.
```

In `measured-gap.json`, do **not** alter any `totalHits` or `byScenario` number — those are
measurements. Add a note to `provenance` instead:

```json
  "dispositionNote": "The tokens dispositioned 'Implement' (FILLMODE, SPECULARMATERIALSOURCE, MAXANISOTROPY) and 'No-op (documented)' (PATCHSEGMENTS, SOFTWAREVERTEXPROCESSING, RANGEFOGENABLE, 6x BUMPENV*) were all actioned on 2026-08-01 — see docs/superpowers/plans/2026-08-01-close-the-remaining-docs-items.md. A re-capture against the current SDK would therefore show a different, smaller set. The counts here are the 2026-07-31 measurement and are left untouched."
```

Then `node scripts/conformance.mjs` once more so the Markdown picks the JSON up.

- [ ] **Step 4: Close the roadmap items**

In `docs/ROADMAP.md`:

Replace the Phase 3 "Remaining fixed-function work" bullet's pointer (added in Task 2) and its
`**Implement:**` / `**No-op** …` sub-bullets with:

```markdown
- ✅ **Measured-gap tail closed (2026-08-01)** — every token in
  [`measured-gap.json`](measured-gap.json) is now actioned, per
  [`superpowers/plans/2026-08-01-close-the-remaining-docs-items.md`](superpowers/plans/2026-08-01-close-the-remaining-docs-items.md).
  Implemented: `D3DRS_FILLMODE` (value-sensitive — `SOLID` is exact, `WIREFRAME`/`POINT` keep
  reporting because GLES3 has no `glPolygonMode`), `D3DTSS_MAXANISOTROPY`
  (`EXT_texture_filter_anisotropic`, clamped to the device limit),
  `D3DRS_SPECULARMATERIALSOURCE` (`MATERIAL`/`COLOR1`; `COLOR2` still reports, since
  `D3DFVF_SPECULAR` is not uploaded as an attribute). Accepted-and-ignored with a reason at each
  call site: `D3DRS_PATCHSEGMENTS`, `D3DRS_SOFTWAREVERTEXPROCESSING`, `D3DRS_RANGEFOGENABLE`, and
  the six `D3DTSS_BUMPENV*` states — `D3DTOP_BUMPENVMAP` stays unimplemented and reported, so the
  prerequisite op remains the signal. All verified by `accepted_states_smoke`.
- ✅ **Both instrumentation blind spots closed:** vertex blending (`D3DFVF_XYZB1-5`) now has a
  coverage counter of its own (`vertexblend_smoke`) — it previously had none, so its absence from
  a capture proved nothing — and fog-mode *transitions* now emit telemetry regardless of value
  (`fogmode_smoke`), so "fog unused" is falsifiable rather than merely unobserved.
```

Replace the Phase 4 line:

```markdown
- Remaining: determinism harness stub (for games with replays).
```

with:

```markdown
- ✅ **Determinism harness done:** `determinism_smoke` digests one fixed render sequence twice
  in-process (catches state left dirty by the first pass) and `scripts/determinism.mjs` compares
  the digest across fresh browser contexts (catches uninitialised memory and iteration-order-
  dependent shader-cache keys). Both run in `ci.sh`. A game with replays extends the same seam by
  digesting its own per-tick simulation state. `runtime/test/frame_digest.h` is the reusable
  FNV-1a-over-`glReadPixels` helper.

**Phase 4 complete.** Every non-parked phase is now closed; the open list is exactly the two
parked phases plus compatlib's grow-on-demand tiers.
```

- [ ] **Step 5: Document the rule in the SDK reference**

In `docs/SDK_REFERENCE.md`, immediately after the existing "Stubs fail loudly" section, add:

```markdown
### Accepted without acting ≠ unimplemented

A coverage counter means one thing: *this backend does not implement the token and fell back*.
Some D3D8 states are deliberately accepted and ignored, and those must **not** count — the
distinction is what keeps `dx8wasm_get_coverage` useful as a work list.

- `D3DRS_FILLMODE(D3DFILL_SOLID)` is exactly what the backend draws, so accepting it is exact.
  `WIREFRAME`/`POINT` still count: GLES3 has no `glPolygonMode`, so they genuinely cannot be
  expressed.
- `D3DRS_PATCHSEGMENTS`, `D3DRS_SOFTWAREVERTEXPROCESSING`, `D3DRS_RANGEFOGENABLE` and the six
  `D3DTSS_BUMPENV*` states are no-ops with the reason written at each call site in
  `runtime/d3d8webgl/device.cpp`.

Why it matters: while these shared a counter with real gaps, `D3DRS_PATCHSEGMENTS` — a float bit
pattern the engine smuggles through a render state, not a rendering request at all — was the
most-hit token in every scenario of a real capture, outranking every genuine finding. Ranking by
hit count is only meaningful once decisions stop being counted as gaps.

When you add a no-op, write the reason at the call site and add it to `accepted_states_smoke`,
which asserts that the accepted set moves no counter *and* that a genuinely-unexpressible token
still does — a smoke that only checked the first half could pass by silencing everything.

### Determinism seam

`runtime/test/frame_digest.h` folds a `glReadPixels` into a chained FNV-1a digest.
`determinism_smoke` uses it to prove one render sequence repeats identically in-process, and
`scripts/determinism.mjs` (in `ci.sh`) proves the digest reproduces across fresh browser
contexts. A game with replays extends the same pattern by digesting its own per-tick simulation
state and comparing across runs — that is the desync check, and this is the SDK-side half of it.
```

- [ ] **Step 6: Regenerate the LLM bundle**

```bash
bash scripts/gen-llms-full.sh
git diff --stat llms-full.txt
```
Expected: `llms-full.txt` picks up the CONFORMANCE / ROADMAP / SDK_REFERENCE changes.

- [ ] **Step 7: Full CI**

```bash
bash scripts/ci.sh
```
Expected: `ALL GREEN`.

- [ ] **Step 8: Commit**

```bash
git add docs/ scripts/conformance.mjs llms-full.txt
git commit -m "docs: close the Phase 3 measured-gap tail and Phase 4, document the no-op rule"
```

- [ ] **Step 9: Report what this plan did not do**

State plainly, in the session's closing summary, the four things deliberately left open, so none
of them is mistaken for finished work:

1. **`D3DFVF_SPECULAR` attribute upload** — Task 5 handles the render state but the specular
   vertex colour is still dropped; `D3DMCS_COLOR2` reports rather than works. Needs a new shader
   variant and a smoke that can distinguish a specular vertex colour from a material one.
2. **Re-capture against the current SDK** — every disposition in `measured-gap.json` was actioned,
   so a fresh three-scenario capture would show a different, smaller gap. The numbers in that file
   are the 2026-07-31 measurement and were deliberately not touched.
3. **The skirmish scenario's provenance asterisk** — its numbers were transcribed from a prior
   run's stdout rather than captured live. A provenance caveat, not a wrong number.
4. **The parked and grow-on-demand items** — Phase 5 (WebGPU), Phase 6 (second game), compatlib
   Tier 3 D3DX textures/shaders and Tier 4, plus local-viewer lighting, >8 lights, EXP/EXP2 fog,
   and cube/volume/render-target surfaces. All have zero measured demand; each phase's unpark
   condition is in `ROADMAP.md`.

---

## Self-Review

**Spec coverage** — every open item found in the `docs/` audit maps to a task:

| Open item (source) | Task |
|---|---|
| `CONFORMANCE.md` curated table: "Second texture stage ❌" contradicts `device.cpp:453,595` + `ff_shader.cpp:241` | 1 |
| `CONFORMANCE.md`: "Textures: level 0, nearest, clamp" contradicts the filter/mip work | 1 |
| 11 of 31 smokes verify features with no row in the matrix | 1 |
| `FVF: SPECULAR` row states "no" where the stride *is* honored | 1 |
| `honest-stubs.md`: 8 tasks unchecked, all implemented | 2 |
| `texture-surfaces.md`: 4 tasks unchecked, all implemented | 2 |
| ROADMAP Phase 3 "Implement: `D3DRS_FILLMODE`" (19,392 hits) | 3 |
| ROADMAP Phase 3 "Implement: `D3DTSS_MAXANISOTROPY`" | 4 |
| ROADMAP + `coverage_smoke` coupling: the probe token must move when implemented | 3 (verified unnecessary), 4 (done) |
| ROADMAP Phase 3 "Implement: `D3DRS_SPECULARMATERIALSOURCE`" | 5 |
| ROADMAP Phase 3 "No-op, and say why in the code" × 9 tokens | 6 |
| `measured-gap.json`: vertex blend "this measurement does not speak to it" — no instrument exists | 7 |
| `CONFORMANCE.md` zero-hit findings: the fog counter cannot prove fog is unused | 8 |
| ROADMAP Phase 4 "Remaining: determinism harness stub" | 9 |
| Regenerate the generated docs after all of the above | 10 |
| Deliberately deferred (parked phases, compatlib tiers, re-capture, `D3DFVF_SPECULAR` upload) | 10, Step 9 — recorded, not silently dropped |

**Placeholder scan:** no "TBD", "similar to Task N", or "add error handling" — every code step
carries the actual code. The three places that say "read the existing file first"
(`ff_shader.cpp`'s `emisFromVertex` idiom in Task 5 Step 5; the `D3DFOG_LINEAR` value in Task 8
Step 2; the `D3DTOP_BUMPENVMAP` enumerator in Task 6 Step 3) each name the exact symbol to match
and the exact grep that resolves it, because inventing a second idiom or a wrong enumerator value
is the failure mode there.

**Type consistency:**
- `total()` — defined in Task 3's `accepted_states_smoke.cpp`, reused by Tasks 4, 5, 6.
- `StageState::maxAniso` (`uint32_t`) — added in Task 4 Step 6, read in Step 7 and in
  `apply_sampler`. Both initialisers updated in the same step, so the aggregate-init stays valid.
- `aniso_limit() -> float`, returning `0.0f` when unavailable — one definition (Task 4 Step 5),
  one call site (Step 7).
- `Device8::specularSource` (`uint32_t`, default `D3DMCS_MATERIAL`) — Task 5 Steps 3, 4, 5.
- `ff::Key::specFromVertex` (`bool`) — added in Task 5 Step 5 to `ff_shader.h:40` and set in
  `device.cpp`; matches the existing `diffFromVertex`/`ambFromVertex`/`emisFromVertex` naming.
  **Step 5b is load-bearing:** `ff_shader.cpp:312` packs those bools into the program-cache key,
  so an unpacked bit means the material-sourced and vertex-sourced programs collide on one cache
  entry and the first one compiled serves both. That failure looks like "the state does nothing",
  which is the same symptom as not implementing it at all — and no existing smoke would catch it,
  since `specular_smoke` only ever exercises the material path.
- `coverage::unhandled_vertex_format(uint32_t)` and `dx8wasm_coverage::unhandled_vertex_formats` —
  declared in Task 7 Steps 5–6, called in Step 7, read by the smoke in Step 1. `Family::FVF = 4`
  extends `kTelKind` and both `static_assert`s in the same step, so a partial edit fails to
  compile rather than indexing out of bounds.
- `digest::fnv1a_framebuffer(uint32_t, int, int) -> uint32_t` and `digest::kSeed` — Task 9 Step 1,
  used in Step 2's `render_sequence()` and its degenerate-value check.
- `window.__det = { digest }` — published in Task 9 Step 2, consumed in Step 5. Separate from
  `window.__gpu` on purpose: the GPU suite's ±2 pixel tolerance must never see a digest.

**One risk worth naming:** Task 4 programs a GL sampler parameter on every `apply_sampler` call,
which is the hottest texture path in the backend. The guard is `aniso_limit()`'s cached static —
the extension is resolved once, and when it is absent the block compiles down to one comparison
against a cached float. The five existing texture smokes are what would catch a regression here,
and Task 4 Step 8 requires them green by name rather than just "all tests pass".
