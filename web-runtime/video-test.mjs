// SPDX-License-Identifier: GPL-3.0-only
// Verify Bink cinematics decode + render on wasm. Boots the game with NO -file MAP
// so m_playIntro fires the EA logo -> sizzle before the menu, then captures a burst
// of screenshots across the window the movie plays in, plus every video/FFmpeg
// console line. A working decode shows the logo/sizzle frames (not the menu) and no
// "Failed avformat_open_input" error.
import { spawn } from 'node:child_process';
import { chromium } from 'playwright';

const ENG = '/home/fla/projects/personal/generals-dx8wasm';
const PORT = 8127;
const OUT = process.env.OUT || '/home/fla/.claude-dev/jobs/1a75cd54/tmp/vid';
const START = Number(process.env.START || 56000);   // first shot after asset load
const SHOTS = Number(process.env.SHOTS || 12);
const STEP = Number(process.env.STEP || 2500);

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
const vid = [];
page.on('console', m => { const t = m.text(); if (/movie|video|ffmpeg|bink|avformat|avcodec|logo|sizzle|\.bik/i.test(t)) vid.push(`[${m.type()}] ${t}`); });
page.on('pageerror', e => vid.push(`[pageerror] ${e.message}`));
await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'load', timeout: 30000 });
console.log(`booting; first shot at ${START}ms, then ${SHOTS} x ${STEP}ms`);
await page.waitForTimeout(START);

for (let i = 0; i < SHOTS; i++) {
  try { await page.screenshot({ path: `${OUT}-${String(i).padStart(2, '0')}.png`, timeout: 8000 }); }
  catch (e) { console.log(`shot ${i}: ${e.name} (page busy = movie decoding or hung)`); }
  await page.waitForTimeout(STEP);
}

console.log('=== video/FFmpeg console lines ===');
console.log(vid.slice(-40).join('\n') || '(none)');
console.log(`shots -> ${OUT}-00..${String(SHOTS - 1).padStart(2, '0')}.png`);
await browser.close();
process.exit(0);
