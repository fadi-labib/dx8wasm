// SPDX-License-Identifier: GPL-3.0-only
// Clean-room D3D8 API declared from the public (documented) interface — the FULL
// COM ABI, in canonical vtable order, so a game compiled against any standard
// D3D8 header (MinGW-w64 / DXVK-native / real Windows) dispatches to the right
// slot. Only a subset is implemented; the rest are honest stubs that log + count
// via the coverage layer. See docs/CONFORMANCE.md for what's real.
#ifndef DX8WASM_D3D8_H
#define DX8WASM_D3D8_H
#include <cstdint>

// --- Base Win32/COM types (standalone; a real game gets these from windows.h) --
using HRESULT = int32_t;
using ULONG   = uint32_t;
using DWORD   = uint32_t;
using UINT    = uint32_t;
using WORD    = uint16_t;
using BYTE    = uint8_t;
using BOOL    = int32_t;
using LONG    = int32_t;
using D3DCOLOR = uint32_t;   // 0xAARRGGBB
using HWND    = void*;
using HMONITOR = void*;

struct RECT { LONG left, top, right, bottom; };
struct POINT { LONG x, y; };
struct PALETTEENTRY { BYTE peRed, peGreen, peBlue, peFlags; };
struct RGNDATA;   // opaque
struct GUID { uint32_t Data1; uint16_t Data2, Data3; uint8_t Data4[8]; };
using IID = GUID;
using REFIID = const GUID&;

#define D3D_OK 0
#define D3DERR_INVALIDCALL      ((HRESULT)0x8876086cL)
#define D3DERR_NOTAVAILABLE     ((HRESULT)0x8876086aL)
#define D3DERR_OUTOFVIDEOMEMORY ((HRESULT)0x8876017cL)
#define E_NOTIMPL               ((HRESULT)0x80004001L)
#define E_NOINTERFACE           ((HRESULT)0x80004002L)
#define D3D_SDK_VERSION 220
#define D3DCLEAR_TARGET  0x00000001u
#define D3DCLEAR_ZBUFFER 0x00000002u
#define D3DCLEAR_STENCIL 0x00000004u
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040u
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020u
#define D3DADAPTER_DEFAULT 0

// --- FVF ---------------------------------------------------------------------
#define D3DFVF_XYZ      0x0002u
#define D3DFVF_XYZRHW   0x0004u
#define D3DFVF_NORMAL   0x0010u
#define D3DFVF_DIFFUSE  0x0040u
#define D3DFVF_SPECULAR 0x0080u
#define D3DFVF_TEX1     0x0100u
#define D3DFVF_TEX2     0x0200u
// Texture-coordinate count is a 4-bit field, not a bitmask: sets = (fvf & MASK) >> SHIFT.
#define D3DFVF_TEXCOUNT_MASK  0x0f00u
#define D3DFVF_TEXCOUNT_SHIFT 8u

// --- Enums (values are the public API; enums may be sparse) ------------------
enum D3DDEVTYPE { D3DDEVTYPE_HAL = 1, D3DDEVTYPE_REF = 2, D3DDEVTYPE_SW = 3 };
enum D3DFORMAT {
  D3DFMT_UNKNOWN = 0, D3DFMT_R5G6B5 = 23, D3DFMT_X1R5G5B5 = 24, D3DFMT_A1R5G5B5 = 25,
  D3DFMT_A4R4G4B4 = 26, D3DFMT_R8G8B8 = 20, D3DFMT_A8R8G8B8 = 21, D3DFMT_X8R8G8B8 = 22,
  D3DFMT_X4R4G4B4 = 30, D3DFMT_L8 = 50, D3DFMT_A8L8 = 51,
  D3DFMT_A8 = 28, D3DFMT_D16 = 80, D3DFMT_D24S8 = 75, D3DFMT_D24X8 = 77, D3DFMT_D32 = 71,
  D3DFMT_INDEX16 = 101, D3DFMT_INDEX32 = 102,
  D3DFMT_DXT1 = 0x31545844, D3DFMT_DXT3 = 0x33545844, D3DFMT_DXT5 = 0x35545844,
};
enum D3DPOOL { D3DPOOL_DEFAULT = 0, D3DPOOL_MANAGED = 1, D3DPOOL_SYSTEMMEM = 2, D3DPOOL_SCRATCH = 3 };
enum D3DRESOURCETYPE { D3DRTYPE_SURFACE = 1, D3DRTYPE_VOLUME = 2, D3DRTYPE_TEXTURE = 3,
                       D3DRTYPE_VOLUMETEXTURE = 4, D3DRTYPE_CUBETEXTURE = 5,
                       D3DRTYPE_VERTEXBUFFER = 6, D3DRTYPE_INDEXBUFFER = 7 };
enum D3DSWAPEFFECT { D3DSWAPEFFECT_DISCARD = 1, D3DSWAPEFFECT_FLIP = 2, D3DSWAPEFFECT_COPY = 3 };
enum D3DMULTISAMPLE_TYPE { D3DMULTISAMPLE_NONE = 0 };
enum D3DBACKBUFFER_TYPE { D3DBACKBUFFER_TYPE_MONO = 0 };
enum D3DSTATEBLOCKTYPE { D3DSBT_ALL = 1, D3DSBT_PIXELSTATE = 2, D3DSBT_VERTEXSTATE = 3 };
enum D3DPRIMITIVETYPE {
  D3DPT_POINTLIST = 1, D3DPT_LINELIST = 2, D3DPT_LINESTRIP = 3,
  D3DPT_TRIANGLELIST = 4, D3DPT_TRIANGLESTRIP = 5, D3DPT_TRIANGLEFAN = 6
};
enum D3DTRANSFORMSTATETYPE { D3DTS_VIEW = 2, D3DTS_PROJECTION = 3, D3DTS_TEXTURE0 = 16, D3DTS_WORLD = 256 };
enum D3DTEXTUREOP {
  D3DTOP_DISABLE = 1, D3DTOP_SELECTARG1 = 2, D3DTOP_SELECTARG2 = 3, D3DTOP_MODULATE = 4,
  D3DTOP_MODULATE2X = 5, D3DTOP_MODULATE4X = 6, D3DTOP_ADD = 7, D3DTOP_ADDSIGNED = 8,
  D3DTOP_ADDSIGNED2X = 9, D3DTOP_SUBTRACT = 10, D3DTOP_ADDSMOOTH = 11,
  D3DTOP_BLENDDIFFUSEALPHA = 12, D3DTOP_BLENDTEXTUREALPHA = 13, D3DTOP_BLENDFACTORALPHA = 14,
  D3DTOP_BLENDCURRENTALPHA = 16, D3DTOP_DOTPRODUCT3 = 24
};
// Texture-stage argument selectors (low nibble) + modifier bits. Used by the
// multi-stage combiner: COMPLEMENT = 1-x, ALPHAREPLICATE = replicate .a to rgb.
enum D3DTEXTUREARG {
  D3DTA_DIFFUSE = 0, D3DTA_CURRENT = 1, D3DTA_TEXTURE = 2, D3DTA_TFACTOR = 3, D3DTA_SPECULAR = 4,
  D3DTA_SELECTMASK = 0x0f, D3DTA_COMPLEMENT = 0x10, D3DTA_ALPHAREPLICATE = 0x20
};
enum D3DTEXTURESTAGESTATETYPE {
  D3DTSS_COLOROP = 1, D3DTSS_COLORARG1 = 2, D3DTSS_COLORARG2 = 3, D3DTSS_ALPHAOP = 4,
  D3DTSS_ALPHAARG1 = 5, D3DTSS_ALPHAARG2 = 6, D3DTSS_TEXCOORDINDEX = 11,
  D3DTSS_ADDRESSU = 13, D3DTSS_ADDRESSV = 14, D3DTSS_MAGFILTER = 16, D3DTSS_MINFILTER = 17,
  D3DTSS_MIPFILTER = 18, D3DTSS_TEXTURETRANSFORMFLAGS = 24
};
// High 16 bits of D3DTSS_TEXCOORDINDEX select automatic texture-coord generation.
enum D3DTSS_TCI {
  D3DTSS_TCI_PASSTHRU = 0x00000000, D3DTSS_TCI_CAMERASPACENORMAL = 0x00010000,
  D3DTSS_TCI_CAMERASPACEPOSITION = 0x00020000, D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR = 0x00030000
};
// D3DTSS_TEXTURETRANSFORMFLAGS: a non-zero COUNTn enables the stage texture matrix.
enum D3DTEXTURETRANSFORMFLAGS {
  D3DTTFF_DISABLE = 0, D3DTTFF_COUNT1 = 1, D3DTTFF_COUNT2 = 2, D3DTTFF_COUNT3 = 3,
  D3DTTFF_COUNT4 = 4, D3DTTFF_PROJECTED = 256
};
// Sampler filter / address modes (D3DTSS_MIN/MAG/MIPFILTER, D3DTSS_ADDRESSU/V values).
enum D3DTEXTUREFILTERTYPE {
  D3DTEXF_NONE = 0, D3DTEXF_POINT = 1, D3DTEXF_LINEAR = 2, D3DTEXF_ANISOTROPIC = 3
};
enum D3DTEXTUREADDRESS {
  D3DTADDRESS_WRAP = 1, D3DTADDRESS_MIRROR = 2, D3DTADDRESS_CLAMP = 3,
  D3DTADDRESS_BORDER = 4, D3DTADDRESS_MIRRORONCE = 5
};
enum D3DRENDERSTATETYPE {
  D3DRS_ZENABLE = 7, D3DRS_FILLMODE = 8, D3DRS_SHADEMODE = 9, D3DRS_ZWRITEENABLE = 14,
  D3DRS_ALPHATESTENABLE = 15, D3DRS_SRCBLEND = 19, D3DRS_DESTBLEND = 20, D3DRS_CULLMODE = 22,
  D3DRS_ZFUNC = 23, D3DRS_ALPHAREF = 24, D3DRS_ALPHAFUNC = 25, D3DRS_DITHERENABLE = 26,
  D3DRS_ALPHABLENDENABLE = 27, D3DRS_FOGENABLE = 28, D3DRS_SPECULARENABLE = 29,
  D3DRS_FOGCOLOR = 34, D3DRS_FOGTABLEMODE = 35, D3DRS_FOGSTART = 36, D3DRS_FOGEND = 37,
  D3DRS_FOGDENSITY = 38, D3DRS_STENCILENABLE = 52,
  D3DRS_STENCILFAIL = 53, D3DRS_STENCILZFAIL = 54, D3DRS_STENCILPASS = 55,
  D3DRS_STENCILFUNC = 56, D3DRS_STENCILREF = 57, D3DRS_STENCILMASK = 58,
  D3DRS_STENCILWRITEMASK = 59, D3DRS_TEXTUREFACTOR = 60,
  D3DRS_LIGHTING = 137, D3DRS_AMBIENT = 139, D3DRS_FOGVERTEXMODE = 140,
  D3DRS_COLORVERTEX = 141, D3DRS_ZBIAS = 47,
  D3DRS_DIFFUSEMATERIALSOURCE = 145, D3DRS_SPECULARMATERIALSOURCE = 146,
  D3DRS_AMBIENTMATERIALSOURCE = 147, D3DRS_EMISSIVEMATERIALSOURCE = 148,
  D3DRS_COLORWRITEENABLE = 168
};
// Stencil operations (D3DRS_STENCILFAIL/ZFAIL/PASS values).
enum D3DSTENCILOP {
  D3DSTENCILOP_KEEP = 1, D3DSTENCILOP_ZERO = 2, D3DSTENCILOP_REPLACE = 3,
  D3DSTENCILOP_INCRSAT = 4, D3DSTENCILOP_DECRSAT = 5, D3DSTENCILOP_INVERT = 6,
  D3DSTENCILOP_INCR = 7, D3DSTENCILOP_DECR = 8
};
// Where fixed-function lighting reads each material color component from.
enum D3DMATERIALCOLORSOURCE { D3DMCS_MATERIAL = 0, D3DMCS_COLOR1 = 1, D3DMCS_COLOR2 = 2 };
enum D3DFOGMODE { D3DFOG_NONE = 0, D3DFOG_EXP = 1, D3DFOG_EXP2 = 2, D3DFOG_LINEAR = 3 };
enum D3DBLEND { D3DBLEND_ZERO = 1, D3DBLEND_ONE = 2, D3DBLEND_SRCCOLOR = 3, D3DBLEND_INVSRCCOLOR = 4,
                D3DBLEND_SRCALPHA = 5, D3DBLEND_INVSRCALPHA = 6, D3DBLEND_DESTALPHA = 7,
                D3DBLEND_INVDESTALPHA = 8, D3DBLEND_DESTCOLOR = 9, D3DBLEND_INVDESTCOLOR = 10 };
enum D3DCULL { D3DCULL_NONE = 1, D3DCULL_CW = 2, D3DCULL_CCW = 3 };
enum D3DCMPFUNC { D3DCMP_NEVER = 1, D3DCMP_LESS = 2, D3DCMP_EQUAL = 3, D3DCMP_LESSEQUAL = 4,
                  D3DCMP_GREATER = 5, D3DCMP_NOTEQUAL = 6, D3DCMP_GREATEREQUAL = 7, D3DCMP_ALWAYS = 8 };
enum D3DLIGHTTYPE { D3DLIGHT_POINT = 1, D3DLIGHT_SPOT = 2, D3DLIGHT_DIRECTIONAL = 3 };

// --- Structs -----------------------------------------------------------------
struct D3DRECT { long x1, y1, x2, y2; };
struct D3DMATRIX { float m[4][4]; };
struct D3DVECTOR { float x, y, z; };
struct D3DCOLORVALUE { float r, g, b, a; };
struct D3DLOCKED_RECT { int32_t Pitch; void* pBits; };
struct D3DDISPLAYMODE { UINT Width, Height, RefreshRate; D3DFORMAT Format; };
struct D3DVIEWPORT8 { DWORD X, Y, Width, Height; float MinZ, MaxZ; };
struct D3DGAMMARAMP { WORD red[256], green[256], blue[256]; };
struct D3DRASTER_STATUS { BOOL InVBlank; UINT ScanLine; };
struct D3DCLIPSTATUS8 { DWORD ClipUnion, ClipIntersection; };
struct D3DDEVICE_CREATION_PARAMETERS { UINT AdapterOrdinal; D3DDEVTYPE DeviceType; HWND hFocusWindow; DWORD BehaviorFlags; };
struct D3DRECTPATCH_INFO { UINT StartVertexOffsetWidth, StartVertexOffsetHeight, Width, Height, Stride; DWORD Basis, Order; };
struct D3DTRIPATCH_INFO { UINT StartVertexOffset, NumVertices; DWORD Basis, Order; };

struct D3DPRESENT_PARAMETERS {
  UINT BackBufferWidth, BackBufferHeight;
  D3DFORMAT BackBufferFormat;
  UINT BackBufferCount, MultiSampleType;
  D3DSWAPEFFECT SwapEffect;
  HWND hDeviceWindow;
  BOOL Windowed, EnableAutoDepthStencil;
  D3DFORMAT AutoDepthStencilFormat;
  DWORD Flags, FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
};
struct D3DSURFACE_DESC {
  D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; UINT Size;
  D3DMULTISAMPLE_TYPE MultiSampleType; UINT Width, Height;
};
struct D3DVERTEXBUFFER_DESC { D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; UINT Size; DWORD FVF; };
struct D3DINDEXBUFFER_DESC  { D3DFORMAT Format; D3DRESOURCETYPE Type; DWORD Usage; D3DPOOL Pool; UINT Size; };

struct D3DLIGHT8 {
  D3DLIGHTTYPE Type; D3DCOLORVALUE Diffuse, Specular, Ambient; D3DVECTOR Position, Direction;
  float Range, Falloff, Attenuation0, Attenuation1, Attenuation2, Theta, Phi;
};
struct D3DMATERIAL8 { D3DCOLORVALUE Diffuse, Ambient, Specular, Emissive; float Power; };

// D3DCAPS8 — the fields dx8caps probes; the rest zeroed by GetDeviceCaps.
struct D3DCAPS8 {
  D3DDEVTYPE DeviceType; UINT AdapterOrdinal;
  DWORD Caps, Caps2, Caps3, PresentationIntervals;
  DWORD CursorCaps, DevCaps;
  DWORD PrimitiveMiscCaps, RasterCaps, ZCmpCaps, SrcBlendCaps, DestBlendCaps, AlphaCmpCaps, ShadeCaps;
  DWORD TextureCaps, TextureFilterCaps, CubeTextureFilterCaps, VolumeTextureFilterCaps;
  DWORD TextureAddressCaps, VolumeTextureAddressCaps, LineCaps;
  DWORD MaxTextureWidth, MaxTextureHeight, MaxVolumeExtent, MaxTextureRepeat, MaxTextureAspectRatio;
  DWORD MaxAnisotropy; float MaxVertexW, GuardBandLeft, GuardBandTop, GuardBandRight, GuardBandBottom, ExtentsAdjust;
  DWORD StencilCaps, FVFCaps, TextureOpCaps; DWORD MaxTextureBlendStages, MaxSimultaneousTextures;
  DWORD VertexProcessingCaps, MaxActiveLights, MaxUserClipPlanes, MaxVertexBlendMatrices, MaxVertexBlendMatrixIndex;
  float MaxPointSize; DWORD MaxPrimitiveCount, MaxVertexIndex, MaxStreams, MaxStreamStride;
  DWORD VertexShaderVersion, MaxVertexShaderConst, PixelShaderVersion; float MaxPixelShaderValue;
};

// D3DCAPS8 bit constants (canonical DirectX 8 SDK values). The engine's
// W3DShaderManager::getChipset() and terrain/water path selection read these to pick a
// rendering path; leaving them zero forces a degraded terrain-blend fallback. Values
// copied to match Leondore's d3d8webgl FillCaps (the working reference).
enum {
  D3DCAPS2_FULLSCREENGAMMA = 0x00020000, D3DCAPS2_DYNAMICTEXTURES = 0x20000000,
  D3DPRESENT_INTERVAL_ONE = 0x00000001, D3DPRESENT_INTERVAL_IMMEDIATE = (int)0x80000000,
  D3DCURSORCAPS_COLOR = 0x1,
  D3DDEVCAPS_EXECUTESYSTEMMEMORY = 0x10, D3DDEVCAPS_EXECUTEVIDEOMEMORY = 0x20,
  D3DDEVCAPS_TLVERTEXSYSTEMMEMORY = 0x40, D3DDEVCAPS_TLVERTEXVIDEOMEMORY = 0x80,
  D3DDEVCAPS_TEXTURESYSTEMMEMORY = 0x100, D3DDEVCAPS_TEXTUREVIDEOMEMORY = 0x200,
  D3DDEVCAPS_DRAWPRIMTLVERTEX = 0x400, D3DDEVCAPS_CANRENDERAFTERFLIP = 0x800,
  D3DDEVCAPS_TEXTURENONLOCALVIDMEM = 0x1000, D3DDEVCAPS_DRAWPRIMITIVES2 = 0x2000,
  D3DDEVCAPS_DRAWPRIMITIVES2EX = 0x8000, D3DDEVCAPS_HWTRANSFORMANDLIGHT = 0x10000,
  D3DDEVCAPS_HWRASTERIZATION = 0x80000,
  D3DPMISCCAPS_MASKZ = 0x2, D3DPMISCCAPS_CULLNONE = 0x10, D3DPMISCCAPS_CULLCW = 0x20,
  D3DPMISCCAPS_CULLCCW = 0x40, D3DPMISCCAPS_COLORWRITEENABLE = 0x80, D3DPMISCCAPS_BLENDOP = 0x800,
  D3DPRASTERCAPS_DITHER = 0x1, D3DPRASTERCAPS_ZTEST = 0x10, D3DPRASTERCAPS_FOGVERTEX = 0x80,
  D3DPRASTERCAPS_FOGTABLE = 0x100, D3DPRASTERCAPS_MIPMAPLODBIAS = 0x400, D3DPRASTERCAPS_ZBIAS = 0x4000,
  D3DPRASTERCAPS_FOGRANGE = 0x10000, D3DPRASTERCAPS_ANISOTROPY = 0x20000,
  D3DPRASTERCAPS_WFOG = 0x100000, D3DPRASTERCAPS_ZFOG = 0x200000,
  D3DPCMPCAPS_NEVER = 0x1, D3DPCMPCAPS_LESS = 0x2, D3DPCMPCAPS_EQUAL = 0x4, D3DPCMPCAPS_LESSEQUAL = 0x8,
  D3DPCMPCAPS_GREATER = 0x10, D3DPCMPCAPS_NOTEQUAL = 0x20, D3DPCMPCAPS_GREATEREQUAL = 0x40, D3DPCMPCAPS_ALWAYS = 0x80,
  D3DPBLENDCAPS_ZERO = 0x1, D3DPBLENDCAPS_ONE = 0x2, D3DPBLENDCAPS_SRCCOLOR = 0x4, D3DPBLENDCAPS_INVSRCCOLOR = 0x8,
  D3DPBLENDCAPS_SRCALPHA = 0x10, D3DPBLENDCAPS_INVSRCALPHA = 0x20, D3DPBLENDCAPS_DESTALPHA = 0x40,
  D3DPBLENDCAPS_INVDESTALPHA = 0x80, D3DPBLENDCAPS_DESTCOLOR = 0x100, D3DPBLENDCAPS_INVDESTCOLOR = 0x200,
  D3DPBLENDCAPS_SRCALPHASAT = 0x400,
  D3DPSHADECAPS_COLORGOURAUDRGB = 0x8, D3DPSHADECAPS_SPECULARGOURAUDRGB = 0x200,
  D3DPSHADECAPS_ALPHAGOURAUDBLEND = 0x4000, D3DPSHADECAPS_FOGGOURAUD = 0x80000,
  D3DPTEXTURECAPS_PERSPECTIVE = 0x1, D3DPTEXTURECAPS_ALPHA = 0x4, D3DPTEXTURECAPS_PROJECTED = 0x400,
  D3DPTEXTURECAPS_CUBEMAP = 0x800, D3DPTEXTURECAPS_MIPMAP = 0x4000, D3DPTEXTURECAPS_MIPCUBEMAP = 0x10000,
  D3DPTFILTERCAPS_MINFPOINT = 0x100, D3DPTFILTERCAPS_MINFLINEAR = 0x200, D3DPTFILTERCAPS_MINFANISOTROPIC = 0x400,
  D3DPTFILTERCAPS_MIPFPOINT = 0x10000, D3DPTFILTERCAPS_MIPFLINEAR = 0x20000,
  D3DPTFILTERCAPS_MAGFPOINT = 0x1000000, D3DPTFILTERCAPS_MAGFLINEAR = 0x2000000,
  D3DPTADDRESSCAPS_WRAP = 0x1, D3DPTADDRESSCAPS_MIRROR = 0x2, D3DPTADDRESSCAPS_CLAMP = 0x4,
  D3DPTADDRESSCAPS_BORDER = 0x8, D3DPTADDRESSCAPS_INDEPENDENTUV = 0x10,
  D3DLINECAPS_TEXTURE = 0x1, D3DLINECAPS_ZTEST = 0x2, D3DLINECAPS_BLEND = 0x4, D3DLINECAPS_ALPHACMP = 0x8, D3DLINECAPS_FOG = 0x10,
  D3DSTENCILCAPS_KEEP = 0x1, D3DSTENCILCAPS_ZERO = 0x2, D3DSTENCILCAPS_REPLACE = 0x4, D3DSTENCILCAPS_INCRSAT = 0x8,
  D3DSTENCILCAPS_DECRSAT = 0x10, D3DSTENCILCAPS_INVERT = 0x20, D3DSTENCILCAPS_INCR = 0x40, D3DSTENCILCAPS_DECR = 0x80,
  D3DTEXOPCAPS_DISABLE = 0x1, D3DTEXOPCAPS_SELECTARG1 = 0x2, D3DTEXOPCAPS_SELECTARG2 = 0x4, D3DTEXOPCAPS_MODULATE = 0x8,
  D3DTEXOPCAPS_MODULATE2X = 0x10, D3DTEXOPCAPS_MODULATE4X = 0x20, D3DTEXOPCAPS_ADD = 0x40, D3DTEXOPCAPS_ADDSIGNED = 0x80,
  D3DTEXOPCAPS_ADDSIGNED2X = 0x100, D3DTEXOPCAPS_SUBTRACT = 0x200, D3DTEXOPCAPS_ADDSMOOTH = 0x400,
  D3DTEXOPCAPS_BLENDDIFFUSEALPHA = 0x800, D3DTEXOPCAPS_BLENDTEXTUREALPHA = 0x1000, D3DTEXOPCAPS_BLENDFACTORALPHA = 0x2000,
  D3DTEXOPCAPS_BLENDCURRENTALPHA = 0x8000, D3DTEXOPCAPS_DOTPRODUCT3 = 0x800000,
  D3DVTXPCAPS_TEXGEN = 0x1, D3DVTXPCAPS_MATERIALSOURCE7 = 0x2, D3DVTXPCAPS_DIRECTIONALLIGHTS = 0x8,
  D3DVTXPCAPS_POSITIONALLIGHTS = 0x10, D3DVTXPCAPS_LOCALVIEWER = 0x20
};

// --- COM interfaces (canonical vtable order; slot 0 = QueryInterface) --------
struct IDirect3DDevice8;
struct IDirect3D8;
struct IDirect3DSurface8;
struct IDirect3DSwapChain8;

struct IUnknownD3D {   // shared IUnknown prefix
  virtual HRESULT QueryInterface(REFIID riid, void** ppvObj) = 0;
  virtual ULONG AddRef() = 0;
  virtual ULONG Release() = 0;
};

struct IDirect3DResource8 : IUnknownD3D {
  virtual HRESULT GetDevice(IDirect3DDevice8** ppDevice) = 0;
  virtual HRESULT SetPrivateData(REFIID, const void*, DWORD, DWORD) = 0;
  virtual HRESULT GetPrivateData(REFIID, void*, DWORD*) = 0;
  virtual HRESULT FreePrivateData(REFIID) = 0;
  virtual DWORD SetPriority(DWORD) = 0;
  virtual DWORD GetPriority() = 0;
  virtual void PreLoad() = 0;
  virtual D3DRESOURCETYPE GetType() = 0;
};

struct IDirect3DBaseTexture8 : IDirect3DResource8 {
  virtual DWORD SetLOD(DWORD) = 0;
  virtual DWORD GetLOD() = 0;
  virtual DWORD GetLevelCount() = 0;
};

struct IDirect3DTexture8 : IDirect3DBaseTexture8 {
  virtual HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC* pDesc) = 0;
  virtual HRESULT GetSurfaceLevel(UINT Level, struct IDirect3DSurface8** ppSurfaceLevel) = 0;
  virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) = 0;
  virtual HRESULT UnlockRect(UINT Level) = 0;
  virtual HRESULT AddDirtyRect(const RECT* pDirtyRect) = 0;
};

struct IDirect3DVertexBuffer8 : IDirect3DResource8 {
  virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE** ppbData, DWORD Flags) = 0;
  virtual HRESULT Unlock() = 0;
  virtual HRESULT GetDesc(D3DVERTEXBUFFER_DESC* pDesc) = 0;
};
struct IDirect3DIndexBuffer8 : IDirect3DResource8 {
  virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, BYTE** ppbData, DWORD Flags) = 0;
  virtual HRESULT Unlock() = 0;
  virtual HRESULT GetDesc(D3DINDEXBUFFER_DESC* pDesc) = 0;
};
struct IDirect3DSurface8 : IUnknownD3D {
  virtual HRESULT GetDevice(IDirect3DDevice8** ppDevice) = 0;
  virtual HRESULT SetPrivateData(REFIID, const void*, DWORD, DWORD) = 0;
  virtual HRESULT GetPrivateData(REFIID, void*, DWORD*) = 0;
  virtual HRESULT FreePrivateData(REFIID) = 0;
  virtual HRESULT GetContainer(REFIID, void**) = 0;
  virtual HRESULT GetDesc(D3DSURFACE_DESC* pDesc) = 0;
  virtual HRESULT LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* pRect, DWORD Flags) = 0;
  virtual HRESULT UnlockRect() = 0;
};

struct IDirect3DDevice8 : IUnknownD3D {
  virtual HRESULT TestCooperativeLevel() = 0;
  virtual UINT GetAvailableTextureMem() = 0;
  virtual HRESULT ResourceManagerDiscardBytes(DWORD Bytes) = 0;
  virtual HRESULT GetDirect3D(struct IDirect3D8** ppD3D8) = 0;
  virtual HRESULT GetDeviceCaps(D3DCAPS8* pCaps) = 0;
  virtual HRESULT GetDisplayMode(D3DDISPLAYMODE* pMode) = 0;
  virtual HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* pParameters) = 0;
  virtual HRESULT SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface8* pCursorBitmap) = 0;
  virtual void SetCursorPosition(UINT XScreenSpace, UINT YScreenSpace, DWORD Flags) = 0;
  virtual BOOL ShowCursor(BOOL bShow) = 0;
  virtual HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, struct IDirect3DSwapChain8**) = 0;
  virtual HRESULT Reset(D3DPRESENT_PARAMETERS* pPresentationParameters) = 0;
  virtual HRESULT Present(const RECT* pSourceRect, const RECT* pDestRect, HWND hDestWindowOverride, const RGNDATA* pDirtyRegion) = 0;
  virtual HRESULT GetBackBuffer(UINT BackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface8** ppBackBuffer) = 0;
  virtual HRESULT GetRasterStatus(D3DRASTER_STATUS* pRasterStatus) = 0;
  virtual void SetGammaRamp(DWORD Flags, const D3DGAMMARAMP* pRamp) = 0;
  virtual void GetGammaRamp(D3DGAMMARAMP* pRamp) = 0;
  virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DTexture8** ppTexture) = 0;
  virtual HRESULT CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, void**) = 0;
  virtual HRESULT CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, void**) = 0;
  virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer8** ppVertexBuffer) = 0;
  virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer8** ppIndexBuffer) = 0;
  virtual HRESULT CreateRenderTarget(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, BOOL, IDirect3DSurface8**) = 0;
  virtual HRESULT CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, IDirect3DSurface8**) = 0;
  virtual HRESULT CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8** ppSurface) = 0;
  virtual HRESULT CopyRects(IDirect3DSurface8*, const RECT*, UINT, IDirect3DSurface8*, const POINT*) = 0;
  virtual HRESULT UpdateTexture(IDirect3DBaseTexture8*, IDirect3DBaseTexture8*) = 0;
  virtual HRESULT GetFrontBuffer(IDirect3DSurface8* pDestSurface) = 0;
  virtual HRESULT SetRenderTarget(IDirect3DSurface8*, IDirect3DSurface8*) = 0;
  virtual HRESULT GetRenderTarget(IDirect3DSurface8**) = 0;
  virtual HRESULT GetDepthStencilSurface(IDirect3DSurface8**) = 0;
  virtual HRESULT BeginScene() = 0;
  virtual HRESULT EndScene() = 0;
  virtual HRESULT Clear(DWORD Count, const D3DRECT* pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) = 0;
  virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) = 0;
  virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix) = 0;
  virtual HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) = 0;
  virtual HRESULT SetViewport(const D3DVIEWPORT8* pViewport) = 0;
  virtual HRESULT GetViewport(D3DVIEWPORT8* pViewport) = 0;
  virtual HRESULT SetMaterial(const D3DMATERIAL8* pMaterial) = 0;
  virtual HRESULT GetMaterial(D3DMATERIAL8* pMaterial) = 0;
  virtual HRESULT SetLight(DWORD Index, const D3DLIGHT8* pLight) = 0;
  virtual HRESULT GetLight(DWORD Index, D3DLIGHT8* pLight) = 0;
  virtual HRESULT LightEnable(DWORD Index, BOOL Enable) = 0;
  virtual HRESULT GetLightEnable(DWORD Index, BOOL* pEnable) = 0;
  virtual HRESULT SetClipPlane(DWORD Index, const float* pPlane) = 0;
  virtual HRESULT GetClipPlane(DWORD Index, float* pPlane) = 0;
  virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) = 0;
  virtual HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue) = 0;
  virtual HRESULT BeginStateBlock() = 0;
  virtual HRESULT EndStateBlock(DWORD* pToken) = 0;
  virtual HRESULT ApplyStateBlock(DWORD Token) = 0;
  virtual HRESULT CaptureStateBlock(DWORD Token) = 0;
  virtual HRESULT DeleteStateBlock(DWORD Token) = 0;
  virtual HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE Type, DWORD* pToken) = 0;
  virtual HRESULT SetClipStatus(const D3DCLIPSTATUS8* pClipStatus) = 0;
  virtual HRESULT GetClipStatus(D3DCLIPSTATUS8* pClipStatus) = 0;
  virtual HRESULT GetTexture(DWORD Stage, IDirect3DBaseTexture8** ppTexture) = 0;
  virtual HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture8* pTexture) = 0;
  virtual HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue) = 0;
  virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) = 0;
  virtual HRESULT ValidateDevice(DWORD* pNumPasses) = 0;
  virtual HRESULT GetInfo(DWORD DevInfoID, void* pDevInfoStruct, DWORD DevInfoStructSize) = 0;
  virtual HRESULT SetPaletteEntries(UINT PaletteNumber, const PALETTEENTRY* pEntries) = 0;
  virtual HRESULT GetPaletteEntries(UINT PaletteNumber, PALETTEENTRY* pEntries) = 0;
  virtual HRESULT SetCurrentTexturePalette(UINT PaletteNumber) = 0;
  virtual HRESULT GetCurrentTexturePalette(UINT* PaletteNumber) = 0;
  virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) = 0;
  virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount) = 0;
  virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
  virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
  virtual HRESULT ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer8*, DWORD) = 0;
  virtual HRESULT CreateVertexShader(const DWORD* pDeclaration, const DWORD* pFunction, DWORD* pHandle, DWORD Usage) = 0;
  virtual HRESULT SetVertexShader(DWORD Handle) = 0;
  virtual HRESULT GetVertexShader(DWORD* pHandle) = 0;
  virtual HRESULT DeleteVertexShader(DWORD Handle) = 0;
  virtual HRESULT SetVertexShaderConstant(DWORD, const void*, DWORD) = 0;
  virtual HRESULT GetVertexShaderConstant(DWORD, void*, DWORD) = 0;
  virtual HRESULT GetVertexShaderDeclaration(DWORD, void*, DWORD*) = 0;
  virtual HRESULT GetVertexShaderFunction(DWORD, void*, DWORD*) = 0;
  virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT Stride) = 0;
  virtual HRESULT GetStreamSource(UINT, IDirect3DVertexBuffer8**, UINT*) = 0;
  virtual HRESULT SetIndices(IDirect3DIndexBuffer8* pIndexData, UINT BaseVertexIndex) = 0;
  virtual HRESULT GetIndices(IDirect3DIndexBuffer8**, UINT*) = 0;
  virtual HRESULT SetPixelShader(DWORD Handle) = 0;
  virtual HRESULT GetPixelShader(DWORD* pHandle) = 0;
  virtual HRESULT CreatePixelShader(const DWORD* pFunction, DWORD* pHandle) = 0;
  virtual HRESULT DeletePixelShader(DWORD Handle) = 0;
  virtual HRESULT SetPixelShaderConstant(DWORD, const void*, DWORD) = 0;
  virtual HRESULT GetPixelShaderConstant(DWORD, void*, DWORD) = 0;
  virtual HRESULT GetPixelShaderFunction(DWORD, void*, DWORD*) = 0;
  virtual HRESULT DrawRectPatch(UINT, const float*, const D3DRECTPATCH_INFO*) = 0;
  virtual HRESULT DrawTriPatch(UINT, const float*, const D3DTRIPATCH_INFO*) = 0;
  virtual HRESULT DeletePatch(UINT Handle) = 0;
  virtual ~IDirect3DDevice8() = default;
};

struct IDirect3D8 : IUnknownD3D {
  virtual HRESULT RegisterSoftwareDevice(void*) = 0;
  virtual UINT GetAdapterCount() = 0;
  virtual HRESULT GetAdapterIdentifier(UINT, DWORD, void*) = 0;
  virtual UINT GetAdapterModeCount(UINT Adapter) = 0;
  virtual HRESULT EnumAdapterModes(UINT Adapter, UINT Mode, D3DDISPLAYMODE* pMode) = 0;
  virtual HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) = 0;
  virtual HRESULT CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, BOOL) = 0;
  virtual HRESULT CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT) = 0;
  virtual HRESULT CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE) = 0;
  virtual HRESULT CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) = 0;
  virtual HRESULT GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS8* pCaps) = 0;
  virtual HMONITOR GetAdapterMonitor(UINT Adapter) = 0;
  virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
                               D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice8** ppReturnedDeviceInterface) = 0;
  virtual ~IDirect3D8() = default;
};

extern "C" IDirect3D8* Direct3DCreate8(unsigned int SDKVersion);
#endif  // DX8WASM_D3D8_H
