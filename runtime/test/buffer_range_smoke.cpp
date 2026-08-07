// SPDX-License-Identifier: GPL-3.0-only
// Covers GLBuffer::Unlock's PARTIAL-RANGE upload path (glBufferSubData), which no other smoke
// reaches: every one of them locks a whole buffer, so they all take the glBufferData respecify
// branch and would stay green if the sub-range path were completely broken. That gap is GT-09 --
// the 24 pixel smokes and the determinism digest passed byte-identically across the behaviour
// change that introduced this path.
//
// THE ORACLE, and why a wrong state cannot produce it.
// One vertex buffer, two quads: the LEFT half of the viewport and the RIGHT half.
//   1. Lock the WHOLE buffer, write both quads RED, Unlock.   -> whole-buffer glBufferData
//   2. Lock ONLY the right quad's bytes, write GREEN, Unlock. -> glBufferSubData at a NON-ZERO
//                                                                offset, the path under test
//   3. Draw both quads, read back one pixel from each half.
//
// Left must be RED and right must be GREEN, and each half catches a different defect:
//   * left RED   -- bytes OUTSIDE the locked range survived. If Unlock uploaded only the locked
//                   bytes into a buffer whose storage had never been fully populated, or clobbered
//                   the remainder, the left half is undefined rather than red. This is the
//                   correctness argument behind "the first upload is always whole-buffer".
//   * right GREEN-- the locked bytes reached the GPU AT THE RIGHT OFFSET. An Unlock that passed 0
//                   instead of lock_off (an easy and silent mistake) paints the LEFT half green
//                   and leaves the right red -- i.e. exactly inverted, not merely "some colour".
//
// So a single wrong offset, a dropped range, or a lost remainder each produce a distinguishable
// failure, and no single bug produces the passing combination. Note the smoke deliberately does NOT
// assert on gl.buf_waste_ratio: those gauges are emitted once per second from Present, and a smoke
// that renders one frame would be asserting on an emission that never happened -- a test passing
// across nothing, which is this project's characteristic failure (HARNESS-TRAPS.md 1).
#include "d3d8/d3d8.h"
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <cstdlib>
#include <cstring>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

struct Vertex { float x, y, z; D3DCOLOR c; };   // FVF: XYZ | DIFFUSE, stride 16

static const int    kDim   = 8;                 // 8x8 back buffer: left = x<4, right = x>=4
static const D3DCOLOR kRed   = 0xFFFF0000u;
static const D3DCOLOR kGreen = 0xFF00FF00u;

// Two triangles covering [x0,x1] x [-1,1].
static void fill_quad(Vertex* v, float x0, float x1, D3DCOLOR c) {
  v[0] = {x0, -1, 0, c}; v[1] = {x1, -1, 0, c}; v[2] = {x1, 1, 0, c};
  v[3] = {x0, -1, 0, c}; v[4] = {x1,  1, 0, c}; v[5] = {x0, 1, 0, c};
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = kDim; pp.BackBufferHeight = kDim;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  Vertex verts[12];
  fill_quad(verts + 0, -1.0f, 0.0f, kRed);      // left  quad, vertices 0..5
  fill_quad(verts + 6,  0.0f, 1.0f, kRed);      // right quad, vertices 6..11

  IDirect3DVertexBuffer8* vb = nullptr;
  if (dev->CreateVertexBuffer(sizeof verts, 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &vb) != D3D_OK) {
    report_error("CreateVertexBuffer failed"); return 1;
  }

  // (1) Whole-buffer lock. D3D8 spec: Lock(0, 0, ...) means the ENTIRE buffer, and this smoke uses
  //     that spelling deliberately -- it is the form the size-is-zero branch in Unlock decodes.
  BYTE* dst = nullptr;
  if (vb->Lock(0, 0, &dst, 0) != D3D_OK || !dst) { report_error("full Lock failed"); return 1; }
  std::memcpy(dst, verts, sizeof verts);
  vb->Unlock();

  // (2) Sub-range lock over the RIGHT quad only, at a non-zero offset, recoloured green.
  const UINT off  = 6 * sizeof(Vertex);
  const UINT size = 6 * sizeof(Vertex);
  Vertex right[6];
  fill_quad(right, 0.0f, 1.0f, kGreen);
  dst = nullptr;
  if (vb->Lock(off, size, &dst, D3DLOCK_NOOVERWRITE) != D3D_OK || !dst) {
    report_error("sub-range Lock failed"); return 1;
  }
  std::memcpy(dst, right, sizeof right);
  vb->Unlock();

  D3DMATRIX id{}; id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
  dev->SetTransform(D3DTS_WORLD, &id);
  dev->SetTransform(D3DTS_VIEW, &id);
  dev->SetTransform(D3DTS_PROJECTION, &id);
  dev->SetStreamSource(0, vb, sizeof(Vertex));
  dev->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE);
  vb->Release();                                 // device holds its own ref

  // Blue background: distinct from both quad colours, so "nothing was drawn" cannot be mistaken
  // for either half being correct.
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);
  if (dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 4) != D3D_OK) {
    report_error("DrawPrimitive failed"); return 1;
  }
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  unsigned char left[4] = {0}, rightpx[4] = {0};
  glReadPixels(1, kDim / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
  glReadPixels(kDim - 2, kDim / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rightpx);

  // Left half: untouched by the sub-range lock, must still be the red written by the full upload.
  if (left[0] < 200 || left[1] > 55 || left[2] > 55) {
    report_error("left half is not red -- bytes outside the locked range were lost");
    return 1;
  }
  // Right half: the only bytes the sub-range lock covered, must be green.
  if (rightpx[1] < 200 || rightpx[0] > 55 || rightpx[2] > 55) {
    report_error("right half is not green -- the locked sub-range did not reach the GPU");
    return 1;
  }
  report_pixel(1, 0, 0, 255);                    // internally-checked smoke: pass sentinel

  dev->Release(); d3d->Release();
  return 0;
}
