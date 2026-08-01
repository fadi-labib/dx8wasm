// SPDX-License-Identifier: GPL-3.0-only
// States this backend deliberately ACCEPTS WITHOUT ACTING must not bump a coverage counter.
// The counters mean "unimplemented, fell back"; a decision to no-op is not a gap, and letting
// the two share a counter is how 40k/frame of D3DRS_PATCHSEGMENTS came to dominate a capture
// that was supposed to rank real work. The mirror assertion matters just as much: a state the
// backend truly cannot express must STILL count, or this smoke would pass by silencing
// everything. Reports [1,0,0,255] when both halves hold.
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <cstring>
#include <emscripten.h>
#include <GLES3/gl3.h>
#include <initializer_list>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

// Sum of every coverage counter. Any single token leaking into any counter moves this.
// `fallbacks_taken` is deliberately NOT included: coverage::note() bumps it alongside whichever
// per-family counter it also bumps, so folding it in here would double-count every delta this
// smoke asserts (before+1 would actually need to be before+2, etc.) for no added falsifiability.
static uint32_t total() {
  dx8wasm_coverage c{};
  dx8wasm_get_coverage(&c);
  return c.unhandled_render_states + c.unhandled_texture_stage_ops +
         c.unhandled_formats + c.unhandled_texture_stage_states + c.unhandled_vertex_formats;
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) { report_error("Direct3DCreate8 returned null"); return 1; }
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  IDirect3DDevice8* dev = nullptr;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev) != D3D_OK || !dev) {
    report_error("CreateDevice failed"); return 1;
  }

  // --- Accepted without acting: no counter may move. ---
  const uint32_t before = total();
  // D3DFILL_SOLID is what this backend already draws, so accepting it is exact, not a fallback.
  dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
  if (total() != before) { report_error("D3DFILL_SOLID was counted as unhandled"); return 1; }

  // Anisotropy is a sampler parameter with a real GL mapping (EXT_texture_filter_anisotropic),
  // clamped to 1 when the extension is absent — either way it is handled, never a fallback.
  dev->SetTextureStageState(0, D3DTSS_MAXANISOTROPY, 4);
  if (total() != before) { report_error("D3DTSS_MAXANISOTROPY was counted as unhandled"); return 1; }

  // The assertion above only proves the state is accepted without a coverage bump — it does not
  // prove apply_sampler()'s glTexParameterf(...MAXANISOTROPY...) actually runs, because that call
  // only fires from a real bound-texture draw. No other smoke in the suite draws with
  // D3DTSS_MAXANISOTROPY set, so without this the GL path (and its absent-extension branch, where
  // aniso_limit() may be 0 under SwiftShader) would be exercised by nothing. Draw a textured quad
  // with the aniso value still set from above and confirm the modulated readback is correct —
  // proving the sampler path runs to completion (and, if the extension is unavailable, that the
  // skip branch is safe) rather than merely accepted.
  {
    struct Vertex { float x, y, z; D3DCOLOR c; float u, v; };   // XYZ|DIFFUSE|TEX1, stride 24
    const uint32_t kFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;
    const D3DCOLOR kDiffuse = 0xFF80FFFFu;   // (0.502,1,1,1)
    const D3DCOLOR kTexel   = 0xFFFF8040u;   // (1,0.502,0.251,1)
    // MODULATE product -> (128,128,64,255), same expected value as draw_tex_smoke.
    Vertex verts[4] = {
      {-1, -1, 0, kDiffuse, 0, 0}, {1, -1, 0, kDiffuse, 1, 0},
      { 1,  1, 0, kDiffuse, 1, 1}, {-1, 1, 0, kDiffuse, 0, 1},
    };
    uint16_t idx[6] = {0, 1, 2, 0, 2, 3};

    IDirect3DVertexBuffer8* vb = nullptr;
    IDirect3DIndexBuffer8* ib = nullptr;
    IDirect3DTexture8* tex = nullptr;
    if (dev->CreateVertexBuffer(sizeof verts, 0, kFVF, D3DPOOL_MANAGED, &vb) != D3D_OK ||
        dev->CreateIndexBuffer(sizeof idx, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib) != D3D_OK ||
        dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex) != D3D_OK) {
      report_error("aniso draw: resource creation failed"); return 1;
    }
    BYTE* dst = nullptr;
    vb->Lock(0, sizeof verts, &dst, 0); std::memcpy(dst, verts, sizeof verts); vb->Unlock();
    ib->Lock(0, sizeof idx, &dst, 0);   std::memcpy(dst, idx, sizeof idx);     ib->Unlock();
    D3DLOCKED_RECT lr{};
    tex->LockRect(0, &lr, nullptr, 0);
    for (int i = 0; i < 4; i++) std::memcpy((BYTE*)lr.pBits + i * 4, &kTexel, 4);
    tex->UnlockRect(0);

    D3DMATRIX id{}; id.m[0][0] = id.m[1][1] = id.m[2][2] = id.m[3][3] = 1.0f;
    dev->SetTransform(D3DTS_WORLD, &id);
    dev->SetTransform(D3DTS_VIEW, &id);
    dev->SetTransform(D3DTS_PROJECTION, &id);
    dev->SetStreamSource(0, vb, sizeof(Vertex));
    dev->SetIndices(ib, 0);
    dev->SetVertexShader(kFVF);
    dev->SetTexture(0, tex);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    // D3DTSS_MAXANISOTROPY is still 4 from the assertion above — bound to stage 0, so
    // apply_sampler() programs it (or safely skips it) when this draw binds the texture.

    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);
    if (dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2) != D3D_OK) {
      report_error("aniso draw: DrawIndexedPrimitive failed"); return 1;
    }
    dev->Present(nullptr, nullptr, nullptr, nullptr);
    unsigned char px[4] = {0};
    glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    if (px[0] != 128 || px[1] != 128 || px[2] != 64 || px[3] != 255) {
      report_error("aniso draw: modulated readback did not match the expected texel*diffuse product");
      return 1;
    }
    tex->Release(); ib->Release(); vb->Release();
    dev->SetTexture(0, nullptr);
  }
  if (total() != before) { report_error("the anisotropy draw was counted as unhandled"); return 1; }

  // The fourth material-colour source. MATERIAL and COLOR1 are answerable from state the device
  // already tracks, so they must not count.
  dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
  dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR1);
  if (total() != before) { report_error("a handled SPECULARMATERIALSOURCE value was counted"); return 1; }

  // --- Genuinely unimplemented: the counter MUST move. ---
  // GLES3 has no glPolygonMode, so wireframe cannot be expressed and must keep reporting.
  dev->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
  if (total() != before + 1) { report_error("D3DFILL_WIREFRAME stopped being reported"); return 1; }

  // COLOR2 sources the specular colour from D3DFVF_SPECULAR, which is not uploaded as an
  // attribute (device.cpp skips its stride to keep texcoord offsets correct). It must keep
  // reporting — specifically, so a future capture that uses it says so instead of going quiet.
  dev->SetRenderState(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2);
  if (total() != before + 2) { report_error("SPECULARMATERIALSOURCE(COLOR2) was silently accepted"); return 1; }

  // The documented no-op group. Each is accepted and ignored for a reason written at the call
  // site; none is a rendering request this backend fails to serve, so none may count. Left
  // counting, D3DRS_PATCHSEGMENTS alone (40,138 hits in the Generals capture) outranks every
  // genuine finding in any ordering by frequency.
  const uint32_t beforeNoop = total();
  dev->SetRenderState(D3DRS_PATCHSEGMENTS, 0x40000000u /* a float bit-pattern, per W3D */);
  dev->SetRenderState(D3DRS_SOFTWAREVERTEXPROCESSING, 0);
  dev->SetRenderState(D3DRS_RANGEFOGENABLE, 0);
  for (D3DTEXTURESTAGESTATETYPE t : {D3DTSS_BUMPENVMAT00, D3DTSS_BUMPENVMAT01, D3DTSS_BUMPENVMAT10,
                                     D3DTSS_BUMPENVMAT11, D3DTSS_BUMPENVLSCALE, D3DTSS_BUMPENVLOFFSET})
    dev->SetTextureStageState(0, t, 0);
  if (total() != beforeNoop) { report_error("a documented no-op token was counted as unhandled"); return 1; }

  // The prerequisite op stays a real gap, so the six matrix states above are still discoverable
  // through the one token that would make them live. Silencing the states must not silence this.
  const uint32_t beforeOp = total();
  dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_BUMPENVMAP);
  if (total() != beforeOp + 1) { report_error("D3DTOP_BUMPENVMAP stopped being reported"); return 1; }

  // Rendering must still work after both.
  dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF3366CCu, 1.0f, 0);
  dev->Present(nullptr, nullptr, nullptr, nullptr);

  dev->Release(); d3d->Release();
  report_pixel(1, 0, 0, 255);
  return 0;
}
