// SPDX-License-Identifier: GPL-3.0-only
#include "d3d8/d3d8.h"
#include "platform/platform.h"
#include "graphics-ff/ff_shader.h"
#include "coverage/coverage.h"
#include <GLES3/gl3.h>
#include <cmath>
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
  HRESULT Lock(UINT off, UINT, BYTE** pp) {
    if (!pp) return D3DERR_INVALIDCALL;
    *pp = cpu.data() + off; return D3D_OK;
  }
  HRESULT Unlock() {
    if (!glbuf) glGenBuffers(1, &glbuf);
    glBindBuffer(target, glbuf);
    glBufferData(target, (GLsizeiptr)cpu.size(), cpu.data(), GL_STATIC_DRAW);
    return D3D_OK;
  }
  ~GLBuffer() { if (glbuf && platform::gl_context_alive()) glDeleteBuffers(1, &glbuf); }
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
  ~Texture8() { if (tex && platform::gl_context_alive()) glDeleteTextures(1, &tex); }
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
  bool zWrite = true;                             // tracks D3DRS_ZWRITEENABLE
  uint32_t alphaFunc = D3DCMP_ALWAYS;
  DWORD alphaRef = 0;
  float world[16], view[16], proj[16];

  // Fixed-function lighting state (single directional light 0 for slice 3.1).
  bool lighting = false;                          // D3DRS_LIGHTING (see roadmap: default diverges from D3D's TRUE)
  float globalAmbient[4] = {0, 0, 0, 0};          // D3DRS_AMBIENT
  D3DLIGHT8 light0{};
  bool light0On = false;
  D3DMATERIAL8 material{ {1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0 };

  Device8() {
    set_identity(world); set_identity(view); set_identity(proj);
    glDepthFunc(GL_LEQUAL);   // D3D default ZFUNC is LESSEQUAL (GL default is LESS)
  }

  uint32_t AddRef() override { return ++refs; }
  uint32_t Release() override {
    uint32_t r = --refs;
    if (!r) {
      // Drop the refs taken by Set{StreamSource,Indices,Texture} while the GL
      // context is still alive, so any GL objects they own are deleted cleanly.
      if (stream) stream->Release();
      if (indices) indices->Release();
      if (texture) texture->Release();
      platform::destroy_gl_context();
      delete this;
    }
    return r;
  }

  HRESULT Clear(uint32_t, const D3DRECT*, uint32_t Flags, D3DCOLOR c, float, uint32_t) override {
    GLbitfield mask = 0;
    if (Flags & D3DCLEAR_TARGET) {
      glClearColor(((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f,
                   (c & 0xff) / 255.0f, ((c >> 24) & 0xff) / 255.0f);
      mask |= GL_COLOR_BUFFER_BIT;
    }
    if (Flags & D3DCLEAR_ZBUFFER) {
      glDepthMask(GL_TRUE);   // D3D Clear ignores ZWRITEENABLE; glClear obeys the mask
      mask |= GL_DEPTH_BUFFER_BIT;
    }
    glClear(mask);
    if (Flags & D3DCLEAR_ZBUFFER) glDepthMask(zWrite ? GL_TRUE : GL_FALSE);   // restore app state
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
  // Set* takes a reference on the bound resource (D3D8 contract), so a game may
  // Release its own handle right after binding. AddRef the new before releasing
  // the old to stay correct when they are the same object.
  HRESULT SetStreamSource(UINT, IDirect3DVertexBuffer8* vb, UINT Stride) override {
    auto* n = static_cast<VertexBuffer8*>(vb);
    if (n) n->AddRef();
    if (stream) stream->Release();
    stream = n; stride = Stride; return D3D_OK;
  }
  // ponytail: BaseVertexIndex ignored (assumed 0). GLES3 has no base-vertex draw;
  // batched meshes that rely on it need base*stride added to the attrib offsets —
  // implement + test when a target game actually uses nonzero base vertices.
  HRESULT SetIndices(IDirect3DIndexBuffer8* ib, UINT) override {
    auto* n = static_cast<IndexBuffer8*>(ib);
    if (n) n->AddRef();
    if (indices) indices->Release();
    indices = n; return D3D_OK;
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
    auto* n = static_cast<Texture8*>(t);
    if (n) n->AddRef();
    if (texture) texture->Release();
    texture = n; return D3D_OK;
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
      case D3DRS_ZWRITEENABLE:    zWrite = Value != 0; glDepthMask(zWrite ? GL_TRUE : GL_FALSE); break;
      case D3DRS_ALPHABLENDENABLE: Value ? glEnable(GL_BLEND) : glDisable(GL_BLEND); break;
      case D3DRS_SRCBLEND:        srcBlend = gl_blend(Value); glBlendFunc(srcBlend, dstBlend); break;
      case D3DRS_DESTBLEND:       dstBlend = gl_blend(Value); glBlendFunc(srcBlend, dstBlend); break;
      case D3DRS_CULLMODE:        apply_cull(Value); break;
      case D3DRS_ALPHATESTENABLE: alphaTestEnable = Value != 0; break;
      case D3DRS_ALPHAREF:        alphaRef = Value; break;
      case D3DRS_ALPHAFUNC:       alphaFunc = Value; break;
      case D3DRS_LIGHTING:        lighting = Value != 0; break;
      case D3DRS_AMBIENT:         // D3DCOLOR 0xAARRGGBB -> linear RGBA
        globalAmbient[0] = ((Value >> 16) & 0xff) / 255.0f;
        globalAmbient[1] = ((Value >> 8) & 0xff) / 255.0f;
        globalAmbient[2] = (Value & 0xff) / 255.0f;
        globalAmbient[3] = ((Value >> 24) & 0xff) / 255.0f;
        break;
      default:                    coverage::unhandled_render_state(State); break;
    }
    return D3D_OK;
  }
  // ponytail: only light index 0 is stored; multi-light accumulation grows when
  // a target game uses more than one enabled light.
  HRESULT SetLight(DWORD Index, const D3DLIGHT8* p) override {
    if (!p) return D3DERR_INVALIDCALL;
    if (Index == 0) light0 = *p;
    return D3D_OK;
  }
  HRESULT LightEnable(DWORD Index, BOOL Enable) override {
    if (Index == 0) light0On = Enable != 0;
    return D3D_OK;
  }
  HRESULT SetMaterial(const D3DMATERIAL8* p) override {
    if (!p) return D3DERR_INVALIDCALL;
    material = *p;
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
  // Upload the fixed-function lighting uniforms. Directional light: the vector to
  // the light is normalize(-Direction), atten 1 (per DXVK d3d9_fixed_function).
  // A disabled light contributes zero, leaving material emissive + global ambient.
  void set_light_uniforms(const ff::Program* p) {
    float ldir[3] = {0, 0, 1};
    const float zero[4] = {0, 0, 0, 0};
    const float* ldiff = zero;
    const float* lamb = zero;
    if (light0On) {
      float dx = -light0.Direction.x, dy = -light0.Direction.y, dz = -light0.Direction.z;
      float len = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (len > 1e-6f) { ldir[0] = dx / len; ldir[1] = dy / len; ldir[2] = dz / len; }
      ldiff = &light0.Diffuse.r;
      lamb  = &light0.Ambient.r;
    }
    glUniform3fv(p->uLightDir, 1, ldir);
    glUniform4fv(p->uLightDiffuse, 1, ldiff);
    glUniform4fv(p->uLightAmbient, 1, lamb);
    glUniform4fv(p->uGlobalAmbient, 1, globalAmbient);
    glUniform4fv(p->uMatDiffuse, 1, &material.Diffuse.r);
    glUniform4fv(p->uMatAmbient, 1, &material.Ambient.r);
    glUniform4fv(p->uMatEmissive, 1, &material.Emissive.r);
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
    const bool lit = lighting && (fvf & D3DFVF_NORMAL);
    const uint32_t af = alphaTestEnable ? alphaFunc : 0;
    const ff::Program* p = ff::program_for(fvf, textured ? colorOp : D3DTOP_DISABLE, af, lit);
    if (!p) return D3DERR_INVALIDCALL;

    glUseProgram(p->prog);
    // D3D row-major uploaded as GL column-major (i.e. transposed); combined with
    // proj*view*world in the shader this reproduces D3D's v*M semantics.
    glUniformMatrix4fv(p->uWorld, 1, GL_FALSE, world);
    glUniformMatrix4fv(p->uView,  1, GL_FALSE, view);
    glUniformMatrix4fv(p->uProj,  1, GL_FALSE, proj);

    glBindBuffer(GL_ARRAY_BUFFER, stream->b.glbuf);
    // Attributes are laid out in FVF order: XYZ, NORMAL, DIFFUSE, TEX1. Locations
    // are fixed (0=pos, 1=diffuse, 2=uv, 3=normal); only the byte offset walks.
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)0);
    GLuint off = 12;
    if (fvf & D3DFVF_NORMAL) {
      glEnableVertexAttribArray(3);
      glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, (GLsizei)stride, (void*)(uintptr_t)off);
      off += 12;
    } else glDisableVertexAttribArray(3);
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
    if (lit) set_light_uniforms(p);
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
