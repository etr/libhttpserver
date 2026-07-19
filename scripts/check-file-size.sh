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
# Metric: source lines of code (SLOC) — physical lines with blank lines,
# comment-only lines, and single-character lines (a lone `{`, `}`, `(`,
# `)`, or `;`) excluded. A dependency-free awk pass strips C/C++
# comments (`//` to end-of-line and `/* ... */` spans, including
# multi-line) and blank lines, then counts what remains. The stripper is
# string-literal aware so a `//` or `/*` inside a quoted string cannot make
# the counter drop real code. Comments are excluded deliberately: this
# codebase documents heavily, and a physical-line ceiling taxes exactly the
# documentation the project invests in. The per-function complexity gate
# already covers semantic density.
#
# Threshold knob:
#   FILE_LOC_MAX  maximum SLOC (code lines) per file (default below)
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

echo "check-file-size: scanning src/ with FILE_LOC_MAX=$FILE_LOC_MAX (SLOC; comments and blank lines excluded)"

# count_sloc FILE — echo the count of source lines (comment- and blank-only
# lines removed). A small state machine tracks block-comment and string
# state across characters so that `//` or `/*` inside a string literal is
# not mistaken for a comment (which would otherwise let the counter silently
# drop real code and mask an oversize file).
count_sloc() {
    awk '
    {
        line = $0; n = length(line); i = 1; out = ""
        while (i <= n) {
            c = substr(line, i, 1); two = substr(line, i, 2)
            if (in_block) { if (two == "*/") { in_block = 0; i += 2 } else i++; continue }
            if (in_str) {
                out = out c
                if (c == "\\") { out = out substr(line, i + 1, 1); i += 2; continue }
                if (c == "\"") in_str = 0
                i++; continue
            }
            if (two == "/*") { in_block = 1; i += 2; continue }
            if (two == "//") break            # rest of line is a comment
            if (c == "\"") { in_str = 1; out = out c; i++; continue }
            out = out c; i++
        }
        gsub(/[ \t\r]/, "", out)
        # Exclude lines that reduce to a single character — a lone brace,
        # paren, bracket, or semicolon (`{`, `}`, `(`, `)`, `;`). These are
        # pure scaffolding: they carry no logic and their count is an
        # artifact of brace style, not of how much a file actually does.
        if (length(out) > 1) count++
    }
    END { print count + 0 }
    ' "$1"
}

violations=0
# -print0/read -d '' keeps the loop safe against any future filename with
# whitespace, even though src/ today is plain ASCII.
while IFS= read -r -d '' file; do
    lines=$(count_sloc "$file")
    if [ "$lines" -gt "$FILE_LOC_MAX" ]; then
        echo "  $file: $lines SLOC (max $FILE_LOC_MAX)" >&2
        violations=$((violations + 1))
    fi
done < <(find src -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0)

if [ "$violations" -gt 0 ]; then
    echo "check-file-size: FAIL — $violations file(s) exceed FILE_LOC_MAX=$FILE_LOC_MAX" >&2
    echo "  threshold lives in scripts/check-file-size.sh (FILE_LOC_MAX default)" >&2
    exit 1
fi

echo "check-file-size: PASS — no file exceeds $FILE_LOC_MAX lines"
