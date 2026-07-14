// SPDX-License-Identifier: GPL-3.0-only
// graphics-ff: generate + cache a fixed-function GLSL program from the render
// state key. 2.3 covered XYZ + DIFFUSE; 2.4 added a TEX1 sampler + one combiner.
// The FF combiner now spans TWO texture stages (the D3D8 multitexture path the
// Generals terrain relies on): a full D3DTOP combiner chain, per-stage
// D3DTSS_TEXCOORDINDEX (uv-set select + camera-space texgen), and per-stage
// texture matrices (D3DTS_TEXTURE0/1).
#ifndef DX8WASM_FF_SHADER_H
#define DX8WASM_FF_SHADER_H
#include <cstdint>
#include <GLES3/gl3.h>
#include "d3d8/d3d8.h"   // D3DTOP_/D3DTA_ constants used in the stage defaults below

namespace ff {
constexpr int MAX_LIGHTS = 8;   // shared cap between the shader loop and the device

// Per-stage combiner + texcoord state, mirroring the D3DTSS_* the device tracks.
// Args keep their full value: the low nibble is the source (DIFFUSE/CURRENT/
// TEXTURE/TFACTOR/SPECULAR) and bits 0x10/0x20 are the COMPLEMENT/ALPHAREPLICATE
// modifiers. `hasTex` is whether a texture is actually bound at the stage — an op
// sourcing TEXTURE with no texture collapses (else it samples an unbound unit).
struct Stage {
  uint32_t colorOp = D3DTOP_DISABLE, colorArg1 = D3DTA_TEXTURE, colorArg2 = D3DTA_CURRENT;
  uint32_t alphaOp = D3DTOP_DISABLE, alphaArg1 = D3DTA_TEXTURE, alphaArg2 = D3DTA_CURRENT;
  uint32_t tci = 0;      // low bits of D3DTSS_TEXCOORDINDEX: which vertex uv set feeds the stage
  uint32_t texgen = 0;   // 0 none, 1 camera-space position (2/3 reserved: reflection/normal)
  bool xform = false;    // apply the stage texture matrix (D3DTSS_TEXTURETRANSFORMFLAGS set)
  bool hasTex = false;   // a texture object is bound at this stage
};

// The full state a program is specialized on. `alphaFunc == 0` means no alpha
// test, else it is a D3DCMPFUNC emulated via discard. `lit` gates per-vertex
// Gouraud lighting (needs NORMAL, non-rhw). `fog` adds a linear-fog blend.
struct Key {
  uint32_t fvf = 0;
  uint32_t alphaFunc = 0;
  bool lit = false, fog = false;
  // Fixed-function material color sources: when set, the lit path reads that
  // component from the vertex diffuse (D3DMCS_COLOR1) instead of the material uniform.
  bool diffFromVertex = false, ambFromVertex = false, emisFromVertex = false;
  Stage stage[2];
};

struct Program {
  GLuint prog = 0;
  GLint uWorld = -1, uView = -1, uProj = -1, uTex = -1, uTex1 = -1, uAlphaRef = -1;
  GLint uTexMat0 = -1, uTexMat1 = -1, uTFactor = -1;   // stage texture matrices; D3DRS_TEXTUREFACTOR
  // Fixed-function lighting uniforms (valid only on lit programs). The uLight*
  // handles are the base of MAX_LIGHTS-sized arrays; uLightCount bounds the loop.
  GLint uLightCount = -1, uLightType = -1, uLightDir = -1, uLightDiffuse = -1, uLightAmbient = -1;
  GLint uLightPos = -1, uLightAtten = -1, uLightRange = -1, uLightSpecular = -1;
  GLint uSpotDir = -1, uSpotParams = -1;   // spot: aim dir; (cosHalfTheta, cosHalfPhi, falloff)
  GLint uSpecularEnable = -1, uMatSpecular = -1, uMatPower = -1;   // Blinn specular (uniform-gated)
  GLint uGlobalAmbient = -1, uMatDiffuse = -1, uMatAmbient = -1, uMatEmissive = -1;
  // Linear fog uniforms (valid only on fog programs).
  GLint uFogColor = -1, uFogStart = -1, uFogEnd = -1;
  GLint uViewport = -1;   // pre-transformed (XYZRHW): screen -> clip mapping
  // Bind-time hints recovered from the key.
  int stagesUsed = 0;     // number of enabled stages the fragment combiner chains (0..2)
};

// Cached by the full Key (FNV-1a over fvf + lighting/fog/alpha + both stages'
// combiner/tci/texgen/xform). Returns nullptr and logs loudly on an unsupported
// combo — never silently wrong. The single-stage defaults (stage 1 DISABLE, tci
// 0, no texgen/xform) reduce to the pre-2-stage behavior bit-for-bit.
const Program* program_for(const Key& key);
}
#endif
