#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fast mechanical guardrails (no build, no tests) — the checks that caught real
# mistakes this project has already made:
#   1. SPDX headers present and non-conflicting on every source file
#   2. vendored binaries stay tracked (a `*.wasm` gitignore once swallowed one)
#   3. commit hygiene: correct author, no AI co-author trailers
# Usage: scripts/check.sh [git-log-range]   (range defaults to all history)
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
fail=0
note() { echo "  FAIL: $*"; fail=1; }

echo "[1/3] SPDX headers"
exts='cpp|cc|c|h|hpp|js|mjs|py|sh|html'
while IFS= read -r f; do
  case "$f" in vendor/*|*/vendor/*|build/*|*/build/*|node_modules/*|.superpowers/*) continue ;; esac
  # Match only real header lines — "SPDX-License-Identifier:" followed by a clean
  # license token. This excludes files (like this one) that merely reference the
  # string in code, which a naive grep -c would miscount as multiple headers.
  pat='SPDX-License-Identifier:[[:space:]]*[A-Za-z0-9.+-]+'
  ids=$(grep -oE "$pat" "$f" | sed 's/.*:[[:space:]]*//' || true)
  n=$(printf '%s\n' "$ids" | grep -c . || true)
  if [ "$n" -eq 0 ]; then
    note "$f: no SPDX-License-Identifier"
  elif [ "$(printf '%s\n' "$ids" | sort -u | grep -c .)" -gt 1 ]; then
    note "$f: conflicting SPDX identifiers ($(printf '%s\n' "$ids" | sort -u | paste -sd, -))"
  fi
done < <(git ls-files | grep -E "\.($exts)\$")

echo "[2/3] vendored binaries tracked"
# Any *.wasm under a vendor/ dir must be tracked despite the *.wasm gitignore rule.
while IFS= read -r f; do
  git ls-files --error-unmatch "$f" >/dev/null 2>&1 || note "$f: present but NOT tracked (gitignore likely swallowed it)"
done < <(find . -path ./node_modules -prune -o -path '*/vendor/*' -name '*.wasm' -print | sed 's|^\./||')

echo "[3/3] commit author + co-author"
range="${1:-}"
allow_email="${DX8_AUTHOR_EMAIL:-github@fadilabib.com}"
co=$(git log ${range:+"$range"} --format='%h %b' | grep -iE 'co-authored-by' || true)
[ -n "$co" ] && note "Co-Authored-By trailer found:"$'\n'"$co"
bad=$(git log ${range:+"$range"} --format='%ae' | sort -u | grep -vx "$allow_email" || true)
[ -n "$bad" ] && note "unexpected author email(s) (allowed: $allow_email): $bad"

if [ "$fail" -eq 0 ]; then echo "OK — guardrails pass"; else echo "GUARDRAILS FAILED"; fi
exit "$fail"
