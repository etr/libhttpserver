#!/usr/bin/env bash
#
# check-fence-balance.sh — ordered open/close verification of ``` fences.
#
# Shared helper for scripts/check-readme.sh and scripts/check-release-notes.sh.
# Replaces the old `count % 2 == 0` parity heuristic, which accepted a
# structurally-unbalanced document as long as the number of ``` lines was
# even. The classic miss is two consecutive opening fences and two closing
# fences (four lines, even) — the parity test passed it silently.
#
# This helper instead walks the document with an ordered state machine:
#   * not-in-block + a ``` line (bare OR with an info string) opens a block;
#   * in-block   + a BARE ``` line closes the block;
#   * in-block   + an info-string fence (e.g. ```cpp) is illegal — a closing
#     fence never carries an info string, so this is the two-consecutive-
#     openers signature — and fails immediately;
#   * a non-zero in_block at EOF (an unclosed block) fails.
#
# Usage:  check-fence-balance.sh <markdown-file>
# Exit: 0 = balanced; 2 = info-string fence inside an open block
#       (consecutive openers); 3 = unclosed block at EOF. Callers may
#       treat any non-zero as unbalanced.
set -euo pipefail

_fence_target="${1:?usage: check-fence-balance.sh <markdown-file>}"

awk '
    BEGIN { in_block = 0; failed = 0 }
    {
        line = $0
        gsub(/\r/, "", line)          # tolerate stray CRLF endings
        if (line ~ /^```/) {
            rest = substr(line, 4)    # everything after the opening ```
            gsub(/[ \t]+$/, "", rest) # trim trailing whitespace
            is_bare = (rest == "")
            if (in_block == 0) {
                in_block = 1          # opener (bare or info-string) — both OK
            } else if (is_bare) {
                in_block = 0          # bare fence closes the open block
            } else {
                printf("check-fence-balance: FAIL: %s:%d: info-string fence ```%s appears inside an open code block (consecutive openers / unbalanced fences)\n", FILENAME, NR, rest) > "/dev/stderr"
                failed = 1
                exit 2
            }
        }
    }
    END {
        if (failed) { exit 2 }
        if (in_block != 0) {
            printf("check-fence-balance: FAIL: %s: reached EOF with an unclosed ``` code block\n", FILENAME) > "/dev/stderr"
            exit 3
        }
    }
' "$_fence_target"
