// SPDX-License-Identifier: GPL-3.0-only
// Windowing/context seam. Backed by SDL3 today; the only thing d3d8webgl needs.
#ifndef DX8WASM_PLATFORM_H
#define DX8WASM_PLATFORM_H
namespace platform {
// Create a WebGL2 (GLES3) context on the Emscripten canvas and make it current.
bool create_gl_context(int width, int height);
void present();               // swap/commit the backbuffer
void destroy_gl_context();
bool gl_context_alive();      // false once destroyed — guards late GL object deletes
}
#endif
