#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# PostToolUse hook wrapper (wired in .claude/settings.json). Runs the fast
# mechanical guardrails after each Edit/Write; on failure, emits the output as
# additionalContext so the model fixes it before committing. Silent on success.
d=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
[ -n "$d" ] || exit 0
out=$(bash "$d/scripts/check.sh" 2>&1) && exit 0
jq -n --arg c "$out" '{hookSpecificOutput:{hookEventName:"PostToolUse",additionalContext:("dx8wasm guardrail scripts/check.sh FAILED after this edit — fix before committing:\n"+$c)}}'
