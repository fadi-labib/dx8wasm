// SPDX-License-Identifier: GPL-3.0-only
// Serve the integration example (or --verify it: screenshot, press an arrow key,
// screenshot again, so the input pump -> sprite motion can be eyeballed).
//   node scripts/minigame.mjs           -> build + serve at http://127.0.0.1:8081
//   node scripts/minigame.mjs --verify <before.png> <after.png>
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

writeFileSync(join(buildDir, 'minigame.html'),
  `<!doctype html><meta charset=utf-8><title>dx8wasm — integration example</title>` +
  `<style>body{background:#0b0d10;margin:0;display:grid;place-items:center;height:100vh;font:14px system-ui;color:#9aa}</style>` +
  `<div style="text-align:center"><p>arrow keys move the sprite — a game plugs in exactly like this</p>` +
  `<canvas id=canvas width=512 height=512 tabindex=0 style="border:1px solid #222;border-radius:6px"></canvas></div>` +
  `<script>var Module={canvas:document.getElementById('canvas')};</script>` +
  `<script src="minigame.js"></script>`);

const server = createServer((req, res) => {
  const file = resolveUnder(buildDir, req.url, 'minigame.html');   // confined to buildDir; null = 404
  let st; try { if (!file) throw 0; st = statSync(file); } catch { res.writeHead(404).end(); return; }
  res.writeHead(200, { 'Content-Type': MIME[file.slice(file.lastIndexOf('.'))] || 'application/octet-stream',
    'Content-Length': st.size });
  createReadStream(file).pipe(res);
});

const vi = process.argv.indexOf('--verify');
if (vi === -1) {
  server.listen(8081, '127.0.0.1', () => console.log('minigame at http://127.0.0.1:8081  (click the canvas, then arrow keys)'));
} else {
  const [before, after] = [process.argv[vi + 1], process.argv[vi + 2]];
  const require = createRequire(join(repo, 'web-runtime', 'package.json'));
  const { chromium } = require('playwright');
  await new Promise((r) => server.listen(0, '127.0.0.1', r));
  const base = `http://127.0.0.1:${server.address().port}`;
  const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
  let browser;
  for (const opts of [{ channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }, {}]) {
    try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch { /* next */ }
  }
  const page = await browser.newPage();
  await page.goto(`${base}/minigame.html`);
  await page.waitForTimeout(600);
  await page.locator('#canvas').screenshot({ path: before });
  await page.locator('#canvas').focus();
  for (let i = 0; i < 20; i++) { await page.keyboard.down('ArrowRight'); await page.waitForTimeout(20); await page.keyboard.up('ArrowRight'); }
  await page.waitForTimeout(200);
  await page.locator('#canvas').screenshot({ path: after });
  console.log(`saved ${before} and ${after}`);
  await browser.close(); server.close();
}
