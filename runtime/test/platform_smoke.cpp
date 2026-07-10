// SPDX-License-Identifier: GPL-3.0-only
// Proves SDL3 builds to wasm and hands back a usable WebGL2 context.
#include "platform/platform.h"
#include <emscripten.h>
#include <GLES3/gl3.h>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

int main() {
  if (!platform::create_gl_context(4, 4)) { report_error("platform: no context"); return 1; }
  glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  unsigned char px[4] = {0,0,0,0};
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
  platform::present();
  report_pixel(px[0], px[1], px[2], px[3]);
  return 0;
}
