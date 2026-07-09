// SPDX-License-Identifier: GPL-3.0-only
// Headless check of client-side asset onboarding (onboard.js). The native folder
// picker can't be automated, but the logic that matters — validate + recursive
// filtered import into OPFS — is exercised by using an OPFS-backed directory as
// a stand-in "install folder": it implements the same FileSystemDirectoryHandle
// interface showDirectoryPicker() returns.
//
// Run: node web-runtime/test/onboard.browser.test.mjs
import { createServer } from 'node:http';
import { statSync, createReadStream } from 'node:fs';
import { join, dirname, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { chromium } from 'playwright';

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = join(here, '..');
const MIME = { '.js': 'text/javascript', '.html': 'text/html', '.wasm': 'application/wasm', '.json': 'application/json' };

let server, browser;
try {
  server = createServer((req, res) => {
    const p = decodeURIComponent(req.url.split('?')[0]);
    const file = join(webRoot, normalize(p === '/' ? 'index.html' : p));
    let st; try { st = statSync(file); } catch { res.writeHead(404).end(); return; }
    res.writeHead(200, { 'Content-Type': MIME[file.slice(file.lastIndexOf('.'))] || 'application/octet-stream',
      'Content-Length': st.size });
    createReadStream(file).pipe(res);
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;

  browser = await launchChromium();
  const page = await (await browser.newContext()).newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  await page.goto(`${base}/onboard.html`);

  const report = await page.evaluate(async () => {
    const { GENERALS, validateInstall, importInstall, isImported } = await import('/onboard.js');

    // Build a fake install in OPFS. `included` = what GENERALS.include should keep.
    const bytes = (seed, n) => Uint8Array.from({ length: n }, (_, i) => (i * seed + 7) & 0xff);
    const included = {
      'GeneralsZH.big': bytes(3, 900),
      'INIZH.big': bytes(5, 400),
      'arial.ttf': bytes(7, 120),
      'Data/INI/GameData.ini': bytes(11, 200),
      'Maps/Alpine/map.txt': bytes(13, 64),
    };
    const excluded = {
      'readme.txt': bytes(2, 30),
      'uninstall.exe': bytes(4, 50),
      'Manual/manual.pdf': bytes(6, 70),
    };

    const root = await navigator.storage.getDirectory();
    await root.removeEntry('fake-install', { recursive: true }).catch(() => {});
    await root.removeEntry(GENERALS.opfsRoot, { recursive: true }).catch(() => {});
    const src = await root.getDirectoryHandle('fake-install', { create: true });
    const put = async (path, data) => {
      const parts = path.split('/');
      let d = src;
      for (let i = 0; i < parts.length - 1; i++) d = await d.getDirectoryHandle(parts[i], { create: true });
      const w = await (await d.getFileHandle(parts.at(-1), { create: true })).createWritable();
      await w.write(data); await w.close();
    };
    for (const [p, d] of [...Object.entries(included), ...Object.entries(excluded)]) await put(p, d);

    const valid = await validateInstall(src, GENERALS);
    const result = await importInstall(src, GENERALS);

    // Read back everything under the target OPFS root.
    async function* walk(dir, prefix = '') {
      for await (const [name, h] of dir.entries()) {
        const path = prefix ? `${prefix}/${name}` : name;
        if (h.kind === 'directory') yield* walk(h, path);
        else yield { path, name, h };
      }
    }
    const target = await root.getDirectoryHandle(GENERALS.opfsRoot);
    const targetPaths = [];
    const mismatches = [];
    let markerFiles = null;
    for await (const { path, h } of walk(target)) {
      if (path === '.dx8wasm-complete') { markerFiles = JSON.parse(await (await h.getFile()).text()).files; continue; }
      targetPaths.push(path);
      const got = new Uint8Array(await (await h.getFile()).arrayBuffer());
      const want = included[path];
      if (!want || got.length !== want.length || got.some((b, i) => b !== want[i])) mismatches.push(path);
    }
    targetPaths.sort();

    return {
      valid: valid.ok,
      importedFiles: result.files,
      targetPaths,
      leakedExcluded: targetPaths.filter((p) => p in excluded),
      mismatches,
      markerFiles,
      isImported: await isImported(GENERALS),
    };
  });

  assert.ok(errors.length === 0, `page errors: ${errors.join('\n')}`);
  assert.equal(report.valid, true, 'install should validate (has a top-level .big)');
  assert.deepEqual(report.targetPaths,
    ['Data/INI/GameData.ini', 'GeneralsZH.big', 'INIZH.big', 'Maps/Alpine/map.txt', 'arial.ttf'],
    'exactly the included files should land in OPFS');
  assert.deepEqual(report.leakedExcluded, [], 'no excluded files should be imported');
  assert.deepEqual(report.mismatches, [], 'imported bytes must match source exactly');
  assert.equal(report.importedFiles, 5, 'import result count');
  assert.equal(report.markerFiles, 5, 'completion marker records file count');
  assert.equal(report.isImported, true, 'isImported() true after import');

  console.log('ok — 5 files imported to OPFS byte-exact, 3 junk files filtered out, marker + isImported set');
} finally {
  await browser?.close();
  server?.close();
}

async function launchChromium() {
  for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
    try { return await chromium.launch({ headless: true, ...opts }); } catch { /* next */ }
  }
  throw new Error('could not launch Chromium');
}
