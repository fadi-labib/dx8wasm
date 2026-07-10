// SPDX-License-Identifier: GPL-3.0-only
// graphics-ff: generate + cache a fixed-function GLSL program from an FVF code.
// 2.3 supports XYZ + DIFFUSE; texture/lighting variants grow here in 2.4+.
#ifndef DX8WASM_FF_SHADER_H
#define DX8WASM_FF_SHADER_H
#include <cstdint>
#include <GLES3/gl3.h>

namespace ff {
struct Program {
  GLuint prog = 0;
  GLint uWorld = -1, uView = -1, uProj = -1;
};
// Cached by FVF (the only state-key component until 2.4). Returns nullptr and
// logs loudly on an FVF we don't yet generate for — never silently wrong.
const Program* program_for_fvf(uint32_t fvf);
}
#endif
