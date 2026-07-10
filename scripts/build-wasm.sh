#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Configure + build all wasm targets. Sources the pinned emsdk first.
set -euo pipefail
source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
cd "$(dirname "$0")/.."
cmake --preset emscripten
cmake --build build/emscripten
