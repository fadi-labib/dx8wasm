// SPDX-License-Identifier: GPL-3.0-only
// Streaming GAXD loader: fetch a .data archive by HTTP Range, decode segment by
// segment, and write files into OPFS. Memory is bounded to one decompressed
// segment regardless of archive size, so multi-GB games stream without OOM.
// Cached by contentHash: a fully-written archive is reused, not re-downloaded.
import { parseHeader, segmentRange } from './gaxd.js';

async function openWritable(dir, relPath) {
  const parts = relPath.split('/');
  let d = dir;
  for (let i = 0; i < parts.length - 1; i++) {
    d = await d.getDirectoryHandle(parts[i], { create: true });
  }
  const fh = await d.getFileHandle(parts[parts.length - 1], { create: true });
  return fh.createWritable();
}

// dataUrl points at the .data file; its sidecar is dataUrl + '.meta.json'.
// opts: { decompress(Uint8Array)=>Uint8Array (required), onProgress?, cache=true }
export async function loadArchive(dataUrl, { decompress, onProgress, cache = true } = {}) {
  if (!decompress) throw new Error('loadArchive: a decompress() function is required');
  const meta = await (await fetch(dataUrl + '.meta.json')).json();
  const root = await navigator.storage.getDirectory();
  const dirName = 'gaxd-' + meta.contentHash;

  if (cache) {
    try {
      const d = await root.getDirectoryHandle(dirName);
      await d.getFileHandle('.complete');            // marker => fully written
      onProgress?.({ phase: 'cache-hit', received: meta.rawSize, total: meta.rawSize });
      return { dir: d, meta, cached: true };
    } catch { /* not cached — download */ }
  }

  // Header first (one small Range request), then plan the segment fetches.
  const headerBuf = new Uint8Array(await (await fetch(dataUrl, {
    headers: { Range: `bytes=0-${meta.headerSize - 1}` },
  })).arrayBuffer());
  const { files, segments, payloadOffset } = parseHeader(headerBuf);

  await root.removeEntry(dirName, { recursive: true }).catch(() => {}); // drop stale partial
  const dir = await root.getDirectoryHandle(dirName, { create: true });

  // Pull-reader over decompressed segments: `buf` holds at most one segment.
  let segIdx = 0, buf = new Uint8Array(0), bufPos = 0, received = 0;
  async function ensure() {
    if (bufPos < buf.length) return true;
    if (segIdx >= segments.length) return false;
    const { start, end } = segmentRange(segments, segIdx);
    const comp = new Uint8Array(await (await fetch(dataUrl, {
      headers: { Range: `bytes=${payloadOffset + start}-${payloadOffset + end - 1}` },
    })).arrayBuffer());
    buf = decompress(comp);
    bufPos = 0;
    segIdx++;
    received += buf.length;
    onProgress?.({ phase: 'stream', received, total: meta.rawSize, segment: segIdx, segments: segments.length });
    return true;
  }

  for (const f of files) {
    const writable = await openWritable(dir, f.path);
    let remaining = f.size;
    while (remaining > 0) {
      if (!(await ensure())) throw new Error(`unexpected EOF writing ${f.path}`);
      const take = Math.min(remaining, buf.length - bufPos);
      await writable.write(buf.subarray(bufPos, bufPos + take));
      bufPos += take;
      remaining -= take;
    }
    await writable.close();
  }

  const marker = await (await dir.getFileHandle('.complete', { create: true })).createWritable();
  await marker.write(meta.contentHash);
  await marker.close();
  return { dir, meta, cached: false };
}
