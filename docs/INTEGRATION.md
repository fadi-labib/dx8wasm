# Integrating a game with dx8wasm

How to connect a DirectX-8 game to dx8wasm and run it in the browser. The
working reference is [`examples/minigame/minigame.cpp`](../examples/minigame/minigame.cpp) —
a complete, tiny "game" that uses **only** the public surface. Read it alongside
this guide; every step below appears there.

## What dx8wasm gives you

- **The D3D8 API** — `Direct3DCreate8`, `IDirect3D8`, `IDirect3DDevice8`, buffers,
  textures, the fixed-function pipeline (transforms, all light types, fog,
  texture combiners, render states). Exactly what a D3D8 game calls. Current
  coverage is measured in [`CONFORMANCE.md`](CONFORMANCE.md).
- **A window + GL context** — created for you on the target `<canvas>` when the
  game calls `CreateDevice`. Backed by SDL3 → WebGL2.
- **The runtime contract** ([`contract.h`](../runtime/include/dx8wasm/contract.h)) —
  `dx8wasm_init` (config), `dx8wasm_pump` (input), and the coverage hooks that
  tell you which D3D8 features a run actually hit that aren't implemented yet.

## What dx8wasm does NOT give you (the game-side work)

1. **A Win32 compatibility layer.** The game's *non-graphics* code (file I/O,
   threads, timers, registry, `WinMain`, window messages) calls Win32. dx8wasm
   does not emulate Win32. Most of it maps to POSIX/emscripten directly; the
   rest you shim on demand (see "Win32 shims" below). This is the largest piece
   of integration work.
2. **A blocking main loop.** The browser can't block. The game's
   `while (running) { ... }` loop must become an `emscripten_set_main_loop`
   callback (one iteration per call). See "Main loop" below.
3. **Asset loading.** Use Phase 1's streaming loader (GAXD + OPFS, see
   `web-runtime/`) or the emscripten filesystem. The game's file reads then hit a
   virtual FS you populate.
4. **Audio.** Not yet wired. DirectSound/Miles → OpenAL is a future layer.

## The five integration steps

### 1. Build + link
Compile the game with `emcc`/`em++` and link the dx8wasm static libraries. In
CMake, against an Emscripten toolchain:

```cmake
add_subdirectory(path/to/dx8wasm)          # provides dx8_d3d8webgl (+ dx8_platform)
target_link_libraries(your_game PRIVATE dx8_d3d8webgl)
target_link_options(your_game PRIVATE
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sEXIT_RUNTIME=0 -g0
  --use-port=sdl3)
```
Include paths: `runtime/`, `runtime/d3d8/`, `runtime/include/`.

### 2. Point the game's D3D8 include at ours
The game includes `<d3d8.h>`; make that resolve to
[`runtime/d3d8/d3d8.h`](../runtime/d3d8/d3d8.h) (add it to the include path, or
alias it). It's a clean-room subset — if the game uses a method not declared
there, add the declaration and implement it in `runtime/d3d8webgl/device.cpp`
(the coverage counters and link errors tell you what's missing).

### 3. Configure the runtime once
Before `Direct3DCreate8`:

```c
dx8wasm_init_desc desc = {0};
desc.abi_version      = DX8WASM_ABI_VERSION;
desc.backend          = DX8WASM_BACKEND_WEBGL2;
desc.canvas_selector  = "#canvas";
desc.log_unimplemented = 1;            // log unhandled D3D8 state to the console
dx8wasm_init(&desc);
```

### 4. Convert the main loop
Replace the game's blocking loop with an emscripten callback. Each frame: pump
input, step the game, render.

```c
void one_frame(void) {
    dx8wasm_input in;
    dx8wasm_pump(&in);                 // keyboard/mouse/quit -> your input layer
    game_update(&in);                  // your simulation tick
    game_render();                     // your existing D3D8 draw calls
}
// after device creation:
emscripten_set_main_loop(one_frame, 0, 1);
```
`dx8wasm_pump` fills `keys[]` (SDL scancode indexed), mouse position/buttons,
wheel delta, and a `quit` flag. Map these onto whatever the game's input system
expects (it replaces the Win32 `WM_KEY*`/DirectInput path).

### 5. Wire assets, then iterate on coverage
Populate the virtual filesystem (Phase 1 loader or emscripten `--preload-file`),
then run. Register a coverage callback to see graphics gaps as they occur:

```c
void on_unhandled(const char* kind, uint32_t value, void* user) {
    printf("gap: %s 0x%x\n", kind, value);   // e.g. an unimplemented D3DRS_*
}
dx8wasm_set_unhandled_callback(on_unhandled, NULL);
```
Anything it prints is a feature to add to `runtime/`. Nothing renders silently
wrong — unhandled state falls back and is counted (`dx8wasm_get_coverage`).

## Win32 shims (compatlib)

There is no `compatlib` yet — grow it driven by the linker. When a link fails on
an undefined Win32 symbol:

- **Timers** (`timeGetTime`, `QueryPerformanceCounter`, `GetTickCount`) →
  `emscripten_get_now()`.
- **File I/O** (`CreateFile`/`ReadFile`) → `fopen`/`fread` on the emscripten FS,
  or route to the Phase 1 loader.
- **Debug** (`OutputDebugStringA`) → `fprintf(stderr, ...)`.
- **Threads** → single-threaded first (stub `CreateThread` to run synchronously),
  or emscripten pthreads if the game truly needs them.
- **Registry / misc** → stub to sane defaults; most reads have a fallback path.

Add these to a new `runtime/compatlib/` as they come up. Keep them minimal and
honest — a stub that returns a plausible value is fine; a stub that silently
corrupts is not (log it, like the graphics coverage layer does).

## Reality check

This is a **foundation**, not a turnkey port. The graphics/window/input/loop
path is proven end-to-end (that's what `minigame` demonstrates). Getting a large
game to boot is mostly the Win32-shim and asset work above, done incrementally —
compile, hit an undefined symbol or a coverage gap, fill it, repeat. Start with
the game's earliest boot path (usually the menu, which is 2D `XYZRHW` UI — a
path dx8wasm already supports) and work outward.
