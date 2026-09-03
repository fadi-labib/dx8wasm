// SPDX-License-Identifier: GPL-3.0-only
// Shared fixed-function DX8 capability set. BOTH IDirect3D8::GetDeviceCaps (d3d8.cpp)
// and IDirect3DDevice8::GetDeviceCaps (device.cpp) must report the SAME caps: the engine
// queries the *device* object for its runtime feature selection (DX8Caps::Init_Caps ->
// DX8CALL(GetDeviceCaps) -> the device vtable), while other paths query the *factory*.
// When these two disagreed, the device path reported an empty TextureFilterCaps, so the
// engine concluded the GPU had no bilinear filtering and downgraded every default-filtered
// texture to D3DTEXF_POINT (nearest) -> blocky/"not smooth" textures. Keeping one source
// of truth guarantees they can never drift again.
#pragma once
#include "d3d8/d3d8.h"
#include <cstring>

// Ported from Leondore's d3d8webgl FillCaps. A near-empty cap set steers terrain to a
// degraded blend fallback (opaque overlay squares) and filtering to nearest.
inline void fill_caps(D3DCAPS8* c) {
  std::memset(c, 0, sizeof *c);
  c->DeviceType = D3DDEVTYPE_HAL;
  c->Caps2 = D3DCAPS2_DYNAMICTEXTURES | D3DCAPS2_FULLSCREENGAMMA;
  c->PresentationIntervals = D3DPRESENT_INTERVAL_ONE | D3DPRESENT_INTERVAL_IMMEDIATE;
  c->CursorCaps = D3DCURSORCAPS_COLOR;
  c->DevCaps = D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_EXECUTEVIDEOMEMORY |
               D3DDEVCAPS_TLVERTEXSYSTEMMEMORY | D3DDEVCAPS_TLVERTEXVIDEOMEMORY |
               D3DDEVCAPS_TEXTURESYSTEMMEMORY | D3DDEVCAPS_TEXTUREVIDEOMEMORY |
               D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP |
               D3DDEVCAPS_TEXTURENONLOCALVIDMEM | D3DDEVCAPS_DRAWPRIMITIVES2 |
               D3DDEVCAPS_DRAWPRIMITIVES2EX | D3DDEVCAPS_HWTRANSFORMANDLIGHT |
               D3DDEVCAPS_HWRASTERIZATION;
  c->PrimitiveMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW |
                         D3DPMISCCAPS_CULLCCW | D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_BLENDOP;
  c->RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX |
                  D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_ZBIAS |
                  D3DPRASTERCAPS_FOGRANGE | D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_WFOG | D3DPRASTERCAPS_ZFOG;
  c->ZCmpCaps = D3DPCMPCAPS_NEVER | D3DPCMPCAPS_LESS | D3DPCMPCAPS_EQUAL | D3DPCMPCAPS_LESSEQUAL |
                D3DPCMPCAPS_GREATER | D3DPCMPCAPS_NOTEQUAL | D3DPCMPCAPS_GREATEREQUAL | D3DPCMPCAPS_ALWAYS;
  c->AlphaCmpCaps = c->ZCmpCaps;
  c->SrcBlendCaps = D3DPBLENDCAPS_ZERO | D3DPBLENDCAPS_ONE | D3DPBLENDCAPS_SRCCOLOR | D3DPBLENDCAPS_INVSRCCOLOR |
                    D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_INVSRCALPHA | D3DPBLENDCAPS_DESTALPHA |
                    D3DPBLENDCAPS_INVDESTALPHA | D3DPBLENDCAPS_DESTCOLOR | D3DPBLENDCAPS_INVDESTCOLOR |
                    D3DPBLENDCAPS_SRCALPHASAT;
  c->DestBlendCaps = c->SrcBlendCaps;
  c->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB |
                 D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGGOURAUD;
  // No cube maps: CreateCubeTexture refuses and CheckDeviceFormat rejects D3DRTYPE_CUBETEXTURE.
  // Advertising them would send an engine down a path that can only fail.
  c->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP |
                   D3DPTEXTURECAPS_PROJECTED;
  c->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC |
                         D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR |
                         D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR;
  c->CubeTextureFilterCaps = c->TextureFilterCaps;
  c->VolumeTextureFilterCaps = c->TextureFilterCaps;
  c->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP |
                          D3DPTADDRESSCAPS_BORDER | D3DPTADDRESSCAPS_INDEPENDENTUV;
  c->VolumeTextureAddressCaps = c->TextureAddressCaps;
  c->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG;
  c->MaxTextureWidth = c->MaxTextureHeight = 4096; c->MaxVolumeExtent = 256;
  c->MaxTextureRepeat = 8192; c->MaxTextureAspectRatio = 4096; c->MaxAnisotropy = 16; c->MaxVertexW = 1e10f;
  c->GuardBandLeft = -32768.0f; c->GuardBandTop = -32768.0f; c->GuardBandRight = 32768.0f; c->GuardBandBottom = 32768.0f;
  c->StencilCaps = D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_ZERO | D3DSTENCILCAPS_REPLACE | D3DSTENCILCAPS_INCRSAT |
                   D3DSTENCILCAPS_DECRSAT | D3DSTENCILCAPS_INVERT | D3DSTENCILCAPS_INCR | D3DSTENCILCAPS_DECR;
  c->FVFCaps = 8;   // 8 texture coordinate sets
  c->TextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2 |
                     D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_MODULATE4X |
                     D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_ADDSIGNED | D3DTEXOPCAPS_ADDSIGNED2X | D3DTEXOPCAPS_SUBTRACT |
                     D3DTEXOPCAPS_ADDSMOOTH | D3DTEXOPCAPS_BLENDDIFFUSEALPHA | D3DTEXOPCAPS_BLENDTEXTUREALPHA |
                     D3DTEXOPCAPS_BLENDFACTORALPHA | D3DTEXOPCAPS_BLENDCURRENTALPHA | D3DTEXOPCAPS_DOTPRODUCT3;
  c->MaxTextureBlendStages = 2; c->MaxSimultaneousTextures = 2;
  c->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS |
                            D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER;
  c->MaxActiveLights = 8; c->MaxUserClipPlanes = 0;   // SetClipPlane is not implemented
  c->MaxPointSize = 64.0f;
  c->MaxPrimitiveCount = 0x000FFFFF; c->MaxVertexIndex = 0x000FFFFF; c->MaxStreams = 1 /* one stream is bound; more are counted, not drawn */; c->MaxStreamStride = 256;
  c->VertexShaderVersion = 0; c->PixelShaderVersion = 0;
}
