// SPDX-License-Identifier: GPL-3.0-only
// The IDirect3D8 factory. Full COM vtable; CreateDevice + adapter/caps queries
// are real enough for device creation, the rest are permissive stubs.
#include "d3d8/d3d8.h"
#include "caps_fill.h"   // shared fill_caps() — device.cpp reports the SAME caps
#include <cstring>
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
// canvas mode for exactly this reason. We offer the native canvas size, the 4:3
// pillarbox internal res the engine derives from it (see SDL3Main.cpp -xres/-yres),
// and 1024x768 as a legacy fallback — so whichever the engine requests, it matches.
struct AdapterMode { UINT w, h; };
inline UINT collect_modes(AdapterMode out[3]) {
  int vpW = MAIN_THREAD_EM_ASM_INT({ return (window.innerWidth | 0) || 1024; });
  int vpH = MAIN_THREAD_EM_ASM_INT({ return (window.innerHeight | 0) || 768; });
  if (vpW < 320) vpW = 1024;
  if (vpH < 240) vpH = 768;
  int winW = vpW, winH = vpH;                       // largest 4:3 box that fits (matches SDL3Main)
  if (winW * 3 > winH * 4) winW = (winH * 4) / 3;
  else if (winW * 3 < winH * 4) winH = (winW * 3) / 4;
  out[0] = { (UINT)(vpW & ~1),  (UINT)(vpH & ~1)  };  // native canvas
  out[1] = { (UINT)(winW & ~1), (UINT)(winH & ~1) };  // 4:3 pillarbox internal res
  out[2] = { 1024, 768 };                            // legacy fallback
  return 3;
}

struct D3D8 : IDirect3D8 {
  ULONG refs = 1;
  HRESULT QueryInterface(REFIID, void** o) override { if (o) { *o = this; ++refs; } return D3D_OK; }
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }

  HRESULT RegisterSoftwareDevice(void*) override { return D3DERR_INVALIDCALL; }
  UINT GetAdapterCount() override { return 1; }
  HRESULT GetAdapterIdentifier(UINT, DWORD, void*) override { return D3D_OK; }
  UINT GetAdapterModeCount(UINT) override { AdapterMode md[3]; return collect_modes(md); }
  HRESULT EnumAdapterModes(UINT, UINT i, D3DDISPLAYMODE* m) override {
    AdapterMode md[3]; UINT n = collect_modes(md);
    if (!m || i >= n) return D3DERR_INVALIDCALL;
    m->Width = md[i].w; m->Height = md[i].h; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
  }
  HRESULT GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* m) override {
    if (!m) return D3DERR_INVALIDCALL;
    AdapterMode md[3]; collect_modes(md);
    m->Width = md[0].w; m->Height = md[0].h; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8;   // native canvas
    return D3D_OK;
  }
  HRESULT CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, BOOL) override { return D3D_OK; }
  HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT) override { return D3D_OK; }
  HRESULT CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE) override { return D3D_OK; }
  HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override { return D3D_OK; }
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
