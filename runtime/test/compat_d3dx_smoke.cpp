// SPDX-License-Identifier: GPL-3.0-only
// Validates compatlib Tier 3 D3DX math: translation transforms a point,
// row-vector multiply order (v·(T·S) = scale-after-translate), inverse
// (M·M⁻¹ = I), and FVF vertex sizing. Pure math; pass = [1,0,0,255].
#include "compatlib/d3dx8math.h"
#include <emscripten.h>
#include <cmath>

EM_JS(void, report_pixel, (int r, int g, int b, int a), { window.__gpu = { pixel: [r, g, b, a] }; });
EM_JS(void, report_error, (const char* m), { window.__gpu = { error: UTF8ToString(m) }; });

#define CHECK(c, msg) do { if (!(c)) { report_error(msg); return 1; } } while (0)
static bool eq(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
  D3DXVECTOR3 origin{0, 0, 0};
  D3DXVECTOR4 o;

  D3DXMATRIX T; D3DXMatrixTranslation(&T, 1, 2, 3);
  D3DXVec3Transform(&o, &origin, &T);
  CHECK(eq(o.x, 1) && eq(o.y, 2) && eq(o.z, 3) && eq(o.w, 1), "translation transform wrong");

  // v·(T·S): translate to (1,2,3), then scale by 2 -> (2,4,6).
  D3DXMATRIX S, M; D3DXMatrixScaling(&S, 2, 2, 2); D3DXMatrixMultiply(&M, &T, &S);
  D3DXVec3Transform(&o, &origin, &M);
  CHECK(eq(o.x, 2) && eq(o.y, 4) && eq(o.z, 6), "multiply order / composition wrong");

  // M · M⁻¹ = I.
  D3DXMATRIX Inv, I;
  CHECK(D3DXMatrixInverse(&Inv, nullptr, &M) != nullptr, "inverse returned null");
  D3DXMatrixMultiply(&I, &M, &Inv);
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      CHECK(eq(I.m[i][j], i == j ? 1.0f : 0.0f), "M * inv(M) is not identity");

  // FVF sizing.
  CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1) == 24, "FVF size XYZ|DIFFUSE|TEX1 != 24");
  CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == 20, "FVF size XYZRHW|DIFFUSE != 20");
  CHECK(D3DXGetFVFVertexSize(D3DFVF_XYZ | D3DFVF_NORMAL) == 24, "FVF size XYZ|NORMAL != 24");

  report_pixel(1, 0, 0, 255);
  return 0;
}
