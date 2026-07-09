// SPDX-License-Identifier: GPL-3.0-only
// Cross-language check: the Python packer (asset-tools/pack.py) and the JS
// decoder (web-runtime/gaxd.js) must agree byte-for-byte. Uses Node's built-in
// brotli as the injected decompressor. Run: node web-runtime/test/gaxd.test.mjs
import { execFileSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { brotliDecompressSync } from 'node:zlib';
import assert from 'node:assert/strict';
import { parseHeader, decodeArchive, segmentRange } from '../gaxd.js';

const here = dirname(fileURLToPath(import.meta.url));
const packer = join(here, '..', '..', 'asset-tools', 'pack.py');
const decompress = (u8) => new Uint8Array(brotliDecompressSync(u8));

const dir = mkdtempSync(join(tmpdir(), 'gaxd-test-'));
try {
  // Fixture: files chosen to force multiple segments and a big-file boundary.
  const fixtures = {
    'menu.txt': Buffer.from('dx8wasm loader test'),
    'Data/pattern.bin': Buffer.concat(Array(12000).fill(Buffer.from([...Array(256).keys()]))), // ~3MB → multi-segment at 1MB
    'Data/nested/rand.dat': Buffer.from(Array.from({ length: 40000 }, (_, i) => (i * 2654435761) & 0xff)),
    'empty.dat': Buffer.alloc(0),
  };
  for (const [rel, buf] of Object.entries(fixtures)) {
    const fp = join(dir, 'src', rel);
    mkdirSync(dirname(fp), { recursive: true });
    writeFileSync(fp, buf);
  }
  const out = join(dir, 'out.data');
  // 128KB segments so the ~512KB file spans several segments.
  execFileSync('python3', [packer, join(dir, 'src'), out, '1'], {
    env: { ...process.env, DX8WASM_PACK_CACHE: join(dir, 'cache') },
  });

  const bytes = new Uint8Array(readFileSync(out));
  const meta = JSON.parse(readFileSync(out + '.meta.json', 'utf8'));

  // 1. Header parse agrees with the sidecar meta.
  const hdr = parseHeader(bytes);
  assert.equal(hdr.payloadOffset, meta.headerSize, 'headerSize mismatch');
  assert.equal(hdr.files.length, meta.fileCount, 'fileCount mismatch');
  assert.equal(hdr.segments.length, meta.segmentCount, 'segmentCount mismatch');
  assert.ok(hdr.segments.length > 1, 'expected multiple segments from small segment size');

  // 2. Full decode round-trips every file byte-for-byte.
  const files = decodeArchive(bytes, decompress);
  assert.equal(files.size, Object.keys(fixtures).length, 'file count mismatch');
  for (const [rel, expected] of Object.entries(fixtures)) {
    const got = files.get(rel);
    assert.ok(got, `missing ${rel}`);
    assert.deepEqual(Buffer.from(got), expected, `content mismatch: ${rel}`);
  }

  // 3. Streaming path: decode each segment from its Range span independently,
  //    exactly as the browser loader will, and reassemble.
  const total = hdr.segments.reduce((n, s) => n + s.uSize, 0);
  const blob = new Uint8Array(total);
  let uPos = 0;
  for (let i = 0; i < hdr.segments.length; i++) {
    const { start, end } = segmentRange(hdr.segments, i);
    const comp = bytes.subarray(hdr.payloadOffset + start, hdr.payloadOffset + end);
    const chunk = decompress(comp);
    blob.set(chunk, uPos);
    uPos += chunk.length;
  }
  for (const { path, offset, size } of hdr.files) {
    assert.deepEqual(Buffer.from(blob.subarray(offset, offset + size)), fixtures[path], `stream mismatch: ${path}`);
  }

  console.log(`ok — ${hdr.files.length} files, ${hdr.segments.length} segments, byte-exact (full + streamed)`);
} finally {
  rmSync(dir, { recursive: true, force: true });
}
