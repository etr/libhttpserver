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
# FILE_LOC_MAX is currently set above the largest existing file so CI
# stays green on the day this gate lands. The long-term target is 500
# lines, matching the per-module ceiling used by the sibling project
# under ../artistai (radon SLOC) and the natural break point where the
# remaining files already comply.
#
# The bar will be ratcheted down per refactor commit as each offender
# is decomposed — same pattern used to drive CCN_MAX to 10 in
# scripts/check-complexity.sh. New files must come in well below the
# long-term target; lifting FILE_LOC_MAX is not allowed.
#
# Current offenders above the long-term 500-line target (2026-05-21):
#   src/webserver.cpp                          2673
#   src/http_request.cpp                       1175
#   src/httpserver/webserver.hpp                845
#   src/http_utils.cpp                          730
#   src/httpserver/detail/webserver_impl.hpp    674
#   src/httpserver/http_request.hpp             656
#   src/httpserver/http_utils.hpp               505
#
# Exit codes:
#   0  no violations
#   1  one or more files exceed FILE_LOC_MAX
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FILE_LOC_MAX="${FILE_LOC_MAX:-2700}"

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
