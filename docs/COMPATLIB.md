# compatlib requirements map

The Win32 surface a real DX8 game (target: C&C Generals) needs beyond D3D8, in
**build order** — each tier unblocks the next stage of boot. Derived from the
symbols the reference port shims (studied for *what* is called, not *how* — the
implementations below are the standard Win32→POSIX/emscripten mappings, to be
clean-room written under `runtime/compatlib/`).

This is the game-side integration work `docs/INTEGRATION.md` refers to. Build a
tier, hit the next wall of undefined symbols, build the next tier. Stub loudly
(log + plausible default), never silently wrong — same rule as the graphics
coverage layer.

## Tier 0 — bedrock (needed on the first frame)
| Area | Symbols | Maps to |
|------|---------|---------|
| Types | `DWORD HANDLE BOOL LARGE_INTEGER HRESULT …` | one umbrella `windows.h`; reconcile with the types already in `d3d8.h` (same `using` aliases are legal; struct types guard-once) |
| Timing | `timeGetTime GetTickCount QueryPerformanceCounter/Frequency Sleep timeBeginPeriod GetLocalTime` | `emscripten_get_now()` (ms); QPF a fixed 1e6; `Sleep` a no-op in the single-threaded loop |
| Debug | `OutputDebugStringA/W` | `fprintf(stderr, …)` |

Tier 0 is small, universal, and conflict-light — the natural first `compatlib`
commit, with a self-test asserting monotonic time and QPF > 0.

## Tier 1 — assets + config (boot reads data)
| Area | Symbols | Maps to |
|------|---------|---------|
| File I/O | `CreateFile ReadFile WriteFile CloseHandle SetFilePointer GetFileSize GetFileAttributes` | `fopen/fread/…` on the emscripten FS, or route to the Phase 1 GAXD/OPFS loader |
| Directory | `FindFirstFile FindNextFile FindClose CreateDirectory GetCurrentDirectory SetCurrentDirectory` | POSIX `opendir/readdir`, `mkdir`, `getcwd/chdir` |
| Shell paths | `SHGetSpecialFolderLocation SHGetPathFromIDList` | fixed virtual paths (a `/userdata` home) |
| Memory | `GlobalAlloc GlobalFree GlobalSize GlobalMemoryStatus` | `malloc/free`; report a plausible fixed RAM size |

## Tier 2 — process/runtime plumbing
| Area | Symbols | Maps to |
|------|---------|---------|
| Modules | `LoadLibrary GetProcAddress FreeLibrary GetModuleFileName` | statically linked → return non-null tokens; `GetProcAddress` resolves a known table or returns null (loudly) |
| Threads | `CreateThread GetCurrentThreadId TerminateThread` | single-threaded first (run the "thread" body synchronously or defer), or emscripten pthreads if truly needed |
| Registry | `RegOpenKeyEx RegCreateKeyEx RegQueryValueEx RegSetValueEx RegCloseKey` | in-memory key/value map persisted to OPFS/localStorage; Generals reads a handful of settings |

## Tier 3 — input + D3DX helper library
| Area | Symbols | Maps to |
|------|---------|---------|
| Keyboard/DInput | `dinput.h`, `GetKeyboardLayout` | **`dx8wasm_pump`** — the input seam already exists; adapt the game's DInput layer onto `dx8wasm_input` |
| D3DX math | `D3DXMatrixMultiply/Inverse/Transpose/Rotation*/Scaling/Translation D3DXVec3/4Transform` | trivial pure math — implement directly (row-major, matching `d3d8.h`'s `D3DMATRIX`) |
| D3DX textures | `D3DXCreateTexture D3DXCreateTextureFromFileExA D3DXFilterTexture D3DXCreateCube/VolumeTexture` | `IDirect3DDevice8::CreateTexture` + an image decoder (PNG/TGA/DDS→RGBA); cube/volume are `dx8wasm_has_cap` = 0 today |
| D3DX shaders | `D3DXAssembleShader(FromFileA)` | the SM1.x `.vso/.pso` path (only ~8 in Generals) — a later graphics slice |

## Tier 4 — deferred (not boot-critical)
| Area | Symbols | Notes |
|------|---------|-------|
| Sockets | `WSAStartup WSACleanup WSAGetLastError` + BSD sockets | multiplayer — a WebSocket/WebRTC bridge, out of scope for single-player boot |
| GDI / windowing | `gdi_compat`, `wnd_compat` | mostly superseded by SDL + dx8wasm; stub the rest |
| COM smart pointers | `_com_ptr_t _bstr_t _variant_t` | header-only templates over `AddRef/Release`; moderate, needed where the game uses `_com_ptr_t<IDirect3D…>` |
| Video (VFW) | `vfw_compat` | intro/movie playback — low priority |

## Recommended first commit
`runtime/compatlib/` with **Tier 0** only (types umbrella + timing + debug) and a
self-test. It's the bedrock every later tier includes, it's low-conflict, and it
gets the game past its very first Win32 references — the right small, verifiable
starting slice for tomorrow.
