#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Configure + build all wasm targets. Uses emcc from PATH if present (CI's
# setup-emsdk), else sources the local emsdk.
set -euo pipefail
command -v emcc >/dev/null 2>&1 || source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
cd "$(dirname "$0")/.."
cmake --preset emscripten
cmake --build build/emscripten
