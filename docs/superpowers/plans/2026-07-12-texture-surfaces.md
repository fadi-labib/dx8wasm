# dx8wasm Texture Surfaces (Phase C) Implementation Plan

> Unblocks GeneralsX texture loading: the engine uploads texture pixels through
> the D3D8 **surface** path, which dx8wasm stubs. This adds it.

> **STATUS: COMPLETE (verified 2026-08-01).** All four tasks are implemented — `Surface8`,
> `GetSurfaceLevel`, `CreateImageSurface`, `CopyRects` (`runtime/d3d8webgl/device.cpp:977`) and
> `UpdateTexture` (`:1001`), covered by `surface_smoke`. The plan's "DXT defers" note is also
> closed: DXT1 landed and is verified by `dxt_smoke`. Kept for rationale, not as open work.

**Goal:** Implement `IDirect3DSurface8` and the surface-based texture-upload API so
the engine's `TextureClass` load path works, taking the in-browser GeneralsX boot
past its first texture-load crash toward a rendered frame.

**Architecture:** One `Surface8` class serving two roles — a *texture-level*
surface (references a `Texture8` mip level; `UnlockRect` uploads that level to GL)
and a *standalone image* surface (`CreateImageSurface`; owns a CPU buffer, no GL).
`Texture8` becomes multi-mip (a vector of per-level CPU buffers, one GL texture with
`glTexImage2D` per level). No pipeline/shader changes — sampling already works.

**Tech stack:** C++17, GLES3/WebGL2, dx8wasm `runtime/d3d8webgl/device.cpp`,
`runtime/d3d8/d3d8.h`, the existing pixel-smoke harness (`web-runtime/test`).

## Global Constraints
- Clean-room: re-derive from the D3D8 spec / behavior; never paste reference-fork code.
- Commit author `Fadi Labib <github@fadilabib.com>`; no AI co-author line.
- Every feature lands with a headless pixel smoke (the SDK rule).
- Formats: handle uncompressed 32-bit `A8R8G8B8`/`X8R8G8B8` now; **DXT compressed
  defers** (flag via `coverage::unhandled_format`) — a follow-up.
- ABI: `IDirect3DSurface8` vtable order per `runtime/d3d8/d3d8.h` exactly.

## Design details

**`Surface8 : IDirect3DSurface8`** fields: `D3DFORMAT fmt`, `UINT w,h`,
`Texture8* parent` (null for standalone), `UINT level`, `std::vector<BYTE> own`
(used only when standalone). Methods:
- `LockRect(D3DLOCKED_RECT*, const RECT* r, DWORD)`: `Pitch=w*4`; `pBits` = base
  (parent level buffer or `own.data()`) + `(r?r->top:0)*Pitch + (r?r->left:0)*4`.
- `UnlockRect()`: if `parent`, call `parent->upload_level(level)`.
- `GetDesc(D3DSURFACE_DESC*)`: fmt/type=`D3DRTYPE_SURFACE`/pool/w/h.
- `GetContainer`/`GetDevice`/private-data: minimal (`D3D_OK`/`E_NOTIMPL` as the
  engine tolerates — it only calls `GetDesc`,`LockRect`,`UnlockRect`,`Release`).
- refcount `AddRef`/`Release` → delete at 0.

**`Texture8` multi-mip refactor:** replace single `cpu` with
`struct Level{UINT w,h; std::vector<BYTE> px;}` `std::vector<Level> levels;`.
`GetLevelCount()=levels.size()`. `LockRect(level,…)`/`UnlockRect(level)` operate on
`levels[level]`. New `void upload_level(UINT l)`: bind `tex`, `glTexImage2D(…,l,…,
levels[l].w,levels[l].h,…,levels[l].px.data())`, set filter/wrap once. Keep the
existing single-level smokes green (a texture created with 1 level behaves as before).

**Device methods:**
- `CreateTexture(w,h,levels,usage,fmt,pool,out)`: build `Texture8` with `levels`
  mip entries (or a full chain when `levels==0`), each halving dims (min 1).
- `GetSurfaceLevel(level,out)`: `*out = new Surface8(this_texture, level)`; AddRef.
- `CreateImageSurface(w,h,fmt,out)`: `*out = new Surface8(standalone w,h,fmt)`.
- `CopyRects(src,srcRects,n,dst,dstPts)`: for each rect (or whole surface if null),
  row-copy `src`→`dst` CPU buffers; if `dst` is texture-backed, `UnlockRect`-upload.
- `UpdateTexture(src,dst)`: copy every level's CPU buffer `src`→`dst`, upload each.

## Tasks (each ends green)

### Task 1: Multi-mip Texture8 + `upload_level` (no behavior change for 1-level)
- Refactor `Texture8` to `levels`; port `LockRect`/`UnlockRect`/`GetLevelDesc`/
  `GetLevelCount`; add `upload_level`. `CreateTexture` honors mip count.
- Verify: existing `lit_tex_smoke` + `draw_tex_smoke` still green (1-level path).
- Commit.

### Task 2: `Surface8` + `GetSurfaceLevel` + `CreateImageSurface`
- Add `Surface8` class; wire `Texture8::GetSurfaceLevel` and
  `Device::CreateImageSurface`.
- Test — new `surface_smoke.cpp`: `CreateTexture(2,2,1,…,A8R8G8B8)` →
  `GetSurfaceLevel(0,&s)` → `s->LockRect` → fill solid red (D3DCOLOR BGRA) →
  `s->UnlockRect` → `s->Release` → draw a textured quad → readback == red.
- Run: fails first (assert), then green after impl. Commit.

### Task 3: `CopyRects` + `UpdateTexture`
- Implement both (CPU row-copy + upload for texture-backed dst).
- Test — extend `surface_smoke` (or add `copyrects_smoke`): `CreateImageSurface`
  filled blue → `CopyRects` into a texture's surface → draw → readback == blue.
- Commit.

### Task 4: Integration verification
- Re-run GeneralsX with `TexturesZH.big`+`TerrainZH.big` mounted (scout harness).
  Expect: past `TextureClass::Apply_New_Surface`; capture the next frontier
  (likely more `D3DTSS_*`, DXT `.dds`, or the render loop). Record it.
- No dx8wasm commit unless a real gap surfaces; update `docs/ROADMAP.md`/CONFORMANCE.

## Out of scope (deferred, flag don't crash)
DXT/compressed formats (`.dds`); cube/volume textures; render-target surfaces
(`GetBackBuffer`/`CreateRenderTarget`); `GetFrontBuffer` screenshots.
