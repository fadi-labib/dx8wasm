// SPDX-License-Identifier: GPL-3.0-only
// Headless capture of the GeneralsX wasm engine. Spawns the engine's serve-game.mjs,
// loads it in headless Chromium (SwiftShader), waits, screenshots the canvas + dumps console.
//   node scratch-capture.mjs                       (menu)
//   MAP='Maps\\Armored Fury.map' WAIT=45000 node scratch-capture.mjs
import { spawn } from 'node:child_process';
import { chromium } from 'playwright';
import { writeFileSync } from 'node:fs';

const ENG = process.env.ENG || '/home/fla/projects/personal/generals-dx8wasm';
const PORT = 8123;
const WAIT = Number(process.env.WAIT || 45000);
const OUT = process.env.OUT || '/tmp/gx-shot.png';

const srv = spawn('node', ['scripts/serve-game.mjs'], {
  cwd: ENG, env: { ...process.env, PORT: String(PORT) }, stdio: 'inherit',
});
const done = () => { try { srv.kill('SIGKILL'); } catch {} };
process.on('exit', done);

await new Promise(r => setTimeout(r, 1500));           // let the server bind

const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
let browser;
for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
  try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch { /* next */ }
}
if (!browser) { console.error('could not launch Chromium'); done(); process.exit(1); }
const VW = Number(process.env.VW || 1024), VH = Number(process.env.VH || 768);
const ctx = await browser.newContext({ viewport: { width: VW, height: VH } });
const page = await ctx.newPage();
const logs = [];
page.on('console', m => logs.push(`[${m.type()}] ${m.text()}`));
page.on('pageerror', e => logs.push(`[pageerror] ${e.message}`));

await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'load', timeout: 30000 });
console.log(`loaded; waiting ${WAIT}ms for engine boot + render...`);
await page.waitForTimeout(WAIT);

// Scroll the camera into the map interior. A -file skirmish has no player start, so
// the camera boots at the map corner (only edge/border cells visible). Arrow keys
// scroll the tactical camera. SCROLL='r30,d20' => 30 right presses then 20 down.
const SCROLL = process.env.SCROLL || '';
if (SCROLL) {
  await page.locator('canvas').first().click({ position: { x: 512, y: 384 } }).catch(() => {});
  for (const part of SCROLL.split(',')) {
    const key = { r: 'ArrowRight', l: 'ArrowLeft', u: 'ArrowUp', d: 'ArrowDown' }[part[0]];
    const n = Number(part.slice(1)) || 0;
    for (let i = 0; i < n && key; i++) { await page.keyboard.press(key); await page.waitForTimeout(40); }
  }
  await page.waitForTimeout(2000);   // let terrain/shroud settle after the move
}
// RTS mouse edge-scroll. EDGE='r' | 'l' | 'u' | 'd' — park cursor at that screen
// edge and hold for EDGE_MS so the engine's edge-scroll pushes the camera inward.
const EDGE = process.env.EDGE || '';
if (EDGE) {
  const pt = { r: [1022, 384], l: [2, 384], u: [512, 2], d: [512, 766] }[EDGE] || [1022, 384];
  const ms = Number(process.env.EDGE_MS || 6000);
  await page.mouse.move(pt[0], pt[1]);
  for (let t = 0; t < ms; t += 200) { await page.mouse.move(pt[0], pt[1]); await page.waitForTimeout(200); }
  await page.mouse.move(512, 384);
  await page.waitForTimeout(1500);
}

await page.screenshot({ path: OUT });
console.log(`\n=== screenshot -> ${OUT} ===`);

// Dump the last chunk of interesting console lines (errors, GL, display, texture, bitdepth).
const interesting = logs.filter(l => /error|fail|gl|display|texture|bit.?depth|format|R5G6B5|16-bit|colou?r mode|resolution|width|height|BackBuffer|stencil|shadow/i.test(l));
writeFileSync('/tmp/gx-console.log', logs.join('\n'));
console.log(`console lines: ${logs.length} total (full -> /tmp/gx-console.log). Interesting tail:`);
console.log(interesting.slice(-40).join('\n'));

await browser.close();
done();
process.exit(0);
