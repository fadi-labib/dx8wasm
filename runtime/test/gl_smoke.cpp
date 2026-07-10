// SPDX-License-Identifier: GPL-3.0-only
// Toolchain + rig proof: raw Emscripten WebGL2 context, clear, read back, report.
// No SDL3, no D3D8 — a failure here is the toolchain or the test rig, nothing else.
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#include <emscripten.h>
#include <GLES3/gl3.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

int main() {
  EmscriptenWebGLContextAttributes a; emscripten_webgl_init_context_attributes(&a);
  a.majorVersion = 2; a.minorVersion = 0;
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &a);
  if (ctx <= 0) { report_error("no WebGL2 context"); return 1; }
  emscripten_webgl_make_context_current(ctx);
  glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char px[4] = {0,0,0,0};
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  report_pixel(px[0], px[1], px[2], px[3]);
  return 0;
}
