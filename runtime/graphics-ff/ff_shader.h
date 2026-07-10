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
  // Fixed-function lighting uniforms (valid only on lit programs).
  GLint uLightDir = -1, uLightDiffuse = -1, uLightAmbient = -1, uGlobalAmbient = -1;
  GLint uMatDiffuse = -1, uMatAmbient = -1, uMatEmissive = -1;
};
// Cached by (FVF, texture color-op, alpha-test func, lit). colorOp is ignored
// without texture coords; alphaFunc == 0 means no alpha test, else it is a
// D3DCMPFUNC emulated via discard (GLES has no fixed-function alpha test). When
// `lit`, the vertex shader computes per-vertex ambient + directional-diffuse
// lighting (Gouraud, matching D3D). Returns nullptr and logs loudly on an
// unsupported combo — never silently wrong.
const Program* program_for(uint32_t fvf, uint32_t colorOp, uint32_t alphaFunc, bool lit);
}
#endif
