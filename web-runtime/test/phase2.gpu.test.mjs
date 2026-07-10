// SPDX-License-Identifier: GPL-3.0-only
// Headless GPU smoke: build the wasm smokes, load each in Chromium (SwiftShader),
// assert the read-back pixel. Ordered gl -> platform -> d3d8 to isolate failures.
import { execFileSync } from 'node:child_process';
import { createServer } from 'node:http';
import { statSync, existsSync, writeFileSync, createReadStream } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import assert from 'node:assert/strict';
import { chromium } from 'playwright';

const here = dirname(fileURLToPath(import.meta.url));
const repo = join(here, '..', '..');
const buildDir = join(repo, 'build', 'emscripten');
const MIME = { '.js': 'text/javascript', '.wasm': 'application/wasm', '.html': 'text/html' };

// Build (sources emsdk, runs cmake). Fails the test loudly if the toolchain is missing.
execFileSync('bash', [join(repo, 'scripts', 'build-wasm.sh')], { stdio: 'inherit' });

// Each smoke: [ jsFile, expectedRGBA ]. Only run those that built.
const SMOKES = [
  ['gl_smoke', [51, 102, 153, 255]],
  ['platform_smoke', [51, 102, 153, 255]],
  ['d3d8_smoke', [51, 102, 204, 255]],   // 0xFF3366CC -> R=51 G=102 B=204
  ['draw_smoke', [51, 204, 102, 255]],   // FVF quad, 0xFF33CC66 -> R=51 G=204 B=102
  ['draw_tex_smoke', [128, 128, 64, 255]], // diffuse(.502,1,1) * texel(1,.502,.251) modulate
  ['coverage_smoke', [1, 1, 1, 3]],        // 1 unhandled RS/TOP/FMT each, callback fired 3x
  ['render_state_smoke', [153, 51, 102, 191]], // 50% red over blue bg (depth/cull/alpha checked internally)
  ['light_smoke', [153, 102, 51, 255]],        // directional diffuse: matDiff * lightDiff(0.6,0.4,0.2) at N·L=1
  ['multilight_smoke', [153, 102, 102, 255]],  // two directional lights sum: (0.5,0.1,0)+(0.1,0.3,0.4)=(0.6,0.4,0.4)
  ['point_light_smoke', [227, 227, 227, 255]], // point light, hitDot 0.943 * atten 0.943 = 0.889
  ['spot_light_smoke', [144, 96, 48, 255]],    // spot inside cone: diffuse(0.6,0.4,0.2) * hitDot 0.943
  ['fog_smoke', [128, 0, 128, 255]],           // linear fog: mix(blue, red, 0.5) at eye-depth 0.5
];

const server = createServer((req, res) => {
  const p = decodeURIComponent(req.url.split('?')[0]);
  const file = join(buildDir, p === '/' ? 'index.html' : p);
  let st; try { st = statSync(file); } catch { res.writeHead(404).end(); return; }
  res.writeHead(200, { 'Content-Type': MIME[file.slice(file.lastIndexOf('.'))] || 'application/octet-stream',
    'Content-Length': st.size });
  createReadStream(file).pipe(res);
});
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;

let browser;
try {
  browser = await launchChromium();
  for (const [name, expected] of SMOKES) {
    assert.ok(existsSync(join(buildDir, `${name}.js`)), `${name}.js was not built`);
    writeFileSync(join(buildDir, `${name}.html`),
      `<!doctype html><canvas id=canvas width=4 height=4></canvas>` +
      `<script>var Module={canvas:document.getElementById('canvas')};</script>` +
      `<script src="${name}.js"></script>`);
    const page = await (await browser.newContext()).newPage();
    const errors = [];
    page.on('pageerror', (e) => errors.push(String(e)));
    await page.goto(`${base}/${name}.html`);
    const r = await page.waitForFunction(() => window.__gpu, null, { timeout: 30000 }).then((h) => h.jsonValue());
    assert.ok(!r.error, `${name}: ${r.error}\n${errors.join('\n')}`);
    for (let i = 0; i < 4; i++) assert.ok(Math.abs(r.pixel[i] - expected[i]) <= 2,
      `${name} pixel[${i}] = ${r.pixel[i]}, expected ~${expected[i]}`);
    console.log(`ok — ${name} cleared to [${r.pixel}]`);
    await page.close();
  }
} finally {
  await browser?.close();
  server.close();
}

async function launchChromium() {
  const args = ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'];
  for (const opts of [{}, { channel: 'chrome' }, { executablePath: '/usr/bin/google-chrome' }]) {
    try { return await chromium.launch({ headless: true, args, ...opts }); } catch { /* next */ }
  }
  throw new Error('could not launch Chromium');
}
