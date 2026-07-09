# asset-tools/

Offline, game-agnostic asset packer. Packs a directory tree into one
segmented-brotli `.data` archive the browser loader streams into OPFS.

## Usage

```bash
# pack a directory → build.data (+ build.data.meta.json)
python3 asset-tools/pack.py <input_dir> build.data [segment_mb]

# verify the packer round-trips (no game needed)
python3 asset-tools/pack.py --selftest
```

Requires Python 3 and the `brotli` module (`pip install brotli`).

- Segment cache dir: `$DX8WASM_PACK_CACHE`, else `.pack-cache/` next to the
  output. Keep it out of any upload directory. A repack after editing a few
  files reuses cached segments and finishes in seconds.
- Format contract: [`GAXD_FORMAT.md`](GAXD_FORMAT.md). The browser loader in
  `web-runtime/` decodes it.

Per-game staging (which game dirs/fonts to include) is the consuming project's
job — `pack.py` just packs whatever directory you give it.
