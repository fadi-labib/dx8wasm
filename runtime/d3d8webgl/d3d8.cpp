// SPDX-License-Identifier: GPL-3.0-only
// The IDirect3D8 factory. Full COM vtable; CreateDevice + adapter/caps queries
// are real enough for device creation, the rest are permissive stubs.
#include "d3d8/d3d8.h"
#include "caps_fill.h"   // shared fill_caps() — device.cpp reports the SAME caps
#include "format_support.h"   // the capability queries answer from the texture path's predicate
#include <cstring>
#include <cstdio>
#include <emscripten.h>  // read the real canvas size for adapter mode enumeration

IDirect3DDevice8* dx8_create_device(int w, int h);   // from device.cpp

namespace {

// Report display modes that MATCH the resolution the engine actually requests, so
// DX8Wrapper::Find_Color_Mode gets an exact width/height/X8R8G8B8 hit and picks a
// 32-bit backbuffer. Previously we hardcoded ONE 1024x768 mode; at any other render
// resolution Find_Color_Mode found no match and fell back to BitDepth=16, where
// Get_Valid_Texture_Format strips texture alpha (A8R8G8B8 -> R5G6B5 / A4R4G4B4). That
// turned every transparent sprite (mouse cursor, smoke/particle billboards, light
// beams) into an opaque square. The reference d3d8webgl port enumerates the native
// canvas mode for exactly this reason.
//
// What the engine requests is the viewport CLAMPED to its 4:3..16:9 aspect band (GeneralsX
// W3DDisplay.cpp clampWidthToAspectBand, applied at boot since 2026-08-28 as well as on every
// resize), with a 1024x768 floor. So the list must contain, for the viewport's height, the
// native size AND both band edges -- the 4:3 box (tall viewports) and the 16:9 box (ultrawide
// viewports) -- plus the legacy 1024x768. The 16:9 box was missing: on a 21:9 window the engine
// asked for 1920x1080, found no mode, fell back to 16-bit, and the player saw black squares
// around every transparent icon, a black 3D scene behind the menu and a misplaced HUD
// (generals.fadilabib.com, 2026-08-28). Any viewport aspect inside the band is the native
// entry; either side of it is one of the boxes; below the floor is 1024x768.
struct AdapterMode { UINT w, h; };
enum { ADAPTER_MODE_COUNT = 4 };
inline UINT collect_modes(AdapterMode out[ADAPTER_MODE_COUNT]) {
  int vpW = MAIN_THREAD_EM_ASM_INT({ return (window.innerWidth | 0) || 1024; });
  int vpH = MAIN_THREAD_EM_ASM_INT({ return (window.innerHeight | 0) || 768; });
  if (vpW < 320) vpW = 1024;
  if (vpH < 240) vpH = 768;
  int boxW43 = vpW, boxH43 = vpH;                   // largest 4:3 box that fits (matches SDL3Main's old default)
  if (boxW43 * 3 > boxH43 * 4) boxW43 = (boxH43 * 4) / 3;
  else if (boxW43 * 3 < boxH43 * 4) boxH43 = (boxW43 * 3) / 4;
  const int boxW169 = (vpH * 16) / 9;               // the band's wide edge for this height
  out[0] = { (UINT)(vpW & ~1),    (UINT)(vpH & ~1)   };  // native canvas (aspect inside the band)
  out[1] = { (UINT)(boxW43 & ~1), (UINT)(boxH43 & ~1) };  // 4:3 box (viewport taller than 4:3)
  out[2] = { (UINT)(boxW169 & ~1), (UINT)(vpH & ~1)   };  // 16:9 box (viewport wider than 16:9)
  out[3] = { 1024, 768 };                            // legacy fallback / the engine's floor
  return ADAPTER_MODE_COUNT;
}

struct D3D8 : IDirect3D8 {
  ULONG refs = 1;
  HRESULT QueryInterface(REFIID, void** o) override { if (o) { *o = this; ++refs; } return D3D_OK; }
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }

  HRESULT RegisterSoftwareDevice(void*) override { return D3DERR_INVALIDCALL; }
  UINT GetAdapterCount() override { return 1; }
  // Name ourselves honestly. Leaving this blank is not neutral: the engine reads the strings
  // and ids to classify the GPU, and an all-zero identifier reads as "unknown card", which
  // pushes quality heuristics to their lowest tier. Vendor/device ids stay 0 on purpose —
  // claiming an NVIDIA or ATI id would trigger vendor-specific driver workarounds. The driver
  // name must not start with '3': dx8caps.cpp reads that as 3dfx.
  HRESULT GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER8* id) override {
    if (!id) return D3DERR_INVALIDCALL;
    *id = D3DADAPTER_IDENTIFIER8{};
    std::snprintf(id->Driver, sizeof id->Driver, "dx8wasm");
    std::snprintf(id->Description, sizeof id->Description, "dx8wasm (D3D8 over WebGL2)");
    id->DriverVersion = 1;
    return D3D_OK;
  }
  UINT GetAdapterModeCount(UINT) override { AdapterMode md[ADAPTER_MODE_COUNT]; return collect_modes(md); }
  HRESULT EnumAdapterModes(UINT, UINT i, D3DDISPLAYMODE* m) override {
    AdapterMode md[ADAPTER_MODE_COUNT]; UINT n = collect_modes(md);
    if (!m || i >= n) return D3DERR_INVALIDCALL;
    m->Width = md[i].w; m->Height = md[i].h; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
  }
  HRESULT GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* m) override {
    if (!m) return D3DERR_INVALIDCALL;
    AdapterMode md[ADAPTER_MODE_COUNT]; collect_modes(md);
    m->Width = md[0].w; m->Height = md[0].h; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8;   // native canvas
    return D3D_OK;
  }
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
  // and behaviour cannot drift. Usages and resource types with no Create* path are refused —
  // a blanket yes here is what lets an engine commit to a format that fails at upload.
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
  HRESULT GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS8* c) override { if (!c) return D3DERR_INVALIDCALL; fill_caps(c); return D3D_OK; }
  HMONITOR GetAdapterMonitor(UINT) override { return nullptr; }

  HRESULT CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice8** out) override {
    if (!pp || !out) return D3DERR_INVALIDCALL;
    *out = nullptr;   // define the out-param on every failure path (foundation ABI)
    IDirect3DDevice8* dev = dx8_create_device((int)pp->BackBufferWidth, (int)pp->BackBufferHeight);
    if (!dev) return D3DERR_INVALIDCALL;
    *out = dev;
    return D3D_OK;
  }
};
} // namespace

extern "C" IDirect3D8* Direct3DCreate8(unsigned int) { return new D3D8(); }
