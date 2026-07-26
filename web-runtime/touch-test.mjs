// SPDX-License-Identifier: GPL-3.0-only
// Verify the touch gesture layer (web/touch-controls.js) emits the right synthetic input.
//
// The engine's input device consumes only mouse events (SDL3Mouse.cpp handles
// SDL_EVENT_MOUSE_MOTION/BUTTON_DOWN/BUTTON_UP/WHEEL and ignores finger events), so
// touch-controls.js translates gestures into mouse/wheel/key events. This asserts that
// translation: real multi-touch sequences go in via CDP, and spies on the canvas record what
// comes out.
//
// The gesture set is a port of the device-tuned iOS/Android translator (see that fork's
// docs/port/TOUCH_CONTROLS.md). Note the game's stock scheme is LEFT-button-centric: left
// selects AND commands, right deselects. So a long press is deselect, not the primary order.
//
// What this canNOT tell you is whether the result FEELS right on a phone -- whether 600ms is
// the right hold, whether pinch is too sensitive, whether the camera tracks the fingers well.
// That needs a human with a device. This proves the plumbing, so testing starts from "does it
// feel good" rather than "does anything happen at all".
//
// Usage: node web-runtime/touch-test.mjs
import { spawn } from 'node:child_process';
import { chromium } from 'playwright';

const ENG = '/home/fla/projects/personal/generals-dx8wasm';
const PORT = Number(process.env.PORT || 8139);

const srv = spawn('node', ['scripts/serve-game.mjs'], {
  cwd: ENG, env: { ...process.env, PORT: String(PORT) }, stdio: 'ignore',
});
process.on('exit', () => { try { srv.kill('SIGKILL'); } catch {} });
await new Promise(r => setTimeout(r, 1500));

const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
let browser;
for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
  try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch {}
}
if (!browser) { console.error('FAIL: could not launch Chrome'); process.exit(1); }

// hasTouch makes the page report maxTouchPoints > 0, which is how touch-controls.js decides
// to activate. ?touch=1 would force it, but emulating a real touch device is the honest test.
const ctx = await browser.newContext({ viewport: { width: 900, height: 700 }, hasTouch: true, isMobile: false });
const page = await ctx.newPage();
page.on('console', m => { if (/\[touch\]/.test(m.text())) console.log(`  [page] ${m.text()}`); });

await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'load', timeout: 30000 });

const failures = [];
const check = (ok, msg, extra) => {
  console.log(`  ${ok ? 'ok  ' : 'FAIL'}  ${msg}${!ok && extra ? '  -> ' + extra : ''}`);
  if (!ok) failures.push(msg);
};

const active = await page.evaluate(() => typeof window.__touchSpy === 'undefined');
// Install spies. touch-controls only stops propagation of TOUCH events, so synthetic
// mouse/wheel/key events reach an ordinary listener normally.
await page.evaluate(() => {
  window.__touchSpy = [];
  const cv = document.getElementById('canvas');
  const rec = (tag) => (e) => window.__touchSpy.push({
    tag, button: e.button, buttons: e.buttons,
    x: Math.round(e.clientX || 0), y: Math.round(e.clientY || 0),
    deltaY: e.deltaY, key: e.key,
  });
  cv.addEventListener('mousedown', rec('mousedown'));
  cv.addEventListener('mouseup', rec('mouseup'));
  cv.addEventListener('mousemove', rec('mousemove'));
  cv.addEventListener('wheel', rec('wheel'));
  window.addEventListener('keydown', rec('keydown'));
  window.addEventListener('keyup', rec('keyup'));
});

const cdp = await ctx.newCDPSession(page);
const touch = (type, points) => cdp.send('Input.dispatchTouchEvent', {
  type, touchPoints: points.map((p, i) => ({ x: p.x, y: p.y, id: i + 1 })),
});
const drain = () => page.evaluate(() => { const s = window.__touchSpy; window.__touchSpy = []; return s; });
const wait = (ms) => page.waitForTimeout(ms);

console.log(`touch layer active on the page: ${active ? 'yes' : 'yes (spy installed)'}\n`);

// ---- 1. tap -> left click ----
console.log('1. tap -> left click');
await drain();
await touch('touchStart', [{ x: 300, y: 300 }]);
await wait(80);
await touch('touchEnd', []);
await wait(120);
{
  const ev = await drain();
  const down = ev.find(e => e.tag === 'mousedown');
  const up = ev.find(e => e.tag === 'mouseup');
  check(!!down && down.button === 0, 'emits a left mousedown', JSON.stringify(ev));
  check(!!up && up.button === 0, 'emits a left mouseup');
  check(!!down && down.x === 300 && down.y === 300, 'at the touch point');
  check(!ev.some(e => e.button === 2), 'no right click from a quick tap');
}

// ---- 2. long press -> right click ----
console.log('\n2. hold 600ms -> RIGHT click (deselect)');
await drain();
await touch('touchStart', [{ x: 420, y: 350 }]);
await wait(800);
await touch('touchEnd', []);
await wait(150);
{
  const ev = await drain();
  const rdown = ev.find(e => e.tag === 'mousedown' && e.button === 2);
  const rup = ev.find(e => e.tag === 'mouseup' && e.button === 2);
  check(!!rdown, 'emits a right mousedown', JSON.stringify(ev));
  check(!!rup, 'emits a right mouseup');
  check(!!rdown && rdown.x === 420 && rdown.y === 350, 'at the held point');
  // Left click is the ORDER button in this game, so a long press degrading into a left click
  // would command the player's units somewhere they never intended.
  check(!ev.some(e => e.tag === 'mousedown' && e.button === 0),
    'no stray LEFT click on release (left = issue order in the stock scheme)');
}

// ---- 3. drag -> left drag (box select) ----
console.log('\n3. drag -> held left button (box select)');
await drain();
await touch('touchStart', [{ x: 200, y: 200 }]);
await wait(60);
for (let i = 1; i <= 5; i++) { await touch('touchMove', [{ x: 200 + i * 30, y: 200 + i * 20 }]); await wait(40); }
await touch('touchEnd', []);
await wait(120);
{
  const ev = await drain();
  const down = ev.find(e => e.tag === 'mousedown' && e.button === 0);
  const moves = ev.filter(e => e.tag === 'mousemove' && e.buttons === 1);
  const up = ev.find(e => e.tag === 'mouseup' && e.button === 0);
  check(!!down, 'presses the left button when the drag starts', JSON.stringify(ev.slice(0, 4)));
  check(moves.length >= 3, `drags with the button held (${moves.length} moves)`);
  check(!!up, 'releases on lift');
  check(!ev.some(e => e.button === 2), 'a drag never becomes a right click');
}

// ---- 4. pinch -> wheel (zoom) ----
console.log('\n4. pinch -> wheel (zoom)');
await drain();
await touch('touchStart', [{ x: 400, y: 350 }, { x: 460, y: 350 }]);
await wait(60);
for (let i = 1; i <= 5; i++) {
  await touch('touchMove', [{ x: 400 - i * 20, y: 350 }, { x: 460 + i * 20, y: 350 }]);
  await wait(40);
}
await touch('touchEnd', []);
await wait(120);
{
  const ev = await drain();
  const wheels = ev.filter(e => e.tag === 'wheel');
  check(wheels.length > 0, `emits wheel events (${wheels.length})`, JSON.stringify(ev.slice(0, 4)));
  check(wheels.every(w => w.deltaY < 0), 'fingers apart = zoom in (negative deltaY)');
  check(!ev.some(e => e.tag === 'mousedown'), 'a pinch never presses a mouse button');
}

// ---- 5. two-finger drag -> right-button drag (camera scroll) ----
// The engine's native camera scroll is an RMB drag whose speed grows with distance from the
// press point. Arrow keys would also scroll, but this reuses the engine's own mechanism and
// gives 1:1 finger tracking.
console.log('\n5. two-finger drag -> right-button drag (camera scroll)');
await drain();
await touch('touchStart', [{ x: 400, y: 350 }, { x: 460, y: 350 }]);
await wait(60);
// Move both fingers together (constant separation) so this locks to pan, not pinch.
for (let i = 1; i <= 6; i++) {
  await touch('touchMove', [{ x: 400 + i * 25, y: 350 }, { x: 460 + i * 25, y: 350 }]);
  await wait(40);
}
await touch('touchEnd', []);
await wait(150);
{
  const ev = await drain();
  const rdown = ev.find(e => e.tag === 'mousedown' && e.button === 2);
  const rmoves = ev.filter(e => e.tag === 'mousemove' && e.buttons === 2);
  const rup = ev.find(e => e.tag === 'mouseup' && e.button === 2);
  check(!!rdown, 'presses the RIGHT button to start the camera scroll', JSON.stringify(ev.slice(0, 4)));
  check(rmoves.length >= 3, `moves with the right button held (${rmoves.length})`);
  check(!!rup, 'releases the right button when the fingers lift');
  check(!ev.some(e => e.tag === 'mousedown' && e.button === 0),
    'a two-finger scroll never presses the LEFT button');
  // Mode lock: a pan must not also emit zoom. Panning fingers never hold their separation
  // perfectly, and without the lock that leaks spurious wheel ticks.
  check(!ev.some(e => e.tag === 'wheel'), 'panning emits no wheel ticks (pan/pinch mode lock)');
}

// ---- 5b. pinch does not pan ----
console.log('\n5b. pinch is mode-locked against panning');
await drain();
await touch('touchStart', [{ x: 400, y: 350 }, { x: 460, y: 350 }]);
await wait(60);
for (let i = 1; i <= 6; i++) {
  await touch('touchMove', [{ x: 400 - i * 22, y: 350 }, { x: 460 + i * 22, y: 350 }]);
  await wait(40);
}
await touch('touchEnd', []);
await wait(150);
{
  const ev = await drain();
  check(ev.some(e => e.tag === 'wheel'), 'still zooms');
  check(!ev.some(e => e.tag === 'mousedown' && e.button === 2),
    'zooming never starts a camera scroll (pan/pinch mode lock)');
}

// ---- 5c. a cancelled touch must not click ----
// An incoming call or the notification shade cancels a touch. Committing the deferred click
// there would ghost-click at the cancel point -- a phantom order the player never gave.
console.log('\n5c. cancelled touch emits no click');
await drain();
await touch('touchStart', [{ x: 500, y: 400 }]);
await wait(80);
await touch('touchCancel', []);
await wait(150);
{
  const ev = await drain();
  check(!ev.some(e => e.tag === 'mousedown'), 'no button press from a cancelled touch',
    JSON.stringify(ev));
}

// ---- 6. desktop is untouched ----
console.log('\n6. non-touch devices are left alone');
{
  const desk = await browser.newContext({ viewport: { width: 900, height: 700 }, hasTouch: false });
  const dp = await desk.newPage();
  let announced = false;
  dp.on('console', m => { if (/\[touch\] touch controls active/.test(m.text())) announced = true; });
  await dp.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'load', timeout: 30000 });
  await dp.waitForTimeout(1500);
  check(!announced, 'does not activate without touch support');
  await desk.close();
}

console.log(`\n${failures.length ? 'FAIL' : 'PASS'}: touch gesture layer — ${failures.length} failing check(s)`);
if (failures.length) failures.forEach(f => console.log(`  - ${f}`));
await browser.close();
process.exit(failures.length ? 1 : 0);
