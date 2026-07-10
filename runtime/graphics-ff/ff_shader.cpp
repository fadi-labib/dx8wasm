// SPDX-License-Identifier: GPL-3.0-only
#include "graphics-ff/ff_shader.h"
#include "d3d8/d3d8.h"
#include <cstdio>
#include <string>
#include <unordered_map>

namespace ff {
namespace {

std::unordered_map<uint64_t, Program> g_cache;

GLuint compile(GLenum type, const std::string& src) {
  GLuint s = glCreateShader(type);
  const char* p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
             std::fprintf(stderr, "[graphics-ff] shader compile failed: %s\n", log); }
  return s;
}

// Assemble the GLSL from the state flags. D3DCOLOR arrives as [B,G,R,A] bytes
// (both diffuse attribute and A8R8G8B8 texel), so every color source is read
// back with a .bgra swizzle to recover RGBA. Matrices are D3D row-major uploaded
// as-is (GL reads the transpose), hence proj*view*world — correct for D3D.
Program build(bool hasDiffuse, bool hasTex, uint32_t colorOp) {
  std::string vs = "#version 300 es\n"
    "layout(location=0) in vec3 aPos;\n";
  if (hasDiffuse) vs += "layout(location=1) in vec4 aColor;\n";
  if (hasTex)     vs += "layout(location=2) in vec2 aUV;\n";
  vs += "uniform mat4 uWorld, uView, uProj;\n";
  if (hasDiffuse) vs += "out vec4 vColor;\n";
  if (hasTex)     vs += "out vec2 vUV;\n";
  vs += "void main(){ gl_Position = uProj*uView*uWorld*vec4(aPos,1.0);\n";
  if (hasDiffuse) vs += "  vColor = aColor.bgra;\n";
  if (hasTex)     vs += "  vUV = aUV;\n";
  vs += "}\n";

  std::string fs = "#version 300 es\nprecision mediump float;\n";
  if (hasDiffuse) fs += "in vec4 vColor;\n";
  if (hasTex)     fs += "in vec2 vUV;\nuniform sampler2D uTex;\n";
  fs += "out vec4 frag;\nvoid main(){ frag = ";
  if (hasTex) {
    const char* tex = "texture(uTex, vUV).bgra";
    if (colorOp == D3DTOP_SELECTARG1)          fs += tex;                        // texture only
    else /* MODULATE (D3D stage-0 default) */  fs += hasDiffuse ? std::string("vColor * ") + tex : tex;
  } else {
    fs += hasDiffuse ? "vColor" : "vec4(1.0)";
  }
  fs += "; }\n";

  GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
  GLuint p = glCreateProgram();
  glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
  glDeleteShader(v); glDeleteShader(f);
  Program prog; prog.prog = p;
  prog.uWorld = glGetUniformLocation(p, "uWorld");
  prog.uView  = glGetUniformLocation(p, "uView");
  prog.uProj  = glGetUniformLocation(p, "uProj");
  prog.uTex   = glGetUniformLocation(p, "uTex");
  return prog;
}

} // namespace

const Program* program_for(uint32_t fvf, uint32_t colorOp) {
  const bool hasTex = fvf & D3DFVF_TEX1;
  if (!hasTex) colorOp = 0;   // op is irrelevant without a texture — collapse the key
  const uint64_t key = ((uint64_t)colorOp << 32) | fvf;

  auto it = g_cache.find(key);
  if (it != g_cache.end()) return &it->second;

  const bool hasDiffuse = fvf & D3DFVF_DIFFUSE;
  // ponytail: XYZ base plus optional DIFFUSE/TEX1 with MODULATE|SELECTARG1.
  // Widen the guard as new FVFs / combiners actually appear in a target game.
  const bool supported = (fvf & D3DFVF_XYZ) &&
      (!hasTex || colorOp == D3DTOP_MODULATE || colorOp == D3DTOP_SELECTARG1);
  if (!supported) {
    std::fprintf(stderr, "[graphics-ff] no program for FVF 0x%08x colorOp %u\n", fvf, colorOp);
    return nullptr;
  }
  auto r = g_cache.emplace(key, build(hasDiffuse, hasTex, colorOp));
  return &r.first->second;
}

} // namespace ff
