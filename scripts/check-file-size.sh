#!/usr/bin/env bash
#
# check-file-size.sh — enforce a per-file line-count ceiling on the C++
# library sources.
#
# Scope: src/ (library code only). test/ and examples/ are not scanned —
# test fixtures and demo scaffolding legitimately grow large and are not
# the subject of this gate.
#
# Scanned extensions: .cpp, .hpp (matches cpplint's --extensions=cpp,hpp
# and the v2.0 source convention; no .h / .cc in src/).
#
# Metric: physical line count via `wc -l` (blank and comment lines
# included). Physical lines were chosen over SLOC to avoid pulling in a
# tokeniser dependency for this gate; the per-function complexity gate
# already covers semantic density.
#
# Threshold knob:
#   FILE_LOC_MAX  maximum physical lines per file (default below)
#
# Long-term target: 500 lines, matching the per-module ceiling used by
# the sibling project under ../artistai (radon SLOC) and the natural
# break point where the remaining files already comply.
#
# Exit codes:
#   0  no violations
#   1  one or more files exceed FILE_LOC_MAX
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Enforced long-term ceiling: 500 lines (see the rationale above). Set
# via the environment to test against a tighter bar locally.
FILE_LOC_MAX="${FILE_LOC_MAX:-500}"

cd "$REPO_ROOT"

echo "check-file-size: scanning src/ with FILE_LOC_MAX=$FILE_LOC_MAX"

violations=0
# -print0/read -d '' keeps the loop safe against any future filename with
# whitespace, even though src/ today is plain ASCII.
while IFS= read -r -d '' file; do
    lines=$(wc -l < "$file" | tr -d '[:space:]')
    if [ "$lines" -gt "$FILE_LOC_MAX" ]; then
        echo "  $file: $lines lines (max $FILE_LOC_MAX)" >&2
        violations=$((violations + 1))
    fi
done < <(find src -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -gt 0 ]; then
    echo "check-file-size: FAIL — $violations file(s) exceed FILE_LOC_MAX=$FILE_LOC_MAX" >&2
    echo "  threshold lives in scripts/check-file-size.sh (FILE_LOC_MAX default)" >&2
    exit 1
fi

echo "check-file-size: PASS — no file exceeds $FILE_LOC_MAX lines"
