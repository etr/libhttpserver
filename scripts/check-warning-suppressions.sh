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
# the regression visible by failing if any of the watched files grows a
# new file-scoped suppression.
#
# Watched files: kept narrow on purpose. Future tasks that fix
# additional suppression sites should add their files to WATCHED_FILES
# below; broadening the scan to all of src/ is a separate decision
# (currently other TUs in src/ legitimately carry no such pragmas, so
# the gate would still pass — but adding files here is the conscious,
# auditable choice).
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
# Exit codes:
#   0  no violations
#   1  one or more watched files carries a file-scoped suppression
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

# Files watched by this gate. Narrow on purpose; see header comment.
WATCHED_FILES=(
    "src/http_utils.cpp"
    "src/detail/ip_representation.cpp"
)

echo "check-warning-suppressions: scanning ${#WATCHED_FILES[@]} file(s)"

violations=0
for file in "${WATCHED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo "  $file: missing — watched file no longer present" >&2
        violations=$((violations + 1))
        continue
    fi

    # Each line that begins with the warning-suppression pragma is a
    # candidate. We then verify it is bracketed by push/pop.
    while IFS=: read -r lineno _; do
        # Search backward for the nearest `#pragma GCC diagnostic push`
        # before this line, and forward for the nearest `pop` after.
        push_before=$(awk -v target="$lineno" '
            /^#pragma[[:space:]]+GCC[[:space:]]+diagnostic[[:space:]]+push/ {
                if (NR < target) last = NR
            }
            END { print (last ? last : 0) }
        ' "$file")
        pop_after=$(awk -v target="$lineno" '
            /^#pragma[[:space:]]+GCC[[:space:]]+diagnostic[[:space:]]+pop/ {
                if (NR > target && first == 0) first = NR
            }
            END { print (first ? first : 0) }
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
