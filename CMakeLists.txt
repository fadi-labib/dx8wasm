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
