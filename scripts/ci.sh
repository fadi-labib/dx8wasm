#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Full guardrail run: mechanical checks + pinned toolchain + every test suite.
# Works locally today; the GitHub Actions workflow (.github/workflows/ci.yml)
# runs this same script once a remote exists.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

echo "== mechanical checks =="
bash scripts/check.sh

echo "== emscripten pin =="
command -v emcc >/dev/null 2>&1 || source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1 || true
pin=$(tr -d '[:space:]' < .emscripten-version)
have=$(emcc --version 2>/dev/null | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1 || echo none)
[ "$have" = "$pin" ] || { echo "  FAIL: emcc $have != pinned $pin (.emscripten-version)"; exit 1; }
echo "  emcc $have (pinned)"

echo "== packer selftest =="
python3 asset-tools/pack.py --selftest

echo "== web-runtime suite (gaxd, loader, onboard) =="
( cd web-runtime && npm test )

echo "== phase2 GPU smokes =="
node web-runtime/test/phase2.gpu.test.mjs

echo "ALL GREEN"
