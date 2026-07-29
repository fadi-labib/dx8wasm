// SPDX-License-Identifier: GPL-3.0-only
// The factory's capability queries must agree with what the texture path can actually do.
// A blanket D3D_OK here is worse than a refusal: WW3D builds its whole supported-format table
// from CheckDeviceFormat and then picks a format that only fails much later, at upload.
// Reports the sentinel [1,0,0,255] when every check agrees.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <initializer_list>

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
  if (d3d->CheckDeviceType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_X8R8G8B8, 1) != D3D_OK) {
    report_error("X8R8G8B8 back buffer was refused"); return 1;
  }
  if (d3d->CheckDeviceType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_DXT1, 1) == D3D_OK) {
    report_error("a compressed format was accepted as a back buffer"); return 1;
  }

  // No multisampling is implemented, so only NONE may be claimed.
  if (d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 1,
                                      D3DMULTISAMPLE_NONE) != D3D_OK) {
    report_error("MULTISAMPLE_NONE was refused"); return 1;
  }
  if (d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 1,
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
