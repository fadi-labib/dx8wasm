#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fast mechanical guardrails (no build, no tests) — the checks that caught real
# mistakes this project has already made:
#   1. SPDX headers present and non-conflicting on every source file
#   2. vendored binaries stay tracked (a `*.wasm` gitignore once swallowed one)
#   3. commit hygiene: correct author, no AI co-author trailers
#   4. no 32-bit truncation of emscripten_get_now() (a silently frozen clock)
# Usage: scripts/check.sh [git-log-range]   (range defaults to all history)
set -uo pipefail
cd "$(git rev-parse --show-toplevel)"
fail=0
note() { echo "  FAIL: $*"; fail=1; }

echo "[1/4] SPDX headers"
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

echo "[2/4] vendored binaries tracked"
# Any *.wasm under a vendor/ dir must be tracked despite the *.wasm gitignore rule.
while IFS= read -r f; do
  git ls-files --error-unmatch "$f" >/dev/null 2>&1 || note "$f: present but NOT tracked (gitignore likely swallowed it)"
done < <(find . -path ./node_modules -prune -o -path '*/vendor/*' -name '*.wasm' -print | sed 's|^\./||')

echo "[3/4] commit author + co-author"
range="${1:-}"
allow_email="${DX8_AUTHOR_EMAIL:-github@fadilabib.com}"
co=$(git log ${range:+"$range"} --format='%h %b' | grep -iE 'co-authored-by' || true)
[ -n "$co" ] && note "Co-Authored-By trailer found:"$'\n'"$co"
bad=$(git log ${range:+"$range"} --format='%ae' | sort -u | grep -vx "$allow_email" || true)
[ -n "$bad" ] && note "unexpected author email(s) (allowed: $allow_email): $bad"

echo "[4/4] no 32-bit truncation of emscripten_get_now()"
# Under -pthread, emscripten_get_now() is `performance.timeOrigin + performance.now()`
# (~1.7e12 ms) so that all pthreads share one time base. Casting that double straight
# to a 32-bit integer is a float->i32 conversion of an out-of-range value, and wasm's
# non-trapping fptoui SATURATES instead of wrapping: the expression becomes a constant
# 0xFFFFFFFF. Nothing warns, nothing traps, and the clock is simply frozen — which is
# how the telemetry pump's rate limit latched shut and delivered nothing for an entire
# session, and how compatlib's timeGetTime()/GetTickCount() stood still. Widen to
# 64-bit first (`(DWORD)(uint64_t)...`) if you genuinely want Win32 wrap semantics, or
# keep the value in a double/uint64_t.
badcast='\(([[:space:]]*(u?int(8|16|32)_t|unsigned([[:space:]]+int)?|int|DWORD|UINT|LONG|ULONG)[[:space:]]*)\)[[:space:]]*emscripten_get_now'
while IFS= read -r f; do
  case "$f" in build/*|*/build/*|node_modules/*|.superpowers/*|scripts/check.sh) continue ;; esac
  hits=$(grep -nE "$badcast" "$f" || true)
  [ -n "$hits" ] && note "$f: 32-bit cast of emscripten_get_now() saturates to a constant:"$'\n'"$hits"
done < <(git ls-files | grep -E "\.(cpp|cc|c|h|hpp)$")

if [ "$fail" -eq 0 ]; then echo "OK — guardrails pass"; else echo "GUARDRAILS FAILED"; fi
exit "$fail"
