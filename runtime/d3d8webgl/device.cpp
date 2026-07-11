// SPDX-License-Identifier: GPL-3.0-only
// The d3d8webgl device. Implements the FULL D3D8 COM vtable (so a game links and
// dispatches correctly); the supported subset does real work, the rest are honest
// stubs (log-once / coverage / sensible defaults) pending Phase C.
#include "d3d8/d3d8.h"
#include "platform/platform.h"
#include "graphics-ff/ff_shader.h"
#include "coverage/coverage.h"
#include <GLES3/gl3.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
void warn_once(const char* what) {   // one line per distinct unimplemented method
  static const char* seen[64]; static int n = 0;
  for (int i = 0; i < n; i++) if (seen[i] == what) return;
  if (n < 64) seen[n++] = what;
  std::fprintf(stderr, "[dx8wasm] %s: stubbed (Phase C)\n", what);
}

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

// Shared IUnknown + IDirect3DResource8 stub prefix, mixed into the resource
// objects. QueryInterface returns self (+AddRef) — permissive but adequate.
#define D3D_RESOURCE_STUBS(RTYPE)                                                     \
  HRESULT QueryInterface(REFIID, void** o) override { if (o) { *o = this; ++refs; } return D3D_OK; } \
  HRESULT GetDevice(IDirect3DDevice8**) override { return D3DERR_INVALIDCALL; }       \
  HRESULT SetPrivateData(REFIID, const void*, DWORD, DWORD) override { return D3D_OK; } \
  HRESULT GetPrivateData(REFIID, void*, DWORD*) override { return D3DERR_INVALIDCALL; } \
  HRESULT FreePrivateData(REFIID) override { return D3D_OK; }                          \
  DWORD SetPriority(DWORD) override { return 0; }                                      \
  DWORD GetPriority() override { return 0; }                                           \
  void PreLoad() override {}                                                           \
  D3DRESOURCETYPE GetType() override { return RTYPE; }

struct VertexBuffer8 : IDirect3DVertexBuffer8 {
  GLBuffer b; ULONG refs = 1; UINT length; DWORD fvf;
  VertexBuffer8(UINT len, DWORD f) : b(GL_ARRAY_BUFFER, len), length(len), fvf(f) {}
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
  D3D_RESOURCE_STUBS(D3DRTYPE_VERTEXBUFFER)
  HRESULT Lock(UINT o, UINT s, BYTE** pp, DWORD) override { return b.Lock(o, s, pp); }
  HRESULT Unlock() override { return b.Unlock(); }
  HRESULT GetDesc(D3DVERTEXBUFFER_DESC* d) override {
    if (d) { std::memset(d, 0, sizeof *d); d->Type = D3DRTYPE_VERTEXBUFFER; d->Pool = D3DPOOL_MANAGED; d->Size = length; d->FVF = fvf; }
    return D3D_OK;
  }
};

struct IndexBuffer8 : IDirect3DIndexBuffer8 {
  GLBuffer b; ULONG refs = 1; UINT length; D3DFORMAT fmt;
  IndexBuffer8(UINT len, D3DFORMAT f) : b(GL_ELEMENT_ARRAY_BUFFER, len), length(len), fmt(f) {}
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
  D3D_RESOURCE_STUBS(D3DRTYPE_INDEXBUFFER)
  HRESULT Lock(UINT o, UINT s, BYTE** pp, DWORD) override { return b.Lock(o, s, pp); }
  HRESULT Unlock() override { return b.Unlock(); }
  HRESULT GetDesc(D3DINDEXBUFFER_DESC* d) override {
    if (d) { std::memset(d, 0, sizeof *d); d->Type = D3DRTYPE_INDEXBUFFER; d->Pool = D3DPOOL_MANAGED; d->Size = length; d->Format = fmt; }
    return D3D_OK;
  }
};

// Level-0-only A8R8G8B8 texture. CPU staging holds the D3D [B,G,R,A] bytes;
// UnlockRect uploads them verbatim as GL_RGBA (the .bgra shader swizzle fixes
// channel order). Nearest + clamp — no mips/filtering until a target needs them.
struct Texture8 : IDirect3DTexture8 {
  ULONG refs = 1;
  UINT w, h;
  std::vector<BYTE> cpu;
  GLuint tex = 0;
  Texture8(UINT width, UINT height) : w(width), h(height), cpu((size_t)width * height * 4) {}
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
  D3D_RESOURCE_STUBS(D3DRTYPE_TEXTURE)
  DWORD SetLOD(DWORD) override { return 0; }
  DWORD GetLOD() override { return 0; }
  DWORD GetLevelCount() override { return 1; }
  HRESULT GetLevelDesc(UINT, D3DSURFACE_DESC* d) override {
    if (d) { std::memset(d, 0, sizeof *d); d->Format = D3DFMT_A8R8G8B8; d->Type = D3DRTYPE_TEXTURE; d->Pool = D3DPOOL_MANAGED; d->Width = w; d->Height = h; }
    return D3D_OK;
  }
  HRESULT GetSurfaceLevel(UINT, IDirect3DSurface8**) override { warn_once("Texture8::GetSurfaceLevel"); return D3DERR_INVALIDCALL; }
  HRESULT LockRect(UINT, D3DLOCKED_RECT* lr, const RECT*, DWORD) override {
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
  HRESULT AddDirtyRect(const RECT*) override { return D3D_OK; }
  ~Texture8() { if (tex && platform::gl_context_alive()) glDeleteTextures(1, &tex); }
};

void set_identity(float* m) {
  std::memset(m, 0, 16 * sizeof(float));
  m[0] = m[5] = m[10] = m[15] = 1.0f;
}
float as_float(DWORD v) { float f; std::memcpy(&f, &v, sizeof f); return f; }   // D3DRS float-in-DWORD

bool prim_info(D3DPRIMITIVETYPE t, UINT pc, GLenum& mode, GLsizei& n) {
  switch (t) {
    case D3DPT_POINTLIST:     mode = GL_POINTS;         n = (GLsizei)pc;       return true;
    case D3DPT_LINELIST:      mode = GL_LINES;          n = (GLsizei)(pc * 2); return true;
    case D3DPT_LINESTRIP:     mode = GL_LINE_STRIP;     n = (GLsizei)(pc + 1); return true;
    case D3DPT_TRIANGLELIST:  mode = GL_TRIANGLES;      n = (GLsizei)(pc * 3); return true;
    case D3DPT_TRIANGLESTRIP: mode = GL_TRIANGLE_STRIP; n = (GLsizei)(pc + 2); return true;
    case D3DPT_TRIANGLEFAN:   mode = GL_TRIANGLE_FAN;   n = (GLsizei)(pc + 2); return true;
  }
  return false;
}
GLenum gl_blend(DWORD b) {
  switch (b) {
    case D3DBLEND_ZERO: return GL_ZERO;             case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;   case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;   case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    default: return GL_ONE;
  }
}

struct Device8 : IDirect3DDevice8 {
  ULONG refs = 1;
  VertexBuffer8* stream = nullptr;
  IndexBuffer8* indices = nullptr;
  UINT stride = 0;
  uint32_t fvf = 0;
  Texture8* texture = nullptr;
  uint32_t colorOp = D3DTOP_MODULATE;
  GLenum srcBlend = GL_ONE, dstBlend = GL_ZERO;
  bool alphaTestEnable = false, zWrite = true;
  uint32_t alphaFunc = D3DCMP_ALWAYS;
  DWORD alphaRef = 0;
  float world[16], view[16], proj[16];
  bool lighting = false, specularEnable = false;
  float globalAmbient[4] = {0, 0, 0, 0};
  D3DLIGHT8 lights[ff::MAX_LIGHTS]{};
  bool lightOn[ff::MAX_LIGHTS] = {false};
  D3DMATERIAL8 material{ {1, 1, 1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, 0}, 0 };
  bool fogEnable = false;
  float fogColor[3] = {0, 0, 0}, fogStart = 0.0f, fogEnd = 1.0f;
  float vpW, vpH;
  D3DVIEWPORT8 viewport;
  GLuint scratchVB = 0, scratchIB = 0;   // reused for DrawPrimitiveUP (user-pointer) draws

  Device8(int w, int h) : vpW((float)w), vpH((float)h) {
    set_identity(world); set_identity(view); set_identity(proj);
    viewport = {0, 0, (DWORD)w, (DWORD)h, 0.0f, 1.0f};
    glDepthFunc(GL_LEQUAL);
  }

  HRESULT QueryInterface(REFIID, void** o) override { if (o) { *o = this; ++refs; } return D3D_OK; }
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override {
    ULONG r = --refs;
    if (!r) {
      if (stream) stream->Release();
      if (indices) indices->Release();
      if (texture) texture->Release();
      platform::destroy_gl_context();
      delete this;
    }
    return r;
  }

  HRESULT Clear(DWORD, const D3DRECT*, DWORD Flags, D3DCOLOR c, float, DWORD) override {
    GLbitfield mask = 0;
    if (Flags & D3DCLEAR_TARGET) {
      glClearColor(((c >> 16) & 0xff) / 255.0f, ((c >> 8) & 0xff) / 255.0f,
                   (c & 0xff) / 255.0f, ((c >> 24) & 0xff) / 255.0f);
      mask |= GL_COLOR_BUFFER_BIT;
    }
    if (Flags & D3DCLEAR_ZBUFFER) { glDepthMask(GL_TRUE); mask |= GL_DEPTH_BUFFER_BIT; }
    if (Flags & D3DCLEAR_STENCIL) mask |= GL_STENCIL_BUFFER_BIT;
    glClear(mask);
    if (Flags & D3DCLEAR_ZBUFFER) glDepthMask(zWrite ? GL_TRUE : GL_FALSE);
    return D3D_OK;
  }
  HRESULT Present(const RECT*, const RECT*, HWND, const RGNDATA*) override { platform::present(); return D3D_OK; }
  HRESULT BeginScene() override { return D3D_OK; }
  HRESULT EndScene() override { return D3D_OK; }

  HRESULT CreateVertexBuffer(UINT Length, DWORD, DWORD FVF, D3DPOOL, IDirect3DVertexBuffer8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    *out = new VertexBuffer8(Length, FVF); return D3D_OK;
  }
  HRESULT CreateIndexBuffer(UINT Length, DWORD, D3DFORMAT Format, D3DPOOL, IDirect3DIndexBuffer8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    *out = new IndexBuffer8(Length, Format); return D3D_OK;
  }
  HRESULT CreateTexture(UINT Width, UINT Height, UINT, DWORD, D3DFORMAT Format, D3DPOOL, IDirect3DTexture8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    if (Format != D3DFMT_A8R8G8B8 && Format != D3DFMT_X8R8G8B8) coverage::unhandled_format(Format);
    *out = new Texture8(Width, Height); return D3D_OK;
  }
  HRESULT SetStreamSource(UINT, IDirect3DVertexBuffer8* vb, UINT Stride) override {
    auto* n = static_cast<VertexBuffer8*>(vb);
    if (n) n->AddRef(); if (stream) stream->Release();
    stream = n; stride = Stride; return D3D_OK;
  }
  HRESULT SetIndices(IDirect3DIndexBuffer8* ib, UINT) override {   // ponytail: BaseVertexIndex assumed 0
    auto* n = static_cast<IndexBuffer8*>(ib);
    if (n) n->AddRef(); if (indices) indices->Release();
    indices = n; return D3D_OK;
  }
  HRESULT SetVertexShader(DWORD Handle) override { fvf = Handle; return D3D_OK; }
  HRESULT SetTexture(DWORD, IDirect3DBaseTexture8* t) override {
    auto* n = static_cast<Texture8*>(t);
    if (n) n->AddRef(); if (texture) texture->Release();
    texture = n; return D3D_OK;
  }
  HRESULT SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override {
    if (Type == D3DTSS_COLOROP) {
      switch (Value) {
        case D3DTOP_DISABLE: case D3DTOP_SELECTARG1: case D3DTOP_SELECTARG2:
        case D3DTOP_MODULATE: case D3DTOP_MODULATE2X: case D3DTOP_MODULATE4X:
        case D3DTOP_ADD: case D3DTOP_ADDSIGNED: colorOp = Value; break;
        default: coverage::unhandled_texture_op(Value); colorOp = D3DTOP_MODULATE;
      }
    }
    return D3D_OK;
  }
  HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override {
    switch (State) {
      case D3DRS_ZENABLE:          Value ? glEnable(GL_DEPTH_TEST) : glDisable(GL_DEPTH_TEST); break;
      case D3DRS_ZWRITEENABLE:     zWrite = Value != 0; glDepthMask(zWrite ? GL_TRUE : GL_FALSE); break;
      case D3DRS_ALPHABLENDENABLE: Value ? glEnable(GL_BLEND) : glDisable(GL_BLEND); break;
      case D3DRS_SRCBLEND:         srcBlend = gl_blend(Value); glBlendFunc(srcBlend, dstBlend); break;
      case D3DRS_DESTBLEND:        dstBlend = gl_blend(Value); glBlendFunc(srcBlend, dstBlend); break;
      case D3DRS_CULLMODE:         apply_cull(Value); break;
      case D3DRS_ALPHATESTENABLE:  alphaTestEnable = Value != 0; break;
      case D3DRS_ALPHAREF:         alphaRef = Value; break;
      case D3DRS_ALPHAFUNC:        alphaFunc = Value; break;
      case D3DRS_LIGHTING:         lighting = Value != 0; break;
      case D3DRS_SPECULARENABLE:   specularEnable = Value != 0; break;
      case D3DRS_FOGENABLE:        fogEnable = Value != 0; break;
      case D3DRS_FOGSTART:         fogStart = as_float(Value); break;
      case D3DRS_FOGEND:           fogEnd = as_float(Value); break;
      case D3DRS_FOGCOLOR:
        fogColor[0] = ((Value >> 16) & 0xff) / 255.0f; fogColor[1] = ((Value >> 8) & 0xff) / 255.0f;
        fogColor[2] = (Value & 0xff) / 255.0f; break;
      case D3DRS_FOGTABLEMODE: case D3DRS_FOGVERTEXMODE:
        if (Value != D3DFOG_LINEAR && Value != D3DFOG_NONE) coverage::unhandled_render_state(State); break;
      case D3DRS_AMBIENT:
        globalAmbient[0] = ((Value >> 16) & 0xff) / 255.0f; globalAmbient[1] = ((Value >> 8) & 0xff) / 255.0f;
        globalAmbient[2] = (Value & 0xff) / 255.0f; globalAmbient[3] = ((Value >> 24) & 0xff) / 255.0f; break;
      default: coverage::unhandled_render_state(State); break;
    }
    return D3D_OK;
  }
  HRESULT SetLight(DWORD Index, const D3DLIGHT8* p) override {
    if (!p || Index >= ff::MAX_LIGHTS) return D3DERR_INVALIDCALL;
    lights[Index] = *p; return D3D_OK;
  }
  HRESULT LightEnable(DWORD Index, BOOL Enable) override {
    if (Index < ff::MAX_LIGHTS) lightOn[Index] = Enable != 0; return D3D_OK;
  }
  HRESULT SetMaterial(const D3DMATERIAL8* p) override { if (!p) return D3DERR_INVALIDCALL; material = *p; return D3D_OK; }
  HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX* pMatrix) override {
    if (!pMatrix) return D3DERR_INVALIDCALL;
    float* dst = State == D3DTS_WORLD ? world : State == D3DTS_VIEW ? view : State == D3DTS_PROJECTION ? proj : nullptr;
    if (!dst) return D3D_OK;   // texture/other transforms ignored for now
    std::memcpy(dst, pMatrix->m, 16 * sizeof(float)); return D3D_OK;
  }
  HRESULT SetViewport(const D3DVIEWPORT8* v) override { if (v) viewport = *v; return D3D_OK; }
  HRESULT GetViewport(D3DVIEWPORT8* v) override { if (v) *v = viewport; return D3D_OK; }

  void apply_cull(DWORD mode) {
    if (mode == D3DCULL_NONE) { glDisable(GL_CULL_FACE); return; }
    glEnable(GL_CULL_FACE); glFrontFace(GL_CCW); glCullFace(mode == D3DCULL_CCW ? GL_FRONT : GL_BACK);
  }
  void set_light_uniforms(const ff::Program* p) {
    int type[ff::MAX_LIGHTS];
    float dir[ff::MAX_LIGHTS * 3], pos[ff::MAX_LIGHTS * 3], atten[ff::MAX_LIGHTS * 3];
    float spotDir[ff::MAX_LIGHTS * 3], spotParams[ff::MAX_LIGHTS * 3];
    float range[ff::MAX_LIGHTS], diff[ff::MAX_LIGHTS * 4], amb[ff::MAX_LIGHTS * 4], spec[ff::MAX_LIGHTS * 4];
    int count = 0;
    for (int i = 0; i < ff::MAX_LIGHTS; i++) {
      const D3DLIGHT8& L = lights[i];
      if (!lightOn[i] || (L.Type != D3DLIGHT_DIRECTIONAL && L.Type != D3DLIGHT_POINT && L.Type != D3DLIGHT_SPOT)) continue;
      type[count] = (L.Type == D3DLIGHT_SPOT) ? 2 : (L.Type == D3DLIGHT_POINT) ? 1 : 0;
      float dx = -L.Direction.x, dy = -L.Direction.y, dz = -L.Direction.z;
      float len = std::sqrt(dx * dx + dy * dy + dz * dz); if (len < 1e-6f) len = 1.0f;
      dir[count * 3 + 0] = dx / len; dir[count * 3 + 1] = dy / len; dir[count * 3 + 2] = dz / len;
      pos[count * 3 + 0] = L.Position.x; pos[count * 3 + 1] = L.Position.y; pos[count * 3 + 2] = L.Position.z;
      atten[count * 3 + 0] = L.Attenuation0; atten[count * 3 + 1] = L.Attenuation1; atten[count * 3 + 2] = L.Attenuation2;
      range[count] = L.Range;
      spotDir[count * 3 + 0] = -dir[count * 3 + 0]; spotDir[count * 3 + 1] = -dir[count * 3 + 1]; spotDir[count * 3 + 2] = -dir[count * 3 + 2];
      spotParams[count * 3 + 0] = std::cos(L.Theta * 0.5f); spotParams[count * 3 + 1] = std::cos(L.Phi * 0.5f); spotParams[count * 3 + 2] = L.Falloff;
      std::memcpy(&diff[count * 4], &L.Diffuse.r, 4 * sizeof(float));
      std::memcpy(&amb[count * 4], &L.Ambient.r, 4 * sizeof(float));
      std::memcpy(&spec[count * 4], &L.Specular.r, 4 * sizeof(float));
      count++;
    }
    glUniform1i(p->uLightCount, count);
    if (count) {
      glUniform1iv(p->uLightType, count, type); glUniform3fv(p->uLightDir, count, dir);
      glUniform3fv(p->uLightPos, count, pos); glUniform3fv(p->uLightAtten, count, atten);
      glUniform1fv(p->uLightRange, count, range); glUniform3fv(p->uSpotDir, count, spotDir);
      glUniform3fv(p->uSpotParams, count, spotParams); glUniform4fv(p->uLightDiffuse, count, diff);
      glUniform4fv(p->uLightAmbient, count, amb); glUniform4fv(p->uLightSpecular, count, spec);
    }
    glUniform1i(p->uSpecularEnable, specularEnable ? 1 : 0);
    glUniform1f(p->uMatPower, material.Power);
    glUniform4fv(p->uGlobalAmbient, 1, globalAmbient); glUniform4fv(p->uMatDiffuse, 1, &material.Diffuse.r);
    glUniform4fv(p->uMatAmbient, 1, &material.Ambient.r); glUniform4fv(p->uMatEmissive, 1, &material.Emissive.r);
    glUniform4fv(p->uMatSpecular, 1, &material.Specular.r);
  }
  // Select the FF program, upload uniforms, and bind vertex attributes from the
  // currently-bound GL_ARRAY_BUFFER at the given stride. Shared by the buffer and
  // user-pointer draw paths. Returns false if no program supports the state.
  bool bind_pipeline(GLsizei vstride) {
    glViewport((GLint)viewport.X, (GLint)viewport.Y, (GLsizei)viewport.Width, (GLsizei)viewport.Height);
    const bool textured = (fvf & D3DFVF_TEX1) && texture;
    const bool lit = lighting && (fvf & D3DFVF_NORMAL);
    const uint32_t af = alphaTestEnable ? alphaFunc : 0;
    const ff::Program* p = ff::program_for(fvf, textured ? colorOp : D3DTOP_DISABLE, af, lit, fogEnable);
    if (!p) return false;
    glUseProgram(p->prog);
    glUniformMatrix4fv(p->uWorld, 1, GL_FALSE, world);
    glUniformMatrix4fv(p->uView, 1, GL_FALSE, view);
    glUniformMatrix4fv(p->uProj, 1, GL_FALSE, proj);
    const bool rhw = fvf & D3DFVF_XYZRHW;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, rhw ? 4 : 3, GL_FLOAT, GL_FALSE, vstride, (void*)0);
    GLuint off = rhw ? 16 : 12;
    if (fvf & D3DFVF_NORMAL) { glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, vstride, (void*)(uintptr_t)off); off += 12; }
    else glDisableVertexAttribArray(3);
    if (fvf & D3DFVF_DIFFUSE) { glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, vstride, (void*)(uintptr_t)off); off += 4; }
    else glDisableVertexAttribArray(1);
    if (fvf & D3DFVF_TEX1) { glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, vstride, (void*)(uintptr_t)off); }
    else glDisableVertexAttribArray(2);
    if (textured) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture->tex); glUniform1i(p->uTex, 0); }
    if (p->uAlphaRef >= 0) glUniform1f(p->uAlphaRef, alphaRef / 255.0f);
    if (rhw) glUniform2f(p->uViewport, vpW, vpH);
    if (lit) set_light_uniforms(p);
    if (fogEnable) { glUniform3fv(p->uFogColor, 1, fogColor); glUniform1f(p->uFogStart, fogStart); glUniform1f(p->uFogEnd, fogEnd); }
    return true;
  }
  HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE Type, UINT, UINT, UINT StartIndex, UINT PrimitiveCount) override {
    GLenum mode; GLsizei icount;
    if (!stream || !indices || !prim_info(Type, PrimitiveCount, mode, icount)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ARRAY_BUFFER, stream->b.glbuf);
    if (!bind_pipeline((GLsizei)stride)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indices->b.glbuf);
    glDrawElements(mode, icount, GL_UNSIGNED_SHORT, (void*)(uintptr_t)(StartIndex * sizeof(uint16_t)));
    return D3D_OK;
  }

  // --- ABI-complete stubs (log-once / sensible defaults; Phase C) --------------
  HRESULT TestCooperativeLevel() override { return D3D_OK; }
  UINT GetAvailableTextureMem() override { return 256u * 1024 * 1024; }
  HRESULT ResourceManagerDiscardBytes(DWORD) override { return D3D_OK; }
  HRESULT GetDirect3D(IDirect3D8** o) override { if (o) *o = nullptr; warn_once("GetDirect3D"); return D3DERR_INVALIDCALL; }
  HRESULT GetDeviceCaps(D3DCAPS8* c) override {
    if (!c) return D3DERR_INVALIDCALL;
    std::memset(c, 0, sizeof *c);
    c->DeviceType = D3DDEVTYPE_HAL;
    c->MaxTextureWidth = c->MaxTextureHeight = 4096; c->MaxTextureRepeat = 8192;
    c->MaxTextureBlendStages = 2; c->MaxSimultaneousTextures = 2;
    c->MaxActiveLights = ff::MAX_LIGHTS; c->MaxVertexBlendMatrices = 0;
    c->MaxPrimitiveCount = 0xffff; c->MaxVertexIndex = 0xffff; c->MaxStreams = 1;
    c->VertexShaderVersion = 0; c->PixelShaderVersion = 0;   // fixed-function only
    c->TextureOpCaps = 0xffffffff; c->TextureCaps = 0;
    return D3D_OK;
  }
  HRESULT GetDisplayMode(D3DDISPLAYMODE* m) override {
    if (m) { m->Width = (UINT)vpW; m->Height = (UINT)vpH; m->RefreshRate = 60; m->Format = D3DFMT_X8R8G8B8; }
    return D3D_OK;
  }
  HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) override {
    if (p) { std::memset(p, 0, sizeof *p); p->DeviceType = D3DDEVTYPE_HAL; p->BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING; }
    return D3D_OK;
  }
  HRESULT SetCursorProperties(UINT, UINT, IDirect3DSurface8*) override { return D3D_OK; }
  void SetCursorPosition(UINT, UINT, DWORD) override {}
  BOOL ShowCursor(BOOL) override { return 1; }
  HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain8**) override { warn_once("CreateAdditionalSwapChain"); return D3DERR_INVALIDCALL; }
  HRESULT Reset(D3DPRESENT_PARAMETERS* pp) override {
    if (pp && pp->BackBufferWidth) { vpW = (float)pp->BackBufferWidth; vpH = (float)pp->BackBufferHeight;
      viewport = {0, 0, pp->BackBufferWidth, pp->BackBufferHeight, 0.0f, 1.0f}; }
    return D3D_OK;
  }
  HRESULT GetBackBuffer(UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface8** o) override { if (o) *o = nullptr; warn_once("GetBackBuffer"); return D3DERR_INVALIDCALL; }
  HRESULT GetRasterStatus(D3DRASTER_STATUS* s) override { if (s) { s->InVBlank = 0; s->ScanLine = 0; } return D3D_OK; }
  void SetGammaRamp(DWORD, const D3DGAMMARAMP*) override {}
  void GetGammaRamp(D3DGAMMARAMP*) override {}
  HRESULT CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, void** o) override { if (o) *o = nullptr; warn_once("CreateVolumeTexture"); return D3DERR_INVALIDCALL; }
  HRESULT CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, void** o) override { if (o) *o = nullptr; warn_once("CreateCubeTexture"); return D3DERR_INVALIDCALL; }
  HRESULT CreateRenderTarget(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, BOOL, IDirect3DSurface8** o) override { if (o) *o = nullptr; warn_once("CreateRenderTarget"); return D3DERR_INVALIDCALL; }
  HRESULT CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, IDirect3DSurface8** o) override { if (o) *o = nullptr; warn_once("CreateDepthStencilSurface"); return D3DERR_INVALIDCALL; }
  HRESULT CreateImageSurface(UINT, UINT, D3DFORMAT, IDirect3DSurface8** o) override { if (o) *o = nullptr; warn_once("CreateImageSurface"); return D3DERR_INVALIDCALL; }
  HRESULT CopyRects(IDirect3DSurface8*, const RECT*, UINT, IDirect3DSurface8*, const POINT*) override { warn_once("CopyRects"); return D3DERR_INVALIDCALL; }
  HRESULT UpdateTexture(IDirect3DBaseTexture8*, IDirect3DBaseTexture8*) override { warn_once("UpdateTexture"); return D3DERR_INVALIDCALL; }
  HRESULT GetFrontBuffer(IDirect3DSurface8*) override { warn_once("GetFrontBuffer"); return D3DERR_INVALIDCALL; }
  HRESULT SetRenderTarget(IDirect3DSurface8*, IDirect3DSurface8*) override { return D3D_OK; }
  HRESULT GetRenderTarget(IDirect3DSurface8** o) override { if (o) *o = nullptr; return D3DERR_INVALIDCALL; }
  HRESULT GetDepthStencilSurface(IDirect3DSurface8** o) override { if (o) *o = nullptr; return D3DERR_INVALIDCALL; }
  HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* m) override {
    if (!m) return D3DERR_INVALIDCALL;
    const float* s = State == D3DTS_WORLD ? world : State == D3DTS_VIEW ? view : State == D3DTS_PROJECTION ? proj : nullptr;
    if (s) std::memcpy(m->m, s, 16 * sizeof(float)); else std::memset(m, 0, sizeof *m);
    return D3D_OK;
  }
  HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) override { warn_once("MultiplyTransform"); return D3D_OK; }
  HRESULT GetMaterial(D3DMATERIAL8* m) override { if (m) *m = material; return D3D_OK; }
  HRESULT GetLight(DWORD i, D3DLIGHT8* l) override { if (l && i < ff::MAX_LIGHTS) *l = lights[i]; return D3D_OK; }
  HRESULT GetLightEnable(DWORD i, BOOL* e) override { if (e) *e = (i < ff::MAX_LIGHTS && lightOn[i]) ? 1 : 0; return D3D_OK; }
  HRESULT SetClipPlane(DWORD, const float*) override { return D3D_OK; }
  HRESULT GetClipPlane(DWORD, float*) override { return D3D_OK; }
  HRESULT GetRenderState(D3DRENDERSTATETYPE, DWORD* v) override { if (v) *v = 0; return D3D_OK; }
  HRESULT BeginStateBlock() override { warn_once("BeginStateBlock"); return D3D_OK; }
  HRESULT EndStateBlock(DWORD* t) override { if (t) *t = 0; return D3D_OK; }
  HRESULT ApplyStateBlock(DWORD) override { return D3D_OK; }
  HRESULT CaptureStateBlock(DWORD) override { return D3D_OK; }
  HRESULT DeleteStateBlock(DWORD) override { return D3D_OK; }
  HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE, DWORD* t) override { if (t) *t = 0; return D3D_OK; }
  HRESULT SetClipStatus(const D3DCLIPSTATUS8*) override { return D3D_OK; }
  HRESULT GetClipStatus(D3DCLIPSTATUS8* s) override { if (s) { s->ClipUnion = 0; s->ClipIntersection = 0xffffffff; } return D3D_OK; }
  HRESULT GetTexture(DWORD, IDirect3DBaseTexture8** o) override { if (o) { *o = texture; if (texture) texture->AddRef(); } return D3D_OK; }
  HRESULT GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD* v) override { if (v) *v = 0; return D3D_OK; }
  HRESULT ValidateDevice(DWORD* n) override { if (n) *n = 1; return D3D_OK; }
  HRESULT GetInfo(DWORD, void*, DWORD) override { return D3DERR_INVALIDCALL; }
  HRESULT SetPaletteEntries(UINT, const PALETTEENTRY*) override { return D3D_OK; }
  HRESULT GetPaletteEntries(UINT, PALETTEENTRY*) override { return D3D_OK; }
  HRESULT SetCurrentTexturePalette(UINT) override { return D3D_OK; }
  HRESULT GetCurrentTexturePalette(UINT* n) override { if (n) *n = 0; return D3D_OK; }
  HRESULT DrawPrimitive(D3DPRIMITIVETYPE Type, UINT StartVertex, UINT PrimitiveCount) override {
    GLenum mode; GLsizei vcount;
    if (!stream || !prim_info(Type, PrimitiveCount, mode, vcount)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ARRAY_BUFFER, stream->b.glbuf);
    if (!bind_pipeline((GLsizei)stride)) return D3DERR_INVALIDCALL;
    glDrawArrays(mode, (GLint)StartVertex, vcount);
    return D3D_OK;
  }
  // User-pointer draws: vertex/index data is inline (no D3D buffer). Stream it
  // through reused scratch GL buffers. Common for UI/particles/dynamic geometry.
  HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE Type, UINT PrimitiveCount, const void* pVertexData, UINT VertexStride) override {
    GLenum mode; GLsizei vcount;
    if (!pVertexData || !prim_info(Type, PrimitiveCount, mode, vcount)) return D3DERR_INVALIDCALL;
    if (!scratchVB) glGenBuffers(1, &scratchVB);
    glBindBuffer(GL_ARRAY_BUFFER, scratchVB);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vcount * VertexStride, pVertexData, GL_STREAM_DRAW);
    if (!bind_pipeline((GLsizei)VertexStride)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDrawArrays(mode, 0, vcount);
    return D3D_OK;
  }
  HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE Type, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount,
                                 const void* pIndexData, D3DFORMAT IndexDataFormat, const void* pVertexData, UINT VertexStride) override {
    GLenum mode; GLsizei icount;
    if (!pIndexData || !pVertexData || !prim_info(Type, PrimitiveCount, mode, icount)) return D3DERR_INVALIDCALL;
    const bool i32 = IndexDataFormat == D3DFMT_INDEX32;
    const GLenum itype = i32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
    if (!scratchVB) glGenBuffers(1, &scratchVB);
    if (!scratchIB) glGenBuffers(1, &scratchIB);
    glBindBuffer(GL_ARRAY_BUFFER, scratchVB);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MinVertexIndex + NumVertices) * VertexStride, pVertexData, GL_STREAM_DRAW);
    if (!bind_pipeline((GLsizei)VertexStride)) return D3DERR_INVALIDCALL;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scratchIB);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)icount * (i32 ? 4 : 2), pIndexData, GL_STREAM_DRAW);
    glDrawElements(mode, icount, itype, (void*)0);
    return D3D_OK;
  }
  HRESULT ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer8*, DWORD) override { warn_once("ProcessVertices"); return D3DERR_INVALIDCALL; }
  HRESULT CreateVertexShader(const DWORD*, const DWORD*, DWORD* h, DWORD) override { if (h) *h = 0; warn_once("CreateVertexShader"); return D3D_OK; }
  HRESULT GetVertexShader(DWORD* h) override { if (h) *h = fvf; return D3D_OK; }
  HRESULT DeleteVertexShader(DWORD) override { return D3D_OK; }
  HRESULT SetVertexShaderConstant(DWORD, const void*, DWORD) override { return D3D_OK; }
  HRESULT GetVertexShaderConstant(DWORD, void*, DWORD) override { return D3D_OK; }
  HRESULT GetVertexShaderDeclaration(DWORD, void*, DWORD*) override { return D3DERR_INVALIDCALL; }
  HRESULT GetVertexShaderFunction(DWORD, void*, DWORD*) override { return D3DERR_INVALIDCALL; }
  HRESULT GetStreamSource(UINT, IDirect3DVertexBuffer8** o, UINT* s) override { if (o) { *o = stream; if (stream) stream->AddRef(); } if (s) *s = stride; return D3D_OK; }
  HRESULT GetIndices(IDirect3DIndexBuffer8** o, UINT* base) override { if (o) { *o = indices; if (indices) indices->AddRef(); } if (base) *base = 0; return D3D_OK; }
  HRESULT SetPixelShader(DWORD) override { warn_once("SetPixelShader"); return D3D_OK; }
  HRESULT GetPixelShader(DWORD* h) override { if (h) *h = 0; return D3D_OK; }
  HRESULT CreatePixelShader(const DWORD*, DWORD* h) override { if (h) *h = 0; warn_once("CreatePixelShader"); return D3D_OK; }
  HRESULT DeletePixelShader(DWORD) override { return D3D_OK; }
  HRESULT SetPixelShaderConstant(DWORD, const void*, DWORD) override { return D3D_OK; }
  HRESULT GetPixelShaderConstant(DWORD, void*, DWORD) override { return D3D_OK; }
  HRESULT GetPixelShaderFunction(DWORD, void*, DWORD*) override { return D3DERR_INVALIDCALL; }
  HRESULT DrawRectPatch(UINT, const float*, const D3DRECTPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
  HRESULT DrawTriPatch(UINT, const float*, const D3DTRIPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
  HRESULT DeletePatch(UINT) override { return D3D_OK; }
};
} // namespace

IDirect3DDevice8* dx8_create_device(int w, int h) {
  if (!platform::create_gl_context(w, h)) return nullptr;
  return new Device8(w, h);
}
