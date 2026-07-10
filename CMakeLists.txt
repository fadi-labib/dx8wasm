# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
project(dx8wasm CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "dx8wasm targets Emscripten only. Use: cmake --preset emscripten")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DX8_WEBGL_LINK -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sEXIT_RUNTIME=0 -g0)

add_executable(gl_smoke runtime/test/gl_smoke.cpp)
target_link_options(gl_smoke PRIVATE ${DX8_WEBGL_LINK})
set_target_properties(gl_smoke PROPERTIES SUFFIX ".js")

# SDL3 via the Emscripten contrib port. If --use-port=sdl3 is unavailable on this
# emsdk, fall back to FetchContent of SDL3 with its Emscripten backend (see risks).
set(DX8_SDL3 "--use-port=sdl3")

add_library(dx8_platform STATIC runtime/platform/platform_sdl3.cpp)
target_include_directories(dx8_platform PUBLIC runtime)
target_compile_options(dx8_platform PRIVATE ${DX8_SDL3})

add_executable(platform_smoke runtime/test/platform_smoke.cpp)
target_include_directories(platform_smoke PRIVATE runtime)
target_link_libraries(platform_smoke PRIVATE dx8_platform)
target_compile_options(platform_smoke PRIVATE ${DX8_SDL3})
target_link_options(platform_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(platform_smoke PROPERTIES SUFFIX ".js")

add_library(dx8_d3d8webgl STATIC
  runtime/d3d8webgl/d3d8.cpp runtime/d3d8webgl/device.cpp
  runtime/graphics-ff/ff_shader.cpp runtime/coverage/coverage.cpp)
target_include_directories(dx8_d3d8webgl PUBLIC runtime runtime/d3d8 runtime/include)
target_link_libraries(dx8_d3d8webgl PUBLIC dx8_platform)
target_compile_options(dx8_d3d8webgl PRIVATE ${DX8_SDL3})

add_executable(d3d8_smoke runtime/test/d3d8_smoke.cpp)
target_include_directories(d3d8_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(d3d8_smoke PRIVATE dx8_d3d8webgl)
target_link_options(d3d8_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(d3d8_smoke PROPERTIES SUFFIX ".js")

add_executable(draw_smoke runtime/test/draw_smoke.cpp)
target_include_directories(draw_smoke PRIVATE runtime runtime/d3d8)
target_link_libraries(draw_smoke PRIVATE dx8_d3d8webgl)
target_link_options(draw_smoke PRIVATE ${DX8_WEBGL_LINK} ${DX8_SDL3})
set_target_properties(draw_smoke PROPERTIES SUFFIX ".js")

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
