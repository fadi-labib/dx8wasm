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
  if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log);
             std::fprintf(stderr, "[graphics-ff] shader compile failed: %s\n--- src ---\n%s\n", log, p); }
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

// One combiner argument. Low nibble selects the source (DIFFUSE reads the vertex/
// lit color, CURRENT the running accumulator, TEXTURE the stage texel, TFACTOR
// the D3DRS_TEXTUREFACTOR, SPECULAR the vertex specular — 0 here, dx8wasm folds
// specular into the lit color). Modifiers: ALPHAREPLICATE (.aaaa) then COMPLEMENT
// (1-x). The Generals road-noise pass leans on DIFFUSE|ALPHAREPLICATE to build white.
std::string combiner_arg(uint32_t arg, const std::string& texExpr) {
  std::string e;
  switch (arg & D3DTA_SELECTMASK) {
    case D3DTA_DIFFUSE:  e = "diffuse"; break;
    case D3DTA_CURRENT:  e = "cur"; break;
    case D3DTA_TEXTURE:  e = texExpr; break;
    case D3DTA_TFACTOR:  e = "uTFactor"; break;
    case D3DTA_SPECULAR: e = "spec"; break;
    default:             e = "diffuse"; break;
  }
  if (arg & D3DTA_ALPHAREPLICATE) e = "vec4(" + e + ".a)";
  if (arg & D3DTA_COMPLEMENT)     e = "(vec4(1.0) - " + e + ")";
  return e;
}

// One combiner op over the two prepared args. D3D saturates its results, so the
// scaling/adding ops clamp. texAlphaExpr is the stage texel's alpha (for
// BLENDTEXTUREALPHA). Unknown ops fall back to MODULATE (logged at bind time).
std::string combiner_op(uint32_t op, const std::string& a1, const std::string& a2,
                        const std::string& texAlphaExpr) {
  switch (op) {
    case D3DTOP_SELECTARG1:       return a1;
    case D3DTOP_SELECTARG2:       return a2;
    case D3DTOP_MODULATE:         return "(" + a1 + " * " + a2 + ")";
    case D3DTOP_MODULATE2X:       return "min((" + a1 + " * " + a2 + ") * 2.0, vec4(1.0))";
    case D3DTOP_MODULATE4X:       return "min((" + a1 + " * " + a2 + ") * 4.0, vec4(1.0))";
    case D3DTOP_ADD:              return "min(" + a1 + " + " + a2 + ", vec4(1.0))";
    case D3DTOP_ADDSIGNED:        return "clamp(" + a1 + " + " + a2 + " - 0.5, 0.0, 1.0)";
    case D3DTOP_ADDSIGNED2X:      return "clamp((" + a1 + " + " + a2 + " - 0.5) * 2.0, 0.0, 1.0)";
    case D3DTOP_SUBTRACT:         return "max(" + a1 + " - " + a2 + ", vec4(0.0))";
    case D3DTOP_ADDSMOOTH:        return "(" + a1 + " + " + a2 + " - " + a1 + " * " + a2 + ")";
    case D3DTOP_BLENDTEXTUREALPHA: return "mix(" + a2 + ", " + a1 + ", " + texAlphaExpr + ")";
    case D3DTOP_BLENDDIFFUSEALPHA: return "mix(" + a2 + ", " + a1 + ", diffuse.a)";
    case D3DTOP_BLENDCURRENTALPHA: return "mix(" + a2 + ", " + a1 + ", cur.a)";
    case D3DTOP_BLENDFACTORALPHA:  return "mix(" + a2 + ", " + a1 + ", uTFactor.a)";
    case D3DTOP_DOTPRODUCT3:
      return "vec4(vec3(clamp(dot(" + a1 + ".rgb - 0.5, " + a2 + ".rgb - 0.5) * 4.0, 0.0, 1.0)), 1.0)";
    default:                      return "(" + a1 + " * " + a2 + ")";
  }
}

// Assemble the GLSL from the full key. The vertex DIFFUSE attribute arrives as D3DCOLOR
// [B,G,R,A] bytes, so it's read back with a .bgra swizzle. Textures are converted to RGBA
// at upload (matching Leondore's d3d8webgl), so texels are sampled plain. uTFactor is
// uploaded already RGBA-ordered. Matrices are D3D row-major uploaded as-is (GL reads the
// transpose), hence proj*view*world (and texMat * uv) — correct for D3D.
Program build(const Key& k) {
  const uint32_t fvf = k.fvf;
  const bool rhw = fvf & D3DFVF_XYZRHW;         // pre-transformed screen-space vertices
  const bool hasDiffuse = fvf & D3DFVF_DIFFUSE;
  const bool lit = k.lit;
  const bool fog = k.fog;
  const bool outColor = lit || hasDiffuse;      // vertex emits an interpolated color
  const int texcount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
  const int texIn = texcount > 2 ? 2 : texcount;   // vertex uv sets we wire up (cap 2)

  int stagesUsed = 0;
  for (int s = 0; s < 2; s++) if (k.stage[s].colorOp != D3DTOP_DISABLE) stagesUsed = s + 1;
  const bool anyTex = stagesUsed > 0;
  bool anyTexgen = false, useMat[2] = {false, false};
  for (int s = 0; s < stagesUsed; s++) {
    if (k.stage[s].texgen && !rhw) anyTexgen = true;
    if ((k.stage[s].xform || (k.stage[s].texgen && !rhw))) useMat[s] = true;
  }

  // ---------------- vertex shader ----------------
  std::string vs = "#version 300 es\n";
  vs += rhw ? "layout(location=0) in vec4 aPos;\n"   // pre-transformed: x,y screen px; z depth; w=rhw
            : "layout(location=0) in vec3 aPos;\n";
  if (hasDiffuse)          vs += "layout(location=1) in vec4 aColor;\n";
  if (anyTex && texIn > 0) vs += "layout(location=2) in vec2 aUV0;\n";
  if (lit)                 vs += "layout(location=3) in vec3 aNormal;\n";
  if (anyTex && texIn > 1) vs += "layout(location=4) in vec2 aUV1;\n";
  if (rhw) vs += "uniform vec2 uViewport;\n";
  else     vs += "uniform mat4 uWorld, uView, uProj;\n";
  if (useMat[0]) vs += "uniform mat4 uTexMat0;\n";
  if (useMat[1]) vs += "uniform mat4 uTexMat1;\n";
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
  for (int s = 0; s < stagesUsed; s++) vs += "out vec2 vUV" + std::to_string(s) + ";\n";
  vs += "void main(){\n";
  if (rhw) vs +=
    // Pre-transformed: aPos is in screen pixels (D3D top-left origin), so map to
    // clip space and flip Y. ponytail: rhw (w) assumed 1 — perspective 2D deferred.
    "  gl_Position = vec4(aPos.x/uViewport.x*2.0 - 1.0, 1.0 - aPos.y/uViewport.y*2.0, aPos.z*2.0 - 1.0, 1.0);\n";
  else vs +=
    "  gl_Position = uProj*uView*uWorld*vec4(aPos,1.0);\n";
  if (anyTexgen)                                   // view-space position for camera-space texgen
    vs += "  vec3 viewPos = (uView*uWorld*vec4(aPos,1.0)).xyz;\n";
  if (fog) {
    // Linear fog: 1 = no fog (near), 0 = full fog (far). Pre-transformed vertices
    // use their supplied depth directly; transformed ones use eye-space depth.
    vs += rhw ? "  float fz = aPos.z;\n" : "  float fz = (uView*uWorld*vec4(aPos,1.0)).z;\n";
    vs += "  vFog = clamp((uFogEnd - fz) / (uFogEnd - uFogStart), 0.0, 1.0);\n";
  }
  if (lit) vs +=
    // D3D fixed-function lighting is per-vertex (Gouraud), accumulated over the
    // enabled lights. Directional: hitDir = -Direction, atten 1. Point: hitDir
    // toward the light, atten = 1/(a0+a1·d+a2·d²) zeroed past Range (per DXVK).
    // Normal must be in world space to match the world-space lights. mat3(uWorld)
    // is the same rotation/scale used for position (line below), so a rotated unit
    // lights correctly. ponytail: uses mat3(world), not the inverse-transpose — exact
    // for rotation + uniform scale (Generals' case); non-uniform scale would skew it.
    "  vec3 N = normalize(mat3(uWorld) * aNormal);\n"
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
    "        float spotf = rho <= cp ? 0.0 : (rho >= ct ? 1.0 : pow((rho - cp) / (ct - cp), fo));\n"
    "        atten *= spotf;\n"
    "      }\n"
    "    }\n"
    "    float nl = dot(N, hitDir);\n"
    "    dsum += uLightDiffuse[i] * (clamp(nl, 0.0, 1.0) * atten);\n"
    "    asum += uLightAmbient[i] * atten;\n"
    // Gate on power > 0: the engine enables SPECULARENABLE with material Power==0, where
    // pow(x,0)==1 would add full specular at every lit vertex and blow geometry out to
    // white (modulated into the texture). Leondore's d3d8webgl omits vertex specular here.
    "    if (uSpecularEnable != 0 && uMatPower > 0.0 && nl > 0.0) {\n"  // Blinn half-vector, infinite viewer V=+Z
    "      vec3 H = normalize(hitDir + vec3(0.0, 0.0, 1.0));\n"
    "      ssum += uLightSpecular[i] * (pow(max(dot(N, H), 0.0), uMatPower) * atten);\n"
    "    }\n"
    "  }\n";
  // Material color sources: D3DMCS_COLOR1 reads the component from the vertex diffuse
  // (aColor is [B,G,R,A], so .bgra/.bgr recovers RGB(A)); otherwise the material
  // uniform. This is the over-bright fix — Generals bakes scene lighting into the
  // vertex diffuse and leaves the material diffuse white, so without this every lit
  // surface multiplies by white and blows out.
  if (lit) {
    vs += k.diffFromVertex ? "  vec4 matDiff = aColor.bgra;\n" : "  vec4 matDiff = uMatDiffuse;\n";
    vs += k.ambFromVertex  ? "  vec3 matAmb = aColor.bgr;\n"   : "  vec3 matAmb = uMatAmbient.rgb;\n";
    vs += k.emisFromVertex ? "  vec3 matEmis = aColor.bgr;\n"  : "  vec3 matEmis = uMatEmissive.rgb;\n";
    vs +=
      "  vec4 c = vec4(matEmis + matAmb*(uGlobalAmbient.rgb + asum.rgb) + matDiff.rgb*dsum.rgb, matDiff.a);\n";
    vs += k.specFromVertex ? "  vec4 matSpec = aColor.bgra;\n" : "  vec4 matSpec = uMatSpecular;\n";
    vs +=
      "  c.rgb += (matSpec * ssum).rgb;\n"
      "  vColor = clamp(c, 0.0, 1.0);\n";
  }
  else if (hasDiffuse) vs += "  vColor = aColor.bgra;\n";
  // Per-stage texcoords: camera-space texgen, or the selected vertex uv set,
  // each optionally run through the stage texture matrix.
  for (int s = 0; s < stagesUsed; s++) {
    const std::string S = std::to_string(s);
    const Stage& st = k.stage[s];
    const int tci = (int)st.tci < texIn ? (int)st.tci : 0;
    const std::string mat = "uTexMat" + S;
    if (st.texgen && !rhw) {
      // D3DTSS_TCI_CAMERASPACEPOSITION: uv = texture matrix * view-space position.
      vs += "  vUV" + S + " = (" + mat + " * vec4(viewPos, 1.0)).xy;\n";
    } else if (texIn == 0) {
      vs += "  vUV" + S + " = vec2(0.0);\n";
    } else if (st.xform) {
      vs += "  vUV" + S + " = (" + mat + " * vec4(aUV" + std::to_string(tci) + ", 0.0, 1.0)).xy;\n";
    } else {
      vs += "  vUV" + S + " = aUV" + std::to_string(tci) + ";\n";
    }
  }
  vs += "}\n";

  // ---------------- fragment shader ----------------
  std::string fs = "#version 300 es\nprecision mediump float;\n";
  if (outColor) fs += "in vec4 vColor;\n";
  for (int s = 0; s < stagesUsed; s++) fs += "in vec2 vUV" + std::to_string(s) + ";\n";
  if (stagesUsed > 0 && k.stage[0].hasTex) fs += "uniform sampler2D uTex;\n";
  if (stagesUsed > 1 && k.stage[1].hasTex) fs += "uniform sampler2D uTex1;\n";
  if (anyTex) fs += "uniform vec4 uTFactor;\n";       // D3DRS_TEXTUREFACTOR (RGBA)
  if (fog)    fs += "in float vFog;\nuniform vec3 uFogColor;\n";
  const bool alphaTest = k.alphaFunc != 0 && k.alphaFunc != D3DCMP_ALWAYS;
  if (alphaTest) fs += "uniform float uAlphaRef;\n";
  fs += "out vec4 frag;\nvoid main(){\n";
  fs += std::string("  vec4 diffuse = ") + (outColor ? "vColor" : "vec4(1.0)") + ";\n";
  fs += "  vec4 spec = vec4(0.0);\n";
  fs += "  vec4 cur = diffuse;\n";
  // Chain the enabled stages: `cur` accumulates stage 0 then stage 1. rgb comes
  // from the color op, alpha from the alpha op (DISABLE keeps the prior alpha).
  for (int s = 0; s < stagesUsed; s++) {
    const Stage& st = k.stage[s];
    const std::string sampler = s == 0 ? "uTex" : "uTex1";
    const std::string texv = "tex" + std::to_string(s);
    if (st.hasTex)
      fs += "  vec4 " + texv + " = texture(" + sampler + ", vUV" + std::to_string(s) + ");\n";
    const std::string ta = texv + ".a";
    std::string colorExpr = combiner_op(st.colorOp,
        combiner_arg(st.colorArg1, texv), combiner_arg(st.colorArg2, texv), ta);
    std::string alphaExpr = st.alphaOp == D3DTOP_DISABLE ? std::string("cur")
        : combiner_op(st.alphaOp, combiner_arg(st.alphaArg1, texv), combiner_arg(st.alphaArg2, texv), ta);
    fs += "  cur = vec4((" + colorExpr + ").rgb, (" + alphaExpr + ").a);\n";
  }
  if (alphaTest) {
    if (k.alphaFunc == D3DCMP_NEVER)      fs += "  discard;\n";
    else if (const char* op = alpha_cmp(k.alphaFunc)) fs += std::string("  if (!(cur.a ") + op + " uAlphaRef)) discard;\n";
  }
  if (fog) fs += "  cur.rgb = mix(uFogColor, cur.rgb, vFog);\n";   // fog blends colour, leaves alpha
  fs += "  frag = cur; }\n";

  GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
  GLuint p = glCreateProgram();
  glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
  glDeleteShader(v); glDeleteShader(f);
  Program prog; prog.prog = p;
  prog.stagesUsed = stagesUsed;
  prog.uWorld    = glGetUniformLocation(p, "uWorld");
  prog.uView     = glGetUniformLocation(p, "uView");
  prog.uProj     = glGetUniformLocation(p, "uProj");
  prog.uTex      = glGetUniformLocation(p, "uTex");
  prog.uTex1     = glGetUniformLocation(p, "uTex1");
  prog.uTexMat0  = glGetUniformLocation(p, "uTexMat0");
  prog.uTexMat1  = glGetUniformLocation(p, "uTexMat1");
  prog.uTFactor  = glGetUniformLocation(p, "uTFactor");
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
  prog.uViewport      = glGetUniformLocation(p, "uViewport");
  return prog;
}

// FNV-1a over the state that changes the generated GLSL. Argument MODIFIER bits
// (COMPLEMENT/ALPHAREPLICATE) and per-stage tci/texgen/xform must all discriminate
// — different terrain passes must not reuse the wrong shader.
uint64_t hash_key(const Key& k) {
  uint64_t h = 0xcbf29ce484222325ull;
  auto put = [&](uint64_t v) { h ^= v + 0x9E37u; h *= 0x100000001b3ull; };
  put(k.fvf);
  put(k.alphaFunc);
  put((k.lit ? 1u : 0u) | (k.fog ? 2u : 0u)
      | (k.diffFromVertex ? 4u : 0u) | (k.ambFromVertex ? 8u : 0u) | (k.emisFromVertex ? 16u : 0u)
      | (k.specFromVertex ? 32u : 0u));
  for (int s = 0; s < 2; s++) {
    const Stage& st = k.stage[s];
    put(st.colorOp); put(st.colorArg1); put(st.colorArg2);
    put(st.alphaOp); put(st.alphaArg1); put(st.alphaArg2);
    put(st.tci); put(st.texgen); put((st.xform ? 1u : 0u) | (st.hasTex ? 2u : 0u));
  }
  return h;
}

} // namespace

const Program* program_for(const Key& k) {
  const bool rhw = k.fvf & D3DFVF_XYZRHW;   // pre-transformed screen-space vertices
  const bool supported = (k.fvf & D3DFVF_XYZ) || rhw;
  const bool ok = supported && (!k.lit || ((k.fvf & D3DFVF_NORMAL) && !rhw));
  if (!ok) {
    std::fprintf(stderr, "[graphics-ff] no program for FVF 0x%08x lit %d\n", k.fvf, (int)k.lit);
    return nullptr;
  }
  const uint64_t key = hash_key(k);
  auto it = g_cache.find(key);
  if (it != g_cache.end()) return &it->second;
  auto r = g_cache.emplace(key, build(k));
  return &r.first->second;
}

} // namespace ff
