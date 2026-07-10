// SPDX-License-Identifier: GPL-3.0-only
// graphics-ff: generate + cache a fixed-function GLSL program from the render
// state key. 2.3 covered XYZ + DIFFUSE; 2.4 adds a TEX1 sampler + one combiner.
#ifndef DX8WASM_FF_SHADER_H
#define DX8WASM_FF_SHADER_H
#include <cstdint>
#include <GLES3/gl3.h>

namespace ff {
struct Program {
  GLuint prog = 0;
  GLint uWorld = -1, uView = -1, uProj = -1, uTex = -1, uAlphaRef = -1;
};
// Cached by (FVF, texture color-op, alpha-test func). colorOp is ignored without
// texture coords; alphaFunc == 0 means no alpha test, else it is a D3DCMPFUNC and
// the shader emulates the (missing in GLES) fixed-function alpha test via discard.
// Returns nullptr and logs loudly on an unsupported combo — never silently wrong.
const Program* program_for(uint32_t fvf, uint32_t colorOp, uint32_t alphaFunc);
}
#endif
