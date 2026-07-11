// SPDX-License-Identifier: GPL-3.0-only
// Verifies full-ABI vtable dispatch. Calls a spread of methods across low, mid,
// and high vtable slots (incl. ones only present in the full interface) and
// checks their return values — a wrong vtable layout would dispatch to the wrong
// slot and return garbage. Pass = [1,0,0,255].
#include "d3d8/d3d8.h"
#include <emscripten.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

#define CHECK(c, msg) do { if (!(c)) { report_error(msg); return 1; } } while (0)

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  CHECK(d3d, "Direct3DCreate8 null");
  CHECK(d3d->GetAdapterCount() == 1, "GetAdapterCount");
  D3DCAPS8 acaps; CHECK(d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &acaps) == D3D_OK, "factory GetDeviceCaps");

  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  CHECK(d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) == D3D_OK && dev, "CreateDevice");

  // Low slot (IUnknown): QueryInterface returns self.
  void* qi = nullptr; CHECK(dev->QueryInterface(IID{}, &qi) == D3D_OK && qi, "QueryInterface");

  // Mid/high slots that only exist in the full interface — a truncated vtable
  // would land these calls on the wrong method.
  CHECK(dev->TestCooperativeLevel() == D3D_OK, "TestCooperativeLevel");
  CHECK(dev->GetAvailableTextureMem() > 0, "GetAvailableTextureMem");
  CHECK(dev->BeginScene() == D3D_OK && dev->EndScene() == D3D_OK, "Begin/EndScene");

  D3DCAPS8 caps;
  CHECK(dev->GetDeviceCaps(&caps) == D3D_OK, "GetDeviceCaps");
  CHECK(caps.MaxTextureBlendStages == 2 && caps.MaxActiveLights == 8 && caps.PixelShaderVersion == 0, "caps values wrong (slot mismatch?)");

  D3DDISPLAYMODE mode; CHECK(dev->GetDisplayMode(&mode) == D3D_OK && mode.Width == 4, "GetDisplayMode");

  D3DVIEWPORT8 vp{1, 2, 3, 3, 0.0f, 1.0f};
  CHECK(dev->SetViewport(&vp) == D3D_OK, "SetViewport");
  D3DVIEWPORT8 got; CHECK(dev->GetViewport(&got) == D3D_OK && got.X == 1 && got.Width == 3, "GetViewport roundtrip");

  DWORD passes = 0; CHECK(dev->ValidateDevice(&passes) == D3D_OK && passes == 1, "ValidateDevice");

  IDirect3DBaseTexture8* bound = (IDirect3DBaseTexture8*)1;
  CHECK(dev->GetTexture(0, &bound) == D3D_OK && bound == nullptr, "GetTexture (none bound)");

  // A real draw still works through the expanded vtable (high slots).
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF204060u, 1.0f, 0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  report_pixel(1, 0, 0, 255);
  dev->Release(); d3d->Release();
  return 0;
}
