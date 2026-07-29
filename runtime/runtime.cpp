// SPDX-License-Identifier: GPL-3.0-only
// Contract runtime entry points (dx8wasm-specific surface beyond stock D3D8).
// A consuming game calls dx8wasm_init() once before Direct3DCreate8() to
// configure the backend and logging; the rest of its rendering uses the stock
// D3D8 API. See runtime/include/dx8wasm/contract.h.
#include "dx8wasm/contract.h"
#include "coverage/coverage.h"

namespace {
dx8wasm_init_desc g_desc{};
bool g_inited = false;
}

// Internal accessor for the platform layer (canvas selector, srgb request).
namespace runtime {
const dx8wasm_init_desc* config() { return g_inited ? &g_desc : nullptr; }
}

extern "C" {

int dx8wasm_init(const dx8wasm_init_desc* desc) {
  if (!desc || desc->abi_version != DX8WASM_ABI_VERSION) return -1;
  g_desc = *desc;
  g_inited = true;
  coverage::set_logging(desc->log_unimplemented != 0);
  return 0;
}

void dx8wasm_shutdown(void) { g_inited = false; }

// The WebGL2 fixed-function backend implements the FF pipeline (see
// docs/CONFORMANCE.md); the introspection caps below are all advanced features
// not yet covered. A porter checks these before relying on them.
int dx8wasm_has_cap(dx8wasm_cap cap) {
  switch (cap) {
    // Stencil is real: SetRenderState stores the D3DRS_STENCIL* group and apply_raster_masks
    // programs glStencilFunc/Op per draw. Denying it sends a porter around a working feature.
    case DX8WASM_CAP_STENCIL: return 1;
    // Still absent: BC/cube/volume textures, vertex-blend, point sprites, SM1.x.
    default: return 0;
  }
}

}
