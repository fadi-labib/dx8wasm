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
import { createRequire } from 'node:module';
import assert from 'node:assert/strict';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..');
// scripts/ has no node_modules of its own; resolve playwright the same way the other
// root-level browser scripts (conformance.mjs, demo.mjs, minigame.mjs) do.
const require = createRequire(join(repo, 'web-runtime', 'package.json'));
const { chromium } = require('playwright');
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
