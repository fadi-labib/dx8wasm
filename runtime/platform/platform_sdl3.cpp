// SPDX-License-Identifier: GPL-3.0-only
#include "platform/platform.h"
#include "dx8wasm/contract.h"
#include <SDL3/SDL.h>
#include <cstring>

namespace {
SDL_Window* g_window = nullptr;
SDL_GLContext g_ctx = nullptr;
int32_t g_wheel = 0;
int g_quit = 0;
}

namespace platform {
bool create_gl_context(int width, int height) {
  destroy_gl_context();  // re-entrant-safe: tear down any prior state (null-safe on first call)
  if (!SDL_Init(SDL_INIT_VIDEO)) return false;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);   // D3D depth test needs a depth buffer
  g_window = SDL_CreateWindow("dx8wasm", width, height, SDL_WINDOW_OPENGL);
  if (!g_window) return false;
  g_ctx = SDL_GL_CreateContext(g_window);
  if (!g_ctx) { SDL_DestroyWindow(g_window); g_window = nullptr; return false; }
  return SDL_GL_MakeCurrent(g_window, g_ctx);
}
void present() { if (g_window) SDL_GL_SwapWindow(g_window); }
bool gl_context_alive() { return g_ctx != nullptr; }
}   // namespace platform

// Input pump (contract.h): SDL events -> raw key/mouse state. Lives here because
// SDL is the platform seam; declared extern "C" for the game-facing ABI.
extern "C" void dx8wasm_pump(dx8wasm_input* out) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_QUIT) g_quit = 1;
    else if (e.type == SDL_EVENT_MOUSE_WHEEL) g_wheel += (int32_t)e.wheel.y;
  }
  if (!out) return;
  std::memset(out, 0, sizeof *out);
  int nkeys = 0;
  const bool* ks = SDL_GetKeyboardState(&nkeys);   // SDL3: const bool*, scancode-indexed
  int n = nkeys < 256 ? nkeys : 256;
  for (int i = 0; i < n; i++) out->keys[i] = ks[i] ? 1 : 0;
  float mx = 0, my = 0;
  SDL_MouseButtonFlags mb = SDL_GetMouseState(&mx, &my);
  out->mouse_x = (int32_t)mx;
  out->mouse_y = (int32_t)my;
  if (mb & SDL_BUTTON_MASK(SDL_BUTTON_LEFT))   out->mouse_buttons |= 1;
  if (mb & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT))  out->mouse_buttons |= 2;
  if (mb & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) out->mouse_buttons |= 4;
  out->wheel = g_wheel; g_wheel = 0;
  out->quit = g_quit;
}

namespace platform {   // reopen so the closing brace below stays balanced
void destroy_gl_context() {
  if (g_ctx) { SDL_GL_DestroyContext(g_ctx); g_ctx = nullptr; }
  if (g_window) { SDL_DestroyWindow(g_window); g_window = nullptr; }
  SDL_Quit();
}
}
