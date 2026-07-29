// SPDX-License-Identifier: GPL-3.0-only
// Which D3DFORMATs this backend can actually carry. Shared deliberately: the factory's
// CheckDeviceFormat must answer from the SAME predicate the texture path enforces, or the
// two drift and the engine picks a format that only fails much later, at upload time.
#ifndef DX8WASM_FORMAT_SUPPORT_H
#define DX8WASM_FORMAT_SUPPORT_H
#include "d3d8/d3d8.h"

namespace dxt {
inline bool is_dxt(D3DFORMAT f) { return f == D3DFMT_DXT1 || f == D3DFMT_DXT3 || f == D3DFMT_DXT5; }
}   // namespace dxt

namespace texfmt {
// Uncompressed formats with a real upload path (see texfmt::prepare in device.cpp).
inline bool supported(D3DFORMAT f) {
  switch (f) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_R8G8B8:
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4: case D3DFMT_A8L8:
    case D3DFMT_A8: case D3DFMT_L8: return true;
    default: return false;
  }
}
}   // namespace texfmt
#endif
