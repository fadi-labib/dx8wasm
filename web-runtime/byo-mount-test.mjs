// SPDX-License-Identifier: GPL-3.0-only
// Verify the bring-your-own-assets OPFS -> MEMFS mount (web/byo-assets.js).
//
// BYO ships no game data: the player picks their Zero Hour folder once, it is copied into
// OPFS, and on every later boot Module.preRun copies OPFS back into the emscripten FS. That
// second half is the part that runs on every launch, and it could NEVER have worked before
// -sEXPORTED_RUNTIME_METHODS=FS was added -- `Module.FS.writeFile` aborted with
// "'FS' was not exported". This test covers it.
//
// The folder picker itself needs a real user gesture and a real install, so it is not
// automated. Instead we write a synthetic asset set straight into OPFS (exactly the shape
// ingestFromFolder leaves behind, marker file included) and assert the mount copies it into
// the engine's filesystem. The engine will not get far without real .big archives -- that is
// fine and expected; the mount happens in preRun, before any of that matters.
//
// Requires a BYO build:  BYO=1 node scripts/build-static.mjs
// Usage:                 node web-runtime/byo-mount-test.mjs
import { createServer } from 'node:http';
import { createReadStream, statSync, existsSync } from 'node:fs';
import { join, extname } from 'node:path';
import { chromium } from 'playwright';

const DIST = process.env.DIST || '/home/fla/projects/personal/generals-dx8wasm/dist/GeneralsMD';
const PORT = Number(process.env.PORT || 8137);
const MARKER = '.gx-complete';

if (!existsSync(join(DIST, 'byo-assets.js'))) {
  console.error(`FAIL: no BYO build at ${DIST}\n      run: BYO=1 node scripts/build-static.mjs`);
  process.exit(1);
}

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm' };
// Serve COOP/COEP directly rather than relying on the COI service worker: the worker needs a
// reload cycle to take effect, which would race the OPFS setup below.
const srv = createServer((q, s) => {
  const p = q.url.split('?')[0];
  const f = join(DIST, p === '/' ? 'index.html' : p);
  s.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
  s.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');
  let st; try { st = statSync(f); } catch { s.writeHead(404).end(); return; }
  s.writeHead(200, { 'Content-Type': MIME[extname(f)] || 'application/octet-stream', 'Content-Length': st.size });
  createReadStream(f).pipe(s);
});
await new Promise(r => srv.listen(PORT, '127.0.0.1', r));
process.on('exit', () => { try { srv.close(); } catch {} });

const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
let browser;
for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
  try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch {}
}
if (!browser) { console.error('FAIL: could not launch Chrome'); process.exit(1); }

const ctx = await browser.newContext({ viewport: { width: 800, height: 600 } });
const page = await ctx.newPage();
page.on('console', m => { const t = m.text(); if (/byo|gxuser|FS/i.test(t)) console.log(`  [page] ${t}`); });

const failures = [];
const check = (ok, msg) => { console.log(`  ${ok ? 'ok  ' : 'FAIL'}  ${msg}`); if (!ok) failures.push(msg); };

// Pass 1: land on the origin so OPFS is reachable. With no assets stored, byo-assets.js parks
// on the folder picker and never removes its run dependency, so the engine stays put.
console.log(`serving ${DIST} on http://127.0.0.1:${PORT}/`);
await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'load', timeout: 30000 });
await page.waitForTimeout(2000);

const FILES = { 'INIZH.big': 'fake-ini', 'MapsZH.big': 'fake-maps', 'Data/Scripts/probe.txt': 'fake-script' };

const seeded = await page.evaluate(async ({ files, marker }) => {
  const root = await navigator.storage.getDirectory();
  const dir = await root.getDirectoryHandle('gx-assets', { create: true });
  async function put(rel, text) {
    const parts = rel.split('/');
    let d = dir;
    for (let i = 0; i < parts.length - 1; i++) d = await d.getDirectoryHandle(parts[i], { create: true });
    const fh = await d.getFileHandle(parts[parts.length - 1], { create: true });
    const w = await fh.createWritable(); await w.write(new Blob([text])); await w.close();
  }
  for (const [rel, text] of Object.entries(files)) await put(rel, text);
  await put(marker, String(1));   // completion marker: makes hasAssets() succeed
  return Object.keys(files).length;
}, { files: FILES, marker: MARKER }).catch(e => 'threw: ' + e.message);

if (typeof seeded !== 'number') { console.error(`FAIL: could not seed OPFS: ${seeded}`); await browser.close(); process.exit(1); }
console.log(`seeded ${seeded} synthetic assets + ${MARKER} into OPFS`);

// Pass 2: reload. hasAssets() now passes, so preRun takes the mountToFS path.
console.log('reloading — preRun should now copy OPFS into the engine FS');
await page.reload({ waitUntil: 'load', timeout: 30000 });

// Poll: the mount is async inside preRun, and the engine may abort afterwards on the fake
// archives. Reading FS stays valid either way.
let found = null;
for (let t = 0; t < 60000 && !found; t += 1000) {
  await page.waitForTimeout(1000);
  found = await page.evaluate(({ files }) => {
    const FS = window.Module && window.Module.FS;
    if (!FS) return null;
    try {
      const out = {};
      for (const rel of Object.keys(files)) {
        try { out[rel] = new TextDecoder().decode(FS.readFile('/' + rel)); }
        catch (e) { out[rel] = null; }
      }
      return Object.values(out).some(v => v !== null) ? out : null;
    } catch (e) { return null; }
  }, { files: FILES }).catch(() => null);
}

console.log('\nOPFS -> engine FS mount');
if (!found) {
  check(false, 'no seeded asset appeared in the engine filesystem');
} else {
  for (const [rel, want] of Object.entries(FILES))
    check(found[rel] === want, `${rel} mounted with the right contents`);
}

console.log(`\n${failures.length ? 'FAIL' : 'PASS'}: BYO OPFS mount — ${failures.length} failing check(s)`);
await browser.close();
process.exit(failures.length ? 1 : 0);
