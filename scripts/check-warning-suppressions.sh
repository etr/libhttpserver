#!/usr/bin/env bash
#
# check-warning-suppressions.sh — guard against file-scoped GCC warning
# suppressions in the library sources flagged by TASK-060.
#
# The audit in specs/tasks/v2-branch-gap-audit.md §1 ("HIGH — Unscoped
# warning suppressions") singled out two `#pragma GCC diagnostic
# ignored "-Warray-bounds"` directives that sat at file scope and
# silenced the warning for entire translation units. TASK-060 either
# removes them or scopes them to a `push`/`pop` block; this gate makes
# the regression visible by failing if any TU in src/ grows a new
# file-scoped suppression.
#
# Watched files: all .cpp and .hpp files under src/, discovered at
# runtime via `find`. This is safe because at the time TASK-060 landed
# no TU in src/ carried an unscoped -Warray-bounds pragma; the broad
# scan ensures that future work which introduces new TUs is also
# guarded without needing to remember to update a static list.
#
# Detection logic:
#   1. Any `#pragma GCC diagnostic ignored "-Warray-bounds"` at the
#      beginning of a line is a candidate violation.
#   2. A candidate is allowed iff it sits between a matching
#      `#pragma GCC diagnostic push` and `#pragma GCC diagnostic pop`
#      pair (push must appear earlier in the file, pop must appear
#      later). Conditional compilation around the push/pop is fine —
#      we only care about the textual ordering.
#   3. Any candidate that fails the push/pop bracketing check is
#      reported and the script exits 1.
#
# Known limitation: the push/pop check uses "nearest push before" /
# "nearest pop after" heuristics. An interleaved pattern such as
# push@5, pop@8, pragma@10, pop@15 would be incorrectly allowed because
# push_before=5 (non-zero) and pop_after=15 (non-zero). This shape is
# not present in the codebase and is extremely unlikely in practice; if
# the project ever adopts nested or interleaved push/pop patterns,
# upgrade the detection to track bracket depth with a counter.
#
# Exit codes:
#   0  no violations
#   1  one or more watched files carries a file-scoped suppression
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Discover all .cpp and .hpp files under src/ at runtime so new TUs
# are automatically included without requiring a manual list update.
# Use a while-read loop for bash 3.x / macOS compatibility (no mapfile).
WATCHED_FILES=()
while IFS= read -r f; do
    WATCHED_FILES+=("$f")
done < <(find src \( -name '*.cpp' -o -name '*.hpp' \) | sort)

echo "check-warning-suppressions: scanning ${#WATCHED_FILES[@]} file(s)"

violations=0
for file in "${WATCHED_FILES[@]}"; do
    # A file the loop just got from `find` can vanish mid-run (e.g. a
    # concurrent rebuild); that race is outside what this gate enforces.
    [ -f "$file" ] || continue

    while IFS=: read -r lineno _rest; do
        # Single-pass awk: find the nearest push before and pop after
        # the candidate line in one read of the file.
        read -r push_before pop_after < <(awk -v target="$lineno" '
            BEGIN { last_push = 0; first_pop = 0 }
            /^#pragma[[:space:]]+GCC[[:space:]]+diagnostic[[:space:]]+push/ {
                if (NR < target) last_push = NR
            }
            /^#pragma[[:space:]]+GCC[[:space:]]+diagnostic[[:space:]]+pop/ {
                if (NR > target && first_pop == 0) first_pop = NR
            }
            END { print (last_push ? last_push : 0), (first_pop ? first_pop : 0) }
        ' "$file")

        if [ "$push_before" = "0" ] || [ "$pop_after" = "0" ]; then
            echo "  $file:$lineno: file-scoped #pragma GCC diagnostic ignored \"-Warray-bounds\" (not bracketed by push/pop)" >&2
            violations=$((violations + 1))
        fi
    done < <(grep -nE '^#pragma GCC diagnostic ignored "-Warray-bounds"' "$file" || true)
done

if [ "$violations" -gt 0 ]; then
    echo "check-warning-suppressions: FAIL — $violations file-scoped suppression(s) found" >&2
    echo "  scope each pragma with #pragma GCC diagnostic push / pop and a comment naming the GCC version range" >&2
    exit 1
fi

echo "check-warning-suppressions: PASS — no file-scoped -Warray-bounds suppressions in watched files"
