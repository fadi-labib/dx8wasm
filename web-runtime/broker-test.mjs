// SPDX-License-Identifier: GPL-3.0-only
// Fast unit test for the local MQTT-over-WebSocket broker (web-mp/local-broker.mjs).
// Boots serve-game.mjs, opens the page (signaling.js loads), and drives two
// GxMqttClient instances through the LOCAL broker only: subscribe + publish +
// verify fan-out (incl. a large SDP-sized payload). No engine boot, no public brokers.
import { spawn } from 'node:child_process';
import { chromium } from 'playwright';

const ENG = '/home/fla/projects/personal/generals-dx8wasm';
const PORT = 8126;
const srv = spawn('node', ['scripts/serve-game.mjs'], { cwd: ENG, env: { ...process.env, PORT: String(PORT) }, stdio: 'inherit' });
process.on('exit', () => { try { srv.kill('SIGKILL'); } catch {} });
await new Promise(r => setTimeout(r, 1500));

const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
let browser;
for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
  try { browser = await chromium.launch({ headless: true, args, ...opts }); break; } catch {}
}
const page = await browser.newPage();
page.on('console', m => { const t = m.text(); if (/broker|mqtt|TEST/i.test(t)) console.log('  page:', t); });
await page.goto(`http://127.0.0.1:${PORT}/`, { waitUntil: 'domcontentloaded', timeout: 30000 });

// Wait for signaling.js to define GxMqttClient + the local broker to be first in the list.
await page.waitForFunction(() => typeof window.GxMqttClient === 'function' && window.gxNetConfig
  && String(window.gxNetConfig.mqttBrokers[0]).endsWith('/mqtt'), { timeout: 10000 });
const brokerUrl = await page.evaluate(() => window.gxNetConfig.mqttBrokers[0]);
console.log('local broker url:', brokerUrl);

const result = await page.evaluate(async (url) => {
  const mkClient = () => new window.GxMqttClient([url]);   // force LOCAL broker only
  const a = mkClient(), b = mkClient();
  await a.connect(); await b.connect();
  const got = [];
  const topic = 'p2pt/TEST-ROOM/req';
  b.subscribe(topic, (t, m) => got.push(m));
  await new Promise(r => setTimeout(r, 300));              // let SUBSCRIBE land
  const big = 'x'.repeat(4000);                            // SDP-sized (>125, <65536)
  a.publish(topic, { t: 'join', _from: 'A', big });
  a.publish(topic, { t: 'ice', _from: 'A', n: 2 });
  await new Promise(r => setTimeout(r, 500));              // let fan-out arrive
  return {
    count: got.length,
    firstType: got[0] && got[0].t,
    bigOk: got[0] && got[0].big && got[0].big.length === 4000,
    secondType: got[1] && got[1].t,
  };
}, brokerUrl);

console.log('TEST result:', JSON.stringify(result));
const pass = result.count === 2 && result.firstType === 'join' && result.bigOk && result.secondType === 'ice';
console.log(pass ? 'BROKER TEST: PASS' : 'BROKER TEST: FAIL');
await browser.close();
process.exit(pass ? 0 : 1);