#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Fast mechanical guardrails (no build, no tests) — the checks that caught real
# mistakes this project has already made:
#   1. SPDX headers present and non-conflicting on every source file
#   2. vendored binaries stay tracked (a `*.wasm` gitignore once swallowed one)
#   3. commit hygiene: correct author, no AI co-author trailers
#   4. no 32-bit truncation of emscripten_get_now()/emscripten_performance_now()
#      (a silently frozen clock)
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

echo "[4/4] no 32-bit truncation of emscripten_get_now()/emscripten_performance_now()"
# Under -pthread, emscripten_get_now() is `performance.timeOrigin + performance.now()`
# (~1.7e12 ms) so that all pthreads share one time base. Casting that double straight
# to a 32-bit integer is a float->i32 conversion of an out-of-range value, and wasm's
# non-trapping fptoui SATURATES instead of wrapping: the expression becomes a constant
# 0xFFFFFFFF. Nothing warns, nothing traps, and the clock is simply frozen — which is
# how the telemetry pump's rate limit latched shut and delivered nothing for an entire
# session, and how compatlib's timeGetTime()/GetTickCount() stood still.
# emscripten_performance_now() (plain performance.now()) is thread-relative and only
# stays in range for ~49 days of thread uptime — the same saturating cast, just with a
# longer fuse, so it is covered by the same rule. Widen to 64-bit first
# (`(DWORD)(uint64_t)...`) if you genuinely want Win32 wrap semantics, or keep the
# value in a double/uint64_t.
#
# HAZARD: this check greps CODE, not tokens the compiler sees — it will happily match
# prose in a comment that happens to contain "(uint32_t)" and "emscripten_get_now" on
# the same logical line. It used to be true only by accident that no comment tripped
# this (one comment's line break saved it — see runtime/coverage/coverage.cpp's now_ms()
# comment, which discusses the exact cast this rule forbids). Comments are stripped
# below before matching specifically so a future reflow of prose cannot produce a false
# FAIL; if this check ever starts failing on a comment, that stripping has a gap — fix
# the stripping, don't loosen the pattern.
#
# Known residual gaps (grep cannot see these; not attempted here):
#   - the two-step form: `double t = emscripten_get_now(); uint32_t u = t;` — no cast
#     token appears on the truncating line.
#   - implicit narrowing on assignment/return: `uint32_t now(){ return
#     emscripten_get_now(); }` — same reason, no cast token to anchor on.
# Closing either would need a real C++ parse (e.g. clang-tidy), not a grep.
badtype='u?int(8|16|32)_t|unsigned[[:space:]]+long|unsigned([[:space:]]+int)?|long|int|DWORD|UINT32|UINT|LONG|ULONG|size_t|uintptr_t'
badfunc='emscripten_get_now|emscripten_performance_now'
# C-style cast, allowing one extra "(" between the cast and the call so
# `(uint32_t)(emscripten_get_now())` is still caught.
badcast_c="\\(([[:space:]]*($badtype)[[:space:]]*)\\)[[:space:]]*\\(?[[:space:]]*($badfunc)"
# static_cast<T>(...)
badcast_static="static_cast[[:space:]]*<[[:space:]]*($badtype)[[:space:]]*>[[:space:]]*\\([[:space:]]*($badfunc)"
badcast="($badcast_c)|($badcast_static)"
# Strip // line comments and /* */ block comments before matching, so prose can never
# trip (or hide from) this check — see the HAZARD note above. Deliberately not a real
# preprocessor: it does not understand string/char literals, which is an acceptable
# heuristic gap for a guardrail whose false-positive failure mode is "check the line
# it names", not a silent miss.
strip_comments() {
  awk '
    BEGIN { incomment = 0 }
    {
      line = $0; out = ""; i = 1; n = length(line)
      while (i <= n) {
        if (incomment) {
          if (substr(line, i, 2) == "*/") { incomment = 0; i += 2 } else { i++ }
          continue
        }
        if (substr(line, i, 2) == "/*") { incomment = 1; i += 2; continue }
        if (substr(line, i, 2) == "//") { break }
        out = out substr(line, i, 1)
        i++
      }
      print NR ":" out
    }
  ' "$1"
}
while IFS= read -r f; do
  case "$f" in build/*|*/build/*|node_modules/*|.superpowers/*|scripts/check.sh) continue ;; esac
  hits=$(strip_comments "$f" | grep -E "$badcast" || true)
  [ -n "$hits" ] && note "$f: 32-bit cast of ${badfunc//|/ or } saturates to a constant:"$'\n'"$hits"
done < <(git ls-files | grep -E "\.(cpp|cc|c|h|hpp)$")

if [ "$fail" -eq 0 ]; then echo "OK — guardrails pass"; else echo "GUARDRAILS FAILED"; fi
exit "$fail"
