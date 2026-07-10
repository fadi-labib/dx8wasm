// SPDX-License-Identifier: GPL-3.0-only
// Clean-room D3D8 subset. Grows one vertical slice at a time; only the methods
// we implement are declared. Full-ABI/Generals compat is a later task.
//   2.0-2.2: device + Clear + Present.
//   2.3:     vertex/index buffers, FVF, transforms, DrawIndexedPrimitive.
#ifndef DX8WASM_D3D8_H
#define DX8WASM_D3D8_H
#include <cstdint>

using HRESULT = int32_t;
using D3DCOLOR = uint32_t;   // 0xAARRGGBB
using HWND = void*;
using DWORD = uint32_t;
using UINT = uint32_t;
using BYTE = uint8_t;
#define D3D_OK 0
#define D3DERR_INVALIDCALL ((HRESULT)0x8876086cL)
#define D3D_SDK_VERSION 220
#define D3DCLEAR_TARGET  0x00000001u
#define D3DCLEAR_ZBUFFER 0x00000002u
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040u

// Flexible vertex format bits (subset).
#define D3DFVF_XYZ     0x0002u
#define D3DFVF_XYZRHW  0x0004u
#define D3DFVF_DIFFUSE 0x0040u

enum D3DDEVTYPE { D3DDEVTYPE_HAL = 1 };
enum D3DFORMAT { D3DFMT_UNKNOWN = 0, D3DFMT_A8R8G8B8 = 21, D3DFMT_X8R8G8B8 = 22,
                 D3DFMT_INDEX16 = 101 };
enum D3DSWAPEFFECT { D3DSWAPEFFECT_DISCARD = 1 };
enum D3DPOOL { D3DPOOL_DEFAULT = 0, D3DPOOL_MANAGED = 1 };
enum D3DPRIMITIVETYPE { D3DPT_TRIANGLELIST = 4 };
// D3DTS_WORLD is the classic 256 alias for D3DTS_WORLDMATRIX(0).
enum D3DTRANSFORMSTATETYPE { D3DTS_VIEW = 2, D3DTS_PROJECTION = 3, D3DTS_WORLD = 256 };

struct D3DRECT { long x1, y1, x2, y2; };
struct D3DMATRIX { float m[4][4]; };   // row-major, D3D convention

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

struct IDirect3DVertexBuffer8 {
  virtual uint32_t AddRef() = 0;
  virtual uint32_t Release() = 0;
  virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE** ppbData, DWORD Flags) = 0;
  virtual HRESULT Unlock() = 0;
  virtual ~IDirect3DVertexBuffer8() = default;
};

struct IDirect3DIndexBuffer8 {
  virtual uint32_t AddRef() = 0;
  virtual uint32_t Release() = 0;
  virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE** ppbData, DWORD Flags) = 0;
  virtual HRESULT Unlock() = 0;
  virtual ~IDirect3DIndexBuffer8() = default;
};

struct IDirect3DDevice8 {
  virtual uint32_t AddRef() = 0;
  virtual uint32_t Release() = 0;
  virtual HRESULT Clear(uint32_t Count, const D3DRECT* pRects, uint32_t Flags,
                        D3DCOLOR Color, float Z, uint32_t Stencil) = 0;
  virtual HRESULT Present(const D3DRECT* pSourceRect, const D3DRECT* pDestRect,
                          HWND hDestWindowOverride, const void* pDirtyRegion) = 0;
  virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                     IDirect3DVertexBuffer8** ppVertexBuffer) = 0;
  virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                    IDirect3DIndexBuffer8** ppIndexBuffer) = 0;
  virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData,
                                  UINT Stride) = 0;
  virtual HRESULT SetIndices(IDirect3DIndexBuffer8* pIndexData, UINT BaseVertexIndex) = 0;
  virtual HRESULT SetVertexShader(DWORD Handle) = 0;   // FVF code for fixed-function
  virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) = 0;
  virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, UINT MinIndex, UINT NumVertices,
                                       UINT StartIndex, UINT PrimitiveCount) = 0;
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
