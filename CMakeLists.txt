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
