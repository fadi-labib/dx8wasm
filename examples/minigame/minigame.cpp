// SPDX-License-Identifier: GPL-3.0-only
// Minimal end-to-end integration example — the template a real DX8 game follows.
// It uses ONLY the public dx8wasm surface: the runtime contract (init + input
// pump) plus the stock D3D8 API. A keyboard-controllable sprite is drawn as a
// pre-transformed (screen-space) quad and moved by dx8wasm_pump() input each
// frame. No engine internals are touched — this is exactly how a game plugs in.
#include "d3d8/d3d8.h"
#include "dx8wasm/contract.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>

// SDL scancodes for the keys we read (contract: keys[] is SDL-scancode-indexed).
enum { SC_RIGHT = 79, SC_LEFT = 80, SC_DOWN = 81, SC_UP = 82 };

namespace {
struct V { float x, y, z, rhw; D3DCOLOR c; };   // FVF: XYZRHW | DIFFUSE

const int   CANVAS = 512;
const float SPRITE = 60.0f, SPEED = 6.0f;

IDirect3DDevice8* g_dev = nullptr;
IDirect3DVertexBuffer8* g_vb = nullptr;
IDirect3DIndexBuffer8* g_ib = nullptr;
float g_x = (CANVAS - SPRITE) / 2, g_y = (CANVAS - SPRITE) / 2;   // sprite top-left

float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void frame() {
  dx8wasm_input in;
  dx8wasm_pump(&in);
  if (in.keys[SC_LEFT])  g_x -= SPEED;
  if (in.keys[SC_RIGHT]) g_x += SPEED;
  if (in.keys[SC_UP])    g_y -= SPEED;
  if (in.keys[SC_DOWN])  g_y += SPEED;
  g_x = clampf(g_x, 0, CANVAS - SPRITE);
  g_y = clampf(g_y, 0, CANVAS - SPRITE);

  // Rewrite the sprite's screen-space quad at the current position (dynamic VB).
  const D3DCOLOR amber = 0xFFFFC020u;
  V v[4] = {
    {g_x,          g_y,          0, 1, amber}, {g_x + SPRITE, g_y,          0, 1, amber},
    {g_x + SPRITE, g_y + SPRITE, 0, 1, amber}, {g_x,          g_y + SPRITE, 0, 1, amber},
  };
  BYTE* dst = nullptr;
  g_vb->Lock(0, sizeof v, &dst, 0); std::memcpy(dst, v, sizeof v); g_vb->Unlock();

  g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFF102030u /* slate */, 1.0f, 0);
  g_dev->SetStreamSource(0, g_vb, sizeof(V));
  g_dev->SetIndices(g_ib, 0);
  g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 4, 0, 2);
  g_dev->Present(nullptr, nullptr, nullptr, nullptr);
}
} // namespace

int main() {
  dx8wasm_init_desc desc{};
  desc.abi_version = DX8WASM_ABI_VERSION;
  desc.backend = DX8WASM_BACKEND_WEBGL2;
  desc.canvas_selector = "#canvas";
  desc.log_unimplemented = 1;
  if (dx8wasm_init(&desc) != 0) return 1;

  IDirect3D8* d3d = Direct3DCreate8(D3D_SDK_VERSION);
  if (!d3d) return 1;
  D3DPRESENT_PARAMETERS pp{};
  pp.BackBufferWidth = CANVAS; pp.BackBufferHeight = CANVAS;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.Windowed = 1;
  if (d3d->CreateDevice(0, D3DDEVTYPE_HAL, nullptr, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev) != D3D_OK)
    return 1;
  emscripten_set_canvas_element_size("#canvas", CANVAS, CANVAS);   // SDL sizing is unreliable; pin it

  g_dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
  g_dev->CreateVertexBuffer(4 * sizeof(V), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_MANAGED, &g_vb);
  g_dev->CreateIndexBuffer(6 * sizeof(uint16_t), 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &g_ib);
  uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
  BYTE* dst = nullptr;
  g_ib->Lock(0, sizeof idx, &dst, 0); std::memcpy(dst, idx, sizeof idx); g_ib->Unlock();

  emscripten_set_main_loop(frame, 0, 1);   // browser drives the loop
  return 0;
}
