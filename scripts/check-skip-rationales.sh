#!/usr/bin/env bash
#
# check-skip-rationales.sh — guard against unexplained platform skips in
# the integration test suite (TASK-077).
#
# Three whole-suite or near-whole-suite skips in `test/integ/` were
# silently removing the library's Windows/Darwin portability claim from
# CI. The audit captured in `test/PORTABILITY.md` records every known
# skip site, its symptom, root cause, and restoration plan. This gate
# enforces that any future skip carries a `// reason:` comment that
# points at PORTABILITY.md (or a follow-up task) so the next reader
# isn't left guessing why a whole suite vanishes on Windows.
#
# Watched files: every `.cpp` / `.hpp` under `test/integ/`, discovered
# at runtime via `find` — the broad scan keeps new test files under the
# same gate without requiring a manual list update (same pattern as
# `check-warning-suppressions.sh`).
#
# Detection logic:
#   1. Lines matching either
#        ^#ifndef[[:space:]]+(_WINDOWS|DARWIN)
#      or
#        ^#if[[:space:]]+!defined\([[:space:]]*(_WINDOWS|DARWIN)[[:space:]]*\)
#      are candidate "skip" directives. Note the deliberate asymmetry:
#      `#ifdef _WINDOWS` / `#ifdef DARWIN` blocks (additive coverage,
#      e.g. a Windows-only smoke variant) do NOT require a `// reason:`
#      comment — those are restoring coverage, not removing it.
#   2. A candidate is compliant iff any of the 5 lines immediately
#      preceding the directive contains the substring `// reason:`
#      (case-sensitive).
#   3. Any candidate that fails the bracketing check is reported and
#      the script exits 1.
#
# Exit codes:
#   0  no violations
#   1  one or more skip directives lack a `// reason:` comment
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

WATCHED_FILES=()
while IFS= read -r f; do
    WATCHED_FILES+=("$f")
done < <(find test/integ -type f \( -name '*.cpp' -o -name '*.hpp' \) 2>/dev/null | sort)

echo "check-skip-rationales: scanning ${#WATCHED_FILES[@]} file(s) under test/integ/"

if [ "${#WATCHED_FILES[@]}" -eq 0 ]; then
    echo "check-skip-rationales: PASS — no test/integ/ files found"
    exit 0
fi

# Matches:
#   #ifndef _WINDOWS
#   #ifndef DARWIN
#   #if !defined(_WINDOWS)            (with or without surrounding spaces)
#   #if !defined(_WINDOWS) && ...
#   #if !defined(DARWIN) && ...
# Does NOT match:
#   #ifdef  _WINDOWS                  (additive coverage)
#   #define _WINDOWS                  (synthetic macro for testing)
SKIP_REGEX='^#ifndef[[:space:]]+(_WINDOWS|DARWIN)([[:space:]]|$)|^#if[[:space:]]+!defined\([[:space:]]*(_WINDOWS|DARWIN)[[:space:]]*\)'

violations=0
for file in "${WATCHED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo "  $file: missing — watched file no longer present" >&2
        violations=$((violations + 1))
        continue
    fi

    while IFS=: read -r lineno directive; do
        # Look at the 5 lines immediately preceding the directive for a
        # `// reason:` substring. `awk` keeps this single-pass and
        # avoids spawning `sed`/`head`/`tail` per directive.
        has_reason=$(awk -v target="$lineno" '
            NR >= target - 5 && NR < target {
                if (index($0, "// reason:") > 0) { found = 1 }
            }
            NR >= target { exit }
            END { print (found ? 1 : 0) }
        ' "$file")

        if [ "$has_reason" != "1" ]; then
            # Trim leading whitespace from the directive for the report.
            trimmed_directive=$(printf '%s' "${directive}" | sed 's/^[[:space:]]*//')
            echo "  $file:$lineno: missing // reason: comment for '$trimmed_directive'" >&2
            violations=$((violations + 1))
        fi
    done < <(grep -nE "$SKIP_REGEX" "$file" || true)
done

if [ "$violations" -gt 0 ]; then
    echo "check-skip-rationales: FAIL — $violations skip directive(s) without // reason: comment" >&2
    echo "  add a '// reason: ...' line within the 5 lines preceding the directive that names the platform limitation" >&2
    echo "  and points at test/PORTABILITY.md (or a follow-up task)" >&2
    exit 1
fi

echo "check-skip-rationales: PASS — every skip directive carries a // reason: comment"
