// SPDX-License-Identifier: GPL-3.0-only
// Interactive browser demo of the d3d8webgl fixed-function pipeline. Two quads
// animate every frame in a real canvas (not a headless 4x4 buffer):
//   left  — a spinning TEXTURED quad (2x2 four-colour pattern, MODULATE)
//   right — a LIT quad whose brightness pulses as the directional light sweeps
// The split reflects a current limitation: lit + textured in one draw isn't a
// supported graphics-ff combo yet. This is the first "see it with your eyes"
// artifact; correctness is still owned by the headless pixel smokes.
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <cstring>

namespace {
// Minimal row-major 4x4 helpers. The device uploads D3D matrices transposed for
// GL, so to_d3d() transposes our GL-desired transform back into D3DMATRIX form.
struct M4 { float m[16]; };   // row-major: m[r*4 + c]
M4 identity() { M4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
M4 mul(const M4& a, const M4& b) {
  M4 r{};
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      for (int k = 0; k < 4; k++)
        r.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
  return r;
}
M4 translate(float x, float y, float z) { M4 r = identity(); r.m[3] = x; r.m[7] = y; r.m[11] = z; return r; }
M4 scale(float s) { M4 r = identity(); r.m[0] = r.m[5] = r.m[10] = s; return r; }
M4 rotZ(float a) {
  M4 r = identity(); float c = std::cos(a), s = std::sin(a);
  r.m[0] = c; r.m[1] = -s; r.m[4] = s; r.m[5] = c; return r;
}
D3DMATRIX to_d3d(const M4& g) {   // D3DMATRIX is read transposed by the device's GL upload
  D3DMATRIX d;
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      d.m[i][j] = g.m[j * 4 + i];
  return d;
}

struct TV { float x, y, z; D3DCOLOR c; float u, v; };   // textured quad vertex
struct LV { float x, y, z, nx, ny, nz; };               // lit quad vertex

IDirect3DDevice8* dev = nullptr;
IDirect3DVertexBuffer8 *vbTex = nullptr, *vbLit = nullptr;
IDirect3DIndexBuffer8 *ibTex = nullptr, *ibLit = nullptr;
IDirect3DTexture8* tex = nullptr;
float angle = 0.0f;

template <class T> void upload(IDirect3DVertexBuffer8* vb, const T* v, size_t n) {
  BYTE* dst = nullptr; vb->Lock(0, n * sizeof(T), &dst, 0); std::memcpy(dst, v, n * sizeof(T)); vb->Unlock();
}

void frame() {
  angle += 0.02f;
  glViewport(0, 0, 512, 512);   // SDL's emscripten window sizing is unreliable; pin it
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF101418u /* near-black */, 1.0f, 0);

  // Left: spinning textured quad, centred at x = -0.5.
  D3DMATRIX wTex = to_d3d(mul(mul(translate(-0.5f, 0, 0), rotZ(angle)), scale(0.35f)));
  dev->SetTransform(D3DTS_WORLD, &wTex);
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
  dev->SetStreamSource(0, vbTex, sizeof(TV));
  dev->SetIndices(ibTex, 0);
  dev->SetTexture(0, tex);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);

  // Right: lit quad, centred at x = +0.5, brightness driven by the sweeping light.
  D3DLIGHT8 light{};
  light.Type = D3DLIGHT_DIRECTIONAL;
  light.Diffuse = {1, 1, 1, 1};
  light.Direction = {std::sin(angle), 0, -std::cos(angle)};   // sweeps -> N·L = cos(angle)
  dev->SetLight(0, &light);
  D3DMATRIX wLit = to_d3d(mul(translate(0.5f, 0, 0), scale(0.35f)));
  dev->SetTransform(D3DTS_WORLD, &wLit);
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_NORMAL);
  dev->SetStreamSource(0, vbLit, sizeof(LV));
  dev->SetIndices(ibLit, 0);
  dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);

  dev->Present(nullptr, nullptr, nullptr, nullptr);
}
} // namespace

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 512; pp.BackBufferHeight = 512;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK) return 1;
  emscripten_set_canvas_element_size("#canvas", 512, 512);   // force the drawing buffer size

  D3DMATRIX id = to_d3d(identity());
  dev->SetTransform(D3DTS_VIEW, &id);
  dev->SetTransform(D3DTS_PROJECTION, &id);

  // Textured quad + 2x2 four-colour texture (red/green/blue/yellow quadrants).
  TV tv[4] = {
    {-1, -1, 0, 0xFFFFFFFFu, 0, 0}, {1, -1, 0, 0xFFFFFFFFu, 1, 0},
    { 1,  1, 0, 0xFFFFFFFFu, 1, 1}, {-1, 1, 0, 0xFFFFFFFFu, 0, 1},
  };
  LV lv[4] = {
    {-1, -1, 0, 0, 0, 1}, {1, -1, 0, 0, 0, 1}, {1, 1, 0, 0, 0, 1}, {-1, 1, 0, 0, 0, 1},
  };
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};

  dev->CreateVertexBuffer(sizeof tv, 0, D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1, D3DPOOL_MANAGED, &vbTex);
  dev->CreateVertexBuffer(sizeof lv, 0, D3DFVF_XYZ | D3DFVF_NORMAL, D3DPOOL_MANAGED, &vbLit);
  dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ibTex);
  dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ibLit);
  upload(vbTex, tv, 4); upload(vbLit, lv, 4);
  { BYTE* d = nullptr; ibTex->Lock(0, sizeof idx, &d, 0); std::memcpy(d, idx, sizeof idx); ibTex->Unlock();
    ibLit->Lock(0, sizeof idx, &d, 0); std::memcpy(d, idx, sizeof idx); ibLit->Unlock(); }

  dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex);
  D3DCOLOR texels[4] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFF00u};
  D3DLOCKED_RECT lr{}; tex->LockRect(0, &lr, nullptr, 0);
  std::memcpy(lr.pBits, texels, sizeof texels); tex->UnlockRect(0);

  // Static state: MODULATE combiner for the textured quad; lighting + material
  // for the lit quad (only affects the NORMAL-bearing FVF).
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
  dev->SetRenderState(D3DRS_LIGHTING, 1);
  dev->SetRenderState(D3DRS_AMBIENT, 0x00202020u);   // subtle fill so the lit quad never goes fully black
  D3DMATERIAL8 mat{};
  // Ambient shares the diffuse hue so the lit quad reads as a pulsing blue (dim
  // ambient floor -> bright) rather than fading through grey as the light sweeps.
  mat.Diffuse = {0.3f, 0.7f, 1.0f, 1.0f}; mat.Ambient = {0.3f, 0.7f, 1.0f, 1.0f};
  dev->SetMaterial(&mat);
  dev->LightEnable(0, 1);

  emscripten_set_main_loop(frame, 0, 1);   // browser drives the loop; never returns
  return 0;
}
