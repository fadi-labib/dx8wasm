#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Full guardrail run: mechanical checks + pinned toolchain + every test suite.
# The GitHub Actions workflow (.github/workflows/ci.yml) runs this same script on every push.
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

echo "== dev-server selftest =="
python3 tools/serve-https.py --selftest

echo "== harness dependencies =="
# web-runtime/node_modules/ is gitignored and this script has no install step, so on a fresh checkout
# the suite below dies with ERR_MODULE_NOT_FOUND naming an innocent test file -- four gates in.
# `npm test` runs gaxd.test.mjs first and that one imports only node built-ins plus ../gaxd.js, so it
# passes with nothing installed; the failure surfaces at loader.browser.test.mjs, which is not the
# file that is wrong. Fail here, with the command, rather than there with a stack trace.
#
# Ported from the Generals integration repo's ci.sh, which grew this check hours before this
# repo hit the identical failure. The browsers are a SECOND install: package.json floats
# ^1.48.0 and the lockfile pins a specific build, which wants a Chromium an older
# ~/.cache/ms-playwright will not have. Playwright's own launch error is explicit about that half,
# so this only guards the package half.
if [ ! -d web-runtime/node_modules/playwright ]; then
  echo "  FAIL: harness dependencies not installed. Run:"
  echo "    (cd web-runtime && npm ci && npx playwright install chromium)"
  exit 1
fi
echo "  ok"

echo "== web-runtime suite (gaxd, loader, onboard) =="
( cd web-runtime && npm test )

echo "== phase2 GPU smokes =="
node web-runtime/test/phase2.gpu.test.mjs

echo "== determinism harness =="
node scripts/determinism.mjs

echo "ALL GREEN"
