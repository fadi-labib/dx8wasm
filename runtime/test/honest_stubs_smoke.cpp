// SPDX-License-Identifier: GPL-3.0-only
// Entry points this backend does not implement must FAIL, not return D3D_OK. A stub that
// reports success with a plausible value gets its value consumed — the save/restore idiom
// turns it into corrupted state, which is how a GetRenderState stub once blanked a whole UI
// while the 3D scene behind it kept rendering.
// Reports the sentinel [1,0,0,255] when every unimplemented call refuses.
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
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

  // Reads with nothing behind them must refuse rather than leave the caller's buffer untouched
  // — an unwritten buffer is worse than a zeroed one, because the garbage is nondeterministic.
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
  // "applying" it restores nothing while the caller believes its state came back.
  DWORD token = 0xFFFFFFFFu;
  if (dev->CreateStateBlock(D3DSBT_ALL, &token) == D3D_OK) { report_error("CreateStateBlock claimed success"); return 1; }
  if (dev->BeginStateBlock() == D3D_OK) { report_error("BeginStateBlock claimed success"); return 1; }
  if (dev->ApplyStateBlock(0) == D3D_OK) { report_error("ApplyStateBlock claimed success"); return 1; }

  // Only the backbuffer exists; switching to any other target must be refused, not ignored,
  // or the caller draws off-screen content straight onto the visible frame.
  if (dev->SetRenderTarget((IDirect3DSurface8*)0x1, nullptr) == D3D_OK) { report_error("SetRenderTarget claimed success"); return 1; }
  if (dev->SetRenderTarget(nullptr, nullptr) != D3D_OK) { report_error("restoring the default target failed"); return 1; }

  // Caps must not advertise what the device refuses to do.
  D3DCAPS8 caps{};
  dev->GetDeviceCaps(&caps);
  if (caps.MaxUserClipPlanes != 0) { report_error("caps advertise clip planes that do nothing"); return 1; }
  if (caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) { report_error("caps advertise cube maps but CreateCubeTexture fails"); return 1; }

  // ...and the introspection contract must not deny what the device DOES do.
  if (dx8wasm_has_cap(DX8WASM_CAP_STENCIL) != 1) { report_error("stencil cap denied but implemented"); return 1; }
  if (dx8wasm_has_cap(DX8WASM_CAP_CUBE_TEXTURE) != 0) { report_error("cube texture cap claimed but absent"); return 1; }

  dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
