// SPDX-License-Identifier: GPL-3.0-only
// N-player transport test for the wasm WebRTC LAN bridge (web-mp/netbridge.js).
//
// Only a 2-player match had ever been verified end-to-end, leaving "does 3-4 players work?"
// open. The stock engine lobby handles N players fine on a real LAN, so the wasm-specific
// risk is the transport, which at 3+ players is structurally different from 2:
//
//   - lobby.js _onMeet dials joiner<->joiner directly, so the steady state is a FULL MESH
//     (every peer holds N-1 channels), not a star. At N=2 there is only one channel, so
//     none of the mesh introduction logic is exercised at all.
//   - netbridge.js additionally keeps a host RELAY fallback for any pair that has no direct
//     channel yet (send() routes via hostVip; _onFrame re-fans broadcasts and forwards
//     unicast). At N=2 that fallback is dead code.
//
// This drives the transport directly rather than through the GUI, which keeps it
// deterministic (no click coordinates, and no 50s engine boot -- gxNet is plain main-thread
// JS, available the moment the page loads).
//
// Checks:
//   1. topology     - the mesh reaches N-1 channels on every peer, with distinct vips
//   2. broadcast    - a datagram from ANY peer reaches every other peer
//   3. unicast      - every ordered joiner->joiner pair is deliverable, and not mis-delivered
//   4. reconnect    - a joiner that drops is eventually dropped, rejoins, and is routable
//
// Payload content is irrelevant here; routing is asserted on the frame's source vip, which
// is the property under test. That lets the probe read 8 arbitrary heap bytes rather than
// writing into the engine's heap.
//
// Usage: node web-runtime/mp-mesh-n.mjs        (N=4)
//        N=3 node web-runtime/mp-mesh-n.mjs
import { spawn } from 'node:child_process';
import { chromium } from 'playwright';

const ENG = '/home/fla/projects/personal/generals-dx8wasm';
const PORT = Number(process.env.PORT || 8133);
const N = Number(process.env.N || 4);
const PROBE_HANDLE = 777;      // synthetic socket handle, well clear of the engine's
const PROBE_PORT = 9999;       // port the engine never binds
const MESH_TIMEOUT = Number(process.env.MESH_TIMEOUT || 60000);
// Recovery ladder before a peer is declared gone: keepalive 3x2s + ICE restart 8s + re-dial 8s.
const DROP_TIMEOUT = Number(process.env.DROP_TIMEOUT || 75000);

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

const failures = [];
function check(ok, msg) {
  console.log(`  ${ok ? 'ok  ' : 'FAIL'}  ${msg}`);
  if (!ok) failures.push(msg);
}

async function newPeer() {
  const ctx = await browser.newContext({ viewport: { width: 640, height: 480 } });
  const p = await ctx.newPage();
  await p.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'load', timeout: 30000 });
  await p.waitForFunction(() => window.gxNet && window.gxLobby && window.gxGenRoomKey,
    null, { timeout: 30000 });
  return p;
}

const bindProbe = (p) => p.evaluate(({ h, port }) => {
  window.gxNet.bind(h, 0, port);
}, { h: PROBE_HANDLE, port: PROBE_PORT });

const vipOf = (p) => p.evaluate(() => window.gxNet.myVip >>> 0);
const peerCount = (p) => p.evaluate(() => window.gxNet.peers.size);

// Drain and return the source vips seen on the probe socket, clearing the queue so each
// assertion starts from a known-empty state.
const drain = (p) => p.evaluate(({ h }) => {
  const s = window.gxNet.sockets.get(h);
  if (!s) return [];
  const ips = s.queue.map(d => d.ip >>> 0);
  s.queue.length = 0;
  return ips;
}, { h: PROBE_HANDLE });

const sendTo = (p, dstIP) => p.evaluate(({ h, dst, port }) => {
  // ptr=0/len=8: routing is asserted on the frame's source vip, so the bytes do not matter
  // and reading the heap avoids writing into the engine's memory.
  window.gxNet.send(h, dst >>> 0, port, 0, 8);
}, { h: PROBE_HANDLE, dst: dstIP, port: PROBE_PORT });

const BROADCAST = 0xffffffff;

// ---- bring up the room ----
console.log(`booting ${N} peers on http://127.0.0.1:${PORT}/`);
const pages = [];
for (let i = 0; i < N; i++) pages.push(await newPeer());
const [host, ...joiners] = pages;

const key = await host.evaluate(() => { const k = window.gxGenRoomKey(); window.gxLobby.host(k); return k; });
console.log(`room key: ${key}`);
for (const j of joiners) await j.evaluate(k => window.gxLobby.join(k), key);

// Wait for the full mesh: the host dials every joiner, then introduces joiners to each
// other (_onMeet), so every peer converges on N-1 channels.
let formed = false;
for (let t = 0; t < MESH_TIMEOUT && !formed; t += 2000) {
  await host.waitForTimeout(2000);
  const hp = await peerCount(host);
  const jp = await Promise.all(joiners.map(peerCount));
  formed = hp === N - 1 && jp.every(c => c === N - 1);
  if (!formed) console.log(`  …forming: host=${hp} joiners=[${jp}] (want ${N - 1} each)`);
}
if (!formed) {
  console.error('MESH DID NOT FORM — signaling is flaky, re-run before treating this as a bug');
  await browser.close(); process.exit(2);
}

console.log('\n1. topology');
check(await peerCount(host) === N - 1, `host holds ${N - 1} channels`);
for (let i = 0; i < joiners.length; i++)
  check(await peerCount(joiners[i]) === N - 1,
    `joiner${i + 1} holds ${N - 1} channels (full mesh, not star)`);

for (const p of pages) await bindProbe(p);
const vips = await Promise.all(pages.map(vipOf));
console.log(`  vips: ${vips.map(v => (v >>> 24) + '.' + ((v >>> 16) & 255) + '.' + ((v >>> 8) & 255) + '.' + (v & 255)).join(' ')}`);
check(new Set(vips).size === N && vips.every(v => v !== 0), 'every peer has a distinct non-zero vip');

// ---- 2. broadcast reaches everyone, from every sender ----
console.log('\n2. broadcast fan-out (host re-fan)');
for (let s = 0; s < N; s++) {
  for (const p of pages) await drain(p);
  await sendTo(pages[s], BROADCAST);
  await pages[s].waitForTimeout(1500);
  for (let r = 0; r < N; r++) {
    if (r === s) continue;
    const seen = await drain(pages[r]);
    check(seen.includes(vips[s] >>> 0),
      `peer${s} broadcast reached peer${r}` + (seen.length ? '' : ' (received nothing)'));
  }
}

// ---- 3. joiner -> joiner unicast, which only the host can deliver ----
console.log('\n3. joiner -> joiner unicast');
if (N >= 3) {
  for (let a = 1; a < N; a++) {
    for (let b = 1; b < N; b++) {
      if (a === b) continue;
      for (const p of pages) await drain(p);
      await sendTo(pages[a], vips[b]);
      await pages[a].waitForTimeout(1200);
      const seen = await drain(pages[b]);
      check(seen.includes(vips[a] >>> 0), `joiner${a} -> joiner${b} unicast delivered`);
      const hostSaw = await drain(host);
      check(!hostSaw.includes(vips[a] >>> 0), `host did not mis-deliver joiner${a} -> joiner${b} to itself`);
    }
  }
} else {
  console.log('  (skipped: needs N>=3)');
}

// ---- 4. reconnect ----
console.log('\n4. reconnect (drop a joiner, rejoin the same room)');
{
  // A dropped peer is not declared dead immediately: keepalive needs 3 misses (~6s), then
  // lobby.js runs an ICE restart (8s) and a full re-dial (8s) before giving up. During that
  // ladder the peer stays in gxNet.peers on purpose, so budget well past the ~22s worst case.
  const victim = joiners[joiners.length - 1];
  await victim.close();
  let dropped = false;
  for (let t = 0; t < DROP_TIMEOUT && !dropped; t += 2000) {
    await host.waitForTimeout(2000);
    dropped = (await peerCount(host)) === N - 2;
  }
  check(dropped, `host dropped the dead peer within ${DROP_TIMEOUT / 1000}s (${N - 2} channels remain)`);
  if (!dropped) {
    // A phantom peer is not cosmetic: lockstep waits on every peer it believes is present,
    // so a channel that never gets removed stalls the match for everyone still playing.
    const dump = await host.evaluate(() => [...window.gxNet.peers.entries()].map(([vip, p]) => ({
      vip: (vip >>> 24) + '.' + ((vip >>> 16) & 255) + '.' + ((vip >>> 8) & 255) + '.' + (vip & 255),
      dc: (p.dc && p.dc.readyState) || 'none',
      pc: (p.pc && p.pc.connectionState) || 'none',
      open: !!p._open,
    })));
    console.log('    host peer table:', JSON.stringify(dump));
  }

  const rejoin = await newPeer();
  pages[pages.length - 1] = rejoin;
  await rejoin.evaluate(k => window.gxLobby.join(k), key);
  let back = false;
  for (let t = 0; t < MESH_TIMEOUT && !back; t += 2000) {
    await host.waitForTimeout(2000);
    back = (await peerCount(host)) === N - 1 && (await peerCount(rejoin)) >= 1;
    if (!back) console.log(`  …rejoining: host=${await peerCount(host)} rejoin=${await peerCount(rejoin)}`);
  }
  check(back, 'rejoined peer is back in the room');

  if (back) {
    await bindProbe(rejoin);
    vips[vips.length - 1] = await vipOf(rejoin);
    for (const p of pages) await drain(p);
    await sendTo(rejoin, BROADCAST);
    await rejoin.waitForTimeout(1500);
    check((await drain(host)).includes(vips[vips.length - 1] >>> 0),
      'rejoined peer is routable again (broadcast reaches the host)');
  }
}

console.log(`\n${failures.length ? 'FAIL' : 'PASS'}: ${N}-player transport — ${failures.length} failing check(s)`);
if (failures.length) failures.forEach(f => console.log(`  - ${f}`));
await browser.close();
process.exit(failures.length ? 1 : 0);
