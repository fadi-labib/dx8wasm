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

// The GLSL comparison operator that emulates each D3DCMPFUNC alpha test. The
// shader discards when the test *fails*, i.e. when `!(alpha OP ref)` holds.
const char* alpha_cmp(uint32_t func) {
  switch (func) {
    case D3DCMP_LESS:         return "<";
    case D3DCMP_LESSEQUAL:    return "<=";
    case D3DCMP_GREATER:      return ">";
    case D3DCMP_GREATEREQUAL: return ">=";
    case D3DCMP_EQUAL:        return "==";
    case D3DCMP_NOTEQUAL:     return "!=";
    default:                  return nullptr;   // NEVER/ALWAYS handled by caller
  }
}

// Assemble the GLSL from the state flags. D3DCOLOR arrives as [B,G,R,A] bytes
// (both diffuse attribute and A8R8G8B8 texel), so every color source is read
// back with a .bgra swizzle to recover RGBA. Matrices are D3D row-major uploaded
// as-is (GL reads the transpose), hence proj*view*world — correct for D3D.
Program build(bool hasDiffuse, bool hasTex, uint32_t colorOp, uint32_t alphaFunc, bool lit) {
  const bool outColor = lit || hasDiffuse;   // vertex emits an interpolated color
  std::string vs = "#version 300 es\n"
    "layout(location=0) in vec3 aPos;\n";
  if (hasDiffuse) vs += "layout(location=1) in vec4 aColor;\n";
  if (hasTex)     vs += "layout(location=2) in vec2 aUV;\n";
  if (lit)        vs += "layout(location=3) in vec3 aNormal;\n";
  vs += "uniform mat4 uWorld, uView, uProj;\n";
  if (lit) vs +=
    "uniform vec3 uLightDir;\n"
    "uniform vec4 uLightDiffuse, uLightAmbient, uGlobalAmbient;\n"
    "uniform vec4 uMatDiffuse, uMatAmbient, uMatEmissive;\n";
  if (outColor) vs += "out vec4 vColor;\n";
  if (hasTex)   vs += "out vec2 vUV;\n";
  vs += "void main(){ gl_Position = uProj*uView*uWorld*vec4(aPos,1.0);\n";
  if (lit) vs +=
    // D3D fixed-function lighting is per-vertex (Gouraud). Single directional
    // light, atten 1. ponytail: object-space normal (identity world so far); a
    // proper inverse-transpose normal matrix lands when non-identity world is used.
    "  vec3 N = normalize(aNormal);\n"
    "  float ndl = clamp(dot(N, uLightDir), 0.0, 1.0);\n"
    "  vec4 c = uMatEmissive + uMatAmbient*uGlobalAmbient + uMatAmbient*uLightAmbient + uMatDiffuse*uLightDiffuse*ndl;\n"
    "  c.a = uMatDiffuse.a;\n"
    "  vColor = clamp(c, 0.0, 1.0);\n";
  else if (hasDiffuse) vs += "  vColor = aColor.bgra;\n";
  if (hasTex) vs += "  vUV = aUV;\n";
  vs += "}\n";

  std::string color;
  if (lit) {
    color = "vColor";   // material/light already combined per-vertex
  } else if (hasTex) {
    const char* tex = "texture(uTex, vUV).bgra";
    if (colorOp == D3DTOP_SELECTARG1)          color = tex;                        // texture only
    else /* MODULATE (D3D stage-0 default) */  color = hasDiffuse ? std::string("vColor * ") + tex : tex;
  } else {
    color = hasDiffuse ? "vColor" : "vec4(1.0)";
  }

  std::string fs = "#version 300 es\nprecision mediump float;\n";
  if (outColor) fs += "in vec4 vColor;\n";
  if (hasTex)   fs += "in vec2 vUV;\nuniform sampler2D uTex;\n";
  const bool alphaTest = alphaFunc != 0 && alphaFunc != D3DCMP_ALWAYS;
  if (alphaTest) fs += "uniform float uAlphaRef;\n";
  fs += "out vec4 frag;\nvoid main(){ vec4 c = " + color + ";\n";
  if (alphaTest) {
    if (alphaFunc == D3DCMP_NEVER)      fs += "  discard;\n";
    else if (const char* op = alpha_cmp(alphaFunc)) fs += std::string("  if (!(c.a ") + op + " uAlphaRef)) discard;\n";
  }
  fs += "  frag = c; }\n";

  GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
  GLuint p = glCreateProgram();
  glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
  glDeleteShader(v); glDeleteShader(f);
  Program prog; prog.prog = p;
  prog.uWorld    = glGetUniformLocation(p, "uWorld");
  prog.uView     = glGetUniformLocation(p, "uView");
  prog.uProj     = glGetUniformLocation(p, "uProj");
  prog.uTex      = glGetUniformLocation(p, "uTex");
  prog.uAlphaRef = glGetUniformLocation(p, "uAlphaRef");
  prog.uLightDir      = glGetUniformLocation(p, "uLightDir");
  prog.uLightDiffuse  = glGetUniformLocation(p, "uLightDiffuse");
  prog.uLightAmbient  = glGetUniformLocation(p, "uLightAmbient");
  prog.uGlobalAmbient = glGetUniformLocation(p, "uGlobalAmbient");
  prog.uMatDiffuse    = glGetUniformLocation(p, "uMatDiffuse");
  prog.uMatAmbient    = glGetUniformLocation(p, "uMatAmbient");
  prog.uMatEmissive   = glGetUniformLocation(p, "uMatEmissive");
  return prog;
}

} // namespace

const Program* program_for(uint32_t fvf, uint32_t colorOp, uint32_t alphaFunc, bool lit) {
  const bool hasTex = fvf & D3DFVF_TEX1;
  if (!hasTex) colorOp = 0;   // op is irrelevant without a texture — collapse the key
  const uint64_t key = ((uint64_t)lit << 48) | ((uint64_t)alphaFunc << 40) |
                       ((uint64_t)colorOp << 20) | fvf;

  auto it = g_cache.find(key);
  if (it != g_cache.end()) return &it->second;

  const bool hasDiffuse = fvf & D3DFVF_DIFFUSE;
  // ponytail: XYZ base plus optional DIFFUSE/TEX1 with MODULATE|SELECTARG1, or a
  // lit variant (requires NORMAL, untextured for now). Widen as targets demand.
  const bool supported = (fvf & D3DFVF_XYZ) &&
      (!hasTex || colorOp == D3DTOP_MODULATE || colorOp == D3DTOP_SELECTARG1) &&
      (!lit || ((fvf & D3DFVF_NORMAL) && !hasTex));
  if (!supported) {
    std::fprintf(stderr, "[graphics-ff] no program for FVF 0x%08x colorOp %u lit %d\n", fvf, colorOp, (int)lit);
    return nullptr;
  }
  auto r = g_cache.emplace(key, build(hasDiffuse, hasTex, colorOp, alphaFunc, lit));
  return &r.first->second;
}

} // namespace ff
