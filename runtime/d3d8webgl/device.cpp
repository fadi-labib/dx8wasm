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

// Debug counters (integration bring-up): draw submissions, bind-pipeline failures,
// and clears. Exported so the platform seam's frame probe can report them.
long g_dx8_draws = 0, g_dx8_bindfail = 0, g_dx8_clears = 0;
extern "C" void dx8wasm_debug_counts(long* draws, long* bindfail, long* clears) {
  if (draws) *draws = g_dx8_draws; if (bindfail) *bindfail = g_dx8_bindfail; if (clears) *clears = g_dx8_clears;
}

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
struct Surface8;  // fwd: texture mip levels are handed out as surfaces

// DXT/S3TC block decompression (Generals' terrain/unit textures are DXT1/3/5).
// Decoded to the same [B,G,R,A] byte order the RGBA path uses, so the shader's
// .bgra swizzle recovers the correct color. CPU decode keeps it portable (no
// reliance on the WEBGL_compressed_texture_s3tc extension).
namespace dxt {
inline bool is_dxt(D3DFORMAT f) { return f == D3DFMT_DXT1 || f == D3DFMT_DXT3 || f == D3DFMT_DXT5; }
inline UINT block_bytes(D3DFORMAT f) { return f == D3DFMT_DXT1 ? 8u : 16u; }
inline size_t data_size(UINT w, UINT h, D3DFORMAT f) { return (size_t)((w + 3) / 4) * ((h + 3) / 4) * block_bytes(f); }
inline void rgb565(uint16_t c, int& r, int& g, int& b) {
  r = (((c >> 11) & 0x1f) * 255 + 15) / 31; g = (((c >> 5) & 0x3f) * 255 + 31) / 63; b = ((c & 0x1f) * 255 + 15) / 31;
}
// Decode the 8-byte color half of a block (shared by DXT1/3/5) into dst[BGRA].
// alpha16 supplies per-pixel alpha for DXT3/5; null => DXT1 (1-bit punch-through).
inline void color_block(const BYTE* b, BYTE* dst, UINT texW, UINT texH, UINT bx, UINT by, const BYTE* alpha16) {
  uint16_t c0 = (uint16_t)(b[0] | (b[1] << 8)), c1 = (uint16_t)(b[2] | (b[3] << 8));
  int r[4], g[4], bl[4]; rgb565(c0, r[0], g[0], bl[0]); rgb565(c1, r[1], g[1], bl[1]);
  bool punch = !alpha16 && c0 <= c1;   // DXT1 with 1-bit alpha
  if (!punch) { r[2] = (2*r[0]+r[1])/3; g[2] = (2*g[0]+g[1])/3; bl[2] = (2*bl[0]+bl[1])/3;
                r[3] = (r[0]+2*r[1])/3; g[3] = (g[0]+2*g[1])/3; bl[3] = (bl[0]+2*bl[1])/3; }
  else        { r[2] = (r[0]+r[1])/2;   g[2] = (g[0]+g[1])/2;   bl[2] = (bl[0]+bl[1])/2;
                r[3] = 0; g[3] = 0; bl[3] = 0; }
  uint32_t idx = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
  for (int py = 0; py < 4; py++) for (int px = 0; px < 4; px++) {
    UINT x = bx*4+px, y = by*4+py; if (x >= texW || y >= texH) continue;
    int i = (idx >> (2*(py*4+px))) & 3;
    int a = alpha16 ? alpha16[py*4+px] : (punch && i == 3 ? 0 : 255);
    BYTE* d = dst + ((size_t)y*texW + x)*4;
    d[0] = (BYTE)bl[i]; d[1] = (BYTE)g[i]; d[2] = (BYTE)r[i]; d[3] = (BYTE)a;   // B,G,R,A
  }
}
inline void dxt5_alpha(const BYTE* b, BYTE* out16) {
  int a0 = b[0], a1 = b[1], al[8]; al[0] = a0; al[1] = a1;
  if (a0 > a1) for (int i = 1; i < 7; i++) al[i+1] = ((7-i)*a0 + i*a1) / 7;
  else { for (int i = 1; i < 5; i++) al[i+1] = ((5-i)*a0 + i*a1) / 5; al[6] = 0; al[7] = 255; }
  uint64_t bits = 0; for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2+i] << (8*i);
  for (int i = 0; i < 16; i++) out16[i] = (BYTE)al[(bits >> (3*i)) & 7];
}
inline void decode(const BYTE* src, UINT w, UINT h, D3DFORMAT f, BYTE* dst /* w*h*4 */) {
  UINT bw = (w + 3) / 4, bh = (h + 3) / 4, bb = block_bytes(f);
  for (UINT by = 0; by < bh; by++) for (UINT bx = 0; bx < bw; bx++) {
    const BYTE* blk = src + ((size_t)by*bw + bx) * bb;
    if (f == D3DFMT_DXT1) { color_block(blk, dst, w, h, bx, by, nullptr); continue; }
    BYTE a16[16];
    if (f == D3DFMT_DXT5) dxt5_alpha(blk, a16);
    else for (int i = 0; i < 16; i++) a16[i] = (BYTE)(((blk[i/2] >> ((i&1)*4)) & 0xf) * 17);  // DXT3 explicit
    color_block(blk + 8, dst, w, h, bx, by, a16);
  }
}
} // namespace dxt

struct Texture8 : IDirect3DTexture8 {
  ULONG refs = 1;
  struct Level { UINT w, h; std::vector<BYTE> px; };
  std::vector<Level> levels;   // mip chain; levels[0] is the base
  D3DFORMAT fmt;
  GLuint tex = 0;
  // mips==0 => full chain down to 1x1. w() / h() below expose the base level so
  // the single-level callers (and existing smokes) read the same values as before.
  Texture8(UINT width, UINT height, UINT mips = 1, D3DFORMAT format = D3DFMT_A8R8G8B8) : fmt(format) {
    UINT lw = width ? width : 1, lh = height ? height : 1;
    UINT count = mips ? mips : 0xffffu;
    const bool compressed = dxt::is_dxt(format);
    for (UINT i = 0; i < count; ++i) {
      size_t bytes = compressed ? dxt::data_size(lw, lh, format) : (size_t)lw * lh * 4;
      levels.push_back({lw, lh, std::vector<BYTE>(bytes)});
      if (lw == 1 && lh == 1) break;
      lw = lw > 1 ? lw / 2 : 1; lh = lh > 1 ? lh / 2 : 1;
    }
    if (levels.empty()) levels.push_back({1, 1, std::vector<BYTE>(4)});
  }
  UINT w() const { return levels[0].w; }
  UINT h() const { return levels[0].h; }
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
  D3D_RESOURCE_STUBS(D3DRTYPE_TEXTURE)
  DWORD SetLOD(DWORD) override { return 0; }
  DWORD GetLOD() override { return 0; }
  DWORD GetLevelCount() override { return (DWORD)levels.size(); }
  HRESULT GetLevelDesc(UINT l, D3DSURFACE_DESC* d) override {
    if (!d || l >= levels.size()) return D3DERR_INVALIDCALL;
    std::memset(d, 0, sizeof *d); d->Format = fmt; d->Type = D3DRTYPE_TEXTURE;
    d->Pool = D3DPOOL_MANAGED; d->Width = levels[l].w; d->Height = levels[l].h;
    return D3D_OK;
  }
  HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface8** ppSurfaceLevel) override;  // out-of-line (needs Surface8)
  HRESULT LockRect(UINT l, D3DLOCKED_RECT* lr, const RECT*, DWORD) override {
    if (!lr || l >= levels.size()) return D3DERR_INVALIDCALL;
    // DXT pitch is bytes per ROW OF BLOCKS; uncompressed is bytes per pixel row.
    lr->Pitch = dxt::is_dxt(fmt) ? (int32_t)(((levels[l].w + 3) / 4) * dxt::block_bytes(fmt))
                                 : (int32_t)(levels[l].w * 4);
    lr->pBits = levels[l].px.data(); return D3D_OK;
  }
  HRESULT UnlockRect(UINT l) override { upload_level(l); return D3D_OK; }
  // Upload one mip level to GL. Filter/wrap kept NEAREST/CLAMP (unchanged from the
  // single-level impl) so existing pixel smokes stay bit-identical; real sampler
  // state is applied elsewhere.
  void upload_level(UINT l) {
    if (l >= levels.size()) return;
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    const Level& L = levels[l];
    if (dxt::is_dxt(fmt)) {
      std::vector<BYTE> rgba((size_t)L.w * L.h * 4);   // CPU-decompress DXT -> RGBA
      dxt::decode(L.px.data(), L.w, L.h, fmt, rgba.data());
      glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA, (GLsizei)L.w, (GLsizei)L.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    } else {
      glTexImage2D(GL_TEXTURE_2D, (GLint)l, GL_RGBA, (GLsizei)L.w, (GLsizei)L.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, L.px.data());
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  HRESULT AddDirtyRect(const RECT*) override { return D3D_OK; }
  ~Texture8() { if (tex && platform::gl_context_alive()) glDeleteTextures(1, &tex); }
};

// A surface is either a view onto a Texture8 mip level (parent != null; UnlockRect
// re-uploads that level) or a standalone CPU image (CreateImageSurface; owns its
// buffer). The engine's TextureClass loads pixels through this path, and D3DX's
// LoadSurfaceFromSurface (engine-side CompatLib) just needs LockRect to work.
struct Surface8 : IDirect3DSurface8 {
  ULONG refs = 1;
  D3DFORMAT fmt;
  UINT w, h;
  Texture8* parent;        // non-null => texture-level surface
  UINT level;
  std::vector<BYTE> own;   // used only when parent == nullptr
  Surface8(Texture8* p, UINT lvl) : fmt(p->fmt), w(p->levels[lvl].w), h(p->levels[lvl].h), parent(p), level(lvl) { p->AddRef(); }
  Surface8(UINT width, UINT height, D3DFORMAT format) : fmt(format), w(width), h(height), parent(nullptr), level(0), own((size_t)width * height * 4) {}
  ~Surface8() { if (parent) parent->Release(); }
  BYTE* base() { return parent ? parent->levels[level].px.data() : own.data(); }
  HRESULT QueryInterface(REFIID, void** o) override { if (o) *o = this; return D3D_OK; }
  ULONG AddRef() override { return ++refs; }
  ULONG Release() override { ULONG r = --refs; if (!r) delete this; return r; }
  HRESULT GetDevice(IDirect3DDevice8**) override { return D3DERR_INVALIDCALL; }
  HRESULT SetPrivateData(REFIID, const void*, DWORD, DWORD) override { return D3D_OK; }
  HRESULT GetPrivateData(REFIID, void*, DWORD*) override { return D3DERR_INVALIDCALL; }
  HRESULT FreePrivateData(REFIID) override { return D3D_OK; }
  HRESULT GetContainer(REFIID, void** o) override { if (o) *o = parent; return parent ? D3D_OK : D3DERR_INVALIDCALL; }
  HRESULT GetDesc(D3DSURFACE_DESC* d) override {
    if (!d) return D3DERR_INVALIDCALL;
    std::memset(d, 0, sizeof *d); d->Format = fmt; d->Type = D3DRTYPE_SURFACE;
    d->Pool = D3DPOOL_MANAGED; d->Width = w; d->Height = h; return D3D_OK;
  }
  HRESULT LockRect(D3DLOCKED_RECT* lr, const RECT* r, DWORD) override {
    if (!lr) return D3DERR_INVALIDCALL;
    UINT top = r ? (UINT)r->top : 0, left = r ? (UINT)r->left : 0;
    lr->Pitch = (int32_t)(w * 4);
    lr->pBits = base() + (size_t)top * (w * 4) + (size_t)left * 4;
    return D3D_OK;
  }
  HRESULT UnlockRect() override { if (parent) parent->upload_level(level); return D3D_OK; }
};

HRESULT Texture8::GetSurfaceLevel(UINT Level, IDirect3DSurface8** ppSurfaceLevel) {
  if (!ppSurfaceLevel || Level >= levels.size()) return D3DERR_INVALIDCALL;
  *ppSurfaceLevel = new Surface8(this, Level);
  return D3D_OK;
}

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
GLenum gl_cmpfunc(DWORD f) {
  switch (f) {
    case D3DCMP_NEVER: return GL_NEVER;   case D3DCMP_LESS: return GL_LESS;
    case D3DCMP_EQUAL: return GL_EQUAL;   case D3DCMP_LESSEQUAL: return GL_LEQUAL;
    case D3DCMP_GREATER: return GL_GREATER; case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
    case D3DCMP_GREATEREQUAL: return GL_GEQUAL; default: return GL_ALWAYS;
  }
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
    g_dx8_clears++;
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
  HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD, D3DFORMAT Format, D3DPOOL, IDirect3DTexture8** out) override {
    if (!out) return D3DERR_INVALIDCALL;
    if (Format != D3DFMT_A8R8G8B8 && Format != D3DFMT_X8R8G8B8 && !dxt::is_dxt(Format)) coverage::unhandled_format(Format);
    *out = new Texture8(Width, Height, Levels, Format); return D3D_OK;  // Levels==0 => full mip chain; DXT decoded on upload
  }
  HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer8* vb, UINT Stride) override {
    // Single-stream fixed-function pipeline: only stream 0 is used. The engine's
    // Apply_Render_State_Changes clears streams 1..N with SetStreamSource(i,null);
    // those must NOT clobber stream 0 (they did when the stream index was ignored).
    if (StreamNumber != 0) return D3D_OK;
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
  HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override {
    // Single texture stage: state for stage 1+ must NOT touch stage 0. Generals
    // sets stage-1 COLOROP=DISABLE to turn off the 2nd stage; when the stage index
    // was ignored that clobbered stage 0's MODULATE and every draw went untextured.
    if (Stage != 0) return D3D_OK;
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
      case D3DRS_ZFUNC:            glDepthFunc(gl_cmpfunc(Value)); break;
      case D3DRS_DITHERENABLE:     Value ? glEnable(GL_DITHER) : glDisable(GL_DITHER); break;
      case D3DRS_ZBIAS:            // legacy 0..16 depth-bias level -> polygon offset
        if (Value) { glPolygonOffset(-(float)Value, -(float)Value); glEnable(GL_POLYGON_OFFSET_FILL); }
        else glDisable(GL_POLYGON_OFFSET_FILL); break;
      case D3DRS_SHADEMODE:        break;   // GOURAUD (our default); FLAT unsupported
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
    g_dx8_draws++;
    glViewport((GLint)viewport.X, (GLint)viewport.Y, (GLsizei)viewport.Width, (GLsizei)viewport.Height);
    // FVF texcoord count is (fvf>>8)&0xf sets (D3DFVF_TEX1=0x100, TEX2=0x200, ...),
    // NOT a bitmask. The engine's 2D UI uses TEX2 (0x200); a `& D3DFVF_TEX1` test
    // wrongly reads that as untextured. Treat any texcoord set as "has UVs".
    const int texcoords = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    const bool textured = texcoords > 0 && texture;
    const bool lit = lighting && (fvf & D3DFVF_NORMAL);
    const uint32_t af = alphaTestEnable ? alphaFunc : 0;
    const ff::Program* p = ff::program_for(fvf, textured ? colorOp : D3DTOP_DISABLE, af, lit, fogEnable);
    if (!p) { g_dx8_bindfail++; return false; }
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
    if (texcoords > 0) { glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, vstride, (void*)(uintptr_t)off); }  // sample the first UV set
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
  // Backbuffer as a standalone Surface8 sized to the framebuffer. Enough for the
  // engine to query its description; pixel readback (glReadPixels) is a later
  // refinement (the smudge/distortion effects that copy from it).
  HRESULT GetBackBuffer(UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface8** o) override {
    if (!o) return D3DERR_INVALIDCALL;
    UINT bw = viewport.Width ? viewport.Width : 1, bh = viewport.Height ? viewport.Height : 1;
    *o = new Surface8(bw, bh, D3DFMT_X8R8G8B8);
    return D3D_OK;
  }
  HRESULT GetRasterStatus(D3DRASTER_STATUS* s) override { if (s) { s->InVBlank = 0; s->ScanLine = 0; } return D3D_OK; }
  void SetGammaRamp(DWORD, const D3DGAMMARAMP*) override {}
  void GetGammaRamp(D3DGAMMARAMP*) override {}
  HRESULT CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, void** o) override { if (o) *o = nullptr; warn_once("CreateVolumeTexture"); return D3DERR_INVALIDCALL; }
  HRESULT CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, void** o) override { if (o) *o = nullptr; warn_once("CreateCubeTexture"); return D3DERR_INVALIDCALL; }
  HRESULT CreateRenderTarget(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, BOOL, IDirect3DSurface8** o) override { if (o) *o = nullptr; warn_once("CreateRenderTarget"); return D3DERR_INVALIDCALL; }
  HRESULT CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, IDirect3DSurface8** o) override { if (o) *o = nullptr; warn_once("CreateDepthStencilSurface"); return D3DERR_INVALIDCALL; }
  HRESULT CreateImageSurface(UINT Width, UINT Height, D3DFORMAT Format, IDirect3DSurface8** o) override {
    if (!o) return D3DERR_INVALIDCALL;
    if (Format != D3DFMT_A8R8G8B8 && Format != D3DFMT_X8R8G8B8) coverage::unhandled_format(Format);
    *o = new Surface8(Width, Height, Format); return D3D_OK;
  }
  // Row-copy src surface region into dst (both 32-bit); re-upload if dst is a
  // texture level. Rects null => whole surface. dstPoints null => same origin.
  HRESULT CopyRects(IDirect3DSurface8* src, const RECT* srcRects, UINT n,
                    IDirect3DSurface8* dst, const POINT* dstPoints) override {
    auto* s = static_cast<Surface8*>(src); auto* d = static_cast<Surface8*>(dst);
    if (!s || !d) return D3DERR_INVALIDCALL;
    UINT count = n ? n : 1;
    for (UINT i = 0; i < count; ++i) {
      RECT r = srcRects ? srcRects[i] : RECT{0, 0, (LONG)s->w, (LONG)s->h};
      POINT p = dstPoints ? dstPoints[i] : POINT{r.left, r.top};
      UINT rw = (UINT)(r.right - r.left), rh = (UINT)(r.bottom - r.top);
      for (UINT y = 0; y < rh; ++y) {
        const BYTE* sp = s->base() + (size_t)(r.top + y) * (s->w * 4) + (size_t)r.left * 4;
        BYTE* dp = d->base() + (size_t)(p.y + y) * (d->w * 4) + (size_t)p.x * 4;
        std::memcpy(dp, sp, (size_t)rw * 4);
      }
    }
    if (d->parent) d->parent->upload_level(d->level);
    return D3D_OK;
  }
  // Copy every matching mip level src->dst (CPU) and re-upload each.
  HRESULT UpdateTexture(IDirect3DBaseTexture8* src, IDirect3DBaseTexture8* dst) override {
    auto* s = static_cast<Texture8*>(src); auto* d = static_cast<Texture8*>(dst);
    if (!s || !d) return D3DERR_INVALIDCALL;
    size_t n = s->levels.size() < d->levels.size() ? s->levels.size() : d->levels.size();
    for (size_t l = 0; l < n; ++l) {
      if (s->levels[l].w == d->levels[l].w && s->levels[l].h == d->levels[l].h)
        d->levels[l].px = s->levels[l].px;
      d->upload_level((UINT)l);
    }
    return D3D_OK;
  }
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
