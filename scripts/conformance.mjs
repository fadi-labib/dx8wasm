// SPDX-License-Identifier: GPL-3.0-only
// Build + run the conformance probe headlessly, then write docs/CONFORMANCE.md.
// The render-state / texture-op / format tables are probed empirically (coverage
// counters); the feature table is curated and paired with the verifying smoke.
import { execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { statSync, writeFileSync, readFileSync, createReadStream, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..');
const buildDir = join(repo, 'build', 'emscripten');
const MIME = { '.js': 'text/javascript', '.wasm': 'application/wasm', '.html': 'text/html' };

execFileSync('bash', [join(repo, 'scripts', 'build-wasm.sh')], { stdio: 'inherit' });

writeFileSync(join(buildDir, 'conformance.html'),
  `<!doctype html><canvas id=canvas width=4 height=4></canvas>` +
  `<script>var Module={canvas:document.getElementById('canvas')};</script>` +
  `<script src="conformance.js"></script>`);

const server = createServer((req, res) => {
  const p = decodeURIComponent(req.url.split('?')[0]);
  const file = join(buildDir, p === '/' ? 'conformance.html' : p);
  let st; try { st = statSync(file); } catch { res.writeHead(404).end(); return; }
  res.writeHead(200, { 'Content-Type': MIME[file.slice(file.lastIndexOf('.'))] || 'application/octet-stream',
    'Content-Length': st.size });
  createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;

const require = createRequire(join(repo, 'web-runtime', 'package.json'));
const { chromium } = require('playwright');
const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
let browser;
for (const opts of [{ channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }, {}]) {
  try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch { /* next */ }
}
const page = await browser.newPage();
await page.goto(`${base}/conformance.html`);
const probed = await page.waitForFunction(() => window.__conf, null, { timeout: 30000 }).then((h) => h.jsonValue());
await browser.close(); server.close();
const data = JSON.parse(probed);
if (data.error) throw new Error(data.error);

// Curated feature coverage (paired with the smoke that verifies it).
const features = [
  ['COM ABI', 'full IDirect3DDevice8 vtable (~94 methods, canonical order); rest stubbed->coverage', 'yes', 'abi_smoke'],
  ['Device / present', 'Direct3DCreate8, CreateDevice, Clear, Present, Begin/EndScene, Reset', 'yes', 'd3d8_smoke / abi_smoke'],
  ['Vertex/index buffers', 'CreateVertexBuffer/IndexBuffer, Lock/Unlock, SetStreamSource, SetIndices', 'yes', 'draw_smoke'],
  ['FVF: XYZ / NORMAL / DIFFUSE / TEX1', 'attribute layout in FVF order', 'yes', 'draw_smoke / light_smoke'],
  ['FVF: XYZRHW (pre-transformed)', 'screen-space UI/HUD vertices (rhw=1)', 'yes', 'rhw_smoke'],
  ['FVF: SPECULAR', 'stride honored so uv offsets stay correct; the colour itself is dropped', 'partial', 'draw_smoke'],
  ['FVF: multi-texcoord (TEX2)', 'second uv set feeds stage 1 (attribute location 4)', 'yes', 'lit_tex_smoke'],
  ['Draw: indexed + non-indexed + user-pointer', 'DrawIndexedPrimitive/DrawPrimitive/DrawPrimitiveUP/DrawIndexedPrimitiveUP, all topologies', 'yes', 'draw_smoke / strip_smoke / drawup_smoke'],
  ['Transforms: WORLD / VIEW / PROJECTION', 'row-major uploaded transposed', 'yes', 'draw_smoke'],
  ['Textures: LockRect upload', 'full mip chain; per-stage filter + address state honored', 'yes', 'draw_tex_smoke'],
  ['Texture combiners (stage 0)', 'MODULATE/2X/4X, ADD, ADDSIGNED, SELECTARG1/2', 'yes', 'draw_tex_smoke / combiner_smoke'],
  ['Texture formats: 16-bit + DXT1', 'A4R4G4B4/R5G6B5/A8/L8 decode + S3TC block upload', 'yes', 'texfmt_smoke / dxt_smoke'],
  ['Surfaces', 'GetSurfaceLevel, CreateImageSurface, CopyRects, UpdateTexture', 'yes', 'surface_smoke'],
  ['Material colour source', 'D3DRS_{DIFFUSE,AMBIENT,EMISSIVE}MATERIALSOURCE + D3DRS_COLORVERTEX', 'yes', 'matsource_smoke'],
  ['World-space normals', 'normal matrix follows a non-identity world transform', 'yes', 'normal_smoke'],
  ['Honest stubs', 'unimplemented entry points refuse; caps derive from the implementation', 'yes', 'honest_stubs_smoke / caps_query_smoke'],
  ['Telemetry ring', 'NDJSON spans + coalesced counters, drop-accounted', 'yes', 'telemetry_smoke'],
  ['compatlib Tiers 0-3', 'timing, file/dir/memory, module/thread/registry, D3DX math', 'yes', 'compat_smoke / compat_file_smoke / compat_sys_smoke / compat_d3dx_smoke'],
  ['Lit + textured geometry', 'lit color modulated by stage-0 texture (terrain/units)', 'yes', 'lit_tex_smoke'],
  ['Second texture stage', 'multi-texture (base + lightmap/detail); stages 0-1 chained in the combiner', 'yes', 'lit_tex_smoke / combiner_smoke'],
  ['Render states: depth / blend / cull / alpha-test', 'the common subset', 'yes', 'render_state_smoke'],
  ['Lighting: directional / point / spot', 'all three D3DLIGHT types', 'yes', 'light/point/spot_light_smoke'],
  ['Lighting: ambient + diffuse + specular', 'full FF equation (infinite viewer)', 'yes', 'light/specular_smoke'],
  ['Lighting: local viewer, >8 lights', 'infinite viewer only; 8-light cap', 'no', '—'],
  ['Fog: linear', 'eye-space depth blend', 'yes', 'fog_smoke'],
  ['Fog: EXP / EXP2', 'flagged via coverage', 'no', '—'],
  ['Coverage / fallback layer', 'dx8wasm_get_coverage + unhandled callback', 'yes', 'coverage_smoke'],
  ['Anisotropic filtering', 'D3DTSS_MAXANISOTROPY via EXT_texture_filter_anisotropic, clamped to the device limit', 'yes', 'accepted_states_smoke'],
  ['Accepted-without-acting states', 'FILLMODE(SOLID), PATCHSEGMENTS, SOFTWAREVERTEXPROCESSING, RANGEFOGENABLE, 6x BUMPENV* — no-op with a written reason, not counted', 'yes', 'accepted_states_smoke'],
  ['Vertex blending (D3DFVF_XYZB1-5)', 'not implemented, but now instrumented so a capture can measure it', 'no', 'vertexblend_smoke'],
  ['Fog usage telemetry', 'every fog-mode transition recorded, so "fog unused" is falsifiable', 'yes', 'fogmode_smoke'],
  ['Determinism harness', 'repeatable framebuffer digest, in-process repeat + fresh-context runs', 'yes', 'determinism_smoke / scripts/determinism.mjs'],
];

const mark = (h) => (h ? '✅ handled' : '⚠️ fallback');
const featMark = { yes: '✅', partial: '🟡', no: '❌' };
const rows = (arr) => arr.map((e) => `| \`${e.name}\` | ${mark(e.handled)} |`).join('\n');
const count = (arr) => `${arr.filter((e) => e.handled).length}/${arr.length}`;

// Optional additional section: what a real game's telemetry captured actually
// asked for, as opposed to the tables above (which the conformance program
// itself probes on a live device). This file is entirely optional — the SDK
// must not hard-depend on a measurement taken against any one game — so its
// absence must reproduce the exact output this generator produced before this
// section existed. See docs/measured-gap.json for the data shape and its own
// provenance block (source repo/doc/commit, scenarios, capture caveats).
const measuredGapPath = join(repo, 'docs', 'measured-gap.json');
let measuredSection = '';
if (existsSync(measuredGapPath)) {
  const measured = JSON.parse(readFileSync(measuredGapPath, 'utf8'));
  const { provenance, tokens, negativeResults } = measured;
  const dispositionLabel = { implement: 'Implement', 'no-op': 'No-op (documented)' };
  const measuredRows = tokens
    .slice()
    .sort((a, b) => b.totalHits - a.totalHits)
    .map((t) => {
      const name = t.standardName + (t.declaredInHeader ? '' : ` *(not declared in this SDK's \`d3d8.h\`)*`);
      const disp = dispositionLabel[t.disposition] || t.disposition;
      return `| \`d3d8.unhandled.${t.kind}.${t.hex}\` | ${name} | ${t.totalHits} | ${disp} | ${t.reason} |`;
    })
    .join('\n');

  // Zero-hit findings: things the coverage layer *can* instrument (a token/op/format
  // kind exists) but that never fired their counter in any of the three scenarios.
  // That silence is itself a result — "provably never asked for", not "unmeasured" —
  // but only for kinds the coverage layer actually watches; see docs/measured-gap.json's
  // own note on each entry for what the zero hit count does and does not prove.
  const negativeSection = (negativeResults && negativeResults.length)
    ? `
### Zero-hit findings

These are also measured, not probed — but instead of "this token fired N times",
each row here is "this coverage-instrumented token/op/format never fired at all,
in any of the three scenarios". Read each row's own caveat before treating a zero
as proof of non-use; several of these prove less than they might first appear to.

| Kind | Describes | Meaning |
|---|---|---|
${negativeResults.map((n) => `| \`${n.kind}\` | ${n.describes} | ${n.meaning} |`).join('\n')}
`
    : '';

  measuredSection = `
## Measured against a real target (not empirically probed)

**This is a capture, not a probe — do not read it as an empirically probed row.**
Every table above is produced by this repo's own conformance program setting each
token on a live device and reading its own coverage counters back, so it cannot
drift from what the runtime does. The table below instead reports what one real
game's telemetry recorded it asking for during real play — a measurement taken
outside this repo and hand-copied in via \`docs/measured-gap.json\`. A row here
says "a target hit this token N times", never "this SDK's probe confirmed this".

Measured against **${provenance.measuredAgainst}**, scenarios: ${provenance.scenarios.join(', ')}.
Source: \`${provenance.sourceRepo}/${provenance.sourceDoc}\` @ \`${provenance.sourceCommit.slice(0, 12)}\`
(commit dated ${provenance.sourceCommitDate}). ${provenance.note}

| Token | Standard D3D8 name | Total hits (all scenarios) | Disposition | Why |
|---|---|---|---|---|
${measuredRows}
${negativeSection}`;
}

const md = `# Conformance matrix

Generated by \`node scripts/conformance.mjs\`. The three token tables below are
**probed empirically** — the conformance program sets each token on a live
device and reads the coverage counters to decide handled vs fallback, so this
report can't drift from what the runtime actually does. The feature table is
curated and paired with the pixel smoke that verifies it.

Scope: the fixed-function subset a DirectX-8 game (target: C&C Generals) needs.
"fallback" means the token is safely ignored/substituted and logged via the
coverage layer — never silently wrong.

## Render states (${count(data.renderStates)} handled)

| State | Status |
|-------|--------|
${rows(data.renderStates)}

## Texture-stage color ops (${count(data.textureOps)} handled)

| Op | Status |
|----|--------|
${rows(data.textureOps)}

## Texture formats (${count(data.formats)} handled)

| Format | Status |
|--------|--------|
${rows(data.formats)}

## Feature coverage

| Feature | Notes | Status | Verified by |
|---------|-------|--------|-------------|
${features.map(([f, n, s, t]) => `| ${f} | ${n} | ${featMark[s]} ${s} | ${t} |`).join('\n')}
${measuredSection}`;

writeFileSync(join(repo, 'docs', 'CONFORMANCE.md'), md);
console.log(`wrote docs/CONFORMANCE.md — render states ${count(data.renderStates)}, ` +
  `ops ${count(data.textureOps)}, formats ${count(data.formats)}` +
  (measuredSection ? ', + measured-gap section' : ''));
