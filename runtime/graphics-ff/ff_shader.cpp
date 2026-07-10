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
Program build(bool hasDiffuse, bool hasTex, uint32_t colorOp, uint32_t alphaFunc, bool lit, bool fog) {
  const bool outColor = lit || hasDiffuse;   // vertex emits an interpolated color
  std::string vs = "#version 300 es\n"
    "layout(location=0) in vec3 aPos;\n";
  if (hasDiffuse) vs += "layout(location=1) in vec4 aColor;\n";
  if (hasTex)     vs += "layout(location=2) in vec2 aUV;\n";
  if (lit)        vs += "layout(location=3) in vec3 aNormal;\n";
  vs += "uniform mat4 uWorld, uView, uProj;\n";
  if (lit) vs +=
    "const int MAXL = " + std::to_string(MAX_LIGHTS) + ";\n"
    "uniform int uLightCount;\n"
    "uniform int uLightType[MAXL];\n"          // 0 = directional, 1 = point, 2 = spot
    "uniform vec3 uLightDir[MAXL];\n"          // directional: normalize(-Direction)
    "uniform vec3 uLightPos[MAXL];\n"          // point/spot: world-space position
    "uniform vec3 uLightAtten[MAXL];\n"        // (a0, a1, a2)
    "uniform float uLightRange[MAXL];\n"
    "uniform vec3 uSpotDir[MAXL];\n"           // spot: normalize(Direction) (aim)
    "uniform vec3 uSpotParams[MAXL];\n"        // spot: (cosHalfTheta, cosHalfPhi, falloff)
    "uniform vec4 uLightDiffuse[MAXL];\n"
    "uniform vec4 uLightAmbient[MAXL];\n"
    "uniform vec4 uLightSpecular[MAXL];\n"
    "uniform vec4 uGlobalAmbient, uMatDiffuse, uMatAmbient, uMatEmissive, uMatSpecular;\n"
    "uniform int uSpecularEnable;\n"
    "uniform float uMatPower;\n";
  if (fog) vs += "uniform float uFogStart, uFogEnd;\nout float vFog;\n";
  if (outColor) vs += "out vec4 vColor;\n";
  if (hasTex)   vs += "out vec2 vUV;\n";
  vs += "void main(){ gl_Position = uProj*uView*uWorld*vec4(aPos,1.0);\n";
  if (fog) vs +=
    // Linear fog on eye-space depth: 1 = no fog (near), 0 = full fog (far).
    "  float fz = (uView*uWorld*vec4(aPos,1.0)).z;\n"
    "  vFog = clamp((uFogEnd - fz) / (uFogEnd - uFogStart), 0.0, 1.0);\n";
  if (lit) vs +=
    // D3D fixed-function lighting is per-vertex (Gouraud), accumulated over the
    // enabled lights. Directional: hitDir = -Direction, atten 1. Point: hitDir
    // toward the light, atten = 1/(a0+a1·d+a2·d²) zeroed past Range (per DXVK).
    // ponytail: object-space normal (identity/rigid world so far); an
    // inverse-transpose normal matrix lands when non-uniform-scale world is used.
    "  vec3 N = normalize(aNormal);\n"
    "  vec3 worldPos = (uWorld * vec4(aPos, 1.0)).xyz;\n"
    "  vec4 dsum = vec4(0.0), asum = vec4(0.0), ssum = vec4(0.0);\n"
    "  for (int i = 0; i < uLightCount; i++) {\n"
    "    vec3 hitDir; float atten;\n"
    "    if (uLightType[i] == 0) { hitDir = uLightDir[i]; atten = 1.0; }\n"
    "    else {\n"                                 // point or spot
    "      vec3 d = uLightPos[i] - worldPos; float dist = length(d);\n"
    "      hitDir = dist > 1e-6 ? d / dist : vec3(0.0, 0.0, 1.0);\n"
    "      float a = uLightAtten[i].x + uLightAtten[i].y*dist + uLightAtten[i].z*dist*dist;\n"
    "      atten = a > 0.0 ? 1.0 / a : 1.0;\n"
    "      if (dist > uLightRange[i]) atten = 0.0;\n"
    "      if (uLightType[i] == 2) {\n"            // spot cone falloff (theta inner, phi outer)
    "        float rho = dot(-hitDir, uSpotDir[i]);\n"
    "        float ct = uSpotParams[i].x, cp = uSpotParams[i].y, fo = uSpotParams[i].z;\n"
    "        float spot = rho <= cp ? 0.0 : (rho >= ct ? 1.0 : pow((rho - cp) / (ct - cp), fo));\n"
    "        atten *= spot;\n"
    "      }\n"
    "    }\n"
    "    float nl = dot(N, hitDir);\n"
    "    dsum += uLightDiffuse[i] * (clamp(nl, 0.0, 1.0) * atten);\n"
    "    asum += uLightAmbient[i] * atten;\n"
    "    if (uSpecularEnable != 0 && nl > 0.0) {\n"       // Blinn half-vector, infinite viewer V=+Z
    "      vec3 H = normalize(hitDir + vec3(0.0, 0.0, 1.0));\n"
    "      ssum += uLightSpecular[i] * (pow(max(dot(N, H), 0.0), uMatPower) * atten);\n"
    "    }\n"
    "  }\n"
    "  vec4 c = uMatEmissive + uMatAmbient*(uGlobalAmbient + asum) + uMatDiffuse*dsum;\n"
    "  c.rgb += (uMatSpecular * ssum).rgb;\n"
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
  if (fog)      fs += "in float vFog;\nuniform vec3 uFogColor;\n";
  const bool alphaTest = alphaFunc != 0 && alphaFunc != D3DCMP_ALWAYS;
  if (alphaTest) fs += "uniform float uAlphaRef;\n";
  fs += "out vec4 frag;\nvoid main(){ vec4 c = " + color + ";\n";
  if (alphaTest) {
    if (alphaFunc == D3DCMP_NEVER)      fs += "  discard;\n";
    else if (const char* op = alpha_cmp(alphaFunc)) fs += std::string("  if (!(c.a ") + op + " uAlphaRef)) discard;\n";
  }
  if (fog) fs += "  c.rgb = mix(uFogColor, c.rgb, vFog);\n";   // fog blends colour, leaves alpha
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
  prog.uLightCount    = glGetUniformLocation(p, "uLightCount");
  prog.uLightType     = glGetUniformLocation(p, "uLightType[0]");
  prog.uLightDir      = glGetUniformLocation(p, "uLightDir[0]");
  prog.uLightPos      = glGetUniformLocation(p, "uLightPos[0]");
  prog.uLightAtten    = glGetUniformLocation(p, "uLightAtten[0]");
  prog.uLightRange    = glGetUniformLocation(p, "uLightRange[0]");
  prog.uSpotDir       = glGetUniformLocation(p, "uSpotDir[0]");
  prog.uSpotParams    = glGetUniformLocation(p, "uSpotParams[0]");
  prog.uLightDiffuse  = glGetUniformLocation(p, "uLightDiffuse[0]");
  prog.uLightAmbient  = glGetUniformLocation(p, "uLightAmbient[0]");
  prog.uLightSpecular = glGetUniformLocation(p, "uLightSpecular[0]");
  prog.uSpecularEnable = glGetUniformLocation(p, "uSpecularEnable");
  prog.uMatSpecular   = glGetUniformLocation(p, "uMatSpecular");
  prog.uMatPower      = glGetUniformLocation(p, "uMatPower");
  prog.uGlobalAmbient = glGetUniformLocation(p, "uGlobalAmbient");
  prog.uMatDiffuse    = glGetUniformLocation(p, "uMatDiffuse");
  prog.uMatAmbient    = glGetUniformLocation(p, "uMatAmbient");
  prog.uMatEmissive   = glGetUniformLocation(p, "uMatEmissive");
  prog.uFogColor      = glGetUniformLocation(p, "uFogColor");
  prog.uFogStart      = glGetUniformLocation(p, "uFogStart");
  prog.uFogEnd        = glGetUniformLocation(p, "uFogEnd");
  return prog;
}

} // namespace

const Program* program_for(uint32_t fvf, uint32_t colorOp, uint32_t alphaFunc, bool lit, bool fog) {
  const bool hasTex = fvf & D3DFVF_TEX1;
  if (!hasTex) colorOp = 0;   // op is irrelevant without a texture — collapse the key
  const uint64_t key = ((uint64_t)fog << 49) | ((uint64_t)lit << 48) | ((uint64_t)alphaFunc << 40) |
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
  auto r = g_cache.emplace(key, build(hasDiffuse, hasTex, colorOp, alphaFunc, lit, fog));
  return &r.first->second;
}

} // namespace ff
