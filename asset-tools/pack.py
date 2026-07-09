#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
dx8wasm asset packer — pack a directory tree into one segmented-brotli archive.

Emits a `.data` file in the **GAXD v2** format (see GAXD_FORMAT.md) plus a
`<output>.meta.json` sidecar the browser loader uses for resumable, cached,
parallel downloads. Game-agnostic: it packs whatever directory you point it at.

Segments are independent brotli streams, so they compress in parallel across
CPU cores, decompress in parallel in the browser, and are the unit of resumable
download. A content-hash segment cache means a repack after editing a few files
recompresses only the affected segments (seconds, not minutes on a multi-GB game).

Usage:
  pack.py <input_dir> <output.data> [segment_mb]
  pack.py --selftest
"""
import os, sys, json, struct, time, hashlib, tempfile, shutil
from concurrent.futures import ProcessPoolExecutor
import brotli

BROTLI_QUALITY = 11
BROTLI_LGWIN = 24
MAGIC = 0x47415844                  # 'GAXD'
VERSION = 2
DEFAULT_SEGMENT = 32 * 1024 * 1024
ALIGN_THRESHOLD = 8 * 1024 * 1024   # files >= this start on a segment boundary
CACHE_ENV = 'DX8WASM_PACK_CACHE'


def uleb128(n):
    buf = []
    while True:
        b = n & 0x7f
        n >>= 7
        if n:
            b |= 0x80
        buf.append(b)
        if not n:
            break
    return bytes(buf)


def read_uleb128(buf, pos):
    """Return (value, new_pos)."""
    shift = 0
    result = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7f) << shift
        if not (b & 0x80):
            return result, pos
        shift += 7


def fmt_time(s):
    return f"{s:.0f}s" if s < 60 else f"{int(s) // 60}m{int(s) % 60:02d}s"


def fmt_size(n):
    if n > 1024 ** 3:
        return f"{n / 1024 ** 3:.2f} GB"
    if n > 1024 ** 2:
        return f"{n / 1024 ** 2:.0f} MB"
    if n > 1024:
        return f"{n // 1024} KB"
    return f"{n} B"


# ── segment planning ─────────────────────────────────────────────────────────

def collect_files(input_dir):
    files = []
    for root, _, filenames in os.walk(input_dir):
        for f in sorted(filenames):
            if f in ('.DS_Store', 'Thumbs.db', 'desktop.ini') or f.startswith('._'):
                continue
            fp = os.path.join(root, f)
            rel = os.path.relpath(fp, input_dir).replace(os.sep, '/')
            files.append((rel, fp, os.path.getsize(fp)))
    files.sort(key=lambda x: x[0])
    return files


def plan_segments(files, segment_size):
    """Return (index, segments).

    index:    [(rel, u_offset, size)] — each file's span in the virtual
              concatenation of all files (what the loader mounts).
    segments: [[(abs_path, file_offset, length), ...]] — the source byte spans
              that make up each segment. Boundaries are at fixed segment_size,
              except a file >= ALIGN_THRESHOLD always begins a new segment so
              editing it never shifts/invalidates unrelated segments' content.
    """
    index, segments = [], []
    cur, cur_len, u = [], 0, 0

    def close():
        nonlocal cur, cur_len
        if cur_len > 0:
            segments.append(cur)
            cur, cur_len = [], 0

    for rel, fp, sz in files:
        index.append((rel, u, sz))
        u += sz
        if sz >= ALIGN_THRESHOLD:
            close()
        remaining, fofs = sz, 0
        while remaining > 0:
            take = min(segment_size - cur_len, remaining)
            cur.append((fp, fofs, take))
            cur_len += take
            fofs += take
            remaining -= take
            if cur_len >= segment_size:
                close()
    close()
    return index, segments


# ── worker: hash + compress one segment (reads sources directly) ──────────────

def _segment_raw(spans):
    parts = []
    for path, ofs, length in spans:
        with open(path, 'rb') as f:
            f.seek(ofs)
            parts.append(f.read(length))
    return b''.join(parts)


def compress_segment(args):
    idx, spans, cache_dir = args
    data = _segment_raw(spans)
    h = hashlib.sha256(data).hexdigest()
    cpath = os.path.join(cache_dir, h + '.br') if cache_dir else None
    if cpath and os.path.exists(cpath):
        with open(cpath, 'rb') as f:
            return idx, len(data), f.read(), h, True
    comp = brotli.compress(data, mode=0, quality=BROTLI_QUALITY, lgwin=BROTLI_LGWIN)
    if cpath:
        # Unique tmp per worker: two segments with identical content hash to the
        # same cpath, so a shared tmp name would race across parallel workers.
        tmp = f"{cpath}.{os.getpid()}.tmp"
        with open(tmp, 'wb') as f:
            f.write(comp)
        os.replace(tmp, cpath)      # atomic; last writer wins (bytes identical)
    return idx, len(data), comp, h, False


# ── pack ──────────────────────────────────────────────────────────────────────

def pack(input_dir, output_file, segment_size=DEFAULT_SEGMENT, quiet=False):
    log = (lambda *a, **k: None) if quiet else print
    files = collect_files(input_dir)
    if not files:
        raise SystemExit(f"no files under {input_dir}")
    total_raw = sum(sz for _, _, sz in files)
    index, seg_spans = plan_segments(files, segment_size)
    log(f"Files: {len(files)},  Raw: {fmt_size(total_raw)},  Segments: {len(seg_spans)}"
        f" (~{fmt_size(segment_size)}, big files aligned)")

    cache_dir = os.environ.get(CACHE_ENV) or \
        os.path.join(os.path.dirname(os.path.abspath(output_file)), '.pack-cache')
    os.makedirs(cache_dir, exist_ok=True)
    workers = os.cpu_count() or 4
    log(f"Brotli q{BROTLI_QUALITY} across {workers} cores, cache: {cache_dir}")

    t0 = time.time()
    results = [None] * len(seg_spans)
    done = cached = total_comp = 0
    jobs = [(i, seg_spans[i], cache_dir) for i in range(len(seg_spans))]
    with ProcessPoolExecutor(max_workers=workers) as ex:
        for idx, usize, comp, h, from_cache in ex.map(compress_segment, jobs):
            results[idx] = (usize, comp, h)
            done += 1
            cached += 1 if from_cache else 0
            total_comp += len(comp)
            elapsed = time.time() - t0
            eta = (elapsed / done) * (len(seg_spans) - done)
            log(f"\r  {done * 100 // len(seg_spans)}%  ({done}/{len(seg_spans)} segments,"
                f" {cached} cached)  elapsed {fmt_time(elapsed)}  ETA {fmt_time(eta)}",
                end='', flush=True)
    elapsed = time.time() - t0
    ratio = total_comp * 100 // max(total_raw, 1)
    log(f"\n  Done in {fmt_time(elapsed)} — {fmt_size(total_comp)} ({ratio}%),"
        f" {cached}/{len(seg_spans)} from cache")

    header = bytearray()
    header += struct.pack('<II', MAGIC, VERSION)
    header += uleb128(len(index))
    for rel, offset, size in index:
        pb = rel.encode('utf-8')
        header += uleb128(len(pb)) + pb
        header += uleb128(offset) + uleb128(size)
    header += uleb128(len(results))
    for usize, comp, _ in results:
        header += uleb128(usize) + uleb128(len(comp))

    with open(output_file, 'wb') as out:
        out.write(header)
        for _, comp, _ in results:
            out.write(comp)

    # Whole-build hash = hash of ordered segment hashes.
    build_hash = hashlib.sha256('|'.join(h for _, _, h in results).encode()).hexdigest()[:16]
    meta = {
        'headerSize': len(header),
        'segmentCount': len(results),
        'fileCount': len(index),
        'rawSize': total_raw,
        'contentHash': build_hash,
    }
    with open(output_file + '.meta.json', 'w') as f:
        json.dump(meta, f)

    log(f"\nWritten: {output_file}  ({fmt_size(os.path.getsize(output_file))})")
    log(f"Meta:    {output_file}.meta.json  (header {len(header)} B, hash {build_hash})")
    return meta


# ── reference decoder (for validation / self-test; the browser loader is JS) ──

def unpack(archive_path):
    """Decode a GAXD v2 archive → {rel_path: bytes}. Reference implementation."""
    with open(archive_path, 'rb') as f:
        buf = f.read()
    magic, version = struct.unpack_from('<II', buf, 0)
    if magic != MAGIC or version != VERSION:
        raise ValueError(f"bad GAXD header: magic={magic:#x} version={version}")
    pos = 8
    file_count, pos = read_uleb128(buf, pos)
    index = []
    for _ in range(file_count):
        plen, pos = read_uleb128(buf, pos)
        rel = buf[pos:pos + plen].decode('utf-8'); pos += plen
        off, pos = read_uleb128(buf, pos)
        size, pos = read_uleb128(buf, pos)
        index.append((rel, off, size))
    seg_count, pos = read_uleb128(buf, pos)
    segs = []
    for _ in range(seg_count):
        usize, pos = read_uleb128(buf, pos)
        csize, pos = read_uleb128(buf, pos)
        segs.append((usize, csize))
    # Payload starts here; decode segments into one virtual blob.
    blob = bytearray()
    for usize, csize in segs:
        chunk = brotli.decompress(buf[pos:pos + csize]); pos += csize
        if len(chunk) != usize:
            raise ValueError("segment size mismatch")
        blob += chunk
    return {rel: bytes(blob[off:off + size]) for rel, off, size in index}


def _selftest():
    d = tempfile.mkdtemp(prefix='dx8pack-')
    try:
        src = os.path.join(d, 'src')
        os.makedirs(os.path.join(src, 'Data'))
        expect = {
            'a.txt': b'hello dx8wasm',
            'Data/b.bin': bytes(range(256)) * 1000,      # ~256 KB, compressible
            'Data/nested/c.dat': os.urandom(50_000),     # incompressible
            'empty': b'',
        }
        for rel, content in expect.items():
            fp = os.path.join(src, rel)
            os.makedirs(os.path.dirname(fp), exist_ok=True)
            with open(fp, 'wb') as f:
                f.write(content)
        out = os.path.join(d, 'out.data')
        # tiny segments to exercise multi-segment planning + splitting
        meta = pack(src, out, segment_size=64 * 1024, quiet=True)
        got = unpack(out)
        assert got == expect, f"round-trip mismatch: {set(got) ^ set(expect)}"
        assert meta['fileCount'] == len(expect), meta
        assert os.path.exists(out + '.meta.json')
        assert len(meta['contentHash']) == 16
        print("selftest OK — round-trip exact, meta valid")
    finally:
        shutil.rmtree(d, ignore_errors=True)


if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] == '--selftest':
        _selftest()
        sys.exit(0)
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    seg = int(sys.argv[3]) * 1024 * 1024 if len(sys.argv) > 3 else DEFAULT_SEGMENT
    pack(sys.argv[1], sys.argv[2], seg)
