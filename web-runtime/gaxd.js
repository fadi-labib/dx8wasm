// SPDX-License-Identifier: GPL-3.0-only
// GAXD v2 archive decoder — pure, dependency-free, runs in browser and Node.
//
// Brotli decompression is *injected* (the browser has no native brotli, and we
// don't want to hard-wire a specific wasm decoder here). Pass a synchronous
// `decompress(Uint8Array) => Uint8Array`. See GAXD_FORMAT.md for the layout.

const MAGIC = 0x47415844; // 'GAXD'
const VERSION = 2;

// Read one ULEB128 from `buf` at `pos`. Returns { value, pos }.
function readUleb(buf, pos) {
  let result = 0, shift = 0, b;
  do {
    b = buf[pos++];
    result += (b & 0x7f) * 2 ** shift; // * 2**shift, not <<: stays exact past 31 bits
    shift += 7;
  } while (b & 0x80);
  return { value: result, pos };
}

// Parse the header (magic, file index, segment table). `buf` must contain at
// least the whole header; in the browser fetch `meta.headerSize` bytes first.
// Returns { version, files:[{path,offset,size}], segments:[{uSize,cSize}], payloadOffset }.
export function parseHeader(buf) {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const magic = dv.getUint32(0, true);
  const version = dv.getUint32(4, true);
  if (magic !== MAGIC) throw new Error(`not a GAXD archive (magic ${magic.toString(16)})`);
  if (version !== VERSION) throw new Error(`unsupported GAXD version ${version}`);

  let pos = 8, r;
  r = readUleb(buf, pos); const fileCount = r.value; pos = r.pos;
  const files = [];
  const dec = new TextDecoder();
  for (let i = 0; i < fileCount; i++) {
    r = readUleb(buf, pos); const plen = r.value; pos = r.pos;
    const path = dec.decode(buf.subarray(pos, pos + plen)); pos += plen;
    r = readUleb(buf, pos); const offset = r.value; pos = r.pos;
    r = readUleb(buf, pos); const size = r.value; pos = r.pos;
    files.push({ path, offset, size });
  }
  r = readUleb(buf, pos); const segCount = r.value; pos = r.pos;
  const segments = [];
  for (let i = 0; i < segCount; i++) {
    r = readUleb(buf, pos); const uSize = r.value; pos = r.pos;
    r = readUleb(buf, pos); const cSize = r.value; pos = r.pos;
    segments.push({ uSize, cSize });
  }
  return { version, files, segments, payloadOffset: pos };
}

// Slice each file out of the reconstructed virtual blob.
export function extractFiles(files, blob) {
  const out = new Map();
  for (const { path, offset, size } of files) {
    out.set(path, blob.subarray(offset, offset + size));
  }
  return out;
}

// Decode a whole in-memory archive → Map<path, Uint8Array>. Convenience for
// tests / small archives; the browser loader streams segments instead (below).
export function decodeArchive(buf, decompress) {
  const { files, segments, payloadOffset } = parseHeader(buf);
  const total = segments.reduce((n, s) => n + s.uSize, 0);
  const blob = new Uint8Array(total);
  let cPos = payloadOffset, uPos = 0;
  for (const { uSize, cSize } of segments) {
    const chunk = decompress(buf.subarray(cPos, cPos + cSize));
    if (chunk.length !== uSize) throw new Error(`segment size mismatch: ${chunk.length} != ${uSize}`);
    blob.set(chunk, uPos);
    cPos += cSize; uPos += uSize;
  }
  return extractFiles(files, blob);
}

// Byte range in the payload for segment `i` (given a parsed header). The loader
// uses this to issue HTTP Range requests: add `payloadOffset` for file offsets.
export function segmentRange(segments, i) {
  let start = 0;
  for (let k = 0; k < i; k++) start += segments[k].cSize;
  return { start, end: start + segments[i].cSize };
}
