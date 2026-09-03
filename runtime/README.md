# runtime/
The WASM runtime layers. See `../docs/ARCHITECTURE.md` (layer map, data flow) and
`../SPEC.md` §5 (the original design).
- `d3d8webgl/`   D3D8 COM device on WebGL2: an independent reimplementation, no code from any game
- `graphics-ff/` fixed-function state → cached GLSL program (DXVK-modeled)
- `coverage/`, `telemetry/`  the "flag + fall back + count" layer and its NDJSON sink
- `compatlib/`   Win32 → POSIX/Emscripten shims, tiered (`../docs/COMPATLIB.md`)
- `platform/`    SDL3 window/context/input seam plus the OPFS bridge; audio stays game-side
- `include/dx8wasm/`  the public ABI: `contract.h` (init, input, coverage), `opfs.h`, `telemetry.h`
- `test/`        one headless smoke per feature, run by `../web-runtime/test/phase2.gpu.test.mjs`
- `demo/`        `spin_demo.cpp`, the pipeline demo behind `scripts/demo.mjs`
