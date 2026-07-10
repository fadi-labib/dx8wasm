// SPDX-License-Identifier: GPL-3.0-only
#include "d3d8/d3d8.h"

IDirect3DDevice8* dx8_create_device(int w, int h);   // from device.cpp

struct D3D8 : IDirect3D8 {
  uint32_t refs = 1;
  uint32_t AddRef() override { return ++refs; }
  uint32_t Release() override { uint32_t r = --refs; if (!r) delete this; return r; }
  HRESULT CreateDevice(uint32_t, D3DDEVTYPE, HWND, uint32_t,
                       D3DPRESENT_PARAMETERS* pp, IDirect3DDevice8** out) override {
    if (!pp || !out) return D3DERR_INVALIDCALL;
    *out = nullptr;   // define the out-param on every failure path (foundation ABI)
    IDirect3DDevice8* dev = dx8_create_device((int)pp->BackBufferWidth, (int)pp->BackBufferHeight);
    if (!dev) return D3DERR_INVALIDCALL;
    *out = dev;
    return D3D_OK;
  }
};

extern "C" IDirect3D8* Direct3DCreate8(unsigned int) { return new D3D8(); }
