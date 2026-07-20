// SPDX-License-Identifier: GPL-3.0-only
// 2-instance in-game LAN test: host + joiner share a mesh room, both enter the LAN
// LOBBY, host CREATE GAME, then check the joiner's game list for the host's game
// (in-game LAN discovery over gxNet broadcast). Screenshots both.
import { spawn } from 'node:child_process';
import { chromium } from 'playwright';

const ENG = '/home/fla/projects/personal/generals-dx8wasm';
const PORT = 8125;
const BOOT = Number(process.env.BOOT || 52000);
const OUT = process.env.OUT || '/tmp/mp2p';

const srv = spawn('node', ['scripts/serve-game.mjs'], { cwd: ENG, env: { ...process.env, PORT: String(PORT) }, stdio: 'ignore' });
process.on('exit', () => { try { srv.kill('SIGKILL'); } catch {} });
await new Promise(r => setTimeout(r, 1500));

const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
let browser;
for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
  try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch {}
}
async function mk() {
  const ctx = await browser.newContext({ viewport: { width: 1024, height: 768 } });
  const p = await ctx.newPage();
  await p.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'load', timeout: 30000 });
  return p;
}
const A = await mk(), B = await mk();
console.log('booting both instances...');
await A.waitForTimeout(BOOT);

// Routing diagnostic: record every unicast destination gxNet.send targets, and every
// frame the host receives that it cannot deliver (dstIP != myVip and not a peer).
// Undeliverable frames mean a peer is aiming packets at the wrong IP (the LAN-IP bug);
// a healthy run shows sentDst on both the lobby port (8086) and the game port (8088)
// with undeliverable == {}.
const inject = (p) => p.evaluate(() => {
  const n = window.gxNet; if (!n || n.__diagOn) return; n.__diagOn = true;
  n._diag = { sentDst: {}, undeliverable: {}, myVip: 0 };
  const ip2s = window.gxVipToString;
  const origSend = n.send.bind(n);
  n.send = function (h, dstIP, dstPort, ptr, len) {
    dstIP = dstIP >>> 0;
    if (dstIP !== 0xffffffff) { const k = ip2s(dstIP) + ':' + dstPort; n._diag.sentDst[k] = (n._diag.sentDst[k] || 0) + 1; }
    return origSend(h, dstIP, dstPort, ptr, len);
  };
  const origFrame = n._onFrame.bind(n);
  n._onFrame = function (buf) {
    n._diag.myVip = n.myVip >>> 0;
    if (buf.byteLength >= 12) {
      const dv = new DataView(buf); const dstIP = dv.getUint32(6, true) >>> 0; const dstPort = dv.getUint16(10, true);
      const bcast = dstIP === 0xffffffff, forMe = bcast || dstIP === (n.myVip >>> 0);
      if (n.isHost && !bcast && !forMe && !n.peers.get(dstIP)) {
        const k = ip2s(dstIP) + ':' + dstPort; n._diag.undeliverable[k] = (n._diag.undeliverable[k] || 0) + 1;
      }
    }
    return origFrame(buf);
  };
});
await Promise.all([inject(A), inject(B)]);

// Host on A, join on B (same room key)
const key = await A.evaluate(() => { const k = window.gxGenRoomKey(); window.gxLobby.host(k); return k; });
console.log('room key:', key);
await B.evaluate(k => window.gxLobby.join(k), key);
// Poll for the WebRTC mesh to come up (MQTT rendezvous latency is variable).
let meshA = 0, meshB = 0;
for (let t = 0; t < 40000; t += 2000) {
  await A.waitForTimeout(2000);
  meshA = await A.evaluate(() => window.gxNet.peers.size);
  meshB = await B.evaluate(() => window.gxNet.peers.size);
  if (meshA >= 1 && meshB >= 1) break;
}
console.log(`mesh peers -> A:${meshA} B:${meshB}`);
if (!(meshA >= 1 && meshB >= 1)) { console.log('MESH DID NOT CONNECT — aborting (flaky signaling, re-run)'); await browser.close(); process.exit(2); }

// Navigate both: MULTIPLAYER -> NETWORK (LAN LOBBY)
async function toLan(p) {
  const cv = p.locator('canvas').first(); const box = await cv.boundingBox();
  const click = async (x, y, w) => { await p.mouse.click(box.x + x / 1024 * box.width, box.y + y / 768 * box.height); await p.waitForTimeout(w); };
  await click(823, 221, 4000);   // MULTIPLAYER
  await click(823, 221, 8000);   // NETWORK -> LAN LOBBY
}
await Promise.all([toLan(A), toLan(B)]);
await A.screenshot({ path: `${OUT}-A-lobby.png` });
await B.screenshot({ path: `${OUT}-B-lobby.png` });

// A: CREATE GAME (bottom-left button ~175,671)
{ const cv = A.locator('canvas').first(); const box = await cv.boundingBox();
  await A.mouse.click(box.x + 175 / 1024 * box.width, box.y + 671 / 768 * box.height); }
await A.waitForTimeout(9000);
await A.screenshot({ path: `${OUT}-A-hostgame.png` });
await B.screenshot({ path: `${OUT}-B-gamelist.png` });   // host's game visible here?

// B joins the host's game. First give B a DISTINCT player name (both instances
// default to "emscripten"; the host rejects a duplicate name), then select the
// game entry and JOIN GAME.
{ const cv = B.locator('canvas').first(); const box = await cv.boundingBox();
  const bclick = async (x, y, w) => { await B.mouse.click(box.x + x / 1024 * box.width, box.y + y / 768 * box.height); await B.waitForTimeout(w); };
  await bclick(582, 117, 500);    // CLEAR the Player Name field
  await bclick(404, 117, 500);    // focus the Player Name field
  await B.keyboard.type('joiner2', { delay: 60 });
  await B.waitForTimeout(500);
  await bclick(390, 177, 1500);   // select the host's game in the Games list
  await bclick(398, 671, 10000);  // JOIN GAME -> should enter the GAME OPTIONS lobby as a player
}
await B.screenshot({ path: `${OUT}-B-joined.png` });
await A.screenshot({ path: `${OUT}-A-afterjoin.png` });   // host lobby should now show 2 players

// Launch the match: joiner ACCEPTs (ready-up), host clicks PLAY GAME. Then wait for
// both to load the map into the in-game view (units + HUD) over the lockstep transport.
if (process.env.LAUNCH === '1') {
  const clickAt = async (p, x, y, w) => { const cv = p.locator('canvas').first(); const box = await cv.boundingBox();
    await p.mouse.click(box.x + x / 1024 * box.width, box.y + y / 768 * box.height); await p.waitForTimeout(w); };
  await clickAt(B, 175, 682, 3000);    // ACCEPT (joiner ready)
  await clickAt(A, 175, 682, 3000);    // PLAY GAME (host launches)
  await A.waitForTimeout(35000);        // map load + unit spawn + lockstep start (slow under SwiftShader)
  await A.screenshot({ path: `${OUT}-A-ingame.png` });
  await B.screenshot({ path: `${OUT}-B-ingame.png` });
  const st = async (p) => p.evaluate(() => ({ peers: window.gxNet.peers.size, drops: window.gxNet.dropsInbound }));
  console.log('in-game gxNet -> A:', JSON.stringify(await st(A)), 'B:', JSON.stringify(await st(B)));
}

// gxNet broadcast state on the joiner (LAN discovery uses UDP broadcast over the mesh)
const bDrops = await B.evaluate(() => ({ peers: window.gxNet.peers.size, drops: window.gxNet.dropsInbound }));
console.log('joiner gxNet:', JSON.stringify(bDrops));
const diagA = await A.evaluate(() => window.gxNet._diag);
const diagB = await B.evaluate(() => window.gxNet._diag);
console.log('HOST(A) diag:', JSON.stringify(diagA));
console.log('JOIN(B) diag:', JSON.stringify(diagB));
console.log('done -> screenshots at', OUT + '-*.png');
await browser.close();
process.exit(0);