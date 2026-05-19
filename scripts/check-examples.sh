#!/usr/bin/env bash
#
# check-examples.sh — enforce TASK-040 invariants on examples/.
#
# This script is a static-source check, not a build check. It asserts that:
#
#   1. examples/hello_world.cpp exists, is at most 10 lines (by the rule
#      defined below), defines no class deriving from http_resource, and
#      registers nothing via raw pointers.
#
#   2. examples/shared_state.cpp exists, defines a class deriving from
#      http_resource, holds a std::mutex, overrides both render_get and
#      render_post, and registers itself via register_path with
#      std::make_unique.
#
# LOC counting rule (TASK-040 plan D2):
#   The LOC count is the number of non-empty, non-comment lines from the
#   first non-comment line to EOF, after stripping any leading /* ... */
#   license header. Implemented with awk below.
#
# Exits non-zero on the first violation.

set -u

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HELLO="$REPO_ROOT/examples/hello_world.cpp"
SHARED="$REPO_ROOT/examples/shared_state.cpp"

fail() {
    echo "check-examples: FAIL: $*" >&2
    exit 1
}

count_loc() {
    # Strip leading /* ... */ block(s), then count non-blank non-// lines.
    awk '
        BEGIN { in_block = 0; seen_first = 0 }
        {
            line = $0
            # Handle block comments.
            if (in_block) {
                if (line ~ /\*\//) { in_block = 0; sub(/.*\*\//, "", line) }
                else { next }
            }
            # Strip inline block-open comments that close on the same line.
            while (match(line, /\/\*[^*]*\*\//)) {
                line = substr(line, 1, RSTART-1) substr(line, RSTART+RLENGTH)
            }
            if (match(line, /\/\*/)) {
                in_block = 1
                line = substr(line, 1, RSTART-1)
            }
            # Strip line comments.
            sub(/\/\/.*/, "", line)
            # Trim whitespace.
            gsub(/^[ \t\r]+|[ \t\r]+$/, "", line)
            if (line == "") next
            count++
        }
        END { print count + 0 }
    ' "$1"
}

# ---- hello_world.cpp ----------------------------------------------------

[ -f "$HELLO" ] || fail "examples/hello_world.cpp does not exist"

loc=$(count_loc "$HELLO")
if [ "$loc" -gt 10 ]; then
    fail "examples/hello_world.cpp is $loc LOC, must be <= 10"
fi
if [ "$loc" -lt 5 ]; then
    fail "examples/hello_world.cpp is $loc LOC, suspiciously short (must be >= 5)"
fi

if grep -Eq 'class[[:space:]]+[A-Za-z_][A-Za-z_0-9]*[[:space:]]*:[[:space:]]*public[[:space:]]+[A-Za-z_:]*http_resource' "$HELLO"; then
    fail "examples/hello_world.cpp defines an http_resource subclass; the lambda form is required"
fi

# No raw-pointer registration like ws.register_path("/x", new Foo()).
if grep -Eq 'register_(path|prefix|resource)[[:space:]]*\([^,]*,[[:space:]]*new[[:space:]]+' "$HELLO"; then
    fail "examples/hello_world.cpp uses raw-pointer registration; use lambda or smart-pointer form"
fi

# ---- shared_state.cpp ---------------------------------------------------

[ -f "$SHARED" ] || fail "examples/shared_state.cpp does not exist"

grep -Eq ':[[:space:]]*public[[:space:]]+[A-Za-z_:]*http_resource' "$SHARED" \
    || fail "examples/shared_state.cpp must define a class inheriting from http_resource"

grep -q 'std::mutex' "$SHARED" \
    || fail "examples/shared_state.cpp must use std::mutex to demonstrate locking"

grep -Eq '\brender_get\b' "$SHARED" \
    || fail "examples/shared_state.cpp must override render_get"

grep -Eq '\brender_post\b' "$SHARED" \
    || fail "examples/shared_state.cpp must override render_post"

grep -Eq 'register_path[[:space:]]*\(.*std::make_unique' "$SHARED" \
    || fail "examples/shared_state.cpp must register via register_path with std::make_unique"

echo "check-examples: OK (hello_world.cpp = $loc LOC; shared_state.cpp asserted)"
exit 0
