// SPDX-License-Identifier: GPL-3.0-only
// Clean-room D3D8 subset for the 2.0-2.2 slice (device + Clear + Present). Only
// the methods we implement are declared; full-ABI/Generals compat is a later task.
#ifndef DX8WASM_D3D8_H
#define DX8WASM_D3D8_H
#include <cstdint>

using HRESULT = int32_t;
using D3DCOLOR = uint32_t;   // 0xAARRGGBB
using HWND = void*;
#define D3D_OK 0
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cL)
#define D3D_SDK_VERSION 220
#define D3DCLEAR_TARGET  0x00000001u
#define D3DCLEAR_ZBUFFER 0x00000002u
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040u

enum D3DDEVTYPE { D3DDEVTYPE_HAL = 1 };
enum D3DFORMAT { D3DFMT_UNKNOWN = 0, D3DFMT_A8R8G8B8 = 21, D3DFMT_X8R8G8B8 = 22 };
enum D3DSWAPEFFECT { D3DSWAPEFFECT_DISCARD = 1 };

struct D3DRECT { long x1, y1, x2, y2; };

struct D3DPRESENT_PARAMETERS {
  uint32_t BackBufferWidth, BackBufferHeight;
  D3DFORMAT BackBufferFormat;
  uint32_t BackBufferCount, MultiSampleType;
  D3DSWAPEFFECT SwapEffect;
  HWND hDeviceWindow;
  int32_t Windowed, EnableAutoDepthStencil;
  D3DFORMAT AutoDepthStencilFormat;
  uint32_t Flags, FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
};

struct IDirect3DDevice8 {
  virtual uint32_t AddRef() = 0;
  virtual uint32_t Release() = 0;
  virtual HRESULT Clear(uint32_t Count, const D3DRECT* pRects, uint32_t Flags,
                        D3DCOLOR Color, float Z, uint32_t Stencil) = 0;
  virtual HRESULT Present(const D3DRECT* pSourceRect, const D3DRECT* pDestRect,
                          HWND hDestWindowOverride, const void* pDirtyRegion) = 0;
  virtual ~IDirect3DDevice8() = default;
};

struct IDirect3D8 {
  virtual uint32_t AddRef() = 0;
  virtual uint32_t Release() = 0;
  virtual HRESULT CreateDevice(uint32_t Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow,
                               uint32_t BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters,
                               IDirect3DDevice8** ppReturnedDeviceInterface) = 0;
  virtual ~IDirect3D8() = default;
};

extern "C" IDirect3D8* Direct3DCreate8(unsigned int SDKVersion);
#endif
