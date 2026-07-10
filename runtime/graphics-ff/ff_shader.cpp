// SPDX-License-Identifier: GPL-3.0-only
#include "graphics-ff/ff_shader.h"
#include "d3d8/d3d8.h"
#include <cstdio>
#include <string>
#include <unordered_map>

namespace ff {
namespace {

std::unordered_map<uint32_t, Program> g_cache;

GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
             std::fprintf(stderr, "[graphics-ff] shader compile failed: %s\n", log); }
  return s;
}

// XYZ position + interpolated DIFFUSE. D3DCOLOR bytes arrive as [B,G,R,A], so
// swizzle .bgra to recover RGBA. Matrices are D3D row-major uploaded as-is
// (GL reads the transpose), hence the proj*view*world order — correct for D3D.
Program build_xyz_diffuse() {
  const char* vs =
    "#version 300 es\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec4 aColor;\n"
    "uniform mat4 uWorld, uView, uProj;\n"
    "out vec4 vColor;\n"
    "void main(){ gl_Position = uProj*uView*uWorld*vec4(aPos,1.0); vColor = aColor.bgra; }\n";
  const char* fs =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 vColor;\n"
    "out vec4 frag;\n"
    "void main(){ frag = vColor; }\n";
  GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
  GLuint p = glCreateProgram();
  glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
  glDeleteShader(v); glDeleteShader(f);
  Program prog; prog.prog = p;
  prog.uWorld = glGetUniformLocation(p, "uWorld");
  prog.uView  = glGetUniformLocation(p, "uView");
  prog.uProj  = glGetUniformLocation(p, "uProj");
  return prog;
}

} // namespace

const Program* program_for_fvf(uint32_t fvf) {
  auto it = g_cache.find(fvf);
  if (it != g_cache.end()) return &it->second;

  // ponytail: only the XYZ|DIFFUSE combo exists today; add variants as FVFs appear.
  if (fvf == (D3DFVF_XYZ | D3DFVF_DIFFUSE)) {
    auto r = g_cache.emplace(fvf, build_xyz_diffuse());
    return &r.first->second;
  }
  std::fprintf(stderr, "[graphics-ff] no fixed-function program for FVF 0x%08x\n", fvf);
  return nullptr;
}

} // namespace ff
