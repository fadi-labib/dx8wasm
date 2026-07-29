// SPDX-License-Identifier: GPL-3.0-only
// dx8wasm — integration contract
//
// The ABI a consuming game targets. A DX8-era game keeps calling the standard
// Win32/D3D8 entry points; dx8wasm provides them. This header declares only the
// *dx8wasm-specific* surface a port needs beyond the stock D3D8/Win32 API:
// runtime init/config, capability introspection, and the backend selector.
#ifndef DX8WASM_CONTRACT_H
#define DX8WASM_CONTRACT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DX8WASM_ABI_VERSION 1

// --- Backend selection -------------------------------------------------------
typedef enum {
    DX8WASM_BACKEND_AUTO   = 0,  // WebGL2 today; may pick WebGPU when mature
    DX8WASM_BACKEND_WEBGL2 = 1,  // max reach (default)
    DX8WASM_BACKEND_WEBGPU = 2,  // opt-in, future
} dx8wasm_backend;

// --- Runtime init ------------------------------------------------------------
// Call once before Direct3DCreate8(). `canvas_selector` is the DOM selector of
// the target <canvas> (e.g. "#canvas"). Returns 0 on success.
typedef struct {
    uint32_t        abi_version;      // set to DX8WASM_ABI_VERSION
    dx8wasm_backend backend;
    const char*     canvas_selector;
    int             srgb;             // request an sRGB default framebuffer
    int             log_unimplemented;// 1 = loudly log unhandled D3D8 state/ops
} dx8wasm_init_desc;

int  dx8wasm_init(const dx8wasm_init_desc* desc);
void dx8wasm_shutdown(void);

// --- Capability / coverage introspection ------------------------------------
// The reference translation implements the subset one game used. A NEW game
// must be able to discover what is and isn't covered so gaps surface as data,
// not as silently-wrong pixels (SPEC §6.2).
typedef enum {
    DX8WASM_CAP_TEXTURE_BC       = 1,  // BC/DXT sampling (platform-gated on the web)
    DX8WASM_CAP_CUBE_TEXTURE     = 2,
    DX8WASM_CAP_VOLUME_TEXTURE   = 3,
    DX8WASM_CAP_VERTEX_BLEND     = 4,  // fixed-function skinning
    DX8WASM_CAP_POINT_SPRITES    = 5,  // emulated via instanced quads
    DX8WASM_CAP_SHADER_SM1X      = 6,  // .vso/.pso translation
    DX8WASM_CAP_STENCIL          = 7,
} dx8wasm_cap;

int dx8wasm_has_cap(dx8wasm_cap cap);   // 1 = supported by the active backend

// Coverage counters: how many D3D8 calls hit an unimplemented path this run.
// A porter watches these to know what to fill in next.
typedef struct {
    uint32_t unhandled_render_states;
    uint32_t unhandled_texture_stage_ops;
    uint32_t unhandled_formats;
    uint32_t fallbacks_taken;
    // Appended (keeps existing offsets): Set*TextureStageState tokens with no implementation.
    // Without this the stage-state path could drop tokens with no trace, while the render-state
    // path reported its gaps — so the conformance matrix under-reported what was missing.
    uint32_t unhandled_texture_stage_states;
} dx8wasm_coverage;

void dx8wasm_get_coverage(dx8wasm_coverage* out);

// Optional callback: fired the first time each distinct unhandled item is seen.
// `kind` is a stable string ("D3DRS_...", "D3DTOP_...", "D3DFMT_..."), `value`
// the numeric token. Use it to drive the conformance matrix (SPEC §Phase 3).
typedef void (*dx8wasm_unhandled_cb)(const char* kind, uint32_t value, void* user);
void dx8wasm_set_unhandled_callback(dx8wasm_unhandled_cb cb, void* user);

// --- Input -------------------------------------------------------------------
// Raw input state filled by dx8wasm_pump(). A game's input layer maps this onto
// its own handling (there is no Win32 message pump — this is the seam instead).
typedef struct {
    uint8_t keys[256];        // [SDL scancode] != 0 while held
    int32_t mouse_x, mouse_y; // canvas-relative pixel position
    uint8_t mouse_buttons;    // bit0 = left, bit1 = right, bit2 = middle
    int32_t wheel;            // wheel delta accumulated since the previous pump
    int     quit;             // window/tab close requested
} dx8wasm_input;

// Drain pending window/input events and fill `out` with the current state. Call
// once per frame (typically at the top of the render loop). `out` may be NULL to
// just service events.
void dx8wasm_pump(dx8wasm_input* out);

#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // DX8WASM_CONTRACT_H
