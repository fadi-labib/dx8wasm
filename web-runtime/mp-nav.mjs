// SPDX-License-Identifier: GPL-3.0-only
// Navigate the menu by clicking a sequence and screenshotting after each step.
// CLICKS='823,221,4000;x,y,wait' — canvas-relative coords (auto-scaled to backing).
import { spawn } from 'node:child_process';
import { chromium } from 'playwright';

const ENG = '/home/fla/projects/personal/generals-dx8wasm';
const PORT = 8124;
const BOOT = Number(process.env.BOOT || 50000);
const OUT = process.env.OUT || '/home/fla/.claude-dev/jobs/1a75cd54/tmp/mpnav';
const CLICKS = (process.env.CLICKS || '').split(';').filter(Boolean);

const srv = spawn('node', ['scripts/serve-game.mjs'], { cwd: ENG, env: { ...process.env, PORT: String(PORT) }, stdio: 'ignore' });
process.on('exit', () => { try { srv.kill('SIGKILL'); } catch {} });
await new Promise(r => setTimeout(r, 1500));

const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
let browser;
for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
  try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch {}
}
const ctx = await browser.newContext({ viewport: { width: 1024, height: 768 } });
const page = await ctx.newPage();
const logs = [];
page.on('console', m => logs.push(`[${m.type()}] ${m.text()}`));
await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'load', timeout: 30000 });
console.log(`boot ${BOOT}ms...`);
await page.waitForTimeout(BOOT);

const cv = page.locator('canvas').first();
await page.screenshot({ path: `${OUT}-0.png` });
let i = 1;
for (const c of CLICKS) {
  const [x, y, w] = c.split(',').map(Number);
  // Click at canvas-relative coords: the canvas is centered; use bounding box.
  const box = await cv.boundingBox();
  const px = box.x + (x / 1024) * box.width;
  const py = box.y + (y / 768) * box.height;
  await page.mouse.click(px, py);
  console.log(`click ${x},${y} -> page ${Math.round(px)},${Math.round(py)}`);
  await page.waitForTimeout(w || 3000);
  await page.screenshot({ path: `${OUT}-${i}.png` });
  i++;
}
console.log('MP overlay/gxNet:', await page.evaluate(() => ({ gxNet: typeof window.gxNet, lobby: typeof window.gxLobby })));
console.log(`console tail:\n` + logs.filter(l=>/lan|network|multiplayer|game|error|fail|gxnet/i.test(l)).slice(-15).join('\n'));
await browser.close();
process.exit(0);