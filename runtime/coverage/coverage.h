// SPDX-License-Identifier: GPL-3.0-only
// Internal coverage sink. The device calls these when it meets a D3D8 token it
// doesn't implement: each bumps the matching contract counter (+ fallbacks) and
// fires the unhandled callback once per distinct token. The public read side is
// dx8wasm_get_coverage / dx8wasm_set_unhandled_callback (see contract.h).
#ifndef DX8WASM_COVERAGE_H
#define DX8WASM_COVERAGE_H
#include <cstdint>
namespace coverage {
void unhandled_render_state(uint32_t state);
void unhandled_texture_op(uint32_t op);
void unhandled_format(uint32_t fmt);
void set_logging(bool on);   // dx8wasm_init(log_unimplemented) gates the stderr logging
}
#endif
