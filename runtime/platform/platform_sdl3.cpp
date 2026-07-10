// SPDX-License-Identifier: GPL-3.0-only
#include "platform/platform.h"
#include <SDL3/SDL.h>

namespace {
SDL_Window* g_window = nullptr;
SDL_GLContext g_ctx = nullptr;
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
void destroy_gl_context() {
  if (g_ctx) { SDL_GL_DestroyContext(g_ctx); g_ctx = nullptr; }
  if (g_window) { SDL_DestroyWindow(g_window); g_window = nullptr; }
  SDL_Quit();
}
}
