// SPDX-License-Identifier: GPL-3.0-only
// Headless end-to-end check of the browser asset pipeline: pack a fixture,
// serve it (Range + COOP/COEP), drive loader.js in real Chromium, and assert
// every file lands in OPFS byte-exact (compared by SHA-256), that the page is
// cross-origin isolated, and that a reload hits the OPFS cache (no re-stream).
//
// Run: node web-runtime/test/loader.browser.test.mjs
import { execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync, statSync, createReadStream } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import { chromium } from 'playwright';

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = join(here, '..');
const packer = join(webRoot, '..', 'asset-tools', 'pack.py');
const sha = (buf) => createHash('sha256').update(buf).digest('hex');

const MIME = { '.js': 'text/javascript', '.mjs': 'text/javascript', '.html': 'text/html',
  '.wasm': 'application/wasm', '.json': 'application/json' };

const dir = mkdtempSync(join(tmpdir(), 'gaxd-browser-'));
let server, browser;
try {
  // ── fixture + pack ──
  const fixtures = {
    'menu.txt': Buffer.from('dx8wasm browser loader'),
    'Data/pattern.bin': Buffer.concat(Array(12000).fill(Buffer.from([...Array(256).keys()]))), // ~3MB → multi-segment
    'Data/nested/rand.dat': Buffer.from(Array.from({ length: 40000 }, (_, i) => (i * 2654435761) & 0xff)),
    'empty.dat': Buffer.alloc(0),
  };
  for (const [rel, buf] of Object.entries(fixtures)) {
    const fp = join(dir, 'src', rel);
    mkdirSync(dirname(fp), { recursive: true });
    writeFileSync(fp, buf);
  }
  const assets = join(dir, 'assets');
  mkdirSync(assets);
  execFileSync('python3', [packer, join(dir, 'src'), join(assets, 'build.data'), '1'],
    { env: { ...process.env, DX8WASM_PACK_CACHE: join(dir, 'cache') } });

  // ── static + Range server, cross-origin isolated ──
  const roots = { '/assets/': assets };
  server = createServer((req, res) => {
    const urlPath = decodeURIComponent(req.url.split('?')[0]);
    let file;
    if (urlPath.startsWith('/assets/')) file = join(assets, normalize(urlPath.slice('/assets/'.length)));
    else file = join(webRoot, normalize(urlPath === '/' ? 'index.html' : urlPath));
    let st;
    try { st = statSync(file); } catch { res.writeHead(404).end('nope'); return; }
    const ext = file.slice(file.lastIndexOf('.'));
    const headers = {
      'Content-Type': MIME[ext] || 'application/octet-stream',
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
      'Cross-Origin-Resource-Policy': 'cross-origin',
      'Accept-Ranges': 'bytes',
    };
    const range = req.headers.range;
    if (range) {
      const m = /bytes=(\d+)-(\d*)/.exec(range);
      const start = +m[1];
      const end = m[2] ? +m[2] : st.size - 1;
      res.writeHead(206, { ...headers, 'Content-Range': `bytes ${start}-${end}/${st.size}`,
        'Content-Length': end - start + 1 });
      createReadStream(file, { start, end }).pipe(res);
    } else {
      res.writeHead(200, { ...headers, 'Content-Length': st.size });
      createReadStream(file).pipe(res);
    }
  });
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;

  // ── drive the loader in a real browser ──
  browser = await launchChromium();
  const page = await (await browser.newContext()).newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });

  await page.goto(`${base}/index.html?archive=/assets/build.data`);
  const result = await page.waitForFunction(() => window.__loadResult, null, { timeout: 30000 })
    .then((h) => h.jsonValue());
  assert.ok(!result.error, `loader error: ${result.error}\nconsole: ${errors.join('\n')}`);
  assert.equal(result.coi, true, 'page should be crossOriginIsolated');
  assert.equal(result.cached, false, 'first load should stream, not cache-hit');
  assert.equal(result.fileCount, Object.keys(fixtures).length, 'file count in OPFS');

  // ── verify OPFS bytes by hashing each file in-page ──
  const hash = result.meta.contentHash;
  for (const [rel, buf] of Object.entries(fixtures)) {
    const got = await page.evaluate(async ({ hash, rel }) => {
      const root = await navigator.storage.getDirectory();
      let d = await root.getDirectoryHandle('gaxd-' + hash);
      const parts = rel.split('/');
      for (let i = 0; i < parts.length - 1; i++) d = await d.getDirectoryHandle(parts[i]);
      const fh = await d.getFileHandle(parts[parts.length - 1]);
      const ab = await (await fh.getFile()).arrayBuffer();
      const digest = await crypto.subtle.digest('SHA-256', ab);
      return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, '0')).join('');
    }, { hash, rel });
    assert.equal(got, sha(buf), `OPFS content mismatch: ${rel}`);
  }

  // ── reload: must hit the OPFS cache ──
  await page.goto(`${base}/index.html?archive=/assets/build.data`);
  const reload = await page.waitForFunction(() => window.__loadResult, null, { timeout: 30000 })
    .then((h) => h.jsonValue());
  assert.equal(reload.cached, true, 'second load should hit the OPFS cache');

  console.log(`ok — ${result.fileCount} files byte-exact in OPFS, crossOriginIsolated, cache-hit on reload`);
} finally {
  await browser?.close();
  server?.close();
  rmSync(dir, { recursive: true, force: true });
}

async function launchChromium() {
  for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
    try { return await chromium.launch({ headless: true, ...opts }); } catch { /* try next */ }
  }
  throw new Error('could not launch a Chromium (tried bundled, chrome channel, /usr/bin/google-chrome)');
}
