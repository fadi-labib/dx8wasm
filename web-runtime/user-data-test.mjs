// SPDX-License-Identifier: GPL-3.0-only
// Verify that player-owned data survives a reload on wasm.
//
// Saved games, replays and options.ini all live under TheGlobalData->getPath_UserData(),
// which on wasm is /gxuser -- an IDBFS (IndexedDB-backed) mount set up by web/user-data.js.
// Before that mount existed the engine wrote to plain MEMFS, so everything was RAM-only and
// vanished on reload. This test proves the round trip:
//
//   boot -> wait for the mount -> write into /gxuser/Save -> flush -> RELOAD -> read it back
//
// It deliberately does NOT wait for the game to finish loading assets: the mount happens in
// Module.preRun, long before the menu, so the interesting behaviour is testable in seconds.
// A pass here means saves/replays persist; the remaining question is only whether the engine's
// own save/replay UI writes to the right place, which the paths above guarantee by construction.
//
// Usage: node web-runtime/user-data-test.mjs
import { spawn } from 'node:child_process';
import { chromium } from 'playwright';

const ENG = '/home/fla/projects/personal/generals-dx8wasm';
const PORT = Number(process.env.PORT || 8131);
const PROBE = '/gxuser/Save/persist-probe.txt';
const READY_TIMEOUT = Number(process.env.READY_TIMEOUT || 120000);

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

// One context for both loads: IndexedDB is per-origin per-context, so reusing it is exactly
// what a returning player's browser does.
const ctx = await browser.newContext({ viewport: { width: 1024, height: 768 } });
const page = await ctx.newPage();
page.on('console', m => { const t = m.text(); if (/gxuser/i.test(t)) console.log(`  [page] ${t}`); });
page.on('pageerror', e => console.log(`  [pageerror] ${e.message}`));

function fail(msg) { console.error(`FAIL: ${msg}`); browser.close().finally(() => process.exit(1)); }

// The mount is complete when user-data.js has flipped gxUserDataState past 'pending'.
async function waitForMount(label) {
  await page.waitForFunction(
    () => window.gxUserDataState && window.gxUserDataState !== 'pending',
    null, { timeout: READY_TIMEOUT },
  );
  const state = await page.evaluate(() => window.gxUserDataState);
  console.log(`${label}: gxUserDataState = ${state}`);
  if (state !== 'ready') fail(`mount did not become ready (${state})`);
  return state;
}

const token = 'probe-' + Math.random().toString(36).slice(2);

// ---- pass 1: write + flush ----
console.log(`boot #1 -> http://127.0.0.1:${PORT}/`);
await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'load', timeout: 30000 });
await waitForMount('boot #1');

const wrote = await page.evaluate(async ({ probe, token }) => {
  const FS = window.Module && window.Module.FS;
  if (!FS) return 'no FS';
  try {
    FS.mkdirTree ? FS.mkdirTree('/gxuser/Save') : FS.mkdir('/gxuser/Save');
  } catch (e) { /* exists */ }
  FS.writeFile(probe, token);
  // Wait for the flush to actually reach IndexedDB rather than assuming the callback ran.
  await new Promise((resolve, reject) =>
    FS.syncfs(false, err => (err ? reject(err) : resolve())));
  return 'ok';
}, { probe: PROBE, token }).catch(e => 'threw: ' + e.message);

if (wrote !== 'ok') fail(`write/flush failed: ${wrote}`);
console.log(`wrote ${PROBE} = ${token}, flushed to IndexedDB`);

// ---- pass 2: reload and read back ----
console.log('reloading (this is the whole point — MEMFS would be empty here)');
await page.reload({ waitUntil: 'load', timeout: 30000 });
await waitForMount('boot #2');

const readBack = await page.evaluate(({ probe }) => {
  const FS = window.Module && window.Module.FS;
  if (!FS) return 'no FS';
  try { return new TextDecoder().decode(FS.readFile(probe)); }
  catch (e) { return 'missing: ' + e.message; }
}, { probe: PROBE });

if (readBack !== token) fail(`after reload, ${PROBE} = ${JSON.stringify(readBack)} (expected ${token})`);

console.log(`\nPASS: ${PROBE} survived the reload (${readBack})`);
console.log('      -> saved games, replays and options.ini now persist on wasm.');
await browser.close();
process.exit(0);
