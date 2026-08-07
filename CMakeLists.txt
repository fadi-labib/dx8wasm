# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
project(dx8wasm CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "dx8wasm targets Emscripten only. Use: cmake --preset emscripten")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -sEXPORTED_RUNTIME_METHODS=UTF8ToString: every smoke's report_error() EM_JS
# helper reads its C-string argument via UTF8ToString, which is not in
# Emscripten's default export set. Whether that reference resolves without this
# flag depends on incidental dead-code retention (another EM_JS/GL/SDL call site
# in the same binary keeping the internal helper alive) — true for most of these
# targets today, but not guaranteed, and already false for telemetry_smoke and
# compat_d3dx_smoke: their report_error() throws `ReferenceError: UTF8ToString is
# not defined` on the one path that matters, the failure path, which is exactly
# the path that never gets exercised by a passing run. Exporting it here, once,
# for every target that links against this variable, makes every smoke's
# failure-reporting path actually reportable instead of accidentally so.
set(DX8_WEBGL_LINK -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sEXIT_RUNTIME=0 -g0
    -sEXPORTED_RUNTIME_METHODS=UTF8ToString)

add_executable(gl_smoke runtime/test/gl_smoke.cpp)
target_link_options(gl_smoke PRIVATE ${DX8_WEBGL_LINK})
set_target_properties(gl_smoke PROPERTIES SUFFIX ".js")

# SDL3 via the Emscripten contrib port. If --use-port=sdl3 is unavailable on this
# emsdk, fall back to FetchContent of SDL3 with its Emscripten backend (see risks).
set(DX8_SDL3 "--use-port=sdl3")

add_library(dx8_platform STATIC runtime/platform/platform_sdl3.cpp)
target_include_directories(dx8_platform PUBLIC runtime runtime/include)
target_compile_options(dx8_platform PRIVATE ${DX8_SDL3})

add_executable(platform_smoke runtime/test/platform_smoke.cpp)
target_include_directories(platform_smoke PRIVATE runtime)
target_link_libraries(platform_smoke PRIVATE dx8_platform)
target_compile_options(platform_smoke PRIVATE ${DX8_SDL3})
target_link_options(platform_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(platform_smoke PROPERTIES SUFFIX ".js")

add_library(dx8_d3d8webgl STATIC
  runtime/d3d8webgl/d3d8.cpp runtime/d3d8webgl/device.cpp
  runtime/graphics-ff/ff_shader.cpp runtime/coverage/coverage.cpp runtime/runtime.cpp
  runtime/telemetry/telemetry.cpp
  # opfs_bridge.cpp is a platform service and lives under runtime/platform/, but it is
  # built into THIS library rather than dx8_platform because it calls the telemetry
  # producers, which live here: putting it in dx8_platform would make the two static
  # libraries mutually dependent, and wasm-ld resolves a static archive only against
  # archives listed after it. The engine's own dx8wasm_backend target compiles all of
  # these sources into a single library, so the distinction does not exist there.
  runtime/platform/opfs_bridge.cpp)
target_include_directories(dx8_d3d8webgl PUBLIC runtime runtime/d3d8 runtime/include)
target_link_libraries(dx8_d3d8webgl PUBLIC dx8_platform)
target_compile_options(dx8_d3d8webgl PRIVATE ${DX8_SDL3})

add_executable(d3d8_smoke runtime/test/d3d8_smoke.cpp)
target_include_directories(d3d8_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(d3d8_smoke PRIVATE dx8_d3d8webgl)
target_link_options(d3d8_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(d3d8_smoke PROPERTIES SUFFIX ".js")

add_executable(honest_stubs_smoke runtime/test/honest_stubs_smoke.cpp)
target_include_directories(honest_stubs_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(honest_stubs_smoke PRIVATE dx8_d3d8webgl)
target_link_options(honest_stubs_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(honest_stubs_smoke PROPERTIES SUFFIX ".js")

add_executable(caps_query_smoke runtime/test/caps_query_smoke.cpp)
target_include_directories(caps_query_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(caps_query_smoke PRIVATE dx8_d3d8webgl)
target_link_options(caps_query_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(caps_query_smoke PROPERTIES SUFFIX ".js")

add_executable(telemetry_smoke runtime/test/telemetry_smoke.cpp)
target_include_directories(telemetry_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(telemetry_smoke PRIVATE dx8_d3d8webgl)
target_link_options(telemetry_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(telemetry_smoke PROPERTIES SUFFIX ".js")

add_executable(abi_smoke runtime/test/abi_smoke.cpp)
target_include_directories(abi_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(abi_smoke PRIVATE dx8_d3d8webgl)
target_link_options(abi_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(abi_smoke PROPERTIES SUFFIX ".js")

add_executable(lit_tex_smoke runtime/test/lit_tex_smoke.cpp)
target_include_directories(lit_tex_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(lit_tex_smoke PRIVATE dx8_d3d8webgl)
target_link_options(lit_tex_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(lit_tex_smoke PROPERTIES SUFFIX ".js")

add_executable(drawup_smoke runtime/test/drawup_smoke.cpp)
target_include_directories(drawup_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(drawup_smoke PRIVATE dx8_d3d8webgl)
target_link_options(drawup_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(drawup_smoke PROPERTIES SUFFIX ".js")

add_executable(surface_smoke runtime/test/surface_smoke.cpp)
target_include_directories(surface_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(surface_smoke PRIVATE dx8_d3d8webgl)
target_link_options(surface_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(surface_smoke PROPERTIES SUFFIX ".js")

add_executable(dxt_smoke runtime/test/dxt_smoke.cpp)
target_include_directories(dxt_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(dxt_smoke PRIVATE dx8_d3d8webgl)
target_link_options(dxt_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(dxt_smoke PROPERTIES SUFFIX ".js")

add_executable(texfmt_smoke runtime/test/texfmt_smoke.cpp)
target_include_directories(texfmt_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(texfmt_smoke PRIVATE dx8_d3d8webgl)
target_link_options(texfmt_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(texfmt_smoke PROPERTIES SUFFIX ".js")

add_executable(draw_smoke runtime/test/draw_smoke.cpp)
target_include_directories(draw_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(draw_smoke PRIVATE dx8_d3d8webgl)
target_link_options(draw_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(draw_smoke PROPERTIES SUFFIX ".js")

# The only smoke that exercises GLBuffer::Unlock's partial-range upload. Every other one locks a
# whole buffer, so they all take the glBufferData respecify branch (GT-09).
add_executable(buffer_range_smoke runtime/test/buffer_range_smoke.cpp)
target_include_directories(buffer_range_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(buffer_range_smoke PRIVATE dx8_d3d8webgl)
target_link_options(buffer_range_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(buffer_range_smoke PROPERTIES SUFFIX ".js")

# The gauges over that path: gl.buf_waste_ratio and the defect counters, asserted from a real
# emission drained in-process. Spans two once-per-second windows, so it costs ~2.2 s (GT-11:
# "validated" is not "gated" -- this is the gate).
add_executable(buffer_gauge_smoke runtime/test/buffer_gauge_smoke.cpp)
target_include_directories(buffer_gauge_smoke PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(buffer_gauge_smoke PRIVATE dx8_d3d8webgl)
target_link_options(buffer_gauge_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(buffer_gauge_smoke PROPERTIES SUFFIX ".js")

add_executable(draw_tex_smoke runtime/test/draw_tex_smoke.cpp)
target_include_directories(draw_tex_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(draw_tex_smoke PRIVATE dx8_d3d8webgl)
target_link_options(draw_tex_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(draw_tex_smoke PROPERTIES SUFFIX ".js")

add_executable(coverage_smoke runtime/test/coverage_smoke.cpp)
target_include_directories(coverage_smoke PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(coverage_smoke PRIVATE dx8_d3d8webgl)
target_link_options(coverage_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(coverage_smoke PROPERTIES SUFFIX ".js")

add_executable(accepted_states_smoke runtime/test/accepted_states_smoke.cpp)
target_include_directories(accepted_states_smoke PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(accepted_states_smoke PRIVATE dx8_d3d8webgl)
target_link_options(accepted_states_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(accepted_states_smoke PROPERTIES SUFFIX ".js")

add_executable(vertexblend_smoke runtime/test/vertexblend_smoke.cpp)
target_include_directories(vertexblend_smoke PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(vertexblend_smoke PRIVATE dx8_d3d8webgl)
target_link_options(vertexblend_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(vertexblend_smoke PROPERTIES SUFFIX ".js")

add_executable(fogmode_smoke runtime/test/fogmode_smoke.cpp)
target_include_directories(fogmode_smoke PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(fogmode_smoke PRIVATE dx8_d3d8webgl)
target_link_options(fogmode_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(fogmode_smoke PROPERTIES SUFFIX ".js")

add_executable(render_state_smoke runtime/test/render_state_smoke.cpp)
target_include_directories(render_state_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(render_state_smoke PRIVATE dx8_d3d8webgl)
target_link_options(render_state_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(render_state_smoke PROPERTIES SUFFIX ".js")

add_executable(light_smoke runtime/test/light_smoke.cpp)
target_include_directories(light_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(light_smoke PRIVATE dx8_d3d8webgl)
target_link_options(light_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(light_smoke PROPERTIES SUFFIX ".js")

add_executable(matsource_smoke runtime/test/matsource_smoke.cpp)
target_include_directories(matsource_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(matsource_smoke PRIVATE dx8_d3d8webgl)
target_link_options(matsource_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(matsource_smoke PROPERTIES SUFFIX ".js")

add_executable(normal_smoke runtime/test/normal_smoke.cpp)
target_include_directories(normal_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(normal_smoke PRIVATE dx8_d3d8webgl)
target_link_options(normal_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(normal_smoke PROPERTIES SUFFIX ".js")

add_executable(multilight_smoke runtime/test/multilight_smoke.cpp)
target_include_directories(multilight_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(multilight_smoke PRIVATE dx8_d3d8webgl)
target_link_options(multilight_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(multilight_smoke PROPERTIES SUFFIX ".js")

add_executable(point_light_smoke runtime/test/point_light_smoke.cpp)
target_include_directories(point_light_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(point_light_smoke PRIVATE dx8_d3d8webgl)
target_link_options(point_light_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(point_light_smoke PROPERTIES SUFFIX ".js")

add_executable(spot_light_smoke runtime/test/spot_light_smoke.cpp)
target_include_directories(spot_light_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(spot_light_smoke PRIVATE dx8_d3d8webgl)
target_link_options(spot_light_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(spot_light_smoke PROPERTIES SUFFIX ".js")

add_executable(fog_smoke runtime/test/fog_smoke.cpp)
target_include_directories(fog_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(fog_smoke PRIVATE dx8_d3d8webgl)
target_link_options(fog_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(fog_smoke PROPERTIES SUFFIX ".js")

add_executable(specular_smoke runtime/test/specular_smoke.cpp)
target_include_directories(specular_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(specular_smoke PRIVATE dx8_d3d8webgl)
target_link_options(specular_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(specular_smoke PROPERTIES SUFFIX ".js")

add_executable(rhw_smoke runtime/test/rhw_smoke.cpp)
target_include_directories(rhw_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(rhw_smoke PRIVATE dx8_d3d8webgl)
target_link_options(rhw_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(rhw_smoke PROPERTIES SUFFIX ".js")

add_executable(strip_smoke runtime/test/strip_smoke.cpp)
target_include_directories(strip_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(strip_smoke PRIVATE dx8_d3d8webgl)
target_link_options(strip_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(strip_smoke PROPERTIES SUFFIX ".js")

add_executable(combiner_smoke runtime/test/combiner_smoke.cpp)
target_include_directories(combiner_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(combiner_smoke PRIVATE dx8_d3d8webgl)
target_link_options(combiner_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(combiner_smoke PROPERTIES SUFFIX ".js")

# compatlib Tier 0 (Win32 bedrock: types + timing + debug). Grows per docs/COMPATLIB.md.
add_library(dx8_compat STATIC
  runtime/compatlib/win32_time.cpp runtime/compatlib/win32_file.cpp runtime/compatlib/win32_mem.cpp
  runtime/compatlib/win32_sys.cpp runtime/compatlib/win32_registry.cpp runtime/compatlib/d3dx8math.cpp)
target_include_directories(dx8_compat PUBLIC runtime runtime/d3d8)

add_executable(compat_smoke runtime/test/compat_smoke.cpp)
target_include_directories(compat_smoke PRIVATE runtime)
target_link_libraries(compat_smoke PRIVATE dx8_compat)
target_link_options(compat_smoke PRIVATE ${DX8_WEBGL_LINK})
set_target_properties(compat_smoke PROPERTIES SUFFIX ".js")

add_executable(compat_file_smoke runtime/test/compat_file_smoke.cpp)
target_include_directories(compat_file_smoke PRIVATE runtime)
target_link_libraries(compat_file_smoke PRIVATE dx8_compat)
target_link_options(compat_file_smoke PRIVATE ${DX8_WEBGL_LINK})
set_target_properties(compat_file_smoke PROPERTIES SUFFIX ".js")

add_executable(compat_sys_smoke runtime/test/compat_sys_smoke.cpp)
target_include_directories(compat_sys_smoke PRIVATE runtime)
target_link_libraries(compat_sys_smoke PRIVATE dx8_compat)
target_link_options(compat_sys_smoke PRIVATE ${DX8_WEBGL_LINK})
set_target_properties(compat_sys_smoke PROPERTIES SUFFIX ".js")

add_executable(compat_d3dx_smoke runtime/test/compat_d3dx_smoke.cpp)
target_include_directories(compat_d3dx_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(compat_d3dx_smoke PRIVATE dx8_compat)
target_link_options(compat_d3dx_smoke PRIVATE ${DX8_WEBGL_LINK})
set_target_properties(compat_d3dx_smoke PROPERTIES SUFFIX ".js")

add_executable(opfs_smoke runtime/test/opfs_smoke.cpp)
target_include_directories(opfs_smoke PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(opfs_smoke PRIVATE dx8_d3d8webgl)
target_link_options(opfs_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(opfs_smoke PROPERTIES SUFFIX ".js")

add_executable(determinism_smoke runtime/test/determinism_smoke.cpp)
target_include_directories(determinism_smoke PRIVATE runtime runtime/d3d8 runtime/include runtime/test)
target_link_libraries(determinism_smoke PRIVATE dx8_d3d8webgl)
target_link_options(determinism_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(determinism_smoke PROPERTIES SUFFIX ".js")

# Integration example: a real game plugs in exactly like this (contract + D3D8).
add_executable(minigame examples/minigame/minigame.cpp)
target_include_directories(minigame PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(minigame PRIVATE dx8_d3d8webgl)
target_link_options(minigame PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(minigame PROPERTIES SUFFIX ".js")

# Conformance probe (tool, not a smoke): emits coverage JSON for CONFORMANCE.md.
add_executable(conformance runtime/test/conformance.cpp)
target_include_directories(conformance PRIVATE runtime runtime/d3d8 runtime/include)
target_link_libraries(conformance PRIVATE dx8_d3d8webgl)
target_link_options(conformance PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(conformance PROPERTIES SUFFIX ".js")

# Interactive browser demo (not a test): keeps EXIT_RUNTIME on for the main loop.
add_executable(spin_demo runtime/demo/spin_demo.cpp)
target_include_directories(spin_demo PRIVATE runtime runtime/d3d8)
target_link_libraries(spin_demo PRIVATE dx8_d3d8webgl)
target_link_options(spin_demo PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(spin_demo PROPERTIES SUFFIX ".js")
