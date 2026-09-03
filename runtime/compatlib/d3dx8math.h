// SPDX-License-Identifier: GPL-3.0-only
// compatlib Tier 3 — the D3DX helper math a DX8 game leans on. Written independently from
// the documented D3DX semantics: matrices are row-major and applied as row
// vectors (v' = v·M), matching d3d8.h's D3DMATRIX and dx8wasm's transform path.
#ifndef DX8WASM_D3DX8MATH_H
#define DX8WASM_D3DX8MATH_H
#include "d3d8/d3d8.h"

struct D3DXVECTOR3 { float x, y, z; };
struct D3DXVECTOR4 { float x, y, z, w; };
typedef D3DMATRIX D3DXMATRIX;

#ifdef __cplusplus
extern "C" {
#endif

D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX* out);
D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX* out, const D3DXMATRIX* a, const D3DXMATRIX* b);
D3DXMATRIX* D3DXMatrixTranspose(D3DXMATRIX* out, const D3DXMATRIX* in);
D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX* out, float* determinant, const D3DXMATRIX* in);  // null if singular
D3DXMATRIX* D3DXMatrixTranslation(D3DXMATRIX* out, float x, float y, float z);
D3DXMATRIX* D3DXMatrixScaling(D3DXMATRIX* out, float sx, float sy, float sz);
D3DXMATRIX* D3DXMatrixRotationZ(D3DXMATRIX* out, float angle);

D3DXVECTOR4* D3DXVec3Transform(D3DXVECTOR4* out, const D3DXVECTOR3* v, const D3DXMATRIX* m);
D3DXVECTOR4* D3DXVec4Transform(D3DXVECTOR4* out, const D3DXVECTOR4* v, const D3DXMATRIX* m);

UINT D3DXGetFVFVertexSize(DWORD fvf);

#ifdef __cplusplus
}
#endif
#endif  // DX8WASM_D3DX8MATH_H
