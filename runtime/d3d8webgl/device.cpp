// SPDX-License-Identifier: GPL-3.0-only
#include "d3d8/d3d8.h"
#include "platform/platform.h"
#include "graphics-ff/ff_shader.h"
#include "coverage/coverage.h"
#include <GLES3/gl3.h>
#include <cstring>
#include <vector>

namespace {
// A GPU buffer backed by a CPU staging vector. Lock hands out a pointer into
// the staging bytes; Unlock uploads them to the GL buffer object.
struct GLBuffer {
  GLenum target;
  std::vector<BYTE> cpu;
  GLuint glbuf = 0;
  explicit GLBuffer(GLenum t, UINT length) : target(t), cpu(length) {}
  HRESULT Lock(UINT off, UINT, BYTE** pp) { *pp = cpu.data() + off; return D3D_OK; }
  HRESULT Unlock() {
    if (!glbuf) glGenBuffers(1, &glbuf);
    glBindBuffer(target, glbuf);
    glBufferData(target, (GLsizeiptr)cpu.size(), cpu.data(), GL_STATIC_DRAW);
    return D3D_OK;
  }
  ~GLBuffer() { if (glbuf) glDeleteBuffers(1, &glbuf); }
};

struct VertexBuffer8 : IDirect3DVertexBuffer8 {
  GLBuffer b; uint32_t refs = 1;
  explicit VertexBuffer8(UINT len) : b(GL_ARRAY_BUFFER, len) {}
  uint32_t AddRef() override { return ++refs; }
  uint32_t Release() override { uint32_t r = --refs; if (!r) delete this; return r; }
  HRESULT Lock(UINT o, UINT s, BYTE** pp, DWORD) override { return b.Lock(o, s, pp); }
  HRESULT Unlock() override { return b.Unlock(); }
};

struct IndexBuffer8 : IDirect3DIndexBuffer8 {
  GLBuffer b; uint32_t refs = 1;
  explicit IndexBuffer8(UINT len) : b(GL_ELEMENT_ARRAY_BUFFER, len) {}
  uint32_t AddRef() override { return ++refs; }
  uint32_t Release() override { uint32_t r = --refs; if (!r) delete this; return r; }
  HRESULT Lock(UINT o, UINT s, BYTE** pp, DWORD) override { return b.Lock(o, s, pp); }
  HRESULT Unlock() override { return b.Unlock(); }
};

// Level-0-only A8R8G8B8 texture. CPU staging holds the D3D [B,G,R,A] bytes;
// UnlockRect uploads them verbatim as GL_RGBA (the .bgra shader swizzle fixes
// channel order). Nearest + clamp — no mips/filtering until a target needs them.
struct Texture8 : IDirect3DTexture8 {
  uint32_t refs = 1;
  UINT w, h;
  std::vector<BYTE> cpu;
  GLuint tex = 0;
  Texture8(UINT width, UINT height) : w(width), h(height), cpu((size_t)width * height * 4) {}
  uint32_t AddRef() override { return ++refs; }
  uint32_t Release() override { uint32_t r = --refs; if (!r) delete this; return r; }
  HRESULT LockRect(UINT, D3DLOCKED_RECT* lr, const D3DRECT*, DWORD) override {
    if (!lr) return D3DERR_INVALIDCALL;
    lr->Pitch = (int32_t)(w * 4); lr->pBits = cpu.data(); return D3D_OK;
  }
  HRESULT UnlockRect(UINT) override {
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, cpu.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return D3D_OK;
  }
  ~Texture8() { if (tex) glDeleteTextures(1, &tex); }
};

void set_identity(float* m) {
  std::memset(m, 0, 16 * sizeof(float));
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}

GLenum gl_blend(DWORD b) {
  switch (b) {
    case D3DBLEND_ZERO:        return GL_ZERO;
    case D3DBLEND_ONE:         return GL_ONE;
    case D3DBLEND_SRCALPHA:    return GL_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    default:                   return GL_ONE;
  }
}

struct Device8 : IDirect3DDevice8 {
  uint32_t refs = 1;
  VertexBuffer8* stream = nullptr;  // bound stream 0 (not owned; game holds the ref)
  IndexBuffer8* indices = nullptr;
  UINT stride = 0;
  uint32_t fvf = 0;
  Texture8* texture = nullptr;              // bound stage-0 texture (not owned)
  uint32_t colorOp = D3DTOP_MODULATE;       // D3D stage-0 COLOROP default
  GLenum srcBlend = GL_ONE, dstBlend = GL_ZERO;   // D3D blend defaults
  bool alphaTestEnable = false;
  uint32_t alphaFunc = D3DCMP_ALWAYS;
  DWORD alphaRef = 0;
  float world[16], view[16], proj[16];

  Device8() {
    set_identity(world); set_identity(view); set_identity(proj);
    glDepthFunc(GL_LEQUAL);   // D3D default ZFUNC is LESSEQUAL (GL default is LESS)
  }

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

  HRESULT CreateVertexBuffer(UINT Length, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    *out = new VertexBuffer8(Length);
    return D3D_OK;
  }
  HRESULT CreateIndexBuffer(UINT Length, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    *out = new IndexBuffer8(Length);
    return D3D_OK;
  }
  HRESULT SetStreamSource(UINT, IDirect3DVertexBuffer8* vb, UINT Stride) override {
    stream = static_cast<VertexBuffer8*>(vb); stride = Stride; return D3D_OK;
  }
  HRESULT SetIndices(IDirect3DIndexBuffer8* ib, UINT) override {
    indices = static_cast<IndexBuffer8*>(ib); return D3D_OK;
  }
  HRESULT SetVertexShader(DWORD Handle) override { fvf = Handle; return D3D_OK; }
  HRESULT CreateTexture(UINT Width, UINT Height, UINT, DWORD, D3DFORMAT Format, D3DPOOL,
                        IDirect3DTexture8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    // Only A8R8G8B8/X8R8G8B8 upload as-is; anything else falls back to RGBA bytes.
    if (Format != D3DFMT_A8R8G8B8 && Format != D3DFMT_X8R8G8B8)
      coverage::unhandled_format(Format);
    *out = new Texture8(Width, Height);
    return D3D_OK;
  }
  HRESULT SetTexture(DWORD, IDirect3DTexture8* t) override {
    texture = static_cast<Texture8*>(t); return D3D_OK;
  }
  HRESULT SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override {
    if (Type == D3DTSS_COLOROP) {
      if (Value == D3DTOP_MODULATE || Value == D3DTOP_SELECTARG1 || Value == D3DTOP_DISABLE) {
        colorOp = Value;                 // args assumed canonical until a game sets them
      } else {
        coverage::unhandled_texture_op(Value);
        colorOp = D3DTOP_MODULATE;       // fall back so the draw still produces pixels
      }
    }
    return D3D_OK;
  }
  HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override {
    switch (State) {
      case D3DRS_ZENABLE:         Value ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST); break;
      case D3DRS_ZWRITEENABLE:    glDepthMask(Value ? GL_TRUE : GL_FALSE); break;
      case D3DRS_ALPHABLENDENABLE: Value ? glEnable(GL_BLEND) : glDisable(GL_BLEND); break;
      case D3DRS_SRCBLEND:        srcBlend = gl_blend(Value); glBlendFunc(srcBlend, dstBlend); break;
      case D3DRS_DESTBLEND:       dstBlend = gl_blend(Value); glBlendFunc(srcBlend, dstBlend); break;
      case D3DRS_CULLMODE:        apply_cull(Value); break;
      case D3DRS_ALPHATESTENABLE: alphaTestEnable = Value != 0; break;
      case D3DRS_ALPHAREF:        alphaRef = Value; break;
      case D3DRS_ALPHAFUNC:       alphaFunc = Value; break;
      default:                    coverage::unhandled_render_state(State); break;
    }
    return D3D_OK;
  }
  // ponytail: NDC winding == GL winding here (no D3D Y-flip projection yet), so
  // D3DCULL_CCW maps straight to culling GL front faces. Revisit when a real
  // projection matrix introduces the D3D screen-space flip.
  void apply_cull(DWORD mode) {
    if (mode == D3DCULL_NONE) { glDisable(GL_CULL_FACE); return; }
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glCullFace(mode == D3DCULL_CCW ? GL_FRONT : GL_BACK);
  }
  HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override {
    if (!pMatrix) return D3DERR_INVALIDCALL;
    float* dst = State == D3DTS_WORLD ? world : State == D3DTS_VIEW ? view
               : State == D3DTS_PROJECTION ? proj : nullptr;
    if (!dst) return D3DERR_INVALIDCALL;
    std::memcpy(dst, pMatrix->m, 16 * sizeof(float));
    return D3D_OK;
  }
  HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, UINT, UINT, UINT StartIndex,
                               UINT PrimitiveCount) override {
    if (Type != D3DPT_TRIANGLELIST || !stream || !indices) return D3DERR_INVALIDCALL;
    const bool textured = (fvf & D3DFVF_TEX1) && texture;
    const uint32_t af = alphaTestEnable ? alphaFunc : 0;
    const ff::Program* p = ff::program_for(fvf, textured ? colorOp : D3DTOP_DISABLE, af);
    if (!p) return D3DERR_INVALIDCALL;

    glUseProgram(p->prog);
    // D3D row-major uploaded as GL column-major (i.e. transposed); combined with
    // proj*view*world in the shader this reproduces D3D's v*M semantics.
    glUniformMatrix4fv(p->uWorld, 1, GL_FALSE, world);
    glUniformMatrix4fv(p->uView,  1, GL_FALSE, view);
    glUniformMatrix4fv(p->uProj,  1, GL_FALSE, proj);

    glBindBuffer(GL_ARRAY_BUFFER, stream->b.glbuf);
    // ponytail: fixed XYZ@0, DIFFUSE@12, TEX1@16 layout; derive offsets from FVF
    // when non-{XYZ,DIFFUSE,TEX1} formats actually appear.
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)0);
    GLuint off = 12;
    if (fvf & D3DFVF_DIFFUSE) {
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, (GLsizei)stride, (void*)(uintptr_t)off);
      off += 4;
    } else glDisableVertexAttribArray(1);
    if (fvf & D3DFVF_TEX1) {
      glEnableVertexAttribArray(2);
      glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(uintptr_t)off);
    } else glDisableVertexAttribArray(2);
    if (textured) {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture->tex);
      glUniform1i(p->uTex, 0);
    }
    if (p->uAlphaRef >= 0) glUniform1f(p->uAlphaRef, alphaRef / 255.0f);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->b.glbuf);
    glDrawElements(GL_TRIANGLES, (GLsizei)(PrimitiveCount * 3), GL_UNSIGNED_SHORT,
                   (void*)(uintptr_t)(StartIndex * sizeof(uint16_t)));
    return D3D_OK;
  }
};
} // namespace

IDirect3DDevice8* dx8_create_device(int w, int h) {
  if (!platform::create_gl_context(w, h)) return nullptr;
  return new Device8();
}
