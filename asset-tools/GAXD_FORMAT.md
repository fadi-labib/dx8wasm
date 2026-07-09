# GAXD v2 — dx8wasm asset archive format

A GAXD archive packs a directory tree into one `.data` file of independently
brotli-compressed **segments**, plus a `.meta.json` sidecar. It is designed for
the browser: segments compress/decompress in parallel and are the unit of
resumable download and content caching.

This format is the **contract** between the packer (`pack.py`) and the browser
loader (`web-runtime`). Both must agree byte-for-byte. It carries no dx8wasm or
game code, so it may be reimplemented freely (see `docs/LICENSING.md`).

## Byte layout

All multi-byte integers in the fixed header are little-endian. Everything else
is [ULEB128](https://en.wikipedia.org/wiki/LEB128) (unsigned, 7 bits/byte,
low group first, high bit = continuation).

```
uint32   magic          0x47415844  ('GAXD', stored little-endian)
uint32   version        2
uleb128  file_count
file_entry * file_count
uleb128  segment_count
segment_entry * segment_count
<segment 0 brotli bytes><segment 1 brotli bytes>...   (payload)

file_entry:
  uleb128  path_len
  bytes    path            UTF-8, '/'-separated, relative to the packed root
  uleb128  u_offset        byte offset of this file in the virtual blob
  uleb128  u_size          file size in bytes

segment_entry:
  uleb128  u_size          decompressed size of this segment
  uleb128  c_size          compressed (brotli) size — its length in the payload
```

The **virtual blob** is the concatenation of every file's bytes in `file_entry`
order. Decompressing all segments in order and concatenating them reproduces the
virtual blob exactly; each file is then `blob[u_offset : u_offset + u_size]`.

## Segmentation rules

- Default segment size 32 MiB (`pack.py <in> <out> [segment_mb]` overrides).
- A file ≥ 8 MiB (`ALIGN_THRESHOLD`) starts a new segment, so re-editing one big
  file only recompresses its own segments — unrelated segments keep their hash
  and stay cached.
- Brotli params: quality 11, lgwin 24, generic mode.

## `.meta.json` sidecar

```json
{
  "headerSize":   <bytes before the payload; payload = file[headerSize:]>,
  "segmentCount": <number of segments>,
  "fileCount":    <number of files>,
  "rawSize":      <total uncompressed bytes>,
  "contentHash":  <16 hex chars: sha256 of '|'-joined per-segment sha256, truncated>
}
```

The loader fetches `headerSize` bytes first to read the index, then streams
segments (HTTP Range), decoding each as it arrives. `contentHash` keys the OPFS
cache so an unchanged build is never re-downloaded.

## Reference decoder

`pack.py:unpack()` is a Python reference decoder used by `--selftest`. The
authoritative browser decoder lives in `web-runtime`.
