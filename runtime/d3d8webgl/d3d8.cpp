// SPDX-License-Identifier: GPL-3.0-only
// The IDirect3D8 factory. Full COM vtable; CreateDevice + adapter/caps queries
// are real enough for device creation, the rest are permissive stubs.
#include "d3d8/d3d8.h"
#include "caps_fill.h"   // shared fill_caps() — device.cpp reports the SAME caps
#include <cstring>

IDirect3DDevice8* dx8_create_device(int w, int h);   // from device.cpp

namespace {

struct D3D8 : IDirect3D8 {
  ULONG refs = 1;
  HRESULT QueryInterface(REFIID, void** o) override { if (o) { *o = this; ++refs; } return D3D_OK; }
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }

  HRESULT RegisterSoftwareDevice(void*) override { return D3DERR_INVALIDCALL; }
  UINT GetAdapterCount() override { return 1; }
  HRESULT GetAdapterIdentifier(UINT, DWORD, void*) override { return D3D_OK; }
  UINT GetAdapterModeCount(UINT) override { return 1; }
  HRESULT EnumAdapterModes(UINT, UINT, D3DDISPLAYMODE* m) override {
    if (m) { m->Width = 1024; m->Height = 768; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8; }
    return D3D_OK;
  }
  HRESULT GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* m) override {
    if (m) { m->Width = 1024; m->Height = 768; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8; }
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
