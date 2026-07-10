// SPDX-License-Identifier: GPL-3.0-only
#include "d3d8/d3d8.h"
#include "platform/platform.h"
#include <GLES3/gl3.h>

struct Device8 : IDirect3DDevice8 {
  uint32_t refs = 1;
  uint32_t AddRef() override { return ++refs; }
  uint32_t Release() override { uint32_t r = --refs; if (!r) { platform::destroy_gl_context(); delete this; } return r; }

  HRESULT Clear(uint32_t, const D3DRECT*, uint32_t Flags, D3DCOLOR c, float, uint32_t) override {
    GLbitfield mask = 0;
    if (Flags & D3DCLEAR_TARGET) {
      glClearColor(((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f,
                   (c & 0xff) / 255.0f, ((c >> 24) & 0xff) / 255.0f);
      mask |= GL_COLOR_BUFFER_BIT;
    }
    if (Flags & D3DCLEAR_ZBUFFER) mask |= GL_DEPTH_BUFFER_BIT;
    glClear(mask);
    return D3D_OK;
  }
  HRESULT Present(const D3DRECT*, const D3DRECT*, HWND, const void*) override {
    platform::present();
    return D3D_OK;
  }
};

IDirect3DDevice8* dx8_create_device(int w, int h) {
  if (!platform::create_gl_context(w, h)) return nullptr;
  return new Device8();
}
