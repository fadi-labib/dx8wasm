// SPDX-License-Identifier: GPL-3.0-only
// Conformance probe: exercises each D3D8 token against the real device and uses
// the coverage counters (contract.h) to classify it HANDLED vs FALLBACK. Emits
// JSON that scripts/conformance.mjs renders into docs/CONFORMANCE.md. This is a
// tool, not a pixel smoke — it derives coverage empirically so the matrix can't
// drift from what the runtime actually does.
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <emscripten.h>
#include <string>

EM_JS(void, report_json, (const char* s), { window.__conf = UTF8ToString(s); });

static IDirect3DDevice8* g_dev = nullptr;

// Each probe returns true if the token did NOT increment its unhandled counter.
static bool probe_rs(D3DRENDERSTATETYPE s, DWORD v) {
  dx8wasm_coverage a{}, b{}; dx8wasm_get_coverage(&a);
  g_dev->SetRenderState(s, v);
  dx8wasm_get_coverage(&b);
  return b.unhandled_render_states == a.unhandled_render_states;
}
static bool probe_top(DWORD op) {
  dx8wasm_coverage a{}, b{}; dx8wasm_get_coverage(&a);
  g_dev->SetTextureStageState(0, D3DTSS_COLOROP, op);
  dx8wasm_get_coverage(&b);
  return b.unhandled_texture_stage_ops == a.unhandled_texture_stage_ops;
}
static bool probe_fmt(D3DFORMAT f) {
  dx8wasm_coverage a{}, b{}; dx8wasm_get_coverage(&a);
  IDirect3DTexture8* t = nullptr;
  g_dev->CreateTexture(2, 2, 1, 0, f, D3DPOOL_MANAGED, &t);
  dx8wasm_get_coverage(&b);
  if (t) t->Release();
  return b.unhandled_formats == a.unhandled_formats;
}

int main() {
  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  D3DPRESENT_PARAMETERS pp{}; pp.BackBufferWidth = 4; pp.BackBufferHeight = 4;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev) != D3D_OK) {
    report_json("{\"error\":\"CreateDevice failed\"}"); return 1;
  }

  std::string j = "{\"renderStates\":[";
  struct RS { const char* name; D3DRENDERSTATETYPE s; DWORD v; };
  const RS rs[] = {
    {"D3DRS_ZENABLE", D3DRS_ZENABLE, 1}, {"D3DRS_ZWRITEENABLE", D3DRS_ZWRITEENABLE, 1},
    {"D3DRS_ALPHABLENDENABLE", D3DRS_ALPHABLENDENABLE, 1}, {"D3DRS_SRCBLEND", D3DRS_SRCBLEND, D3DBLEND_SRCALPHA},
    {"D3DRS_DESTBLEND", D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA}, {"D3DRS_CULLMODE", D3DRS_CULLMODE, D3DCULL_CCW},
    {"D3DRS_ALPHATESTENABLE", D3DRS_ALPHATESTENABLE, 1}, {"D3DRS_ALPHAREF", D3DRS_ALPHAREF, 128},
    {"D3DRS_ALPHAFUNC", D3DRS_ALPHAFUNC, D3DCMP_GREATER}, {"D3DRS_LIGHTING", D3DRS_LIGHTING, 1},
    {"D3DRS_SPECULARENABLE", D3DRS_SPECULARENABLE, 1}, {"D3DRS_AMBIENT", D3DRS_AMBIENT, 0x00202020u},
    {"D3DRS_FOGENABLE", D3DRS_FOGENABLE, 1}, {"D3DRS_FOGCOLOR", D3DRS_FOGCOLOR, 0x00ffffffu},
    {"D3DRS_FOGSTART", D3DRS_FOGSTART, 0}, {"D3DRS_FOGEND", D3DRS_FOGEND, 0},
    {"D3DRS_FOGTABLEMODE(LINEAR)", D3DRS_FOGTABLEMODE, D3DFOG_LINEAR},
    {"D3DRS_FOGTABLEMODE(EXP)", D3DRS_FOGTABLEMODE, D3DFOG_EXP},
    {"D3DRS_FOGDENSITY", D3DRS_FOGDENSITY, 0}, {"D3DRS_FILLMODE", D3DRS_FILLMODE, 2},
    {"D3DRS_ZFUNC", D3DRS_ZFUNC, D3DCMP_LESSEQUAL}, {"D3DRS_DITHERENABLE", D3DRS_DITHERENABLE, 1},
    {"D3DRS_ZBIAS", D3DRS_ZBIAS, 1}, {"D3DRS_SHADEMODE", D3DRS_SHADEMODE, 2},
    {"D3DRS_STENCILENABLE", D3DRS_STENCILENABLE, 1}, {"D3DRS_TEXTUREFACTOR", D3DRS_TEXTUREFACTOR, 0xffffffffu},
  };
  for (size_t i = 0; i < sizeof(rs) / sizeof(rs[0]); i++) {
    j += std::string(i ? "," : "") + "{\"name\":\"" + rs[i].name + "\",\"handled\":" +
         (probe_rs(rs[i].s, rs[i].v) ? "true" : "false") + "}";
  }

  j += "],\"textureOps\":[";
  struct OP { const char* name; DWORD v; };
  const OP ops[] = {
    {"D3DTOP_DISABLE", D3DTOP_DISABLE}, {"D3DTOP_SELECTARG1", D3DTOP_SELECTARG1},
    {"D3DTOP_MODULATE", D3DTOP_MODULATE}, {"D3DTOP_ADD", D3DTOP_ADD},
    {"D3DTOP_MODULATE2X", 5}, {"D3DTOP_ADDSIGNED", 8},
  };
  for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
    j += std::string(i ? "," : "") + "{\"name\":\"" + ops[i].name + "\",\"handled\":" +
         (probe_top(ops[i].v) ? "true" : "false") + "}";
  }

  j += "],\"formats\":[";
  struct FM { const char* name; int v; };
  const FM fmts[] = {
    {"D3DFMT_A8R8G8B8", D3DFMT_A8R8G8B8}, {"D3DFMT_X8R8G8B8", D3DFMT_X8R8G8B8},
    {"D3DFMT_R5G6B5", 23}, {"D3DFMT_A8", 28}, {"D3DFMT_DXT1", 0x31545844 /* 'DXT1' */},
  };
  for (size_t i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
    j += std::string(i ? "," : "") + "{\"name\":\"" + fmts[i].name + "\",\"handled\":" +
         (probe_fmt((D3DFORMAT)fmts[i].v) ? "true" : "false") + "}";
  }
  j += "]}";

  report_json(j.c_str());
  g_dev->Release(); d3d->Release();
  return 0;
}
