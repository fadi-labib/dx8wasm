// SPDX-License-Identifier: GPL-3.0-only
// Serve the spin_demo in a browser (or screenshot it headlessly with --shot).
//   node scripts/demo.mjs            -> build + serve at http://127.0.0.1:8080
//   node scripts/demo.mjs --shot out.png  -> build + render headlessly, save PNG
import { execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { resolveUnder } from './lib/static-path.mjs';
import { statSync, writeFileSync, createReadStream } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..');
const buildDir = join(repo, 'build', 'emscripten');
const MIME = { '.js': 'text/javascript', '.wasm': 'application/wasm', '.html': 'text/html' };

execFileSync('bash', [join(repo, 'scripts', 'build-wasm.sh')], { stdio: 'inherit' });

// Host page: a real 512x512 canvas wired to the emscripten Module.
writeFileSync(join(buildDir, 'spin_demo.html'),
  `<!doctype html><meta charset=utf-8><title>dx8wasm — fixed-function demo</title>` +
  `<style>body{background:#0b0d10;margin:0;display:grid;place-items:center;height:100vh;` +
  `font:14px system-ui;color:#9aa}h1{font-size:13px;font-weight:600;letter-spacing:.04em}</style>` +
  `<div style="text-align:center"><h1>dx8wasm — D3D8→WebGL2 fixed-function pipeline</h1>` +
  `<canvas id=canvas width=512 height=512 style="border:1px solid #222;border-radius:6px"></canvas>` +
  `<p>left: spinning textured quad (MODULATE) &nbsp;•&nbsp; right: lit quad, sweeping directional light</p></div>` +
  `<script>var Module={canvas:document.getElementById('canvas')};</script>` +
  `<script src="spin_demo.js"></script>`);

const shotIdx = process.argv.indexOf('--shot');
const server = createServer((req, res) => {
  const file = resolveUnder(buildDir, req.url, 'spin_demo.html');   // confined to buildDir; null = 404
  let st; try { if (!file) throw 0; st = statSync(file); } catch { res.writeHead(404).end(); return; }
  res.writeHead(200, { 'Content-Type': MIME[file.slice(file.lastIndexOf('.'))] || 'application/octet-stream',
    'Content-Length': st.size });
  createReadStream(file).pipe(res);
});

if (shotIdx !== -1) {
  const out = process.argv[shotIdx + 1];
  // Playwright is a web-runtime dev dependency; resolve it from there.
  const require = createRequire(join(repo, 'web-runtime', 'package.json'));
  const { chromium } = require('playwright');
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
  let browser, lastErr;
  for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
    try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch (e) { lastErr = e; }
  }
  if (!browser) throw lastErr;
  const page = await browser.newPage();
  await page.goto(`${base}/spin_demo.html`);
  await page.waitForTimeout(1500);   // let several frames render + the light sweep
  await page.locator('#canvas').screenshot({ path: out });
  console.log(`saved ${out}`);
  await browser.close(); server.close();
} else {
  server.listen(8080, '127.0.0.1', () => console.log('demo at http://127.0.0.1:8080  (Ctrl-C to stop)'));
}
