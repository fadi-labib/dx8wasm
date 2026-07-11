// SPDX-License-Identifier: GPL-3.0-only
#include "compatlib/d3dx8math.h"
#include <cmath>

extern "C" {

D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX* o) {
  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) o->m[i][j] = (i == j) ? 1.0f : 0.0f;
  return o;
}

D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX* o, const D3DXMATRIX* a, const D3DXMATRIX* b) {
  D3DXMATRIX r;   // temp so out may alias a or b
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) {
      float s = 0;
      for (int k = 0; k < 4; k++) s += a->m[i][k] * b->m[k][j];
      r.m[i][j] = s;
    }
  *o = r;
  return o;
}

D3DXMATRIX* D3DXMatrixTranspose(D3DXMATRIX* o, const D3DXMATRIX* in) {
  D3DXMATRIX r;
  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) r.m[i][j] = in->m[j][i];
  *o = r;
  return o;
}

D3DXMATRIX* D3DXMatrixTranslation(D3DXMATRIX* o, float x, float y, float z) {
  D3DXMatrixIdentity(o);
  o->m[3][0] = x; o->m[3][1] = y; o->m[3][2] = z;   // row-vector convention: translation in row 3
  return o;
}
D3DXMATRIX* D3DXMatrixScaling(D3DXMATRIX* o, float sx, float sy, float sz) {
  D3DXMatrixIdentity(o);
  o->m[0][0] = sx; o->m[1][1] = sy; o->m[2][2] = sz;
  return o;
}
D3DXMATRIX* D3DXMatrixRotationZ(D3DXMATRIX* o, float angle) {
  D3DXMatrixIdentity(o);
  float c = std::cos(angle), s = std::sin(angle);
  o->m[0][0] = c; o->m[0][1] = s; o->m[1][0] = -s; o->m[1][1] = c;
  return o;
}

// General 4x4 inverse via the adjugate / determinant. Returns null if singular.
D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX* o, float* determinant, const D3DXMATRIX* in) {
  const float* m = &in->m[0][0];
  float inv[16];
  inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
  inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
  inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
  inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
  inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
  inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
  inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
  inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
  inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
  inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
  inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
  inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
  inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

  float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
  if (determinant) *determinant = det;
  if (det == 0.0f) return nullptr;
  float invDet = 1.0f / det;
  for (int i = 0; i < 16; i++) (&o->m[0][0])[i] = inv[i] * invDet;
  return o;
}

D3DXVECTOR4* D3DXVec3Transform(D3DXVECTOR4* o, const D3DXVECTOR3* v, const D3DXMATRIX* m) {
  o->x = v->x*m->m[0][0] + v->y*m->m[1][0] + v->z*m->m[2][0] + m->m[3][0];
  o->y = v->x*m->m[0][1] + v->y*m->m[1][1] + v->z*m->m[2][1] + m->m[3][1];
  o->z = v->x*m->m[0][2] + v->y*m->m[1][2] + v->z*m->m[2][2] + m->m[3][2];
  o->w = v->x*m->m[0][3] + v->y*m->m[1][3] + v->z*m->m[2][3] + m->m[3][3];
  return o;
}
D3DXVECTOR4* D3DXVec4Transform(D3DXVECTOR4* o, const D3DXVECTOR4* v, const D3DXMATRIX* m) {
  D3DXVECTOR4 r;
  r.x = v->x*m->m[0][0] + v->y*m->m[1][0] + v->z*m->m[2][0] + v->w*m->m[3][0];
  r.y = v->x*m->m[0][1] + v->y*m->m[1][1] + v->z*m->m[2][1] + v->w*m->m[3][1];
  r.z = v->x*m->m[0][2] + v->y*m->m[1][2] + v->z*m->m[2][2] + v->w*m->m[3][2];
  r.w = v->x*m->m[0][3] + v->y*m->m[1][3] + v->z*m->m[2][3] + v->w*m->m[3][3];
  *o = r;
  return o;
}

// Vertex stride from an FVF (position + optional normal/diffuse/specular + N
// 2-float texcoord sets; the texcoord-count lives in bits 8..15).
UINT D3DXGetFVFVertexSize(DWORD fvf) {
  UINT size = 0;
  if (fvf & D3DFVF_XYZRHW) size += 16; else if (fvf & D3DFVF_XYZ) size += 12;
  if (fvf & D3DFVF_NORMAL)  size += 12;
  if (fvf & D3DFVF_DIFFUSE) size += 4;
  if (fvf & 0x0080u /* D3DFVF_SPECULAR */) size += 4;
  UINT texCount = (fvf >> 8) & 0xf;   // D3DFVF_TEXn count
  size += texCount * 2 * sizeof(float);
  return size;
}

}
