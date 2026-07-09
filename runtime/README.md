# runtime/
The WASM runtime layers. See ../SPEC.md §5.
- `d3d8webgl/`  D3D8 COM → WebGL2 (extract, Phase 2)
- `compatlib/`  Win32 → POSIX shims (extract, Phase 2)
- `platform/`   SDL3 window/input + OpenAL (new, Phase 2)
- `graphics-ff/` fixed-function → shader gen (DXVK-modeled, Phase 3)
- `include/dx8wasm/contract.h`  integration ABI (present)
