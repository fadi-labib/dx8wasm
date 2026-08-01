# Honest Stubs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **STATUS: COMPLETE (verified 2026-08-01).** Every task below is implemented; the checkboxes
> were never ticked, so ignore them. Verified by inspection, not by checkbox: `format_support.h`,
> `caps_query_smoke.cpp` and `honest_stubs_smoke.cpp` exist and are in the CI smoke list;
> `dx8wasm_has_cap` reports stencil (`runtime/runtime.cpp:38`); `docs/SDK_REFERENCE.md` carries
> the "Stubs fail loudly" contract. Kept for its rationale — the *why* behind each demotion is
> still the reference for future stubs — not as an open work item.

**Goal:** Remove every dx8wasm stub that reports success with a plausible-but-fabricated
value, so an unimplemented D3D8 feature always surfaces as a failure or a coverage counter
rather than as silently-wrong rendering.

**Architecture:** Three mechanical rules, applied across `runtime/d3d8webgl/`. (1) A `Get*`
that mirrors state the device already tracks must answer from that state. (2) A `Get*`/`Create*`
for something the backend does not implement must return `D3DERR_INVALIDCALL` /
`D3DERR_NOTAVAILABLE`, never `D3D_OK`. (3) A capability query must be derived from the same
predicate the implementation uses, so caps and behaviour cannot drift apart. Every task is
test-first against the existing headless smoke harness.

**Tech Stack:** C++17, Emscripten 6.0.2, WebGL2/GLES3, CMake + Ninja, headless Chromium
smokes driven by `web-runtime/test/phase2.gpu.test.mjs`.

## Global Constraints

- Author every commit as `Fadi Labib <github@fadilabib.com>`. Never add an AI co-author line.
- SPDX header `// SPDX-License-Identifier: GPL-3.0-only` on every new file.
- `bash scripts/ci.sh` must print `ALL GREEN` before any task is considered done.
- Emscripten SDK pinned to 6.0.2 (`.emscripten-version`); `source ~/emsdk/emsdk_env.sh` first.
- Headless smokes render to a **4×4** canvas — they cannot catch viewport/full-canvas bugs.
- The consuming game (`../generals-dx8wasm`) compiles against **DXVK's** full-ABI `d3d8.h`
  (`build/wasm-engine-release/_deps/dxvk-src/include/dxvk/d3d8types.h`) while linking dx8wasm's
  implementation. Any struct dx8wasm writes through must match that layout **exactly**.
- Do not rename existing public symbols; `runtime/include/dx8wasm/contract.h` is the ABI.
- Behaviour changes here can alter which texture formats the game picks. Task 8 is mandatory,
  not optional: no task set is complete until the real-GPU game capture is re-verified.

---

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `runtime/d3d8webgl/format_support.h` | **new** — the single predicate for "can this backend do this D3DFORMAT", shared by the device and the factory | 1 |
| `runtime/d3d8webgl/device.cpp` | device-side state mirrors + stub demotions | 1, 4, 5, 6 |
| `runtime/d3d8webgl/d3d8.cpp` | factory-side capability queries + adapter identity | 2, 3, 6 |
| `runtime/d3d8webgl/caps_fill.h` | advertised `D3DCAPS8` — must stop over-claiming | 6 |
| `runtime/d3d8/d3d8.h` | clean-room API subset — add `D3DUSAGE_*` and the adapter-identifier struct | 2, 3 |
| `runtime/include/dx8wasm/contract.h` | coverage counter for unhandled stage states | 5 |
| `runtime/coverage/coverage.{h,cpp}` | the new coverage sink entry point | 5 |
| `runtime/runtime.cpp` | `dx8wasm_has_cap` must stop denying stencil | 7 |
| `runtime/test/caps_query_smoke.cpp` | **new** — capability queries match implementation | 2 |
| `runtime/test/honest_stubs_smoke.cpp` | **new** — unimplemented entry points fail loudly | 6 |
| `runtime/test/render_state_smoke.cpp` | extended: stage-state round-trip | 4 |
| `runtime/test/coverage_smoke.cpp` | extended: unhandled stage state is counted | 5 |
| `CMakeLists.txt` | register the two new smoke targets | 2, 6 |
| `web-runtime/test/phase2.gpu.test.mjs` | expected sentinel pixels for the new smokes | 2, 6 |
| `docs/CONFORMANCE.md` | regenerated from the probe | 8 |
| `docs/SDK_REFERENCE.md` | document the "stubs fail loudly" contract | 8 |
| `../generals-dx8wasm/docs/OPEN-ITEMS.md` | record the audit + outcome game-side | 8 |

---

### Task 1: Shared format-support predicate

`texfmt::supported()` and `dxt::is_dxt()` live inside `device.cpp`, so `d3d8.cpp` (the factory,
which answers `CheckDeviceFormat`) cannot see them. That is *why* the capability query was
written as a blanket yes. Extract them first; Task 2 depends on it. Pure refactor — no
behaviour change, so the existing suite is the test.

**Files:**
- Create: `runtime/d3d8webgl/format_support.h`
- Modify: `runtime/d3d8webgl/device.cpp` (delete the two moved predicates, include the header)

**Interfaces:**
- Consumes: nothing.
- Produces: `dxt::is_dxt(D3DFORMAT) -> bool`, `texfmt::supported(D3DFORMAT) -> bool`, both
  `inline` in namespace scope, available to any translation unit including this header.

- [ ] **Step 1: Create the shared header**

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Which D3DFORMATs this backend can actually carry. Shared deliberately: the factory's
// CheckDeviceFormat must answer from the SAME predicate the texture path enforces, or the
// two drift and the engine picks a format that later fails to upload.
#ifndef DX8WASM_FORMAT_SUPPORT_H
#define DX8WASM_FORMAT_SUPPORT_H
#include "d3d8/d3d8.h"

namespace dxt {
inline bool is_dxt(D3DFORMAT f) { return f == D3DFMT_DXT1 || f == D3DFMT_DXT3 || f == D3DFMT_DXT5; }
}   // namespace dxt

namespace texfmt {
// Uncompressed formats with a real upload path (see texfmt::prepare in device.cpp).
inline bool supported(D3DFORMAT f) {
  switch (f) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_R8G8B8:
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4: case D3DFMT_A8L8:
    case D3DFMT_A8: case D3DFMT_L8: return true;
    default: return false;
  }
}
}   // namespace texfmt
#endif
```

- [ ] **Step 2: Remove the duplicates from `device.cpp`**

Delete the `inline bool is_dxt(...)` line from `namespace dxt` and the whole
`inline bool supported(D3DFORMAT f) { ... }` body from `namespace texfmt`, leaving the rest of
both namespaces (`block_bytes`, `data_size`, `rgb565`, `bpp`, `prepare`, …) untouched. Then add
the include next to the existing ones near the top of the file:

```cpp
#include "format_support.h"
```

- [ ] **Step 3: Build and run the full suite — nothing should change**

```bash
source ~/emsdk/emsdk_env.sh && cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: every smoke reports `ok`, in particular `dxt_smoke` and `texfmt_smoke`.

- [ ] **Step 4: Commit**

```bash
git add runtime/d3d8webgl/format_support.h runtime/d3d8webgl/device.cpp
git commit -m "d3d8webgl: share the format-support predicate with the factory"
```

---

### Task 2: Capability queries answer from the implementation

`CheckDeviceFormat`, `CheckDeviceType`, `CheckDeviceMultiSampleType` and
`CheckDepthStencilMatch` all `return D3D_OK` unconditionally. WW3D builds its entire
supported-texture-format table from `CheckDeviceFormat` (`dx8caps.cpp:717/749/793`), so the
engine believes every format works and only finds out at upload time — after it has committed.

**Files:**
- Modify: `runtime/d3d8/d3d8.h` (add the two `D3DUSAGE_*` flags)
- Modify: `runtime/d3d8webgl/d3d8.cpp:60-63`
- Create: `runtime/test/caps_query_smoke.cpp`
- Modify: `CMakeLists.txt`, `web-runtime/test/phase2.gpu.test.mjs`

**Interfaces:**
- Consumes: `texfmt::supported`, `dxt::is_dxt` from Task 1.
- Produces: no new symbols; the four query methods gain real answers.

- [ ] **Step 1: Write the failing test**

Create `runtime/test/caps_query_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// The factory's capability queries must agree with what the texture path can actually do.
// A blanket D3D_OK here is worse than a refusal: WW3D builds its whole format table from
// CheckDeviceFormat and then picks a format that only fails much later, at upload.
// Reports the sentinel [1,0,0,255] when every check agrees.
#include "d3d8/d3d8.h"
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }

  auto texOk = [&](D3DFORMAT f) {
    return d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0,
                                  D3DRTYPE_TEXTURE, f) == D3D_OK;
  };

  // Formats with a real upload path must be accepted.
  for (D3DFORMAT f : {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_A4R4G4B4,
                      D3DFMT_A8, D3DFMT_L8, D3DFMT_DXT1, D3DFMT_DXT5})
    if (!texOk(f)) { report_error("a supported texture format was refused"); return 1; }

  // Formats with no upload path must be refused, not waved through.
  if (texOk(D3DFMT_UNKNOWN)) { report_error("D3DFMT_UNKNOWN was accepted"); return 1; }
  if (texOk(D3DFMT_D24S8))   { report_error("a depth format was accepted as a texture"); return 1; }

  // Resource types the backend cannot create must be refused.
  if (d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0,
                             D3DRTYPE_CUBETEXTURE, D3DFMT_A8R8G8B8) == D3D_OK) {
    report_error("cube texture format was accepted but CreateCubeTexture fails"); return 1;
  }
  if (d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET,
                             D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8) == D3D_OK) {
    report_error("render-target usage was accepted but CreateRenderTarget fails"); return 1;
  }

  // Back-buffer formats: the ones CreateDevice really presents.
  if (d3d->CheckDeviceType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8, TRUE) != D3D_OK) {
    report_error("X8R8G8B8 back buffer was refused"); return 1;
  }
  if (d3d->CheckDeviceType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_DXT1, TRUE) == D3D_OK) {
    report_error("a compressed format was accepted as a back buffer"); return 1;
  }

  // No multisampling is implemented, so only NONE may be claimed.
  if (d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE,
                                      D3DMULTISAMPLE_NONE) != D3D_OK) {
    report_error("MULTISAMPLE_NONE was refused"); return 1;
  }
  if (d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE,
                                      (D3DMULTISAMPLE_TYPE)4) == D3D_OK) {
    report_error("4x multisampling was claimed but is not implemented"); return 1;
  }

  // The depth/stencil the context is actually created with (24-bit depth + 8-bit stencil).
  if (d3d->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                  D3DFMT_X8R8G8B8, D3DFMT_D24S8) != D3D_OK) {
    report_error("D24S8 was refused"); return 1;
  }
  if (d3d->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                                  D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8) == D3D_OK) {
    report_error("a colour format was accepted as depth/stencil"); return 1;
  }

  d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
```

- [ ] **Step 2: Register the smoke**

In `CMakeLists.txt`, after the `d3d8_smoke` block, add:

```cmake
add_executable(caps_query_smoke runtime/test/caps_query_smoke.cpp)
target_include_directories(caps_query_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(caps_query_smoke PRIVATE dx8_d3d8webgl)
target_link_options(caps_query_smoke PRIVATE ${DX8_WEBGL_LINK})
set_target_properties(caps_query_smoke PROPERTIES SUFFIX ".js")
```

In `web-runtime/test/phase2.gpu.test.mjs`, add to the expectations list next to `d3d8_smoke`:

```js
  ['caps_query_smoke', [1, 0, 0, 255]],   // capability queries agree with the texture path
```

- [ ] **Step 3: Run it and watch it fail**

```bash
source ~/emsdk/emsdk_env.sh && cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `caps_query_smoke: D3DFMT_UNKNOWN was accepted` (the first refusal check to
run against the current blanket `D3D_OK`).

- [ ] **Step 4: Add the usage flags to the clean-room header**

In `runtime/d3d8/d3d8.h`, beside the other `#define`s near `D3DADAPTER_DEFAULT`:

```cpp
// Usage flags. Only the two the capability queries must reason about are defined; both name
// resources this backend cannot create (see CreateRenderTarget / CreateDepthStencilSurface).
#define D3DUSAGE_RENDERTARGET   0x00000001L
#define D3DUSAGE_DEPTHSTENCIL   0x00000002L
```

- [ ] **Step 5: Implement the real queries**

In `runtime/d3d8webgl/d3d8.cpp`, add `#include "format_support.h"` to the includes, then
replace lines 60-63 with:

```cpp
  // A back buffer is what CreateDevice actually presents: an 8888/565 colour surface.
  static bool presentable(D3DFORMAT f) {
    return f == D3DFMT_X8R8G8B8 || f == D3DFMT_A8R8G8B8 || f == D3DFMT_R5G6B5;
  }
  // The context is created with 24-bit depth + 8-bit stencil (SDL3Main sets both), so those
  // are the only depth formats that mean anything here.
  static bool depth_format(D3DFORMAT f) {
    return f == D3DFMT_D24S8 || f == D3DFMT_D24X8 || f == D3DFMT_D16 || f == D3DFMT_D32;
  }

  HRESULT CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat,
                          BOOL) override {
    return presentable(DisplayFormat) && presentable(BackBufferFormat) ? D3D_OK : D3DERR_NOTAVAILABLE;
  }
  // Answered from the same predicates the texture path enforces (format_support.h), so caps
  // and behaviour cannot drift. Usages and resource types with no Create* path are refused.
  HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD Usage, D3DRESOURCETYPE RType,
                            D3DFORMAT CheckFormat) override {
    if (Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) return D3DERR_NOTAVAILABLE;
    switch (RType) {
      case D3DRTYPE_TEXTURE:
        return texfmt::supported(CheckFormat) || dxt::is_dxt(CheckFormat) ? D3D_OK : D3DERR_NOTAVAILABLE;
      case D3DRTYPE_SURFACE:
        return texfmt::supported(CheckFormat) ? D3D_OK : D3DERR_NOTAVAILABLE;
      default:   // cube, volume, vertex/index buffers: no Create* path in this backend
        return D3DERR_NOTAVAILABLE;
    }
  }
  // No multisampled path exists; claiming one would silently produce aliased output.
  HRESULT CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT SurfaceFormat, BOOL,
                                     D3DMULTISAMPLE_TYPE MultiSampleType) override {
    if (MultiSampleType != D3DMULTISAMPLE_NONE) return D3DERR_NOTAVAILABLE;
    return presentable(SurfaceFormat) ? D3D_OK : D3DERR_NOTAVAILABLE;
  }
  HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT RenderTargetFormat,
                                 D3DFORMAT DepthStencilFormat) override {
    return presentable(RenderTargetFormat) && depth_format(DepthStencilFormat)
           ? D3D_OK : D3DERR_NOTAVAILABLE;
  }
```

If `D3DMULTISAMPLE_NONE` is not declared in `runtime/d3d8/d3d8.h`, add it next to the other
enums: `enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_NONE = 0 };` — check first, do not duplicate.

- [ ] **Step 6: Run the test — it must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `ok — caps_query_smoke cleared to [1,0,0,255]`, everything else still `ok`.

- [ ] **Step 7: Commit**

```bash
git add runtime/d3d8/d3d8.h runtime/d3d8webgl/d3d8.cpp runtime/test/caps_query_smoke.cpp \
        CMakeLists.txt web-runtime/test/phase2.gpu.test.mjs
git commit -m "d3d8webgl: capability queries answer from the texture path, not blanket yes"
```

---

### Task 3: Honest adapter identity

`GetAdapterIdentifier` returns `D3D_OK` and writes nothing. The engine `ZeroMemory`s the struct
first (`dx8wrapper.cpp:1034`) so there is no garbage read, but `DX8Caps::Compute_Caps` then
classifies a zeroed vendor as `VENDOR_UNKNOWN` and the device presents as a nameless card.

**Files:**
- Modify: `runtime/d3d8/d3d8.h` (add the struct, replacing the `void*` parameter)
- Modify: `runtime/d3d8webgl/d3d8.cpp:46`

**Interfaces:**
- Consumes: nothing.
- Produces: `D3DADAPTER_IDENTIFIER8` in the clean-room header, layout-identical to DXVK's.

**Layout warning:** the game compiles against DXVK's `d3d8types.h`, so the struct below must
match it byte for byte — `MAX_DEVICE_IDENTIFIER_STRING` is **512**, and `DriverVersion` is a
`LARGE_INTEGER` (8 bytes), not a `DWORD`. Verified against
`build/wasm-engine-release/_deps/dxvk-src/include/dxvk/d3d8types.h:995`.

**Vendor-string warning:** `dx8caps.cpp:559` falls back to `if (DriverDLL[0]=='3') VendorId=VENDOR_3DFX;`
when the vendor id is unknown. The driver name must therefore **not** begin with the character
`3`, or the engine will apply 3dfx-specific workarounds to a WebGL2 context.

- [ ] **Step 1: Add the struct to the clean-room header**

In `runtime/d3d8/d3d8.h`, before the `IDirect3D8` interface declaration:

```cpp
// Layout must match the full-ABI d3d8.h a consuming game compiles against (DXVK's
// d3d8types.h): 512-byte name buffers and a 64-bit DriverVersion.
#define MAX_DEVICE_IDENTIFIER_STRING 512
typedef struct _D3DADAPTER_IDENTIFIER8 {
  char     Driver[MAX_DEVICE_IDENTIFIER_STRING];
  char     Description[MAX_DEVICE_IDENTIFIER_STRING];
  int64_t  DriverVersion;
  DWORD    VendorId, DeviceId, SubSysId, Revision;
  struct { uint32_t a; uint16_t b, c; uint8_t d[8]; } DeviceIdentifier;   // GUID
  DWORD    WHQLLevel;
} D3DADAPTER_IDENTIFIER8;
```

Change the interface method signature from `void*` to the real type:

```cpp
  virtual HRESULT GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER8* pIdentifier) = 0;
```

- [ ] **Step 2: Write the failing test**

Append to `runtime/test/caps_query_smoke.cpp`, immediately before `d3d->Release();`:

```cpp
  // The adapter must name itself. A zeroed identifier makes the engine classify the device as
  // an unknown card, which silently drags quality heuristics (GameLOD) down to their floor.
  {
    D3DADAPTER_IDENTIFIER8 id{};
    if (d3d->GetAdapterIdentifier(0, 0, &id) != D3D_OK) { report_error("GetAdapterIdentifier failed"); return 1; }
    if (id.Description[0] == '\0') { report_error("adapter reported an empty description"); return 1; }
    if (id.Driver[0] == '3') { report_error("driver name starting with '3' trips the 3dfx heuristic"); return 1; }
  }
```

- [ ] **Step 3: Run it and watch it fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `caps_query_smoke: adapter reported an empty description`.

- [ ] **Step 4: Implement**

Replace `runtime/d3d8webgl/d3d8.cpp:46` with:

```cpp
  // Name ourselves honestly. Leaving this blank is not neutral: the engine reads the strings
  // and ids to classify the GPU, and an all-zero identifier reads as "unknown card", which
  // pushes quality heuristics to their lowest tier. Vendor/device ids stay 0 on purpose —
  // claiming an NVIDIA or ATI id would trigger vendor-specific driver workarounds.
  HRESULT GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER8* id) override {
    if (!id) return D3DERR_INVALIDCALL;
    *id = D3DADAPTER_IDENTIFIER8{};
    std::snprintf(id->Driver, sizeof id->Driver, "dx8wasm");
    std::snprintf(id->Description, sizeof id->Description, "dx8wasm (D3D8 over WebGL2)");
    id->DriverVersion = 1;
    return D3D_OK;
  }
```

Add `#include <cstdio>` to the includes if it is not already present.

- [ ] **Step 5: Run the test — it must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: all `ok`.

- [ ] **Step 6: Commit**

```bash
git add runtime/d3d8/d3d8.h runtime/d3d8webgl/d3d8.cpp runtime/test/caps_query_smoke.cpp
git commit -m "d3d8webgl: the adapter identifies itself instead of returning a blank struct"
```

---

### Task 4: `GetTextureStageState` round-trips

`GetTextureStageState` writes `0` and returns `D3D_OK` — the exact shape of the
`GetRenderState` defect that blanked the game's UI, one call site away from being live.

**Files:**
- Modify: `runtime/d3d8webgl/device.cpp` (`SetTextureStageState` ~line 612, `GetTextureStageState` ~line 1033)
- Modify: `runtime/test/render_state_smoke.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `tssCache[stage][type]` mirror inside `Device8`; no public symbol change.

- [ ] **Step 1: Write the failing test**

In `runtime/test/render_state_smoke.cpp`, inside the scene-4 block, after the existing
`COLORWRITEENABLE` round-trip assertion:

```cpp
    // Same contract for stage state: a Get that always answers 0 breaks save/restore.
    DWORD tss = 0xDEADBEEFu;
    g_dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    g_dev->GetTextureStageState(0, D3DTSS_COLOROP, &tss);
    if (tss != D3DTOP_MODULATE) { report_error("GetTextureStageState did not round-trip"); return 1; }
```

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `render_state_smoke: GetTextureStageState did not round-trip`.

- [ ] **Step 3: Implement the mirror**

In `Device8`, beside the `rsCache` declaration added by the earlier `GetRenderState` fix:

```cpp
  // Stage-state mirror, same contract as rsCache: every Set is recorded so Get can answer.
  static constexpr unsigned kStageCount = 8, kStageStateCount = 32;
  DWORD tssCache[kStageCount][kStageStateCount]{};
```

At the top of `SetTextureStageState`, before its `switch`:

```cpp
    if (Stage < kStageCount && Type < kStageStateCount) tssCache[Stage][Type] = Value;
```

Replace `GetTextureStageState` with:

```cpp
  HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* v) override {
    if (!v) return D3DERR_INVALIDCALL;
    if (Stage >= kStageCount || Type >= kStageStateCount) return D3DERR_INVALIDCALL;
    *v = tssCache[Stage][Type];
    return D3D_OK;
  }
```

- [ ] **Step 4: Run the test — it must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `ok — render_state_smoke cleared to [153,51,102,191]`.

- [ ] **Step 5: Commit**

```bash
git add runtime/d3d8webgl/device.cpp runtime/test/render_state_smoke.cpp
git commit -m "d3d8webgl: GetTextureStageState reports what SetTextureStageState stored"
```

---

### Task 5: Unhandled stage states are counted, not swallowed

`SetRenderState`'s `default:` reports through `coverage::unhandled_render_state`; the
stage-state twin ends in `default: break;  // remaining stage states unused`. Anything the
backend does not implement — anisotropy, LOD bias, bump-env — disappears with no trace, and
`docs/CONFORMANCE.md`, which is meant to be authoritative, cannot see it.

**Files:**
- Modify: `runtime/include/dx8wasm/contract.h`, `runtime/coverage/coverage.h`,
  `runtime/coverage/coverage.cpp`, `runtime/d3d8webgl/device.cpp`
- Modify: `runtime/test/coverage_smoke.cpp`, `web-runtime/test/phase2.gpu.test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: `coverage::unhandled_stage_state(uint32_t type)`; new trailing field
  `uint32_t unhandled_texture_stage_states;` in `dx8wasm_coverage`.

**ABI note:** append the field at the **end** of `dx8wasm_coverage` so existing offsets are
untouched.

- [ ] **Step 1: Write the failing test**

`runtime/test/coverage_smoke.cpp` currently asserts `[1, 1, 1, 3]` (one unhandled render
state / texture op / format, callback fired 3×). Add one unhandled stage state and assert the
new counter. After the existing unhandled-token calls, before it reports:

```cpp
  // D3DTSS_BUMPENVMAT00 (5) has no implementation — it must be COUNTED, not swallowed.
  dev->SetTextureStageState(0, (D3DTEXTURESTAGESTATETYPE)5, 0);
```

and extend the reported sentinel to carry the new counter — change the `report_pixel` call to:

```cpp
  dx8wasm_coverage cov{};
  dx8wasm_get_coverage(&cov);
  report_pixel(cov.unhandled_render_states, cov.unhandled_texture_stage_ops,
               cov.unhandled_texture_stage_states, callbacks);
```

Update the expectation in `web-runtime/test/phase2.gpu.test.mjs` to:

```js
  ['coverage_smoke', [1, 1, 1, 4]],  // 1 unhandled RS/TOP/TSS each, callback fired 4x
```

Read the existing `coverage_smoke.cpp` before editing — keep its variable names (`callbacks`,
`dev`) as they actually are; the names above are illustrative of shape, not a rename.

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `coverage_smoke` reports a `0` where the stage-state count belongs.

- [ ] **Step 3: Extend the contract**

In `runtime/include/dx8wasm/contract.h`, append to `dx8wasm_coverage`:

```c
    uint32_t unhandled_texture_stage_states;   // Set*StageState tokens with no implementation
```

In `runtime/coverage/coverage.h`, beside the other sinks:

```cpp
void unhandled_stage_state(uint32_t type);
```

In `runtime/coverage/coverage.cpp`, mirror `unhandled_render_state` exactly — bump the new
counter, fire the callback once per distinct token with kind `"D3DTSS_"`, and honour
`set_logging`. Copy the existing function body and change the counter and kind string.

- [ ] **Step 4: Wire it into the device**

In `SetTextureStageState`, replace:

```cpp
      default: break;   // remaining stage states unused
```

with:

```cpp
      default: coverage::unhandled_stage_state(Type); break;
```

- [ ] **Step 5: Run the test — it must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `ok — coverage_smoke cleared to [1,1,1,4]`.

- [ ] **Step 6: Commit**

```bash
git add runtime/include/dx8wasm/contract.h runtime/coverage/coverage.h runtime/coverage/coverage.cpp \
        runtime/d3d8webgl/device.cpp runtime/test/coverage_smoke.cpp web-runtime/test/phase2.gpu.test.mjs
git commit -m "coverage: count unhandled texture stage states instead of dropping them"
```

---

### Task 6: Unimplemented entry points fail instead of lying

The Tier-2 sweep. Each of these returns `D3D_OK` while doing nothing, and several leave the
caller's buffer **uninitialised** — worse than returning zero, because it is nondeterministic.

**Files:**
- Modify: `runtime/d3d8webgl/device.cpp`, `runtime/d3d8webgl/caps_fill.h`
- Create: `runtime/test/honest_stubs_smoke.cpp`
- Modify: `CMakeLists.txt`, `web-runtime/test/phase2.gpu.test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: no new symbols; listed methods change their return values.

- [ ] **Step 1: Write the failing test**

Create `runtime/test/honest_stubs_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Entry points this backend does not implement must FAIL, not return D3D_OK. A stub that
// reports success with a plausible value gets its value consumed — the save/restore idiom
// turns it into corrupted state, which is how a GetRenderState stub once blanked a whole UI.
// Reports the sentinel [1,0,0,255] when every unimplemented call refuses.
#include "d3d8/d3d8.h"
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  // Reads with nothing behind them must refuse rather than leave the buffer untouched.
  float plane[4] = {9, 9, 9, 9};
  if (dev->GetClipPlane(0, plane) == D3D_OK) { report_error("GetClipPlane claimed success"); return 1; }
  DWORD constants[4] = {9, 9, 9, 9};
  if (dev->GetVertexShaderConstant(0, constants, 1) == D3D_OK) { report_error("GetVertexShaderConstant claimed success"); return 1; }
  if (dev->GetPixelShaderConstant(0, constants, 1) == D3D_OK) { report_error("GetPixelShaderConstant claimed success"); return 1; }

  // Creation of things that do not exist must fail, so callers take their fallback path.
  DWORD handle = 0xFFFFFFFFu;
  if (dev->CreateVertexShader(nullptr, nullptr, &handle, 0) == D3D_OK) { report_error("CreateVertexShader claimed success"); return 1; }
  if (dev->CreatePixelShader(nullptr, &handle) == D3D_OK) { report_error("CreatePixelShader claimed success"); return 1; }

  // State blocks are the same save/restore trap in another API: recording nothing and then
  // "applying" it silently reverts nothing while the caller believes state was restored.
  DWORD token = 0xFFFFFFFFu;
  if (dev->CreateStateBlock(D3DSBT_ALL, &token) == D3D_OK) { report_error("CreateStateBlock claimed success"); return 1; }
  if (dev->BeginStateBlock() == D3D_OK) { report_error("BeginStateBlock claimed success"); return 1; }
  if (dev->ApplyStateBlock(0) == D3D_OK) { report_error("ApplyStateBlock claimed success"); return 1; }

  // Only the backbuffer exists; switching to any other target must be refused, not ignored.
  if (dev->SetRenderTarget((IDirect3DSurface8*)0x1, nullptr) == D3D_OK) { report_error("SetRenderTarget claimed success"); return 1; }

  // Clip planes are not implemented, so the cap must not advertise any.
  D3DCAPS8 caps{};
  dev->GetDeviceCaps(&caps);
  if (caps.MaxUserClipPlanes != 0) { report_error("caps advertise clip planes that do nothing"); return 1; }
  if (caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) { report_error("caps advertise cube maps but CreateCubeTexture fails"); return 1; }

  dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
```

- [ ] **Step 2: Register the smoke**

In `CMakeLists.txt`:

```cmake
add_executable(honest_stubs_smoke runtime/test/honest_stubs_smoke.cpp)
target_include_directories(honest_stubs_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(honest_stubs_smoke PRIVATE dx8_d3d8webgl)
target_link_options(honest_stubs_smoke PRIVATE ${DX8_WEBGL_LINK})
set_target_properties(honest_stubs_smoke PROPERTIES SUFFIX ".js")
```

In `web-runtime/test/phase2.gpu.test.mjs`:

```js
  ['honest_stubs_smoke', [1, 0, 0, 255]],  // unimplemented entry points refuse, never lie
```

- [ ] **Step 3: Run it and watch it fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `honest_stubs_smoke: GetClipPlane claimed success`.

- [ ] **Step 4: Demote the lying stubs in `device.cpp`**

Replace each listed method with the version below. Keep every `warn_once` call that is already
there — it is the porter-facing signal.

```cpp
  // Reads with no state behind them: refuse, so the caller cannot consume a fabricated value.
  HRESULT GetClipPlane(DWORD, float*) override { warn_once("GetClipPlane"); return D3DERR_INVALIDCALL; }
  HRESULT SetClipPlane(DWORD, const float*) override { warn_once("SetClipPlane"); return D3DERR_INVALIDCALL; }
  HRESULT GetPaletteEntries(UINT, PALETTEENTRY*) override { warn_once("GetPaletteEntries"); return D3DERR_INVALIDCALL; }
  HRESULT SetPaletteEntries(UINT, const PALETTEENTRY*) override { warn_once("SetPaletteEntries"); return D3DERR_INVALIDCALL; }
  HRESULT GetVertexShaderConstant(DWORD, void*, DWORD) override { warn_once("GetVertexShaderConstant"); return D3DERR_INVALIDCALL; }
  HRESULT GetPixelShaderConstant(DWORD, void*, DWORD) override { warn_once("GetPixelShaderConstant"); return D3DERR_INVALIDCALL; }

  // Nothing is created, so creation must fail and the caller must take its fallback path.
  HRESULT CreateVertexShader(const DWORD*, const DWORD*, DWORD* h, DWORD) override {
    if (h) *h = 0; warn_once("CreateVertexShader"); return D3DERR_INVALIDCALL;
  }
  HRESULT CreatePixelShader(const DWORD*, DWORD* h) override {
    if (h) *h = 0; warn_once("CreatePixelShader"); return D3DERR_INVALIDCALL;
  }

  // State blocks record nothing. Reporting success would make "restore" a silent no-op — the
  // same class of failure as a GetRenderState that always answers zero.
  HRESULT BeginStateBlock() override { warn_once("BeginStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT EndStateBlock(DWORD* t) override { if (t) *t = 0; warn_once("EndStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE, DWORD* t) override { if (t) *t = 0; warn_once("CreateStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT ApplyStateBlock(DWORD) override { warn_once("ApplyStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT CaptureStateBlock(DWORD) override { warn_once("CaptureStateBlock"); return D3DERR_INVALIDCALL; }
  HRESULT DeleteStateBlock(DWORD) override { return D3D_OK; }   // deleting nothing is honest

  // Only the backbuffer exists (CreateRenderTarget/CreateDepthStencilSurface both fail), so a
  // request to render elsewhere must be refused rather than silently drawn to the screen.
  HRESULT SetRenderTarget(IDirect3DSurface8* target, IDirect3DSurface8*) override {
    if (!target) return D3D_OK;   // "restore the default target" — already there
    warn_once("SetRenderTarget"); return D3DERR_INVALIDCALL;
  }

  // No texture-memory accounting exists; reporting a made-up figure feeds quality heuristics.
  // 0 reads as "unknown" to callers that check, and cannot masquerade as a real budget.
  UINT GetAvailableTextureMem() override { warn_once("GetAvailableTextureMem"); return 0; }
```

- [ ] **Step 5: Stop over-claiming in `caps_fill.h`**

Remove `D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_MIPCUBEMAP` from `TextureCaps` (both
`CreateCubeTexture` and cube `CheckDeviceFormat` refuse), and set `MaxUserClipPlanes = 0`
(`SetClipPlane` refuses). Leave `StencilCaps` as-is — stencil is genuinely implemented.

```cpp
  c->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP |
                   D3DPTEXTURECAPS_PROJECTED;   // no cube maps: CreateCubeTexture refuses
```
```cpp
  c->MaxActiveLights = 8; c->MaxUserClipPlanes = 0;   // SetClipPlane is not implemented
```

- [ ] **Step 6: Run the test — it must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: `ok — honest_stubs_smoke cleared to [1,0,0,255]`, everything else still `ok`.

- [ ] **Step 7: Commit**

```bash
git add runtime/d3d8webgl/device.cpp runtime/d3d8webgl/caps_fill.h runtime/test/honest_stubs_smoke.cpp \
        CMakeLists.txt web-runtime/test/phase2.gpu.test.mjs
git commit -m "d3d8webgl: unimplemented entry points refuse instead of reporting success"
```

---

### Task 7: `dx8wasm_has_cap` stops denying stencil

The reverse lie: `dx8wasm_has_cap` returns `0` for every capability, including
`DX8WASM_CAP_STENCIL`, which is fully implemented (and, since the `GetRenderState` fix,
correct). A porter reading the contract avoids a feature that works.

**Files:**
- Modify: `runtime/runtime.cpp:34-38`
- Modify: `runtime/test/abi_smoke.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `dx8wasm_has_cap(DX8WASM_CAP_STENCIL) == 1`; all other caps stay `0`.

- [ ] **Step 1: Write the failing test**

In `runtime/test/abi_smoke.cpp`, alongside the existing contract assertions:

```cpp
  // Stencil is implemented (Set/GetRenderState + per-draw apply_raster_masks), so the cap
  // must say so. Denying a working feature is as misleading as claiming a missing one.
  if (dx8wasm_has_cap(DX8WASM_CAP_STENCIL) != 1) { report_error("stencil cap denied but implemented"); return 1; }
  if (dx8wasm_has_cap(DX8WASM_CAP_CUBE_TEXTURE) != 0) { report_error("cube texture cap claimed but absent"); return 1; }
```

Read `abi_smoke.cpp` first and match its existing error-reporting helper — it may use a
different name than `report_error`.

- [ ] **Step 2: Run it and watch it fail**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: FAIL — `abi_smoke: stencil cap denied but implemented`.

- [ ] **Step 3: Implement**

Replace the body of `dx8wasm_has_cap` in `runtime/runtime.cpp`:

```cpp
// The WebGL2 fixed-function backend implements the FF pipeline (see docs/CONFORMANCE.md).
// Everything still missing reports 0 so a porter checks before relying on it — but a feature
// that IS implemented must report 1, or the introspection is misleading in the other direction.
int dx8wasm_has_cap(dx8wasm_cap cap) {
  switch (cap) {
    case DX8WASM_CAP_STENCIL: return 1;   // Set/GetRenderState + per-draw apply_raster_masks
    default: return 0;                    // BC/cube/volume, vertex-blend, point sprites, SM1.x
  }
}
```

- [ ] **Step 4: Run the test — it must pass**

```bash
cmake --build build/emscripten && node web-runtime/test/phase2.gpu.test.mjs
```
Expected: all `ok`.

- [ ] **Step 5: Commit**

```bash
git add runtime/runtime.cpp runtime/test/abi_smoke.cpp
git commit -m "runtime: report the stencil capability that is actually implemented"
```

---

### Task 8: Verify against the real game, then document

The smokes render to a 4×4 canvas and cannot see a format-selection regression. Task 2 changes
which formats WW3D believes exist, which is exactly the machinery behind two shipped rendering
bugs (blocky textures, A4R4G4B4 alpha loss). This task is the gate.

**Files:**
- Modify: `docs/CONFORMANCE.md` (regenerated), `docs/SDK_REFERENCE.md`
- Modify: `../generals-dx8wasm/docs/OPEN-ITEMS.md`

- [ ] **Step 1: Full CI**

```bash
bash scripts/ci.sh
```
Expected: `ALL GREEN`.

- [ ] **Step 2: Rebuild the game against the changed runtime**

```bash
cd ../generals-dx8wasm && BUILD_TYPE=Release bash scripts/build-engine.sh z_generals
```
Expected: exit 0, and `device.cpp.o` freshly rebuilt under
`build/wasm-engine-release/CMakeFiles/dx8wasm_backend.dir/`. Confirm the artifact was relinked
rather than assuming it — check the mtime of `build/wasm-engine-release/GeneralsMD/GeneralsXZH.wasm`.

- [ ] **Step 3: Capture the menu on a real GPU and compare against the known-good frame**

```bash
GX_BUILD_DIR=build/wasm-engine-release AUTO_PORT=1 PORT=8100 node scripts/serve-game.mjs &
CAPGPU=1 node scripts/cap-shot.mjs http://127.0.0.1:8100/ /tmp/after-honest-stubs.png 120000
```
Open the PNG and **look at it**. Required: full main menu, readable text, correct cursor,
no black or opaque squares where transparency belongs (that is the signature of a 16-bit
format fallback). Compare against `/tmp/options-lod-none.png` from the previous session.
Check `/tmp/after-honest-stubs.log` for `unhandled` coverage lines that were not there before.

- [ ] **Step 4: Capture an in-game skirmish**

```bash
MAP='Maps\Alpine Assault.map' GX_BUILD_DIR=build/wasm-engine-release PORT=8144 node scripts/serve-game.mjs &
CAPGPU=1 node scripts/cap-shot.mjs http://127.0.0.1:8144/ /tmp/after-honest-stubs-game.png 160000
```
Required: terrain textured (not checkerboard), units and HUD present, particles/smoke still
transparent. If any of these regress, the cause is almost certainly Task 2 refusing a format
WW3D actually needs — widen `texfmt::supported` to match reality rather than reverting the
honesty change, and add the format to `caps_query_smoke`.

- [ ] **Step 5: Regenerate the conformance matrix**

```bash
cd ../dx8wasm && node scripts/conformance.mjs
```
Expected: `docs/CONFORMANCE.md` rewritten. It should now be able to report unhandled texture
stage states, which it structurally could not before Task 5.

- [ ] **Step 6: Document the contract in `docs/SDK_REFERENCE.md`**

Add a short section titled **"Stubs fail loudly"** stating the three rules this plan enforces:
a `Get*` for tracked state answers from that state; anything unimplemented returns
`D3DERR_INVALIDCALL`/`D3DERR_NOTAVAILABLE`; capability queries derive from the same predicate
the implementation uses. Note that `dx8wasm_get_coverage` is the porter's read-out, and cite
the `GetRenderState`/`COLORWRITEENABLE` incident as the worked example of why.

- [ ] **Step 7: Record the outcome game-side**

In `../generals-dx8wasm/docs/OPEN-ITEMS.md` §0, extend the existing Detail=High entry with a
line noting the follow-up audit: which stubs were found, that they are now fixed upstream in
dx8wasm, and the commit range.

- [ ] **Step 8: Commit both repos**

```bash
cd ../dx8wasm && git add docs/ && git commit -m "docs: record the honest-stub contract and regenerate the conformance matrix"
cd ../generals-dx8wasm && git add docs/OPEN-ITEMS.md && git commit -m "docs: record the dx8wasm stub audit outcome"
```

---

## Self-Review

**Spec coverage** — every audit finding maps to a task:

| Audit item | Task |
|---|---|
| Tier 1.1 `CheckDeviceFormat` & friends blanket `D3D_OK` | 1, 2 |
| Tier 1.2 `GetAdapterIdentifier` writes nothing | 3 |
| Tier 1.3 `GetTextureStageState` → 0 | 4 |
| Tier 1.4 stage states silently swallowed | 5 |
| Tier 2 uninitialised `Get*` (clip plane, palette, shader constants) | 6 |
| Tier 2 `CreateVertex/PixelShader` false success | 6 |
| Tier 2 state blocks false success | 6 |
| Tier 2 `GetAvailableTextureMem` fabricated 256 MB | 6 |
| Tier 2 `SetRenderTarget` silent no-op | 6 |
| Tier 2 `SetClipPlane` + `MaxUserClipPlanes` over-claim | 6 |
| Tier 2 cube-map caps over-claim | 6 |
| Tier 2 `dx8wasm_has_cap` denies working stencil | 7 |
| Tier 3 (honest already) | none — no action by design |

**Known risk, accepted:** Task 6 changes `GetAvailableTextureMem` from 256 MB to 0. No Generals
caller consumes it (verified: `Get_Available_Texture_Mem` has no call sites outside the wrapper
itself), so the risk is confined to future ports, where 0-as-unknown is the safer default.

**Deliberate non-change:** `TestCooperativeLevel` keeps returning `D3D_OK`. WebGL context loss
is a real event but it is detected in `platform::present()` and surfaced to the page, and there
is no D3D-style device-reset path to recover into — making this fail would send the engine down
a reset path that cannot succeed. Documented rather than "fixed".
